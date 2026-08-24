#pragma once

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <utility>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Passive diagnostics for the 23.1.3 end-of-match screen.
//
// Symptom: after a match the screen shows only the "team won" caption, with no
// player table, no buttons and no rewards. The screen is not frozen; after
// 10-20 seconds it closes by itself and the game continues.
//
// The post-match flow lives in NetworkStartTableNGUIController (dump2313.cs
// line 50731, TypeDefIndex 1175, namespace empty - verified, not Rilisoft).
//
// ---------------------------------------------------------------------------
// Revision 1 traced the thirteen public no-argument nodes of that class. The
// device log answered the question it was built to answer, and the answer was
// that the class splits cleanly in two:
//
//   fired:     ShowStartInterface, OnTablesShow, OnMatchEndAnimationDone,
//              OnTablesShown, OnTrophyAnimationDone, OnRewardShow,
//              OnRewardAnimationEnds
//   never:     CanShowNextReward, StartTrophyAnim, OnHideTrophy,
//              OnTrophyOkButtonPress, OnCWViewShow,
//              HandleContinue_GoToLobbyButton
//
// tools/find_callers.py explains that split exactly. Every node that fired has
// ZERO call sites in libil2cpp.so, i.e. it is invoked by name from a Unity
// animation event. Every node that never fired is either a button handler or is
// called from inside a coroutine body:
//
//   CanShowNextReward  <- 3 call sites, all in reward-queue MoveNext bodies
//   OnCWViewShow       <- 1 call site, results coroutine MoveNext +0x1FF0
//
// So the animation timeline of the end screen runs to completion while the
// data-driven half never runs. OnTrophyAnimationDone firing without any
// preceding StartTrophyAnim is the same finding from the other side: the clip
// reports "animation finished" for a trophy that was never set up.
//
// The data-driven half is one big coroutine, NetworkStartTableNGUIController
// .一丗丈丞丑丐丗三丆 (RVA 0x484F130, iterator 世丟丈丄丙丒专丘丂, MoveNext 0x4E61894). It
// takes the entire match result as ~30 arguments (added experience, coins,
// gems, rating change, winner, clan currency, tournament and pixel-pass
// values, VIP rewards) and it is the code that fills the labels, switches the
// end-of-match panels through 三丕丟丅丐丕丆丘万(bool, bool) at +0x16A8, drives
// RewardWindowController and calls TrophyMagicAnimation.SetValues. If it never
// runs, the result is precisely the reported symptom.
//
// Its three call sites are NetworkStartTable.丝丕世丂世丑东丌下 (+0x744) plus two
// wrappers inside the controller itself, so revision 2 instruments the whole
// driver side instead of only the animation side. What the next log decides:
//
//   no 丝丕世丂世丑东丌下 and no wrapper       -> the result payload never arrives,
//                                         the retired backend owes it
//   entry fires, coroutine never starts -> the entry bails out before
//                                         StartCoroutine
//   coroutine starts and stops at a
//   state, panel switch never reached   -> that state's yield never completes
//
// Still passive on purpose: these wrappers only log and always delegate. No
// reward, panel, button or callback is fabricated or suppressed, and the
// payload fields are read, never written. RVAs are recorded for review only;
// every target is resolved by metadata name at runtime, so a different build
// fails to hook instead of patching a wrong address:
//   OnMatchEndAnimationDone 0x484FDA4   ShowStartInterface     0x484E65C
//   OnTablesShow            0x484FDFC   OnTablesShown          0x485247C
//   OnRewardShow            0x484FF90   CanShowNextReward      0x4850138
//   OnRewardAnimationEnds   0x4856704   StartTrophyAnim        0x4850400
//   OnTrophyAnimationDone   0x484FB3C   OnHideTrophy           0x484FE04
//   OnTrophyOkButtonPress   0x4856790   OnCWViewShow           0x484FEA8
//   HandleContinue_GoToLobbyButton      0x4846310
//   NetworkStartTable.丝丕世丂世丑东丌下      0x475C7C4  (int, int[])
//   controller.丏丟一丒世东丄下丈             0x484F094  (string, ratingChange)
//   controller.丅丆丟丙七丐丒七与             0x484F594  (object, object)
//   controller.三丕丟丅丐丕丆丘万             0x484DD44  (bool, bool)
//   results iterator MoveNext           0x4E61894
//   reward-queue iterator MoveNext      0x4E64414 and three siblings
namespace post_match_trace_2313 {
namespace detail {

using MethodInfo = void;
using InstanceVoidFn = void (*)(void*, const MethodInfo*);
using MoveNextFn = bool (*)(void*, const MethodInfo*);
using ResultsEntryFn = void (*)(void*, int32_t, void*, const MethodInfo*);
using TwoObjectFn = void (*)(void*, void*, void*, const MethodInfo*);
using TwoBoolFn = void (*)(void*, bool, bool, const MethodInfo*);

inline constexpr const char* kGlobalNs = "";
inline constexpr const char* kController = "NetworkStartTableNGUIController";
inline constexpr const char* kTable = "NetworkStartTable";

// Obfuscated members of the driver side, taken verbatim from dump2313.cs.
inline constexpr const char* kResultsEntry = u8"\u4e1d\u4e15\u4e16\u4e02\u4e16\u4e11\u4e1c\u4e0c\u4e0b";
inline constexpr const char* kResultsWrapperA = u8"\u4e0f\u4e1f\u4e00\u4e12\u4e16\u4e1c\u4e04\u4e0b\u4e08";
inline constexpr const char* kResultsWrapperB = u8"\u4e05\u4e06\u4e1f\u4e19\u4e03\u4e10\u4e12\u4e03\u4e0e";
inline constexpr const char* kPanelSwitch = u8"\u4e09\u4e15\u4e1f\u4e05\u4e10\u4e15\u4e06\u4e18\u4e07";

// Nested iterator types. il2cpp_class_from_name only sees top-level types, so
// the Outer/Nested spelling that il2cpp::find_class resolves is used here. The
// whole literal is one UTF-8 literal: mixing "" and u8"" is not portable.
inline constexpr const char* kResultsIterator =
    u8"NetworkStartTableNGUIController/\u4e16\u4e1f\u4e08\u4e04\u4e19\u4e12\u4e13\u4e18\u4e02";

inline constexpr const char* kStateField = "<>1__state";

// ---------------------------------------------------------------------------
// Animation-driven nodes (revision 1, unchanged).
// ---------------------------------------------------------------------------

// Ordered along the expected runtime sequence so a truncated log reads top to
// bottom as "how far the match-end flow got".
inline constexpr const char* kNodes[] = {
    "OnMatchEndAnimationDone",
    "ShowStartInterface",
    "OnTablesShow",
    "OnTablesShown",
    "OnRewardShow",
    "CanShowNextReward",
    "OnRewardAnimationEnds",
    "StartTrophyAnim",
    "OnTrophyAnimationDone",
    "OnHideTrophy",
    "OnTrophyOkButtonPress",
    "OnCWViewShow",
    "HandleContinue_GoToLobbyButton",
};
inline constexpr size_t kNodeCount = sizeof(kNodes) / sizeof(kNodes[0]);

// Reward-queue coroutines. All three call CanShowNextReward, and the fourth is
// the delay helper that starts the queue, so a missing CanShowNextReward can be
// attributed to a specific queue instead of guessed at.
inline constexpr const char* kQueueIterators[] = {
    u8"NetworkStartTableNGUIController/\u4e1b\u4e0c\u4e19\u4e1a\u4e14\u4e1a\u4e16\u4e09\u4e0e",
    u8"NetworkStartTableNGUIController/\u4e0e\u4e16\u4e12\u4e0a\u4e14\u4e19\u4e0d\u4e06\u4e1c",
    u8"NetworkStartTableNGUIController/\u4e15\u4e15\u4e18\u4e0b\u4e18\u4e03\u4e0a\u4e0d\u4e15",
    u8"NetworkStartTableNGUIController/\u4e0a\u4e00\u4e0e\u4e00\u4e0c\u4e0a\u4e06\u4e14\u4e16",
};
inline constexpr size_t kQueueCount =
    sizeof(kQueueIterators) / sizeof(kQueueIterators[0]);

// Short labels for the log; the obfuscated names are unreadable in a log line.
inline constexpr const char* kQueueLabels[kQueueCount] = {
    "reward-delay",
    "reward-queue-a",
    "reward-queue-b",
    "reward-queue-c",
};

inline InstanceVoidFn g_original[kNodeCount] = {};
inline uint32_t g_hits[kNodeCount] = {};

inline ResultsEntryFn g_results_entry = nullptr;
inline TwoObjectFn g_results_wrapper_a = nullptr;
inline TwoObjectFn g_results_wrapper_b = nullptr;
inline TwoBoolFn g_panel_switch = nullptr;
inline MoveNextFn g_results_move_next = nullptr;
inline MoveNextFn g_queue_move_next[kQueueCount] = {};

// A yielding coroutine is stepped every frame, so only transitions are logged.
// INT32_MIN cannot be a real iterator state, which makes it a safe "nothing
// seen yet" marker.
inline constexpr int32_t kNoState = INT32_MIN;
inline int32_t g_results_state = kNoState;
inline int32_t g_queue_state[kQueueCount] = {kNoState, kNoState, kNoState,
                                            kNoState};
inline bool g_results_payload_logged = false;

// `finishedInterface` is the only non-obfuscated state flag on the controller,
// so it is the one field worth reporting next to each step.
template <typename T>
bool read_field(void* object, const char* name, T* out) {
    static_assert(sizeof(T) <= 8, "diagnostic field must be scalar/pointer");
    if (object == nullptr || out == nullptr ||
        il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_field_from_name == nullptr ||
        il2cpp::field_get_value == nullptr) return false;
    void* klass = il2cpp::object_get_class(object);
    void* field = klass != nullptr
                      ? il2cpp::class_get_field_from_name(klass, name)
                      : nullptr;
    if (field == nullptr) return false;
    alignas(8) uint8_t scratch[16] = {0};
    il2cpp::field_get_value(object, field, scratch);
    std::memcpy(out, scratch, sizeof(T));
    return true;
}

inline int32_t read_int(void* object, const char* name) {
    int32_t value = 0;
    return read_field(object, name, &value) ? value : -1;
}

inline const char* read_bool(void* object, const char* name) {
    bool value = false;
    if (!read_field(object, name, &value)) return "?";
    return value ? "true" : "false";
}

// One distinct native function per node, so each keeps its own trampoline and
// its own saved original pointer without hand-written copy/paste.
template <size_t Index>
void hook_node(void* self, const MethodInfo* method) {
    static_assert(Index < kNodeCount, "node index out of range");
    const uint32_t hit = ++g_hits[Index];

    bool finished = false;
    const bool have_finished = read_field(self, "finishedInterface", &finished);

    LOGI("23.1.3-post-match: -> %s hit=%" PRIu32 " self=%p "
         "finishedInterface=%s",
         kNodes[Index], hit, self,
         have_finished ? (finished ? "true" : "false") : "?");

    if (g_original[Index] == nullptr) {
        // Unreachable in practice: without a saved original the hook is never
        // installed. Fail loudly rather than silently swallowing a UI step.
        LOGE("23.1.3-post-match: %s has no saved original; step dropped",
             kNodes[Index]);
        return;
    }

    g_original[Index](self, method);

    bool finished_after = false;
    const bool have_after = read_field(self, "finishedInterface", &finished_after);
    LOGI("23.1.3-post-match: <- %s done finishedInterface=%s", kNodes[Index],
         have_after ? (finished_after ? "true" : "false") : "?");
}

// ---------------------------------------------------------------------------
// Driver side (revision 2).
// ---------------------------------------------------------------------------

// NetworkStartTable.丝丕世丂世丑东丌下(int, int[]): the only entry into the results
// flow that lives outside the controller. If this never fires, the match
// result payload never reaches the UI at all.
inline void results_entry_hook(void* self, int32_t winner, void* totals,
                               const MethodInfo* method) {
    LOGI("23.1.3-post-match: -> results entry self=%p winner=%" PRId32
         " totals=%s",
         self, winner, totals != nullptr ? "present" : "NULL");
    if (g_results_entry == nullptr) {
        LOGE("23.1.3-post-match: results entry has no saved original; "
             "the match result would be dropped");
        return;
    }
    g_results_entry(self, winner, totals, method);
    LOGI("23.1.3-post-match: <- results entry done");
}

inline void results_wrapper_a_hook(void* self, void* winner, void* rating,
                                   const MethodInfo* method) {
    LOGI("23.1.3-post-match: -> results wrapper A self=%p winner=%s rating=%s",
         self, winner != nullptr ? "present" : "NULL",
         rating != nullptr ? "present" : "NULL");
    if (g_results_wrapper_a == nullptr) {
        LOGE("23.1.3-post-match: results wrapper A has no saved original");
        return;
    }
    g_results_wrapper_a(self, winner, rating, method);
    LOGI("23.1.3-post-match: <- results wrapper A done");
}

inline void results_wrapper_b_hook(void* self, void* first, void* second,
                                   const MethodInfo* method) {
    LOGI("23.1.3-post-match: -> results wrapper B self=%p a=%s b=%s", self,
         first != nullptr ? "present" : "NULL",
         second != nullptr ? "present" : "NULL");
    if (g_results_wrapper_b == nullptr) {
        LOGE("23.1.3-post-match: results wrapper B has no saved original");
        return;
    }
    g_results_wrapper_b(self, first, second, method);
    LOGI("23.1.3-post-match: <- results wrapper B done");
}

// 三丕丟丅丐丕丆丘万(bool, bool) is the panel switcher and has exactly one call
// site: the results coroutine at +0x16A8. Reaching it proves the coroutine got
// that far; never reaching it bounds the stall to an earlier state.
inline void panel_switch_hook(void* self, bool first, bool second,
                              const MethodInfo* method) {
    LOGI("23.1.3-post-match: -> panel switch self=%p a=%s b=%s", self,
         first ? "true" : "false", second ? "true" : "false");
    if (g_panel_switch == nullptr) {
        LOGE("23.1.3-post-match: panel switch has no saved original; "
             "the end-of-match panels would never be shown");
        return;
    }
    g_panel_switch(self, first, second, method);
    LOGI("23.1.3-post-match: <- panel switch done");
}

// The payload field names on the generated iterator are not obfuscated, so the
// first step can report what the UI was actually asked to display. Reported
// once: this runs every frame while the coroutine lives.
inline void log_results_payload(void* self) {
    if (g_results_payload_logged) return;
    g_results_payload_logged = true;
    LOGI("23.1.3-post-match: results payload exp=%" PRId32 " coins=%" PRId32
         " gems=%" PRId32 " clan=%" PRId32 " winnerCommand=%" PRId32
         " amIWinner=%s firstPlace=%s showAward=%s",
         read_int(self, "_addExpierence"), read_int(self, "_addCoin"),
         read_int(self, "_addGems"), read_int(self, "_addClanCurrency"),
         read_int(self, "_winnerCommand"), read_bool(self, "amIWinner"),
         read_bool(self, "firstPlace"), read_bool(self, "showAward"));
}

inline bool results_move_next_hook(void* self, const MethodInfo* method) {
    if (g_results_move_next == nullptr) {
        LOGE("23.1.3-post-match: results coroutine has no saved original; "
             "refusing to stop it");
        return false;
    }

    log_results_payload(self);

    int32_t before = kNoState;
    const bool have_before = read_field(self, kStateField, &before);
    if (have_before && before != g_results_state) {
        LOGI("23.1.3-post-match: results coroutine state -> %" PRId32, before);
        g_results_state = before;
    }

    const bool running = g_results_move_next(self, method);

    int32_t after = kNoState;
    if (read_field(self, kStateField, &after) && after != g_results_state) {
        LOGI("23.1.3-post-match: results coroutine state %" PRId32
             " -> %" PRId32 " running=%d",
             g_results_state, after, running ? 1 : 0);
        g_results_state = after;
    }
    if (!running) {
        LOGI("23.1.3-post-match: results coroutine finished at state %" PRId32,
             after);
    }
    return running;
}

template <size_t Index>
bool queue_move_next_hook(void* self, const MethodInfo* method) {
    static_assert(Index < kQueueCount, "queue index out of range");
    if (g_queue_move_next[Index] == nullptr) {
        LOGE("23.1.3-post-match: %s has no saved original; refusing to stop it",
             kQueueLabels[Index]);
        return false;
    }

    int32_t before = kNoState;
    if (read_field(self, kStateField, &before) &&
        before != g_queue_state[Index]) {
        LOGI("23.1.3-post-match: %s state -> %" PRId32, kQueueLabels[Index],
             before);
        g_queue_state[Index] = before;
    }

    const bool running = g_queue_move_next[Index](self, method);

    int32_t after = kNoState;
    if (read_field(self, kStateField, &after) &&
        after != g_queue_state[Index]) {
        LOGI("23.1.3-post-match: %s state %" PRId32 " -> %" PRId32
             " running=%d",
             kQueueLabels[Index], g_queue_state[Index], after,
             running ? 1 : 0);
        g_queue_state[Index] = after;
    }
    if (!running) {
        LOGI("23.1.3-post-match: %s finished at state %" PRId32,
             kQueueLabels[Index], after);
    }
    return running;
}

inline bool add(const hook::ManagedMethod& method, void* replacement_pointer,
                void** original_pointer, int* installed) {
    const bool ok = hook::install(method, replacement_pointer,
                                 original_pointer, false);
    if (ok) ++(*installed);
    else
        LOGW("23.1.3-post-match: could not hook %s%s%s.%s", method.namespaze,
             (method.namespaze[0] != '\0') ? "." : "", method.klass,
             method.method);
    return ok;
}

template <size_t... Index>
void install_nodes(std::index_sequence<Index...>, int* installed) {
    (void)std::initializer_list<int>{
        (add({kGlobalNs, kController, kNodes[Index], 0},
             reinterpret_cast<void*>(&hook_node<Index>),
             reinterpret_cast<void**>(&g_original[Index]), installed),
         0)...};
}

template <size_t... Index>
void install_queues(std::index_sequence<Index...>, int* installed) {
    (void)std::initializer_list<int>{
        (add({kGlobalNs, kQueueIterators[Index], "MoveNext", 0},
             reinterpret_cast<void*>(&queue_move_next_hook<Index>),
             reinterpret_cast<void**>(&g_queue_move_next[Index]), installed),
         0)...};
}

} // namespace detail

// Installs the passive trace. Returns true only when the driver side that the
// previous device log proved to be silent is instrumented; without those hooks
// the log cannot answer the question this revision exists to answer.
inline bool install_hooks() {
#if defined(__ANDROID__)
    static_assert(sizeof(void*) == 8,
                  "PG3D 23.1.3 target must be arm64-v8a");
#endif
    using namespace detail;
    int installed = 0;
    const int total =
        static_cast<int>(kNodeCount) + static_cast<int>(kQueueCount) + 5;

    install_nodes(std::make_index_sequence<kNodeCount>{}, &installed);

    add({kGlobalNs, kTable, kResultsEntry, 2},
        reinterpret_cast<void*>(&results_entry_hook),
        reinterpret_cast<void**>(&g_results_entry), &installed);
    add({kGlobalNs, kController, kResultsWrapperA, 2},
        reinterpret_cast<void*>(&results_wrapper_a_hook),
        reinterpret_cast<void**>(&g_results_wrapper_a), &installed);
    add({kGlobalNs, kController, kResultsWrapperB, 2},
        reinterpret_cast<void*>(&results_wrapper_b_hook),
        reinterpret_cast<void**>(&g_results_wrapper_b), &installed);
    add({kGlobalNs, kController, kPanelSwitch, 2},
        reinterpret_cast<void*>(&panel_switch_hook),
        reinterpret_cast<void**>(&g_panel_switch), &installed);
    add({kGlobalNs, kResultsIterator, "MoveNext", 0},
        reinterpret_cast<void*>(&results_move_next_hook),
        reinterpret_cast<void**>(&g_results_move_next), &installed);

    install_queues(std::make_index_sequence<kQueueCount>{}, &installed);

    const bool entry = g_original[0] != nullptr;   // OnMatchEndAnimationDone
    const bool tables = g_original[3] != nullptr;  // OnTablesShown
    const bool results = g_results_entry != nullptr;
    const bool coroutine = g_results_move_next != nullptr;
    const bool panels = g_panel_switch != nullptr;

    LOGI("23.1.3-post-match-trace: installed %d/%d hooks (match-end=%s "
         "tables-shown=%s results-entry=%s results-coroutine=%s "
         "panel-switch=%s)",
         installed, total, entry ? "OK" : "FAILED", tables ? "OK" : "FAILED",
         results ? "OK" : "FAILED", coroutine ? "OK" : "FAILED",
         panels ? "OK" : "FAILED");
    return entry && tables && results && coroutine && panels;
}

} // namespace post_match_trace_2313
