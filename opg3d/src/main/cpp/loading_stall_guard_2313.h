#pragma once

// PG3D 23.1.3 ARM64 -- loading stall guard for the boot sequence.
//
// Two captures from the same build drive this file.  Both come from the PR
// artifact, which has no PHOTON_APP_ID; the AppID is unrelated to either
// stall (it is reported ~400 ms before the first freeze, and Photon is only
// used once the main menu exists).
//
// ---------------------------------------------------------------------------
// capture 1 -- freeze at 90%   (logcat_2026-08-22_16-37-38.txt)
// ---------------------------------------------------------------------------
//   #000119 +010688ms 23.1.3-swt: InitSwitcher state=33 ranchoOk=0 result=1
//   #000120 +010705ms 23.1.3-swt: InitSwitcher state=34 ranchoOk=0 result=1
//   +011.2s  W/Unity DontDestroyOnLoad only works for root GameObjects ...
//              UnityEngine.Object:Internal_InstantiateSingle(...)
//              InfoWindowController:\u4e0b\u4e0c\u4e11\u4e01\u4e0b\u4e1f\u4e1b\u4e18\u4e0a()
//              \u4e1c\u4e03\u4e17\u4e0c\u4e11\u4e05\u4e1e\u4e07\u4e01:MoveNext()
//   ... and then nothing at all for the remaining ~64 s of the capture.
//
// The heartbeat in startup_trace_2313.h is emitted *after* the original
// MoveNext returns, so the call entered at <>1__state == 35 never returned:
// the game thread is blocked inside stock code, not suspended on a yield.
//
// Static cross-check (dump2313.cs + libil2cpp.so, RVA == file offset):
//   * Switcher.\u4e1c\u4e03\u4e17\u4e0c\u4e11\u4e05\u4e1e\u4e07\u4e01.MoveNext = 0x217B040;
//   * its only call to InfoWindowController.\u4e0b\u4e0c\u4e11\u4e01\u4e0b\u4e1f\u4e1b\u4e18\u4e0a()
//     (0x28F7428) sits at 0x217B8A4, inside the block that writes
//     <>1__state = 36 at 0x217B8F8 -- that is the state-35 body.  The same
//     block calls \u4e0b\u4e0b\u4e19\u4e0b\u4e16\u4e1a\u4e09\u4e13\u4e17.\u4e16\u4e10\u4e1a\u4e1b\u4e18\u4e15\u4e0f\u4e0f\u4e0c(enum 6) (0x4C9E4E4,
//     a 0xB8-byte wrapper that tail-calls MonoBehaviour.StartCoroutine) and
//     \u4e1a\u4e16\u4e0b\u4e18\u4e1f\u4e09\u4e0a\u4e0c\u4e1e.\u4e05\u4e1f\u4e1e\u4e07\u4e11\u4e1f\u4e12\u4e10\u4e00(float,float) (0x35AD0BC).
//
// ---------------------------------------------------------------------------
// capture 2 -- freeze at 45% with v1 of this guard
//              (logcat_2026-08-22_17-32-48.txt)
// ---------------------------------------------------------------------------
// v1 ended the whole enumeration at state 35 by writing <>1__state = -1.  The
// 90% block was indeed gone and LoadMainMenu started, but:
//
//   +009014ms 23.1.3-stall: skipping InitializeSwitcher state=35 #1
//   +009014ms 23.1.3-swt:   InitSwitcher state=35 result=0 (guard finished it)
//   +009015ms 23.1.3-swt:   LoadMainMenu state=0 result=1
//   +009048ms 23.1.3-swt:   LoadMainMenu state=1 result=1
//   +011080ms 23.1.3-swt:   LoadMainMenu state=1 result=1  <- last heartbeat
//   +020398ms 23.1.3-startup: skipped ConnectionLostChecker.Update #600
//
// The Update guard still counts at +20 s, so the process and the Unity main
// loop are alive, yet the LoadMainMenu iterator is not pumped any more -- the
// heartbeat is time-based and would print at least every 2 s.  The coroutine
// itself died.
//
// Switcher.\u4e1d\u4e01\u4e09\u4e1f\u4e10\u4e15\u4e01\u4e0e\u4e0f.MoveNext (LoadMainMenu, 0x1F21610) is short:
//
//   state 1, wait loop @0x1F21758:
//     BL    Stopwatch.get_ElapsedMilliseconds
//     LDR   S0, [X24,#0x128]        ; Switcher field 0x128 == 3.0
//     MOVZ  W8, #0x447A, lsl 16     ; 1000.0f
//     SCVTF S1, X0 / FMOV S2, W8 / FDIV S1, S1, S2 / FCMP S1, S0
//     B.mi  0x1F21984               ; elapsed/1000 < 3.0 -> yield, stay in 1
//
//   tail @0x1F217B0 (runs once, then yields with state 2 and finishes):
//     Stopwatch.Stop()
//     GameConnect.\u4e0d\u4e18\u4e0c\u4e19\u4e03\u4e13\u4e13\u4e0f\u4e15(enum 0)              0x14D048C
//     <static bool @0x3A0> = false
//     X20 = <static @0x230>          ; CBZ -> throw NullReference (0x1F219DC)
//     X21 = [X20,#0x78]              ; CBZ -> throw NullReference
//     WeaponManager.\u4e17\u4e18\u4e02\u4e08\u4e04\u4e05\u4e05\u4e0c\u4e07(int)          0x1428A1C
//     <static @0x0>.field_0x70 -> Component.get_gameObject()
//                              -> GameObject.SetActive(false) ; CBZ -> throw
//     \u4e10\u4e18\u4e0c\u4e1e\u4e19\u4e0c\u4e17\u4e1c\u4e0e.\u4e14\u4e07\u4e14\u4e0a\u4e0d\u4e04\u4e1b\u4e19\u4e14(string, ...)      0x4573458  (scene load)
//
// So the 3 s wait expires around +12 s and the tail immediately walks objects
// that the states v1 cut away (36 and up) never created.  libopg3d is built
// with -fno-exceptions on purpose (see CMakeLists.txt), so a managed
// NullReferenceException unwinds straight through the hook frame: Unity drops
// the coroutine, no heartbeat is ever emitted again and the bar stays at 45%.
// That is exactly the shape of capture 2.
//
// ---------------------------------------------------------------------------
// policy
// ---------------------------------------------------------------------------
// Skip the *single* stalling step instead of the whole tail: when MoveNext is
// entered at a listed state, write <>1__state = state + 1 and report a yield
// without running the body.  The state machine keeps every remaining step, so
// the managers the LoadMainMenu tail dereferences are still built.  Mode 2
// restores the v1 behaviour for A/B testing.
//
// The watchdog now covers both iterators and separates three failure modes:
//   * "blocked"    - MoveNext was entered and never returned (stuck thread);
//   * "not pumped" - MoveNext returns but is not called again;
//   * "aborted"    - LoadMainMenu stopped before its tail ran, the signature
//                    of a managed exception escaping the stock code.
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

// Entry states of InitializeSwitcher whose body must not run.  Comma-separated
// list, e.g. -DOPG3D_2313_INIT_SKIP_STATES="35,37".
#ifndef OPG3D_2313_INIT_SKIP_STATES
#define OPG3D_2313_INIT_SKIP_STATES 35
#endif

// 0 = run stock code, watchdog only
// 1 = skip the listed steps, resume the state machine at state + 1 (default)
// 2 = finish the whole iterator at the first listed step (v1; known to break
//     the LoadMainMenu tail, kept for A/B testing)
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

// Switcher instance fields.
// 0x128 is not the progress bar: LoadMainMenu compares it against the loading
// Stopwatch in seconds (it reads 3.000 in every capture).  0x138 is the value
// the iterators yield, i.e. what the bar actually shows.
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
                 "Switcher.Start tick %" PRIu64 " ms ago, scene load never "
                 "requested). libopg3d is built with -fno-exceptions, so a "
                 "managed exception thrown by the stock tail unwinds through "
                 "the hook: capture an unfiltered logcat to see the Unity "
                 "stack trace",
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

// Before the stock LoadMainMenu MoveNext.  Returns true when the caller should
// log a pre-call line, i.e. only when the state actually changed -- that line
// is the last evidence left if the stock tail throws.
inline bool note_menu_enter(int32_t state) {
    detail::g_menu_enter_state.store(state, std::memory_order_relaxed);
    detail::g_menu_enter_ms.store(opg3d_log::monotonic_ms(),
                                  std::memory_order_relaxed);

    if (!detail::g_menu_seen.exchange(true)) {
        LOGI("23.1.3-stall: LoadMainMenu is running (%u skipped step(s)); "
             "watching the menu iterator now",
             detail::g_skipped.load(std::memory_order_relaxed));
    }

    if (state >= detail::kMenuTailState &&
        !detail::g_menu_tail_done.exchange(true)) {
        LOGI("23.1.3-stall: LoadMainMenu tail completed at state=%d (main menu "
             "scene requested); loading watchdog disarmed",
             state);
    }

    return detail::g_menu_logged_state.exchange(state) != state;
}

inline void note_menu_exit(int32_t state, bool result) {
    detail::g_menu_enter_ms.store(0u, std::memory_order_relaxed);
    detail::g_menu_exit_state.store(state, std::memory_order_relaxed);
    detail::g_menu_exit_ms.store(opg3d_log::monotonic_ms(),
                                 std::memory_order_relaxed);
    if (!result) {
        LOGI("23.1.3-stall: LoadMainMenu iterator finished at state=%d", state);
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
