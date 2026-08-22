#pragma once

#include <atomic>
#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// The original Pixel Gun Photon application used a custom server plugin. A
// fresh Photon Cloud application exposes only the Default plugin, so 16.1.1's
// legacy RoomOptions.Plugins value makes CreateGame fail on the game server
// with ErrorCode.PluginMismatch (32751). Keep every stock room property, but
// temporarily clear only RoomOptions.Plugins while PUN serializes operation
// parameters. The original managed field is restored immediately afterwards.
namespace photon_default_plugin_1610 {
namespace detail {

using MethodInfo = void;
using RoomOptionsToParametersFn = void (*)(
    void* self, void* parameters, void* room_options,
    const MethodInfo* method);

inline constexpr const char* kLoadBalancingPeer =
    u8"丏丗上丂丗世与万丅";
inline constexpr const char* kRoomOptionsToParameters =
    u8"丏丙丞一专不万万世";
inline constexpr const char* kRoomOptions =
    u8"不丈丏一丏万丅丂下";
inline constexpr const char* kPluginsField =
    u8"丂下丒不丞业专不丏";

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
        il2cpp::field_get_value(
            room_options, g_plugins_field, &legacy_plugins);
        if (legacy_plugins != nullptr) {
            void* default_plugins = nullptr;
            il2cpp::field_set_value(
                room_options, g_plugins_field, &default_plugins);
            cleared = true;
            const uint32_t operation =
                g_stripped_operations.fetch_add(1u) + 1u;
            LOGW("16.1.1-photon-plugin: removed legacy "
                 "RoomOptions.Plugins for operation #%u; Photon Cloud will "
                 "use the Default plugin", operation);
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
    static_assert(sizeof(void*) == 4,
                  "PG3D 16.1.1 target must be armeabi-v7a");
#endif

    if (il2cpp::field_get_value == nullptr ||
        il2cpp::field_set_value == nullptr) {
        LOGE("16.1.1-photon-plugin: IL2CPP field API is unavailable");
        return false;
    }
    detail::g_plugins_field = il2cpp::find_field(
        "", detail::kRoomOptions, detail::kPluginsField);
    if (detail::g_plugins_field == nullptr) {
        LOGE("16.1.1-photon-plugin: cannot resolve "
             "RoomOptions.Plugins metadata");
        return false;
    }

    const bool installed = hook::install(
        {"", detail::kLoadBalancingPeer,
         detail::kRoomOptionsToParameters, 2},
        detail::replacement(
            &detail::hook_room_options_to_parameters),
        detail::original_slot(&detail::g_original), true);
    LOGI("16.1.1-photon-plugin: Default-plugin compatibility hook %s "
         "(only ParameterCode.Plugins=204 is omitted)",
         installed ? "installed" : "FAILED");
    return installed;
}

} // namespace photon_default_plugin_1610
