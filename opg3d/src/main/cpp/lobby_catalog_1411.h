#pragma once

#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// 14.1.1 changed LobbyItemsController.AddItemNow from AddItemNow(item) to
// AddItemNow(item, autoEquip = true). The inherited 13.2.1 lobby grant is
// deliberately left fail-closed when its one-argument target is absent; this
// module ports the same stock grant/save path to the new two-argument ABI.
namespace lobby_catalog_1411 {
namespace detail {

using MethodInfo = void;
using UpdateFn = void (*)(void* self, const MethodInfo* method);
using BoolInstanceFn = bool (*)(void* self, const MethodInfo* method);
using ObjectInstanceFn = void* (*)(void* self, const MethodInfo* method);
using VoidInstanceFn = void (*)(void* self, const MethodInfo* method);
using AddItemFn = bool (*)(void* self, void* item, bool auto_equip,
                           const MethodInfo* method);
using ListCountFn = int32_t (*)(void* self, const MethodInfo* method);
using ListItemFn = void* (*)(void* self, int32_t index,
                             const MethodInfo* method);

inline UpdateFn g_update = nullptr;
inline BoolInstanceFn g_is_ready = nullptr;
inline const MethodInfo* g_mi_is_ready = nullptr;
inline ObjectInstanceFn g_all_items = nullptr;
inline const MethodInfo* g_mi_all_items = nullptr;
inline AddItemFn g_add_item_now = nullptr;
inline const MethodInfo* g_mi_add_item_now = nullptr;
inline VoidInstanceFn g_save = nullptr;
inline const MethodInfo* g_mi_save = nullptr;
inline BoolInstanceFn g_item_exists = nullptr;
inline const MethodInfo* g_mi_item_exists = nullptr;
inline ListCountFn g_list_count = nullptr;
inline const MethodInfo* g_mi_list_count = nullptr;
inline ListItemFn g_list_item = nullptr;
inline const MethodInfo* g_mi_list_item = nullptr;

inline bool g_list_ready = false;
inline bool g_list_failed = false;
inline bool g_disabled = false;
inline bool g_complete = false;
inline int32_t g_cursor = 0;
inline int32_t g_pass = 0;
inline uint32_t g_pass_granted = 0u;
inline uint32_t g_total_granted = 0u;
inline uint32_t g_failures = 0u;
inline uint32_t g_idle_ticks = 0u;

inline constexpr int32_t kGrantsPerTick = 3;
inline constexpr int32_t kScansPerTick = 96;
inline constexpr int32_t kMaxPasses = 8;
inline constexpr uint32_t kMaxFailures = 32u;
inline constexpr uint32_t kRecheckTicks = 1800u;

template <typename Fn>
void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

bool resolve_call(const hook::ManagedMethod& target, void** out_fn,
                  const MethodInfo** out_mi, bool required = true) {
    void* info = il2cpp::find_method_info(target.namespaze, target.klass,
                                          target.method, target.args_count);
    void* pointer = il2cpp::method_pointer(info);
    if (info == nullptr || pointer == nullptr) {
        if (required) {
            LOGE("lobby-14.1.1: cannot resolve %s.%s/%d", target.klass,
                 target.method, target.args_count);
        }
        return false;
    }
    *out_fn = pointer;
    *out_mi = info;
    return true;
}

bool resolve_list_api(void* list) {
    if (g_list_ready) return true;
    if (g_list_failed || list == nullptr ||
        il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_method_from_name == nullptr) {
        return false;
    }

    void* klass = il2cpp::object_get_class(list);
    void* count_info = klass != nullptr
                           ? il2cpp::class_get_method_from_name(klass,
                                                               "get_Count", 0)
                           : nullptr;
    void* item_info = klass != nullptr
                          ? il2cpp::class_get_method_from_name(klass,
                                                              "get_Item", 1)
                          : nullptr;
    void* count_ptr = il2cpp::method_pointer(count_info);
    void* item_ptr = il2cpp::method_pointer(item_info);
    if (count_ptr == nullptr || item_ptr == nullptr) {
        g_list_failed = true;
        LOGE("lobby-14.1.1: List<LobbyItem> accessors unavailable (%d/%d)",
             count_ptr != nullptr ? 1 : 0, item_ptr != nullptr ? 1 : 0);
        return false;
    }

    g_list_count = reinterpret_cast<ListCountFn>(count_ptr);
    g_mi_list_count = count_info;
    g_list_item = reinterpret_cast<ListItemFn>(item_ptr);
    g_mi_list_item = item_info;
    g_list_ready = true;
    return true;
}

void finish_pass(int32_t count) {
    (void)count;
    ++g_pass;
    if (g_pass_granted > 0u && g_pass < kMaxPasses) {
        LOGI("lobby-14.1.1: pass %d granted %u/%d; verifying catalogue again",
             g_pass, g_pass_granted, count);
        g_cursor = 0;
        g_pass_granted = 0u;
        return;
    }
    g_complete = true;
    g_cursor = 0;
    g_pass = 0;
    g_pass_granted = 0u;
    g_idle_ticks = 0u;
    LOGI("lobby-14.1.1: catalogue complete (%d entries, %u newly granted)",
         count, g_total_granted);
}

void grant_batch(void* self) {
    void* list = g_all_items(self, g_mi_all_items);
    if (list == nullptr || !resolve_list_api(list)) return;

    const int32_t count = g_list_count(list, g_mi_list_count);
    if (count <= 0) return;
    if (g_cursor >= count) {
        finish_pass(count);
        return;
    }

    int32_t scanned = 0;
    int32_t granted = 0;
    while (g_cursor < count && scanned < kScansPerTick &&
           granted < kGrantsPerTick) {
        void* item = g_list_item(list, g_cursor, g_mi_list_item);
        ++g_cursor;
        ++scanned;
        if (item == nullptr || g_item_exists(item, g_mi_item_exists)) continue;

        // autoEquip=false prevents the bulk grant from replacing the currently
        // selected lobby item; AddItemNow still creates and saves real ownership.
        if (!g_add_item_now(self, item, false, g_mi_add_item_now)) {
            ++g_failures;
            if (g_failures >= kMaxFailures) {
                g_disabled = true;
                LOGE("lobby-14.1.1: %u grants were refused; module disabled "
                     "without further save changes", g_failures);
                return;
            }
            continue;
        }

        g_failures = 0u;
        ++granted;
        ++g_pass_granted;
        ++g_total_granted;
    }

    if (granted > 0 && g_save != nullptr) {
        g_save(self, g_mi_save);
    }
    if (g_cursor >= count) finish_pass(count);
}

void hook_update(void* self, const MethodInfo* method) {
    g_update(self, method);
    if (self == nullptr || g_disabled || !g_is_ready(self, g_mi_is_ready)) {
        return;
    }
    if (g_complete) {
        if (++g_idle_ticks < kRecheckTicks) return;
        g_idle_ticks = 0u;
        g_complete = false;
        g_cursor = 0;
        g_pass = 0;
        g_pass_granted = 0u;
    }
    grant_batch(self);
}

} // namespace detail

inline bool install_hooks() {
    bool ok = detail::resolve_call(
        {"Rilisoft", "LobbyItemsController", "get_IsReady", 0},
        reinterpret_cast<void**>(&detail::g_is_ready),
        &detail::g_mi_is_ready);
    ok &= detail::resolve_call(
        {"Rilisoft", "LobbyItemsController", "get_AllItems", 0},
        reinterpret_cast<void**>(&detail::g_all_items),
        &detail::g_mi_all_items);
    ok &= detail::resolve_call(
        {"Rilisoft", "LobbyItemsController", "AddItemNow", 2},
        reinterpret_cast<void**>(&detail::g_add_item_now),
        &detail::g_mi_add_item_now);
    ok &= detail::resolve_call(
        {"Rilisoft", "LobbyItem", "get_IsExists", 0},
        reinterpret_cast<void**>(&detail::g_item_exists),
        &detail::g_mi_item_exists);
    if (!ok) {
        LOGE("lobby-14.1.1: required catalogue API incomplete");
        return false;
    }

    if (!detail::resolve_call(
            {"Rilisoft", "LobbyItemsController", "SavePlayerCurrentData", 0},
            reinterpret_cast<void**>(&detail::g_save),
            &detail::g_mi_save, false)) {
        detail::g_save = nullptr;
        LOGW("lobby-14.1.1: explicit batch save unavailable; relying on "
             "AddItemNow's own save");
    }

    if (!hook::install(
            {"Rilisoft", "LobbyItemsController", "Update", 0},
            detail::replacement(&detail::hook_update),
            detail::original_slot(&detail::g_update), true)) {
        LOGE("lobby-14.1.1: controller Update hook failed");
        return false;
    }

    LOGI("lobby-14.1.1: catalogue grant armed "
         "(AddItemNow(item, autoEquip=false), stock ownership/save, "
         "%d grants per frame)", detail::kGrantsPerTick);
    return true;
}

} // namespace lobby_catalog_1411
