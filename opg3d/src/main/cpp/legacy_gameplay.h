#pragma once

#include <atomic>
#include <cstdint>
#include <ctime>

#include "elf_sym.h"
#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Local replacements for legacy services that disappeared with the original
// PG3D backend. Stock inventory, upgrade and save routines remain responsible
// for state; this module supplies tutorial completion and wall-clock input.
namespace legacy_gameplay {
namespace detail {

using MethodInfo = void;
using ManagedString = void;
using TrainingCompletedFn = bool (*)(void* static_context,
                                     const MethodInfo* method);
using SetTrainingStageFn = void (*)(void* static_context, int32_t stage,
                                    const MethodInfo* method);
using StoragerSetIntFn = void (*)(void* static_context, ManagedString* key,
                                 int32_t value, bool direct_write,
                                 bool direct_write_v2,
                                 const MethodInfo* method);
using ServerTimeFn = int64_t (*)(void* static_context,
                                 const MethodInfo* method);
using GcHandleNewFn = uint32_t (*)(void* object, bool pinned);
using GcHandleGetTargetFn = void* (*)(uint32_t handle);

inline TrainingCompletedFn g_training_completed = nullptr;
inline SetTrainingStageFn g_set_training_stage = nullptr;
inline const MethodInfo* g_mi_set_training_stage = nullptr;
inline StoragerSetIntFn g_storager_set_int = nullptr;
inline const MethodInfo* g_mi_storager_set_int = nullptr;
inline ServerTimeFn g_server_time = nullptr;
inline GcHandleNewFn g_gchandle_new = nullptr;
inline GcHandleGetTargetFn g_gchandle_get_target = nullptr;
inline uint32_t g_shop_tutorial_key_handle = 0;

inline std::atomic<bool> g_tutorial_persisted{false};
inline std::atomic<bool> g_time_fallback_logged{false};
inline std::atomic<int32_t> g_last_time{0};
inline thread_local bool g_persisting_tutorial = false;

inline constexpr int32_t kFirstMatchCompleted = 3;
inline constexpr const char* kShopTutorialKey =
    "shop_tutorial_state_passed_VER_12_1";

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
    void* pointer = il2cpp::method_pointer(info);
    if (info == nullptr || pointer == nullptr) {
        LOGE("legacy: cannot resolve %s.%s/%d", target.klass, target.method,
             target.args_count);
        return false;
    }
    *out_fn = pointer;
    *out_mi = info;
    return true;
}

bool hook_training_completed(void* static_context,
                             const MethodInfo* method) {
    if (g_persisting_tutorial) return true;

    const bool was_completed = g_training_completed(static_context, method);
    if (!g_tutorial_persisted.load(std::memory_order_acquire)) {
        g_persisting_tutorial = true;
        g_set_training_stage(nullptr, kFirstMatchCompleted,
                             g_mi_set_training_stage);
        auto* shop_key = static_cast<ManagedString*>(
            g_gchandle_get_target(g_shop_tutorial_key_handle));
        g_storager_set_int(nullptr, shop_key, 1, false, false,
                           g_mi_storager_set_int);
        g_persisting_tutorial = false;
        g_tutorial_persisted.store(true, std::memory_order_release);
        LOGI("legacy: tutorial %s; stage 3 and shop tutorial completion saved",
             was_completed ? "already completed" : "skipped automatically");
    }
    return true;
}

int64_t hook_server_time(void* static_context, const MethodInfo* method) {
    const int64_t stock = g_server_time(static_context, method);
    int64_t candidate = stock;
    if (candidate <= 0) {
        candidate = static_cast<int64_t>(std::time(nullptr));
        bool expected = false;
        if (g_time_fallback_logged.compare_exchange_strong(expected, true)) {
            LOGI("legacy: retired server-time endpoint unavailable; using local "
                 "UTC seconds for crafting and upgrades");
        }
    }
    if (candidate <= 0) candidate = 1;

    // Unix seconds fit in signed 32-bit for the target game's lifetime. The
    // 32-bit atomic also avoids a libatomic dependency on ARMv7.
    int32_t current = candidate > INT32_MAX
                          ? INT32_MAX
                          : static_cast<int32_t>(candidate);
    int32_t observed = g_last_time.load(std::memory_order_relaxed);
    while (current > observed &&
           !g_last_time.compare_exchange_weak(observed, current,
                                              std::memory_order_relaxed)) {
    }
    return static_cast<int64_t>(current < observed ? observed : current);
}

} // namespace detail

inline bool install_hooks() {
    bool ok = true;
    ok &= detail::resolve_call(
        {"", "TrainingController", "set_CompletedTrainingStage", 1},
        reinterpret_cast<void**>(&detail::g_set_training_stage),
        &detail::g_mi_set_training_stage);
    ok &= detail::resolve_call(
        {"", "Storager", "setInt", 4},
        reinterpret_cast<void**>(&detail::g_storager_set_int),
        &detail::g_mi_storager_set_int);

    detail::g_gchandle_new = reinterpret_cast<detail::GcHandleNewFn>(
        elfsym::find_symbol("libil2cpp.so", "il2cpp_gchandle_new"));
    detail::g_gchandle_get_target =
        reinterpret_cast<detail::GcHandleGetTargetFn>(
            elfsym::find_symbol("libil2cpp.so",
                                "il2cpp_gchandle_get_target"));
    auto* shop_key = static_cast<detail::ManagedString*>(
        il2cpp::string_new(detail::kShopTutorialKey));
    if (shop_key == nullptr || detail::g_gchandle_new == nullptr ||
        detail::g_gchandle_get_target == nullptr) {
        LOGE("legacy: managed string/rooting API unavailable");
        ok = false;
    }
    if (ok) {
        detail::g_shop_tutorial_key_handle =
            detail::g_gchandle_new(shop_key, false);
        if (detail::g_shop_tutorial_key_handle == 0) {
            LOGE("legacy: failed to root shop tutorial key");
            ok = false;
        }
    }
    if (!ok) {
        LOGE("legacy: tutorial or upgrade-time targets incomplete");
        return false;
    }

    const bool tutorial = hook::install(
        {"", "TrainingController", "get_TrainingCompleted", 0},
        detail::replacement(&detail::hook_training_completed),
        detail::original_slot(&detail::g_training_completed), true);
    const bool server_time = hook::install(
        {"", "FriendsController", "get_ServerTime", 0},
        detail::replacement(&detail::hook_server_time),
        detail::original_slot(&detail::g_server_time), true);
    if (!tutorial || !server_time) {
        LOGE("legacy: tutorial or server-time hook failed");
        return false;
    }

    LOGI("legacy: tutorial auto-skip and local upgrade/crafting time armed");
    return true;
}

} // namespace legacy_gameplay
