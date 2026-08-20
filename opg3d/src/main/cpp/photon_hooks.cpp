#include "photon_hooks.h"

#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "config.h"
#include "hook.h"
#include "il2cpp.h"
#include "log.h"

namespace photon {
namespace {

using MethodInfo = void;
using ManagedString = void;

using SelectPhotonAppIdFn = ManagedString* (*)(void* settings, const MethodInfo* method);
using SetUpPhotonFn = void (*)(void* settings, const MethodInfo* method);
using UseCloudFn = void (*)(void* self, ManagedString* app_id, const MethodInfo* method);
using UseCloudRegionFn = void (*)(void* self, ManagedString* app_id, int32_t region,
                                  const MethodInfo* method);
using UseMyServerFn = void (*)(void* self, ManagedString* address, int32_t port,
                               ManagedString* app_id, const MethodInfo* method);
using ConnectUsingSettingsFn = bool (*)(ManagedString* game_version,
                                        const MethodInfo* method);
using ConnectToMasterFn = bool (*)(ManagedString* address, int32_t port,
                                   ManagedString* app_id, ManagedString* game_version,
                                   const MethodInfo* method);
using GetGameVersionFn = ManagedString* (*)(const MethodInfo* method);
using ConnectToPhotonFn = bool (*)(const MethodInfo* method);
using DisconnectFn = void (*)(const MethodInfo* method);
using DebugReturnFn = void (*)(void* self, int32_t level, ManagedString* message,
                               const MethodInfo* method);
using OnStatusChangedFn = void (*)(void* self, int32_t status,
                                   const MethodInfo* method);
using OnOperationResponseFn = void (*)(void* self, void* response,
                                       const MethodInfo* method);
using FailedToConnectFn = void (*)(void* self, int32_t cause,
                                   const MethodInfo* method);
using InstanceVoidFn = void (*)(void* self, const MethodInfo* method);
using GetIntFn = int32_t (*)(const MethodInfo* method);
using GetStringFn = ManagedString* (*)(const MethodInfo* method);
using GetObjectFn = void* (*)(const MethodInfo* method);

SelectPhotonAppIdFn g_select_app_id = nullptr;
SetUpPhotonFn g_set_up_photon = nullptr;
UseCloudFn g_use_cloud = nullptr;
UseCloudFn g_use_cloud_best = nullptr;
UseCloudRegionFn g_use_cloud_region = nullptr;
UseMyServerFn g_use_my_server = nullptr;
ConnectUsingSettingsFn g_connect_using_settings = nullptr;
ConnectToMasterFn g_connect_to_master = nullptr;
GetGameVersionFn g_get_game_version = nullptr;
ConnectToPhotonFn g_game_connect = nullptr;
DisconnectFn g_disconnect = nullptr;
DebugReturnFn g_debug_return = nullptr;
OnStatusChangedFn g_on_status_changed = nullptr;
OnOperationResponseFn g_on_operation_response = nullptr;
FailedToConnectFn g_failed_to_connect = nullptr;
InstanceVoidFn g_disconnected = nullptr;
InstanceVoidFn g_connected_to_master = nullptr;

std::atomic<uint32_t> g_select_calls{0};
std::atomic<bool> g_verbose_ready{false};

uint32_t fnv1a(const char* data, size_t length) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; ++i) {
        hash ^= static_cast<uint8_t>(data[i]);
        hash *= 16777619u;
    }
    return hash;
}

// Никогда не логируем credential целиком: только длину и отпечаток.
// chars= берём у рантайма, потому что to_utf8() обрезает строку до 256 UTF-16
// code unit, и его size() — это длина УСЕЧЁННОГО UTF-8, а не строки. Раньше в
// логе было app={len=622}, по которому нельзя отличить 36-символьный GUID от
// обфусцированного блоба, который расшифровывает Switcher.SelectPhotonAppId.
std::string secret_summary(ManagedString* value) {
    if (value == nullptr) return "<null>";
    const std::string text = il2cpp::to_utf8(value, 256);
    const int32_t chars = il2cpp::string_length != nullptr
                              ? il2cpp::string_length(value)
                              : -1;
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer),
                  "chars=%" PRId32 " utf8=%zu fnv1a=%08" PRIx32,
                  chars, text.size(), fnv1a(text.data(), text.size()));
    return buffer;
}

// Тот же формат, что и у secret_summary(), чтобы отпечаток нашего AppID и
// игрового можно было сравнить глазами прямо в логе.
std::string configured_secret_summary() {
    constexpr const char* app_id = PHOTON_APP_ID;
    const size_t length = std::strlen(app_id);
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer),
                  "chars=%zu utf8=%zu fnv1a=%08" PRIx32,
                  length, length, fnv1a(app_id, length));
    return buffer;
}

template <typename T>
bool read_field(void* object, const char* name, T* value) {
    static_assert(sizeof(T) <= 8, "read_field рассчитан на скаляры и ссылки");
    if (object == nullptr || value == nullptr || !il2cpp::object_get_class ||
        !il2cpp::class_get_field_from_name || !il2cpp::field_get_value) {
        return false;
    }
    void* klass = il2cpp::object_get_class(object);
    if (klass == nullptr) return false;
    void* field = il2cpp::class_get_field_from_name(klass, name);
    if (field == nullptr) return false;

    // il2cpp_field_get_value() копирует РОВНО размер поля из метаданных, а не
    // sizeof(T). ServerSettings.Protocol — это enum ConnectionProtocol с
    // базовым типом byte (dump.cs: `public byte value__`), поэтому при чтении
    // напрямую в int32_t перезаписывался только младший байт, а три старших
    // оставались от инициализатора -1: лог показывал protocol=-256(unknown)
    // вместо 0(Udp). Пишем в обнулённый буфер с запасом и забираем младшие
    // sizeof(T) байт — ARM little-endian. Запас заодно защищает стек, если
    // поле окажется шире ожидаемого типа.
    alignas(8) uint8_t scratch[16] = {0};
    il2cpp::field_get_value(object, field, scratch);
    std::memcpy(value, scratch, sizeof(T));
    return true;
}

template <typename T>
bool write_field(void* object, const char* name, T value) {
    if (object == nullptr || !il2cpp::object_get_class ||
        !il2cpp::class_get_field_from_name || !il2cpp::field_set_value) {
        return false;
    }
    void* klass = il2cpp::object_get_class(object);
    if (klass == nullptr) return false;
    void* field = il2cpp::class_get_field_from_name(klass, name);
    if (field == nullptr) return false;
    il2cpp::field_set_value(object, field, &value);
    return true;
}

void* current_server_settings() {
    static void* field = nullptr;
    if (field == nullptr) {
        field = il2cpp::find_field("", "PhotonNetwork", "PhotonServerSettings");
    }
    if (field == nullptr) return nullptr;
    void* settings = nullptr;
    il2cpp::field_static_get_value(field, &settings);
    return settings;
}

const char* hosting_name(int32_t value) {
    switch (value) {
        case 0: return "NotSet";
        case 1: return "PhotonCloud";
        case 2: return "SelfHosted";
        case 3: return "OfflineMode";
        case 4: return "BestRegion";
        default: return "unknown";
    }
}

const char* protocol_name(int32_t value) {
    switch (value) {
        case 0: return "Udp";
        case 1: return "Tcp";
        case 4: return "WebSocket";
        case 5: return "WebSocketSecure";
        default: return "unknown";
    }
}

void log_settings(const char* point, void* settings) {
    if (settings == nullptr) {
        LOGW("settings[%s]: <null>", point);
        return;
    }

    int32_t host = -1;
    int32_t protocol = -1;
    int32_t port = -1;
    int32_t region = -1;
    int32_t regions = -1;
    bool join_lobby = false;
    bool lobby_stats = false;
    ManagedString* address = nullptr;
    ManagedString* app_id = nullptr;

    read_field(settings, "HostType", &host);
    read_field(settings, "Protocol", &protocol);
    read_field(settings, "ServerAddress", &address);
    read_field(settings, "ServerPort", &port);
    read_field(settings, "AppID", &app_id);
    read_field(settings, "PreferredRegion", &region);
    read_field(settings, "EnabledRegions", &regions);
    read_field(settings, "JoinLobby", &join_lobby);
    read_field(settings, "EnableLobbyStatistics", &lobby_stats);

    const std::string server = il2cpp::to_utf8(address, 256);
    const std::string app = secret_summary(app_id);
    LOGI("settings[%s]: host=%d(%s) protocol=%d(%s) server='%s':%d "
         "app={%s} region=%d enabled=0x%x joinLobby=%d lobbyStats=%d",
         point, host, hosting_name(host), protocol, protocol_name(protocol),
         server.c_str(), port, app.c_str(), region, regions,
         join_lobby ? 1 : 0, lobby_stats ? 1 : 0);
}

bool apply_config_to_settings(void* settings, const char* point) {
    if (settings == nullptr) return false;

    bool changed = false;
    if (std::strlen(PHOTON_APP_ID) != 0u) {
        ManagedString* app_id = il2cpp::string_new(PHOTON_APP_ID);
        if (app_id != nullptr && write_field(settings, "AppID", app_id)) {
            changed = true;
        }
    }

#ifdef PHOTON_MODE_SELFHOSTED
    changed |= write_field<int32_t>(settings, "HostType", 2);
    changed |= write_field<int32_t>(settings, "Protocol", 0);
    if (std::strlen(PHOTON_SERVER_ADDRESS) != 0u) {
        ManagedString* address = il2cpp::string_new(PHOTON_SERVER_ADDRESS);
        changed |= write_field(settings, "ServerAddress", address);
        changed |= write_field<int32_t>(settings, "ServerPort", PHOTON_SERVER_PORT);
    }
#else
    // В Cloud-режиме сохраняем выбранную самой игрой стратегию (обычный
    // PhotonCloud или BestRegion). Исправляем только credential/опциональный
    // endpoint и не меняем поведение матчмейкинга.
    if (std::strlen(PHOTON_SERVER_ADDRESS) != 0u) {
        ManagedString* address = il2cpp::string_new(PHOTON_SERVER_ADDRESS);
        changed |= write_field(settings, "ServerAddress", address);
        changed |= write_field<int32_t>(settings, "ServerPort", PHOTON_SERVER_PORT);
    }
#endif

    if (changed) {
        LOGI("settings[%s]: build configuration applied", point);
    }
    return changed;
}

int32_t connection_state() {
    static GetIntFn fn = nullptr;
    static void* method = nullptr;
    if (fn == nullptr) {
        method = il2cpp::find_method_info("", "PhotonNetwork",
                                           "get_connectionStateDetailed", 0);
        fn = reinterpret_cast<GetIntFn>(il2cpp::method_pointer(method));
    }
    return fn != nullptr ? fn(method) : -1;
}

std::string server_address() {
    static GetStringFn fn = nullptr;
    static void* method = nullptr;
    if (fn == nullptr) {
        method = il2cpp::find_method_info("", "PhotonNetwork",
                                           "get_ServerAddress", 0);
        fn = reinterpret_cast<GetStringFn>(il2cpp::method_pointer(method));
    }
    return fn != nullptr ? il2cpp::to_utf8(fn(method), 256) : "<unavailable>";
}

void log_auth_snapshot(const char* point) {
    static GetObjectFn fn = nullptr;
    static void* method = nullptr;
    if (fn == nullptr) {
        method = il2cpp::find_method_info("", "PhotonNetwork", "get_AuthValues", 0);
        fn = reinterpret_cast<GetObjectFn>(il2cpp::method_pointer(method));
    }
    if (fn == nullptr) {
        LOGW("auth[%s]: getter unavailable", point);
        return;
    }

    void* auth = fn(method);
    if (auth == nullptr) {
        LOGI("auth[%s]: <null>", point);
        return;
    }

    int32_t type = -1;
    ManagedString* token = nullptr;
    ManagedString* user_id = nullptr;
    // Token и UserId здесь — auto-property: физические поля называются
    // <Token>k__BackingField / <UserId>k__BackingField (одинаково в 12.5.0 и
    // 13.2.1). Сам класс AuthenticationValues в 13.2.1 живёт в глобальном
    // namespace вместо ExitGames.Client.Photon, но мы читаем поля через
    // object_get_class() инстанса, поэтому переезд namespace'а нам неважен.
    const bool has_type = read_field(auth, "authType", &type);
    const bool has_token = read_field(auth, "<Token>k__BackingField", &token);
    const bool has_user = read_field(auth, "<UserId>k__BackingField", &user_id);
    const int32_t token_len = (has_token && token != nullptr && il2cpp::string_length)
                                  ? il2cpp::string_length(token) : 0;
    const int32_t user_len = (has_user && user_id != nullptr && il2cpp::string_length)
                                 ? il2cpp::string_length(user_id) : 0;
    LOGI("auth[%s]: object=%p type=%s%d token=%s(len=%d) userId=%s(len=%d)",
         point, auth, has_type ? "" : "?", type,
         token != nullptr ? "set" : "null", token_len,
         user_id != nullptr ? "set" : "null", user_len);
}

void enable_verbose_photon_logs() {
    static void* log_level_field = nullptr;
    static void* peer_field = nullptr;
    static void* debug_out_field = nullptr;

    if (log_level_field == nullptr) {
        log_level_field = il2cpp::find_field("", "PhotonNetwork", "logLevel");
    }
    if (log_level_field != nullptr) {
        int32_t full = 2;
        il2cpp::field_static_set_value(log_level_field, &full);
    }

    if (peer_field == nullptr) {
        peer_field = il2cpp::find_field("", "PhotonNetwork", "networkingPeer");
    }
    void* peer = nullptr;
    if (peer_field != nullptr) {
        il2cpp::field_static_get_value(peer_field, &peer);
    }

    // В 13.2.1 у PhotonPeer нет метода set_DebugOut — DebugOut это обычное
    // поле (dump.cs: `public DebugLevel DebugOut; // 0x1C`; сеттер существует
    // только у ChatClient и к PhotonPeer отношения не имеет). FieldInfo берём
    // у объявляющего класса PhotonPeer: il2cpp_class_get_field_from_name по
    // базовым классам не идёт, а offset поля валиден и для инстанса
    // NetworkingPeer. DebugLevel — byte-backed enum, пишем ровно один байт.
    if (debug_out_field == nullptr) {
        debug_out_field = il2cpp::find_field(
            "ExitGames.Client.Photon", "PhotonPeer", "DebugOut");
    }
    if (peer != nullptr && debug_out_field != nullptr &&
        il2cpp::field_set_value != nullptr) {
        uint8_t all = 5; // DebugLevel.ALL
        il2cpp::field_set_value(peer, debug_out_field, &all);
        if (!g_verbose_ready.exchange(true)) {
            LOGI("trace: PhotonLogLevel=Full, PhotonPeer.DebugOut=ALL");
        }
    }
}

// Имена — по enum StatusCode из 13.2.1. Отличия от 12.5.0: добавились входящие
// предупреждения 1033/1035, QueueSentWarning переехал 1033 -> 1037, а 1039
// переименован из InternalReceiveException в ExceptionOnReceive.
const char* status_name(int32_t status) {
    switch (status) {
        case 1022: return "SecurityExceptionOnConnect";
        case 1023: return "ExceptionOnConnect";
        case 1024: return "Connect";
        case 1025: return "Disconnect";
        case 1026: return "Exception";
        case 1027: return "QueueOutgoingReliableWarning";
        case 1029: return "QueueOutgoingUnreliableWarning";
        case 1030: return "SendError";
        case 1031: return "QueueOutgoingAcksWarning";
        case 1033: return "QueueIncomingReliableWarning";
        case 1035: return "QueueIncomingUnreliableWarning";
        case 1037: return "QueueSentWarning";
        case 1039: return "ExceptionOnReceive";
        case 1040: return "TimeoutDisconnect";
        case 1041: return "DisconnectByServer";
        case 1042: return "DisconnectByServerUserLimit";
        case 1043: return "DisconnectByServerLogic";
        case 1048: return "EncryptionEstablished";
        case 1049: return "EncryptionFailedToEstablish";
        default: return "unknown";
    }
}

const char* disconnect_name(int32_t cause) {
    switch (cause) {
        case 1022: return "SecurityExceptionOnConnect";
        case 1023: return "ExceptionOnConnect";
        case 1026: return "Exception";
        case 1039: return "InternalReceiveException";
        case 1040: return "DisconnectByClientTimeout";
        case 1041: return "DisconnectByServerTimeout";
        case 1042: return "DisconnectByServerUserLimit";
        case 1043: return "DisconnectByServerLogic";
        case 32753: return "AuthenticationTicketExpired";
        case 32756: return "InvalidRegion";
        case 32757: return "MaxCcuReached";
        case 32767: return "InvalidAuthentication";
        default: return "unknown";
    }
}

const char* debug_level_name(int32_t level) {
    switch (level) {
        case 0: return "OFF";
        case 1: return "ERROR";
        case 2: return "WARNING";
        case 3: return "INFO";
        case 5: return "ALL";
        default: return "unknown";
    }
}

ManagedString* hook_select_app_id(void* settings, const MethodInfo* method) {
    const uint32_t call = g_select_calls.fetch_add(1u) + 1u;
    if (std::strlen(PHOTON_APP_ID) != 0u) {
        ManagedString* replacement = il2cpp::string_new(PHOTON_APP_ID);
        if (replacement != nullptr) {
            const std::string summary = configured_secret_summary();
            LOGI("appid: SelectPhotonAppId #%" PRIu32
                 " -> configured AppID {%s}; original kill-switch path skipped",
                 call, summary.c_str());
            return replacement;
        }
        LOGE("appid: il2cpp_string_new failed; falling back to game logic");
    } else {
        LOGW("appid: PHOTON_APP_ID is empty; passthrough to game logic");
    }

    ManagedString* original = g_select_app_id != nullptr
                                  ? g_select_app_id(settings, method) : nullptr;
    const std::string summary = secret_summary(original);
    LOGI("appid: game result {%s}", summary.c_str());
    return original;
}

void hook_set_up_photon(void* settings, const MethodInfo* method) {
    LOGI("trace: Switcher.SetUpPhoton begin (HiddenSettings=%p)", settings);
    g_set_up_photon(settings, method);
    // Только здесь, ПОСЛЕ оригинала, PhotonNetwork гарантированно
    // инициализирован и его статические поля безопасно читать и писать.
    enable_verbose_photon_logs();
    void* server_settings = current_server_settings();
    apply_config_to_settings(server_settings, "SetUpPhoton/end");
    log_settings("SetUpPhoton/end", server_settings);
}

void hook_use_cloud(void* self, ManagedString* app_id, const MethodInfo* method) {
    const std::string summary = secret_summary(app_id);
    LOGI("trace: ServerSettings.UseCloud app={%s}", summary.c_str());
    g_use_cloud(self, app_id, method);
    log_settings("UseCloud/end", self);
}

void hook_use_cloud_best(void* self, ManagedString* app_id,
                         const MethodInfo* method) {
    const std::string summary = secret_summary(app_id);
    LOGI("trace: ServerSettings.UseCloudBestRegion app={%s}", summary.c_str());
    g_use_cloud_best(self, app_id, method);
    log_settings("UseCloudBestRegion/end", self);
}

void hook_use_cloud_region(void* self, ManagedString* app_id, int32_t region,
                           const MethodInfo* method) {
    const std::string summary = secret_summary(app_id);
    LOGI("trace: ServerSettings.UseCloud(region=%d) app={%s}",
         region, summary.c_str());
    g_use_cloud_region(self, app_id, region, method);
    log_settings("UseCloud(region)/end", self);
}

void hook_use_my_server(void* self, ManagedString* address, int32_t port,
                        ManagedString* app_id, const MethodInfo* method) {
    const std::string host = il2cpp::to_utf8(address, 256);
    const std::string summary = secret_summary(app_id);
    LOGI("trace: ServerSettings.UseMyServer '%s':%d app={%s}",
         host.c_str(), port, summary.c_str());
    g_use_my_server(self, address, port, app_id, method);
    log_settings("UseMyServer/end", self);
}

bool hook_connect_using_settings(ManagedString* game_version,
                                 const MethodInfo* method) {
    enable_verbose_photon_logs();
    const std::string version = il2cpp::to_utf8(game_version, 128);
    LOGI("net: ConnectUsingSettings begin version='%s' state=%d server='%s'",
         version.c_str(), connection_state(), server_address().c_str());
    log_settings("ConnectUsingSettings/before", current_server_settings());
    log_auth_snapshot("ConnectUsingSettings/before");
    const bool result = g_connect_using_settings(game_version, method);
    LOGI("net: ConnectUsingSettings end result=%d state=%d server='%s'",
         result ? 1 : 0, connection_state(), server_address().c_str());
    return result;
}

bool hook_connect_to_master(ManagedString* address, int32_t port,
                            ManagedString* app_id, ManagedString* game_version,
                            const MethodInfo* method) {
    enable_verbose_photon_logs();
    const std::string host = il2cpp::to_utf8(address, 256);
    const std::string version = il2cpp::to_utf8(game_version, 128);
    const std::string app = secret_summary(app_id);
    LOGI("net: ConnectToMaster begin '%s':%d version='%s' app={%s} state=%d",
         host.c_str(), port, version.c_str(), app.c_str(), connection_state());
    log_auth_snapshot("ConnectToMaster/before");
    const bool result = g_connect_to_master(address, port, app_id, game_version, method);
    LOGI("net: ConnectToMaster end result=%d state=%d server='%s'",
         result ? 1 : 0, connection_state(), server_address().c_str());
    return result;
}

ManagedString* hook_get_game_version(const MethodInfo* method) {
    ManagedString* result = g_get_game_version(method);
    const std::string version = il2cpp::to_utf8(result, 128);
    LOGI("net: GameConnect.GetConnectGameVersion -> '%s'", version.c_str());
    return result;
}

bool hook_game_connect(const MethodInfo* method) {
    LOGI("net: GameConnect.ConnectToPhoton begin state=%d", connection_state());
    const bool result = g_game_connect(method);
    LOGI("net: GameConnect.ConnectToPhoton end result=%d state=%d",
         result ? 1 : 0, connection_state());
    return result;
}

void hook_disconnect(const MethodInfo* method) {
    LOGW("net: PhotonNetwork.Disconnect requested state=%d server='%s'",
         connection_state(), server_address().c_str());
    g_disconnect(method);
}

void hook_debug_return(void* self, int32_t level, ManagedString* message,
                       const MethodInfo* method) {
    const std::string text = il2cpp::to_utf8(message, 1024);
    if (level == 1) {
        LOGE("photon[%s/%d]: %s", debug_level_name(level), level, text.c_str());
    } else if (level == 2) {
        LOGW("photon[%s/%d]: %s", debug_level_name(level), level, text.c_str());
    } else {
        LOGI("photon[%s/%d]: %s", debug_level_name(level), level, text.c_str());
    }
    g_debug_return(self, level, message, method);
}

void hook_on_status_changed(void* self, int32_t status,
                            const MethodInfo* method) {
    LOGI("photon-status: %d (%s), state-before=%d",
         status, status_name(status), connection_state());
    g_on_status_changed(self, status, method);
    LOGI("photon-status: handled %d, state-after=%d server='%s'",
         status, connection_state(), server_address().c_str());
}

// OperationResponse в этой сборке Photon — ССЫЛОЧНЫЙ тип
// (dump.cs 13.2.1 L197596: `public class OperationResponse // TypeDefIndex: 2628`),
// поэтому параметр приходит обычным Il2CppObject* и read_field() применим.
// Если апстрим когда-нибудь сделает его struct, тут появится указатель на
// сырую копию без заголовка объекта и object_get_class() читать будет НЕЛЬЗЯ.
void hook_on_operation_response(void* self, void* response,
                                const MethodInfo* method) {
    uint8_t operation = 0;
    int16_t return_code = 0;
    ManagedString* debug_message = nullptr;
    const bool have_operation = read_field(response, "OperationCode", &operation);
    const bool have_code = read_field(response, "ReturnCode", &return_code);
    read_field(response, "DebugMessage", &debug_message);
    const std::string debug = il2cpp::to_utf8(debug_message, 512);

    if (have_code && return_code != 0) {
        LOGE("photon-op: code=%s%u return=%d debug='%s'",
             have_operation ? "" : "?", static_cast<unsigned>(operation),
             static_cast<int>(return_code), debug.c_str());
    } else {
        LOGI("photon-op: code=%s%u return=%s%d debug='%s'",
             have_operation ? "" : "?", static_cast<unsigned>(operation),
             have_code ? "" : "?", static_cast<int>(return_code), debug.c_str());
    }
    g_on_operation_response(self, response, method);
}

void hook_failed_to_connect(void* self, int32_t cause,
                            const MethodInfo* method) {
    LOGE("ui: ConnectionControl.OnFailedToConnect cause=%d (%s) state=%d server='%s'",
         cause, disconnect_name(cause), connection_state(), server_address().c_str());
    g_failed_to_connect(self, cause, method);
}

void hook_disconnected(void* self, const MethodInfo* method) {
    LOGW("ui: ConnectionControl.OnDisconnected state=%d server='%s'",
         connection_state(), server_address().c_str());
    g_disconnected(self, method);
}

void hook_connected_to_master(void* self, const MethodInfo* method) {
    LOGI("ui: ConnectionControl.OnConnectedToMaster state=%d server='%s'",
         connection_state(), server_address().c_str());
    g_connected_to_master(self, method);
}

template <typename Fn>
void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

bool optional(const hook::ManagedMethod& target, void* replace, void** original,
              int* installed) {
    if (hook::install(target, replace, original, false)) {
        ++(*installed);
        return true;
    }
    return false;
}

} // namespace

bool install_hooks() {
#if defined(__ANDROID__)
    static_assert(sizeof(void*) == 4, "PG3D 13.2.1 target must be armeabi-v7a");
#endif

    LOGI("hook: engine %s; resolving targets through IL2CPP metadata",
         hook::engine_version());

    int installed = 0;
    const bool core = hook::install(
        {"", "Switcher", "SelectPhotonAppId", 1},
        replacement(&hook_select_app_id), original_slot(&g_select_app_id), true);
    if (core) ++installed;

    optional({"", "Switcher", "SetUpPhoton", 1},
             replacement(&hook_set_up_photon), original_slot(&g_set_up_photon), &installed);
    optional({"", "ServerSettings", "UseCloud", 1},
             replacement(&hook_use_cloud), original_slot(&g_use_cloud), &installed);
    optional({"", "ServerSettings", "UseCloudBestRegion", 1},
             replacement(&hook_use_cloud_best), original_slot(&g_use_cloud_best), &installed);
    optional({"", "ServerSettings", "UseCloud", 2},
             replacement(&hook_use_cloud_region), original_slot(&g_use_cloud_region), &installed);
    optional({"", "ServerSettings", "UseMyServer", 3},
             replacement(&hook_use_my_server), original_slot(&g_use_my_server), &installed);
    optional({"", "PhotonNetwork", "ConnectUsingSettings", 1},
             replacement(&hook_connect_using_settings),
             original_slot(&g_connect_using_settings), &installed);
    optional({"", "PhotonNetwork", "ConnectToMaster", 4},
             replacement(&hook_connect_to_master), original_slot(&g_connect_to_master), &installed);
    optional({"", "PhotonNetwork", "Disconnect", 0},
             replacement(&hook_disconnect), original_slot(&g_disconnect), &installed);
    optional({"", "GameConnect", "GetConnectGameVersion", 0},
             replacement(&hook_get_game_version), original_slot(&g_get_game_version), &installed);
    optional({"", "GameConnect", "ConnectToPhoton", 0},
             replacement(&hook_game_connect), original_slot(&g_game_connect), &installed);
    optional({"", "NetworkingPeer", "DebugReturn", 2},
             replacement(&hook_debug_return), original_slot(&g_debug_return), &installed);
    optional({"", "NetworkingPeer", "OnStatusChanged", 1},
             replacement(&hook_on_status_changed), original_slot(&g_on_status_changed), &installed);
    optional({"", "NetworkingPeer", "OnOperationResponse", 1},
             replacement(&hook_on_operation_response),
             original_slot(&g_on_operation_response), &installed);
    optional({"", "ConnectionControl", "OnFailedToConnect", 1},
             replacement(&hook_failed_to_connect), original_slot(&g_failed_to_connect), &installed);
    optional({"", "ConnectionControl", "OnDisconnected", 0},
             replacement(&hook_disconnected), original_slot(&g_disconnected), &installed);
    optional({"", "ConnectionControl", "OnConnectedToMaster", 0},
             replacement(&hook_connected_to_master),
             original_slot(&g_connected_to_master), &installed);

    LOGI("hook: installed %d managed hooks (core=%s)", installed, core ? "OK" : "FAILED");

    // ВАЖНО: здесь НЕЛЬЗЯ трогать статические поля PhotonNetwork.
    // install_hooks() выполняется на нашем фоновом потоке сразу после того,
    // как в домене появился Assembly-CSharp.dll — задолго до того, как
    // отработает статический конструктор PhotonNetwork.
    // il2cpp_field_static_(get|set)_value адресует klass->static_fields + offset,
    // и пока класс не инициализирован эта база равна nullptr: чтение
    // PhotonServerSettings (+0x10) или запись logLevel (+0x18) вырождаются в
    // разыменование 0x10/0x18 и роняют процесс прямо на старте игры.
    // Всю работу с настройками делает хук SetUpPhoton: он вызывается уже из
    // main-потока игры и строго ПОСЛЕ оригинала, когда PhotonNetwork уже жив.
    LOGI("settings[post-install]: deferred to SetUpPhoton hook "
         "(PhotonNetwork statics are not safe to touch yet)");

    if (std::strlen(PHOTON_APP_ID) == 0u) {
        LOGW("appid: build has no PHOTON_APP_ID secret; hook is diagnostic passthrough");
    }
    return core;
}

} // namespace photon
