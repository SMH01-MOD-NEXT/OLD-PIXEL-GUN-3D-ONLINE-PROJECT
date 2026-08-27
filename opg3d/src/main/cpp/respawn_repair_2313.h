#pragma once

#include <cinttypes>
#include <cstdint>
#include <cstring>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Repair for the anomaly "after a bot kill only the rotating kill camera is
// left: no respawn buttons, no killer weapon, no loadout".
//
// battle_flow_trace_2313.h pinned the exact divergence point in log_battle.txt:
//
//   -> local death #1 self=0x75675d2000; the respawn coroutine is started here
//   respawn coroutine -> state 0
//   -> respawn camera setup killerInfo=present
//   <- respawn camera setup returned
//   -> RespawnWindow show self=0x75e1f337e0 killerInfo=present
//   -> killer weapon panel killerInfo=present dps=42.00/10.00/61.00
//   <- local death #1 returned
//
// The camera work completes, the window show is entered, and the killer weapon
// panel never returns. Neither "<- killer weapon panel returned" nor
// "<- RespawnWindow show returned" nor any further coroutine state appears, so
// WeaponInfoInRespawnWindow.上上三丝上不丂东东 0x4340DE8 raises, the exception unwinds
// through RespawnWindow.丐丆丙专一丒丗上且 0x1394144 and the iterator MoveNext, and the
// states that would call SetRespawnButtonActive (MoveNext +0xC5C / +0xCE4)
// never run. That is exactly the reported picture.
//
// The stock fill is a chain of il2cpp null-check-throws: every cbz inside it
// jumps to 0x4341560, whose only instruction is "bl 0x1291FD8", the il2cpp
// null-reference thrower. On the branch taken for a killer with a positive
// efficiency value these must all be non-null:
//
//   0x4340F30  Component.get_gameObject() result
//   0x434110C  丂丞世万丅下万丌业() 0x4340D74, the lazily cached itemImage sprite
//   0x43410BC  eventItemLabel  +0x80
//   0x43411E0  itemNameLabel   +0x20
//   0x4341204  barPanel        +0x28   (only when efficiency > 0)
//   0x4341220  barSprite       +0x30   (only when efficiency > 0)
//   0x4341234  arrowDown       +0x38   (only when efficiency > 0)
//   0x4341258  arrowUp         +0x40   (only when efficiency > 0)
//   0x4341360  WeaponManager.丐丈丁丒丏丗丈一丐() 0x141E150, the event weapon set
//
// The managed exception cannot be caught here: this library is built with
// -fno-exceptions on purpose (CMakeLists.txt: two incompatible stack unwinders
// live in one process, and the crash happens during the handler search phase),
// so the fix must prevent the throw instead of catching it. Two layers:
//
//   1. Precondition guard. Before the stock fill runs, exactly the values it
//      dereferences are checked. If one is null, the panel is hidden the same
//      way the stock empty-weapon path hides it - SetActive(false) on its own
//      game object - and the stock call is skipped. RespawnWindow.Show then
//      returns normally and the coroutine keeps building the interface, so the
//      respawn buttons and the loadout appear.
//   2. Watchdog. Should the stock fill still raise for a reason outside that
//      list, the next RespawnWindow.Update sees that the fill never returned
//      and re-arms the respawn button once, so the player can always leave the
//      kill camera instead of being stuck watching it.
//
// Nothing else is changed: the killer payload, the respawn delay, the camera
// and every healthy panel keep running stock code, and a healthy panel still
// logs its enter/exit pair.
namespace respawn_repair_2313 {
namespace detail {

using MethodInfo = void;

using FillFn = void (*)(void* self, void* killer_info, float first,
                        float second, float third, const MethodInfo* method);
using VoidFn = void (*)(void* self, const MethodInfo* method);
using ObjectFn = void* (*)(void* self, const MethodInfo* method);
using StaticObjectFn = void* (*)(const MethodInfo* method);
using SetActiveFn = void (*)(void* game_object, bool value,
                             const MethodInfo* method);
using SetButtonFn = void (*)(void* self, bool active, bool now,
                             const MethodInfo* method);

inline constexpr const char* kNs = "";
inline constexpr const char* kPanel = "WeaponInfoInRespawnWindow";
inline constexpr const char* kWindow = "RespawnWindow";
inline constexpr const char* kWeaponManager = "WeaponManager";

// Obfuscated metadata names, copied verbatim from dump2313.cs.
inline constexpr const char* kFill = u8"上上三丝上不丂东东";
inline constexpr const char* kSpriteGetter = u8"丂丞世万丅下万丌业";
inline constexpr const char* kEventWeapons = u8"丐丈丁丒丏丗丈一丐";

// Serialized references the stock fill dereferences without a Unity-style
// null test, i.e. the ones whose absence throws.
inline constexpr const char* kItemNameLabel = "itemNameLabel";
inline constexpr const char* kEventItemLabel = "eventItemLabel";
inline constexpr const char* kBarPanel = "barPanel";
inline constexpr const char* kBarSprite = "barSprite";
inline constexpr const char* kArrowDown = "arrowDown";
inline constexpr const char* kArrowUp = "arrowUp";

inline constexpr uint32_t kSkipLogLimit = 20u;

inline FillFn g_fill = nullptr;
inline VoidFn g_update = nullptr;

inline ObjectFn g_get_game_object = nullptr;
inline const MethodInfo* g_mi_get_game_object = nullptr;
inline SetActiveFn g_set_active = nullptr;
inline const MethodInfo* g_mi_set_active = nullptr;
inline ObjectFn g_sprite_getter = nullptr;
inline const MethodInfo* g_mi_sprite_getter = nullptr;
inline StaticObjectFn g_event_weapons = nullptr;
inline const MethodInfo* g_mi_event_weapons = nullptr;
inline SetButtonFn g_set_button = nullptr;
inline const MethodInfo* g_mi_set_button = nullptr;

// Both hooks run on the Unity main thread only, so plain globals are enough.
inline bool g_fill_pending = false;
inline uint32_t g_skips = 0u;
inline uint32_t g_recoveries = 0u;
inline uint32_t g_fills = 0u;

inline bool resolve_call(const hook::ManagedMethod& target, void** out_fn,
                         const MethodInfo** out_mi) {
    if (out_fn == nullptr || out_mi == nullptr) return false;
    void* info = il2cpp::find_method_info(target.namespaze, target.klass,
                                         target.method, target.args_count);
    void* pointer = info != nullptr ? il2cpp::method_pointer(info) : nullptr;
    if (info == nullptr || pointer == nullptr) {
        LOGE("23.1.3-respawn-repair: cannot resolve %s.%s/%d", target.klass,
             target.method, target.args_count);
        return false;
    }
    *out_fn = pointer;
    *out_mi = info;
    return true;
}

inline bool reference_is_null(void* object, const char* field_name) {
    if (object == nullptr || il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_field_from_name == nullptr ||
        il2cpp::field_get_value == nullptr) {
        return false;
    }
    void* klass = il2cpp::object_get_class(object);
    void* field = klass != nullptr
                      ? il2cpp::class_get_field_from_name(klass, field_name)
                      : nullptr;
    // An unknown field name means our metadata assumption is wrong, not that
    // the reference is missing. Fail open and let stock code run.
    if (field == nullptr) return false;
    alignas(8) uint8_t scratch[16] = {0};
    il2cpp::field_get_value(object, field, scratch);
    void* value = nullptr;
    std::memcpy(&value, scratch, sizeof(value));
    return value == nullptr;
}

// Returns the name of the first missing dependency, or nullptr when the stock
// fill is safe to run.
inline const char* first_missing(void* self, void* killer_info,
                                float efficiency) {
    if (self == nullptr) return "panel instance";
    if (killer_info == nullptr) return "killer info";
    if (g_get_game_object != nullptr &&
        g_get_game_object(self, g_mi_get_game_object) == nullptr) {
        return "panel game object";
    }
    // Stock calls this getter first; it caches itemImage and the caller throws
    // when it still hands back null.
    if (g_sprite_getter != nullptr &&
        g_sprite_getter(self, g_mi_sprite_getter) == nullptr) {
        return "itemImage sprite";
    }
    if (reference_is_null(self, kEventItemLabel)) return kEventItemLabel;
    if (reference_is_null(self, kItemNameLabel)) return kItemNameLabel;
    if (efficiency > 0.0f) {
        if (reference_is_null(self, kBarPanel)) return kBarPanel;
        if (reference_is_null(self, kBarSprite)) return kBarSprite;
        if (reference_is_null(self, kArrowDown)) return kArrowDown;
        if (reference_is_null(self, kArrowUp)) return kArrowUp;
    }
    if (g_event_weapons != nullptr &&
        g_event_weapons(g_mi_event_weapons) == nullptr) {
        return "WeaponManager event weapon set";
    }
    return nullptr;
}

// Same shape as the stock "this weapon has no name" path: hide the panel and
// leave the rest of the window alone.
inline void hide_panel(void* self) {
    if (g_get_game_object == nullptr || g_set_active == nullptr) {
        LOGE("23.1.3-respawn-repair: cannot hide the panel; Unity GameObject"
             " API is missing");
        return;
    }
    void* game_object = g_get_game_object(self, g_mi_get_game_object);
    if (game_object == nullptr) return;
    g_set_active(game_object, false, g_mi_set_active);
}

inline void fill_hook(void* self, void* killer_info, float first, float second,
                      float third, const MethodInfo* method) {
    const char* missing = first_missing(self, killer_info, first);
    if (missing != nullptr) {
        const uint32_t index = ++g_skips;
        if (index <= kSkipLogLimit || (index % kSkipLogLimit) == 0u) {
            LOGW("23.1.3-respawn-repair: killer weapon panel skipped #%" PRIu32
                 " (%s is null); the respawn interface keeps building",
                 index, missing);
        }
        hide_panel(self);
        return;
    }
    if (g_fill == nullptr) {
        LOGE("23.1.3-respawn-repair: killer weapon panel has no saved"
             " original");
        return;
    }
    const uint32_t index = ++g_fills;
    LOGW("23.1.3-respawn-repair: -> killer weapon panel #%" PRIu32
         " dps=%.2f/%.2f/%.2f",
         index, static_cast<double>(first), static_cast<double>(second),
         static_cast<double>(third));
    g_fill_pending = true;
    g_fill(self, killer_info, first, second, third, method);
    g_fill_pending = false;
    LOGW("23.1.3-respawn-repair: <- killer weapon panel #%" PRIu32 " returned",
         index);
}

// The flag can only still be set when the stock fill above did not return,
// which means it raised and took the respawn coroutine with it.
inline void update_hook(void* self, const MethodInfo* method) {
    if (g_fill_pending) {
        g_fill_pending = false;
        const uint32_t index = ++g_recoveries;
        LOGE("23.1.3-respawn-repair: stock killer weapon panel raised and"
             " killed the respawn coroutine; re-arming the respawn button"
             " (recovery #%" PRIu32 ")", index);
        if (g_set_button != nullptr) {
            g_set_button(self, true, false, g_mi_set_button);
        } else {
            LOGE("23.1.3-respawn-repair: SetRespawnButtonActive is not"
                 " resolved; cannot re-arm the button");
        }
    }
    if (g_update == nullptr) return;
    g_update(self, method);
}

} // namespace detail

inline bool install_hooks() {
    using namespace detail;

    bool unity_api = resolve_call(
        {"UnityEngine", "Component", "get_gameObject", 0},
        reinterpret_cast<void**>(&g_get_game_object), &g_mi_get_game_object);
    unity_api &= resolve_call({"UnityEngine", "GameObject", "SetActive", 1},
                              reinterpret_cast<void**>(&g_set_active),
                              &g_mi_set_active);

    const bool sprite = resolve_call(
        {kNs, kPanel, kSpriteGetter, 0},
        reinterpret_cast<void**>(&g_sprite_getter), &g_mi_sprite_getter);
    const bool event_set = resolve_call(
        {kNs, kWeaponManager, kEventWeapons, 0},
        reinterpret_cast<void**>(&g_event_weapons), &g_mi_event_weapons);
    const bool button = resolve_call(
        {kNs, kWindow, "SetRespawnButtonActive", 2},
        reinterpret_cast<void**>(&g_set_button), &g_mi_set_button);

    const bool guard = hook::install({kNs, kPanel, kFill, 4},
                                     reinterpret_cast<void*>(&fill_hook),
                                     reinterpret_cast<void**>(&g_fill), false);
    if (!guard) {
        LOGE("23.1.3-respawn-repair: could not hook %s.%s/4", kPanel, kFill);
    }
    const bool watchdog = hook::install({kNs, kWindow, "Update", 0},
                                        reinterpret_cast<void*>(&update_hook),
                                        reinterpret_cast<void**>(&g_update),
                                        false);
    if (!watchdog) {
        LOGW("23.1.3-respawn-repair: could not hook %s.Update/0; the button"
             " watchdog is inactive", kWindow);
    }

    LOGI("23.1.3-respawn-repair: guard=%s watchdog=%s (gameobject-api=%s"
         " sprite-getter=%s event-set=%s respawn-button=%s)",
         guard ? "OK" : "FAILED", watchdog ? "OK" : "FAILED",
         unity_api ? "OK" : "MISSING", sprite ? "OK" : "MISSING",
         event_set ? "OK" : "MISSING", button ? "OK" : "MISSING");

    // The guard plus the Unity object API is the part that actually keeps the
    // window alive; the rest only sharpens the diagnosis.
    return guard && unity_api;
}

} // namespace respawn_repair_2313
