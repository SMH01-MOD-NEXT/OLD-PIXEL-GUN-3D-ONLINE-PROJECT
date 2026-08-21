#pragma once

#include <atomic>
#include <cstdint>

#include "hook.h"
#include "log.h"

// 13.2.1 has a second local punishment UI that is independent of
// CheatDetectedBanner. NotificationController.Update and Switcher's startup
// coroutine call CustomHungerBase.set_isShow(true). The stock setter then
// calls GameConnect.Disconnect() and tail-calls SceneManager.LoadScene().
// Inside that scene set_isTableUpdated(true) performs PlayerPrefs.DeleteAll(),
// writes abuse state through Storager and starts CloudSyncController work.
//
// Both setters are gates: their destructive bodies execute only for true.
// Forward false unchanged and refuse true at the narrowest possible boundary.
// No Photon leave/reconnect path, NotificationController timing, PlayerPrefs,
// Storager key or ordinary scene load is modified.
namespace custom_hunger_guard {
namespace detail {

using MethodInfo = void;
using SetBoolStaticFn = void (*)(void* static_context, bool value,
                                 const MethodInfo* method);
using SetBoolInstanceFn = void (*)(void* self, bool value,
                                   const MethodInfo* method);

inline SetBoolStaticFn g_set_is_show = nullptr;
inline SetBoolInstanceFn g_set_is_table_updated = nullptr;

inline constexpr uint32_t kMaxLoggedEvents = 16u;
inline std::atomic<uint32_t> g_logged_show{0u};
inline std::atomic<uint32_t> g_logged_table{0u};

template <typename Fn>
void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

bool should_log(std::atomic<uint32_t>& counter) {
    return counter.fetch_add(1u, std::memory_order_relaxed) <
           kMaxLoggedEvents;
}

void hook_set_is_show(void* static_context, bool value,
                      const MethodInfo* method) {
    if (!value) {
        if (g_set_is_show != nullptr) {
            g_set_is_show(static_context, value, method);
        }
        return;
    }

    if (should_log(g_logged_show)) {
        LOGW("custom-hunger-guard: blocked CustomHungerBase.set_isShow(true); "
             "GameConnect.Disconnect and the punishment-scene load were not "
             "reached");
    }
}

void hook_set_is_table_updated(void* self, bool value,
                               const MethodInfo* method) {
    if (!value) {
        if (g_set_is_table_updated != nullptr) {
            g_set_is_table_updated(self, value, method);
        }
        return;
    }

    if (should_log(g_logged_table)) {
        LOGW("custom-hunger-guard: blocked "
             "CustomHungerBase.set_isTableUpdated(true); "
             "PlayerPrefs.DeleteAll, Storager abuse writes and cloud apply "
             "were not reached");
    }
}

} // namespace detail

inline bool install_hooks() {
    bool installed = hook::install(
        {"", "CustomHungerBase", "set_isShow", 1},
        detail::replacement(&detail::hook_set_is_show),
        detail::original_slot(&detail::g_set_is_show), true);
    installed &= hook::install(
        {"", "CustomHungerBase", "set_isTableUpdated", 1},
        detail::replacement(&detail::hook_set_is_table_updated),
        detail::original_slot(&detail::g_set_is_table_updated), true);

    if (!installed) {
        LOGE("custom-hunger-guard: required punishment setters are incomplete; "
             "the delayed CustomHunger window is not safely blocked");
        return false;
    }

    LOGI("custom-hunger-guard: armed (set_isShow(true)=refused, "
         "set_isTableUpdated(true)=refused; false values and unrelated game "
         "logic stay stock)");
    return true;
}

} // namespace custom_hunger_guard
