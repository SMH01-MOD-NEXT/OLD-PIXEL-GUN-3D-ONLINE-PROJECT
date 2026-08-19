#include "photon_hook.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <shadowhook.h>

#include "config.h"
#include "il2cpp.h"
#include "log.h"

// ---------------------------------------------------------------------------
// Смещения и адреса восстановлены из dump.cs (PG3D 12.5.0, IL2CPP metadata v22)
// и побайтово сверены с libil2cpp.so (прологи функций совпадают, код ARM-режим,
// поэтому thumb-бит НЕ выставляем).
//
//   PhotonNetwork.ConnectUsingSettings(string)   VA 0xC8002C
//   ServerSettings.HostType        0x0C  (enum: 1 = PhotonCloud, 2 = SelfHosted)
//   ServerSettings.Protocol        0x10  (enum: 0 = Udp, 1 = Tcp, 4 = WebSocket)
//   ServerSettings.ServerAddress   0x14  (Il2CppString*)
//   ServerSettings.ServerPort      0x18  (int)
//   ServerSettings.AppID           0x1C  (Il2CppString*)
//   ServerSettings.VoiceAppID      0x20  (Il2CppString*)
// ---------------------------------------------------------------------------
namespace {

constexpr uintptr_t kConnectUsingSettingsRva = 0xC8002C;

constexpr int32_t kHostingPhotonCloud = 1;
constexpr int32_t kHostingSelfHosted  = 2;
constexpr int32_t kProtocolUdp        = 0;

constexpr ptrdiff_t kOffHostType      = 0x0C;
constexpr ptrdiff_t kOffProtocol      = 0x10;
constexpr ptrdiff_t kOffServerAddress = 0x14;
constexpr ptrdiff_t kOffServerPort    = 0x18;
constexpr ptrdiff_t kOffAppId         = 0x1C;
constexpr ptrdiff_t kOffVoiceAppId    = 0x20;

// У IL2CPP-статиков последним скрытым параметром идёт MethodInfo* —
// пробрасываем его в оригинал, иначе в r1 окажется мусор.
using ConnectUsingSettings_t = bool (*)(void* gameVersion, void* method);
ConnectUsingSettings_t g_orig = nullptr;
bool g_applied = false;

template <typename T>
void write_field(void* obj, ptrdiff_t off, T value) {
    *reinterpret_cast<T*>(reinterpret_cast<char*>(obj) + off) = value;
}

// Разово переписывает поля статического объекта PhotonNetwork.PhotonServerSettings.
void apply_photon_override() {
    if (g_applied) return;

    void* image = il2cpp::find_image("Assembly-CSharp.dll");
    if (!image) {
        LOGW("Assembly-CSharp image ещё не загружен, повторим при следующем вызове");
        return;
    }

    void* klass = il2cpp::class_from_name(image, "", "PhotonNetwork");
    if (!klass) {
        LOGE("класс PhotonNetwork не найден!");
        return;
    }

    // Статик-конструктор PhotonNetwork грузит Resources/PhotonServerSettings.asset.
    // Инициализируем класс сами, иначе статик-поле может быть ещё null.
    if (il2cpp::runtime_class_init) {
        il2cpp::runtime_class_init(klass);
    }

    void* field = il2cpp::class_get_field_from_name(klass, "PhotonServerSettings");
    if (!field) {
        LOGE("статик-поле PhotonServerSettings не найдено!");
        return;
    }

    void* settings = nullptr;
    il2cpp::field_static_get_value(field, &settings);
    if (!settings) {
        LOGW("PhotonServerSettings == null (ассет не загрузился?), повторим позже");
        return;
    }

#ifdef PHOTON_MODE_SELFHOSTED
    // Свой Photon Server OnPremise: хост/порт свои, AppID = имя приложения на сервере.
    write_field<int32_t>(settings, kOffHostType, kHostingSelfHosted);
    write_field<int32_t>(settings, kOffProtocol, kProtocolUdp);
    write_field<void*>(settings, kOffServerAddress, il2cpp::string_new(PHOTON_SERVER_ADDRESS));
    write_field<int32_t>(settings, kOffServerPort, PHOTON_SERVER_PORT);
    LOGI("режим SelfHosted -> %s:%d", PHOTON_SERVER_ADDRESS, PHOTON_SERVER_PORT);
#else
    // Свой Photon Cloud: адреса штатные (ns.exitgames.com), меняется только AppID.
    // Опционально можно зафиксировать адрес/порт (фиксированный регион и т.п.).
    write_field<int32_t>(settings, kOffHostType, kHostingPhotonCloud);
    if (std::strlen(PHOTON_SERVER_ADDRESS) > 0) {
        write_field<void*>(settings, kOffServerAddress, il2cpp::string_new(PHOTON_SERVER_ADDRESS));
        write_field<int32_t>(settings, kOffServerPort, PHOTON_SERVER_PORT);
        LOGI("режим Cloud, фикс. адрес -> %s:%d", PHOTON_SERVER_ADDRESS, PHOTON_SERVER_PORT);
    } else {
        LOGI("режим Cloud, штатный Name Server, меняем только AppID");
    }
#endif

    if (std::strlen(PHOTON_APP_ID) > 0) {
        write_field<void*>(settings, kOffAppId, il2cpp::string_new(PHOTON_APP_ID));
        write_field<void*>(settings, kOffVoiceAppId, il2cpp::string_new(PHOTON_APP_ID));
        LOGI("AppID подменён (длина %zu)", std::strlen(PHOTON_APP_ID));
    } else {
        LOGW("PHOTON_APP_ID пуст (сборка без секрета) — AppID НЕ меняем, игра пойдёт на мёртвые официальные серверы");
    }

    g_applied = true;
    LOGI("PhotonServerSettings переписан, отдаём управление оригиналу");
}

bool hooked_ConnectUsingSettings(void* gameVersion, void* method) {
    apply_photon_override();
    return g_orig(gameVersion, method);
}

} // namespace

namespace photon {

bool install_hook(void* il2cpp_base) {
    void* target = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(il2cpp_base) + kConnectUsingSettingsRva);

    void* err = shadowhook_hook_func_addr(
        target,
        reinterpret_cast<void*>(&hooked_ConnectUsingSettings),
        reinterpret_cast<void**>(&g_orig));

    if (err != nullptr || g_orig == nullptr) {
        int e = shadowhook_get_errno();
        LOGE("shadowhook_hook_func_addr упал: %d (%s)", e, shadowhook_to_errmsg(e));
        return false;
    }

    LOGI("хук установлен: ConnectUsingSettings @ %p (orig %p)", target, reinterpret_cast<void*>(g_orig));
    return true;
}

} // namespace photon
