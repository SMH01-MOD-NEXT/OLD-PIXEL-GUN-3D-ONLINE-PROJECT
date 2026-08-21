#pragma once

#include <atomic>
#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// PG3D 14.1.1 post-match flow.
//
// 14.1.1 removed NetworkStartTableNGUIController.OnRewardShow() and
// OnRewardAnimationEnds(). The equivalent hand-off is now
// AnimationEventShowRewardsFinished(): the opening animation has completed,
// and the stock body starts ShowRewardsCoroutine() via
// OnRewardFirstWindowShow(0). We keep the completed opening animation, but
// replace that event body so reward collection/counting never starts.
//
// The stock OK handler is still HideAvardPanel(), and it is still guarded by
// isCancelHideAvardPanel (offset 0x98 in this build). The controller survives
// into another round, so a stale true value makes the next OK press return
// before ShowEndInterface(winner, winnerCommand). Reset the field for every
// panel and immediately before every accepted press, then let the original
// method perform the normal player/team-table transition and cleanup.
namespace post_match_ui {
namespace detail {

using MethodInfo = void;
using InstanceVoidFn = void (*)(void* self, const MethodInfo* method);

inline InstanceVoidFn g_hide_award_panel = nullptr;
inline InstanceVoidFn g_show_rewards_finished = nullptr;
inline void* g_cancel_hide_award_field = nullptr;
inline void* g_rewards_showing_field = nullptr;

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

bool set_bool(void* self, void* field, bool value) {
    if (self == nullptr || field == nullptr || il2cpp::field_set_value == nullptr) {
        return false;
    }
    il2cpp::field_set_value(self, field, &value);
    return true;
}

void hook_show_rewards_finished(void* self, const MethodInfo* method) {
    (void)method;
    g_transition_started.store(false, std::memory_order_release);

    const bool guard_reset = set_bool(self, g_cancel_hide_award_field, false);
    const bool showing_reset = set_bool(self, g_rewards_showing_field, false);
    if (!guard_reset || !showing_reset) {
        LOGE("post-match-14.1.1: reward state could not be normalised; "
             "forwarding the stock reward coroutine for safety");
        if (g_show_rewards_finished != nullptr) {
            g_show_rewards_finished(self, method);
        }
        return;
    }

    const uint32_t panel =
        g_panels_seen.fetch_add(1u, std::memory_order_relaxed) + 1u;
    (void)panel;
    LOGI("post-match-14.1.1: reward panel #%u opened; "
         "ShowRewardsCoroutine and reward collection/counting skipped, "
         "OK remains routed to the stock player/team table", panel);
}

void hook_hide_award_panel(void* self, const MethodInfo* method) {
    if (self == nullptr || g_hide_award_panel == nullptr) return;

    bool expected = false;
    if (!g_transition_started.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        const uint32_t duplicate =
            g_duplicate_clicks.fetch_add(1u, std::memory_order_relaxed) + 1u;
        if (duplicate <= 4u) {
            LOGW("post-match-14.1.1: ignored duplicate OK press while the "
                 "player/team-table transition is running");
        }
        return;
    }

    if (!set_bool(self, g_cancel_hide_award_field, false)) {
        g_transition_started.store(false, std::memory_order_release);
        LOGE("post-match-14.1.1: OK could not reset "
             "isCancelHideAvardPanel; transition refused safely");
        return;
    }

    // Verified 14.1.1 body at RVA 0xBA6A98: after the guard it clears
    // rewardWindow, calls ShowEndInterface(winner, winnerCommand), performs
    // normal panel cleanup and finally restores the one-shot flag to true.
    g_hide_award_panel(self, method);
    LOGI("post-match-14.1.1: OK opened the stock player/team result table");
}

} // namespace detail

inline bool install_hooks() {
    detail::g_cancel_hide_award_field = il2cpp::find_field(
        "", "NetworkStartTableNGUIController", "isCancelHideAvardPanel");
    detail::g_rewards_showing_field = il2cpp::find_field(
        "", "NetworkStartTableNGUIController", "isRewardsShowing");
    if (detail::g_cancel_hide_award_field == nullptr ||
        detail::g_rewards_showing_field == nullptr ||
        il2cpp::field_set_value == nullptr) {
        LOGE("post-match-14.1.1: required reward-state fields are unavailable");
        return false;
    }

    const bool reward_event = hook::install(
        {"", "NetworkStartTableNGUIController",
         "AnimationEventShowRewardsFinished", 0},
        detail::replacement(&detail::hook_show_rewards_finished),
        detail::original_slot(&detail::g_show_rewards_finished), true);
    const bool ok_button = hook::install(
        {"", "NetworkStartTableNGUIController", "HideAvardPanel", 0},
        detail::replacement(&detail::hook_hide_award_panel),
        detail::original_slot(&detail::g_hide_award_panel), true);
    if (!reward_event || !ok_button) {
        LOGE("post-match-14.1.1: reward bypass hooks are incomplete");
        return false;
    }

    LOGI("post-match-14.1.1: armed (reward coroutine skipped, stale guard "
         "reset per panel/press, OK -> stock player/team table, duplicates "
         "ignored)");
    return true;
}

} // namespace post_match_ui
