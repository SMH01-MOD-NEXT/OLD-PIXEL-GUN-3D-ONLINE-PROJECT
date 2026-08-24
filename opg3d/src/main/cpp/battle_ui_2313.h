#pragma once

#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <string>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Restores the 23.1.3 in-battle Armory (Shop) and Bank controls.
//
// The first three iterations of this module chased UIButton.set_isEnabled and
// the ChangeTeamButton prefab. The device log proved both were the wrong
// target: ChangeTeamButton is the team switcher, and its caller RVA resolves
// to the IL2CPP runtime invoke thunk, so it never identified a real driver.
//
// The real driver was found by resolving the caller RVAs that the SetState
// proxy printed for the two buttons that are actually gray in battle:
//
//   caller-rva 0x02E74044  ShopButton
//   caller-rva 0x02E740EC  BankButton
//     both inside  Rilisoft.BankShopViewGuiElement.\u4E0A\u4E13\u4E05\u4E11\u4E18\u4E1F\u4E19\u4E1C\u4E0E()
//     method start RVA 0x02E738B8
//
// Disassembling that method shows one deliberate "retire the shelf" block per
// button, and it is not a paint-only change:
//
//   2e73fd0  tbz  w0, #0, 0x2e74044          ; skip the whole block
//   2e73fdc  bl   BankShopView.get_ShopButtonScript
//   2e73fe8  bl   UnityEngine.Behaviour.get_enabled
//   2e73fec  tbz  w0, #0, 0x2e74044          ; already off, nothing to do
//   2e74008  bl   UnityEngine.Behaviour.set_enabled   (w1 = 0  -> false)
//   2e74024  bl   UnityEngine.Collider.set_enabled    (w1 = 0  -> false)
//   2e74040  bl   UIButtonColor.set_state             (w1 = 3  -> Disabled)
//   2e74044  ...  the identical sequence for BankButton
//
// So the button is switched off on three surfaces at once: the NGUI script,
// its BoxCollider (which is why the control also stops receiving taps) and the
// gray Disabled presentation. Intercepting only isEnabled or only SetState can
// never restore it, which is exactly what the previous builds observed.
//
// The condition in w0 is derived from a static flag this build reads at
// [statics + 0xB8] + 0x700 - a retired-service verdict that nothing on device
// can satisfy any more. Rather than fight that flag (it is shared with paths
// that must keep their stock behaviour), this module lets the stock body run
// and then puts the two controls back exactly the way the stock enable path
// would have: script enabled, collider enabled, presentation Normal. Every
// step goes through the game's own property getters and Unity setters, so no
// field layout is assumed and no unrelated button is touched.
//
// The narrow name filter on the isEnabled/SetState proxies is kept as a second
// line of defence and now matches the two prefabs the log actually named.
// ChangeTeamButton was removed: it is a different control and it only produced
// log noise.
namespace battle_ui_2313 {
namespace detail {

using MethodInfo = void;
using InstanceVoidFn = void (*)(void*, const MethodInfo*);
using InstanceBoolFn = void (*)(void*, bool, const MethodInfo*);
// UIButtonColor.SetState(State, bool): the managed enum is a plain int32 in
// this ABI, and this is the single surface every NGUI button uses to apply the
// gray "Disabled" presentation (disabledColor plus disabledSprite).
using InstanceStateFn = void (*)(void*, int32_t, bool, const MethodInfo*);
// UIButtonColor.set_state(State): the property setter the retiring block calls.
using SetStateFn = void (*)(void*, int32_t, const MethodInfo*);
using GetObjectFn = void* (*)(void*, const MethodInfo*);
using GetGameObjectFn = void* (*)(void*, const MethodInfo*);
using GetNameFn = void* (*)(void*, const MethodInfo*);
using GetComponentByNameFn = void* (*)(void*, void*, const MethodInfo*);

// UIButtonColor.State values (dump2313.cs, TypeDefIndex 1202).
inline constexpr int32_t kStateNormal = 0;
inline constexpr int32_t kStateDisabled = 3;

inline constexpr const char* kUiNs = "PGCompany.UI";
inline constexpr const char* kUIGotoArmory = "UIGotoArmory";
inline constexpr const char* kCaptureMethod =
    u8"\u4E1D\u4E0A\u4E09\u4E09\u4E0A\u4E08\u4E16\u4E0B\u4E1E";
inline constexpr const char* kButtonHandlerEnable =
    u8"\u4E07\u4E1E\u4E0B\u4E02\u4E0F\u4E13\u4E12\u4E0E\u4E1A";

// Rilisoft.BankShopViewGuiElement (dump2313.cs, TypeDefIndex 7931).
inline constexpr const char* kRilisoftNs = "Rilisoft";
inline constexpr const char* kBankShopElement = "BankShopViewGuiElement";
inline constexpr const char* kBankShopView = "BankShopView";
// BankShopViewGuiElement.\u4E0A\u4E13\u4E05\u4E11\u4E18\u4E1F\u4E19\u4E1C\u4E0E() - RVA 0x02E738B8, the refresh that
// retires both shelves.
inline constexpr const char* kBankShopRefresh =
    u8"\u4E0A\u4E13\u4E05\u4E11\u4E18\u4E1F\u4E19\u4E1C\u4E0E";
// BankShopViewGuiElement.\u4E11\u4E02\u4E1C\u4E08\u4E19\u4E1A\u4E1C\u4E1C\u4E04() - RVA 0x02E736BC, the compiler
// generated getter for the <BankShopView>k__BackingField.
inline constexpr const char* kBankShopViewGetter =
    u8"\u4E11\u4E02\u4E1C\u4E08\u4E19\u4E1A\u4E1C\u4E1C\u4E04";

inline constexpr size_t kRememberedArmoryObjects = 8u;
inline constexpr uint32_t kDisabledDiagnosticLimit = 48u;
// The refresh runs on every currency change, so its diagnostics are printed
// for the first few calls and then only periodically.
inline constexpr uint32_t kRepairDiagnosticBurst = 6u;
inline constexpr uint32_t kRepairDiagnosticPeriod = 240u;
// Component.GetComponent(string). Metadata lookup cannot select this overload
// from GetComponent(Type) because both have arity 1, so the exact supplied
// 23.1.3 RVA is used for the eager post-setup repair only.
inline constexpr uintptr_t kComponentGetComponentStringRva = 0x443709Cu;

inline GetGameObjectFn g_get_game_object = nullptr;
inline const MethodInfo* g_mi_get_game_object = nullptr;
inline GetNameFn g_get_name = nullptr;
inline const MethodInfo* g_mi_get_name = nullptr;
inline GetComponentByNameFn g_get_component_by_name = nullptr;
inline const MethodInfo* g_mi_ui_button_set_enabled = nullptr;
inline const MethodInfo* g_mi_button_handler_set_enabled = nullptr;

inline InstanceBoolFn g_ui_button_set_enabled = nullptr;
inline InstanceBoolFn g_button_color_set_enabled = nullptr;
inline InstanceBoolFn g_button_handler_set_enabled = nullptr;
inline InstanceStateFn g_ui_button_set_state = nullptr;
inline InstanceStateFn g_button_color_set_state = nullptr;
inline InstanceVoidFn g_armory_capture = nullptr;
inline InstanceVoidFn g_armory_click = nullptr;
inline InstanceVoidFn g_bank_shop_refresh = nullptr;
inline uintptr_t g_il2cpp_base = 0u;

inline std::array<std::atomic<void*>, kRememberedArmoryObjects>
    g_armory_game_objects{};
inline std::atomic<uint32_t> g_capture_cursor{0u};
inline std::atomic<uint32_t> g_disabled_calls{0u};
inline std::atomic<uint32_t> g_restored_calls{0u};
inline std::atomic<uint32_t> g_state_calls{0u};
inline std::atomic<uint32_t> g_state_restored{0u};
inline std::atomic<uint32_t> g_clicks{0u};
inline std::atomic<uint32_t> g_eager_repairs{0u};
inline std::atomic<uint32_t> g_shelf_repairs{0u};

// A resolved managed method: body pointer plus the MethodInfo* the managed ABI
// expects as the trailing argument.
struct ManagedCall {
    void* fn = nullptr;
    const MethodInfo* mi = nullptr;
    explicit operator bool() const noexcept { return fn != nullptr; }
};

inline ManagedCall g_view_of_element{};
inline ManagedCall g_shop_script{};
inline ManagedCall g_shop_collider{};
inline ManagedCall g_bank_script{};
inline ManagedCall g_bank_collider{};
inline ManagedCall g_behaviour_set_enabled{};
inline ManagedCall g_collider_set_enabled{};
inline ManagedCall g_button_color_state_setter{};

template <typename Fn>
void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

bool resolve_call(const hook::ManagedMethod& target, void** out_fn,
                  const MethodInfo** out_mi) {
    void* info = il2cpp::find_method_info(target.namespaze, target.klass,
                                          target.method, target.args_count);
    void* pointer = info != nullptr ? il2cpp::method_pointer(info) : nullptr;
    if (info == nullptr || pointer == nullptr) {
        LOGE("23.1.3-battle-ui: cannot resolve %s.%s/%d", target.klass,
             target.method, target.args_count);
        return false;
    }
    *out_fn = pointer;
    *out_mi = info;
    return true;
}

bool bind_call(ManagedCall& out, const char* namespaze, const char* klass,
               const char* method, int args_count) {
    void* info =
        il2cpp::find_method_info(namespaze, klass, method, args_count);
    void* pointer = info != nullptr ? il2cpp::method_pointer(info) : nullptr;
    if (pointer == nullptr) {
        LOGE("23.1.3-battle-ui: cannot resolve %s.%s/%d", klass, method,
             args_count);
        return false;
    }
    out.fn = pointer;
    out.mi = reinterpret_cast<const MethodInfo*>(info);
    return true;
}

void* invoke_getter(const ManagedCall& call, void* self) {
    if (!call || self == nullptr) return nullptr;
    return reinterpret_cast<GetObjectFn>(call.fn)(self, call.mi);
}

void* game_object_of(void* component) {
    if (component == nullptr || g_get_game_object == nullptr ||
        g_mi_get_game_object == nullptr) {
        return nullptr;
    }
    return g_get_game_object(component, g_mi_get_game_object);
}

std::string game_object_name(void* component) {
    void* game_object = game_object_of(component);
    if (game_object == nullptr || g_get_name == nullptr ||
        g_mi_get_name == nullptr) {
        return "<unnamed>";
    }
    void* managed = g_get_name(game_object, g_mi_get_name);
    std::string name = il2cpp::to_utf8(managed, 96u);
    return name.empty() ? "<unnamed>" : name;
}

std::string ascii_lower(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

bool target_name(const std::string& name) {
    const std::string lower = ascii_lower(name);
    return lower.find("armory") != std::string::npos ||
           lower.find("arsenal") != std::string::npos ||
           lower.find("loadout") != std::string::npos ||
           // The two prefabs the 23.1.3 device log named as grayed in battle.
           // Rilisoft.BankShopView hosts them: ShopButton is the Armory entry
           // point, BankButton its currency sibling.
           lower == "shopbutton" || lower == "bankbutton";
}

void remember_armory_object(void* game_object) {
    if (game_object == nullptr) return;
    const uint32_t cursor = g_capture_cursor.fetch_add(1u);
    g_armory_game_objects[cursor % kRememberedArmoryObjects].store(
        game_object, std::memory_order_release);
}

bool is_remembered_armory_object(void* game_object) {
    if (game_object == nullptr) return false;
    for (const auto& remembered : g_armory_game_objects) {
        if (remembered.load(std::memory_order_acquire) == game_object) {
            return true;
        }
    }
    return false;
}

bool is_armory_control(void* component, const std::string& name) {
    return target_name(name) ||
           is_remembered_armory_object(game_object_of(component));
}

void eagerly_enable_attached_controls(void* armory_component) {
    if (armory_component == nullptr || g_get_component_by_name == nullptr ||
        il2cpp::string_new == nullptr) {
        return;
    }

    bool repaired = false;
    void* ui_button_name = il2cpp::string_new("UIButton");
    if (ui_button_name != nullptr && g_ui_button_set_enabled != nullptr) {
        void* button = g_get_component_by_name(armory_component,
                                               ui_button_name, nullptr);
        if (button != nullptr) {
            g_ui_button_set_enabled(button, true,
                                    g_mi_ui_button_set_enabled);
            repaired = true;
        }
    }

    void* handler_name = il2cpp::string_new("Rilisoft.ButtonHandler");
    if (handler_name != nullptr && g_button_handler_set_enabled != nullptr) {
        void* handler = g_get_component_by_name(armory_component,
                                                handler_name, nullptr);
        if (handler != nullptr) {
            g_button_handler_set_enabled(handler, true,
                                         g_mi_button_handler_set_enabled);
            repaired = true;
        }
    }

    const uint32_t count = g_eager_repairs.fetch_add(1u) + 1u;
    LOGI("23.1.3-battle-ui: UIGotoArmory eager repair #%u attached-control=%s",
         count, repaired ? "enabled" : "not-found (name filter remains armed)");
}

// Managed callers reach the NGUI setters through the virtual slot, so the
// image contains no direct BL site and the driving controller can only be
// identified from the live return address. This converts it into the RVA that
// tools/resolve_rva.py understands.
uintptr_t caller_rva(const void* caller) {
    const uintptr_t pc = reinterpret_cast<uintptr_t>(caller) &
                         ~static_cast<uintptr_t>(1u);
    if (pc == 0u || g_il2cpp_base == 0u || pc < g_il2cpp_base) return 0u;
    return pc - g_il2cpp_base;
}

bool repaired_enabled_value(void* component, bool requested,
                            const char* surface, const void* caller) {
    if (requested) return true;

    const std::string name = game_object_name(component);
    const bool target = is_armory_control(component, name);
    const uint32_t disabled = g_disabled_calls.fetch_add(1u) + 1u;
    if (disabled <= kDisabledDiagnosticLimit) {
        LOGI("23.1.3-battle-ui: disabled %s candidate #%u object='%s' "
             "target=%d caller-rva=0x%08" PRIxPTR,
             surface, disabled, name.c_str(), target ? 1 : 0,
             caller_rva(caller));
    }

    if (!target) return false;

    const uint32_t restored = g_restored_calls.fetch_add(1u) + 1u;
    if (restored <= kDisabledDiagnosticLimit) {
        LOGW("23.1.3-battle-ui: restored %s Armory control #%u object='%s'",
             surface, restored, name.c_str());
    }
    return true;
}

// The gray look itself is applied here: NGUI paints disabledColor and swaps in
// disabledSprite from SetState(Disabled), and a controller can drive that
// without ever touching isEnabled. Only the narrow Armory filter is forced
// back to Normal; every other button keeps the stock state.
int32_t repaired_state_value(void* component, int32_t state,
                            const char* surface, const void* caller) {
    if (state != kStateDisabled) return state;

    const std::string name = game_object_name(component);
    const bool target = is_armory_control(component, name);
    const uint32_t seen = g_state_calls.fetch_add(1u) + 1u;
    if (seen <= kDisabledDiagnosticLimit) {
        LOGI("23.1.3-battle-ui: %s grayed candidate #%u object='%s' target=%d "
             "caller-rva=0x%08" PRIxPTR,
             surface, seen, name.c_str(), target ? 1 : 0, caller_rva(caller));
    }

    if (!target) return state;

    const uint32_t restored = g_state_restored.fetch_add(1u) + 1u;
    if (restored <= kDisabledDiagnosticLimit) {
        LOGW("23.1.3-battle-ui: repainted %s Armory control #%u object='%s' "
             "(Disabled -> Normal)",
             surface, restored, name.c_str());
    }
    return kStateNormal;
}

void ui_button_set_enabled_hook(void* self, bool enabled,
                                const MethodInfo* method) {
    const void* caller = OPG3D_RETURN_ADDRESS();
    if (g_ui_button_set_enabled == nullptr) {
        LOGE("23.1.3-battle-ui: UIButton setter has no saved original");
        return;
    }
    g_ui_button_set_enabled(
        self, repaired_enabled_value(self, enabled, "UIButton", caller),
        method);
}

// UIButton derives from UIButtonColor, so a control that carries only the base
// component (or a caller that assigns through the base slot) never reaches the
// UIButton override above.
void button_color_set_enabled_hook(void* self, bool enabled,
                                   const MethodInfo* method) {
    const void* caller = OPG3D_RETURN_ADDRESS();
    if (g_button_color_set_enabled == nullptr) {
        LOGE("23.1.3-battle-ui: UIButtonColor setter has no saved original");
        return;
    }
    g_button_color_set_enabled(
        self, repaired_enabled_value(self, enabled, "UIButtonColor", caller),
        method);
}

void ui_button_set_state_hook(void* self, int32_t state, bool immediate,
                              const MethodInfo* method) {
    const void* caller = OPG3D_RETURN_ADDRESS();
    if (g_ui_button_set_state == nullptr) {
        LOGE("23.1.3-battle-ui: UIButton.SetState has no saved original");
        return;
    }
    g_ui_button_set_state(
        self, repaired_state_value(self, state, "UIButton", caller), immediate,
        method);
}

void button_color_set_state_hook(void* self, int32_t state, bool immediate,
                                 const MethodInfo* method) {
    const void* caller = OPG3D_RETURN_ADDRESS();
    if (g_button_color_set_state == nullptr) {
        LOGE("23.1.3-battle-ui: UIButtonColor.SetState has no saved original");
        return;
    }
    g_button_color_set_state(
        self, repaired_state_value(self, state, "UIButtonColor", caller),
        immediate, method);
}

void button_handler_set_enabled_hook(void* self, bool enabled,
                                     const MethodInfo* method) {
    const void* caller = OPG3D_RETURN_ADDRESS();
    if (g_button_handler_set_enabled == nullptr) {
        LOGE("23.1.3-battle-ui: ButtonHandler setter has no saved original");
        return;
    }
    g_button_handler_set_enabled(
        self, repaired_enabled_value(self, enabled, "ButtonHandler", caller),
        method);
}

// Puts one shelf back exactly the way the stock enable path leaves it: the
// NGUI script live, its collider live and the presentation Normal. Missing
// members are skipped, so a prefab that legitimately has no collider is not a
// failure.
bool restore_shelf(void* script, void* collider) {
    bool touched = false;
    if (script != nullptr) {
        if (g_behaviour_set_enabled) {
            reinterpret_cast<InstanceBoolFn>(g_behaviour_set_enabled.fn)(
                script, true, g_behaviour_set_enabled.mi);
            touched = true;
        }
        if (g_button_color_state_setter) {
            reinterpret_cast<SetStateFn>(g_button_color_state_setter.fn)(
                script, kStateNormal, g_button_color_state_setter.mi);
            touched = true;
        }
    }
    if (collider != nullptr && g_collider_set_enabled) {
        reinterpret_cast<InstanceBoolFn>(g_collider_set_enabled.fn)(
            collider, true, g_collider_set_enabled.mi);
        touched = true;
    }
    return touched;
}

// The stock refresh is allowed to run untouched - it also drives the currency
// labels, the x3 frames and the event icons, and none of that may be skipped.
// Only the retiring decision it makes for the two buttons is undone afterwards.
void bank_shop_refresh_hook(void* self, const MethodInfo* method) {
    if (g_bank_shop_refresh != nullptr) {
        g_bank_shop_refresh(self, method);
    } else {
        LOGE("23.1.3-battle-ui: Bank/Shop refresh has no saved original");
    }

    void* view = invoke_getter(g_view_of_element, self);
    if (view == nullptr) return;

    const bool shop = restore_shelf(invoke_getter(g_shop_script, view),
                                    invoke_getter(g_shop_collider, view));
    const bool bank = restore_shelf(invoke_getter(g_bank_script, view),
                                    invoke_getter(g_bank_collider, view));
    if (!shop && !bank) return;

    const uint32_t count = g_shelf_repairs.fetch_add(1u) + 1u;
    if (count <= kRepairDiagnosticBurst ||
        (count % kRepairDiagnosticPeriod) == 0u) {
        LOGW("23.1.3-battle-ui: Bank/Shop shelf repair #%u "
             "(shop=%d bank=%d script+collider+Normal reapplied)",
             count, shop ? 1 : 0, bank ? 1 : 0);
    }
}

void armory_capture_hook(void* self, const MethodInfo* method) {
    if (g_armory_capture != nullptr) g_armory_capture(self, method);
    void* game_object = game_object_of(self);
    remember_armory_object(game_object);
    eagerly_enable_attached_controls(self);
    LOGI("23.1.3-battle-ui: captured UIGotoArmory object=%p name='%s'",
         game_object, game_object_name(self).c_str());
}

void armory_click_hook(void* self, const MethodInfo* method) {
    const uint32_t click = g_clicks.fetch_add(1u) + 1u;
    LOGI("23.1.3-battle-ui: Armory click #%u accepted object='%s'", click,
         game_object_name(self).c_str());
    if (g_armory_click != nullptr) {
        g_armory_click(self, method);
    } else {
        LOGE("23.1.3-battle-ui: UIGotoArmory click has no saved original");
    }
}

bool add(const hook::ManagedMethod& target, void* replace, void** original,
         bool required, int* installed) {
    const bool ok = hook::install(target, replace, original, required);
    if (ok) ++(*installed);
    return ok;
}

// Resolves everything the shelf repair needs. Returns false if any member is
// missing, in which case the repair stays disarmed instead of running with a
// partial view.
bool resolve_shelf_api() {
    bool ok = bind_call(g_view_of_element, kRilisoftNs, kBankShopElement,
                        kBankShopViewGetter, 0);
    ok &= bind_call(g_shop_script, kRilisoftNs, kBankShopView,
                    "get_ShopButtonScript", 0);
    ok &= bind_call(g_shop_collider, kRilisoftNs, kBankShopView,
                    "get_ShopButtonCollider", 0);
    ok &= bind_call(g_bank_script, kRilisoftNs, kBankShopView,
                    "get_BankButtonScript", 0);
    ok &= bind_call(g_bank_collider, kRilisoftNs, kBankShopView,
                    "get_BankButtonCollider", 0);
    ok &= bind_call(g_behaviour_set_enabled, "UnityEngine", "Behaviour",
                    "set_enabled", 1);
    ok &= bind_call(g_collider_set_enabled, "UnityEngine", "Collider",
                    "set_enabled", 1);
    ok &= bind_call(g_button_color_state_setter, "", "UIButtonColor",
                    "set_state", 1);
    return ok;
}

} // namespace detail

inline bool install_hooks(uintptr_t libil2cpp_base) {
#if defined(__ANDROID__)
    static_assert(sizeof(void*) == 8,
                  "PG3D 23.1.3 target must be arm64-v8a");
#endif
    using namespace detail;

    g_il2cpp_base = libil2cpp_base;
    g_get_component_by_name = libil2cpp_base != 0u
        ? reinterpret_cast<GetComponentByNameFn>(
              libil2cpp_base + kComponentGetComponentStringRva)
        : nullptr;

    bool unity_api = resolve_call(
        {"UnityEngine", "Component", "get_gameObject", 0},
        reinterpret_cast<void**>(&g_get_game_object), &g_mi_get_game_object);
    unity_api &= resolve_call(
        {"UnityEngine", "Object", "get_name", 0},
        reinterpret_cast<void**>(&g_get_name), &g_mi_get_name);

    g_mi_ui_button_set_enabled = reinterpret_cast<const MethodInfo*>(
        il2cpp::find_method_info("", "UIButton", "set_isEnabled", 1));
    g_mi_button_handler_set_enabled = reinterpret_cast<const MethodInfo*>(
        il2cpp::find_method_info("Rilisoft", "ButtonHandler",
                                 kButtonHandlerEnable, 1));

    const bool shelf_api = resolve_shelf_api();

    int installed = 0;
    const bool ui_button = add(
        {"", "UIButton", "set_isEnabled", 1},
        replacement(&ui_button_set_enabled_hook),
        original_slot(&g_ui_button_set_enabled), true, &installed);
    const bool button_color = add(
        {"", "UIButtonColor", "set_isEnabled", 1},
        replacement(&button_color_set_enabled_hook),
        original_slot(&g_button_color_set_enabled), false, &installed);
    // SetState is the only surface that paints disabledColor and swaps in
    // disabledSprite, so a control can be grayed with isEnabled untouched.
    const bool ui_button_state = add(
        {"", "UIButton", "SetState", 2},
        replacement(&ui_button_set_state_hook),
        original_slot(&g_ui_button_set_state), false, &installed);
    const bool button_color_state = add(
        {"", "UIButtonColor", "SetState", 2},
        replacement(&button_color_set_state_hook),
        original_slot(&g_button_color_set_state), false, &installed);
    const bool button_handler = add(
        {"Rilisoft", "ButtonHandler", kButtonHandlerEnable, 1},
        replacement(&button_handler_set_enabled_hook),
        original_slot(&g_button_handler_set_enabled), false, &installed);
    // The in-battle gray Armory is decided here, on three surfaces at once.
    const bool shelf = shelf_api && add(
        {kRilisoftNs, kBankShopElement, kBankShopRefresh, 0},
        replacement(&bank_shop_refresh_hook),
        original_slot(&g_bank_shop_refresh), false, &installed);
    const bool capture = add(
        {kUiNs, kUIGotoArmory, kCaptureMethod, 0},
        replacement(&armory_capture_hook), original_slot(&g_armory_capture),
        true, &installed);
    const bool click = add(
        {kUiNs, kUIGotoArmory, "U_Click", 0},
        replacement(&armory_click_hook), original_slot(&g_armory_click), false,
        &installed);

    const bool ready = unity_api && g_get_component_by_name != nullptr &&
                       ui_button && capture;
    LOGI("23.1.3-battle-ui: installed %d/8 Armory hooks "
         "(unity-api=%d eager-helper=%d ui-button=%d button-color=%d "
         "state=%d/%d button-handler=%d shelf-api=%d shelf-repair=%d "
         "capture=%d click=%d)",
         installed, unity_api ? 1 : 0,
         g_get_component_by_name != nullptr ? 1 : 0, ui_button ? 1 : 0,
         button_color ? 1 : 0, ui_button_state ? 1 : 0,
         button_color_state ? 1 : 0, button_handler ? 1 : 0,
         shelf_api ? 1 : 0, shelf ? 1 : 0, capture ? 1 : 0, click ? 1 : 0);
    if (!shelf) {
        LOGE("23.1.3-battle-ui: the Bank/Shop shelf repair is NOT armed; the "
             "in-battle Armory button will stay gray");
    }
    if (!ready) {
        LOGE("23.1.3-battle-ui: Armory restoration is incomplete");
    }
    return ready;
}

} // namespace battle_ui_2313
