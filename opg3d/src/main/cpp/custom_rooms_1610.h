#pragma once

#include <atomic>
#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Stock 16.1.0 keeps public matchmaking and Custom Games in the same
// PixelGun3D SQL lobby. Ordinary matchmaking requires C1="" and C2 equal to
// the device platform (1/2), while the Custom Games list also accepts the
// special cross-platform value C2=3. The stock creator already uses C2=3 for
// password-protected rooms, but leaves open named rooms at the device value;
// those rooms can consequently satisfy a normal JoinRandomRoom query.
//
// Keep the existing UI and the complete stock RoomOptions/property builder.
// Immediately before GameConnect submits a *named* room, replace only C2 with
// the stock `PlatformConnect.custom` value. Auto-match rooms have an empty
// generated name at this layer and remain untouched.
//
// Security boundary: the legacy password in C1 is lobby-visible metadata and
// is checked by the client before JoinRoom. C1 plus C2=3 prevents accidental
// entry through the unmodified matchmaking UI, but cannot authenticate a
// hostile client that deliberately calls JoinRoom by name. True password
// enforcement would require a Photon server plugin or another trusted server.
namespace custom_rooms_1610 {
namespace detail {

using MethodInfo = void;
using ManagedString = void;

using CreateRoomFn = void (*)(void* context, ManagedString* room_name, bool is_visible,
                              bool is_open, int32_t max_players, void* room_properties,
                              void* lobby_properties, bool first_option, bool second_option,
                              void* expected_users, bool final_option, const MethodInfo* method);
using HashtableSetItemFn = void (*)(void* self, void* key, void* value, const MethodInfo* method);

inline constexpr const char* kGameConnect = "GameConnect";
inline constexpr const char* kCreateRoom = u8"下丂丝丈丛万丑丆丈";
inline constexpr const char* kHashtableNamespace = "ExitGames.Client.Photon";
inline constexpr const char* kHashtable = "Hashtable";
inline constexpr const char* kPlatformProperty = "C2";
inline constexpr int32_t kCustomPlatform = 3;

inline CreateRoomFn g_original = nullptr;
inline HashtableSetItemFn g_set_item = nullptr;
inline void* g_set_item_method = nullptr;
inline void* g_int32_class = nullptr;
inline std::atomic<uint32_t> g_isolated_rooms{0u};
inline std::atomic<uint32_t> g_blocked_rooms{0u};

inline bool has_explicit_name(ManagedString* room_name, int32_t* length) {
    if (length != nullptr)
        *length = 0;
    if (room_name == nullptr || il2cpp::string_length == nullptr)
        return false;
    const int32_t current_length = il2cpp::string_length(room_name);
    if (length != nullptr)
        *length = current_length;
    return current_length > 0;
}

inline bool set_custom_platform(void* room_properties) {
    if (room_properties == nullptr || g_int32_class == nullptr || g_set_item == nullptr ||
        g_set_item_method == nullptr || il2cpp::string_new == nullptr ||
        il2cpp::value_box == nullptr) {
        return false;
    }

    void* key = il2cpp::string_new(kPlatformProperty);
    int32_t platform = kCustomPlatform;
    void* boxed_platform = il2cpp::value_box(g_int32_class, &platform);
    if (key == nullptr || boxed_platform == nullptr)
        return false;

    // Hashtable uses System.String value equality, so this replaces the C2
    // entry produced by the stock builder instead of adding a second key.
    g_set_item(room_properties, key, boxed_platform, g_set_item_method);
    return true;
}

inline void hook_create_room(void* context, ManagedString* room_name, bool is_visible, bool is_open,
                             int32_t max_players, void* room_properties, void* lobby_properties,
                             bool first_option, bool second_option, void* expected_users,
                             bool final_option, const MethodInfo* method) {
    int32_t name_length = 0;
    const bool named_room = has_explicit_name(room_name, &name_length);
    // Both stock Custom controllers supply a property table and a lobby-key
    // array. A named specialized room without that contract is left alone.
    const bool custom_candidate =
        named_room && room_properties != nullptr && lobby_properties != nullptr;

    if (custom_candidate && !set_custom_platform(room_properties)) {
        const uint32_t blocked = g_blocked_rooms.fetch_add(1u, std::memory_order_relaxed) + 1u;
        LOGE("16.1.0-custom-room: BLOCKED named room #%u (chars=%d): "
             "could not stamp C2=3; refusing a room that could leak into "
             "ordinary matchmaking",
             blocked, name_length);
        return;
    }

    if (custom_candidate) {
        const uint32_t isolated = g_isolated_rooms.fetch_add(1u, std::memory_order_relaxed) + 1u;
        LOGI("16.1.0-custom-room: isolated named room #%u "
             "(chars=%d visible=%d open=%d maxPlayers=%d) with C2=3; "
             "ordinary matchmaking remains on device C2",
             isolated, name_length, is_visible ? 1 : 0, is_open ? 1 : 0, max_players);
    }

    if (g_original != nullptr) {
        g_original(context, room_name, is_visible, is_open, max_players, room_properties,
                   lobby_properties, first_option, second_option, expected_users, final_option,
                   method);
    }
}

template <typename Fn> void* replacement(Fn fn) { return reinterpret_cast<void*>(fn); }

template <typename Fn> void** original_slot(Fn* fn) { return reinterpret_cast<void**>(fn); }

} // namespace detail

inline bool install_hooks() {
#if defined(__ANDROID__)
    static_assert(sizeof(void*) == 4, "PG3D 16.1.0 target must be armeabi-v7a");
#endif

    if (il2cpp::string_length == nullptr) {
        LOGE("16.1.0-custom-room: cannot inspect room names; isolation hook "
             "was not installed");
        return false;
    }

    const bool marker_api_ready = il2cpp::string_new != nullptr && il2cpp::value_box != nullptr;
    if (marker_api_ready) {
        detail::g_int32_class = il2cpp::find_class("System", "Int32");
        detail::g_set_item_method = il2cpp::find_method_info(detail::kHashtableNamespace,
                                                             detail::kHashtable, "set_Item", 2);
        detail::g_set_item = reinterpret_cast<detail::HashtableSetItemFn>(
            il2cpp::method_pointer(detail::g_set_item_method));
    }

    const bool marker_ready = marker_api_ready && detail::g_int32_class != nullptr &&
                              detail::g_set_item != nullptr && detail::g_set_item_method != nullptr;
    if (!marker_ready) {
        LOGE("16.1.0-custom-room: C2 marker API is unavailable; installing "
             "the named-room guard in BLOCKED mode");
    }

    const bool installed = hook::install({"", detail::kGameConnect, detail::kCreateRoom, 10},
                                         detail::replacement(&detail::hook_create_room),
                                         detail::original_slot(&detail::g_original), true);
    if (!installed) {
        LOGE("16.1.0-custom-room: FAILED to install named-room isolation; "
             "Custom creation must not be treated as safe");
        return false;
    }
    if (!marker_ready) {
        // The hook remains active: automatic unnamed rooms delegate to stock,
        // while named rooms return before Photon can publish an unsafe C2.
        LOGE("16.1.0-custom-room: BLOCKED-mode guard installed; named room "
             "creation is disabled until C2=3 can be written");
        return false;
    }

    LOGI("16.1.0-custom-room: named-room C2 isolation hook installed "
         "(stock UI/properties/lobby preserved)");
    return true;
}

} // namespace custom_rooms_1610
