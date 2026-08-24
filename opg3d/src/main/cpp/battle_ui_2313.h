#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Restores only the 23.1.3 Armory/Loadout entry points that are rendered as
// disabled NGUI controls. The previous forced-online module intentionally
// remains separate: the device log proved that none of its connectivity gates
// runs on the gray in-battle Armory path.
//
// The primary target is UIButton.set_isEnabled(bool). Calls are virtual in
// this build (there are no direct BL sites), so the implementation itself is
// hooked. A button is restored only when either:
//   * its GameObject name contains Armory, Arsenal, or Loadout; or
//   * the same GameObject was observed hosting PGCompany.UI.UIGotoArmory.
// Every unrelated NGUI button keeps the stock enabled value.
//
// Rilisoft.ButtonHandler has a second enable surface and receives the same
// narrow filter. The bounded diagnostics retain the first disabled object
// names so a future prefab rename can be mapped without globally enabling the
// whole interface.
namespace battle_ui_2313 {
namespace detail {

using MethodInfo = void;
using InstanceVoidFn = void (*)(void*, const MethodInfo*);
using InstanceBoolFn = void (*)(void*, bool, const MethodInfo*);
using GetGameObjectFn = void* (*)(void*, const MethodInfo*);
using GetNameFn = void* (*)(void*, const MethodInfo*);
using GetComponentByNameFn = void* (*)(void*, void*, const MethodInfo*);

inline constexpr const char* kUiNs = "PGCompany.UI";
inline constexpr const char* kUIGotoArmory = "UIGotoArmory";
inline constexpr const char* kCaptureMethod =
    u8"\u4E1D\u4E0A\u4E09\u4E09\u4E0A\u4E08\u4E16\u4E0B\u4E1E";
inline constexpr const char* kButtonHandlerEnable =
    u8"\u4E07\u4E1E\u4E0B\u4E02\u4E0F\u4E13\u4E12\u4E0E\u4E1A";
inline constexpr size_t kRememberedArmoryObjects = 8u;
inline constexpr uint32_t kDisabledDiagnosticLimit = 48u;
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
inline InstanceBoolFn g_button_handler_set_enabled = nullptr;
inline InstanceVoidFn g_armory_capture = nullptr;
inline InstanceVoidFn g_armory_click = nullptr;

inline std::array<std::atomic<void*>, kRememberedArmoryObjects>
    g_armory_game_objects{};
inline std::atomic<uint32_t> g_capture_cursor{0u};
inline std::atomic<uint32_t> g_disabled_calls{0u};
inline std::atomic<uint32_t> g_restored_calls{0u};
inline std::atomic<uint32_t> g_clicks{0u};
inline std::atomic<uint32_t> g_eager_repairs{0u};

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
           // The latest 23.1.3 device log shows that this legacy prefab is the
           // only UIButton continuously driven disabled during the battle.
           lower == "changeteambutton";
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

bool repaired_enabled_value(void* component, bool requested,
                            const char* surface) {
    if (requested) return true;

    const std::string name = game_object_name(component);
    const bool target = is_armory_control(component, name);
    const uint32_t disabled = g_disabled_calls.fetch_add(1u) + 1u;
    if (disabled <= kDisabledDiagnosticLimit) {
        LOGI("23.1.3-battle-ui: disabled %s candidate #%u object='%s' "
             "target=%d", surface, disabled, name.c_str(), target ? 1 : 0);
    }

    if (!target) return false;

    const uint32_t restored = g_restored_calls.fetch_add(1u) + 1u;
    LOGW("23.1.3-battle-ui: restored %s Armory control #%u object='%s'",
         surface, restored, name.c_str());
    return true;
}

void ui_button_set_enabled_hook(void* self, bool enabled,
                                const MethodInfo* method) {
    if (g_ui_button_set_enabled == nullptr) {
        LOGE("23.1.3-battle-ui: UIButton setter has no saved original");
        return;
    }
    g_ui_button_set_enabled(
        self, repaired_enabled_value(self, enabled, "UIButton"), method);
}

void button_handler_set_enabled_hook(void* self, bool enabled,
                                     const MethodInfo* method) {
    if (g_button_handler_set_enabled == nullptr) {
        LOGE("23.1.3-battle-ui: ButtonHandler setter has no saved original");
        return;
    }
    g_button_handler_set_enabled(
        self, repaired_enabled_value(self, enabled, "ButtonHandler"), method);
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

} // namespace detail

inline bool install_hooks(uintptr_t libil2cpp_base) {
#if defined(__ANDROID__)
    static_assert(sizeof(void*) == 8,
                  "PG3D 23.1.3 target must be arm64-v8a");
#endif
    using namespace detail;

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

    int installed = 0;
    const bool ui_button = add(
        {"", "UIButton", "set_isEnabled", 1},
        replacement(&ui_button_set_enabled_hook),
        original_slot(&g_ui_button_set_enabled), true, &installed);
    const bool button_handler = add(
        {"Rilisoft", "ButtonHandler", kButtonHandlerEnable, 1},
        replacement(&button_handler_set_enabled_hook),
        original_slot(&g_button_handler_set_enabled), false, &installed);
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
    LOGI("23.1.3-battle-ui: installed %d/4 Armory hooks "
         "(unity-api=%d eager-helper=%d ui-button=%d button-handler=%d "
         "capture=%d click=%d)",
         installed, unity_api ? 1 : 0,
         g_get_component_by_name != nullptr ? 1 : 0, ui_button ? 1 : 0,
         button_handler ? 1 : 0, capture ? 1 : 0, click ? 1 : 0);
    if (!ready) {
        LOGE("23.1.3-battle-ui: Armory restoration is incomplete");
    }
    return ready;
}

} // namespace battle_ui_2313
