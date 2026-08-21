#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Clan blueprints on a client without a backend.
//
// Detail prices are already zero for every recipe (see free_detail_weapons.h)
// and the armory correctly shows "0 of 0" for a clan blueprint, yet pressing
// Craft never started a craft. Two earlier hypotheses were tested on device
// and both were disproved:
//
//   * Forcing CraftSectionAvailability to Available only changed the hint
//     window's own call-to-action from "Find a Clan" to "Raise the level".
//     The enum is read by that window, not by the press.
//   * Scoping ClansController.IsClanItem(string) to the craft press had no
//     effect whatsoever: the armory v6 log shows the hook installed and then
//     never called during a press, and the same is true for
//     ShopCraftManager.CraftItemAndNotCrafted(string).
//
// Static disassembly settled the question. The libil2cpp.so of this build is
// ARM (A32) code, so plain BL scanning resolves the call graph exactly:
//
//   ArmoryInfoScreenController.HandleCraftButtonClicked  (RVA 0xC92A5C)
//     +0xDC  -> ShopNGUIController.HandleCraftButton_NoInfo (RVA 0x9F1740)
//              (the only caller; the logged press PC is +0xC92B3C)
//
// and that handler is the only caller of the craft start itself:
//
//   WeaponManager.StartCraftWeaponOrAvatar(string, long)  (RVA 0x962FB8)
//     called once, from HandleCraftButton_NoInfo +0x8FC
//
// The refusal happens long before any clan state is consulted. At +0x1A0 the
// handler asks a list of ordinary craft recipes whether it contains the
// pressed id:
//
//   0x9F18E0  bl   List<string>.Contains(id)
//   0x9F18E4  cmp  r0, #1
//   0x9F18E8  bne  0x9F1A7C          ; returns without ever reaching +0x8FC
//
// Clan blueprints are absent from that list: their recipes are parsed
// separately, by BalanceController.ParseClanRecipesConfig, so the ordinary
// recipe table this branch consults has no entry for them. This is why every
// clan-side override was ineffective — the branch that refuses the press is
// not about clans at all, and no clan answer can influence it.
//
// So this module stops arguing with the handler. When the stock handler
// refuses a press, the craft is started directly through the game's own
// methods, reproducing the exact sequence of the handler's own success path
// (+0x810 .. +0x8FC), including where each argument comes from:
//
//   ItemDb.GetByTag(id)                                  -> ItemRecord
//   ItemRecord.PrefabName                                -> string prefab
//   BalanceController.GetFullTimeCraftInSeconds(prefab)   -> int seconds
//   FriendsController.ServerTime + seconds                -> long endTime
//   WeaponManager.StartCraftWeaponOrAvatar(id, endTime)
//
// Note that the stock code keys the craft duration by prefab name while the
// craft itself is keyed by item tag; both are reproduced faithfully instead of
// passing the same string twice. Persistence, the craft timer, the finish
// handler and the reward path all stay inside the game's own code, and the
// caller refreshes the armory by calling ArmoryInfoScreenController.SetItem
// immediately after the handler returns, so no UI is driven from here either.
//
// Three guards keep the forced start honest:
//
//   1. It runs only for the outermost craft press, and only when the stock
//      handler actually refused it — observed as the hint window being raised
//      inside the press scope, which is the refusal signature of this branch.
//      A press the stock handler completes on its own is never touched.
//   2. It runs only when the craft slot is empty. The handler's own "already
//      crafting" check lives at +0x494, far behind the recipe-list exit, so a
//      clan blueprint pressed while another weapon is crafting would never
//      reach it. WeaponManager.CurrentCraftingWeaponOrAvatar is read first,
//      and a busy slot means the press is declined instead of overwriting a
//      craft that is already running.
//   3. It verifies the slot afterwards and reports an error if the game did
//      not accept the craft, rather than logging success unconditionally.
//
// Nothing here constructs a Clan instance, assigns ClansController.myClan,
// rewrites the "Clan.MyClanCache" entry on disk or calls any retired clan
// endpoint (SendUpdateStock, AskCraftFortItem, AddClanCurrency,
// SendClanMessageDetailsBought). A synthetic clan would leak into every clan
// screen, siege matchmaking, chest and season logic, and analytics payload,
// which is exactly how a dead-backend client crashes.
//
// The clan-side answers that remain are the ones that keep the armory screen
// itself coherent, because they read state that no longer exists:
//
//   ShopCraftManager.GetCraftSectionAvailability()        -> Available
//   ShopNGUIController.IsCraftSectionAvailable()          -> true
//   BalanceController.MedalsForClanCraft(string)          -> 0
//   ClansController.AnyPartExistsInStock(string, type)    -> true
//   ClansController.GetPartCountInStock(string, type)     -> synthetic if empty
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
using ShowSectionInfoFn = void (*)(void* self, const MethodInfo* method);
using CraftPressFn = void (*)(void* self, ManagedString* item_id,
                              void* bank_panel, void* instant_window_parent,
                              const MethodInfo* method);

// Stock calls used by the forced craft start. Signatures are taken from the
// handler's own call sites, not guessed.
using ItemByTagFn = void* (*)(void* static_context, ManagedString* tag,
                              const MethodInfo* method);
using PrefabNameFn = ManagedString* (*)(void* self, const MethodInfo* method);
using CraftSecondsFn = int32_t (*)(void* static_context,
                                   ManagedString* prefab_name,
                                   const MethodInfo* method);
using ServerTimeFn = int64_t (*)(void* static_context,
                                 const MethodInfo* method);
using StartCraftFn = void (*)(void* static_context, ManagedString* tag,
                              int64_t end_time, const MethodInfo* method);
// KeyValuePair<string, long> is returned through a hidden result pointer in
// r0, which pushes the null static context to r1 and MethodInfo* to r2. This
// is how the handler itself calls the getter at +0x494.
using CurrentCraftFn = void (*)(void* result, void* static_context,
                                const MethodInfo* method);

inline AvailabilityFn g_availability = nullptr;
inline SectionAvailableFn g_section_available = nullptr;
inline MedalsFn g_medals_for_clan_craft = nullptr;
inline PartCountFn g_part_count = nullptr;
inline AnyPartFn g_any_part = nullptr;
inline ShowSectionInfoFn g_show_section_info = nullptr;
inline CraftPressFn g_craft_press = nullptr;

inline ItemByTagFn g_item_by_tag = nullptr;
inline const MethodInfo* g_mi_item_by_tag = nullptr;
inline PrefabNameFn g_prefab_name = nullptr;
inline const MethodInfo* g_mi_prefab_name = nullptr;
inline CraftSecondsFn g_craft_seconds = nullptr;
inline const MethodInfo* g_mi_craft_seconds = nullptr;
inline ServerTimeFn g_server_time = nullptr;
inline const MethodInfo* g_mi_server_time = nullptr;
inline StartCraftFn g_start_craft = nullptr;
inline const MethodInfo* g_mi_start_craft = nullptr;
inline CurrentCraftFn g_current_craft = nullptr;
inline const MethodInfo* g_mi_current_craft = nullptr;
inline bool g_direct_start_ready = false;

// CraftSectionAvailability values, verified in the 13.2.1 dump.
inline constexpr int32_t kUnavailableClansNotOpened = 0;
inline constexpr int32_t kUnavailableNoClan = 1;
inline constexpr int32_t kUnavailableNoDetails = 2;
inline constexpr int32_t kAvailable = 3;
inline constexpr int32_t kUnknownAvailability = -1;

// Reported as "present in clan storage" when that storage is empty. Kept
// small on purpose: every recipe already costs zero details, so this value
// only has to be non-zero for boolean and ">= required" comparisons.
inline constexpr int32_t kSyntheticStockParts = 99;

// Per-path logging is capped so a per-frame UI refresh cannot flood logcat.
// The forced start is rarer and more interesting, so it gets its own budget.
inline constexpr uint32_t kMaxLoggedDecisions = 8;
inline constexpr uint32_t kMaxLoggedStarts = 24;

// KeyValuePair<string, long> is 16 bytes in this build (4-byte reference, then
// the 8-byte value at offset 8). The buffer is oversized and 8-byte aligned so
// the managed getter can never write past it.
inline constexpr size_t kCraftSlotBufferSize = 32;
inline constexpr size_t kCraftSlotKeyOffset = 0;

// Last value the stock game computed for the craft section. The armory polls
// that getter while the screen is open, so this is a cheap, side-effect-free
// way for other hooks to know the real reason.
inline std::atomic<int32_t> g_last_stock_availability{kUnknownAvailability};

inline std::atomic<uint32_t> g_logged_availability{0u};
inline std::atomic<uint32_t> g_logged_section{0u};
inline std::atomic<uint32_t> g_logged_medals{0u};
inline std::atomic<uint32_t> g_logged_stock{0u};
inline std::atomic<uint32_t> g_logged_info{0u};
inline std::atomic<uint32_t> g_logged_press{0u};
inline std::atomic<uint32_t> g_logged_start{0u};

// Depth of craft-button handlers currently executing on this thread, and
// whether the stock handler refused the current press by raising its hint
// window. Managed UI callbacks all run on the Unity main thread, and both
// values are thread-local anyway, so a hook on another thread never observes a
// press it does not belong to.
inline thread_local int32_t g_press_depth = 0;
inline thread_local bool g_press_refused = false;

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

// Reads WeaponManager.CurrentCraftingWeaponOrAvatar and reports whether a
// craft is running. The stock handler tests the same value by checking its Key
// for null/empty, which is reproduced here without calling into the generic
// KeyValuePair accessor.
bool craft_slot_busy(std::string* out_tag) {
    if (g_current_craft == nullptr) return false;

    alignas(8) unsigned char buffer[kCraftSlotBufferSize];
    std::memset(buffer, 0, sizeof(buffer));
    g_current_craft(buffer, nullptr, g_mi_current_craft);

    ManagedString* key = nullptr;
    std::memcpy(&key, buffer + kCraftSlotKeyOffset, sizeof(key));
    if (key == nullptr) return false;
    if (il2cpp::string_length != nullptr && il2cpp::string_length(key) <= 0) {
        return false;
    }
    const std::string tag = il2cpp::to_utf8(key, 64);
    if (tag.empty()) return false;
    if (out_tag != nullptr) *out_tag = tag;
    return true;
}

// Runs the handler's own success path for an item its recipe list refuses.
// Every step is a stock managed method; nothing is written directly.
bool start_craft_directly(ManagedString* item_id) {
    if (!g_direct_start_ready) {
        if (should_log(g_logged_start)) {
            LOGE("clan-craft: craft of '%s' was refused and the direct start "
                 "is unavailable; the stock craft methods could not be "
                 "resolved at startup", item_name(item_id).c_str());
        }
        return false;
    }

    const std::string tag = item_name(item_id);

    std::string running_tag;
    if (craft_slot_busy(&running_tag)) {
        if (should_log(g_logged_start)) {
            LOGW("clan-craft: '%s' was not started because '%s' is still "
                 "being crafted; the stock client also allows only one craft "
                 "at a time", tag.c_str(), running_tag.c_str());
        }
        return false;
    }

    void* record = g_item_by_tag(nullptr, item_id, g_mi_item_by_tag);
    if (record == nullptr) {
        LOGE("clan-craft: ItemDb has no record for '%s'; craft not started",
             tag.c_str());
        return false;
    }
    ManagedString* prefab = g_prefab_name(record, g_mi_prefab_name);
    if (prefab == nullptr) {
        LOGE("clan-craft: item '%s' has no prefab name; craft not started",
             tag.c_str());
        return false;
    }

    int32_t seconds = g_craft_seconds(nullptr, prefab, g_mi_craft_seconds);
    if (seconds < 0) seconds = 0;
    const int64_t now = g_server_time(nullptr, g_mi_server_time);
    const int64_t end_time = now + seconds;

    g_start_craft(nullptr, item_id, end_time, g_mi_start_craft);

    std::string started_tag;
    if (!craft_slot_busy(&started_tag)) {
        LOGE("clan-craft: WeaponManager did not accept the craft of '%s' "
             "(prefab '%s', %d s); the craft slot is still empty",
             tag.c_str(), il2cpp::to_utf8(prefab, 64).c_str(), seconds);
        return false;
    }

    if (should_log(g_logged_start)) {
        LOGI("clan-craft: stock handler refused '%s' because the ordinary "
             "craft recipe list has no entry for a clan blueprint; craft "
             "started through WeaponManager (prefab '%s', %d s, ends at %lld, "
             "slot now '%s')",
             tag.c_str(), il2cpp::to_utf8(prefab, 64).c_str(), seconds,
             static_cast<long long>(end_time), started_tag.c_str());
    }
    return true;
}

// The hint window shows one requirement line plus one call-to-action per
// availability state. Both clan states point at something unreachable here,
// and their button opens the dead clan search, so the window is skipped for
// those states. During a craft press it is also the refusal signature of the
// recipe-list branch: the window is suppressed and the press is remembered as
// refused, so the craft can be started directly once the handler returns.
void hook_show_section_info(void* self, const MethodInfo* method) {
    const int32_t stock_value =
        g_last_stock_availability.load(std::memory_order_relaxed);

    if (inside_craft_press()) {
        g_press_refused = true;
        if (should_log(g_logged_info)) {
            LOGI("clan-craft: stock handler refused the press and raised its "
                 "hint window (last section state %s); window suppressed, "
                 "starting the craft directly",
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

// Marks the craft press, forwards it unchanged, and starts the craft directly
// if the stock handler refused it. Only the outermost press acts, so a nested
// handler cannot start the same craft twice.
void hook_craft_press(void* self, ManagedString* item_id, void* bank_panel,
                      void* instant_window_parent,
                      const MethodInfo* method) {
    const bool outermost = (g_press_depth == 0);
    PressScope scope;
    if (outermost) g_press_refused = false;

    if (should_log(g_logged_press)) {
        LOGI("clan-craft: craft pressed for '%s' (stock section state %s)",
             item_name(item_id).c_str(),
             availability_name(
                 g_last_stock_availability.load(std::memory_order_relaxed)));
    }

    g_craft_press(self, item_id, bank_panel, instant_window_parent, method);

    if (!outermost || !g_press_refused) return;
    g_press_refused = false;
    start_craft_directly(item_id);
}

bool resolve_call(const hook::ManagedMethod& target, void** out_fn,
                  const MethodInfo** out_mi) {
    void* info = il2cpp::find_method_info(target.namespaze, target.klass,
                                          target.method, target.args_count);
    void* pointer = il2cpp::method_pointer(info);
    if (info == nullptr || pointer == nullptr) {
        LOGE("clan-craft: cannot resolve %s.%s/%d", target.klass, target.method,
             target.args_count);
        return false;
    }
    *out_fn = pointer;
    *out_mi = info;
    return true;
}

// Resolves the stock craft methods used by the forced start. The craft slot
// getter is optional: without it the start is still correct, it just cannot
// decline a press while another craft is running, so its absence downgrades
// the feature instead of disabling it.
bool resolve_direct_start() {
    bool ok = true;
    ok &= resolve_call({"", "ItemDb", "GetByTag", 1},
                       reinterpret_cast<void**>(&g_item_by_tag),
                       &g_mi_item_by_tag);
    ok &= resolve_call({"", "ItemRecord", "get_PrefabName", 0},
                       reinterpret_cast<void**>(&g_prefab_name),
                       &g_mi_prefab_name);
    ok &= resolve_call({"", "BalanceController", "GetFullTimeCraftInSeconds", 1},
                       reinterpret_cast<void**>(&g_craft_seconds),
                       &g_mi_craft_seconds);
    ok &= resolve_call({"", "FriendsController", "get_ServerTime", 0},
                       reinterpret_cast<void**>(&g_server_time),
                       &g_mi_server_time);
    ok &= resolve_call({"", "WeaponManager", "StartCraftWeaponOrAvatar", 2},
                       reinterpret_cast<void**>(&g_start_craft),
                       &g_mi_start_craft);

    if (!resolve_call({"", "WeaponManager", "get_CurrentCraftingWeaponOrAvatar",
                       0},
                      reinterpret_cast<void**>(&g_current_craft),
                      &g_mi_current_craft)) {
        g_current_craft = nullptr;
        LOGW("clan-craft: the craft slot getter is unavailable; a refused "
             "press cannot be declined while another craft is running");
    }
    return ok;
}

} // namespace detail

inline bool install_hooks() {
    // Mandatory: the enum the whole craft section is driven by. Without it
    // clan blueprints stay locked in the UI no matter what the prices say.
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

    // The refusal signature of the recipe-list branch, and the window that
    // would otherwise advertise a requirement which no longer blocks anything.
    const bool info_window = hook::install(
        {"", "ShopNGUIController", "ShowCraftSectionInfo", 0},
        detail::replacement(&detail::hook_show_section_info),
        detail::original_slot(&detail::g_show_section_info), false);
    if (!info_window) {
        LOGE("clan-craft: ShopNGUIController.ShowCraftSectionInfo could not "
             "be hooked; a refused press cannot be detected and the clan hint "
             "window may still be shown");
    }

    // The press itself: the scope the refusal is observed in and the place
    // the direct craft start is issued from.
    const bool craft_press = hook::install(
        {"", "ShopNGUIController", "HandleCraftButton_NoInfo", 3},
        detail::replacement(&detail::hook_craft_press),
        detail::original_slot(&detail::g_craft_press), false);
    if (!craft_press) {
        LOGE("clan-craft: ShopNGUIController.HandleCraftButton_NoInfo could "
             "not be hooked; clan blueprints cannot be crafted");
    }

    detail::g_direct_start_ready = detail::resolve_direct_start();
    if (!detail::g_direct_start_ready) {
        LOGE("clan-craft: the stock craft methods could not be resolved; a "
             "refused press cannot be completed");
    }

    const bool ready = craft_press && info_window &&
                       detail::g_direct_start_ready;

    LOGI("clan-craft: armed (section=Available, shop shortcut=%s, medals=%s, "
         "clan storage=%s, clan hint window=%s, press scope=%s, direct craft "
         "start=%s, busy-slot guard=%s); no clan object is created",
         section_available ? "forced" : "stock", medals ? "free" : "stock",
         (any_part && part_count) ? "synthetic" : "partial",
         info_window ? "suppressed" : "stock",
         craft_press ? "on" : "off",
         ready ? "on" : "off",
         detail::g_current_craft != nullptr ? "on" : "off");
    return true;
}

} // namespace clan_craft
