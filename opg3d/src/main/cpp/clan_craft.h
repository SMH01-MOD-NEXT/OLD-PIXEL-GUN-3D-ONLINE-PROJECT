#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Clan blueprints on a client without a backend.
//
// Detail prices are already zero for every recipe (see
// free_detail_weapons.h) and the armory correctly shows "0 of 0" for clan
// blueprints, yet the craft press is refused: the craft section itself
// reports that the player is not in a clan. Clans were a purely server-side
// feature, so on this build that state can never become true again.
//
// The important detail from the supplied 13.2.1 dump is that the gate is an
// enum predicate, not a clan object:
//
//   enum CraftSectionAvailability { UnavailableClansNotOpened = 0,
//                                  UnavailableNoClan         = 1,
//                                  UnavailableNoDetails      = 2,
//                                  Available                 = 3 }
//
//   ShopCraftManager.GetCraftSectionAvailability() -> CraftSectionAvailability
//   ShopNGUIController.IsCraftSectionAvailable()   -> bool
//
// Both are read-only, so the workaround answers them instead of inventing
// membership. Nothing here constructs a Clan instance, assigns
// ClansController.myClan, rewrites the "Clan.MyClanCache" entry on disk or
// calls any retired clan endpoint (SendUpdateStock, AskCraftFortItem,
// AddClanCurrency, SendClanMessageDetailsBought). That distinction is the
// whole point: a synthetic clan would leak into every clan screen, siege
// matchmaking, the chest/season logic and the analytics payloads, while a
// forced predicate is only observable where the client asks "may this player
// craft right now".
//
// Three more clan-side inputs of the same craft path are neutralized because
// they read from clan storage that no longer exists:
//
//   BalanceController.MedalsForClanCraft(string)          -> 0
//   ClansController.AnyPartExistsInStock(string, type)    -> true
//   ClansController.GetPartCountInStock(string, type)     -> synthetic if empty
//
// Only the availability gate is mandatory. Everything else installs when the
// metadata matches and degrades with a warning otherwise. The craft press is
// traced once per item so a follow-up report shows exactly which predicate
// the client consulted.
namespace clan_craft {
namespace detail {

using MethodInfo = void;
using ManagedString = void;

// Old-IL2CPP ARM32 static ABI: r0 is a hidden null context, the first managed
// argument starts in r1, and MethodInfo* follows all managed arguments.
// Instance methods take the object in r0 instead of that hidden context.
using AvailabilityFn = int32_t (*)(void* static_context,
                                   const MethodInfo* method);
using SectionAvailableFn = bool (*)(void* static_context,
                                    const MethodInfo* method);
using MedalsFn = int32_t (*)(void* static_context, ManagedString* item_id,
                             const MethodInfo* method);
using PartCountFn = int32_t (*)(void* static_context, ManagedString* item_id,
                                int32_t item_type, const MethodInfo* method);
using AnyPartFn = bool (*)(void* static_context, ManagedString* item_id,
                           int32_t item_type, const MethodInfo* method);
using CraftPressFn = void (*)(void* self, ManagedString* item_id,
                              void* bank_panel, void* instant_window_parent,
                              const MethodInfo* method);

inline AvailabilityFn g_availability = nullptr;
inline SectionAvailableFn g_section_available = nullptr;
inline MedalsFn g_medals_for_clan_craft = nullptr;
inline PartCountFn g_part_count = nullptr;
inline AnyPartFn g_any_part = nullptr;
inline CraftPressFn g_craft_press = nullptr;

// CraftSectionAvailability values, verified in the 13.2.1 dump.
inline constexpr int32_t kUnavailableClansNotOpened = 0;
inline constexpr int32_t kUnavailableNoClan = 1;
inline constexpr int32_t kUnavailableNoDetails = 2;
inline constexpr int32_t kAvailable = 3;

// Reported as "present in clan storage" when that storage is empty. Kept
// small on purpose: every recipe already costs zero details, so this value
// only has to be non-zero for boolean and ">= required" comparisons. If a
// clan screen ever displays it, it stays a plausible number instead of a
// nine-digit one.
inline constexpr int32_t kSyntheticStockParts = 99;

// Per-path logging is capped so a per-frame UI refresh cannot flood logcat.
inline constexpr uint32_t kMaxLoggedDecisions = 8;

inline std::atomic<uint32_t> g_logged_availability{0u};
inline std::atomic<uint32_t> g_logged_section{0u};
inline std::atomic<uint32_t> g_logged_medals{0u};
inline std::atomic<uint32_t> g_logged_stock{0u};
inline std::atomic<uint32_t> g_logged_press{0u};

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

const char* availability_name(int32_t value) {
    switch (value) {
        case kUnavailableClansNotOpened: return "UnavailableClansNotOpened";
        case kUnavailableNoClan:         return "UnavailableNoClan";
        case kUnavailableNoDetails:      return "UnavailableNoDetails";
        case kAvailable:                 return "Available";
        default:                         return "<unknown>";
    }
}

int32_t hook_availability(void* static_context, const MethodInfo* method) {
    const int32_t stock_value = g_availability(static_context, method);
    if (stock_value == kAvailable) return stock_value;

    if (should_log(g_logged_availability)) {
        LOGI("clan-craft: craft section reported %s(%d) -> Available(%d)",
             availability_name(stock_value), stock_value, kAvailable);
    }
    return kAvailable;
}

bool hook_section_available(void* static_context, const MethodInfo* method) {
    const bool stock_result = g_section_available(static_context, method);
    if (!stock_result && should_log(g_logged_section)) {
        LOGI("clan-craft: craft section reported unavailable -> allowed");
    }
    return true;
}

int32_t hook_medals_for_clan_craft(void* static_context,
                                   ManagedString* item_id,
                                   const MethodInfo* method) {
    const int32_t stock_price = g_medals_for_clan_craft(static_context, item_id,
                                                       method);
    if (stock_price <= 0) return stock_price;

    if (should_log(g_logged_medals)) {
        LOGI("clan-craft: medal price for '%s': %d -> 0",
             item_name(item_id).c_str(), stock_price);
    }
    return 0;
}

int32_t hook_part_count(void* static_context, ManagedString* item_id,
                        int32_t item_type, const MethodInfo* method) {
    const int32_t stock_count = g_part_count(static_context, item_id, item_type,
                                             method);
    if (stock_count > 0) return stock_count;

    if (should_log(g_logged_stock)) {
        LOGI("clan-craft: clan storage for '%s' (type %d) is empty: %d -> %d",
             item_name(item_id).c_str(), item_type, stock_count,
             kSyntheticStockParts);
    }
    return kSyntheticStockParts;
}

bool hook_any_part(void* static_context, ManagedString* item_id,
                   int32_t item_type, const MethodInfo* method) {
    const bool stock_result = g_any_part(static_context, item_id, item_type,
                                        method);
    return stock_result ? stock_result : true;
}

// Diagnostics only: the craft press is forwarded unchanged. It exists so a
// logcat capture shows whether the button handler is reached at all and which
// item id it carries, which is the one piece of information the previous
// reports were missing.
void hook_craft_press(void* self, ManagedString* item_id, void* bank_panel,
                      void* instant_window_parent,
                      const MethodInfo* method) {
    if (should_log(g_logged_press)) {
        LOGI("clan-craft: craft pressed for '%s'", item_name(item_id).c_str());
    }
    g_craft_press(self, item_id, bank_panel, instant_window_parent, method);
}

} // namespace detail

inline bool install_hooks() {
    // Mandatory: the enum the whole craft section is driven by. Without it
    // clan blueprints stay locked no matter what the prices say.
    const bool availability = hook::install(
        {"", "ShopCraftManager", "GetCraftSectionAvailability", 0},
        detail::replacement(&detail::hook_availability),
        detail::original_slot(&detail::g_availability), true);
    if (!availability) {
        LOGE("clan-craft: ShopCraftManager.GetCraftSectionAvailability could "
             "not be hooked; clan blueprints stay locked");
        return false;
    }

    // Optional: the boolean shortcut the shop UI uses for the same question.
    const bool section_available = hook::install(
        {"", "ShopNGUIController", "IsCraftSectionAvailable", 0},
        detail::replacement(&detail::hook_section_available),
        detail::original_slot(&detail::g_section_available), false);
    if (!section_available) {
        LOGW("clan-craft: ShopNGUIController.IsCraftSectionAvailable is "
             "unavailable; relying on the availability enum only");
    }

    // Optional: clan medals were earned server-side and can no longer be
    // farmed, so the medal part of a clan recipe is free as well.
    const bool medals = hook::install(
        {"", "BalanceController", "MedalsForClanCraft", 1},
        detail::replacement(&detail::hook_medals_for_clan_craft),
        detail::original_slot(&detail::g_medals_for_clan_craft), false);
    if (!medals) {
        LOGW("clan-craft: BalanceController.MedalsForClanCraft is "
             "unavailable; a medal price may still be requested");
    }

    // Optional: clan storage is server state. Both accessors are read-only,
    // so answering them never writes anything back into the clan cache.
    const bool any_part = hook::install(
        {"", "ClansController", "AnyPartExistsInStock", 2},
        detail::replacement(&detail::hook_any_part),
        detail::original_slot(&detail::g_any_part), false);
    const bool part_count = hook::install(
        {"", "ClansController", "GetPartCountInStock", 2},
        detail::replacement(&detail::hook_part_count),
        detail::original_slot(&detail::g_part_count), false);
    if (!any_part || !part_count) {
        LOGW("clan-craft: clan storage accessors partially unavailable "
             "(anyPart=%d, partCount=%d)", any_part ? 1 : 0,
             part_count ? 1 : 0);
    }

    // Optional, diagnostics only.
    const bool craft_press = hook::install(
        {"", "ShopNGUIController", "HandleCraftButton_NoInfo", 3},
        detail::replacement(&detail::hook_craft_press),
        detail::original_slot(&detail::g_craft_press), false);
    if (!craft_press) {
        LOGW("clan-craft: craft-press tracing is unavailable");
    }

    LOGI("clan-craft: armed (section=Available, shop shortcut=%s, medals=%s, "
         "clan storage=%s, tracing=%s); no clan object is created",
         section_available ? "forced" : "stock", medals ? "free" : "stock",
         (any_part && part_count) ? "synthetic" : "partial",
         craft_press ? "on" : "off");
    return true;
}

} // namespace clan_craft
