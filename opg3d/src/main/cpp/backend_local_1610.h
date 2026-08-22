#pragma once

#include <atomic>
#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Local backend-session bridge for the supplied obfuscated PG3D 16.1.x
// ARMv7 build (libil2cpp.so SHA-256
// 2aab620cb58a597e86975a78ab20987e71685b507456707ed42fa63fad54032b).
//
// The original Cubic Games authorization service is retired. Calling the
// visible "offline" button reaches AuthSceneController's offline callback,
// which sets the process-wide backend-offline flag. That flag survives the
// lobby and prevents the normal in-match HUD, input and loadout lifecycle even
// after PUN itself is switched back online.
//
// This module never calls that callback and never fabricates an HTTP response.
// AuthSceneController.Awake is left stock so all local serializers and model
// owners are created. Its Start method is then replaced with the controller's
// own successful completion routine. The routine performs the exact stock
// online transition recovered from this binary:
//   CheckAppVersion -> FullySynchronized -> clear backend-offline flag ->
//   invoke synchronization-complete listeners -> load the main menu.
// Existing Storager/PlayerPrefs data therefore remains the source of truth and
// continues to be saved by the game's original controllers.
namespace backend_local_1610 {
namespace detail {

using MethodInfo = void;
using InstanceVoidFn = void (*)(void* self, const MethodInfo* method);
using InstanceBoolFn = bool (*)(void* self, const MethodInfo* method);
using StaticGetIntFn = int32_t (*)(void* static_context,
                                   const MethodInfo* method);
using StaticGetBoolFn = bool (*)(void* static_context,
                                 const MethodInfo* method);

// AuthSceneController metadata names from the supplied dump.cs.
inline constexpr const char* kCompleteAuthorizationMethod =
    u8"丐业丆一七专丌丝丑";
inline constexpr const char* kCompletionBlockerMethod =
    u8"丕丈丁丝丈丑丛业不";
inline constexpr const char* kStateGetterMethod =
    u8"且丙丐丟丞丞丗世下";
inline constexpr const char* kSessionReadyGetterMethod =
    u8"丁丌丁丅丐丈丕丌丕";

inline constexpr int32_t kFullySynchronized = 3;
inline constexpr int32_t kEmpty = 4;

inline InstanceVoidFn g_auth_start = nullptr;
inline InstanceVoidFn g_complete_authorization = nullptr;
inline const MethodInfo* g_mi_complete_authorization = nullptr;
inline InstanceBoolFn g_completion_blocker = nullptr;
inline StaticGetIntFn g_get_state = nullptr;
inline const MethodInfo* g_mi_get_state = nullptr;
inline StaticGetBoolFn g_get_session_ready = nullptr;
inline const MethodInfo* g_mi_get_session_ready = nullptr;

inline thread_local bool g_starting_locally = false;
inline thread_local bool g_completing_locally = false;
inline std::atomic<bool> g_runtime_ready{false};
inline std::atomic<bool> g_gate_logged{false};

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
        LOGE("16.1.x-local-backend: cannot resolve %s.%s/%d", target.klass,
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
        default: return "Unknown";
    }
}

int32_t auth_state() {
    return g_get_state != nullptr && g_mi_get_state != nullptr
               ? g_get_state(nullptr, g_mi_get_state)
               : -999;
}

bool session_ready_flag() {
    return g_get_session_ready != nullptr &&
                   g_mi_get_session_ready != nullptr
               ? g_get_session_ready(nullptr, g_mi_get_session_ready)
               : false;
}

// The stock completion routine consults a backend-fed VersionBlocker predicate
// before it publishes FullySynchronized. The old service can no longer supply
// a meaningful block decision. Bypass that one predicate only for the
// synchronous local completion call; every other caller retains stock logic.
bool hook_completion_blocker(void* self, const MethodInfo* method) {
    if (g_completing_locally) {
        if (!g_gate_logged.exchange(true, std::memory_order_relaxed)) {
            LOGI("16.1.x-local-backend: ignored retired backend version gate "
                 "for the local completion transaction");
        }
        return false;
    }
    return g_completion_blocker != nullptr
               ? g_completion_blocker(self, method)
               : true;
}

void hook_auth_start(void* self, const MethodInfo* method) {
    (void)method;
    if (self == nullptr || g_complete_authorization == nullptr ||
        g_mi_complete_authorization == nullptr) {
        LOGE("16.1.x-local-backend: cannot start local session "
             "(self=%p complete=%p info=%p); retired backend remains blocked",
             self, reinterpret_cast<void*>(g_complete_authorization),
             g_mi_complete_authorization);
        return;
    }
    // Unity normally calls Start once. Keep only a re-entrancy guard rather
    // than remembering an object address forever: a later auth scene may reuse
    // the same native allocation after logout/restart.
    if (g_starting_locally) return;
    g_starting_locally = true;

    const int32_t before = auth_state();
    LOGW("16.1.x-local-backend: suppressing AuthSceneController.Start "
         "network route; completing from local persisted state (before=%d/%s)",
         before, state_name(before));

    g_completing_locally = true;
    g_complete_authorization(self, g_mi_complete_authorization);
    g_completing_locally = false;
    g_starting_locally = false;

    const int32_t after = auth_state();
    const bool ready = session_ready_flag();
    // The completion routine publishes state 3, then its stock scene loader
    // changes the now-unused auth-scene state to Empty (4). Either value is
    // healthy after the synchronous call, but the ready flag must be true.
    const bool healthy = ready &&
                         (after == kFullySynchronized || after == kEmpty);
    g_runtime_ready.store(healthy, std::memory_order_release);
    if (healthy) {
        LOGI("16.1.x-local-backend: local online-compatible session ready "
             "(state=%d/%s readyFlag=1 backendOffline=cleared by stock "
             "completion)",
             after, state_name(after));
    } else {
        LOGE("16.1.x-local-backend: stock completion did not publish a usable "
             "session (state=%d/%s readyFlag=%d); refusing the retired "
             "backend and offline callback",
             after, state_name(after), ready ? 1 : 0);
    }
}

} // namespace detail

inline bool runtime_ready() {
    return detail::g_runtime_ready.load(std::memory_order_acquire);
}

inline bool install_hooks() {
#if defined(__ANDROID__)
    static_assert(sizeof(void*) == 4,
                  "PG3D 16.1.x target must be armeabi-v7a");
#endif

    bool resolved = detail::resolve_call(
        {"", "AuthSceneController", detail::kCompleteAuthorizationMethod, 0},
        reinterpret_cast<void**>(&detail::g_complete_authorization),
        &detail::g_mi_complete_authorization);
    resolved &= detail::resolve_call(
        {"", "AuthSceneController", detail::kStateGetterMethod, 0},
        reinterpret_cast<void**>(&detail::g_get_state),
        &detail::g_mi_get_state);
    resolved &= detail::resolve_call(
        {"", "AuthSceneController", detail::kSessionReadyGetterMethod, 0},
        reinterpret_cast<void**>(&detail::g_get_session_ready),
        &detail::g_mi_get_session_ready);
    if (!resolved) {
        LOGE("16.1.x-local-backend: completion metadata incomplete");
        return false;
    }

    // Install the scoped blocker first. Start is installed last so no local
    // completion can execute while one of its dependencies is missing.
    const bool blocker = hook::install(
        {"", "AuthSceneController", detail::kCompletionBlockerMethod, 0},
        detail::replacement(&detail::hook_completion_blocker),
        detail::original_slot(&detail::g_completion_blocker), true);
    const bool start = blocker && hook::install(
        {"", "AuthSceneController", "Start", 0},
        detail::replacement(&detail::hook_auth_start),
        detail::original_slot(&detail::g_auth_start), true);
    if (!blocker || !start) {
        LOGE("16.1.x-local-backend: hooks incomplete (blocker=%d start=%d)",
             blocker ? 1 : 0, start ? 1 : 0);
        return false;
    }

    LOGI("16.1.x-local-backend: armed stock FullySynchronized completion "
         "from local Storager state; offline callback and retired auth "
         "transport are not used");
    return true;
}

} // namespace backend_local_1610
