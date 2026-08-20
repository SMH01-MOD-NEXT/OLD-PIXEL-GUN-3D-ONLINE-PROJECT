#pragma once

#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <cstring>

#include "config.h"
#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Compatibility layer for the dead PG3D 12.5.0 backend.
//
// The original build leaves PUN in SelfHosted mode and points it at the dead
// rilisoft-us endpoint. It also lets FriendsController.Update disconnect PUN
// while the peer is authenticating. BestRegion additionally depends on an
// asynchronous ping/cache warm-up and can split clients between regional room
// pools. This layer fixes those pieces by routing every connection through the
// SDK's explicit UseCloud(appId, CloudRegionCode.eu) path and quarantining
// FriendsController.Update while a Photon session is active.
// Manual disconnects, Photon status callbacks and room/game networking are not
// replaced or globally faked.
namespace cloud_guard {
namespace detail {

using MethodInfo = void;
using ManagedString = void;
using InstanceVoidFn = void (*)(void* self, const MethodInfo* method);
using ConnectTierFn = bool (*)(int32_t tier, const MethodInfo* method);
using StaticBoolFn = bool (*)(const MethodInfo* method);
using UseCloudRegionFn = void (*)(void* self, ManagedString* app_id,
                                  int32_t region, const MethodInfo* method);
using GetIntFn = int32_t (*)(const MethodInfo* method);

// dump1250.cs: ServerSettings.HostingOption.PhotonCloud = 1,
// CloudRegionCode.eu = 0. Keep this fixed so every client enters the same
// regional room pool without a first-launch BestRegion warm-up.
inline constexpr int32_t kPhotonCloudHost = 1;
inline constexpr int32_t kForcedRegionEu = 0;

inline InstanceVoidFn g_friends_update = nullptr;
inline InstanceVoidFn g_connect_scene_go = nullptr;
inline InstanceVoidFn g_connection_control_connect = nullptr;
inline ConnectTierFn g_game_connect_tier = nullptr;
inline StaticBoolFn g_game_connect_squad = nullptr;

inline std::atomic<bool> g_cloud_ready{false};
inline std::atomic<uint32_t> g_cloud_apply_count{0u};
inline std::atomic<uint32_t> g_quarantined_updates{0u};
inline std::atomic<bool> g_missing_app_id_logged{false};

template <typename T>
bool read_field(void* object, const char* name, T* value) {
    static_assert(sizeof(T) <= 8, "field helper supports scalars and pointers");
    if (object == nullptr || value == nullptr || !il2cpp::object_get_class ||
        !il2cpp::class_get_field_from_name || !il2cpp::field_get_value) {
        return false;
    }
    void* klass = il2cpp::object_get_class(object);
    if (klass == nullptr) return false;
    void* field = il2cpp::class_get_field_from_name(klass, name);
    if (field == nullptr) return false;
    alignas(8) uint8_t scratch[16] = {0};
    il2cpp::field_get_value(object, field, scratch);
    std::memcpy(value, scratch, sizeof(T));
    return true;
}

inline void* current_server_settings() {
    static void* field = nullptr;
    if (field == nullptr) {
        field = il2cpp::find_field("", "PhotonNetwork", "PhotonServerSettings");
    }
    if (field == nullptr || il2cpp::field_static_get_value == nullptr) return nullptr;
    void* settings = nullptr;
    il2cpp::field_static_get_value(field, &settings);
    return settings;
}

inline int32_t connection_state() {
    static void* method = nullptr;
    static GetIntFn fn = nullptr;
    if (fn == nullptr) {
        method = il2cpp::find_method_info(
            "", "PhotonNetwork", "get_connectionStateDetailed", 0);
        fn = reinterpret_cast<GetIntFn>(il2cpp::method_pointer(method));
    }
    return fn != nullptr ? fn(method) : -1;
}

inline const char* state_name(int32_t state) {
    switch (state) {
        case 0: return "Uninitialized";
        case 1: return "PeerCreated";
        case 2: return "Queued";
        case 3: return "Authenticated";
        case 4: return "JoinedLobby";
        case 5: return "DisconnectingFromMasterserver";
        case 6: return "ConnectingToGameserver";
        case 7: return "ConnectedToGameserver";
        case 8: return "Joining";
        case 9: return "Joined";
        case 10: return "Leaving";
        case 11: return "DisconnectingFromGameserver";
        case 12: return "ConnectingToMasterserver";
        case 13: return "QueuedComingFromGameserver";
        case 14: return "Disconnecting";
        case 15: return "Disconnected";
        case 16: return "ConnectedToMaster";
        case 17: return "ConnectingToNameServer";
        case 18: return "ConnectedToNameServer";
        case 19: return "DisconnectingFromNameServer";
        case 20: return "Authenticating";
        default: return "unknown";
    }
}

// FriendsController is the dead HTTP/social backend owner. Its Update method
// contains the proven call site FriendsController.Update+0x310 that repeatedly
// calls PhotonNetwork.Disconnect. Quarantine only while PUN has an active or
// transitioning session; when PUN is idle/disconnected, the original Update
// still runs so local initialization and UI state are preserved.
inline bool should_quarantine_friends_update(int32_t state) {
    return state >= 2 && state != 15;
}

inline bool force_cloud(const char* point, bool reapply) {
#ifdef PHOTON_MODE_SELFHOSTED
    (void)point;
    (void)reapply;
    return true;
#else
    if (!reapply && g_cloud_ready.load(std::memory_order_acquire)) return true;

    if (std::strlen(PHOTON_APP_ID) == 0u) {
        if (!g_missing_app_id_logged.exchange(true)) {
            LOGE("cloud-force[%s]: PHOTON_APP_ID is empty; refusing fake success",
                 point);
        }
        return false;
    }

    void* settings = current_server_settings();
    if (settings == nullptr) {
        LOGW("cloud-force[%s]: PhotonServerSettings is not ready", point);
        return false;
    }

    ManagedString* app_id = il2cpp::string_new != nullptr
                                ? il2cpp::string_new(PHOTON_APP_ID)
                                : nullptr;
    if (app_id == nullptr) {
        LOGE("cloud-force[%s]: il2cpp_string_new failed", point);
        return false;
    }

    static void* use_cloud_method = nullptr;
    static UseCloudRegionFn use_cloud = nullptr;
    if (use_cloud == nullptr) {
        use_cloud_method = il2cpp::find_method_info(
            "", "ServerSettings", "UseCloud", 2);
        use_cloud = reinterpret_cast<UseCloudRegionFn>(
            il2cpp::method_pointer(use_cloud_method));
    }

    if (use_cloud == nullptr || use_cloud_method == nullptr) {
        // dump1250.cs guarantees this overload. If runtime metadata disagrees,
        // do not guess field ABI or continue through a stale route.
        LOGE("cloud-force[%s]: UseCloud(appId, eu) unavailable; connection blocked",
             point);
        g_cloud_ready.store(false, std::memory_order_release);
        return false;
    }

    // This address may already carry the diagnostic hook. Calling it is
    // intentional: that hook delegates to the original SDK method and gives
    // us before/after evidence in logcat.
    use_cloud(settings, app_id, kForcedRegionEu, use_cloud_method);

    int32_t host = -1;
    int32_t region = -1;
    ManagedString* stored_app = nullptr;
    const bool have_host = read_field(settings, "HostType", &host);
    const bool have_region = read_field(settings, "PreferredRegion", &region);
    const bool have_app = read_field(settings, "AppID", &stored_app);

    const int32_t chars = il2cpp::string_length != nullptr
                              ? il2cpp::string_length(app_id)
                              : -1;
    const bool stored_same = have_app && stored_app == app_id;
    const uint32_t apply = g_cloud_apply_count.fetch_add(
                               1u, std::memory_order_relaxed) + 1u;
    const bool ready = have_host && host == kPhotonCloudHost &&
                       have_region && region == kForcedRegionEu && stored_same;
    g_cloud_ready.store(ready, std::memory_order_release);

    LOGI("cloud-force[%s]: apply#%" PRIu32
         " sdk=1 host=%d(PhotonCloud expected=1) "
         "region=%d(eu expected=0) appChars=%d storedSame=%d ready=%d",
         point, apply, host, region, chars,
         stored_same ? 1 : 0, ready ? 1 : 0);
    if (!ready) {
        LOGE("cloud-force[%s]: FAIL-CLOSED — PhotonCloud/eu route was not applied",
             point);
    }
    return ready;
#endif
}

inline void hook_connect_scene_go(void* self, const MethodInfo* method) {
    if (!force_cloud("ConnectScene.HandleGoBtnClicked", true)) {
        LOGE("cloud-force: blocked ConnectScene.HandleGoBtnClicked");
        return;
    }
    g_connect_scene_go(self, method);
}

inline void hook_connection_control_connect(void* self, const MethodInfo* method) {
    if (!force_cloud("ConnectionControl.ConnectToPhoton", true)) {
        LOGE("cloud-force: blocked ConnectionControl.ConnectToPhoton");
        return;
    }
    g_connection_control_connect(self, method);
}

inline bool hook_game_connect_tier(int32_t tier, const MethodInfo* method) {
    if (!force_cloud("GameConnect.ConnectToPhoton(tier)", true)) return false;
    return g_game_connect_tier(tier, method);
}

inline bool hook_game_connect_squad(const MethodInfo* method) {
    if (!force_cloud("GameConnect.ConnectToPhotonSquad", true)) return false;
    return g_game_connect_squad(method);
}

inline void hook_friends_update(void* self, const MethodInfo* method) {
    const int32_t state = connection_state();

    // Do not touch PhotonNetwork settings from this callback while idle: this
    // component can start before Switcher.SetUpPhoton initializes static fields.
    // Cloud routing is re-applied by the explicit connection-entry hooks below.

    if (should_quarantine_friends_update(state)) {
        const uint32_t count = g_quarantined_updates.fetch_add(
                                   1u, std::memory_order_relaxed) + 1u;
        if (count <= 8u || count % 120u == 0u) {
            LOGW("backend-guard: skipped FriendsController.Update #%" PRIu32
                 " state=%d(%s); blocks proven Disconnect call site +0x310",
                 count, state, state_name(state));
        }
        return;
    }
    g_friends_update(self, method);
}

template <typename Fn>
void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

inline bool install_optional(const hook::ManagedMethod& target, void* replace,
                             void** original, int* installed) {
    if (hook::install(target, replace, original, false)) {
        ++(*installed);
        return true;
    }
    return false;
}

} // namespace detail

inline bool install_hooks() {
#ifdef PHOTON_MODE_SELFHOSTED
    LOGI("cloud-force: PHOTON_MODE=selfhosted; cloud routing guard disabled");
    return true;
#else
    int installed = 0;
    const bool backend_guard = hook::install(
        {"", "FriendsController", "Update", 0},
        detail::replacement(&detail::hook_friends_update),
        detail::original_slot(&detail::g_friends_update), true);
    if (backend_guard) ++installed;

    detail::install_optional(
        {"", "ConnectScene", "HandleGoBtnClicked", 0},
        detail::replacement(&detail::hook_connect_scene_go),
        detail::original_slot(&detail::g_connect_scene_go), &installed);
    detail::install_optional(
        {"", "ConnectionControl", "ConnectToPhoton", 0},
        detail::replacement(&detail::hook_connection_control_connect),
        detail::original_slot(&detail::g_connection_control_connect), &installed);
    detail::install_optional(
        {"", "GameConnect", "ConnectToPhoton", 1},
        detail::replacement(&detail::hook_game_connect_tier),
        detail::original_slot(&detail::g_game_connect_tier), &installed);
    detail::install_optional(
        {"", "GameConnect", "ConnectToPhotonSquad", 0},
        detail::replacement(&detail::hook_game_connect_squad),
        detail::original_slot(&detail::g_game_connect_squad), &installed);

    LOGI("cloud-force: installed %d hooks (backend-guard=%s)",
         installed, backend_guard ? "OK" : "FAILED");
    return backend_guard && installed >= 2;
#endif
}

} // namespace cloud_guard
