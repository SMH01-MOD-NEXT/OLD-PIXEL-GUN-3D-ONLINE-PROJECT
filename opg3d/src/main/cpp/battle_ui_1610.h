#pragma once

#include <atomic>
#include <cinttypes>
#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Narrow UI bridge for the supplied obfuscated PG3D 16.1.1 ARMv7 build.
//
// The retired backend is bypassed through the game's stock offline-auth route.
// OfflineModController then disables a curated group of backend-dependent
// UIButtons, including MainMenuController.multiplayerButton. This module keeps
// that stock gate intact and overrides only the setter call for the UIButton
// whose GameObject is exactly multiplayerButton. All other buttons and all
// non-offline-gate calls to UIButton.set_isEnabled remain stock.
//
// Some offline-auth lobby instances have no working multiplayer EventDelegate:
// the UIButton visibly presses, but MainMenuController never receives the
// click. The target UIButton.OnClick hook first preserves the stock delegate
// list, then dispatches the stock OnClickMultiplyerButtonCore(true) only when
// that list did not call it. If the core returns before GoMulty, the same click
// falls back to the stock GoMulty method. No scene or matchmaking logic is
// reimplemented here.
namespace battle_ui_1610 {
namespace detail {

using MethodInfo = void;
using InstanceVoidFn = void (*)(void* self, const MethodInfo* method);
using InstanceBoolFn = void (*)(void* self, bool value,
                                const MethodInfo* method);
using ComponentGameObjectFn = void* (*)(void* self,
                                        const MethodInfo* method);

inline constexpr const char* kOfflineButtonGateMethod =
    u8"丁丅三七丆丙丛不丈";
inline constexpr const char* kMainMenuInstanceField =
    u8"丕丘丒业东丂丑下丟";
inline constexpr const char* kGoMultyMethod =
    u8"丏丛一丐世丂丌万丛";

inline InstanceBoolFn g_offline_button_gate = nullptr;
inline InstanceBoolFn g_button_set_enabled = nullptr;
inline InstanceVoidFn g_button_on_click = nullptr;
inline InstanceBoolFn g_multiplayer_click_core = nullptr;
inline InstanceVoidFn g_go_multy = nullptr;
inline ComponentGameObjectFn g_component_game_object = nullptr;
inline const MethodInfo* g_component_game_object_info = nullptr;
inline const MethodInfo* g_multiplayer_click_core_info = nullptr;
inline const MethodInfo* g_go_multy_info = nullptr;
inline void* g_main_menu_instance_field = nullptr;
inline void* g_multiplayer_button_field = nullptr;

inline thread_local bool g_inside_offline_button_gate = false;
inline thread_local bool g_inside_target_click = false;
inline thread_local bool g_click_core_seen = false;
inline thread_local bool g_go_multy_seen = false;
inline std::atomic<bool> g_restore_logged{false};
inline std::atomic<uint32_t> g_target_clicks{0u};

bool resolve_call(const char* namespaze, const char* klass,
                  const char* method_name, int args, void** function,
                  const MethodInfo** info) {
    void* method = il2cpp::find_method_info(
        namespaze, klass, method_name, args);
    void* pointer = method != nullptr ? il2cpp::method_pointer(method)
                                      : nullptr;
    if (method == nullptr || pointer == nullptr) {
        LOGE("16.1.1-battle-ui: cannot resolve %s%s%s.%s/%d", namespaze,
             namespaze[0] != '\0' ? "." : "", klass, method_name, args);
        return false;
    }
    *function = pointer;
    *info = method;
    return true;
}

bool resolve_metadata() {
    if (il2cpp::field_static_get_value == nullptr ||
        il2cpp::field_get_value == nullptr) {
        LOGE("16.1.1-battle-ui: required field API is unavailable");
        return false;
    }

    g_main_menu_instance_field = il2cpp::find_field(
        "", "MainMenuController", kMainMenuInstanceField);
    g_multiplayer_button_field = il2cpp::find_field(
        "", "MainMenuController", "multiplayerButton");

    void* component_entry = nullptr;
    bool component = resolve_call(
        "UnityEngine", "Component", "get_gameObject", 0,
        &component_entry, &g_component_game_object_info);
    g_component_game_object =
        reinterpret_cast<ComponentGameObjectFn>(component_entry);
    void* core_entry = nullptr;
    bool core = resolve_call(
        "", "MainMenuController", "OnClickMultiplyerButtonCore", 1,
        &core_entry, &g_multiplayer_click_core_info);
    void* go_entry = nullptr;
    bool go = resolve_call(
        "", "MainMenuController", kGoMultyMethod, 0,
        &go_entry, &g_go_multy_info);

    const bool ready = g_main_menu_instance_field != nullptr &&
                       g_multiplayer_button_field != nullptr &&
                       component && core && go;
    if (!ready) {
        LOGE("16.1.1-battle-ui: metadata incomplete "
             "(menu=%p button=%p component=%d core=%d go=%d)",
             g_main_menu_instance_field, g_multiplayer_button_field,
             component ? 1 : 0, core ? 1 : 0, go ? 1 : 0);
    }
    return ready;
}

void* main_menu_controller() {
    if (g_main_menu_instance_field == nullptr ||
        il2cpp::field_static_get_value == nullptr) {
        return nullptr;
    }
    void* main_menu = nullptr;
    il2cpp::field_static_get_value(g_main_menu_instance_field, &main_menu);
    return main_menu;
}

void* multiplayer_game_object() {
    if (g_multiplayer_button_field == nullptr ||
        il2cpp::field_get_value == nullptr) {
        return nullptr;
    }
    void* main_menu = main_menu_controller();
    if (main_menu == nullptr) return nullptr;

    void* multiplayer = nullptr;
    il2cpp::field_get_value(main_menu, g_multiplayer_button_field,
                            &multiplayer);
    return multiplayer;
}

bool is_multiplayer_button(void* button) {
    if (button == nullptr || g_component_game_object == nullptr ||
        g_component_game_object_info == nullptr) {
        return false;
    }
    void* multiplayer = multiplayer_game_object();
    if (multiplayer == nullptr) return false;
    void* button_object =
        g_component_game_object(button, g_component_game_object_info);
    return button_object != nullptr && button_object == multiplayer;
}

void hook_offline_button_gate(void* self, bool offline,
                              const MethodInfo* method) {
    const bool previous = g_inside_offline_button_gate;
    g_inside_offline_button_gate = previous || offline;
    if (g_offline_button_gate != nullptr) {
        g_offline_button_gate(self, offline, method);
    }
    g_inside_offline_button_gate = previous;
}

void hook_button_set_enabled(void* self, bool enabled,
                             const MethodInfo* method) {
    bool restored = false;
    if (g_inside_offline_button_gate && !enabled &&
        is_multiplayer_button(self)) {
        enabled = true;
        restored = true;
    }
    if (g_button_set_enabled != nullptr) {
        g_button_set_enabled(self, enabled, method);
    }
    if (restored && !g_restore_logged.exchange(true)) {
        LOGW("16.1.1-battle-ui: restored the stock 'В бой' UIButton; other "
             "backend-dependent buttons remain gated");
    }
}

void hook_go_multy(void* self, const MethodInfo* method) {
    if (g_inside_target_click) {
        g_go_multy_seen = true;
        LOGI("16.1.1-battle-ui: stock GoMulty entered from target click");
    }
    if (g_go_multy != nullptr) g_go_multy(self, method);
}

void hook_multiplayer_click_core(void* self, bool play_sound,
                                 const MethodInfo* method) {
    if (g_inside_target_click) {
        g_click_core_seen = true;
        LOGI("16.1.1-battle-ui: stock multiplayer click core entered "
             "(playSound=%d)", play_sound ? 1 : 0);
    }
    if (g_multiplayer_click_core != nullptr) {
        g_multiplayer_click_core(self, play_sound, method);
    }
}

void hook_button_on_click(void* self, const MethodInfo* method) {
    if (!is_multiplayer_button(self)) {
        if (g_button_on_click != nullptr) g_button_on_click(self, method);
        return;
    }

    const bool previous_inside = g_inside_target_click;
    const bool previous_core = g_click_core_seen;
    const bool previous_go = g_go_multy_seen;
    g_inside_target_click = true;
    g_click_core_seen = false;
    g_go_multy_seen = false;

    const uint32_t click_number = g_target_clicks.fetch_add(1u) + 1u;
    LOGI("16.1.1-battle-ui: target UIButton.OnClick #%" PRIu32,
         click_number);

    if (g_button_on_click != nullptr) g_button_on_click(self, method);

    void* main_menu = main_menu_controller();
    if (!g_click_core_seen) {
        LOGW("16.1.1-battle-ui: target EventDelegate did not invoke "
             "MainMenuController; dispatching the stock click core");
        if (main_menu != nullptr && g_multiplayer_click_core != nullptr &&
            g_multiplayer_click_core_info != nullptr) {
            hook_multiplayer_click_core(
                main_menu, true, g_multiplayer_click_core_info);
        } else {
            LOGE("16.1.1-battle-ui: cannot dispatch stock click core "
                 "(menu=%p fn=%p info=%p)", main_menu,
                 reinterpret_cast<void*>(g_multiplayer_click_core),
                 g_multiplayer_click_core_info);
        }
    }

    if (!g_go_multy_seen) {
        LOGW("16.1.1-battle-ui: stock click core did not reach GoMulty; "
             "dispatching the stock GoMulty route");
        if (main_menu != nullptr && g_go_multy != nullptr &&
            g_go_multy_info != nullptr) {
            hook_go_multy(main_menu, g_go_multy_info);
        } else {
            LOGE("16.1.1-battle-ui: cannot dispatch stock GoMulty "
                 "(menu=%p fn=%p info=%p)", main_menu,
                 reinterpret_cast<void*>(g_go_multy), g_go_multy_info);
        }
    }

    g_inside_target_click = previous_inside;
    g_click_core_seen = previous_core;
    g_go_multy_seen = previous_go;
}

template <typename Fn>
void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

} // namespace detail

inline bool install_hooks() {
#if defined(__ANDROID__)
    static_assert(sizeof(void*) == 4,
                  "PG3D 16.1.1 target must be armeabi-v7a");
#endif

    if (!detail::resolve_metadata()) return false;

    // Install the leaves first. UIButton.OnClick is installed last, so a
    // target click can never run while a downstream fallback is unhooked.
    const bool setter = hook::install(
        {"", "UIButton", "set_isEnabled", 1},
        detail::replacement(&detail::hook_button_set_enabled),
        detail::original_slot(&detail::g_button_set_enabled), true);
    const bool gate = hook::install(
        {"", "OfflineModController", detail::kOfflineButtonGateMethod, 1},
        detail::replacement(&detail::hook_offline_button_gate),
        detail::original_slot(&detail::g_offline_button_gate), true);
    const bool core = hook::install(
        {"", "MainMenuController", "OnClickMultiplyerButtonCore", 1},
        detail::replacement(&detail::hook_multiplayer_click_core),
        detail::original_slot(&detail::g_multiplayer_click_core), true);
    const bool go = hook::install(
        {"", "MainMenuController", detail::kGoMultyMethod, 0},
        detail::replacement(&detail::hook_go_multy),
        detail::original_slot(&detail::g_go_multy), true);
    const bool on_click = hook::install(
        {"", "UIButton", "OnClick", 0},
        detail::replacement(&detail::hook_button_on_click),
        detail::original_slot(&detail::g_button_on_click), true);

    const int installed = (gate ? 1 : 0) + (setter ? 1 : 0) +
                          (core ? 1 : 0) + (go ? 1 : 0) +
                          (on_click ? 1 : 0);
    LOGI("16.1.1-battle-ui: installed %d hooks "
         "(gate=%s setter=%s onclick=%s core=%s go=%s; "
         "target=MainMenuController.multiplayerButton)",
         installed, gate ? "OK" : "FAILED", setter ? "OK" : "FAILED",
         on_click ? "OK" : "FAILED", core ? "OK" : "FAILED",
         go ? "OK" : "FAILED");
    return gate && setter && on_click && core && go;
}

} // namespace battle_ui_1610
