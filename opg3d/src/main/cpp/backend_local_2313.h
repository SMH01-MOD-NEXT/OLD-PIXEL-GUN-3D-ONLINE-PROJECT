#pragma once

#include <atomic>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Local authorization bootstrap for the supplied PG3D 23.1.3 ARM64 binary
// (libil2cpp.so SHA-256
// f0a130c4e8d9487059eab4b0f08462f6aa7d057510cada0fd6fb3043c77deb5c).
//
// Obfuscated names are version-local lookup keys only. Their roles were mapped
// from the supplied dump.cs plus the A64 call graph of this exact binary. No
// RVA, opcode or field offset is used at runtime: every entry point is resolved
// by metadata name through IL2CPP. The RVAs in the comments are analysis
// references for reviewers, nothing reads them.
// docs/PORT_23_1_3_AUTH_COMPLETION.md carries the full evidence.
//
// Why the boot parked at 90% (logcat_2026-08-22_19-55-26.txt): the previous
// revision suppressed AuthSceneController.Start completely and started the
// completion coroutine by hand. In this build Start (0x3DC1BCC) tail-calls the
// auth state dispatcher (0x3DC1F38) with the static AuthSceneState, and the
// dispatcher is the only thing that moves the state; the completion iterator
// (MoveNext 0x3DC9320) sets the session-ready bool, clears the offline flag,
// commits Storager and drives the loading bar, but never writes the state. So
// the coroutine ran to its end (state 1 -> -1 after 72 frames, awaiting no
// Task), the state stayed 0/Initial forever, publish_if_ready() -- which needs
// ready && (FullySynchronized || Empty) -- could never become true, and the
// transaction parked until the fail-closed timeout while the loading screen
// sat at 90%.
//
// This revision therefore:
//   1. runs the stock Start, so the FpsManager setup, loading bar, coroutine
//      cleanup, UI reset and the dispatcher all happen as in the stock build;
//   2. avoids the retired transport at the branch that selects it, by forcing
//      the persisted local-session gate (0x2B71728) the dispatcher consults to
//      true while, and only while, the local auth transaction is active -- the
//      stock "a local session already exists" route;
//   3. publishes the session explicitly through the controller's own static
//      setters (session-ready = true, AuthSceneState = FullySynchronized) once
//      the coroutine has ended, instead of waiting for a transition that
//      nothing in this build performs;
//   4. keeps every step bounded and fail-soft: hand-scheduling if the
//      dispatcher never created the iterator, one invocation of the stock
//      post-auth continuation if the scene still does not move, and the
//      existing fail-closed timeout.
//
// Mapped 23.1.3 entry points (all resolved by metadata name):
//   AuthSceneController.<completion>()        0x3DC3EE0  iterator factory
//   AuthSceneController.<versionGate>()       0x3DC5F54  blocker predicate
//   AuthSceneController.<getState>()          0x3DC0920
//   AuthSceneController.<setState>(state)     0x3DC0968
//   AuthSceneController.<getSessionReady>()   0x3DC09BC
//   AuthSceneController.<setSessionReady>(b)  0x3DC0A04
//   AuthSceneController.<postAuthContinue>()  0x3DC2CC4  banner + SceneLoader
//   <settings>.<localSessionGate>()           0x2B71728  PlayerPrefs-backed
namespace backend_local_2313 {
namespace detail {

using MethodInfo = void;
using InstanceVoidFn = void (*)(void* self, const MethodInfo* method);
using InstanceBoolFn = bool (*)(void* self, const MethodInfo* method);
using InstanceStateFn = void (*)(void* self, int32_t state,
                                 const MethodInfo* method);
using IteratorFactoryFn = void* (*)(void* self, const MethodInfo* method);
using StartCoroutineFn = void* (*)(void* self, void* iterator,
                                   const MethodInfo* method);
using StaticGetIntFn = int32_t (*)(const MethodInfo* method);
using StaticGetBoolFn = bool (*)(const MethodInfo* method);
using StaticSetIntFn = void (*)(int32_t value, const MethodInfo* method);
using StaticSetBoolFn = void (*)(bool value, const MethodInfo* method);

inline constexpr const char* kCompletionIteratorMethod =
    u8"\u4e07\u4e15\u4e02\u4e11\u4e04\u4e16\u4e08\u4e01\u4e0c";
inline constexpr const char* kCompletionBlockerMethod =
    u8"\u4e11\u4e03\u4e0f\u4e09\u4e0c\u4e15\u4e1e\u4e0d\u4e19";
inline constexpr const char* kStateGetterMethod =
    u8"\u4e12\u4e0e\u4e14\u4e19\u4e1a\u4e01\u4e0f\u4e01\u4e09";
inline constexpr const char* kStateSetterMethod =
    u8"\u4e02\u4e19\u4e1d\u4e12\u4e16\u4e07\u4e13\u4e11\u4e1a";
inline constexpr const char* kSessionReadyGetterMethod =
    u8"\u4e04\u4e0d\u4e18\u4e1d\u4e1a\u4e14\u4e04\u4e01\u4e16";
inline constexpr const char* kSessionReadySetterMethod =
    u8"\u4e19\u4e1a\u4e09\u4e0f\u4e19\u4e19\u4e1e\u4e17\u4e0c";
// Post-auth continuation: NewVersionBanner/VersionBlocker check, then the
// SceneLoader hand-off out of the auth scene.
inline constexpr const char* kSceneContinuationMethod =
    u8"\u4e14\u4e0c\u4e07\u4e18\u4e1a\u4e14\u4e10\u4e05\u4e0d";
// Static settings holder whose PlayerPrefs-backed predicate the auth
// dispatcher consults before it chooses a route.
inline constexpr const char* kLocalSessionGateClass =
    u8"\u4e19\u4e00\u4e1a\u4e09\u4e05\u4e07\u4e02\u4e19\u4e1e";
inline constexpr const char* kLocalSessionGateMethod =
    u8"\u4e14\u4e1c\u4e19\u4e13\u4e11\u4e09\u4e19\u4e06\u4e15";

inline constexpr int32_t kFullySynchronized = 3;
inline constexpr int32_t kEmpty = 4;
inline constexpr int32_t kTechnicalWorks = 15;
// AuthSceneController state dispatcher. Passing TechnicalWorks here creates
// the maintenance window immediately, so guarding only the stored state on a
// later Update tick is too late.
inline constexpr const char* kStateDispatcherMethod =
    u8"丙丟不丗丑下丌丁专"; // void(AuthSceneState), 0x3DC2458

// Frame budget of one local auth transaction, in Update ticks.
inline constexpr uint32_t kScheduleGraceFrames = 60u;    // dispatcher grace
inline constexpr uint32_t kSettleFrames = 30u;           // after MoveNext ends
inline constexpr uint32_t kPublishDeadlineFrames = 300u; // publish regardless
inline constexpr uint32_t kContinuationFrames = 900u;    // stock scene hand-off
inline constexpr uint32_t kTimeoutFrames = 3600u;        // fail closed

// Roslyn iterator fields, resolved by metadata name.
inline constexpr const char* kIteratorStateField = "<>1__state";
inline constexpr const char* kIteratorCurrentField = "<>2__current";
inline constexpr int32_t kIteratorFinished = -1;

// Awaited-task field names used by this controller's completion iterators.
// "<task>5__2" is declared by four different generated classes at three
// different offsets, which is exactly why the lookup goes through the class of
// the live object instead of through a constant.
inline constexpr const char* kAwaitedTaskFields[] = {
    "<getRemoteSlots>5__2",
    "<getRemoteSlotsTask>5__2",
    "<task>5__2",
};

// System.Threading.Tasks.Task.m_stateFlags bits.
inline constexpr int32_t kTaskStarted = 0x00010000;
inline constexpr int32_t kTaskFaulted = 0x00200000;
inline constexpr int32_t kTaskCanceled = 0x00400000;
inline constexpr int32_t kTaskRanToCompletion = 0x01000000;
inline constexpr int32_t kTaskWaitingForActivation = 0x02000000;
inline constexpr int32_t kTaskCompletedMask =
    kTaskFaulted | kTaskCanceled | kTaskRanToCompletion;

// A yielded iterator chain deeper than this is a defect, not a park.
inline constexpr int kMaxChainDepth = 4;
inline constexpr uint32_t kChainReportFrames = 300u;
inline constexpr uint32_t kChainRepeatFrames = 900u;

inline InstanceVoidFn g_auth_start = nullptr;
inline InstanceVoidFn g_auth_update = nullptr;
inline InstanceVoidFn g_auth_destroy = nullptr;
inline IteratorFactoryFn g_completion_iterator = nullptr;
inline const MethodInfo* g_mi_completion_iterator = nullptr;
inline StartCoroutineFn g_start_coroutine = nullptr;
inline const MethodInfo* g_mi_start_coroutine = nullptr;
inline InstanceBoolFn g_completion_blocker = nullptr;
inline InstanceStateFn g_state_dispatcher = nullptr;
inline StaticGetBoolFn g_local_session_gate = nullptr;
inline StaticGetIntFn g_get_state = nullptr;
inline const MethodInfo* g_mi_get_state = nullptr;
inline StaticSetIntFn g_set_state = nullptr;
inline const MethodInfo* g_mi_set_state = nullptr;
inline StaticGetBoolFn g_get_session_ready = nullptr;
inline const MethodInfo* g_mi_get_session_ready = nullptr;
inline StaticSetBoolFn g_set_session_ready = nullptr;
inline const MethodInfo* g_mi_set_session_ready = nullptr;
inline InstanceVoidFn g_scene_continuation = nullptr;
inline const MethodInfo* g_mi_scene_continuation = nullptr;

inline thread_local bool g_starting_locally = false;
inline std::atomic<bool> g_local_transaction{false};
inline std::atomic<bool> g_runtime_ready{false};
inline std::atomic<bool> g_gate_logged{false};
inline std::atomic<bool> g_local_gate_logged{false};
inline std::atomic<bool> g_published{false};
inline std::atomic<bool> g_scheduled_by_hand{false};
inline std::atomic<bool> g_continuation_done{false};
inline std::atomic<bool> g_technical_works_suppressed{false};
inline bool g_suppress_technical_works = true;
inline std::atomic<uint32_t> g_transaction_frames{0u};

// Diagnostics for the auth-scene restart loop.
inline std::atomic<uint32_t> g_scene_instances{0u};
inline std::atomic<uint64_t> g_start_ms{0u};
inline std::atomic<int32_t> g_traced_state{INT32_MIN};
inline std::atomic<int32_t> g_traced_ready{-1};

// The exact IEnumerator instance Unity is running, captured from the stock
// factory so the machine is identified instead of inferred.
inline std::atomic<void*> g_completion_object{nullptr};
inline std::atomic<int32_t> g_iterator_state{INT32_MIN};

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

// Reads a managed field by metadata name. Returns false when the class does
// not declare it, which is how the awaited-task field is probed.
template <typename T>
bool read_managed_field(void* object, const char* name, T* out) {
    static_assert(sizeof(T) <= 8, "diagnostic field must be scalar/pointer");
    if (object == nullptr || out == nullptr ||
        il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_field_from_name == nullptr ||
        il2cpp::field_get_value == nullptr) {
        return false;
    }
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

// il2cpp_class_get_name is bound outside the required export set, so a layout
// without it still logs everything else.
const char* managed_class_name(void* object) {
    if (object == nullptr || il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_name == nullptr) {
        return "<unnamed>";
    }
    void* klass = il2cpp::object_get_class(object);
    if (klass == nullptr) return "<unnamed>";
    const char* name = il2cpp::class_get_name(klass);
    return name != nullptr ? name : "<unnamed>";
}

const char* task_status(int32_t flags) {
    if ((flags & kTaskRanToCompletion) != 0) return "RanToCompletion";
    if ((flags & kTaskFaulted) != 0) return "Faulted";
    if ((flags & kTaskCanceled) != 0) return "Canceled";
    if ((flags & kTaskWaitingForActivation) != 0) return "WaitingForActivation";
    if ((flags & kTaskStarted) != 0) return "Running";
    return "NotStarted";
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

// True once the scheduled completion state machine has run to its end.
bool completion_finished() {
    void* node = g_completion_object.load(std::memory_order_acquire);
    if (node == nullptr) return false;
    int32_t state = INT32_MIN;
    if (!read_managed_field<int32_t>(node, kIteratorStateField, &state)) {
        return false;
    }
    return state == kIteratorFinished;
}

// Walks the yielded-object chain of the scheduled completion iterator. One
// line per node: which generated class it is, which <>1__state it sits on and
// the status of the Task it awaits. This is what names a parked machine.
void log_completion_chain(const char* where, uint32_t frames) {
    void* node = g_completion_object.load(std::memory_order_acquire);
    if (node == nullptr) {
        LOGW("23.1.3-local-backend: %s no completion iterator is recorded",
             where);
        return;
    }

    for (int depth = 0; depth < kMaxChainDepth && node != nullptr; ++depth) {
        int32_t state = INT32_MIN;
        (void)read_managed_field<int32_t>(node, kIteratorStateField, &state);

        void* task = nullptr;
        const char* task_field = nullptr;
        for (const char* candidate : kAwaitedTaskFields) {
            void* value = nullptr;
            if (read_managed_field<void*>(node, candidate, &value)) {
                task = value;
                task_field = candidate;
                break;
            }
        }

        if (task_field == nullptr) {
            LOGW("23.1.3-local-backend: %s chain[%d] %s state=%d after %u "
                 "frame(s); this class awaits no Task",
                 where, depth, managed_class_name(node), state, frames);
        } else if (task == nullptr) {
            LOGE("23.1.3-local-backend: %s chain[%d] %s state=%d after %u "
                 "frame(s); %s is null, the lookup was never started",
                 where, depth, managed_class_name(node), state, frames,
                 task_field);
        } else {
            int32_t flags = 0;
            const bool has_flags =
                read_managed_field<int32_t>(task, "m_stateFlags", &flags);
            LOGE("23.1.3-local-backend: %s chain[%d] %s state=%d after %u "
                 "frame(s); awaits %s (%s) status=%s flags=0x%08" PRIx32
                 " completed=%d",
                 where, depth, managed_class_name(node), state, frames,
                 task_field, managed_class_name(task),
                 has_flags ? task_status(flags) : "<no m_stateFlags>",
                 static_cast<uint32_t>(flags),
                 has_flags ? ((flags & kTaskCompletedMask) != 0 ? 1 : 0) : -1);
        }

        void* current = nullptr;
        if (!read_managed_field<void*>(node, kIteratorCurrentField, &current) ||
            current == nullptr || current == node) {
            break;
        }
        node = current;
    }
}

// Logs only when the scheduled iterator actually advances, so a parked machine
// stays silent instead of emitting one line per frame.
void trace_iterator(const char* where, uint32_t frames) {
    void* node = g_completion_object.load(std::memory_order_acquire);
    if (node == nullptr) return;
    int32_t state = INT32_MIN;
    if (!read_managed_field<int32_t>(node, kIteratorStateField, &state)) return;
    const int32_t previous =
        g_iterator_state.exchange(state, std::memory_order_relaxed);
    if (state == previous) return;
    LOGI("23.1.3-local-backend: %s iterator %s advanced %d -> %d after %u "
         "frame(s)", where, managed_class_name(node), previous, state, frames);
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
        LOGI("23.1.3-local-backend: the local session is usable (state=%d/%s "
             "readyFlag=1)", state, state_name(state));
    }
    return true;
}

// The stock completion coroutine of 23.1.3 sets the session-ready flag but
// never writes AuthSceneState: that write belongs to Start -> dispatcher,
// which only the retired transport used to drive to the end. Publish it
// explicitly through the controller's own static setters. version_2313.h
// traces the setter, so the write shows up in the next capture.
bool publish_local_session(const char* why, uint32_t frames) {
    if (g_published.exchange(true, std::memory_order_acq_rel)) {
        return publish_if_ready();
    }
    if (g_set_state == nullptr || g_mi_set_state == nullptr) {
        LOGE("23.1.3-local-backend: cannot publish the local session (%s), the "
             "state setter was not resolved", why);
        return false;
    }

    const int32_t before = auth_state();
    if (!session_ready_flag() && g_set_session_ready != nullptr &&
        g_mi_set_session_ready != nullptr) {
        g_set_session_ready(true, g_mi_set_session_ready);
    }
    g_set_state(kFullySynchronized, g_mi_set_state);

    const int32_t after = auth_state();
    LOGW("23.1.3-local-backend: published the local session explicitly (%s "
         "after %u frame(s); state %d/%s -> %d/%s ready=%d)",
         why, frames, before, state_name(before), after, state_name(after),
         session_ready_flag() ? 1 : 0);
    return publish_if_ready();
}

// Fallback only: the stock dispatcher normally creates and schedules this
// iterator. If it did not, schedule it exactly as the previous revision did,
// so a metadata mismatch degrades instead of hanging.
bool schedule_completion(void* self, uint32_t frames) {
    if (self == nullptr || g_completion_iterator == nullptr ||
        g_mi_completion_iterator == nullptr || g_start_coroutine == nullptr ||
        g_mi_start_coroutine == nullptr) {
        return false;
    }
    if (g_scheduled_by_hand.exchange(true, std::memory_order_acq_rel)) {
        return false;
    }

    g_starting_locally = true;
    void* iterator = g_completion_iterator(self, g_mi_completion_iterator);
    void* coroutine =
        iterator != nullptr
            ? g_start_coroutine(self, iterator, g_mi_start_coroutine)
            : nullptr;
    g_starting_locally = false;
    if (coroutine == nullptr) {
        LOGE("23.1.3-local-backend: fallback scheduling of the completion "
             "coroutine failed after %u frame(s)", frames);
        return false;
    }

    g_completion_object.store(iterator, std::memory_order_release);
    g_iterator_state.store(INT32_MIN, std::memory_order_relaxed);
    LOGW("23.1.3-local-backend: the stock dispatcher did not schedule the "
         "completion coroutine within %u frame(s); scheduled it by hand "
         "(iterator %s)", frames, managed_class_name(iterator));
    return true;
}

// The dispatcher asks this persisted predicate before it chooses a route.
// True means "a local session already exists" and selects the completion
// coroutine; false selects the retired ping/login transport. Force it only
// while the one local auth transaction is active.
bool hook_local_session_gate(const MethodInfo* method) {
    if (g_local_transaction.load(std::memory_order_acquire)) {
        if (!g_local_gate_logged.exchange(true, std::memory_order_relaxed)) {
            LOGI("23.1.3-local-backend: reporting an existing local session to "
                 "the auth dispatcher for this transaction only");
        }
        return true;
    }
    return g_local_session_gate != nullptr ? g_local_session_gate(method)
                                           : false;
}

// The completion coroutine asks a VersionBlocker-backed predicate before it
// can finish. Ignore that retired backend decision only while the one local
// completion transaction is active.
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

// TechnicalWorks is a retired-backend failure state, not a meaningful state
// for an in-process backend. Intercept it before the stock dispatcher creates
// the maintenance modal, publish the local session through the game's own
// setters, and continue along the synchronized route.
void hook_state_dispatcher(void* self, int32_t state, const MethodInfo* method) {
    if (state == kTechnicalWorks && g_suppress_technical_works) {
        if (!g_technical_works_suppressed.exchange(true,
                                                    std::memory_order_relaxed)) {
            LOGW("23.1.3-local-backend: suppressed TechnicalWorks before its "
                 "maintenance UI was created; continuing as FullySynchronized");
        }
        if (g_set_session_ready != nullptr &&
            g_mi_set_session_ready != nullptr) {
            g_set_session_ready(true, g_mi_set_session_ready);
        }
        if (g_set_state != nullptr && g_mi_set_state != nullptr) {
            g_set_state(kFullySynchronized, g_mi_set_state);
        }
        g_runtime_ready.store(true, std::memory_order_release);
        g_local_transaction.store(false, std::memory_order_release);
        state = kFullySynchronized;
    }
    if (g_state_dispatcher != nullptr) g_state_dispatcher(self, state, method);
}

// Captures the iterator the stock dispatcher creates, so introspection keeps
// working without this module scheduling anything itself.
void* hook_completion_factory(void* self, const MethodInfo* method) {
    void* iterator = g_completion_iterator != nullptr
                         ? g_completion_iterator(self, method)
                         : nullptr;
    if (iterator != nullptr &&
        g_local_transaction.load(std::memory_order_acquire)) {
        g_completion_object.store(iterator, std::memory_order_release);
        g_iterator_state.store(INT32_MIN, std::memory_order_relaxed);
        LOGI("23.1.3-local-backend: the stock dispatcher created the "
             "completion iterator %s (%p)", managed_class_name(iterator),
             iterator);
    }
    return iterator;
}

void hook_auth_start(void* self, const MethodInfo* method) {
    if (g_auth_start == nullptr) {
        LOGE("23.1.3-local-backend: the stock Auth Start is unavailable; the "
             "auth scene cannot be driven");
        return;
    }
    if (self == nullptr || g_starting_locally) {
        g_auth_start(self, method);
        return;
    }

    bool expected = false;
    if (!g_local_transaction.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        LOGW("23.1.3-local-backend: duplicate AuthSceneController.Start while a "
             "local transaction is active; running the stock Start only");
        g_starting_locally = true;
        g_auth_start(self, method);
        g_starting_locally = false;
        return;
    }

    g_transaction_frames.store(0u, std::memory_order_relaxed);
    g_gate_logged.store(false, std::memory_order_relaxed);
    g_local_gate_logged.store(false, std::memory_order_relaxed);
    g_published.store(false, std::memory_order_relaxed);
    g_scheduled_by_hand.store(false, std::memory_order_relaxed);
    g_continuation_done.store(false, std::memory_order_relaxed);
    g_technical_works_suppressed.store(false, std::memory_order_relaxed);
    g_traced_state.store(INT32_MIN, std::memory_order_relaxed);
    g_traced_ready.store(-1, std::memory_order_relaxed);
    g_completion_object.store(nullptr, std::memory_order_release);
    g_iterator_state.store(INT32_MIN, std::memory_order_relaxed);

    const uint64_t now = opg3d_log::monotonic_ms();
    const uint64_t previous = g_start_ms.exchange(now, std::memory_order_relaxed);
    const uint32_t instance = g_scene_instances.fetch_add(1u) + 1u;
    if (instance > 1u) {
        LOGE("23.1.3-local-backend: auth scene restarted (instance #%u) %" PRIu64
             " ms after the previous Start; the previous transaction never "
             "published a session",
             instance, previous != 0u && now >= previous ? now - previous : 0u);
    }

    const int32_t before = auth_state();
    LOGW("23.1.3-local-backend: running the stock Auth Start on the "
         "local-session route (before=%d/%s ready=%d)",
         before, state_name(before), session_ready_flag() ? 1 : 0);

    // The stock Start prepares the scene and tail-calls the auth state
    // dispatcher, which schedules the completion coroutine because the local
    // session gate above answers true for this transaction.
    g_starting_locally = true;
    g_auth_start(self, method);
    g_starting_locally = false;

    trace_transition("stock Start returned;", 0u);
    trace_iterator("stock Start returned;", 0u);
    if (g_completion_object.load(std::memory_order_acquire) == nullptr) {
        LOGW("23.1.3-local-backend: the stock Start did not schedule a "
             "completion coroutine yet; Update will retry");
    }
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
    trace_iterator("completion", frames);

    // Step 1: the dispatcher should have scheduled the coroutine by now.
    if (frames == kScheduleGraceFrames &&
        g_completion_object.load(std::memory_order_acquire) == nullptr) {
        (void)schedule_completion(self, frames);
    }

    // Step 2: publish once the machine has finished, or when it overruns its
    // budget. The coroutine of this build never publishes the state itself.
    const bool finished = completion_finished();
    if ((finished && frames >= kSettleFrames) ||
        frames >= kPublishDeadlineFrames) {
        const char* why =
            finished ? "the completion coroutine ended without a state write"
                     : "the completion coroutine overran its frame budget";
        if (publish_local_session(why, frames)) return;
    }

    // Step 3: the session is published but the scene is still up, so run the
    // stock post-auth continuation (version banner check + SceneLoader) once.
    if (frames >= kContinuationFrames &&
        g_published.load(std::memory_order_acquire) &&
        !g_continuation_done.exchange(true, std::memory_order_acq_rel)) {
        if (g_scene_continuation != nullptr &&
            g_mi_scene_continuation != nullptr) {
            LOGW("23.1.3-local-backend: the auth scene is still alive %u "
                 "frame(s) after the session was published; invoking the stock "
                 "post-auth continuation once", frames);
            g_scene_continuation(self, g_mi_scene_continuation);
        } else {
            LOGE("23.1.3-local-backend: the stock post-auth continuation was "
                 "not resolved; the scene hand-off is left to the game");
        }
    }

    if (frames == kChainReportFrames) {
        const int32_t state = auth_state();
        LOGW("23.1.3-local-backend: completion still pending after %u frames "
             "(state=%d/%s ready=%d)", frames, state, state_name(state),
             session_ready_flag() ? 1 : 0);
        log_completion_chain("still pending;", frames);
    } else if (frames > kChainReportFrames &&
               frames % kChainRepeatFrames == 0u) {
        log_completion_chain("still parked;", frames);
    }
    if (frames >= kTimeoutFrames) {
        g_local_transaction.store(false, std::memory_order_release);
        const int32_t state = auth_state();
        LOGE("23.1.3-local-backend: completion timed out fail-closed after "
             "%u frames (state=%d/%s ready=%d)", frames, state,
             state_name(state), session_ready_flag() ? 1 : 0);
        log_completion_chain("timed out;", frames);
        g_completion_object.store(nullptr, std::memory_order_release);
    }
}

void hook_auth_destroy(void* self, const MethodInfo* method) {
    if (g_local_transaction.load(std::memory_order_acquire) &&
        !publish_if_ready() && completion_finished()) {
        (void)publish_local_session(
            "the auth scene is being destroyed",
            g_transaction_frames.load(std::memory_order_relaxed));
    }
    if (g_auth_destroy != nullptr) g_auth_destroy(self, method);
    const bool was_active =
        g_local_transaction.exchange(false, std::memory_order_acq_rel);
    if (was_active && !g_runtime_ready.load(std::memory_order_acquire)) {
        const uint64_t now = opg3d_log::monotonic_ms();
        const uint64_t started = g_start_ms.load(std::memory_order_relaxed);
        const uint32_t frames =
            g_transaction_frames.load(std::memory_order_relaxed);
        const int32_t state = auth_state();
        LOGE("23.1.3-local-backend: auth scene destroyed before a ready "
             "session was observed -- %" PRIu64 " ms and %u frame(s) after "
             "Start, still state=%d/%s ready=%d",
             started != 0u && now >= started ? now - started : 0u, frames,
             state, state_name(state), session_ready_flag() ? 1 : 0);
        log_completion_chain("scene destroyed;", frames);
    }
    g_completion_object.store(nullptr, std::memory_order_release);
    g_iterator_state.store(INT32_MIN, std::memory_order_relaxed);
}

} // namespace detail

inline bool runtime_ready() {
    return detail::g_runtime_ready.load(std::memory_order_acquire);
}

inline bool install_hooks(bool suppress_technical_works) {
    detail::g_suppress_technical_works = suppress_technical_works;
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
    // The completion coroutine of this build never writes the state, so the
    // setter is a hard dependency of the local route, not a diagnostic.
    resolved &= detail::resolve_call(
        {"", "AuthSceneController", detail::kStateSetterMethod, 1},
        reinterpret_cast<void**>(&detail::g_set_state),
        &detail::g_mi_set_state);
    resolved &= detail::resolve_call(
        {"", "AuthSceneController", detail::kSessionReadyGetterMethod, 0},
        reinterpret_cast<void**>(&detail::g_get_session_ready),
        &detail::g_mi_get_session_ready);
    // StartCoroutine has two one-argument overloads. The obsolete _Auto entry
    // has a unique name and the exact IEnumerator ABI, avoiding an ambiguous
    // args-count-only lookup. Only the fallback path uses it.
    resolved &= detail::resolve_call(
        {"UnityEngine", "MonoBehaviour", "StartCoroutine_Auto", 1},
        reinterpret_cast<void**>(&detail::g_start_coroutine),
        &detail::g_mi_start_coroutine);
    if (!resolved) {
        LOGE("23.1.3-local-backend: completion metadata incomplete");
        return false;
    }

    // Optional: publication works without them, they only make it tidier.
    if (!detail::resolve_call(
            {"", "AuthSceneController", detail::kSessionReadySetterMethod, 1},
            reinterpret_cast<void**>(&detail::g_set_session_ready),
            &detail::g_mi_set_session_ready)) {
        LOGW("23.1.3-local-backend: the session-ready setter is unavailable; "
             "only the state will be published");
    }
    if (!detail::resolve_call(
            {"", "AuthSceneController", detail::kSceneContinuationMethod, 0},
            reinterpret_cast<void**>(&detail::g_scene_continuation),
            &detail::g_mi_scene_continuation)) {
        LOGW("23.1.3-local-backend: the post-auth continuation is unavailable; "
             "the scene hand-off is left to the game");
    }

    // Optional: without it the dispatcher may still pick the retired transport
    // branch, and the explicit publication below is what keeps the boot alive.
    const bool gate = hook::install(
        {"", detail::kLocalSessionGateClass, detail::kLocalSessionGateMethod, 0},
        detail::replacement(&detail::hook_local_session_gate),
        detail::original_slot(&detail::g_local_session_gate), false);
    if (!gate) {
        LOGW("23.1.3-local-backend: the local-session gate could not be "
             "hooked; the dispatcher may choose the retired transport branch");
    }

    const bool factory = hook::install(
        {"", "AuthSceneController", detail::kCompletionIteratorMethod, 0},
        detail::replacement(&detail::hook_completion_factory),
        detail::original_slot(&detail::g_completion_iterator), true);
    const bool blocker = factory && hook::install(
        {"", "AuthSceneController", detail::kCompletionBlockerMethod, 0},
        detail::replacement(&detail::hook_completion_blocker),
        detail::original_slot(&detail::g_completion_blocker), true);
    const bool dispatcher = blocker && hook::install(
        {"", "AuthSceneController", detail::kStateDispatcherMethod, 1},
        detail::replacement(&detail::hook_state_dispatcher),
        detail::original_slot(&detail::g_state_dispatcher), true);
    const bool update = dispatcher && hook::install(
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
    if (!factory || !blocker || !dispatcher || !update || !destroy || !start) {
        LOGE("23.1.3-local-backend: hooks incomplete (factory=%d blocker=%d "
             "dispatcher=%d update=%d destroy=%d start=%d)", factory ? 1 : 0,
             blocker ? 1 : 0, dispatcher ? 1 : 0, update ? 1 : 0,
             destroy ? 1 : 0, start ? 1 : 0);
        return false;
    }

    if (il2cpp::class_get_name == nullptr) {
        LOGW("23.1.3-local-backend: il2cpp_class_get_name is not bound; "
             "completion iterator classes will be logged as <unnamed>");
    }

    LOGI("23.1.3-local-backend: armed the stock auth state machine on the "
         "local-session route (gate=%d) with explicit session publication; "
         "the retired auth transport is not used", gate ? 1 : 0);
    return true;
}

} // namespace backend_local_2313
