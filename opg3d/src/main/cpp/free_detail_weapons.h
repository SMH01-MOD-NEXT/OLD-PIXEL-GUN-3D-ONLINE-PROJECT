#pragma once

#include <atomic>
#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Compatibility for weapon blueprints that depended on retired backend
// progression. Stock inventory and persistence code still grants the item;
// this module only replaces the required detail count.
namespace free_detail_weapons {
namespace detail {

using MethodInfo = void;
using ManagedString = void;
using NumDetailsFn = int32_t (*)(void* static_context, ManagedString* item_id,
                                 const MethodInfo* method);
using GetItemCategoryFn = int32_t (*)(void* static_context,
                                      ManagedString* item_id,
                                      const MethodInfo* method);

inline NumDetailsFn g_num_details = nullptr;
inline GetItemCategoryFn g_get_item_category = nullptr;
inline const MethodInfo* g_mi_get_item_category = nullptr;
inline std::atomic<bool> g_first_override_logged{false};
inline std::atomic<bool> g_first_unmatched_logged{false};

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
    // ItemDb.GetItemCategory returns the armory presentation category. Detail
    // weapons are shown under the craft groups rather than their loadout slot.
    constexpr int32_t kWeaponCraftCategory = 110000;
    constexpr int32_t kEventCraftCategory = 135000;
    constexpr int32_t kSetsCraftCategory = 140000;
    return (category >= 0 && category <= 5) ||
           category == kWeaponCraftCategory ||
           category == kEventCraftCategory ||
           category == kSetsCraftCategory;
}

int32_t hook_num_details(void* static_context, ManagedString* item_id,
                         const MethodInfo* method) {
    const int32_t stock_required =
        g_num_details(static_context, item_id, method);
    if (stock_required <= 0 || item_id == nullptr) return stock_required;

    const int32_t category =
        g_get_item_category(nullptr, item_id, g_mi_get_item_category);
    if (!is_weapon_category(category)) {
        bool expected = false;
        if (g_first_unmatched_logged.compare_exchange_strong(expected, true)) {
            LOGI("free-details: preserving non-weapon detail requirement "
                 "(category=%d, required=%d)",
                 category, stock_required);
        }
        return stock_required;
    }

    bool expected = false;
    if (g_first_override_logged.compare_exchange_strong(expected, true)) {
        LOGI("free-details: detail weapons now require 0 details "
             "(first category=%d, stock requirement=%d)",
             category, stock_required);
    }
    return 0;
}

} // namespace detail

inline bool install_hooks() {
    const detail::MethodInfo* num_details_method = nullptr;
    bool ok = detail::resolve_call(
        {"", "BalanceController", "NumOfDetailsForCraft", 1},
        reinterpret_cast<void**>(&detail::g_num_details),
        &num_details_method);
    ok &= detail::resolve_call(
        {"", "ItemDb", "GetItemCategory", 1},
        reinterpret_cast<void**>(&detail::g_get_item_category),
        &detail::g_mi_get_item_category);
    if (!ok) {
        LOGE("free-details: required 13.2.1 methods are unavailable");
        return false;
    }

    const bool detail_count = hook::install(
        {"", "BalanceController", "NumOfDetailsForCraft", 1},
        detail::replacement(&detail::hook_num_details),
        detail::original_slot(&detail::g_num_details), true);
    if (!detail_count) {
        LOGE("free-details: detail-count hook failed");
        return false;
    }

    LOGI("free-details: zero-detail weapon crafting armed");
    return true;
}

} // namespace free_detail_weapons
