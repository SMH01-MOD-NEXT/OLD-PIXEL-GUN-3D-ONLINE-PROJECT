#pragma once

#include <atomic>
#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Compatibility for weapon blueprints that depended on retired backend
// progression. Stock inventory and persistence code still grants the item;
// only the detail requirement and craft duration are replaced.
namespace free_detail_weapons {
namespace detail {

using MethodInfo = void;
using ManagedString = void;
using IntByItemFn = int32_t (*)(void* static_context, ManagedString* item_id,
                                const MethodInfo* method);
using GetItemCategoryFn = int32_t (*)(void* static_context,
                                      ManagedString* item_id,
                                      const MethodInfo* method);

inline IntByItemFn g_num_details = nullptr;
inline const MethodInfo* g_mi_num_details = nullptr;
inline IntByItemFn g_full_craft_time = nullptr;
inline GetItemCategoryFn g_get_item_category = nullptr;
inline const MethodInfo* g_mi_get_item_category = nullptr;
inline std::atomic<bool> g_first_override_logged{false};

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
        LOGE("free-details: cannot resolve %s.%s/%d", target.klass,
             target.method, target.args_count);
        return false;
    }
    *out_fn = pointer;
    *out_mi = info;
    return true;
}

bool is_weapon_category(int32_t category) {
    // The supplied 13.2.1 metadata confirms that ItemDb.GetItemCategory
    // returns the six ordinary weapon slots as categories 0 through 5.
    return category >= 0 && category <= 5;
}

bool is_detail_weapon(ManagedString* item_id) {
    if (item_id == nullptr || g_num_details == nullptr ||
        g_get_item_category == nullptr) {
        return false;
    }
    const int32_t stock_required =
        g_num_details(nullptr, item_id, g_mi_num_details);
    if (stock_required <= 0) return false;
    const int32_t category =
        g_get_item_category(nullptr, item_id, g_mi_get_item_category);
    return is_weapon_category(category);
}

void log_first_override() {
    bool expected = false;
    if (g_first_override_logged.compare_exchange_strong(expected, true)) {
        LOGI("free-details: detail weapons now require 0 details and 0 craft "
             "seconds");
    }
}

int32_t hook_num_details(void* static_context, ManagedString* item_id,
                         const MethodInfo* method) {
    const int32_t stock_required =
        g_num_details(static_context, item_id, method);
    if (stock_required <= 0 || item_id == nullptr) return stock_required;

    const int32_t category =
        g_get_item_category(nullptr, item_id, g_mi_get_item_category);
    if (!is_weapon_category(category)) return stock_required;

    log_first_override();
    return 0;
}

int32_t hook_full_craft_time(void* static_context, ManagedString* item_id,
                             const MethodInfo* method) {
    if (is_detail_weapon(item_id)) {
        log_first_override();
        return 0;
    }
    return g_full_craft_time(static_context, item_id, method);
}

} // namespace detail

inline bool install_hooks() {
    bool ok = true;
    ok &= detail::resolve_call(
        {"", "BalanceController", "NumOfDetailsForCraft", 1},
        reinterpret_cast<void**>(&detail::g_num_details),
        &detail::g_mi_num_details);
    const detail::MethodInfo* full_time_method = nullptr;
    ok &= detail::resolve_call(
        {"", "BalanceController", "GetFullTimeCraftInSeconds", 1},
        reinterpret_cast<void**>(&detail::g_full_craft_time),
        &full_time_method);
    ok &= detail::resolve_call(
        {"", "ItemDb", "GetItemCategory", 1},
        reinterpret_cast<void**>(&detail::g_get_item_category),
        &detail::g_mi_get_item_category);
    if (!ok) {
        LOGE("free-details: required 13.2.1 methods are unavailable");
        return false;
    }

    const bool craft_time = hook::install(
        {"", "BalanceController", "GetFullTimeCraftInSeconds", 1},
        detail::replacement(&detail::hook_full_craft_time),
        detail::original_slot(&detail::g_full_craft_time), true);
    const bool detail_count = hook::install(
        {"", "BalanceController", "NumOfDetailsForCraft", 1},
        detail::replacement(&detail::hook_num_details),
        detail::original_slot(&detail::g_num_details), true);
    if (!craft_time || !detail_count) {
        LOGE("free-details: detail count or craft-time hook failed");
        return false;
    }

    LOGI("free-details: zero-detail, instant weapon crafting armed");
    return true;
}

} // namespace free_detail_weapons
