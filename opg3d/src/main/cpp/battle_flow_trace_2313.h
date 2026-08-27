#pragma once

#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <string>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Passive comparison trace for two 23.1.3 battle-flow anomalies that appear
// together with the offline/account anomaly cluster:
//
//   1. After a bot kills the local player the respawn interface is missing.
//      The camera still turns to the killer, but no buttons, no killer weapon
//      and no equipment panel appear.
//   2. The profile Stats tab keeps showing the "receiving data" caption
//      forever, in affected and healthy launches alike.
//
// The A64 call graph of this exact image (tools/find_callers.py) pins both
// flows to a single driver each, which is what this file instruments.
//
// Respawn flow, verified single-driver chain:
//   Player_move_c.世丐丟丂业东不上丑            0x4820C6C  local death; the ONLY caller of
//   RespawnWindowController coroutine factory 0x3431B60 (its 1 call site)
//   RespawnWindowController/丂丂东丈丟且丈世丑 MoveNext     0x3432154  drives everything:
//       +0x1B8  show gate 七丞三丒丏丘不丐三(bool)     0x3431A40
//       +0xA38  RespawnWindow.丐丆丙专一丒丗上且(killer) 0x1394144  (its only call site)
//       +0xC5C/+0xCE4  SetRespawnButtonActive  0x1393F74
//       25 call sites of get_window            0x34316D8
//   RespawnWindowController.get_window lazily instantiates the window from
//   PrefabHandler; every failed step in it falls into the il2cpp null-check
//   thrower, and the show gate does the same when the handle is null. An
//   exception raised inside the coroutine leaves the camera work done and the
//   interface unbuilt, which is exactly the reported picture. The trace logs
//   MoveNext entry and exit per state: an entry without its exit line proves
//   the state that threw.
//
// Stats flow, verified chain:
//   PlayerProfileGUI.丌丛一丏丛一上丌丕       0x412B754  request entry (6 call sites)
//     -> PlayerProfileViewController.丐丆丙专一丒丗上且     0x412F2A4  (its only caller)
//   PlayerProfileViewController.丘丙丝丟丑丏丂上丄        0x413204C  is the only node
//   that hides pendingDataGameObject (+0xD8, SetActive(false) at +0x90) and
//   then tail-calls 丆不丙丑丑与一丛业(true) 0x413044C, which fills
//   PlayerProfileStatsView.丝万不丘下丄丄三丟 0x412DE30. 丘丙丝丟丑丏丂上丄 has ZERO call sites,
//   so it is invoked by name from the UI, and it returns early when
//   一且丅丁丂专专丒丁() 0x4131444 answers false. Therefore "receiving data" staying on
//   screen means one of: the request never runs, the open call carries no
//   PlayerData, the reveal node never fires, or its gate answers false. Each
//   of those four is a distinct line below.
//
// The killer weapon panel node (WeaponInfoInRespawnWindow.上上三丝上不丂东东) is no
// longer hooked here: respawn_repair_2313.h owns that method now, because a
// single method can only carry one hook, and it logs the same enter/exit pair
// under the 23.1.3-respawn-repair prefix.
//
// Everything here delegates to stock code and returns stock values. No
// argument, return value, window, panel or player state is modified.
namespace battle_flow_trace_2313 {
namespace detail {

using MethodInfo = void;
using ManagedString = void;

using VoidFn = void (*)(void*, const MethodInfo*);
using MoveNextFn = bool (*)(void*, const MethodInfo*);
using BoolGetterFn = bool (*)(void*, const MethodInfo*);
using WindowGetterFn = void* (*)(void*, const MethodInfo*);
using OneBoolFn = void (*)(void*, bool, const MethodInfo*);
using TwoBoolFn = void (*)(void*, bool, bool, const MethodInfo*);
using OneObjectFn = void (*)(void*, void*, const MethodInfo*);
using ProfileRequestFn = void (*)(void*, ManagedString*, int32_t, void*,
                                  int32_t, const MethodInfo*);
using ProfileOpenFn = void (*)(void*, ManagedString*, void*, int32_t, int32_t,
                               void*, const MethodInfo*);

inline constexpr const char* kNs = "";
inline constexpr const char* kPlayer = "Player_move_c";
inline constexpr const char* kController = "RespawnWindowController";
inline constexpr const char* kWindow = "RespawnWindow";
inline constexpr const char* kProfileGui = "PlayerProfileGUI";
inline constexpr const char* kProfileView = "PlayerProfileViewController";
inline constexpr const char* kStatsView = "PlayerProfileStatsView";

// Nested iterator type. il2cpp_class_from_name only sees top-level types, so
// the Outer/Nested spelling that il2cpp::find_class resolves is used.
inline constexpr const char* kIterator = u8"RespawnWindowController/丂丂东丈丟且丈世丑";

// Obfuscated metadata names, copied verbatim from dump2313.cs.
inline constexpr const char* kDeathEntry = u8"世丐丟丂业东不上丑";
inline constexpr const char* kWindowUser = u8"丞不一且丟丌世一丅";
inline constexpr const char* kShowGate = u8"七丞三丒丏丘不丐三";
inline constexpr const char* kCameraSetup = u8"丂丁世下丆万不丄与";
inline constexpr const char* kWindowShow = u8"丐丆丙专一丒丗上且";
inline constexpr const char* kProfileRequest = u8"丌丛一丏丛一上丌丕";
inline constexpr const char* kProfileOpen = u8"丐丆丙专一丒丗上且";
inline constexpr const char* kProfileGate = u8"一且丅丁丂专专丒丁";
inline constexpr const char* kProfileReveal = u8"丘丙丝丟丑丏丂上丄";
inline constexpr const char* kProfileFill = u8"丆不丙丑丑与一丛业";
inline constexpr const char* kStatsFill = u8"丝万不丘下丄丄三丟";

inline constexpr const char* kStateField = "<>1__state";
inline constexpr int32_t kNoState = INT32_MIN;
inline constexpr uint32_t kRepeatLogLimit = 5u;
inline constexpr size_t kMaxStringChars = 64u;

inline VoidFn g_death_entry = nullptr;
inline VoidFn g_window_user = nullptr;
inline WindowGetterFn g_get_window = nullptr;
inline OneBoolFn g_show_gate = nullptr;
inline OneObjectFn g_camera_setup = nullptr;
inline MoveNextFn g_move_next = nullptr;
inline OneObjectFn g_window_show = nullptr;
inline TwoBoolFn g_button_active = nullptr;
inline VoidFn g_window_close = nullptr;
inline VoidFn g_window_hide = nullptr;
inline VoidFn g_window_enable = nullptr;
inline ProfileRequestFn g_profile_request = nullptr;
inline ProfileOpenFn g_profile_open = nullptr;
inline BoolGetterFn g_profile_gate = nullptr;
inline VoidFn g_profile_reveal = nullptr;
inline OneBoolFn g_profile_fill = nullptr;
inline OneObjectFn g_stats_fill = nullptr;

inline uint32_t g_deaths = 0u;
inline uint32_t g_null_window_logs = 0u;
inline int32_t g_window_null = -1;
inline int32_t g_iterator_state = kNoState;
inline int32_t g_gate_result = -1;
inline uint32_t g_gate_logs = 0u;
inline uint32_t g_window_user_calls = 0u;
inline int32_t g_button_state = -1;
inline uint32_t g_button_logs = 0u;

template <typename T>
bool read_field(void* object, const char* name, T* out) {
    static_assert(sizeof(T) <= 8, "diagnostic field must be scalar/pointer");
    if (object == nullptr || out == nullptr ||
        il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_field_from_name == nullptr ||
        il2cpp::field_get_value == nullptr) return false;
    void* klass = il2cpp::object_get_class(object);
    void* field = klass != nullptr
                      ? il2cpp::class_get_field_from_name(klass, name)
                      : nullptr;
    if (field == nullptr) return false;
    alignas(8) uint8_t scratch[16] = {0};
    il2cpp::field_get_value(object, field, scratch);
    std::memcpy(out, scratch, sizeof(T));
    return true;
}

inline const char* presence(const void* pointer) {
    return pointer != nullptr ? "present" : "NULL";
}

// ---------------------------------------------------------------------------
// Respawn interface
// ---------------------------------------------------------------------------

inline void death_entry_hook(void* self, const MethodInfo* method) {
    const uint32_t index = ++g_deaths;
    LOGW("23.1.3-battle-flow: -> local death #%" PRIu32 " self=%p; the respawn"
         " coroutine is started from here", index, self);
    if (g_death_entry == nullptr) {
        LOGE("23.1.3-battle-flow: death entry has no saved original");
        return;
    }
    g_death_entry(self, method);
    LOGW("23.1.3-battle-flow: <- local death #%" PRIu32 " returned", index);
}

// This one can be polled from Update, so it is heavily throttled: the log
// must stay readable next to the coroutine states.
inline void window_user_hook(void* self, const MethodInfo* method) {
    const uint32_t calls = ++g_window_user_calls;
    const bool verbose = calls <= kRepeatLogLimit || (calls % 300u) == 0u;
    if (verbose) {
        LOGI("23.1.3-battle-flow: -> Player_move_c respawn-window consumer"
             " self=%p call=%" PRIu32, self, calls);
    }
    if (g_window_user == nullptr) {
        LOGE("23.1.3-battle-flow: window consumer has no saved original");
        return;
    }
    g_window_user(self, method);
    if (verbose) {
        LOGI("23.1.3-battle-flow: <- Player_move_c respawn-window consumer"
             " done call=%" PRIu32, calls);
    }
}

inline void* get_window_hook(void* self, const MethodInfo* method) {
    if (g_get_window == nullptr) {
        LOGE("23.1.3-battle-flow: get_window has no saved original");
        return nullptr;
    }
    void* window = g_get_window(self, method);
    const int32_t is_null = window == nullptr ? 1 : 0;
    if (is_null != g_window_null) {
        g_window_null = is_null;
        if (is_null != 0) {
            LOGE("23.1.3-battle-flow: respawn window handle -> NULL; the"
                 " prefab was not instantiated and the next stock null check"
                 " raises");
        } else {
            LOGI("23.1.3-battle-flow: respawn window handle -> present (%p)",
                 window);
        }
    } else if (is_null != 0 && ++g_null_window_logs <= kRepeatLogLimit) {
        LOGE("23.1.3-battle-flow: respawn window still NULL (repeat #%" PRIu32
             ")", g_null_window_logs);
    }
    return window;
}

inline void show_gate_hook(void* self, bool visible, const MethodInfo* method) {
    LOGW("23.1.3-battle-flow: -> respawn show gate self=%p visible=%d", self,
         visible ? 1 : 0);
    if (g_show_gate == nullptr) {
        LOGE("23.1.3-battle-flow: show gate has no saved original");
        return;
    }
    g_show_gate(self, visible, method);
    LOGW("23.1.3-battle-flow: <- respawn show gate returned (visible=%d)",
         visible ? 1 : 0);
}

inline void camera_setup_hook(void* self, void* killer_info,
                              const MethodInfo* method) {
    LOGW("23.1.3-battle-flow: -> respawn camera setup killerInfo=%s",
         presence(killer_info));
    if (g_camera_setup == nullptr) {
        LOGE("23.1.3-battle-flow: camera setup has no saved original");
        return;
    }
    g_camera_setup(self, killer_info, method);
    LOGW("23.1.3-battle-flow: <- respawn camera setup returned");
}

// State transitions only: a live coroutine is stepped every frame. An entry
// line without the matching exit line is the signature of an exception thrown
// inside that state.
inline bool move_next_hook(void* self, const MethodInfo* method) {
    if (g_move_next == nullptr) {
        LOGE("23.1.3-battle-flow: respawn coroutine has no saved original;"
             " refusing to stop it");
        return false;
    }

    int32_t before = kNoState;
    const bool have_before = read_field(self, kStateField, &before);
    if (have_before && before != g_iterator_state) {
        LOGI("23.1.3-battle-flow: respawn coroutine -> state %" PRId32, before);
        g_iterator_state = before;
    }

    const bool running = g_move_next(self, method);

    int32_t after = kNoState;
    const bool have_after = read_field(self, kStateField, &after);
    if (have_after && after != g_iterator_state) {
        LOGI("23.1.3-battle-flow: respawn coroutine state %" PRId32
             " -> %" PRId32 " running=%d",
             g_iterator_state, after, running ? 1 : 0);
        g_iterator_state = after;
    }
    if (!running) {
        LOGI("23.1.3-battle-flow: respawn coroutine finished at state %" PRId32,
             after);
        g_iterator_state = kNoState;
    }
    return running;
}

inline void window_show_hook(void* self, void* killer_info,
                             const MethodInfo* method) {
    LOGW("23.1.3-battle-flow: -> RespawnWindow show self=%p killerInfo=%s",
         self, presence(killer_info));
    if (g_window_show == nullptr) {
        LOGE("23.1.3-battle-flow: window show has no saved original");
        return;
    }
    g_window_show(self, killer_info, method);
    LOGW("23.1.3-battle-flow: <- RespawnWindow show returned");
}

// Called from three coroutine sites and possibly per frame; only argument
// changes carry information about the interface state.
inline void button_active_hook(void* self, bool active, bool second,
                               const MethodInfo* method) {
    const int32_t combined = (active ? 2 : 0) | (second ? 1 : 0);
    if (combined != g_button_state || ++g_button_logs <= kRepeatLogLimit) {
        g_button_state = combined;
        LOGI("23.1.3-battle-flow: respawn button active=%d secondary=%d",
             active ? 1 : 0, second ? 1 : 0);
    }
    if (g_button_active == nullptr) {
        LOGE("23.1.3-battle-flow: button setter has no saved original");
        return;
    }
    g_button_active(self, active, second, method);
}

inline void window_close_hook(void* self, const MethodInfo* method) {
    LOGI("23.1.3-battle-flow: RespawnWindow.CloseRespawnWindow self=%p", self);
    if (g_window_close == nullptr) {
        LOGE("23.1.3-battle-flow: close has no saved original");
        return;
    }
    g_window_close(self, method);
}

inline void window_hide_hook(void* self, const MethodInfo* method) {
    LOGI("23.1.3-battle-flow: RespawnWindow.Hide self=%p", self);
    if (g_window_hide == nullptr) {
        LOGE("23.1.3-battle-flow: hide has no saved original");
        return;
    }
    g_window_hide(self, method);
}

inline void window_enable_hook(void* self, const MethodInfo* method) {
    LOGI("23.1.3-battle-flow: RespawnWindow.OnEnable self=%p", self);
    if (g_window_enable == nullptr) {
        LOGE("23.1.3-battle-flow: OnEnable has no saved original");
        return;
    }
    g_window_enable(self, method);
    LOGI("23.1.3-battle-flow: RespawnWindow.OnEnable returned");
}

// ---------------------------------------------------------------------------
// Profile / Stats tab
// ---------------------------------------------------------------------------

inline void profile_request_hook(void* self, ManagedString* nickname,
                                 int32_t source, void* callback, int32_t extra,
                                 const MethodInfo* method) {
    const std::string name = il2cpp::to_utf8(nickname, kMaxStringChars);
    LOGW("23.1.3-battle-flow: -> profile request nickname='%s' source=%" PRId32
         " callback=%s extra=%" PRId32,
         name.c_str(), source, presence(callback), extra);
    if (g_profile_request == nullptr) {
        LOGE("23.1.3-battle-flow: profile request has no saved original");
        return;
    }
    g_profile_request(self, nickname, source, callback, extra, method);
    LOGW("23.1.3-battle-flow: <- profile request returned");
}

inline void profile_open_hook(void* self, ManagedString* nickname, void* data,
                              int32_t number, int32_t source, void* callback,
                              const MethodInfo* method) {
    const std::string name = il2cpp::to_utf8(nickname, kMaxStringChars);
    LOGW("23.1.3-battle-flow: -> profile open nickname='%s' playerData=%s"
         " number=%" PRId32 " source=%" PRId32 " callback=%s",
         name.c_str(), presence(data), number, source, presence(callback));
    if (g_profile_open == nullptr) {
        LOGE("23.1.3-battle-flow: profile open has no saved original");
        return;
    }
    g_profile_open(self, nickname, data, number, source, callback, method);
    LOGW("23.1.3-battle-flow: <- profile open returned");
}

inline bool profile_gate_hook(void* self, const MethodInfo* method) {
    if (g_profile_gate == nullptr) {
        LOGE("23.1.3-battle-flow: profile gate has no saved original");
        return false;
    }
    const bool ready = g_profile_gate(self, method);
    const int32_t value = ready ? 1 : 0;
    if (value != g_gate_result) {
        g_gate_result = value;
        LOGW("23.1.3-battle-flow: profile data gate -> %d (false keeps the"
             " \"receiving data\" caption on screen)", value);
    } else if (++g_gate_logs <= kRepeatLogLimit) {
        LOGI("23.1.3-battle-flow: profile data gate = %d (repeat #%" PRIu32
             ")", value, g_gate_logs);
    }
    return ready;
}

inline void profile_reveal_hook(void* self, const MethodInfo* method) {
    LOGW("23.1.3-battle-flow: -> profile reveal node (hides the pending-data"
         " caption) self=%p", self);
    if (g_profile_reveal == nullptr) {
        LOGE("23.1.3-battle-flow: profile reveal has no saved original");
        return;
    }
    g_profile_reveal(self, method);
    LOGW("23.1.3-battle-flow: <- profile reveal node returned");
}

inline void profile_fill_hook(void* self, bool flag,
                              const MethodInfo* method) {
    LOGW("23.1.3-battle-flow: -> profile views fill flag=%d", flag ? 1 : 0);
    if (g_profile_fill == nullptr) {
        LOGE("23.1.3-battle-flow: profile fill has no saved original");
        return;
    }
    g_profile_fill(self, flag, method);
    LOGW("23.1.3-battle-flow: <- profile views fill returned");
}

inline void stats_fill_hook(void* self, void* data,
                            const MethodInfo* method) {
    LOGW("23.1.3-battle-flow: -> stats tab fill playerData=%s",
         presence(data));
    if (g_stats_fill == nullptr) {
        LOGE("23.1.3-battle-flow: stats fill has no saved original");
        return;
    }
    g_stats_fill(self, data, method);
    LOGW("23.1.3-battle-flow: <- stats tab fill returned");
}

inline bool add(const hook::ManagedMethod& method, void* replacement_pointer,
                void** original_pointer, int* installed) {
    const bool ok = hook::install(method, replacement_pointer,
                                 original_pointer, false);
    if (ok) ++(*installed);
    else
        LOGW("23.1.3-battle-flow: could not hook %s.%s", method.klass,
             method.method);
    return ok;
}

} // namespace detail

inline bool install_hooks() {
    using namespace detail;
    int installed = 0;

    // Respawn interface, in the order the stock code runs it.
    const bool death = add({kNs, kPlayer, kDeathEntry, 0},
                           reinterpret_cast<void*>(&death_entry_hook),
                           reinterpret_cast<void**>(&g_death_entry),
                           &installed);
    add({kNs, kPlayer, kWindowUser, 0},
        reinterpret_cast<void*>(&window_user_hook),
        reinterpret_cast<void**>(&g_window_user), &installed);
    const bool window = add({kNs, kController, "get_window", 0},
                            reinterpret_cast<void*>(&get_window_hook),
                            reinterpret_cast<void**>(&g_get_window),
                            &installed);
    add({kNs, kController, kShowGate, 1},
        reinterpret_cast<void*>(&show_gate_hook),
        reinterpret_cast<void**>(&g_show_gate), &installed);
    add({kNs, kController, kCameraSetup, 1},
        reinterpret_cast<void*>(&camera_setup_hook),
        reinterpret_cast<void**>(&g_camera_setup), &installed);
    const bool coroutine = add({kNs, kIterator, "MoveNext", 0},
                               reinterpret_cast<void*>(&move_next_hook),
                               reinterpret_cast<void**>(&g_move_next),
                               &installed);
    add({kNs, kWindow, kWindowShow, 1},
        reinterpret_cast<void*>(&window_show_hook),
        reinterpret_cast<void**>(&g_window_show), &installed);
    add({kNs, kWindow, "SetRespawnButtonActive", 2},
        reinterpret_cast<void*>(&button_active_hook),
        reinterpret_cast<void**>(&g_button_active), &installed);
    add({kNs, kWindow, "CloseRespawnWindow", 0},
        reinterpret_cast<void*>(&window_close_hook),
        reinterpret_cast<void**>(&g_window_close), &installed);
    add({kNs, kWindow, "Hide", 0},
        reinterpret_cast<void*>(&window_hide_hook),
        reinterpret_cast<void**>(&g_window_hide), &installed);
    add({kNs, kWindow, "OnEnable", 0},
        reinterpret_cast<void*>(&window_enable_hook),
        reinterpret_cast<void**>(&g_window_enable), &installed);

    // Profile / Stats tab.
    const bool request = add({kNs, kProfileGui, kProfileRequest, 4},
                             reinterpret_cast<void*>(&profile_request_hook),
                             reinterpret_cast<void**>(&g_profile_request),
                             &installed);
    add({kNs, kProfileView, kProfileOpen, 5},
        reinterpret_cast<void*>(&profile_open_hook),
        reinterpret_cast<void**>(&g_profile_open), &installed);
    const bool gate = add({kNs, kProfileView, kProfileGate, 0},
                          reinterpret_cast<void*>(&profile_gate_hook),
                          reinterpret_cast<void**>(&g_profile_gate),
                          &installed);
    const bool reveal = add({kNs, kProfileView, kProfileReveal, 0},
                            reinterpret_cast<void*>(&profile_reveal_hook),
                            reinterpret_cast<void**>(&g_profile_reveal),
                            &installed);
    add({kNs, kProfileView, kProfileFill, 1},
        reinterpret_cast<void*>(&profile_fill_hook),
        reinterpret_cast<void**>(&g_profile_fill), &installed);
    add({kNs, kStatsView, kStatsFill, 1},
        reinterpret_cast<void*>(&stats_fill_hook),
        reinterpret_cast<void**>(&g_stats_fill), &installed);

    LOGI("23.1.3-battle-flow: installed %d/17 passive hooks (death=%s"
         " window=%s coroutine=%s profile-request=%s gate=%s reveal=%s)",
         installed, death ? "OK" : "FAILED", window ? "OK" : "FAILED",
         coroutine ? "OK" : "FAILED", request ? "OK" : "FAILED",
         gate ? "OK" : "FAILED", reveal ? "OK" : "FAILED");
    return death && window && coroutine && gate && reveal;
}

} // namespace battle_flow_trace_2313
