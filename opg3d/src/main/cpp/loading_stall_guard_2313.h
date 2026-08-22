#pragma once

// PG3D 23.1.3 ARM64 -- loading stall guard for the 90% freeze.
//
// Symptom: the loading bar stops at 90% and the process goes completely
// silent.  Photon is not involved: "PHOTON_APP_ID is empty" is reported at
// +010310ms, 400 ms *before* the freeze, and Photon is only used once the main
// menu is up.
//
// Evidence (logcat_2026-08-22_16-37-38.txt, build "23.1.3 ARM64 lobby gate
// v3", package com.pixel.gun3d, main thread 3772):
//
//   #000119 +010688ms 23.1.3-swt: InitSwitcher state=33 ranchoOk=0 result=1
//   #000120 +010705ms 23.1.3-swt: InitSwitcher state=34 ranchoOk=0 result=1
//   +011.2s  W/Unity DontDestroyOnLoad only works for root GameObjects ...
//              UnityEngine.Object:Internal_InstantiateSingle(...)
//              UnityEngine.Object:Instantiate(...)
//              InfoWindowController:\u4e0b\u4e0c\u4e11\u4e01\u4e0b\u4e1f\u4e1b\u4e18\u4e0a()
//              \u4e1c\u4e03\u4e17\u4e0c\u4e11\u4e05\u4e1e\u4e07\u4e01:MoveNext()
//   ... and then nothing at all for the remaining ~64 s of the capture.
//
// startup_trace_2313.h emits its heartbeat *after* the original MoveNext
// returns.  The call entered with <>1__state == 34 returned normally; the next
// call (entry state 35) produced the Unity warning above and never returned.
// The coroutine is therefore not suspended on a yield instruction -- the step
// itself does not complete, which is why the whole process stops logging.
//
// Static cross-check against dump2313.cs and libil2cpp.so
// (SHA-256 f0a130c4e8d9487059eab4b0f08462f6aa7d057510cada0fd6fb3043c77deb5c):
//
//   * Switcher.\u4e1c\u4e03\u4e17\u4e0c\u4e11\u4e05\u4e1e\u4e07\u4e01.MoveNext is RVA 0x217B040.
//   * Its only call to InfoWindowController.\u4e0b\u4e0c\u4e11\u4e01\u4e0b\u4e1f\u4e1b\u4e18\u4e0a()
//     (RVA 0x28F7428) is at RVA 0x217B8A4, inside the block that ends by
//     writing <>1__state = 36 at RVA 0x217B8F8 -- i.e. the state-35 body.
//   * InfoWindowController.\u4e02\u4e14\u4e15\u4e0a\u4e01\u4e09\u4e07\u4e00\u4e0b(bool), the panel setter
//     guarded in startup_guards_2313.h, has exactly one caller in the whole
//     binary: ConnectionLostChecker.\u4e06\u4e06\u4e06\u4e05\u4e07\u4e02\u4e10\u4e19\u4e19() at RVA
//     0x3DCC24C.  ConnectionLostChecker.Update is already suppressed, so that
//     guard never fires (no "suppressed false no-internet modal" line exists
//     in the capture) and cannot be the cause of the stall.
//
// Fix: the tail of InitializeSwitcher (states >= 35) only decorates the
// loading screen with retired-service UI and telemetry.  When MoveNext is
// entered at that state we finish the enumeration ourselves: write the Roslyn
// "iterator finished" marker (<>1__state = -1) and report completion, so the
// foreach inside Switcher.Start ends normally and the outer coroutine
// continues into LoadMainMenu().  Disposal of the iterator is a no-op in that
// state, so nothing is left half-torn-down.
//
// A watchdog thread stays armed until LoadMainMenu runs.  It separates the two
// failure modes the original capture could not distinguish:
//   * "blocked"    - MoveNext was entered and has not returned: the game
//                    thread is stuck inside stock code;
//   * "not pumped" - MoveNext returned but is not being called again: the
//                    coroutine was stopped or Unity no longer schedules it.
//
// Object offsets below come from dump2313.cs and match the layout documented
// in startup_trace_2313.h.

#include <atomic>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <pthread.h>
#include <unistd.h>

#include "log.h"

// Entry state of the InitializeSwitcher step that never returns.  Override at
// build time if a future capture shows the stall moved to another step.
#ifndef OPG3D_2313_INIT_BYPASS_STATE
#define OPG3D_2313_INIT_BYPASS_STATE 35
#endif

// Set to 1 to keep the stock (stalling) behaviour and only run the watchdog.
#ifndef OPG3D_2313_DISABLE_INIT_BYPASS
#define OPG3D_2313_DISABLE_INIT_BYPASS 0
#endif

namespace loading_stall_guard_2313 {
namespace detail {

inline constexpr size_t kStateOffset    = 0x10;   // <>1__state
inline constexpr size_t kThisOffset     = 0x20;   // <>4__this (Switcher)
inline constexpr size_t kRanchoOffset   = 0x28;   // <ranchoComplete>5__2
inline constexpr size_t kProgressOffset = 0x128;  // Switcher progress field

inline constexpr int32_t kBypassState   = OPG3D_2313_INIT_BYPASS_STATE;
inline constexpr int32_t kFinishedState = -1;     // Roslyn: iterator finished

inline constexpr uint64_t kPollMs        = 1000u;
inline constexpr uint64_t kStuckAfterMs  = 5000u;
inline constexpr uint64_t kRepeatEveryMs = 15000u;

template <typename T>
inline T read_at(const void* base, size_t offset, T fallback) noexcept {
    if (base == nullptr) return fallback;
    T value = fallback;
    std::memcpy(&value, static_cast<const uint8_t*>(base) + offset, sizeof(T));
    return value;
}

inline void write_state(void* base, int32_t value) noexcept {
    if (base == nullptr) return;
    std::memcpy(static_cast<uint8_t*>(base) + kStateOffset, &value,
                sizeof(value));
}

#if OPG3D_2313_DISABLE_INIT_BYPASS
inline std::atomic<bool> g_bypass_enabled{false};
#else
inline std::atomic<bool> g_bypass_enabled{true};
#endif

inline std::atomic<uint32_t> g_bypassed{0u};
inline std::atomic<uint64_t> g_enter_ms{0u};
inline std::atomic<int32_t>  g_enter_state{INT32_MIN};
inline std::atomic<uint64_t> g_exit_ms{0u};
inline std::atomic<int32_t>  g_exit_state{INT32_MIN};
inline std::atomic<uint64_t> g_pump_ms{0u};
inline std::atomic<uint64_t> g_report_ms{0u};
inline std::atomic<bool>     g_menu_reached{false};
inline std::atomic<bool>     g_watchdog_running{false};

inline void* watchdog_main(void*) {
    for (;;) {
        usleep(static_cast<useconds_t>(kPollMs * 1000u));
        if (g_menu_reached.load(std::memory_order_relaxed)) continue;

        const uint64_t now      = opg3d_log::monotonic_ms();
        const uint64_t entered  = g_enter_ms.load(std::memory_order_relaxed);
        const uint64_t exited   = g_exit_ms.load(std::memory_order_relaxed);
        const uint64_t pumped   = g_pump_ms.load(std::memory_order_relaxed);
        const uint64_t reported = g_report_ms.load(std::memory_order_relaxed);
        const bool throttled =
            reported != 0u && now >= reported && now - reported < kRepeatEveryMs;

        if (entered != 0u && now >= entered && now - entered >= kStuckAfterMs) {
            if (!throttled) {
                g_report_ms.store(now, std::memory_order_relaxed);
                LOGE("23.1.3-stall: blocked -- InitializeSwitcher.MoveNext "
                     "entered at state=%d has not returned for %" PRIu64
                     " ms; the game thread is stuck inside stock 23.1.3 code",
                     g_enter_state.load(std::memory_order_relaxed),
                     now - entered);
            }
            continue;
        }

        if (exited != 0u && now >= exited && now - exited >= kStuckAfterMs) {
            if (!throttled) {
                g_report_ms.store(now, std::memory_order_relaxed);
                LOGW("23.1.3-stall: not pumped -- no InitializeSwitcher "
                     "MoveNext for %" PRIu64 " ms after state=%d (last "
                     "Switcher.Start tick %" PRIu64 " ms ago); the loading "
                     "coroutine is no longer scheduled",
                     now - exited,
                     g_exit_state.load(std::memory_order_relaxed),
                     pumped != 0u && now >= pumped ? now - pumped : 0u);
            }
        }
    }
    return nullptr;
}

} // namespace detail

// Called from the Switcher.Start heartbeat: proves Unity is still pumping the
// outer loading coroutine.
inline void note_pump() {
    detail::g_pump_ms.store(opg3d_log::monotonic_ms(),
                            std::memory_order_relaxed);
}

// Called immediately before and after the stock InitializeSwitcher MoveNext.
inline void note_enter(int32_t state) {
    detail::g_enter_state.store(state, std::memory_order_relaxed);
    detail::g_enter_ms.store(opg3d_log::monotonic_ms(),
                             std::memory_order_relaxed);
}

inline void note_exit(int32_t state, bool result) {
    detail::g_enter_ms.store(0u, std::memory_order_relaxed);
    detail::g_exit_state.store(state, std::memory_order_relaxed);
    detail::g_exit_ms.store(opg3d_log::monotonic_ms(),
                            std::memory_order_relaxed);
    if (!result) {
        LOGI("23.1.3-stall: InitializeSwitcher finished on its own at state=%d",
             state);
    }
}

inline void note_menu_reached() {
    if (!detail::g_menu_reached.exchange(true)) {
        LOGI("23.1.3-stall: LoadMainMenu is running (%u bypassed step(s)); "
             "loading watchdog disarmed",
             detail::g_bypassed.load(std::memory_order_relaxed));
    }
}

// Returns true when the caller must NOT enter the stock MoveNext body.
inline bool should_bypass(void* self, int32_t state) {
    if (!detail::g_bypass_enabled.load(std::memory_order_relaxed)) return false;
    if (self == nullptr || state < detail::kBypassState) return false;

    const uint8_t rancho =
        detail::read_at<uint8_t>(self, detail::kRanchoOffset, 0u);
    void* const switcher =
        detail::read_at<void*>(self, detail::kThisOffset, nullptr);
    const float progress =
        detail::read_at<float>(switcher, detail::kProgressOffset, -1.0f);

    // Mark the iterator finished before returning: a stray MoveNext can then
    // never re-enter the stalling body, and Dispose() becomes a no-op.
    detail::write_state(self, detail::kFinishedState);
    detail::g_enter_ms.store(0u, std::memory_order_relaxed);
    detail::g_exit_state.store(state, std::memory_order_relaxed);
    detail::g_exit_ms.store(opg3d_log::monotonic_ms(),
                            std::memory_order_relaxed);

    const uint32_t count = detail::g_bypassed.fetch_add(1u) + 1u;
    if (count <= 2u) {
        LOGW("23.1.3-stall: skipping InitializeSwitcher state=%d #%u "
             "(ranchoOk=%d progress=%.3f) -- that step never returns while "
             "the 23.1.3 backend is retired; iterator marked finished so "
             "Switcher.Start continues into LoadMainMenu",
             state, count, rancho ? 1 : 0, progress);
    }
    return true;
}

inline bool start_watchdog() {
    if (detail::g_watchdog_running.exchange(true)) return true;
    pthread_t thread;
    if (pthread_create(&thread, nullptr, &detail::watchdog_main, nullptr) != 0) {
        detail::g_watchdog_running.store(false);
        LOGE("23.1.3-stall: pthread_create failed; loading watchdog is off");
        return false;
    }
    pthread_detach(thread);
    LOGI("23.1.3-stall: loading watchdog armed (%s at state>=%d, stall "
         "reported after %" PRIu64 " ms)",
         detail::g_bypass_enabled.load(std::memory_order_relaxed)
             ? "bypass enabled"
             : "bypass disabled",
         detail::kBypassState, detail::kStuckAfterMs);
    return true;
}

} // namespace loading_stall_guard_2313
