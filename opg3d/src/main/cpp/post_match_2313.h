#pragma once

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "elf_sym.h"
#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// 23.1.3 end-of-match screen repair.
//
// Symptom: after a match the victory screen shows only the "team won" caption.
// There is no player table, no reward panel and no OK button, so the screen
// cannot be closed by hand.
//
// What the revision-2 trace proved on device (2026-08-24 log):
//
//   results wrapper B  -> payload exp=20 coins=4 gems=0 amIWinner=true
//   results coroutine state -> 0
//   panel switch a=true b=false
//   results coroutine state 0 -> 1          <- last transition, ever
//   OnTablesShow / OnMatchEndAnimationDone / OnTablesShown fire 2.4 s later
//   reward-delay state -> 0 then 0 -> 1     <- last transition, ever
//
// So the data half does start. NetworkStartTableNGUIController.世丟丈丄丙丒专丘丂
// .MoveNext (RVA 0x4E61894) runs its first segment, calls the panel switch
// 三丕丟丅丐丕丆丘万(true, false) at +0x16A8 - the code that enables the
// finishedInterface panel, plays the win sound and writes the caption into
// finishedInterfaceLabels, which is exactly the single thing the player sees -
// and then parks on a wait:
//
//   +0x16B0  strb w8,  [x19, #0x5B0]   controller.与东丞丙丏丝丗与业 = true
//   +0x16B8  ldrb w8,  [x19, #0x5B0]   while (that flag)
//   +0x16C0  str  xzr, [x20, #0x18]      <>2__current = null
//   +0x16D4  stur w0,  [x20, #-0x8]      <>1__state  = 1   (yield return null)
//
// That flag is cleared by the very first instruction of OnTablesShow
// (0x484FDFC: strb wzr, [x0, #0x5B0]) and the log shows OnTablesShow firing
// 2.4 s after the park - yet the coroutine never advanced past state 1. A
// "yield return null" that is not resumed once its wait condition is already
// satisfied means nobody steps the routine any more, and the reward-delay
// coroutine (a different iterator on the same controller) dies the same way at
// its own first yield. Two independent coroutines of one MonoBehaviour going
// silent after exactly one step is the Unity signature of a host GameObject
// that is not active: StartCoroutine runs the first step inline, then the
// scheduler drops the routine and never resumes it. Everything that still
// fires afterwards is an animation event, which Unity delivers through another
// object, so the animation half looks healthy while the data half is frozen.
//
// This module therefore does three things, in order:
//
//   1. Notice the freeze. Every iterator step passes through here, so "Unity
//      has not stepped this routine for kOrphanMs while it sits at a yield" is
//      an observation, not a guess.
//   2. Repair it the way the game itself would: switch the inactive GameObject
//      chain that hosts the controller back on and hand the very same iterator
//      back to Unity through MonoBehaviour.StartCoroutineManaged2, so the stock
//      coroutine finishes the job with stock timing - player table, rewards,
//      trophy and the OK button.
//   3. Guarantee an exit. If the results coroutine still has not completed
//      kGiveUpMs after the freeze was first seen, press the screen's own
//      Continue button (HandleContinue_GoToLobbyButton, 0x4846310) so the
//      player always reaches the next screen instead of a dead end.
//
// Nothing is fabricated: no reward is granted, no label is written and no
// payload field is modified. Every managed target is resolved by metadata name
// at runtime and the module fails closed; the RVAs above are review aids only.
//
// The passive revision-2 instrumentation lives on in post_match_trace_2313.h.
// It is deliberately no longer installed - both modules hook the same iterator
// and shadowhook keeps one hook per target - so re-enable it in main.cpp only
// when a future flow needs to be mapped again.

namespace post_match_2313 {
namespace {

constexpr const char* kTag = "23.1.3-post-match";
constexpr const char* kGlobalNs = "";
constexpr const char* kController = "NetworkStartTableNGUIController";
constexpr const char* kTable = "NetworkStartTable";
constexpr const char* kStateField = "<>1__state";
constexpr const char* kThisField = "<>4__this";

// A routine that has not been stepped for this long while parked at a yield is
// no longer owned by Unity's scheduler. Three frames at 30 fps is 100 ms, so
// half a second cannot be confused with a slow frame.
constexpr uint64_t kOrphanMs = 500;
// Time given to Unity to pick the routine up again after a hand-back.
constexpr uint64_t kRestartGraceMs = 1500;
// The screen may never trap the player: this is the deadline for the whole
// repair, measured from the moment the freeze was first seen.
constexpr uint64_t kGiveUpMs = 12000;
constexpr int kMaxRepairs = 3;
constexpr int kMaxParents = 8;

constexpr int kSlotResults = 0;
constexpr int kSlotCount = 5;

struct IteratorTarget {
    const char* klass;
    const char* label;
};

// The five compiler-generated iterators of the end-of-match controller
// (dump2313.cs lines 50183-50398). The results one carries the whole match
// payload; the other four run the reward queue. Nested spelling is what
// il2cpp::find_class() needs, because metadata v22 reaches a nested type only
// through its declaring type.
constexpr IteratorTarget kIterators[kSlotCount] = {
    {"NetworkStartTableNGUIController/\u4e16\u4e1f\u4e08\u4e04\u4e19\u4e12\u4e13\u4e18\u4e02",
     "results"},
    {"NetworkStartTableNGUIController/\u4e1b\u4e0c\u4e19\u4e1a\u4e14\u4e1a\u4e16\u4e09\u4e0e",
     "reward-delay"},
    {"NetworkStartTableNGUIController/\u4e0e\u4e16\u4e12\u4e0a\u4e14\u4e19\u4e0d\u4e06\u4e1c",
     "reward-queue-a"},
    {"NetworkStartTableNGUIController/\u4e15\u4e15\u4e18\u4e0b\u4e18\u4e03\u4e0a\u4e0d\u4e15",
     "reward-queue-b"},
    {"NetworkStartTableNGUIController/\u4e0a\u4e00\u4e0e\u4e00\u4e0c\u4e0a\u4e06\u4e14\u4e16",
     "reward-queue-c"},
};

using MoveNextFn = bool (*)(void*, void*);
using UpdateFn = void (*)(void*, void*);

struct Slot {
    void* iterator;
    void* controller;
    uint32_t handle;
    uint64_t last_step_ms;
    uint64_t orphan_since_ms;
    uint64_t restart_ms;
    int32_t state;
    int repairs;
    bool live;
    bool finished;
    bool orphan;
    bool exit_pressed;
};

// Everything below runs on the Unity main thread only: coroutine steps come
// from SetupCoroutine.InvokeMoveNext and the heartbeats are Update() bodies.
Slot g_slots[kSlotCount] = {};
MoveNextFn g_orig_move_next[kSlotCount] = {};
UpdateFn g_orig_table_update = nullptr;
UpdateFn g_orig_controller_update = nullptr;
bool g_any_live = false;
uint64_t g_last_service_ms = 0;

// ---------------------------------------------------------------------------
// GC handles. Once Unity drops a routine, the only remaining reference to the
// iterator may be the raw pointer seen in the hook, and a collection between
// two heartbeats would leave it dangling. il2cpp_gchandle_new keeps the object
// alive and il2cpp_gchandle_get_target hands back a pointer that is safe to
// use; both are plain exports of the already mapped libil2cpp.so.
uint32_t (*g_gchandle_new)(void*, bool) = nullptr;
void* (*g_gchandle_get_target)(uint32_t) = nullptr;
void (*g_gchandle_free)(uint32_t) = nullptr;

void resolve_gc_api() {
    if (g_gchandle_new != nullptr) return;
    g_gchandle_new = reinterpret_cast<uint32_t (*)(void*, bool)>(
        elfsym::find_symbol("libil2cpp.so", "il2cpp_gchandle_new"));
    g_gchandle_get_target = reinterpret_cast<void* (*)(uint32_t)>(
        elfsym::find_symbol("libil2cpp.so", "il2cpp_gchandle_get_target"));
    g_gchandle_free = reinterpret_cast<void (*)(uint32_t)>(
        elfsym::find_symbol("libil2cpp.so", "il2cpp_gchandle_free"));
}

// ---------------------------------------------------------------------------
// Managed helpers.

struct ManagedCall {
    void* info = nullptr;
    void* code = nullptr;
    bool ok() const { return info != nullptr && code != nullptr; }
};

ManagedCall resolve_call(const char* namespaze, const char* klass,
                         const char* method, int args_count) {
    ManagedCall call;
    call.info = il2cpp::find_method_info(namespaze, klass, method, args_count);
    if (call.info != nullptr) call.code = il2cpp::method_pointer(call.info);
    return call;
}

void* call_object(const ManagedCall& call, void* self) {
    if (!call.ok() || self == nullptr) return nullptr;
    using Fn = void* (*)(void*, void*);
    return reinterpret_cast<Fn>(call.code)(self, call.info);
}

bool call_bool(const ManagedCall& call, void* self, bool fallback) {
    if (!call.ok() || self == nullptr) return fallback;
    using Fn = bool (*)(void*, void*);
    return reinterpret_cast<Fn>(call.code)(self, call.info);
}

bool call_static_bool(const ManagedCall& call, void* argument, bool fallback) {
    if (!call.ok()) return fallback;
    using Fn = bool (*)(void*, void*);
    return reinterpret_cast<Fn>(call.code)(argument, call.info);
}

void call_void(const ManagedCall& call, void* self) {
    if (!call.ok() || self == nullptr) return;
    using Fn = void (*)(void*, void*);
    reinterpret_cast<Fn>(call.code)(self, call.info);
}

void call_void_bool(const ManagedCall& call, void* self, bool value) {
    if (!call.ok() || self == nullptr) return;
    using Fn = void (*)(void*, bool, void*);
    reinterpret_cast<Fn>(call.code)(self, value, call.info);
}

void* call_object_object(const ManagedCall& call, void* self, void* argument) {
    if (!call.ok() || self == nullptr) return nullptr;
    using Fn = void* (*)(void*, void*, void*);
    return reinterpret_cast<Fn>(call.code)(self, argument, call.info);
}

struct UnityApi {
    ManagedCall game_object;      // UnityEngine.Component.get_gameObject()
    ManagedCall transform;        // UnityEngine.Component.get_transform()
    ManagedCall parent;           // UnityEngine.Transform.get_parent()
    ManagedCall active_self;      // UnityEngine.GameObject.get_activeSelf()
    ManagedCall active_in_tree;   // UnityEngine.GameObject.get_activeInHierarchy()
    ManagedCall set_active;       // UnityEngine.GameObject.SetActive(bool)
    ManagedCall name;             // UnityEngine.Object.get_name()
    ManagedCall alive;            // UnityEngine.Object.op_Implicit(Object)
    ManagedCall start_coroutine;  // UnityEngine.MonoBehaviour
    ManagedCall press_continue;   // controller.HandleContinue_GoToLobbyButton()
    bool resolved = false;
};

UnityApi g_api;

void resolve_api() {
    if (g_api.resolved) return;
    g_api.resolved = true;
    g_api.game_object = resolve_call("UnityEngine", "Component", "get_gameObject", 0);
    g_api.transform = resolve_call("UnityEngine", "Component", "get_transform", 0);
    g_api.parent = resolve_call("UnityEngine", "Transform", "get_parent", 0);
    g_api.active_self = resolve_call("UnityEngine", "GameObject", "get_activeSelf", 0);
    g_api.active_in_tree =
        resolve_call("UnityEngine", "GameObject", "get_activeInHierarchy", 0);
    g_api.set_active = resolve_call("UnityEngine", "GameObject", "SetActive", 1);
    g_api.name = resolve_call("UnityEngine", "Object", "get_name", 0);
    // The implicit bool operator is the only safe liveness test for a Unity
    // object: it never throws, while touching a property of a destroyed object
    // raises a managed exception that must not cross back into this library.
    g_api.alive = resolve_call("UnityEngine", "Object", "op_Implicit", 1);
    // MonoBehaviour has three one-argument StartCoroutine overloads, so a
    // lookup by name and argument count would be ambiguous. This is the
    // internal entry that StartCoroutine(IEnumerator) itself calls, and its
    // name is unique.
    g_api.start_coroutine =
        resolve_call("UnityEngine", "MonoBehaviour", "StartCoroutineManaged2", 1);
    if (!g_api.start_coroutine.ok()) {
        g_api.start_coroutine =
            resolve_call("UnityEngine", "MonoBehaviour", "StartCoroutine_Auto", 1);
    }
    g_api.press_continue =
        resolve_call(kGlobalNs, kController, "HandleContinue_GoToLobbyButton", 0);
    LOGI("%s: managed api gameObject=%d transform=%d parent=%d activeSelf=%d "
         "activeInHierarchy=%d setActive=%d name=%d alive=%d startCoroutine=%d "
         "continue=%d",
         kTag, g_api.game_object.ok() ? 1 : 0, g_api.transform.ok() ? 1 : 0,
         g_api.parent.ok() ? 1 : 0, g_api.active_self.ok() ? 1 : 0,
         g_api.active_in_tree.ok() ? 1 : 0, g_api.set_active.ok() ? 1 : 0,
         g_api.name.ok() ? 1 : 0, g_api.alive.ok() ? 1 : 0,
         g_api.start_coroutine.ok() ? 1 : 0, g_api.press_continue.ok() ? 1 : 0);
}

bool unity_alive(void* unity_object) {
    if (unity_object == nullptr) return false;
    return call_static_bool(g_api.alive, unity_object, true);
}

std::string object_name(void* unity_object) {
    void* managed = call_object(g_api.name, unity_object);
    if (managed == nullptr) return std::string("?");
    return il2cpp::to_utf8(managed, 64);
}

template <typename T>
bool read_field(void* object, const char* name, T* out) {
    if (object == nullptr || out == nullptr) return false;
    if (il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_field_from_name == nullptr ||
        il2cpp::field_get_value == nullptr) {
        return false;
    }
    void* klass = il2cpp::object_get_class(object);
    if (klass == nullptr) return false;
    void* field = il2cpp::class_get_field_from_name(klass, name);
    if (field == nullptr) return false;
    il2cpp::field_get_value(object, field, out);
    return true;
}

int32_t read_state(void* iterator) {
    int32_t value = INT32_MIN;
    if (!read_field(iterator, kStateField, &value)) return INT32_MIN;
    return value;
}

void* read_reference(void* object, const char* name) {
    void* value = nullptr;
    if (!read_field(object, name, &value)) return nullptr;
    return value;
}

// ---------------------------------------------------------------------------
// Slot bookkeeping.

void release(Slot& slot) {
    if (slot.handle != 0u && g_gchandle_free != nullptr) g_gchandle_free(slot.handle);
    slot.handle = 0u;
    slot.finished = true;
    slot.orphan = false;
}

void* live_iterator(const Slot& slot) {
    if (slot.handle != 0u && g_gchandle_get_target != nullptr) {
        return g_gchandle_get_target(slot.handle);
    }
    return slot.iterator;
}

void adopt(Slot& slot, int index, void* iterator) {
    if (slot.handle != 0u && g_gchandle_free != nullptr) g_gchandle_free(slot.handle);
    slot = Slot{};
    slot.iterator = iterator;
    slot.live = true;
    slot.state = INT32_MIN;
    if (g_gchandle_new != nullptr) slot.handle = g_gchandle_new(iterator, false);
    g_any_live = true;
    LOGI("%s: %s coroutine started (pinned=%d)", kTag, kIterators[index].label,
         slot.handle != 0u ? 1 : 0);
}

// Switches the inactive GameObject chain that hosts `component` back on.
// Returns the number of objects that had to be re-activated, or -1 when the
// hierarchy could not be inspected.
int revive_host(void* component) {
    void* host = call_object(g_api.game_object, component);
    if (host == nullptr || !unity_alive(host)) return -1;
    if (call_bool(g_api.active_in_tree, host, true)) return 0;

    int revived = 0;
    if (!call_bool(g_api.active_self, host, true)) {
        LOGI("%s: re-activating host object '%s'", kTag, object_name(host).c_str());
        call_void_bool(g_api.set_active, host, true);
        ++revived;
    }
    void* transform = call_object(g_api.transform, component);
    for (int depth = 0; depth < kMaxParents && transform != nullptr; ++depth) {
        transform = call_object(g_api.parent, transform);
        if (transform == nullptr || !unity_alive(transform)) break;
        void* parent_host = call_object(g_api.game_object, transform);
        if (parent_host == nullptr || !unity_alive(parent_host)) break;
        if (call_bool(g_api.active_self, parent_host, true)) continue;
        LOGI("%s: re-activating parent object '%s'", kTag,
             object_name(parent_host).c_str());
        call_void_bool(g_api.set_active, parent_host, true);
        ++revived;
    }
    return revived;
}

void repair(Slot& slot, int index, uint64_t now, void* iterator) {
    const bool active_before =
        call_bool(g_api.active_in_tree, call_object(g_api.game_object, slot.controller),
                  true);
    LOGW("%s: %s coroutine was abandoned by Unity at state %" PRId32
         " (host active=%d, attempt %d)",
         kTag, kIterators[index].label, slot.state, active_before ? 1 : 0,
         slot.repairs + 1);

    const int revived = revive_host(slot.controller);
    void* accepted = call_object_object(g_api.start_coroutine, slot.controller, iterator);
    ++slot.repairs;
    slot.restart_ms = now;
    LOGI("%s: %s handed back to Unity (re-activated=%d accepted=%d)", kTag,
         kIterators[index].label, revived, accepted != nullptr ? 1 : 0);
}

void force_exit(Slot& slot, void* live_controller) {
    void* controller = live_controller != nullptr ? live_controller : slot.controller;
    if (controller == nullptr || !unity_alive(controller)) return;
    if (!g_api.press_continue.ok()) {
        slot.exit_pressed = true;
        LOGE("%s: results screen is stuck and its Continue handler is missing", kTag);
        return;
    }
    slot.exit_pressed = true;
    LOGW("%s: results screen never finished %" PRIu64
         "ms after the freeze; pressing its own Continue button so the player "
         "is not trapped",
         kTag, kGiveUpMs);
    call_void(g_api.press_continue, controller);
}

// One pass over the live routines. `live_controller` is non-null only when the
// heartbeat came from the controller's own Update(), which proves that this
// controller is alive and its object active.
void service(void* live_controller) {
    if (!g_any_live) return;
    const uint64_t now = ::opg3d_log::monotonic_ms();
    if (now == g_last_service_ms) return;
    g_last_service_ms = now;
    resolve_api();

    bool any_live = false;
    for (int index = 0; index < kSlotCount; ++index) {
        Slot& slot = g_slots[index];
        if (!slot.live || slot.finished) continue;

        void* iterator = live_iterator(slot);
        if (iterator == nullptr) {
            release(slot);
            continue;
        }
        if (slot.controller == nullptr) {
            slot.controller = read_reference(iterator, kThisField);
        }
        if (slot.controller != nullptr && !unity_alive(slot.controller)) {
            LOGI("%s: %s controller is gone; the screen was closed elsewhere", kTag,
                 kIterators[index].label);
            release(slot);
            continue;
        }
        any_live = true;

        const uint64_t idle = now > slot.last_step_ms ? now - slot.last_step_ms : 0u;
        if (idle < kOrphanMs) continue;
        if (!slot.orphan) {
            slot.orphan = true;
            if (slot.orphan_since_ms == 0u) slot.orphan_since_ms = now;
        }

        const bool waiting_for_unity =
            slot.restart_ms != 0u && now - slot.restart_ms < kRestartGraceMs;
        if (!waiting_for_unity && slot.repairs < kMaxRepairs) {
            repair(slot, index, now, iterator);
            continue;
        }
        if (waiting_for_unity) continue;

        // Every repair attempt failed. The player still has to leave, and only
        // the results screen owns that transition.
        if (index != kSlotResults || slot.exit_pressed) continue;
        if (now - slot.orphan_since_ms < kGiveUpMs) continue;
        force_exit(slot, live_controller);
    }
    g_any_live = any_live;
}

bool step(int index, void* self, void* method) {
    Slot& slot = g_slots[index];
    if (self != nullptr && slot.iterator != self) adopt(slot, index, self);

    if (slot.orphan) {
        LOGI("%s: %s coroutine is being stepped again from state %" PRId32, kTag,
             kIterators[index].label, slot.state);
        slot.orphan = false;
        slot.orphan_since_ms = 0u;
        slot.restart_ms = 0u;
    }
    slot.last_step_ms = ::opg3d_log::monotonic_ms();

    MoveNextFn original = g_orig_move_next[index];
    const bool running = original != nullptr ? original(self, method) : false;

    slot.state = read_state(self);
    slot.last_step_ms = ::opg3d_log::monotonic_ms();
    if (!running) {
        LOGI("%s: %s coroutine finished normally", kTag, kIterators[index].label);
        release(slot);
    }
    return running;
}

bool results_move_next(void* self, void* method) { return step(0, self, method); }
bool reward_delay_move_next(void* self, void* method) { return step(1, self, method); }
bool reward_queue_a_move_next(void* self, void* method) { return step(2, self, method); }
bool reward_queue_b_move_next(void* self, void* method) { return step(3, self, method); }
bool reward_queue_c_move_next(void* self, void* method) { return step(4, self, method); }

// NetworkStartTable lives on its own object and keeps ticking even while the
// controller's object is switched off, so it is the heartbeat that can notice
// the freeze in the first place.
void table_update(void* self, void* method) {
    if (g_orig_table_update != nullptr) g_orig_table_update(self, method);
    service(nullptr);
}

// Once the host object is active again this one runs too, and its `self` is a
// provably live controller, which is what the guaranteed exit needs.
void controller_update(void* self, void* method) {
    if (g_orig_controller_update != nullptr) g_orig_controller_update(self, method);
    service(self);
}

} // namespace

inline bool install_hooks() {
    resolve_gc_api();

    void* const proxies[kSlotCount] = {
        reinterpret_cast<void*>(&results_move_next),
        reinterpret_cast<void*>(&reward_delay_move_next),
        reinterpret_cast<void*>(&reward_queue_a_move_next),
        reinterpret_cast<void*>(&reward_queue_b_move_next),
        reinterpret_cast<void*>(&reward_queue_c_move_next),
    };

    int iterators = 0;
    for (int index = 0; index < kSlotCount; ++index) {
        const hook::ManagedMethod target{kGlobalNs, kIterators[index].klass,
                                         "MoveNext", 0};
        if (hook::install(target, proxies[index],
                          reinterpret_cast<void**>(&g_orig_move_next[index]))) {
            ++iterators;
        } else {
            LOGE("%s: could not hook the %s iterator", kTag,
                 kIterators[index].label);
        }
    }

    const bool table_tick =
        hook::install({kGlobalNs, kTable, "Update", 0},
                      reinterpret_cast<void*>(&table_update),
                      reinterpret_cast<void**>(&g_orig_table_update));
    const bool controller_tick =
        hook::install({kGlobalNs, kController, "Update", 0},
                      reinterpret_cast<void*>(&controller_update),
                      reinterpret_cast<void**>(&g_orig_controller_update));

    const bool results_hooked = g_orig_move_next[kSlotResults] != nullptr;
    const bool ok = results_hooked && table_tick && controller_tick;
    if (ok) {
        LOGI("%s: repair armed (iterators=%d/%d table-tick=OK controller-tick=OK "
             "gc-handles=%d orphan=%" PRIu64 "ms give-up=%" PRIu64 "ms)",
             kTag, iterators, kSlotCount, g_gchandle_new != nullptr ? 1 : 0,
             kOrphanMs, kGiveUpMs);
    } else {
        LOGE("%s: repair NOT armed (iterators=%d/%d results=%d table-tick=%d "
             "controller-tick=%d)",
             kTag, iterators, kSlotCount, results_hooked ? 1 : 0,
             table_tick ? 1 : 0, controller_tick ? 1 : 0);
    }
    return ok;
}

} // namespace post_match_2313
