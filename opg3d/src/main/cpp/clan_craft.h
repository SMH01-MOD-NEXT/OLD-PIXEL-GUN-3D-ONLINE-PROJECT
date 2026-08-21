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
// blueprints, yet pressing Craft used to open the "Crafting Process" hint
// window instead of starting the craft. Clans were a purely server-side
// feature, so every clan requirement of this build is unreachable for good.
//
// What the device logs proved, step by step:
//
//   1. Forcing the craft-section enum works. The dump defines
//
//        enum CraftSectionAvailability { UnavailableClansNotOpened = 0,
//                                       UnavailableNoClan         = 1,
//                                       UnavailableNoDetails      = 2,
//                                       Available                 = 3 }
//
//      and the hook reports "UnavailableNoClan(1) -> Available(3)". The
//      hint window is built from that same value, which is why its
//      call-to-action changed from "Find a Clan" to "Raise the level":
//      CraftSectionClanInfoController picks its header and button label from
//      the enum, and both clan states point at something unreachable here.
//
//   2. The craft press is refused *before* that enum is consulted. The press
//      trace prints "stock section state <unknown>", meaning
//      GetCraftSectionAvailability had not been called even once at that
//      moment, and the first call right afterwards returns into
//      0x1ABD670..0x1ABD8F8, the body of CraftSectionClanInfoController
//      .Awake. In other words the enum is only read by the window the
//      handler had already decided to open.
//
//   3. Clan storage is never queried during the press either: no clan-storage
//      decision is logged between the press and the window.
//
// The only clan input the handler can consult that early is the item
// classification itself:
//
//   ClansController.IsClanItem(string id) -> bool     (static, RVA 0x1016364)
//
// A clan blueprint is therefore routed away from the clan branch: while a
// craft press is on the stack, that classification is answered with "not a
// clan item", so the handler takes the ordinary detail recipe, which already
// costs zero details and finishes without a timer. Outside the press the
// stock answer is returned untouched, which keeps item categories, the clan
// storage screens and the chest logic exactly as the game built them.
//
// Nothing here constructs a Clan instance, assigns ClansController.myClan,
// rewrites the "Clan.MyClanCache" entry on disk or calls any retired clan
// endpoint (SendUpdateStock, AskCraftFortItem, AddClanCurrency,
// SendClanMessageDetailsBought). That distinction is the whole point: a
// synthetic clan would leak into every clan screen, siege matchmaking, the
// chest/season logic and the analytics payloads, while a scoped predicate is
// only observable in the one code path that asks "may this player craft this
// item right now".
//
// Two clan-side inputs of the same craft path stay neutralized because they
// read from clan storage that no longer exists, and the medal price is waived
// for the same reason:
//
//   BalanceController.MedalsForClanCraft(string)          -> 0
//   ClansController.AnyPartExistsInStock(string, type)    -> true
//   ClansController.GetPartCountInStock(string, type)     -> synthetic if empty
//
// Only the availability gate is mandatory. Everything else installs when the
// metadata matches and degrades with a warning otherwise. The craft press is
// traced so a follow-up report shows which predicate the client consulted.
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
using IsClanItemFn = bool (*)(void* static_context, ManagedString* item_id,
                              const MethodInfo* method);
using CraftableFn = bool (*)(void* static_context, ManagedString* item_id,
                             const MethodInfo* method);
using ShowSectionInfoFn = void (*)(void* self, const MethodInfo* method);
using CraftPressFn = void (*)(void* self, ManagedString* item_id,
                              void* bank_panel, void* instant_window_parent,
                              const MethodInfo* method);

inline AvailabilityFn g_availability = nullptr;
inline SectionAvailableFn g_section_available = nullptr;
inline MedalsFn g_medals_for_clan_craft = nullptr;
inline PartCountFn g_part_count = nullptr;
inline AnyPartFn g_any_part = nullptr;
inline IsClanItemFn g_is_clan_item = nullptr;
inline CraftableFn g_craft_and_not_crafted = nullptr;
inline ShowSectionInfoFn g_show_section_info = nullptr;
inline CraftPressFn g_craft_press = nullptr;

// CraftSectionAvailability values, verified in the 13.2.1 dump.
inline constexpr int32_t kUnavailableClansNotOpened = 0;
inline constexpr int32_t kUnavailableNoClan = 1;
inline constexpr int32_t kUnavailableNoDetails = 2;
inline constexpr int32_t kAvailable = 3;
inline constexpr int32_t kUnknownAvailability = -1;

// Reported as "present in clan storage" when that storage is empty. Kept
// small on purpose: every recipe already costs zero details, so this value
// only has to be non-zero for boolean and ">= required" comparisons. If a
// clan screen ever displays it, it stays a plausible number instead of a
// nine-digit one.
inline constexpr int32_t kSyntheticStockParts = 99;

// Per-path logging is capped so a per-frame UI refresh cannot flood logcat.
inline constexpr uint32_t kMaxLoggedDecisions = 8;

// Last value the stock game computed for the craft section. The armory polls
// that getter while the screen is open, so this is a cheap, side-effect-free
// way for other hooks to know the real reason without synthesising a managed
// call of their own.
inline std::atomic<int32_t> g_last_stock_availability{kUnknownAvailability};

inline std::atomic<uint32_t> g_logged_availability{0u};
inline std::atomic<uint32_t> g_logged_section{0u};
inline std::atomic<uint32_t> g_logged_medals{0u};
inline std::atomic<uint32_t> g_logged_stock{0u};
inline std::atomic<uint32_t> g_logged_clan_item{0u};
inline std::atomic<uint32_t> g_logged_craftable{0u};
inline std::atomic<uint32_t> g_logged_info{0u};
inline std::atomic<uint32_t> g_logged_press{0u};

// Depth of craft-button handlers currently executing on this thread. Managed
// UI callbacks all run on the Unity main thread, and the counter is
// thread-local anyway, so a hook on another thread never sees a scope it does
// not belong to.
inline thread_local int32_t g_press_depth = 0;

class PressScope {
  public:
    PressScope() { ++g_press_depth; }
    ~PressScope() { --g_press_depth; }

    PressScope(const PressScope&) = delete;
    PressScope& operator=(const PressScope&) = delete;
};

bool inside_craft_press() { return g_press_depth > 0; }

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

// A clan reason is one this client can never satisfy again: clan membership
// and the clan unlock both lived on the retired backend.
bool is_clan_reason(int32_t availability) {
    return availability == kUnavailableNoClan ||
           availability == kUnavailableClansNotOpened;
}

int32_t hook_availability(void* static_context, const MethodInfo* method) {
    const int32_t stock_value = g_availability(static_context, method);
    g_last_stock_availability.store(stock_value, std::memory_order_relaxed);
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

// The item classification that routes a blueprint into the clan branch of the
// craft handler. Answering it honestly outside a craft press keeps every clan
// screen intact; answering "not a clan item" during the press sends the
// handler down the ordinary detail recipe, which this build can complete.
bool hook_is_clan_item(void* static_context, ManagedString* item_id,
                       const MethodInfo* method) {
    const bool stock_result = g_is_clan_item(static_context, item_id, method);
    if (!stock_result || !inside_craft_press()) return stock_result;

    if (should_log(g_logged_clan_item)) {
        LOGI("clan-craft: '%s' is a clan blueprint; answered 'regular craft "
             "item' for this press so the detail recipe is used instead of "
             "the clan branch",
             item_name(item_id).c_str());
    }
    return false;
}

// Diagnostics only: reports whether the handler still considers the item a
// craftable, not yet crafted recipe once the clan branch is out of the way.
bool hook_craft_and_not_crafted(void* static_context, ManagedString* item_id,
                                const MethodInfo* method) {
    const bool stock_result =
        g_craft_and_not_crafted(static_context, item_id, method);
    if (inside_craft_press() && should_log(g_logged_craftable)) {
        LOGI("clan-craft: craft recipe check for '%s' -> %s",
             item_name(item_id).c_str(), stock_result ? "craftable" : "no");
    }
    return stock_result;
}

// The hint window shows one requirement line plus one call-to-action per
// availability state. Both clan states point at something unreachable here,
// and their button opens the dead clan search, so the window is skipped for
// those states. It is also skipped for anything raised during a craft press:
// at that point the press has already been granted, so a requirement popup
// would only advertise a rule that no longer applies. The details variant
// outside a press stays useful and is forwarded untouched.
void hook_show_section_info(void* self, const MethodInfo* method) {
    const int32_t stock_value =
        g_last_stock_availability.load(std::memory_order_relaxed);

    if (inside_craft_press()) {
        if (should_log(g_logged_info)) {
            LOGW("clan-craft: craft press still opened the hint window (last "
                 "section state %s); window suppressed, the craft itself did "
                 "not start",
                 availability_name(stock_value));
        }
        return;
    }

    if (is_clan_reason(stock_value)) {
        if (should_log(g_logged_info)) {
            LOGI("clan-craft: suppressed craft hint window for %s(%d); that "
                 "requirement no longer blocks crafting",
                 availability_name(stock_value), stock_value);
        }
        return;
    }
    g_show_section_info(self, method);
}

// Marks the craft press for the hooks above and traces it, then forwards the
// call unchanged. The scope ends when the stock handler returns, so nothing
// outside the button press observes the relaxed clan classification.
void hook_craft_press(void* self, ManagedString* item_id, void* bank_panel,
                      void* instant_window_parent,
                      const MethodInfo* method) {
    PressScope scope;
    if (should_log(g_logged_press)) {
        LOGI("clan-craft: craft pressed for '%s' (stock section state %s)",
             item_name(item_id).c_str(),
             availability_name(
                 g_last_stock_availability.load(std::memory_order_relaxed)));
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

    // The item classification that decides whether the craft handler takes
    // the clan branch. This is the hook that actually starts the craft, so a
    // failure here is reported loudly even though the module keeps working.
    const bool clan_item = hook::install(
        {"", "ClansController", "IsClanItem", 1},
        detail::replacement(&detail::hook_is_clan_item),
        detail::original_slot(&detail::g_is_clan_item), false);
    if (!clan_item) {
        LOGE("clan-craft: ClansController.IsClanItem could not be hooked; the "
             "craft press will keep falling back to the clan branch");
    }

    // Optional, diagnostics only.
    const bool craftable = hook::install(
        {"", "ShopCraftManager", "CraftItemAndNotCrafted", 1},
        detail::replacement(&detail::hook_craft_and_not_crafted),
        detail::original_slot(&detail::g_craft_and_not_crafted), false);
    if (!craftable) {
        LOGW("clan-craft: craft-recipe tracing is unavailable");
    }

    // Optional: the hint window that would keep advertising a clan or level
    // requirement which no longer blocks anything.
    const bool info_window = hook::install(
        {"", "ShopNGUIController", "ShowCraftSectionInfo", 0},
        detail::replacement(&detail::hook_show_section_info),
        detail::original_slot(&detail::g_show_section_info), false);
    if (!info_window) {
        LOGW("clan-craft: ShopNGUIController.ShowCraftSectionInfo is "
             "unavailable; the clan hint window may still be shown");
    }

    // Required for the scoped classification above: without this hook there
    // is no craft press to scope to.
    const bool craft_press = hook::install(
        {"", "ShopNGUIController", "HandleCraftButton_NoInfo", 3},
        detail::replacement(&detail::hook_craft_press),
        detail::original_slot(&detail::g_craft_press), false);
    if (!craft_press) {
        LOGE("clan-craft: ShopNGUIController.HandleCraftButton_NoInfo could "
             "not be hooked; the scoped clan-item workaround stays inactive");
    }

    LOGI("clan-craft: armed (section=Available, shop shortcut=%s, medals=%s, "
         "clan storage=%s, clan item during press=%s, clan hint window=%s, "
         "press scope=%s, recipe trace=%s); no clan object is created",
         section_available ? "forced" : "stock", medals ? "free" : "stock",
         (any_part && part_count) ? "synthetic" : "partial",
         clan_item ? "regular" : "stock",
         info_window ? "suppressed" : "stock",
         craft_press ? "on" : "off", craftable ? "on" : "off");
    return true;
}

} // namespace clan_craft
