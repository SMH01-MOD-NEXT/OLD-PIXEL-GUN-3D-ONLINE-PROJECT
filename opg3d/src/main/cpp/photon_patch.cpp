#include "photon_patch.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "config.h"
#include "il2cpp.h"
#include "log.h"

// ---------------------------------------------------------------------------
// Смещения полей ServerSettings восстановлены из dump.cs (PG3D 12.5.0,
// IL2CPP metadata v22, 32-бит). Заголовок объекта 8 байт + m_CachedPtr от
// UnityEngine.Object на 0x08, поэтому свои поля начинаются с 0x0C.
//
//   HostType       0x0C  (1 = PhotonCloud, 2 = SelfHosted)
//   Protocol       0x10  (0 = Udp, 1 = Tcp, 4 = WebSocket, 5 = WebSocketSecure)
//   ServerAddress  0x14  (Il2CppString*)
//   ServerPort     0x18  (int)
//   AppID          0x1C  (Il2CppString*)
//   VoiceAppID     0x20  (Il2CppString*)
// ---------------------------------------------------------------------------
namespace photon {
namespace {

constexpr ptrdiff_t kOffHostType      = 0x0C;
constexpr ptrdiff_t kOffProtocol      = 0x10;
constexpr ptrdiff_t kOffServerAddress = 0x14;
constexpr ptrdiff_t kOffServerPort    = 0x18;
constexpr ptrdiff_t kOffAppId         = 0x1C;
constexpr ptrdiff_t kOffVoiceAppId    = 0x20;

constexpr int32_t kHostingPhotonCloud = 1;
constexpr int32_t kHostingSelfHosted  = 2;
constexpr int32_t kProtocolUdp        = 0;

void* g_field = nullptr;           // FieldInfo* PhotonNetwork.PhotonServerSettings
void* g_last_settings = nullptr;   // объект, который мы уже правили
void* g_written_appid = nullptr;   // строка, которую мы туда положили

template <typename T>
void write_field(void* obj, ptrdiff_t off, T value) {
    std::memcpy(reinterpret_cast<char*>(obj) + off, &value, sizeof(T));
}

template <typename T>
T read_field(void* obj, ptrdiff_t off) {
    T value{};
    std::memcpy(&value, reinterpret_cast<const char*>(obj) + off, sizeof(T));
    return value;
}

bool ensure_field() {
    if (g_field != nullptr) return true;

    void* image = il2cpp::find_image("Assembly-CSharp.dll");
    if (image == nullptr) return false;

    void* klass = il2cpp::class_from_name(image, "", "PhotonNetwork");
    if (klass == nullptr) return false;

    // Этот вызов готовит раскладку класса (не путать со статическим
    // конструктором): блок статических полей выделяется и обнуляется,
    // поэтому читать его безопасно даже до того, как игра тронет Photon.
    g_field = il2cpp::class_get_field_from_name(klass, "PhotonServerSettings");
    return g_field != nullptr;
}

} // namespace

ApplyResult apply() {
    if (!ensure_field()) return ApplyResult::NoClass;

    void* settings = nullptr;
    il2cpp::field_static_get_value(g_field, &settings);
    if (settings == nullptr) return ApplyResult::NotReady;

    // Уже наш объект и наша строка — игра ничего не перезагружала.
    if (settings == g_last_settings && read_field<void*>(settings, kOffAppId) == g_written_appid) {
        return ApplyResult::AlreadyApplied;
    }

#ifdef PHOTON_MODE_SELFHOSTED
    write_field<int32_t>(settings, kOffHostType, kHostingSelfHosted);
    write_field<int32_t>(settings, kOffProtocol, kProtocolUdp);
    write_field<void*>(settings, kOffServerAddress, il2cpp::string_new(PHOTON_SERVER_ADDRESS));
    write_field<int32_t>(settings, kOffServerPort, PHOTON_SERVER_PORT);
    LOGI("patch: режим SelfHosted -> %s:%d", PHOTON_SERVER_ADDRESS, PHOTON_SERVER_PORT);
#else
    write_field<int32_t>(settings, kOffHostType, kHostingPhotonCloud);
    if (std::strlen(PHOTON_SERVER_ADDRESS) > 0) {
        write_field<void*>(settings, kOffServerAddress, il2cpp::string_new(PHOTON_SERVER_ADDRESS));
        write_field<int32_t>(settings, kOffServerPort, PHOTON_SERVER_PORT);
        LOGI("patch: режим Cloud, фикс. адрес -> %s:%d", PHOTON_SERVER_ADDRESS, PHOTON_SERVER_PORT);
    } else {
        LOGI("patch: режим Cloud, штатный Name Server, меняем только AppID");
    }
#endif

    if (std::strlen(PHOTON_APP_ID) > 0) {
        void* app_id = il2cpp::string_new(PHOTON_APP_ID);
        write_field<void*>(settings, kOffAppId, app_id);
        write_field<void*>(settings, kOffVoiceAppId, app_id);
        g_written_appid = app_id;
        LOGI("patch: AppID подменён (длина %zu)", std::strlen(PHOTON_APP_ID));
    } else {
        g_written_appid = read_field<void*>(settings, kOffAppId);
        LOGW("patch: PHOTON_APP_ID пуст (сборка без секрета) — AppID НЕ меняем, "
             "игра пойдёт на мёртвые официальные серверы");
    }

    g_last_settings = settings;
    return ApplyResult::Applied;
}

} // namespace photon
