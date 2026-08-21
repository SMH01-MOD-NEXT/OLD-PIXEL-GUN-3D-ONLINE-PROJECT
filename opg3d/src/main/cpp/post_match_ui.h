#pragma once

#include <atomic>
#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// The stock reward-panel OK handler is guarded by
// NetworkStartTableNGUIController.isCancelHideAvardPanel. The controller is
// reused for another round, but that flag can remain true after battle one;
// HideAvardPanel() then returns before ShowEndInterface() and the second battle
// appears to have a dead button.
//
// Rewards are redundant in this restoration build. Keep the stock panel and
// its OK button, but skip RewardWindowController.StartRewardAnimation(). The
// animation-complete callback advances the Animator to the clickable state,
// and the original HideAvardPanel() performs only the proven stock transition
// to ShowEndInterface(winner, winnerCommand) plus normal panel cleanup.
namespace post_match_ui {
namespace detail {

using MethodInfo = void;
using InstanceVoidFn = void (*)(void* self, const MethodInfo* method);

inline InstanceVoidFn g_hide_award_panel = nullptr;
inline InstanceVoidFn g_on_reward_show = nullptr;
inline InstanceVoidFn g_on_reward_animation_ends = nullptr;
inline const MethodInfo* g_mi_on_reward_animation_ends = nullptr;
inline void* g_cancel_hide_award_field = nullptr;

inline std::atomic<bool> g_transition_started{false};
inline std::atomic<uint32_t> g_panels_seen{0u};
inline std::atomic<uint32_t> g_duplicate_clicks{0u};

template <typename Fn>
void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

bool set_cancel_guard(void* self, bool value) {
    if (self == nullptr || g_cancel_hide_award_field == nullptr ||
        il2cpp::field_set_value == nullptr) {
        return false;
    }
    il2cpp::field_set_value(self, g_cancel_hide_award_field, &value);
    return true;
}

bool resolve_call(const hook::ManagedMethod& target, void** out_fn,
                  const MethodInfo** out_mi) {
    void* info = il2cpp::find_method_info(target.namespaze, target.klass,
                                          target.method, target.args_count);
    void* pointer = il2cpp::method_pointer(info);
    if (info == nullptr || pointer == nullptr) {
        LOGE("post-match: cannot resolve %s.%s/%d", target.klass,
             target.method, target.args_count);
        return false;
    }
    *out_fn = pointer;
    *out_mi = info;
    return true;
}

void hook_on_reward_show(void* self, const MethodInfo* method) {
    (void)method;
    g_transition_started.store(false, std::memory_order_release);

    if (!set_cancel_guard(self, false)) {
        LOGE("post-match: could not reset isCancelHideAvardPanel; forwarding "
             "the stock reward path for safety");
        if (g_on_reward_show != nullptr) g_on_reward_show(self, method);
        return;
    }

    const uint32_t panel =
        g_panels_seen.fetch_add(1u, std::memory_order_relaxed) + 1u;
    (void)panel; // Keep -Werror builds clean when logging is compiled out.
    LOGI("post-match: reward panel #%u ready; reward collection/counting "
         "skipped and OK routed directly to the player/team table", panel);

    // This is the callback normally passed to StartRewardAnimation(). Calling
    // it immediately keeps the Animator/button state stock without starting
    // any reward coroutine.
    if (g_on_reward_animation_ends != nullptr) {
        g_on_reward_animation_ends(self, g_mi_on_reward_animation_ends);
    }
}

void hook_hide_award_panel(void* self, const MethodInfo* method) {
    if (self == nullptr || g_hide_award_panel == nullptr) return;

    bool expected = false;
    if (!g_transition_started.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        const uint32_t duplicate =
            g_duplicate_clicks.fetch_add(1u, std::memory_order_relaxed) + 1u;
        if (duplicate <= 4u) {
            LOGW("post-match: ignored duplicate OK press while the result "
                 "table transition is already running");
        }
        return;
    }

    // Ignore the stale one-shot value left by the prior round. Disassembly of
    // 13.2.1 proves that the original method then clears rewardWindow, calls
    // ShowEndInterface(winner, winnerCommand), removes the reward panel and
    // sets this flag back to true. No reward grant method is called here.
    if (!set_cancel_guard(self, false)) {
        g_transition_started.store(false, std::memory_order_release);
        LOGE("post-match: OK press could not reset isCancelHideAvardPanel");
        return;
    }

    g_hide_award_panel(self, method);
    LOGI("post-match: OK opened the stock player/team result table");
}

} // namespace detail

inline bool install_hooks() {
    detail::g_cancel_hide_award_field = il2cpp::find_field(
        "", "NetworkStartTableNGUIController", "isCancelHideAvardPanel");
    if (detail::g_cancel_hide_award_field == nullptr ||
        il2cpp::field_set_value == nullptr) {
        LOGE("post-match: isCancelHideAvardPanel field/write API unavailable");
        return false;
    }

    if (!detail::resolve_call(
            {"", "NetworkStartTableNGUIController",
             "OnRewardAnimationEnds", 0},
            reinterpret_cast<void**>(&detail::g_on_reward_animation_ends),
            &detail::g_mi_on_reward_animation_ends)) {
        return false;
    }

    const bool reward_show = hook::install(
        {"", "NetworkStartTableNGUIController", "OnRewardShow", 0},
        detail::replacement(&detail::hook_on_reward_show),
        detail::original_slot(&detail::g_on_reward_show), true);
    const bool ok_button = hook::install(
        {"", "NetworkStartTableNGUIController", "HideAvardPanel", 0},
        detail::replacement(&detail::hook_hide_award_panel),
        detail::original_slot(&detail::g_hide_award_panel), true);
    if (!reward_show || !ok_button) {
        LOGE("post-match: reward-screen bypass hooks are incomplete");
        return false;
    }

    LOGI("post-match: armed (reward animation/collection skipped, stale "
         "isCancelHideAvardPanel reset per battle, OK -> stock player/team "
         "table, duplicate presses ignored)");
    return true;
}

} // namespace post_match_ui
