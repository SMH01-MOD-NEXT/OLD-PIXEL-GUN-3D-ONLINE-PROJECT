#pragma once

#include <atomic>
#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Forced online state for the frozen 23.1.3 client.
//
// The retired backend makes several independent subsystems conclude that the
// device has no internet access. Every conclusion was mapped from the 23.1.3
// metadata dump instead of being guessed:
//
//   * UnityEngine.Application.get_internetReachability has 19 call sites in
//     this build. All of them belong to the asset-bundle stack
//     (PGCompany.AssetBundles_v3, 8 sites), the private-games panels
//     (PrivateGamesPanel.Update and PrivateGamesPanelMiniGame.Update, 10
//     sites) and the Flurry static constructor (1 site). Reporting
//     ReachableViaLocalAreaNetwork keeps all of them on their online path.
//   * The asset-bundle downloaders expose HasLossOfWiFiConnection three times
//     (IosBundleDownloader plus two obfuscated siblings that share the same
//     abstract base) and one static wrapper. They gate bundle traffic, so they
//     must report that WiFi is intact.
//   * InternetChecker performs a synchronous HTTP probe (WebRequest.Create ->
//     StreamReader -> marker comparison) and stores the verdict in its own
//     static bool. The probe has no direct call sites, so it runs from a
//     background thread; short-circuiting it avoids a blocking request against
//     a dead host and publishes the connected verdict directly. The static
//     field is written only from inside the probe hook, where the declaring
//     class is guaranteed to be initialized.
//   * Rilisoft.DisableIfOfflineMode.OnEnable deactivates its own GameObject
//     whenever the client believes it runs offline. That component is what
//     removes backend-dependent entries such as the in-battle arsenal, so the
//     deactivation is skipped entirely.
//   * OfflineModController drives a curated group of buttons through an
//     instance gate taking the offline flag. The flag is forced to false so
//     the group stays interactive (the 16.1.0 port hooks the same gate under
//     its old obfuscated name, which no longer exists in 23.1.3).
//   * The two craft connection-error banners show themselves from OnEnable.
//     They are hidden the same way the startup guards hide retired modals.
//
// InfoWindowController's no-internet panel is already forced hidden by
// startup_guards_2313.h; it must not be hooked twice. InGameConnection is left
// stock on purpose: its reconnect bookkeeping has unknown polarity and Photon
// already owns the real connection state.
namespace online_state_2313 {
namespace detail {

using MethodInfo = void;
using StaticInt32Fn = int32_t (*)(const MethodInfo* method);
using StaticVoidFn = void (*)(const MethodInfo* method);
using StaticBoolFn = bool (*)(const MethodInfo* method);
using InstanceBoolFn = bool (*)(void* self, const MethodInfo* method);
using InstanceVoidFn = void (*)(void* self, const MethodInfo* method);
using InstanceVoidBoolFn = void (*)(void* self, bool value,
                                    const MethodInfo* method);
using GetGameObjectFn = void* (*)(void* component, const MethodInfo* method);
using SetActiveFn = void (*)(void* game_object, bool value,
                             const MethodInfo* method);

inline constexpr const char* kAssetBundleNs = "PGCompany.AssetBundles_v3";
inline constexpr const char* kRilisoftNs = "Rilisoft";
inline constexpr const char* kWiFiCheck = "HasLossOfWiFiConnection";
inline constexpr const char* kWiFiCheckWrapper =
    "HasLossOfWiFiConnectionWrapper";

// PGCompany.AssetBundles_v3 downloader siblings of IosBundleDownloader.
inline constexpr const char* kDownloaderA =
    u8"\u4E11\u4E1D\u4E15\u4E0A\u4E10\u4E0F\u4E07\u4E11\u4E12";
inline constexpr const char* kDownloaderB =
    u8"\u4E1B\u4E15\u4E06\u4E1D\u4E04\u4E18\u4E15\u4E0C\u4E15";
// InternetChecker: static probe method and the static verdict it publishes.
inline constexpr const char* kInternetProbe =
    u8"\u4E15\u4E0C\u4E1A\u4E10\u4E15\u4E1E\u4E03\u4E12\u4E11";
inline constexpr const char* kInternetFlag =
    u8"\u4E1C\u4E10\u4E10\u4E10\u4E04\u4E11\u4E01\u4E03\u4E13";
// OfflineModController instance gate taking the offline flag.
inline constexpr const char* kOfflineGroupGate =
    u8"\u4E11\u4E07\u4E0D\u4E18\u4E0E\u4E06\u4E03\u4E17\u4E19";

// NetworkReachability.ReachableViaLocalAreaNetwork in this build.
inline constexpr int32_t kReachableViaLan = 2;

inline StaticInt32Fn g_reachability = nullptr;
inline StaticVoidFn g_internet_probe = nullptr;
inline InstanceBoolFn g_wifi_ios = nullptr;
inline InstanceBoolFn g_wifi_a = nullptr;
inline InstanceBoolFn g_wifi_b = nullptr;
inline StaticBoolFn g_wifi_wrapper = nullptr;
inline InstanceVoidFn g_offline_disable = nullptr;
inline InstanceVoidBoolFn g_offline_group_gate = nullptr;
inline InstanceVoidFn g_fort_banner = nullptr;
inline InstanceVoidFn g_lobby_banner = nullptr;

inline GetGameObjectFn g_get_game_object = nullptr;
inline const MethodInfo* g_mi_get_game_object = nullptr;
inline SetActiveFn g_set_active = nullptr;
inline const MethodInfo* g_mi_set_active = nullptr;

inline std::atomic<uint32_t> g_reachability_forced{0u};
inline std::atomic<uint32_t> g_probes_short_circuited{0u};
inline std::atomic<uint32_t> g_wifi_checks_cleared{0u};
inline std::atomic<uint32_t> g_offline_hides_blocked{0u};
inline std::atomic<uint32_t> g_offline_gates_forced{0u};
inline std::atomic<uint32_t> g_banners_hidden{0u};

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
        LOGE("23.1.3-online: cannot resolve %s.%s/%d", target.klass,
             target.method, target.args_count);
        return false;
    }
    *out_fn = pointer;
    *out_mi = info;
    return true;
}

void hide_component(void* component, const char* reason) {
    if (component == nullptr || g_get_game_object == nullptr ||
        g_set_active == nullptr || g_mi_get_game_object == nullptr ||
        g_mi_set_active == nullptr) {
        LOGE("23.1.3-online: cannot hide %s; Unity GameObject API missing",
             reason);
        return;
    }
    void* game_object = g_get_game_object(component, g_mi_get_game_object);
    if (game_object == nullptr) {
        LOGE("23.1.3-online: %s component has no GameObject", reason);
        return;
    }
    g_set_active(game_object, false, g_mi_set_active);
    const uint32_t count = g_banners_hidden.fetch_add(1u) + 1u;
    LOGW("23.1.3-online: hid retired %s #%u", reason, count);
}

// Publishes the connected verdict into InternetChecker's static bool. The
// disassembly of the stock probe stores 1 when the fetched body carries the
// expected marker and 0 otherwise, so 1 is the connected value. This runs only
// from inside the probe hook, where the declaring class is already initialized
// and its static storage exists.
void publish_internet_verdict() {
    if (il2cpp::field_static_set_value == nullptr) {
        return;
    }
    void* field = il2cpp::find_field("", "InternetChecker", kInternetFlag);
    if (field == nullptr) {
        LOGE("23.1.3-online: InternetChecker verdict field is missing");
        return;
    }
    uint8_t connected = 1u;
    il2cpp::field_static_set_value(field, &connected);
}

int32_t hook_reachability(const MethodInfo*) {
    const uint32_t count = g_reachability_forced.fetch_add(1u) + 1u;
    if (count <= 4u || count % 600u == 0u) {
        LOGI("23.1.3-online: reported reachability=%d (local area network) "
             "#%u; retired backend must not look like a dead radio",
             kReachableViaLan, count);
    }
    return kReachableViaLan;
}

void hook_internet_probe(const MethodInfo*) {
    publish_internet_verdict();
    const uint32_t count = g_probes_short_circuited.fetch_add(1u) + 1u;
    LOGW("23.1.3-online: short-circuited InternetChecker HTTP probe #%u; "
         "published the connected verdict without a request", count);
}

bool hook_wifi_intact(void*, const MethodInfo*) {
    const uint32_t count = g_wifi_checks_cleared.fetch_add(1u) + 1u;
    if (count <= 4u || count % 200u == 0u) {
        LOGI("23.1.3-online: reported intact WiFi to the bundle downloader "
             "#%u", count);
    }
    return false;
}

bool hook_wifi_intact_static(const MethodInfo*) {
    return hook_wifi_intact(nullptr, nullptr);
}

void hook_offline_disable(void*, const MethodInfo*) {
    // Stock body deactivates its own GameObject in offline mode. Skipping the
    // body keeps backend-dependent UI (in-battle arsenal entries included)
    // present and interactive.
    const uint32_t count = g_offline_hides_blocked.fetch_add(1u) + 1u;
    if (count <= 8u || count % 50u == 0u) {
        LOGW("23.1.3-online: kept an offline-disabled object alive #%u",
             count);
    }
}

void hook_offline_group_gate(void* self, bool offline,
                            const MethodInfo* method) {
    if (offline) {
        const uint32_t count = g_offline_gates_forced.fetch_add(1u) + 1u;
        if (count <= 8u || count % 50u == 0u) {
            LOGW("23.1.3-online: forced the offline button group online #%u",
                 count);
        }
    }
    if (g_offline_group_gate != nullptr) {
        g_offline_group_gate(self, false, method);
    }
}

void hook_fort_banner(void* self, const MethodInfo*) {
    hide_component(self, "FortCraftConnectionErrorBanner");
}

void hook_lobby_banner(void* self, const MethodInfo*) {
    hide_component(self, "LobbyCraftConnectionErrorBanner");
}

bool add(const hook::ManagedMethod& target, void* replace, void** original,
         bool required, int* installed) {
    const bool ok = hook::install(target, replace, original, required);
    if (ok) ++(*installed);
    return ok;
}

} // namespace detail

inline bool install_hooks() {
#if defined(__ANDROID__)
    static_assert(sizeof(void*) == 8,
                  "PG3D 23.1.3 target must be arm64-v8a");
#endif

    bool unity_api = detail::resolve_call(
        {"UnityEngine", "Component", "get_gameObject", 0},
        reinterpret_cast<void**>(&detail::g_get_game_object),
        &detail::g_mi_get_game_object);
    unity_api &= detail::resolve_call(
        {"UnityEngine", "GameObject", "SetActive", 1},
        reinterpret_cast<void**>(&detail::g_set_active),
        &detail::g_mi_set_active);

    int installed = 0;

    // Core: every reachability consumer in this build.
    const bool reachability = detail::add(
        {"UnityEngine", "Application", "get_internetReachability", 0},
        detail::replacement(&detail::hook_reachability),
        detail::original_slot(&detail::g_reachability), true, &installed);

    // Core: the component that removes backend-dependent UI when offline.
    const bool offline_disable = detail::add(
        {detail::kRilisoftNs, "DisableIfOfflineMode", "OnEnable", 0},
        detail::replacement(&detail::hook_offline_disable),
        detail::original_slot(&detail::g_offline_disable), true, &installed);

    const bool offline_gate = detail::add(
        {"", "OfflineModController", detail::kOfflineGroupGate, 1},
        detail::replacement(&detail::hook_offline_group_gate),
        detail::original_slot(&detail::g_offline_group_gate), true,
        &installed);

    const bool probe = detail::add(
        {"", "InternetChecker", detail::kInternetProbe, 0},
        detail::replacement(&detail::hook_internet_probe),
        detail::original_slot(&detail::g_internet_probe), false, &installed);

    // Bundle downloaders: three concrete overrides plus the static wrapper.
    const bool wifi_ios = detail::add(
        {detail::kAssetBundleNs, "IosBundleDownloader", detail::kWiFiCheck, 0},
        detail::replacement(&detail::hook_wifi_intact),
        detail::original_slot(&detail::g_wifi_ios), false, &installed);
    const bool wifi_a = detail::add(
        {detail::kAssetBundleNs, detail::kDownloaderA, detail::kWiFiCheck, 0},
        detail::replacement(&detail::hook_wifi_intact),
        detail::original_slot(&detail::g_wifi_a), false, &installed);
    const bool wifi_b = detail::add(
        {detail::kAssetBundleNs, detail::kDownloaderB, detail::kWiFiCheck, 0},
        detail::replacement(&detail::hook_wifi_intact),
        detail::original_slot(&detail::g_wifi_b), false, &installed);
    const bool wifi_wrapper = detail::add(
        {detail::kAssetBundleNs, "IosBundleDownloader",
         detail::kWiFiCheckWrapper, 0},
        detail::replacement(&detail::hook_wifi_intact_static),
        detail::original_slot(&detail::g_wifi_wrapper), false, &installed);

    // Craft banners can only be hidden when the Unity GameObject API resolved.
    bool fort_banner = false;
    bool lobby_banner = false;
    if (unity_api) {
        fort_banner = detail::add(
            {detail::kRilisoftNs, "FortCraftConnectionErrorBanner", "OnEnable",
             0},
            detail::replacement(&detail::hook_fort_banner),
            detail::original_slot(&detail::g_fort_banner), false, &installed);
        lobby_banner = detail::add(
            {detail::kRilisoftNs, "LobbyCraftConnectionErrorBanner",
             "OnEnable", 0},
            detail::replacement(&detail::hook_lobby_banner),
            detail::original_slot(&detail::g_lobby_banner), false, &installed);
    } else {
        LOGE("23.1.3-online: Unity GameObject API incomplete; craft banners "
             "stay stock");
    }

    const bool ready = reachability && offline_disable;
    LOGI("23.1.3-online: installed %d/10 forced-online hooks "
         "(reachability=%d offline-disable=%d offline-gate=%d probe=%d "
         "wifi=%d/%d/%d/%d banners=%d/%d)",
         installed, reachability ? 1 : 0, offline_disable ? 1 : 0,
         offline_gate ? 1 : 0, probe ? 1 : 0, wifi_ios ? 1 : 0,
         wifi_a ? 1 : 0, wifi_b ? 1 : 0, wifi_wrapper ? 1 : 0,
         fort_banner ? 1 : 0, lobby_banner ? 1 : 0);
    if (!ready) {
        LOGE("23.1.3-online: forced-online state incomplete; the client can "
             "still decide it is offline");
    }
    return ready;
}

} // namespace online_state_2313
