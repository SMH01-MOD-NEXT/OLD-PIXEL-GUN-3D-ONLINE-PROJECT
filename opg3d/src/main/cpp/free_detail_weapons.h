#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Compatibility for weapon blueprints that depended on retired backend
// progression.
//
// Approach v3. The two previous attempts rewrote a single balance value
// (BalanceController.NumOfDetailsForCraft) and additionally filtered items by
// their armory category. Both assumptions were wrong for this client:
//
//   * The craft flow does not derive "can craft" from that balance value
//     alone; CraftSetsManager asks its own helper whether the player owns
//     enough details, and the armory UI reads the owned count separately.
//   * Category matching was pure guesswork and silently disabled the
//     override for the items the player actually opens.
//
// This version drops every category guess and neutralizes all three detail
// inputs of the stock craft flow, verified in the supplied 13.2.1 dump:
//
//   1. BalanceController.NumOfDetailsForCraft(string)            -> 0
//   2. CraftSetsManager.IsEnoughDetailsForCraftItem(string, str) -> true
//   3. WeaponCraftDetailsInfo.GetDetailsCount(string)            -> large
//
// Only (1) is mandatory. (2) and (3) install when the class exists, so a
// metadata mismatch degrades instead of failing the whole library, and each
// path logs the first time it is used. If crafting is still refused, logcat
// now shows exactly which of the three paths the game consulted.
//
// Nothing here fabricates a server response: the stock craft button,
// inventory provisioning, persistence and UI refresh still grant the weapon.
namespace free_detail_weapons {
namespace detail {

using MethodInfo = void;
using ManagedString = void;

// Old-IL2CPP ARM32 static ABI: r0 is a hidden null context, the first managed
// argument starts in r1, and MethodInfo* follows all managed arguments.
using NumDetailsFn = int32_t (*)(void* static_context, ManagedString* item_id,
                                 const MethodInfo* method);
using IsEnoughDetailsFn = bool (*)(void* static_context,
                                   ManagedString* item_id,
                                   ManagedString* balance_item_id,
                                   const MethodInfo* method);
using DetailsCountFn = int32_t (*)(void* static_context, ManagedString* tag,
                                   const MethodInfo* method);

inline NumDetailsFn g_num_details = nullptr;
inline IsEnoughDetailsFn g_is_enough_details = nullptr;
inline DetailsCountFn g_details_count = nullptr;

// Reported as owned for every detail tag. Large enough for any stock recipe,
// small enough to stay far away from int32 overflow in UI arithmetic.
inline constexpr int32_t kSyntheticOwnedDetails = 99999;

// Per-path logging is capped so a per-frame UI refresh cannot flood logcat.
inline constexpr uint32_t kMaxLoggedDecisions = 12;

inline std::atomic<uint32_t> g_logged_required{0u};
inline std::atomic<uint32_t> g_logged_gate{0u};
inline std::atomic<uint32_t> g_logged_owned{0u};

template <typename Fn>
void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

bool should_log(std::atomic<uint32_t>& counter) {
    return counter.fetch_add(1u, std::memory_order_relaxed) <
           kMaxLoggedDecisions;
}

std::string item_name(ManagedString* item_id) {
    if (item_id == nullptr) return std::string("<null>");
    return il2cpp::to_utf8(item_id, 64);
}

int32_t hook_num_details(void* static_context, ManagedString* item_id,
                         const MethodInfo* method) {
    const int32_t stock_required =
        g_num_details(static_context, item_id, method);
    if (stock_required <= 0) return stock_required;

    if (should_log(g_logged_required)) {
        LOGI("free-details: required details for '%s': %d -> 0",
             item_name(item_id).c_str(), stock_required);
    }
    return 0;
}

bool hook_is_enough_details(void* static_context, ManagedString* item_id,
                            ManagedString* balance_item_id,
                            const MethodInfo* method) {
    const bool stock_result = g_is_enough_details(static_context, item_id,
                                                 balance_item_id, method);
    if (!stock_result && should_log(g_logged_gate)) {
        LOGI("free-details: craft gate for '%s' (balance id '%s') reported "
             "not enough details -> allowed",
             item_name(item_id).c_str(), item_name(balance_item_id).c_str());
    }
    return true;
}

int32_t hook_details_count(void* static_context, ManagedString* tag,
                           const MethodInfo* method) {
    const int32_t stock_owned = g_details_count(static_context, tag, method);
    if (stock_owned >= kSyntheticOwnedDetails) return stock_owned;

    if (should_log(g_logged_owned)) {
        LOGI("free-details: owned count for detail '%s': %d -> %d",
             item_name(tag).c_str(), stock_owned, kSyntheticOwnedDetails);
    }
    return kSyntheticOwnedDetails;
}

} // namespace detail

inline bool install_hooks() {
    // Mandatory: the balance value every craft screen starts from.
    const bool required_details = hook::install(
        {"", "BalanceController", "NumOfDetailsForCraft", 1},
        detail::replacement(&detail::hook_num_details),
        detail::original_slot(&detail::g_num_details), true);
    if (!required_details) {
        LOGE("free-details: BalanceController.NumOfDetailsForCraft could not "
             "be hooked; detail weapons stay locked");
        return false;
    }

    // Optional: the decision the craft button itself makes.
    const bool craft_gate = hook::install(
        {"", "CraftSetsManager", "IsEnoughDetailsForCraftItem", 2},
        detail::replacement(&detail::hook_is_enough_details),
        detail::original_slot(&detail::g_is_enough_details), false);
    if (!craft_gate) {
        LOGW("free-details: CraftSetsManager.IsEnoughDetailsForCraftItem is "
             "unavailable; relying on the balance override only");
    }

    // Optional: the owned-detail count shown and compared by the armory UI.
    const bool owned_count = hook::install(
        {"", "WeaponCraftDetailsInfo", "GetDetailsCount", 1},
        detail::replacement(&detail::hook_details_count),
        detail::original_slot(&detail::g_details_count), false);
    if (!owned_count) {
        LOGW("free-details: WeaponCraftDetailsInfo.GetDetailsCount is "
             "unavailable; the armory may still show a shortage");
    }

    LOGI("free-details: armed (required=0 for every recipe, craft gate=%s, "
         "owned count=%s); no category filtering",
         craft_gate ? "forced" : "stock", owned_count ? "synthetic" : "stock");
    return true;
}

} // namespace free_detail_weapons
