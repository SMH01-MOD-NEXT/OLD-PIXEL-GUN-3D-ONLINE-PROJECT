#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Passive diagnostics for the obfuscated 16.1.1 PUN 1.91 peer. Wrappers log
// the real SDK result and always delegate; no callback, operation response or
// disconnect is fabricated or suppressed.
namespace photon_trace_1610 {
namespace detail {

using MethodInfo = void;
using ManagedString = void;
using StaticGetIntFn = int32_t (*)(void*, const MethodInfo*);
using StaticVoidFn = void (*)(void*, const MethodInfo*);
using InstanceVoidFn = void (*)(void*, const MethodInfo*);
using InstanceIntFn = void (*)(void*, int32_t, const MethodInfo*);
using DebugReturnFn = void (*)(void*, int32_t, ManagedString*,
                               const MethodInfo*);
using OperationResponseFn = void (*)(void*, void*, const MethodInfo*);

inline constexpr const char* kPhotonNetwork = u8"上丁丈与丘丟丈专丄";
inline constexpr const char* kNetworkingPeer = u8"丛丗丄与丐与三丐丄";
inline constexpr const char* kStateGetter = u8"专丑三丛不东丛丕丝";
inline constexpr const char* kDisconnect = u8"丑丑万丂上丅丌世丝";

inline StaticVoidFn g_disconnect = nullptr;
inline DebugReturnFn g_debug = nullptr;
inline InstanceIntFn g_status = nullptr;
inline OperationResponseFn g_operation = nullptr;
inline InstanceIntFn g_failed = nullptr;
inline InstanceVoidFn g_master = nullptr;
inline InstanceVoidFn g_created = nullptr;
inline InstanceVoidFn g_joined = nullptr;

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

int32_t state() {
    static StaticGetIntFn fn = nullptr;
    static const MethodInfo* info = nullptr;
    if (fn == nullptr) {
        void* method = il2cpp::find_method_info(
            "", kPhotonNetwork, kStateGetter, 0);
        fn = method != nullptr
                 ? reinterpret_cast<StaticGetIntFn>(
                       il2cpp::method_pointer(method))
                 : nullptr;
        info = method;
    }
    return fn != nullptr ? fn(nullptr, info) : -1;
}

const char* state_name(int32_t value) {
    switch (value) {
        case 1: return "PeerCreated";
        case 3: return "Authenticated";
        case 4: return "JoinedLobby";
        case 6: return "ConnectingToGameserver";
        case 7: return "ConnectedToGameserver";
        case 8: return "Joining";
        case 9: return "Joined";
        case 12: return "ConnectingToMasterserver";
        case 15: return "Disconnected";
        case 16: return "ConnectedToMaster";
        case 17: return "ConnectingToNameServer";
        case 18: return "ConnectedToNameServer";
        case 20: return "Authenticating";
        default: return "other";
    }
}

const char* status_name(int32_t value) {
    switch (value) {
        case 1022: return "SecurityExceptionOnConnect";
        case 1023: return "ExceptionOnConnect";
        case 1024: return "Connect";
        case 1025: return "Disconnect";
        case 1026: return "Exception";
        case 1030: return "SendError";
        case 1039: return "ExceptionOnReceive";
        case 1040: return "TimeoutDisconnect";
        case 1041: return "DisconnectByServer";
        case 1042: return "DisconnectByServerUserLimit";
        case 1043: return "DisconnectByServerLogic";
        case 1048: return "EncryptionEstablished";
        case 1049: return "EncryptionFailedToEstablish";
        default: return "other";
    }
}

const char* return_name(int32_t value) {
    switch (value) {
        case 0: return "Ok";
        case 32753: return "AuthenticationTicketExpired";
        case 32756: return "InvalidRegion";
        case 32757: return "MaxCcuReached";
        case 32760: return "GameDoesNotExist";
        case 32765: return "GameFull";
        case 32767: return "InvalidAuthentication";
        default: return "other";
    }
}

void hook_disconnect(void* context, const MethodInfo* method) {
    const int32_t current = state();
    LOGW("16.1.1-photon: stock PhotonNetwork.Disconnect requested "
         "state=%d(%s)", current, state_name(current));
    if (g_disconnect != nullptr) g_disconnect(context, method);
}

void hook_debug(void* self, int32_t level, ManagedString* message,
                const MethodInfo* method) {
    const std::string text = il2cpp::to_utf8(message, 1024);
    if (level == 1) LOGE("16.1.1-photon-debug[%d]: %s", level, text.c_str());
    else if (level == 2) LOGW("16.1.1-photon-debug[%d]: %s", level, text.c_str());
    else LOGI("16.1.1-photon-debug[%d]: %s", level, text.c_str());
    if (g_debug != nullptr) g_debug(self, level, message, method);
}

void hook_status(void* self, int32_t code, const MethodInfo* method) {
    const int32_t before = state();
    LOGI("16.1.1-photon-status: %d(%s) before=%d(%s)", code,
         status_name(code), before, state_name(before));
    if (g_status != nullptr) g_status(self, code, method);
    const int32_t after = state();
    LOGI("16.1.1-photon-status: handled %d after=%d(%s)", code, after,
         state_name(after));
}

void hook_operation(void* self, void* response, const MethodInfo* method) {
    uint8_t operation = 0u;
    int16_t result = 0;
    ManagedString* message = nullptr;
    const bool have_operation = read_field(response, "OperationCode", &operation);
    const bool have_result = read_field(response, "ReturnCode", &result);
    read_field(response, "DebugMessage", &message);
    const std::string debug = il2cpp::to_utf8(message, 768);
    const int32_t current = state();
    if (have_result && result != 0) {
        LOGE("16.1.1-photon-op: op=%s%u return=%d(%s) debug='%s' "
             "state=%d(%s)", have_operation ? "" : "?",
             static_cast<unsigned>(operation), static_cast<int>(result),
             return_name(result), debug.c_str(), current,
             state_name(current));
    } else {
        LOGI("16.1.1-photon-op: op=%s%u return=%s%d(%s) debug='%s' "
             "state=%d(%s)", have_operation ? "" : "?",
             static_cast<unsigned>(operation), have_result ? "" : "?",
             static_cast<int>(result), return_name(result), debug.c_str(),
             current, state_name(current));
    }
    if (g_operation != nullptr) g_operation(self, response, method);
}

void hook_failed(void* self, int32_t code, const MethodInfo* method) {
    const int32_t current = state();
    LOGE("16.1.1-connection-ui: OnFailedToConnect code=%d(%s) "
         "state=%d(%s)", code, return_name(code), current,
         state_name(current));
    if (g_failed != nullptr) g_failed(self, code, method);
}

void hook_master(void* self, const MethodInfo* method) {
    const int32_t current = state();
    LOGI("16.1.1-connection-ui: OnConnectedToMaster state=%d(%s)",
         current, state_name(current));
    if (g_master != nullptr) g_master(self, method);
}

void hook_created(void* self, const MethodInfo* method) {
    const int32_t current = state();
    LOGI("16.1.1-connection-ui: OnCreatedRoom state=%d(%s)", current,
         state_name(current));
    if (g_created != nullptr) g_created(self, method);
}

void hook_joined(void* self, const MethodInfo* method) {
    const int32_t current = state();
    LOGI("16.1.1-connection-ui: OnJoinedRoom state=%d(%s)", current,
         state_name(current));
    if (g_joined != nullptr) g_joined(self, method);
}

template <typename Fn>
void* replacement(Fn fn) { return reinterpret_cast<void*>(fn); }
template <typename Fn>
void** original(Fn* fn) { return reinterpret_cast<void**>(fn); }

bool add(const hook::ManagedMethod& method, void* replacement_pointer,
         void** original_pointer, int* installed) {
    const bool ok = hook::install(
        method, replacement_pointer, original_pointer, false);
    if (ok) ++(*installed);
    return ok;
}

} // namespace detail

inline bool install_hooks() {
    using namespace detail;
    int installed = 0;
    const bool disconnect = add(
        {"", kPhotonNetwork, kDisconnect, 0}, replacement(&hook_disconnect),
        original(&g_disconnect), &installed);
    add({"", kNetworkingPeer, "DebugReturn", 2}, replacement(&hook_debug),
        original(&g_debug), &installed);
    const bool status = add(
        {"", kNetworkingPeer, "OnStatusChanged", 1}, replacement(&hook_status),
        original(&g_status), &installed);
    const bool operation = add(
        {"", kNetworkingPeer, "OnOperationResponse", 1},
        replacement(&hook_operation), original(&g_operation), &installed);
    add({"", "ConnectionControl", "OnFailedToConnect", 1},
        replacement(&hook_failed), original(&g_failed), &installed);
    add({"", "ConnectionControl", "OnConnectedToMaster", 0},
        replacement(&hook_master), original(&g_master), &installed);
    add({"", "ConnectionControl", "OnCreatedRoom", 0},
        replacement(&hook_created), original(&g_created), &installed);
    add({"", "ConnectionControl", "OnJoinedRoom", 0},
        replacement(&hook_joined), original(&g_joined), &installed);
    LOGI("16.1.1-photon-trace: installed %d/8 hooks "
         "(status=%s operation=%s disconnect=%s)", installed,
         status ? "OK" : "FAILED", operation ? "OK" : "FAILED",
         disconnect ? "OK" : "FAILED");
    return status && operation && disconnect;
}

} // namespace photon_trace_1610
