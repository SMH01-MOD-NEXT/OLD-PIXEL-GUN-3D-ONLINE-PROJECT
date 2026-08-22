#pragma once

// PG3D 23.1.3 ARM64 -- loading stall guard for the boot sequence.
//
// Full write-up with disassembly: docs/PORT_23_1_3_LOADING_STALL.md.
// None of this is related to PHOTON_APP_ID; that warning is printed before the
// stall and Photon is only used once the main menu exists.
//
// ---------------------------------------------------------------------------
// capture 1 -- freeze at 90%   (logcat_2026-08-22_16-37-38.txt)
// ---------------------------------------------------------------------------
// The heartbeat is emitted after the original MoveNext returns. State 34
// returned, the call entered at <>1__state == 35 never did, and the last
// managed line was an InfoWindowController instantiation from that same body:
//
//   +010705ms InitSwitcher state=34 ranchoOk=0 result=1
//   +011.2s   W/Unity DontDestroyOnLoad only works for root GameObjects
//               InfoWindowController:\u4e0b\u4e0c\u4e11\u4e01\u4e0b\u4e1f\u4e1b\u4e18\u4e0a()
//               \u4e1c\u4e03\u4e17\u4e0c\u4e11\u4e05\u4e1e\u4e07\u4e01:MoveNext()
//   ... nothing for the remaining ~64 s.
//
// Static cross-check: Switcher.\u4e1c\u4e03\u4e17\u4e0c\u4e11\u4e05\u4e1e\u4e07\u4e01.MoveNext = 0x217B040; its
// only InfoWindowController.\u4e0b\u4e0c\u4e11\u4e01\u4e0b\u4e1f\u4e1b\u4e18\u4e0a() call (0x28F7428) is at
// 0x217B8A4, inside the block that writes <>1__state = 36 at 0x217B8F8 -- the
// state-35 body. A coroutine parked on a yield keeps producing heartbeats, so
// this is a blocked game thread, not a wait.
//
// ---------------------------------------------------------------------------
// capture 2 -- freeze at 45%   (logcat_2026-08-22_17-32-48.txt)
// ---------------------------------------------------------------------------
// v1 of this guard ended the whole enumeration at state 35 (<>1__state = -1),
// so states 36+ never ran. LoadMainMenu started, waited out its 3 s minimum
// and its tail then walked singletons those states never created:
//
//   Stopwatch.Stop() / GameConnect.<static>(0) / <static @0x230> (CBZ -> throw)
//   / [X20+0x78] (CBZ -> throw) / WeaponManager.<method>(int)
//   / <static @0x0>.field_0x70 -> SetActive(false) / scene load 0x4573458
//
// libopg3d is built with -fno-exceptions on purpose (see CMakeLists.txt), so
// the managed NullReferenceException unwound through the hook frame, Unity
// dropped the coroutine and the bar stayed at 45%.
//
// ---------------------------------------------------------------------------
// capture 3 -- loading completes   (logcat_2026-08-22_17-54-21.txt)
// ---------------------------------------------------------------------------
// With the single-step skip below:
//
//   +008971ms skipping InitializeSwitcher state=35 (bar=0.450)
//   +009009..009321ms states 36..44, bar 0.450 -> 0.750
//   +009354ms InitializeSwitcher finished on its own at state=45
//   +009355ms LoadMainMenu state=0 -> 1
//   +011422ms LoadMainMenu state=1 (last heartbeat)
//   +011616ms AuthSceneController.Awake  <- a new scene is up
//
// No AuthSceneController.Awake exists anywhere earlier in that boot, so the
// tail ran at ~+11.5 s and its scene load succeeded; the Switcher died with
// its scene, which is why both iterators stop being pumped. v2 of the
// watchdog called that an abort because it only recognised the tail when
// MoveNext was entered again at state 2 -- impossible after the scene swap.
// Tail completion is now detected on the exit path instead.
//
// ---------------------------------------------------------------------------
// policy
// ---------------------------------------------------------------------------
// Skip the *single* stalling step: when MoveNext is entered at a listed state,
// write <>1__state = state + 1 and report a yield without running the body, so
// every remaining step still runs. Mode 2 restores the v1 behaviour for A/B
// testing. The watchdog separates:
//   * "blocked"    - MoveNext was entered and never returned;
//   * "not pumped" - it returns but is not called again;
//   * "aborted"    - LoadMainMenu stopped before its tail ran (managed
//                    exception escaping stock code).
// It disarms as soon as the tail completes, because the scene load legitimately
// destroys the Switcher and everything it was running.
//
// Object offsets come from dump2313.cs and match startup_trace_2313.h.

#include <atomic>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <pthread.h>
#include <unistd.h>

#include "log.h"

// Backwards compatibility with the v1 switch names.
#if defined(OPG3D_2313_INIT_BYPASS_STATE) && !defined(OPG3D_2313_INIT_SKIP_STATES)
#define OPG3D_2313_INIT_SKIP_STATES OPG3D_2313_INIT_BYPASS_STATE
#endif

// Entry states of InitializeSwitcher whose body must not run. Comma-separated,
// e.g. -DOPG3D_2313_INIT_SKIP_STATES="35,37".
#ifndef OPG3D_2313_INIT_SKIP_STATES
#define OPG3D_2313_INIT_SKIP_STATES 35
#endif

// 0 = run stock code, watchdog only
// 1 = skip the listed steps, resume the state machine at state + 1 (default)
// 2 = finish the whole iterator at the first listed step (v1; breaks the
//     LoadMainMenu tail, kept for A/B testing)
#ifndef OPG3D_2313_INIT_BYPASS_MODE
#define OPG3D_2313_INIT_BYPASS_MODE 1
#endif

#if defined(OPG3D_2313_DISABLE_INIT_BYPASS) && OPG3D_2313_DISABLE_INIT_BYPASS
#undef OPG3D_2313_INIT_BYPASS_MODE
#define OPG3D_2313_INIT_BYPASS_MODE 0
#endif

namespace loading_stall_guard_2313 {
namespace detail {

// Iterator object fields.
inline constexpr size_t kStateOffset  = 0x10;   // <>1__state
inline constexpr size_t kThisOffset   = 0x20;   // <>4__this (Switcher)
inline constexpr size_t kRanchoOffset = 0x28;   // <ranchoComplete>5__2

// Switcher instance fields. 0x128 is not the progress bar: LoadMainMenu
// compares it against the loading Stopwatch in seconds (it reads 3.000 in
// every capture). 0x138 is the value the iterators yield, i.e. the bar.
inline constexpr size_t kMinSecOffset = 0x128;
inline constexpr size_t kBarOffset    = 0x138;

inline constexpr int32_t kSkipStates[]  = {OPG3D_2313_INIT_SKIP_STATES};
inline constexpr int32_t kFinishedState = -1;   // Roslyn: iterator finished
inline constexpr int32_t kMenuTailState = 2;    // LoadMainMenu: tail is done
inline constexpr int     kBypassMode    = OPG3D_2313_INIT_BYPASS_MODE;

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

inline bool is_skip_state(int32_t state) noexcept {
    for (const int32_t candidate : kSkipStates) {
        if (state == candidate) return true;
    }
    return false;
}

// InitializeSwitcher iterator.
inline std::atomic<uint32_t> g_skipped{0u};
inline std::atomic<uint64_t> g_init_enter_ms{0u};
inline std::atomic<int32_t>  g_init_enter_state{INT32_MIN};
inline std::atomic<uint64_t> g_init_exit_ms{0u};
inline std::atomic<int32_t>  g_init_exit_state{INT32_MIN};

// LoadMainMenu iterator.
inline std::atomic<bool>     g_menu_seen{false};
inline std::atomic<bool>     g_menu_tail_done{false};
inline std::atomic<uint64_t> g_menu_enter_ms{0u};
inline std::atomic<int32_t>  g_menu_enter_state{INT32_MIN};
inline std::atomic<uint64_t> g_menu_exit_ms{0u};
inline std::atomic<int32_t>  g_menu_exit_state{INT32_MIN};
inline std::atomic<int32_t>  g_menu_logged_state{INT32_MIN};

// Shared.
inline std::atomic<uint64_t> g_pump_ms{0u};
inline std::atomic<uint64_t> g_report_ms{0u};
inline std::atomic<bool>     g_watchdog_running{false};

inline void* watchdog_main(void*) {
    for (;;) {
        usleep(static_cast<useconds_t>(kPollMs * 1000u));
        if (g_menu_tail_done.load(std::memory_order_relaxed)) continue;

        const uint64_t now      = opg3d_log::monotonic_ms();
        const uint64_t reported = g_report_ms.load(std::memory_order_relaxed);
        if (reported != 0u && now >= reported &&
            now - reported < kRepeatEveryMs) {
            continue;
        }

        const uint64_t pumped = g_pump_ms.load(std::memory_order_relaxed);
        const uint64_t since_pump =
            (pumped != 0u && now >= pumped) ? now - pumped : 0u;

        if (!g_menu_seen.load(std::memory_order_relaxed)) {
            const uint64_t entered =
                g_init_enter_ms.load(std::memory_order_relaxed);
            if (entered != 0u && now >= entered &&
                now - entered >= kStuckAfterMs) {
                g_report_ms.store(now, std::memory_order_relaxed);
                LOGE("23.1.3-stall: blocked -- InitializeSwitcher.MoveNext "
                     "entered at state=%d has not returned for %" PRIu64
                     " ms; the game thread is stuck inside stock 23.1.3 code. "
                     "Add that state to OPG3D_2313_INIT_SKIP_STATES",
                     g_init_enter_state.load(std::memory_order_relaxed),
                     now - entered);
                continue;
            }

            const uint64_t exited =
                g_init_exit_ms.load(std::memory_order_relaxed);
            if (exited != 0u && now >= exited &&
                now - exited >= kStuckAfterMs) {
                g_report_ms.store(now, std::memory_order_relaxed);
                LOGW("23.1.3-stall: not pumped -- no InitializeSwitcher "
                     "MoveNext for %" PRIu64 " ms after state=%d (last "
                     "Switcher.Start tick %" PRIu64 " ms ago); the loading "
                     "coroutine is no longer scheduled",
                     now - exited,
                     g_init_exit_state.load(std::memory_order_relaxed),
                     since_pump);
            }
            continue;
        }

        const uint64_t entered = g_menu_enter_ms.load(std::memory_order_relaxed);
        if (entered != 0u && now >= entered && now - entered >= kStuckAfterMs) {
            g_report_ms.store(now, std::memory_order_relaxed);
            LOGE("23.1.3-stall: blocked -- LoadMainMenu.MoveNext entered at "
                 "state=%d has not returned for %" PRIu64 " ms",
                 g_menu_enter_state.load(std::memory_order_relaxed),
                 now - entered);
            continue;
        }

        const uint64_t exited = g_menu_exit_ms.load(std::memory_order_relaxed);
        if (exited != 0u && now >= exited && now - exited >= kStuckAfterMs) {
            g_report_ms.store(now, std::memory_order_relaxed);
            LOGE("23.1.3-stall: aborted -- LoadMainMenu.MoveNext returned from "
                 "state=%d %" PRIu64 " ms ago and is never called again (last "
                 "Switcher.Start tick %" PRIu64 " ms ago; the iterator never "
                 "reached its tail, so no scene load was requested). libopg3d "
                 "is built with -fno-exceptions, so a managed exception thrown "
                 "by the stock tail unwinds through the hook: capture an "
                 "unfiltered logcat to see the Unity stack trace",
                 g_menu_exit_state.load(std::memory_order_relaxed),
                 now - exited, since_pump);
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

// Around the stock InitializeSwitcher MoveNext.
inline void note_init_enter(int32_t state) {
    detail::g_init_enter_state.store(state, std::memory_order_relaxed);
    detail::g_init_enter_ms.store(opg3d_log::monotonic_ms(),
                                  std::memory_order_relaxed);
}

inline void note_init_exit(int32_t state, bool result) {
    detail::g_init_enter_ms.store(0u, std::memory_order_relaxed);
    detail::g_init_exit_state.store(state, std::memory_order_relaxed);
    detail::g_init_exit_ms.store(opg3d_log::monotonic_ms(),
                                 std::memory_order_relaxed);
    if (!result) {
        LOGI("23.1.3-stall: InitializeSwitcher finished on its own at state=%d "
             "(%u step(s) skipped)",
             state, detail::g_skipped.load(std::memory_order_relaxed));
    }
}

// Returns true when the caller must NOT enter the stock MoveNext body.
// *resume_state receives the state written into the iterator: state + 1 for a
// skipped step (the hook must then report a yield) or -1 when the whole
// iterator is finished (the hook must report completion).
inline bool should_skip_step(void* self, int32_t state, int32_t* resume_state) {
    if (resume_state != nullptr) *resume_state = state;
    if (detail::kBypassMode == 0) return false;
    if (self == nullptr || !detail::is_skip_state(state)) return false;

    const int32_t resume =
        detail::kBypassMode == 2 ? detail::kFinishedState : state + 1;

    const uint8_t rancho =
        detail::read_at<uint8_t>(self, detail::kRanchoOffset, 0u);
    void* const switcher =
        detail::read_at<void*>(self, detail::kThisOffset, nullptr);
    const float min_sec =
        detail::read_at<float>(switcher, detail::kMinSecOffset, -1.0f);
    const float bar =
        detail::read_at<float>(switcher, detail::kBarOffset, -1.0f);

    detail::write_state(self, resume);
    detail::g_init_enter_ms.store(0u, std::memory_order_relaxed);
    detail::g_init_exit_state.store(state, std::memory_order_relaxed);
    detail::g_init_exit_ms.store(opg3d_log::monotonic_ms(),
                                 std::memory_order_relaxed);

    const uint32_t count = detail::g_skipped.fetch_add(1u) + 1u;
    if (count <= 2u) {
        if (resume >= 0) {
            LOGW("23.1.3-stall: skipping InitializeSwitcher state=%d #%u "
                 "(ranchoOk=%d minSec=%.3f bar=%.3f) -- that step never "
                 "returns while the 23.1.3 backend is retired; state machine "
                 "resumed at %d so every remaining step still runs",
                 state, count, rancho ? 1 : 0, min_sec, bar, resume);
        } else {
            LOGW("23.1.3-stall: finishing the InitializeSwitcher iterator at "
                 "state=%d #%u (mode 2; states above %d will not run and the "
                 "LoadMainMenu tail is known to fail that way)",
                 state, count, state);
        }
    }

    if (resume_state != nullptr) *resume_state = resume;
    return true;
}

// Before the stock LoadMainMenu MoveNext. Returns true when the caller should
// log a pre-call line, i.e. only when the state actually changed -- that line
// is the last evidence left if the stock tail throws.
inline bool note_menu_enter(void* self, int32_t state) {
    (void)self;
    detail::g_menu_enter_state.store(state, std::memory_order_relaxed);
    detail::g_menu_enter_ms.store(opg3d_log::monotonic_ms(),
                                  std::memory_order_relaxed);

    if (!detail::g_menu_seen.exchange(true)) {
        LOGI("23.1.3-stall: LoadMainMenu is running (%u skipped step(s)); "
             "watching the menu iterator now",
             detail::g_skipped.load(std::memory_order_relaxed));
    }

    return detail::g_menu_logged_state.exchange(state) != state;
}

// After the stock LoadMainMenu MoveNext. The tail requests the main-menu scene
// load and only then advances <>1__state to 2, and that scene load destroys the
// Switcher together with every coroutine it was running. Detect the handover
// here, on the state the iterator holds *after* the call: waiting for another
// MoveNext entry would report a false abort forever.
inline void note_menu_exit(void* self, int32_t state, bool result) {
    const int32_t after =
        detail::read_at<int32_t>(self, detail::kStateOffset, state);

    detail::g_menu_enter_ms.store(0u, std::memory_order_relaxed);
    detail::g_menu_exit_state.store(after, std::memory_order_relaxed);
    detail::g_menu_exit_ms.store(opg3d_log::monotonic_ms(),
                                 std::memory_order_relaxed);

    if ((after >= detail::kMenuTailState || !result) &&
        !detail::g_menu_tail_done.exchange(true)) {
        LOGI("23.1.3-stall: LoadMainMenu tail completed (state %d -> %d, "
             "result=%d); the main-menu scene load is requested and the "
             "Switcher is about to be destroyed with its scene, so the "
             "loading watchdog is disarmed",
             state, after, result ? 1 : 0);
    }
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
    LOGI("23.1.3-stall: loading watchdog armed (mode=%d, %zu skip state(s), "
         "first=%d, stall reported after %" PRIu64 " ms)",
         detail::kBypassMode,
         sizeof(detail::kSkipStates) / sizeof(detail::kSkipStates[0]),
         detail::kSkipStates[0], detail::kStuckAfterMs);
    return true;
}

} // namespace loading_stall_guard_2313
