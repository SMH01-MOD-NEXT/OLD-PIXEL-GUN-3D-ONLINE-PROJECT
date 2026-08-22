#pragma once

#include <atomic>
#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// A normal Photon Cloud application exposes the Default plugin, so the legacy
// RoomOptions.Plugins value makes CreateGame fail with PluginMismatch (32751).
// Preserve every stock room property but temporarily clear only Plugins while
// PUN serializes operation parameters, then restore the managed field.
namespace photon_default_plugin_2313 {
namespace detail {

using MethodInfo = void;
using RoomOptionsToParametersFn = void (*)(
    void* self, void* parameters, void* room_options,
    const MethodInfo* method);

inline constexpr const char* kLoadBalancingPeer =
    u8"丅丙丆三丒丞丈且世";
inline constexpr const char* kRoomOptionsToParameters =
    u8"丕丐丈丈东丟不与三";
inline constexpr const char* kRoomOptions =
    u8"丏上丆与下业不丄丈";
inline constexpr const char* kPluginsField =
    u8"丟世丘一丗丄万不下";

inline RoomOptionsToParametersFn g_original = nullptr;
inline void* g_plugins_field = nullptr;
inline std::atomic<uint32_t> g_stripped_operations{0u};

void hook_room_options_to_parameters(
    void* self, void* parameters, void* room_options,
    const MethodInfo* method) {
    void* legacy_plugins = nullptr;
    bool cleared = false;
    if (room_options != nullptr && g_plugins_field != nullptr &&
        il2cpp::field_get_value != nullptr &&
        il2cpp::field_set_value != nullptr) {
        il2cpp::field_get_value(room_options, g_plugins_field, &legacy_plugins);
        if (legacy_plugins != nullptr) {
            void* default_plugins = nullptr;
            il2cpp::field_set_value(
                room_options, g_plugins_field, &default_plugins);
            cleared = true;
            const uint32_t operation =
                g_stripped_operations.fetch_add(1u) + 1u;
            LOGW("23.1.3-photon-plugin: removed legacy RoomOptions.Plugins "
                 "for operation #%u; Photon Cloud will use the Default plugin",
                 operation);
        }
    }

    if (g_original != nullptr) {
        g_original(self, parameters, room_options, method);
    }

    if (cleared) {
        il2cpp::field_set_value(
            room_options, g_plugins_field, &legacy_plugins);
    }
}

template <typename Fn>
void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

} // namespace detail

inline bool install_hooks() {
#if defined(__ANDROID__)
    static_assert(sizeof(void*) == 8,
                  "PG3D 23.1.3 target must be arm64-v8a");
#endif

    if (il2cpp::field_get_value == nullptr ||
        il2cpp::field_set_value == nullptr) {
        LOGE("23.1.3-photon-plugin: IL2CPP field API is unavailable");
        return false;
    }
    detail::g_plugins_field = il2cpp::find_field(
        "", detail::kRoomOptions, detail::kPluginsField);
    if (detail::g_plugins_field == nullptr) {
        LOGE("23.1.3-photon-plugin: cannot resolve mapped "
             "RoomOptions.Plugins metadata");
        return false;
    }

    const bool installed = hook::install(
        {"", detail::kLoadBalancingPeer,
         detail::kRoomOptionsToParameters, 2},
        detail::replacement(
            &detail::hook_room_options_to_parameters),
        detail::original_slot(&detail::g_original), true);
    LOGI("23.1.3-photon-plugin: Default-plugin compatibility hook %s "
         "(only ParameterCode.Plugins=204 is omitted)",
         installed ? "installed" : "FAILED");
    return installed;
}

} // namespace photon_default_plugin_2313
