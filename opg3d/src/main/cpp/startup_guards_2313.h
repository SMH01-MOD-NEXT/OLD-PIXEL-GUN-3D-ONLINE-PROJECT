#pragma once

#include <atomic>
#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Startup-only compatibility for services that no longer describe the frozen
// 23.1.3 client correctly. The live update service advertises 25.x and the
// retired game backend makes ConnectionLostChecker display a false offline
// modal before Photon is even allowed to connect.
//
// Keep the stock AppsMenu/scene-loading coroutine. Suppress only:
//   * UpdatesChecker.Start network/version request;
//   * the three update-banner presentation paths;
//   * VersionBlocker/NewVersionBanner blocking predicates;
//   * ConnectionLostChecker's retired-backend polling;
//   * InfoWindowController's no-internet panel when asked to show it.
// Photon owns online status after this point and reports real failures through
// photon_trace_2313.h.
namespace startup_guards_2313 {
namespace detail {

using MethodInfo = void;
using InstanceVoidFn = void (*)(void* self, const MethodInfo* method);
using InstanceBoolVoidFn = void (*)(void* self, bool value,
                                    const MethodInfo* method);
using StaticBoolFn = bool (*)(const MethodInfo* method);
using GetGameObjectFn = void* (*)(void* component,
                                  const MethodInfo* method);
using SetActiveFn = void (*)(void* game_object, bool value,
                             const MethodInfo* method);

inline constexpr const char* kVersionBlocked =
    u8"丐不丐七丕万丝丆丟";
inline constexpr const char* kNewVersionBlocking =
    u8"丝七下专下丘丝且丕";
inline constexpr const char* kNoInternetVisible =
    u8"丂且丕上丁三万一下";

inline InstanceVoidFn g_updates_start = nullptr;
inline InstanceVoidFn g_new_version_enable = nullptr;
inline InstanceVoidFn g_client_update_show = nullptr;
inline InstanceVoidFn g_update_banner_show = nullptr;
inline InstanceVoidFn g_connection_update = nullptr;
inline InstanceBoolVoidFn g_no_internet_visible = nullptr;
inline StaticBoolFn g_version_blocked = nullptr;
inline StaticBoolFn g_new_version_blocking = nullptr;

inline GetGameObjectFn g_get_game_object = nullptr;
inline const MethodInfo* g_mi_get_game_object = nullptr;
inline SetActiveFn g_set_active = nullptr;
inline const MethodInfo* g_mi_set_active = nullptr;

inline std::atomic<uint32_t> g_update_checks_suppressed{0u};
inline std::atomic<uint32_t> g_update_banners_hidden{0u};
inline std::atomic<uint32_t> g_connection_ticks_suppressed{0u};
inline std::atomic<uint32_t> g_no_internet_suppressed{0u};

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
        LOGE("23.1.3-startup: cannot resolve %s.%s/%d", target.klass,
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
        LOGE("23.1.3-startup: cannot hide %s; Unity GameObject API missing",
             reason);
        return;
    }
    void* game_object = g_get_game_object(component, g_mi_get_game_object);
    if (game_object == nullptr) {
        LOGE("23.1.3-startup: %s component has no GameObject", reason);
        return;
    }
    g_set_active(game_object, false, g_mi_set_active);
    const uint32_t count = g_update_banners_hidden.fetch_add(1u) + 1u;
    LOGW("23.1.3-startup: hid retired %s presentation #%u", reason, count);
}

void hook_updates_start(void*, const MethodInfo*) {
    const uint32_t count = g_update_checks_suppressed.fetch_add(1u) + 1u;
    LOGW("23.1.3-startup: skipped live UpdatesChecker.Start #%u; frozen "
         "23.1.3 must not compare itself with current store version", count);
}

void hook_new_version_enable(void* self, const MethodInfo*) {
    // Do not enter the stock presenter: UpdatesChecker.Start is intentionally
    // disabled, so cached version text/change lists are not guaranteed to be
    // populated. Deactivating the root invokes stock OnDisable, whose 23.1.3
    // A64 body null-checks its IDisposable field before cleanup.
    hide_component(self, "NewVersionBanner");
}

void hook_client_update_show(void* self, const MethodInfo*) {
    hide_component(self, "ClientUpdateBannerWindow");
}

void hook_update_banner_show(void* self, const MethodInfo*) {
    hide_component(self, "UpdateBannerController");
}

bool hook_not_blocked(const MethodInfo*) {
    return false;
}

void hook_connection_update(void*, const MethodInfo*) {
    const uint32_t count = g_connection_ticks_suppressed.fetch_add(1u) + 1u;
    if (count <= 4u || count % 600u == 0u) {
        LOGW("23.1.3-startup: skipped retired ConnectionLostChecker.Update "
             "#%u; Photon callbacks own connection state", count);
    }
}

void hook_no_internet_visible(void* self, bool visible,
                              const MethodInfo* method) {
    if (visible) {
        const uint32_t count = g_no_internet_suppressed.fetch_add(1u) + 1u;
        LOGW("23.1.3-startup: suppressed false no-internet modal #%u", count);
    }
    if (g_no_internet_visible != nullptr) {
        // Always drive the stock panel/touch-blocker to the hidden state.
        g_no_internet_visible(self, false, method);
    }
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
    if (!unity_api) {
        LOGE("23.1.3-startup: Unity API incomplete; refusing modal guards");
        return false;
    }

    int installed = 0;
    const bool updates = detail::add(
        {"", "UpdatesChecker", "Start", 0},
        detail::replacement(&detail::hook_updates_start),
        detail::original_slot(&detail::g_updates_start), true, &installed);
    const bool new_version = detail::add(
        {"", "NewVersionBanner", "OnEnable", 0},
        detail::replacement(&detail::hook_new_version_enable),
        detail::original_slot(&detail::g_new_version_enable), true, &installed);
    const bool version_gate = detail::add(
        {"", "VersionBlocker", detail::kVersionBlocked, 0},
        detail::replacement(&detail::hook_not_blocked),
        detail::original_slot(&detail::g_version_blocked), true, &installed);
    const bool banner_gate = detail::add(
        {"", "NewVersionBanner", detail::kNewVersionBlocking, 0},
        detail::replacement(&detail::hook_not_blocked),
        detail::original_slot(&detail::g_new_version_blocking), true,
        &installed);
    const bool connection = detail::add(
        {"", "ConnectionLostChecker", "Update", 0},
        detail::replacement(&detail::hook_connection_update),
        detail::original_slot(&detail::g_connection_update), true, &installed);
    const bool no_internet = detail::add(
        {"", "InfoWindowController", detail::kNoInternetVisible, 1},
        detail::replacement(&detail::hook_no_internet_visible),
        detail::original_slot(&detail::g_no_internet_visible), true,
        &installed);

    (void)detail::add(
        {"", "ClientUpdateBannerWindow", "Show", 0},
        detail::replacement(&detail::hook_client_update_show),
        detail::original_slot(&detail::g_client_update_show), false,
        &installed);
    (void)detail::add(
        {"PGCompany.UpdateBanner", "UpdateBannerController", "Show", 0},
        detail::replacement(&detail::hook_update_banner_show),
        detail::original_slot(&detail::g_update_banner_show), false,
        &installed);

    const bool ready = updates && new_version && version_gate && banner_gate &&
                       connection && no_internet;
    LOGI("23.1.3-startup: installed %d/8 guards "
         "(updates=%d new-version=%d version-gate=%d banner-gate=%d "
         "connection=%d no-internet=%d)",
         installed, updates ? 1 : 0, new_version ? 1 : 0,
         version_gate ? 1 : 0, banner_gate ? 1 : 0,
         connection ? 1 : 0, no_internet ? 1 : 0);
    return ready;
}

} // namespace startup_guards_2313
