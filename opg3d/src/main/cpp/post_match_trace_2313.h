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
// buttons and no rewards.
//
// The post-match flow lives in NetworkStartTableNGUIController (dump2313.cs
// line 50731, TypeDefIndex 1175). Every node below is declared in that dump as
// `public void X()`, i.e. instance, no arguments, void return, so a single
// InstanceVoidFn signature covers all of them.
//
// Why tracing first instead of a direct fix: the chain is driven by Unity
// Animation Events plus coroutines. OnTablesShown, OnRewardShow and
// OnRewardAnimationEnds have ZERO call sites in libil2cpp.so (verified with
// tools/find_callers.py) because the animation clips invoke them by name. That
// means the stalling link cannot be identified statically from the image, and
// any "fix" written without on-device evidence would be a guess. These
// wrappers only log and always delegate: no reward, button or callback is
// fabricated or suppressed.
//
// RVAs are recorded for review only; every target is resolved by metadata name
// at runtime, so a different build simply fails to hook instead of patching a
// wrong address:
//   OnMatchEndAnimationDone 0x484FDA4   ShowStartInterface     0x484E65C
//   OnTablesShow            0x484FDFC   OnTablesShown          0x485247C
//   OnRewardShow            0x484FF90   CanShowNextReward      0x4850138
//   OnRewardAnimationEnds   0x4856704   StartTrophyAnim        0x4850400
//   OnTrophyAnimationDone   0x484FB3C   OnHideTrophy           0x484FE04
//   OnTrophyOkButtonPress   0x4856790   OnCWViewShow           0x484FEA8
//   HandleContinue_GoToLobbyButton      0x4846310
namespace post_match_trace_2313 {
namespace detail {

using MethodInfo = void;
using InstanceVoidFn = void (*)(void*, const MethodInfo*);

inline constexpr const char* kController = "NetworkStartTableNGUIController";

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

inline InstanceVoidFn g_original[kNodeCount] = {};
inline uint32_t g_hits[kNodeCount] = {};

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

inline bool add(const hook::ManagedMethod& method, void* replacement_pointer,
                void** original_pointer, int* installed) {
    const bool ok = hook::install(method, replacement_pointer,
                                 original_pointer, false);
    if (ok) ++(*installed);
    else LOGW("23.1.3-post-match: could not hook %s.%s", method.klass,
              method.method);
    return ok;
}

template <size_t... Index>
void install_all(std::index_sequence<Index...>, int* installed) {
    (void)std::initializer_list<int>{
        (add({"", kController, kNodes[Index], 0},
             reinterpret_cast<void*>(&hook_node<Index>),
             reinterpret_cast<void**>(&g_original[Index]), installed),
         0)...};
}

} // namespace detail

// Installs the passive trace. Returns true when at least the two entry points
// that decide whether the reward flow starts at all are instrumented; without
// those the log cannot answer the question we are asking.
inline bool install_hooks() {
#if defined(__ANDROID__)
    static_assert(sizeof(void*) == 8,
                  "PG3D 23.1.3 target must be arm64-v8a");
#endif
    using namespace detail;
    int installed = 0;
    install_all(std::make_index_sequence<kNodeCount>{}, &installed);

    const bool entry = g_original[0] != nullptr;   // OnMatchEndAnimationDone
    const bool tables = g_original[3] != nullptr;  // OnTablesShown
    LOGI("23.1.3-post-match-trace: installed %d/%zu hooks "
         "(match-end=%s tables-shown=%s)",
         installed, kNodeCount, entry ? "OK" : "FAILED",
         tables ? "OK" : "FAILED");
    return entry && tables;
}

} // namespace post_match_trace_2313
