#pragma once

#include <atomic>

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
namespace battle_ui_1610 {
namespace detail {

using MethodInfo = void;
using InstanceBoolFn = void (*)(void* self, bool value,
                                const MethodInfo* method);
using ComponentGameObjectFn = void* (*)(void* self,
                                        const MethodInfo* method);

inline constexpr const char* kOfflineButtonGateMethod =
    u8"丁丅三七丆丙丛不丈";
inline constexpr const char* kMainMenuInstanceField =
    u8"丕丘丒业东丂丑下丟";

inline InstanceBoolFn g_offline_button_gate = nullptr;
inline InstanceBoolFn g_button_set_enabled = nullptr;
inline ComponentGameObjectFn g_component_game_object = nullptr;
inline const MethodInfo* g_component_game_object_info = nullptr;
inline void* g_main_menu_instance_field = nullptr;
inline void* g_multiplayer_button_field = nullptr;

inline thread_local bool g_inside_offline_button_gate = false;
inline std::atomic<bool> g_restore_logged{false};

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

    void* method = il2cpp::find_method_info(
        "UnityEngine", "Component", "get_gameObject", 0);
    void* pointer = il2cpp::method_pointer(method);
    g_component_game_object_info = method;
    g_component_game_object =
        reinterpret_cast<ComponentGameObjectFn>(pointer);

    const bool ready = g_main_menu_instance_field != nullptr &&
                       g_multiplayer_button_field != nullptr &&
                       g_component_game_object_info != nullptr &&
                       g_component_game_object != nullptr;
    if (!ready) {
        LOGE("16.1.1-battle-ui: metadata incomplete "
             "(menu=%p button=%p component=%p)",
             g_main_menu_instance_field, g_multiplayer_button_field,
             reinterpret_cast<void*>(g_component_game_object));
    }
    return ready;
}

void* multiplayer_game_object() {
    if (g_main_menu_instance_field == nullptr ||
        g_multiplayer_button_field == nullptr ||
        il2cpp::field_static_get_value == nullptr ||
        il2cpp::field_get_value == nullptr) {
        return nullptr;
    }

    void* main_menu = nullptr;
    il2cpp::field_static_get_value(g_main_menu_instance_field, &main_menu);
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

    // Install the setter first so the gate cannot run without the narrow
    // target filter already being active.
    const bool setter = hook::install(
        {"", "UIButton", "set_isEnabled", 1},
        detail::replacement(&detail::hook_button_set_enabled),
        detail::original_slot(&detail::g_button_set_enabled), true);
    const bool gate = hook::install(
        {"", "OfflineModController", detail::kOfflineButtonGateMethod, 1},
        detail::replacement(&detail::hook_offline_button_gate),
        detail::original_slot(&detail::g_offline_button_gate), true);

    LOGI("16.1.1-battle-ui: installed %d hooks (gate=%s setter=%s; "
         "target=MainMenuController.multiplayerButton)",
         (gate ? 1 : 0) + (setter ? 1 : 0),
         gate ? "OK" : "FAILED", setter ? "OK" : "FAILED");
    return gate && setter;
}

} // namespace battle_ui_1610
