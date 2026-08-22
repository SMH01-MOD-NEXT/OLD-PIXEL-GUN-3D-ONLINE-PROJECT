#pragma once

#include <chrono>
#include <cinttypes>
#include <cstdint>

#include "battle_ui_1610.h"
#include "log.h"

// The 16.1.1 main-menu prefab dispatches UIButton.OnClick through three
// UIButton components for one physical tap. battle_ui_1610 deliberately keeps
// the stock callback chain, so debounce the saved stock delegate trampoline
// rather than installing a second managed hook in ShadowHook UNIQUE mode.
namespace battle_click_debounce_1610 {
namespace detail {

using MethodInfo = void;
using ButtonOnClickFn = battle_ui_1610::detail::InstanceVoidFn;
inline constexpr uint64_t kWindowMs = 500u;
inline ButtonOnClickFn g_stock_button_on_click = nullptr;
inline thread_local uint64_t g_last_target_dispatch_ms = 0u;

uint64_t monotonic_millis() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count());
}

void debounced_stock_dispatch(void* self, const MethodInfo* method) {
    if (!battle_ui_1610::detail::is_multiplayer_button(self)) {
        if (g_stock_button_on_click != nullptr) {
            g_stock_button_on_click(self, method);
        }
        return;
    }

    const uint64_t now_ms = monotonic_millis();
    if (g_last_target_dispatch_ms != 0u &&
        now_ms >= g_last_target_dispatch_ms &&
        now_ms - g_last_target_dispatch_ms < kWindowMs) {
        // Tell the existing click bridge that this duplicate is fully handled,
        // so it does not invoke either fallback after this wrapper returns.
        battle_ui_1610::detail::g_click_core_seen = true;
        battle_ui_1610::detail::g_go_multy_seen = true;
        const uint32_t click =
            battle_ui_1610::detail::g_target_clicks.load();
        LOGW("16.1.1-battle-ui: target UIButton.OnClick #%" PRIu32
             " suppressed as a duplicate component dispatch within %" PRIu64
             " ms", click, kWindowMs);
        return;
    }

    g_last_target_dispatch_ms = now_ms;
    LOGI("16.1.1-battle-ui: accepted one physical target click");
    if (g_stock_button_on_click != nullptr) {
        g_stock_button_on_click(self, method);
    }
}

} // namespace detail

inline bool install() {
    using namespace detail;
    if (battle_ui_1610::detail::g_button_on_click == nullptr) {
        LOGE("16.1.1-battle-ui: click debounce could not capture the stock "
             "UIButton.OnClick trampoline");
        return false;
    }
    g_stock_button_on_click = battle_ui_1610::detail::g_button_on_click;
    battle_ui_1610::detail::g_button_on_click = &debounced_stock_dispatch;
    LOGI("16.1.1-battle-ui: armed 500 ms target-only click debounce");
    return true;
}

} // namespace battle_click_debounce_1610
