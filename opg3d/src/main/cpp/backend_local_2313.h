#pragma once

#include <atomic>
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
    u8"万丕丂丑丄世丈丁丌";
inline constexpr const char* kCompletionBlockerMethod =
    u8"丑七丏三丌丕丞不丙";
inline constexpr const char* kStateGetterMethod =
    u8"丒与且丙业丁丏丁三";
inline constexpr const char* kSessionReadyGetterMethod =
    u8"丄不丘丝业且丄丁世";

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
    (void)publish_if_ready();
}

void hook_auth_update(void* self, const MethodInfo* method) {
    if (g_auth_update != nullptr) g_auth_update(self, method);
    if (!g_local_transaction.load(std::memory_order_acquire)) return;
    if (publish_if_ready()) return;

    const uint32_t frames =
        g_transaction_frames.fetch_add(1u, std::memory_order_relaxed) + 1u;
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
        LOGE("23.1.3-local-backend: auth scene destroyed before a ready "
             "session was observed");
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
