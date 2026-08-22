#pragma once

#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <cstring>

#include "config.h"
#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Photon/PUN compatibility for the supplied obfuscated PG3D 23.1.3 ARM64
// build (libil2cpp.so SHA-256
// f0a130c4e8d9487059eab4b0f08462f6aa7d057510cada0fd6fb3043c77deb5c).
//
// Obfuscated identifiers below were remapped independently from the 16.1.0
// dump. The mapping uses the PUN 1.91 class layout, ordered method signatures,
// stable ServerSettings fields/enums and the AArch64 direct-call graph. No
// 16.1.0 ARM32 RVA, field offset or opcode is reused.
//
// This module changes only the connection route:
//   * Switcher returns the configured Photon AppID;
//   * ServerSettings is put on Photon Cloud / EU through its own UseCloud API;
//   * PUN offlineMode is cleared immediately before ConnectUsingSettings;
//   * FriendsController.Update is quarantined only while Photon is connecting
//     or connected, preserving the old project's dead-backend guard.
// Rooms, matchmaking, RPCs, callbacks and manual disconnects remain stock.
namespace photon_2313 {
namespace detail {

using MethodInfo = void;
using ManagedString = void;

// Unity/IL2CPP in 23.1.3 no longer passes the old ARM32 dummy static-context
// argument. Static methods receive only their managed arguments followed by
// MethodInfo; instance methods still receive `this` first.
using SelectAppIdFn = ManagedString* (*)(void* hidden_settings,
                                         const MethodInfo* method);
using SetUpPhotonFn = void (*)(void* hidden_settings,
                              const MethodInfo* method);
using ConnectUsingSettingsFn = bool (*)(ManagedString* game_version,
                                        const MethodInfo* method);
using UseCloudRegionFn = void (*)(void* settings, ManagedString* app_id,
                                  int32_t region,
                                  const MethodInfo* method);
using StaticGetIntFn = int32_t (*)(const MethodInfo* method);
using StaticGetBoolFn = bool (*)(const MethodInfo* method);
using StaticSetBoolFn = void (*)(bool value, const MethodInfo* method);
using InstanceVoidFn = void (*)(void* self, const MethodInfo* method);

inline constexpr const char* kPhotonNetworkClass =
    u8"丟丝专丄丑世丞世丒";
inline constexpr const char* kPhotonSettingsField =
    u8"丁丟丄业东东业不且";
inline constexpr const char* kConnectionStateMethod =
    u8"丝三丒丙丛丄丂丟丒";
inline constexpr const char* kOfflineGetterMethod =
    u8"丑丂丞下世东丆丝不";
inline constexpr const char* kOfflineSetterMethod =
    u8"丟不业丏七丗丈丌丁";
inline constexpr const char* kConnectUsingSettingsMethod =
    u8"丏东丁丕专世丈丄上";
inline constexpr const char* kUseCloudMethod =
    u8"一不丘世上专丞丛世";
inline constexpr const char* kSetUpPhotonMethod =
    u8"丒与下丐丕丏东丆不";
inline constexpr const char* kSelectAppIdMethod =
    u8"丄丟丒丏丈丘不丁丁";

inline constexpr int32_t kPhotonCloudHost = 1;
inline constexpr int32_t kForcedRegionEu = 0;
inline constexpr int32_t kDisconnectedState = 15;

inline SelectAppIdFn g_select_app_id = nullptr;
inline SetUpPhotonFn g_set_up_photon = nullptr;
inline ConnectUsingSettingsFn g_connect_using_settings = nullptr;
inline InstanceVoidFn g_friends_update = nullptr;

inline void* g_photon_settings_field = nullptr;
inline const MethodInfo* g_use_cloud_info = nullptr;
inline UseCloudRegionFn g_use_cloud = nullptr;
inline const MethodInfo* g_state_info = nullptr;
inline StaticGetIntFn g_get_state = nullptr;
inline const MethodInfo* g_offline_get_info = nullptr;
inline StaticGetBoolFn g_get_offline = nullptr;
inline const MethodInfo* g_offline_set_info = nullptr;
inline StaticSetBoolFn g_set_offline = nullptr;

inline std::atomic<bool> g_connect_started{false};
inline std::atomic<uint32_t> g_route_applies{0u};
inline std::atomic<uint32_t> g_blocked_backend_ticks{0u};
inline std::atomic<bool> g_empty_app_logged{false};

uint32_t fnv1a(const char* text, size_t size) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; ++i) {
        hash ^= static_cast<uint8_t>(text[i]);
        hash *= 16777619u;
    }
    return hash;
}

template <typename T>
bool read_field(void* object, const char* name, T* out) {
    static_assert(sizeof(T) <= 8, "field helper supports scalars and pointers");
    if (object == nullptr || out == nullptr ||
        il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_field_from_name == nullptr ||
        il2cpp::field_get_value == nullptr) {
        return false;
    }
    void* klass = il2cpp::object_get_class(object);
    if (klass == nullptr) return false;
    void* field = il2cpp::class_get_field_from_name(klass, name);
    if (field == nullptr) return false;
    alignas(8) uint8_t scratch[16] = {0};
    il2cpp::field_get_value(object, field, scratch);
    std::memcpy(out, scratch, sizeof(T));
    return true;
}

template <typename T>
bool write_field(void* object, const char* name, T value) {
    if (object == nullptr || il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_field_from_name == nullptr ||
        il2cpp::field_set_value == nullptr) {
        return false;
    }
    void* klass = il2cpp::object_get_class(object);
    if (klass == nullptr) return false;
    void* field = il2cpp::class_get_field_from_name(klass, name);
    if (field == nullptr) return false;
    il2cpp::field_set_value(object, field, &value);
    return true;
}

bool resolve_method(const char* klass, const char* name, int args,
                    void** function, const MethodInfo** info) {
    void* method = il2cpp::find_method_info("", klass, name, args);
    void* pointer = method != nullptr ? il2cpp::method_pointer(method) : nullptr;
    if (method == nullptr || pointer == nullptr) {
        LOGE("23.1.3-photon: cannot resolve %s.%s/%d", klass, name, args);
        return false;
    }
    *function = pointer;
    *info = method;
    return true;
}

void* current_server_settings() {
    if (g_photon_settings_field == nullptr) {
        g_photon_settings_field = il2cpp::find_field(
            "", kPhotonNetworkClass, kPhotonSettingsField);
    }
    if (g_photon_settings_field == nullptr ||
        il2cpp::field_static_get_value == nullptr) {
        return nullptr;
    }
    void* settings = nullptr;
    il2cpp::field_static_get_value(g_photon_settings_field, &settings);
    return settings;
}

int32_t connection_state() {
    if (g_get_state == nullptr &&
        !resolve_method(kPhotonNetworkClass, kConnectionStateMethod, 0,
                        reinterpret_cast<void**>(&g_get_state),
                        &g_state_info)) {
        return -1;
    }
    return g_get_state != nullptr ? g_get_state(g_state_info) : -1;
}

bool ensure_online_mode() {
    if (g_get_offline == nullptr &&
        !resolve_method(kPhotonNetworkClass, kOfflineGetterMethod, 0,
                        reinterpret_cast<void**>(&g_get_offline),
                        &g_offline_get_info)) {
        return false;
    }
    if (g_set_offline == nullptr &&
        !resolve_method(kPhotonNetworkClass, kOfflineSetterMethod, 1,
                        reinterpret_cast<void**>(&g_set_offline),
                        &g_offline_set_info)) {
        return false;
    }

    const bool before = g_get_offline(g_offline_get_info);
    if (before) {
        LOGW("23.1.3-photon: PUN offlineMode was set by an auth fallback; "
             "clearing it before online connect");
        g_set_offline(false, g_offline_set_info);
    }
    const bool after = g_get_offline(g_offline_get_info);
    if (after) {
        LOGE("23.1.3-photon: PUN offlineMode stayed enabled; online connect "
             "will be refused by the stock client");
        return false;
    }
    return true;
}

bool force_cloud(const char* point) {
#ifdef PHOTON_MODE_SELFHOSTED
    (void)point;
    return true;
#else
    const size_t app_length = std::strlen(PHOTON_APP_ID);
    if (app_length == 0u) {
        if (!g_empty_app_logged.exchange(true)) {
            LOGE("23.1.3-photon[%s]: PHOTON_APP_ID is empty", point);
        }
        return false;
    }

    void* settings = current_server_settings();
    if (settings == nullptr) {
        LOGW("23.1.3-photon[%s]: PhotonServerSettings is not ready", point);
        return false;
    }
    ManagedString* app_id = il2cpp::string_new != nullptr
                                ? il2cpp::string_new(PHOTON_APP_ID)
                                : nullptr;
    if (app_id == nullptr) {
        LOGE("23.1.3-photon[%s]: il2cpp_string_new failed", point);
        return false;
    }

    if (g_use_cloud == nullptr) {
        (void)resolve_method("ServerSettings", kUseCloudMethod, 2,
                             reinterpret_cast<void**>(&g_use_cloud),
                             &g_use_cloud_info);
    }
    bool called_sdk = false;
    if (g_use_cloud != nullptr && g_use_cloud_info != nullptr) {
        g_use_cloud(settings, app_id, kForcedRegionEu, g_use_cloud_info);
        called_sdk = true;
    }

    int32_t host = -1;
    int32_t region = -1;
    ManagedString* stored_app = nullptr;
    bool have_host = read_field(settings, "HostType", &host);
    bool have_region = read_field(settings, "PreferredRegion", &region);
    bool have_app = read_field(settings, "AppID", &stored_app);
    if (!have_host || host != kPhotonCloudHost ||
        !have_region || region != kForcedRegionEu ||
        !have_app || stored_app != app_id) {
        (void)write_field<int32_t>(settings, "HostType", kPhotonCloudHost);
        (void)write_field<int32_t>(settings, "PreferredRegion", kForcedRegionEu);
        (void)write_field(settings, "AppID", app_id);
        have_host = read_field(settings, "HostType", &host);
        have_region = read_field(settings, "PreferredRegion", &region);
        have_app = read_field(settings, "AppID", &stored_app);
    }

    const bool ready = have_host && host == kPhotonCloudHost &&
                       have_region && region == kForcedRegionEu &&
                       have_app && stored_app == app_id;
    const uint32_t apply = g_route_applies.fetch_add(1u) + 1u;
    LOGI("23.1.3-photon[%s]: apply#%" PRIu32
         " sdk=%d host=%d region=%d appChars=%zu appFNV=%08" PRIx32
         " ready=%d",
         point, apply, called_sdk ? 1 : 0, host, region, app_length,
         fnv1a(PHOTON_APP_ID, app_length), ready ? 1 : 0);
    if (!ready) {
        LOGE("23.1.3-photon[%s]: PhotonCloud/eu route verification failed",
             point);
    }
    return ready;
#endif
}

ManagedString* hook_select_app_id(void* hidden_settings,
                                  const MethodInfo* method) {
    const size_t length = std::strlen(PHOTON_APP_ID);
    if (length != 0u && il2cpp::string_new != nullptr) {
        ManagedString* replacement = il2cpp::string_new(PHOTON_APP_ID);
        if (replacement != nullptr) {
            LOGI("23.1.3-photon: mapped Switcher AppID selector -> configured "
                 "credential (chars=%zu fnv=%08" PRIx32 ")",
                 length, fnv1a(PHOTON_APP_ID, length));
            return replacement;
        }
    }
    LOGW("23.1.3-photon: AppID selector passthrough (build secret missing "
         "or string allocation failed)");
    return g_select_app_id != nullptr
               ? g_select_app_id(hidden_settings, method)
               : nullptr;
}

void hook_set_up_photon(void* hidden_settings, const MethodInfo* method) {
    if (g_set_up_photon != nullptr) {
        g_set_up_photon(hidden_settings, method);
    }
    (void)force_cloud("Switcher.SetUpPhoton/end");
}

bool hook_connect_using_settings(ManagedString* game_version,
                                 const MethodInfo* method) {
    g_connect_started.store(true, std::memory_order_release);
    const bool online_mode = ensure_online_mode();
    const bool route = force_cloud("PUN.ConnectUsingSettings");
    LOGI("23.1.3-photon: ConnectUsingSettings begin state=%d "
         "onlineMode=%d route=%d",
         connection_state(), online_mode ? 1 : 0, route ? 1 : 0);
    if (g_connect_using_settings == nullptr) return false;
    const bool result = g_connect_using_settings(game_version, method);
    LOGI("23.1.3-photon: ConnectUsingSettings returned %d state=%d",
         result ? 1 : 0, connection_state());
    return result;
}

void hook_friends_update(void* self, const MethodInfo* method) {
    if (g_connect_started.load(std::memory_order_acquire)) {
        const int32_t state = connection_state();
        if (state >= 2 && state != kDisconnectedState) {
            const uint32_t count = g_blocked_backend_ticks.fetch_add(1u) + 1u;
            if (count <= 8u || count % 120u == 0u) {
                LOGW("23.1.3-photon: skipped FriendsController.Update #%" PRIu32
                     " state=%d; retired social/backend tick cannot disconnect "
                     "PUN", count, state);
            }
            return;
        }
    }
    if (g_friends_update != nullptr) g_friends_update(self, method);
}

template <typename Fn>
void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

bool install_optional(const hook::ManagedMethod& target, void* replace,
                      void** original, int* installed) {
    if (hook::install(target, replace, original, false)) {
        ++(*installed);
        return true;
    }
    return false;
}

} // namespace detail

inline bool install_hooks() {
#if defined(__ANDROID__)
    static_assert(sizeof(void*) == 8,
                  "PG3D 23.1.3 target must be arm64-v8a");
#endif

    int installed = 0;
    const bool app_id = hook::install(
        {"", "Switcher", detail::kSelectAppIdMethod, 1},
        detail::replacement(&detail::hook_select_app_id),
        detail::original_slot(&detail::g_select_app_id), true);
    if (app_id) ++installed;

    const bool connect = hook::install(
        {"", detail::kPhotonNetworkClass,
         detail::kConnectUsingSettingsMethod, 1},
        detail::replacement(&detail::hook_connect_using_settings),
        detail::original_slot(&detail::g_connect_using_settings), true);
    if (connect) ++installed;

    const bool backend_guard = hook::install(
        {"", "FriendsController", "Update", 0},
        detail::replacement(&detail::hook_friends_update),
        detail::original_slot(&detail::g_friends_update), true);
    if (backend_guard) ++installed;

    (void)detail::install_optional(
        {"", "Switcher", detail::kSetUpPhotonMethod, 1},
        detail::replacement(&detail::hook_set_up_photon),
        detail::original_slot(&detail::g_set_up_photon), &installed);

    LOGI("23.1.3-photon: installed %d hooks "
         "(appid=%s connect=%s backend-guard=%s; mapped from independent "
         "23.1.3 metadata and A64 call graph)",
         installed, app_id ? "OK" : "FAILED",
         connect ? "OK" : "FAILED",
         backend_guard ? "OK" : "FAILED");
    if (std::strlen(PHOTON_APP_ID) == 0u) {
        LOGW("23.1.3-photon: build has no PHOTON_APP_ID; online route is "
             "diagnostic passthrough until a trusted build supplies it");
    }
    return app_id && connect && backend_guard;
}

} // namespace photon_2313
