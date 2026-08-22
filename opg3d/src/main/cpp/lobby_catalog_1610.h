#pragma once

#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Grants the complete 16.1.0 lobby catalogue through the stock ownership path.
// AddItemNow grew from two managed arguments to five. A stock 16.1.0 call site
// passes a null price descriptor, proving it is valid; the final true flag is
// retained so AddItemNow performs its normal local persistence.
namespace lobby_catalog_1610 {
namespace detail {

using MethodInfo = void;
using UpdateFn = void (*)(void*, const MethodInfo*);
using BoolInstanceFn = bool (*)(void*, const MethodInfo*);
using ObjectInstanceFn = void* (*)(void*, const MethodInfo*);
using AddItemFn = bool (*)(void*, void*, bool, bool, void*, bool,
                           const MethodInfo*);
using ListCountFn = int32_t (*)(void*, const MethodInfo*);
using ListItemFn = void* (*)(void*, int32_t, const MethodInfo*);

inline UpdateFn g_update = nullptr;
inline BoolInstanceFn g_is_ready = nullptr;
inline ObjectInstanceFn g_all_items = nullptr;
inline AddItemFn g_add_item = nullptr;
inline BoolInstanceFn g_item_exists = nullptr;
inline ListCountFn g_list_count = nullptr;
inline ListItemFn g_list_item = nullptr;
inline const MethodInfo* g_mi_is_ready = nullptr;
inline const MethodInfo* g_mi_all_items = nullptr;
inline const MethodInfo* g_mi_add_item = nullptr;
inline const MethodInfo* g_mi_item_exists = nullptr;
inline const MethodInfo* g_mi_list_count = nullptr;
inline const MethodInfo* g_mi_list_item = nullptr;

inline bool g_list_ready = false;
inline bool g_list_failed = false;
inline bool g_disabled = false;
inline bool g_complete = false;
inline int32_t g_cursor = 0;
inline int32_t g_pass = 0;
inline uint32_t g_pass_granted = 0;
inline uint32_t g_total_granted = 0;
inline uint32_t g_failures = 0;
inline uint32_t g_idle_ticks = 0;

inline constexpr int32_t kGrantsPerTick = 3;
inline constexpr int32_t kScansPerTick = 96;
inline constexpr int32_t kMaxPasses = 8;
inline constexpr uint32_t kMaxFailures = 32;
inline constexpr uint32_t kRecheckTicks = 1800;

template <typename Fn>
void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

bool resolve_call(const hook::ManagedMethod& target, void** out,
                  const MethodInfo** out_mi) {
    void* info = il2cpp::find_method_info(target.namespaze, target.klass,
                                          target.method, target.args_count);
    void* pointer = il2cpp::method_pointer(info);
    if (info == nullptr || pointer == nullptr) {
        LOGE("lobby-16.1.0: cannot resolve %s.%s/%d", target.klass,
             target.method, target.args_count);
        return false;
    }
    *out = pointer;
    *out_mi = info;
    return true;
}

bool resolve_list_api(void* list) {
    if (g_list_ready) return true;
    if (g_list_failed || list == nullptr || il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_method_from_name == nullptr) {
        return false;
    }
    void* klass = il2cpp::object_get_class(list);
    void* count_info = klass == nullptr
                           ? nullptr
                           : il2cpp::class_get_method_from_name(klass,
                                                               "get_Count", 0);
    void* item_info = klass == nullptr
                          ? nullptr
                          : il2cpp::class_get_method_from_name(klass,
                                                              "get_Item", 1);
    void* count_pointer = il2cpp::method_pointer(count_info);
    void* item_pointer = il2cpp::method_pointer(item_info);
    if (count_pointer == nullptr || item_pointer == nullptr) {
        g_list_failed = true;
        LOGE("lobby-16.1.0: List<LobbyItem> accessors unavailable");
        return false;
    }
    g_list_count = reinterpret_cast<ListCountFn>(count_pointer);
    g_list_item = reinterpret_cast<ListItemFn>(item_pointer);
    g_mi_list_count = count_info;
    g_mi_list_item = item_info;
    g_list_ready = true;
    return true;
}

void finish_pass(int32_t count) {
    (void)count;
    ++g_pass;
    if (g_pass_granted > 0 && g_pass < kMaxPasses) {
        LOGI("lobby-16.1.0: pass %d granted %u/%d; verifying again", g_pass,
             g_pass_granted, count);
        g_cursor = 0;
        g_pass_granted = 0;
        return;
    }
    g_complete = true;
    g_cursor = 0;
    g_pass = 0;
    g_pass_granted = 0;
    g_idle_ticks = 0;
    LOGI("lobby-16.1.0: catalogue complete (%d entries, %u newly granted)",
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
        void* item = g_list_item(list, g_cursor++, g_mi_list_item);
        ++scanned;
        if (item == nullptr || g_item_exists(item, g_mi_item_exists)) continue;

        // 16.1.0 stock calls use: item, autoEquip, normalGrant,
        // optionalPriceDescriptor, save. Keep the persistent stock path and
        // only disable auto-equip during the bulk grant.
        if (!g_add_item(self, item, false, true, nullptr, true,
                        g_mi_add_item)) {
            if (++g_failures >= kMaxFailures) {
                g_disabled = true;
                LOGE("lobby-16.1.0: %u grants refused; module disabled",
                     g_failures);
                return;
            }
            continue;
        }
        g_failures = 0;
        ++granted;
        ++g_pass_granted;
        ++g_total_granted;
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
        g_complete = false;
        g_idle_ticks = 0;
    }
    grant_batch(self);
}

} // namespace detail

inline bool install_hooks() {
    bool ok = detail::resolve_call(
        {"Rilisoft", "LobbyItemsController", u8"丞丈丕丑丑丟丂丗丛", 0},
        reinterpret_cast<void**>(&detail::g_is_ready),
        &detail::g_mi_is_ready);
    ok &= detail::resolve_call(
        {"Rilisoft", "LobbyItemsController", u8"丑下万丄世且丂丑丈", 0},
        reinterpret_cast<void**>(&detail::g_all_items),
        &detail::g_mi_all_items);
    ok &= detail::resolve_call(
        {"Rilisoft", "LobbyItemsController", u8"下丗专丟三丅丗东丂", 5},
        reinterpret_cast<void**>(&detail::g_add_item),
        &detail::g_mi_add_item);
    ok &= detail::resolve_call(
        {"Rilisoft", u8"丗业丑世丞世丑万专", u8"丌下下丛丘丝丕东丗", 0},
        reinterpret_cast<void**>(&detail::g_item_exists),
        &detail::g_mi_item_exists);
    if (!ok) {
        LOGE("lobby-16.1.0: required catalogue API incomplete");
        return false;
    }
    if (!hook::install(
            {"Rilisoft", "LobbyItemsController", "Update", 0},
            detail::replacement(&detail::hook_update),
            detail::original_slot(&detail::g_update), true)) {
        LOGE("lobby-16.1.0: Update hook failed");
        return false;
    }
    LOGI("lobby-16.1.0: complete local catalogue grant armed (%d/frame)",
         detail::kGrantsPerTick);
    return true;
}

} // namespace lobby_catalog_1610
