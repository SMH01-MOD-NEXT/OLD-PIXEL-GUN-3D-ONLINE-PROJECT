#pragma once

#include <atomic>
#include <cinttypes>
#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Local authorization bootstrap for the supplied PG3D 23.1.3 ARM64 binary
// (libil2cpp.so SHA-256
// f0a130c4e8d9487059eab4b0f08462f6aa7d057510cada0fd6fb3043c77deb5c).
//
// Obfuscated names are version-local lookup keys only. Their roles were mapped
// from 16.1.0 -> 21.2.3 -> 23.1.3 by method order/signature, iterator state
// machines and native call graph. No ARM32 RVA, opcode or field offset is used.
//
// Awake stays stock. Start's retired transport route is suppressed and the
// controller's own successful-completion IEnumerator is scheduled through
// Unity's uniquely named StartCoroutine_Auto(IEnumerator) overload. The stock
// completion state machine publishes the auth flags and chooses tutorial/menu.
//
// Status after logcat_2026-08-22_17-54-21.txt: the loading screen now finishes
// and the LoadMainMenu tail hands over to this scene (first
// AuthSceneController.Awake of the whole boot at +11616 ms), but the completion
// transaction parks:
//
//   +011726ms suppressing retired Auth Start transport (before=0/Initial)
//   +011726ms ignored retired version gate for the local transaction
//   +011726ms stock completion coroutine accepted (0x6c395e0360)
//   +015000ms auth scene destroyed before a ready session was observed
//   +015003ms AuthSceneController.Awake  (scene reloaded itself)
//   +025039ms completion still pending after 300 frames (state=0/Initial ready=1)
//
// The session-ready flag is already 1 while the state never leaves Initial, so
// publish_if_ready() can never fire and the scene restarts in a loop. Several
// sibling completion iterators of this controller await Task<HashSet<string>>
// remote-slot lookups, which is the prime suspect for a transaction that hangs
// with the backend retired. Until that is proven, this file only adds
// visibility: every state/ready transition, the lifetime of each auth scene
// instance and the restart count are logged so one capture is enough to see
// where the stock machine parks. No behaviour is guessed.
namespace backend_local_2313 {
namespace detail {

using MethodInfo = void;
using InstanceVoidFn = void (*)(void* self, const MethodInfo* method);
using InstanceBoolFn = bool (*)(void* self, const MethodInfo* method);
using IteratorFactoryFn = void* (*)(void* self, const MethodInfo* method);
using StartCoroutineFn = void* (*)(void* self, void* iterator,
                                   const MethodInfo* method);
using StaticGetIntFn = int32_t (*)(const MethodInfo* method);
using StaticGetBoolFn = bool (*)(const MethodInfo* method);

inline constexpr const char* kCompletionIteratorMethod =
    u8"\u4e07\u4e15\u4e02\u4e11\u4e04\u4e16\u4e08\u4e01\u4e0c";
inline constexpr const char* kCompletionBlockerMethod =
    u8"\u4e11\u4e03\u4e0f\u4e09\u4e0c\u4e15\u4e1e\u4e0d\u4e19";
inline constexpr const char* kStateGetterMethod =
    u8"\u4e12\u4e0e\u4e14\u4e19\u4e1a\u4e01\u4e0f\u4e01\u4e09";
inline constexpr const char* kSessionReadyGetterMethod =
    u8"\u4e04\u4e0d\u4e18\u4e1d\u4e1a\u4e14\u4e04\u4e01\u4e16";

inline constexpr int32_t kFullySynchronized = 3;
inline constexpr int32_t kEmpty = 4;
inline constexpr uint32_t kTimeoutFrames = 3600u;

inline InstanceVoidFn g_auth_start = nullptr;
inline InstanceVoidFn g_auth_update = nullptr;
inline InstanceVoidFn g_auth_destroy = nullptr;
inline IteratorFactoryFn g_completion_iterator = nullptr;
inline const MethodInfo* g_mi_completion_iterator = nullptr;
inline StartCoroutineFn g_start_coroutine = nullptr;
inline const MethodInfo* g_mi_start_coroutine = nullptr;
inline InstanceBoolFn g_completion_blocker = nullptr;
inline StaticGetIntFn g_get_state = nullptr;
inline const MethodInfo* g_mi_get_state = nullptr;
inline StaticGetBoolFn g_get_session_ready = nullptr;
inline const MethodInfo* g_mi_get_session_ready = nullptr;

inline thread_local bool g_starting_locally = false;
inline std::atomic<bool> g_local_transaction{false};
inline std::atomic<bool> g_runtime_ready{false};
inline std::atomic<bool> g_gate_logged{false};
inline std::atomic<uint32_t> g_transaction_frames{0u};

// Diagnostics for the auth-scene restart loop.
inline std::atomic<uint32_t> g_scene_instances{0u};
inline std::atomic<uint64_t> g_start_ms{0u};
inline std::atomic<int32_t> g_traced_state{INT32_MIN};
inline std::atomic<int32_t> g_traced_ready{-1};

template <typename Fn>
void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

bool resolve_call(const hook::ManagedMethod& target, void** out_fn,
                  const MethodInfo** out_mi) {
    void* info = il2cpp::find_method_info(target.namespaze, target.klass,
                                          target.method, target.args_count);
    void* pointer = info != nullptr ? il2cpp::method_pointer(info) : nullptr;
    if (info == nullptr || pointer == nullptr) {
        LOGE("23.1.3-local-backend: cannot resolve %s.%s/%d", target.klass,
             target.method, target.args_count);
        return false;
    }
    *out_fn = pointer;
    *out_mi = info;
    return true;
}

const char* state_name(int32_t state) {
    switch (state) {
        case -1: return "None";
        case 0: return "Initial";
        case 1: return "Authorizing";
        case 2: return "Authorized";
        case 3: return "FullySynchronized";
        case 4: return "Empty";
        case 5: return "CheckBindedId";
        case 6: return "ChooseId";
        case 7: return "ChooseProgress";
        case 8: return "SendCachedCommands";
        case 9: return "SynchronizeProgress";
        case 10: return "CheckAppVersion";
        case 11: return "Easy";
        case 12: return "CheckConnection";
        case 13: return "SendProgress";
        case 14: return "WaitAsync";
        case 15: return "TechnicalWorks";
        case 16: return "Login";
        default: return "Unknown";
    }
}

int32_t auth_state() {
    return g_get_state != nullptr && g_mi_get_state != nullptr
               ? g_get_state(g_mi_get_state)
               : -999;
}

bool session_ready_flag() {
    return g_get_session_ready != nullptr &&
                   g_mi_get_session_ready != nullptr
               ? g_get_session_ready(g_mi_get_session_ready)
               : false;
}

// Logs only on an actual transition, so a parked state machine costs one line
// instead of one line per frame.
void trace_transition(const char* where, uint32_t frames) {
    const int32_t state = auth_state();
    const int32_t ready = session_ready_flag() ? 1 : 0;
    const int32_t last_state =
        g_traced_state.exchange(state, std::memory_order_relaxed);
    const int32_t last_ready =
        g_traced_ready.exchange(ready, std::memory_order_relaxed);
    if (state == last_state && ready == last_ready) return;
    LOGI("23.1.3-local-backend: %s state=%d/%s ready=%d (was %d/%s ready=%d) "
         "after %u frame(s)",
         where, state, state_name(state), ready, last_state,
         state_name(last_state), last_ready, frames);
}

bool publish_if_ready() {
    const int32_t state = auth_state();
    const bool ready = session_ready_flag();
    const bool healthy = ready &&
                         (state == kFullySynchronized || state == kEmpty);
    if (!healthy) return false;

    const bool first = !g_runtime_ready.exchange(true, std::memory_order_acq_rel);
    g_local_transaction.store(false, std::memory_order_release);
    if (first) {
        LOGI("23.1.3-local-backend: stock completion published a usable "
             "session (state=%d/%s readyFlag=1)", state, state_name(state));
    }
    return true;
}

// The completion coroutine asks a VersionBlocker-backed predicate before it
// can publish FullySynchronized. Ignore that retired backend decision only
// while the one local completion transaction is active.
bool hook_completion_blocker(void* self, const MethodInfo* method) {
    if (g_local_transaction.load(std::memory_order_acquire)) {
        if (!g_gate_logged.exchange(true, std::memory_order_relaxed)) {
            LOGI("23.1.3-local-backend: ignored retired version gate only for "
                 "the local completion transaction");
        }
        return false;
    }
    return g_completion_blocker != nullptr
               ? g_completion_blocker(self, method)
               : true;
}

void hook_auth_start(void* self, const MethodInfo* method) {
    (void)method;
    if (self == nullptr || g_completion_iterator == nullptr ||
        g_mi_completion_iterator == nullptr || g_start_coroutine == nullptr ||
        g_mi_start_coroutine == nullptr) {
        LOGE("23.1.3-local-backend: completion dependencies missing; "
             "retired transport remains blocked");
        return;
    }
    if (g_starting_locally) return;

    bool expected = false;
    if (!g_local_transaction.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        LOGW("23.1.3-local-backend: duplicate AuthSceneController.Start "
             "ignored while local completion is active");
        return;
    }

    g_starting_locally = true;
    g_transaction_frames.store(0u, std::memory_order_relaxed);
    g_gate_logged.store(false, std::memory_order_relaxed);
    g_traced_state.store(INT32_MIN, std::memory_order_relaxed);
    g_traced_ready.store(-1, std::memory_order_relaxed);

    const uint64_t now = opg3d_log::monotonic_ms();
    const uint64_t previous = g_start_ms.exchange(now, std::memory_order_relaxed);
    const uint32_t instance = g_scene_instances.fetch_add(1u) + 1u;
    if (instance > 1u) {
        LOGE("23.1.3-local-backend: auth scene restarted (instance #%u) %" PRIu64
             " ms after the previous Start; the previous completion never "
             "published a session",
             instance, previous != 0u && now >= previous ? now - previous : 0u);
    }

    const int32_t before = auth_state();
    LOGW("23.1.3-local-backend: suppressing retired Auth Start transport; "
         "scheduling mapped stock completion (before=%d/%s)",
         before, state_name(before));

    void* iterator = g_completion_iterator(self, g_mi_completion_iterator);
    if (iterator == nullptr) {
        g_local_transaction.store(false, std::memory_order_release);
        g_starting_locally = false;
        LOGE("23.1.3-local-backend: stock completion returned null iterator");
        return;
    }

    void* coroutine = g_start_coroutine(self, iterator, g_mi_start_coroutine);
    g_starting_locally = false;
    if (coroutine == nullptr) {
        g_local_transaction.store(false, std::memory_order_release);
        LOGE("23.1.3-local-backend: Unity rejected the completion iterator");
        return;
    }

    LOGI("23.1.3-local-backend: stock completion coroutine accepted (%p)",
         coroutine);
    trace_transition("completion scheduled;", 0u);
    (void)publish_if_ready();
}

void hook_auth_update(void* self, const MethodInfo* method) {
    if (g_auth_update != nullptr) g_auth_update(self, method);
    if (!g_local_transaction.load(std::memory_order_acquire)) return;
    if (publish_if_ready()) return;

    const uint32_t frames =
        g_transaction_frames.fetch_add(1u, std::memory_order_relaxed) + 1u;

    // One line per real transition of the stock machine; silent while parked.
    trace_transition("completion advanced to", frames);

    if (frames == 300u) {
        const int32_t state = auth_state();
        LOGW("23.1.3-local-backend: completion still pending after 300 frames "
             "(state=%d/%s ready=%d)", state, state_name(state),
             session_ready_flag() ? 1 : 0);
    }
    if (frames >= kTimeoutFrames) {
        g_local_transaction.store(false, std::memory_order_release);
        const int32_t state = auth_state();
        LOGE("23.1.3-local-backend: completion timed out fail-closed after "
             "%u frames (state=%d/%s ready=%d)", frames, state,
             state_name(state), session_ready_flag() ? 1 : 0);
    }
}

void hook_auth_destroy(void* self, const MethodInfo* method) {
    if (g_local_transaction.load(std::memory_order_acquire)) {
        (void)publish_if_ready();
    }
    if (g_auth_destroy != nullptr) g_auth_destroy(self, method);
    const bool was_active =
        g_local_transaction.exchange(false, std::memory_order_acq_rel);
    if (was_active && !g_runtime_ready.load(std::memory_order_acquire)) {
        const uint64_t now = opg3d_log::monotonic_ms();
        const uint64_t started = g_start_ms.load(std::memory_order_relaxed);
        const int32_t state = auth_state();
        LOGE("23.1.3-local-backend: auth scene destroyed before a ready "
             "session was observed -- %" PRIu64 " ms and %u frame(s) after "
             "Start, still state=%d/%s ready=%d",
             started != 0u && now >= started ? now - started : 0u,
             g_transaction_frames.load(std::memory_order_relaxed), state,
             state_name(state), session_ready_flag() ? 1 : 0);
    }
}

} // namespace detail

inline bool runtime_ready() {
    return detail::g_runtime_ready.load(std::memory_order_acquire);
}

inline bool install_hooks() {
#if defined(__ANDROID__)
    static_assert(sizeof(void*) == 8,
                  "PG3D 23.1.3 target must be arm64-v8a");
#endif

    bool resolved = detail::resolve_call(
        {"", "AuthSceneController", detail::kCompletionIteratorMethod, 0},
        reinterpret_cast<void**>(&detail::g_completion_iterator),
        &detail::g_mi_completion_iterator);
    resolved &= detail::resolve_call(
        {"", "AuthSceneController", detail::kStateGetterMethod, 0},
        reinterpret_cast<void**>(&detail::g_get_state),
        &detail::g_mi_get_state);
    resolved &= detail::resolve_call(
        {"", "AuthSceneController", detail::kSessionReadyGetterMethod, 0},
        reinterpret_cast<void**>(&detail::g_get_session_ready),
        &detail::g_mi_get_session_ready);
    // StartCoroutine has two one-argument overloads. The obsolete _Auto entry
    // has a unique name and the exact IEnumerator ABI, avoiding an ambiguous
    // args-count-only lookup.
    resolved &= detail::resolve_call(
        {"UnityEngine", "MonoBehaviour", "StartCoroutine_Auto", 1},
        reinterpret_cast<void**>(&detail::g_start_coroutine),
        &detail::g_mi_start_coroutine);
    if (!resolved) {
        LOGE("23.1.3-local-backend: completion metadata incomplete");
        return false;
    }

    const bool blocker = hook::install(
        {"", "AuthSceneController", detail::kCompletionBlockerMethod, 0},
        detail::replacement(&detail::hook_completion_blocker),
        detail::original_slot(&detail::g_completion_blocker), true);
    const bool update = blocker && hook::install(
        {"", "AuthSceneController", "Update", 0},
        detail::replacement(&detail::hook_auth_update),
        detail::original_slot(&detail::g_auth_update), true);
    const bool destroy = update && hook::install(
        {"", "AuthSceneController", "OnDestroy", 0},
        detail::replacement(&detail::hook_auth_destroy),
        detail::original_slot(&detail::g_auth_destroy), true);
    // Start is last: no local transaction can begin with a missing dependency.
    const bool start = destroy && hook::install(
        {"", "AuthSceneController", "Start", 0},
        detail::replacement(&detail::hook_auth_start),
        detail::original_slot(&detail::g_auth_start), true);
    if (!blocker || !update || !destroy || !start) {
        LOGE("23.1.3-local-backend: hooks incomplete (blocker=%d update=%d "
             "destroy=%d start=%d)", blocker ? 1 : 0, update ? 1 : 0,
             destroy ? 1 : 0, start ? 1 : 0);
        return false;
    }

    LOGI("23.1.3-local-backend: armed mapped stock completion coroutine; "
         "offline callback and retired auth transport are not used");
    return true;
}

} // namespace backend_local_2313
