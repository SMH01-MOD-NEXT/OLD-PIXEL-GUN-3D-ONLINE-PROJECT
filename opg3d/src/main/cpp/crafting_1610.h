#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Local weapon-upgrade and craft compatibility for the supplied 16.1.0
// ARMv7 IL2CPP build. Every target below was mapped from dump.cs and checked
// against libil2cpp.so call sites. Ownership, persistence and craft completion
// remain in the game's stock methods.
namespace crafting_1610 {
namespace detail {

using MethodInfo = void;
using ManagedString = void;

using ServerTimeFn = int64_t (*)(void*, const MethodInfo*);
using IntStringFn = int32_t (*)(void*, ManagedString*, const MethodInfo*);
using BoolStringsFn = bool (*)(void*, ManagedString*, ManagedString*,
                               const MethodInfo*);
using AvailabilityFn = int32_t (*)(void*, const MethodInfo*);
using PartCountFn = int32_t (*)(void*, ManagedString*, int32_t,
                                const MethodInfo*);
using AnyPartFn = bool (*)(void*, ManagedString*, int32_t,
                           const MethodInfo*);
using ShowInfoFn = void (*)(void*, const MethodInfo*);
using CraftPressFn = void (*)(void*, ManagedString*, void*, void*,
                              const MethodInfo*);
using ItemByTagFn = void* (*)(void*, ManagedString*, const MethodInfo*);
using PrefabNameFn = ManagedString* (*)(void*, const MethodInfo*);
using StartCraftFn = void (*)(void*, ManagedString*, int64_t,
                              const MethodInfo*);
using CurrentCraftFn = void (*)(void*, void*, const MethodInfo*);

inline ServerTimeFn g_server_time = nullptr;
inline IntStringFn g_required_details = nullptr;
inline BoolStringsFn g_enough_details = nullptr;
inline IntStringFn g_owned_details = nullptr;
inline AvailabilityFn g_availability = nullptr;
inline IntStringFn g_clan_medals = nullptr;
inline PartCountFn g_part_count = nullptr;
inline AnyPartFn g_any_part = nullptr;
inline ShowInfoFn g_show_info = nullptr;
inline CraftPressFn g_craft_press = nullptr;

inline ItemByTagFn g_item_by_tag = nullptr;
inline PrefabNameFn g_prefab_name = nullptr;
inline IntStringFn g_craft_seconds = nullptr;
inline StartCraftFn g_start_craft = nullptr;
inline CurrentCraftFn g_current_craft = nullptr;
inline const MethodInfo* g_mi_server_time = nullptr;
inline const MethodInfo* g_mi_item_by_tag = nullptr;
inline const MethodInfo* g_mi_prefab_name = nullptr;
inline const MethodInfo* g_mi_craft_seconds = nullptr;
inline const MethodInfo* g_mi_start_craft = nullptr;
inline const MethodInfo* g_mi_current_craft = nullptr;
inline bool g_direct_start_ready = false;

inline constexpr int32_t kAvailable = 3;
inline constexpr int32_t kSyntheticOwnedDetails = 99999;
inline constexpr int32_t kSyntheticClanParts = 99;
inline constexpr uint32_t kMaxDecisionLogs = 12;
inline constexpr uint32_t kMaxStartLogs = 24;
inline constexpr size_t kCraftSlotBufferSize = 32;

inline std::atomic<int32_t> g_last_time{0};
inline std::atomic<int32_t> g_last_availability{-1};
inline std::atomic<bool> g_time_fallback_logged{false};
inline std::atomic<uint32_t> g_required_logs{0};
inline std::atomic<uint32_t> g_gate_logs{0};
inline std::atomic<uint32_t> g_owned_logs{0};
inline std::atomic<uint32_t> g_clan_logs{0};
inline std::atomic<uint32_t> g_start_logs{0};
inline thread_local int32_t g_press_depth = 0;
inline thread_local bool g_press_refused = false;

template <typename Fn>
void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

bool should_log(std::atomic<uint32_t>& counter, uint32_t limit = kMaxDecisionLogs) {
    return counter.fetch_add(1u, std::memory_order_relaxed) < limit;
}

std::string item_name(ManagedString* value) {
    return value == nullptr ? std::string("<null>") : il2cpp::to_utf8(value, 64);
}

int64_t hook_server_time(void* context, const MethodInfo* method) {
    int64_t candidate = g_server_time(context, method);
    if (candidate <= 0) {
        candidate = static_cast<int64_t>(std::time(nullptr));
        bool expected = false;
        if (g_time_fallback_logged.compare_exchange_strong(expected, true)) {
            LOGI("craft-16.1.0: retired server time unavailable; local UTC "
                 "seconds now drive weapon upgrades and craft timers");
        }
    }
    if (candidate <= 0) candidate = 1;
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

int32_t hook_required_details(void* context, ManagedString* id,
                              const MethodInfo* method) {
    const int32_t stock = g_required_details(context, id, method);
    if (stock > 0 && should_log(g_required_logs)) {
        LOGI("craft-16.1.0: required details for '%s': %d -> 0",
             item_name(id).c_str(), stock);
    }
    return 0;
}

bool hook_enough_details(void* context, ManagedString* id,
                         ManagedString* balance_id, const MethodInfo* method) {
    const bool stock = g_enough_details(context, id, balance_id, method);
    if (!stock && should_log(g_gate_logs)) {
        LOGI("craft-16.1.0: detail gate for '%s' -> allowed",
             item_name(id).c_str());
    }
    return true;
}

int32_t hook_owned_details(void* context, ManagedString* tag,
                           const MethodInfo* method) {
    const int32_t stock = g_owned_details(context, tag, method);
    if (stock < kSyntheticOwnedDetails && should_log(g_owned_logs)) {
        LOGI("craft-16.1.0: owned detail '%s': %d -> %d",
             item_name(tag).c_str(), stock, kSyntheticOwnedDetails);
    }
    return stock >= kSyntheticOwnedDetails ? stock : kSyntheticOwnedDetails;
}

int32_t hook_availability(void* context, const MethodInfo* method) {
    const int32_t stock = g_availability(context, method);
    g_last_availability.store(stock, std::memory_order_relaxed);
    if (stock != kAvailable && should_log(g_clan_logs)) {
        LOGI("craft-16.1.0: clan craft section state %d -> Available", stock);
    }
    return kAvailable;
}

int32_t hook_clan_medals(void* context, ManagedString* id,
                         const MethodInfo* method) {
    const int32_t stock = g_clan_medals(context, id, method);
    if (stock > 0 && should_log(g_clan_logs)) {
        LOGI("craft-16.1.0: clan medal price for '%s': %d -> 0",
             item_name(id).c_str(), stock);
    }
    return 0;
}

int32_t hook_part_count(void* context, ManagedString* id, int32_t type,
                        const MethodInfo* method) {
    const int32_t stock = g_part_count(context, id, type, method);
    return stock > 0 ? stock : kSyntheticClanParts;
}

bool hook_any_part(void* context, ManagedString* id, int32_t type,
                   const MethodInfo* method) {
    (void)g_any_part(context, id, type, method);
    return true;
}

class PressScope {
  public:
    PressScope() { ++g_press_depth; }
    ~PressScope() { --g_press_depth; }
    PressScope(const PressScope&) = delete;
    PressScope& operator=(const PressScope&) = delete;
};

bool craft_slot_busy(std::string* out_tag) {
    if (g_current_craft == nullptr) return true;
    alignas(8) unsigned char result[kCraftSlotBufferSize];
    std::memset(result, 0, sizeof(result));
    g_current_craft(result, nullptr, g_mi_current_craft);
    ManagedString* key = nullptr;
    std::memcpy(&key, result, sizeof(key));
    if (key == nullptr) return false;
    if (il2cpp::string_length != nullptr && il2cpp::string_length(key) <= 0) {
        return false;
    }
    const std::string tag = item_name(key);
    if (tag.empty()) return false;
    if (out_tag != nullptr) *out_tag = tag;
    return true;
}

bool start_craft_directly(ManagedString* item_id) {
    const std::string tag = item_name(item_id);
    if (!g_direct_start_ready || item_id == nullptr) {
        LOGE("craft-16.1.0: cannot start refused craft '%s'; stock start API "
             "is incomplete", tag.c_str());
        return false;
    }

    std::string current;
    if (craft_slot_busy(&current)) {
        if (should_log(g_start_logs, kMaxStartLogs)) {
            LOGW("craft-16.1.0: '%s' not started; craft slot contains '%s'",
                 tag.c_str(), current.c_str());
        }
        return false;
    }

    void* record = g_item_by_tag(nullptr, item_id, g_mi_item_by_tag);
    if (record == nullptr) {
        LOGE("craft-16.1.0: item database has no record for '%s'", tag.c_str());
        return false;
    }
    ManagedString* prefab = g_prefab_name(record, g_mi_prefab_name);
    if (prefab == nullptr) {
        LOGE("craft-16.1.0: '%s' has no prefab name", tag.c_str());
        return false;
    }

    int32_t seconds = g_craft_seconds(nullptr, prefab, g_mi_craft_seconds);
    if (seconds < 0) seconds = 0;
    const int64_t now = hook_server_time(nullptr, g_mi_server_time);
    g_start_craft(nullptr, item_id, now + seconds, g_mi_start_craft);

    std::string accepted;
    if (!craft_slot_busy(&accepted)) {
        LOGE("craft-16.1.0: WeaponManager refused direct start for '%s'",
             tag.c_str());
        return false;
    }
    if (should_log(g_start_logs, kMaxStartLogs)) {
        LOGI("craft-16.1.0: clan blueprint '%s' started through stock "
             "WeaponManager (%d s, slot '%s')",
             tag.c_str(), seconds, accepted.c_str());
    }
    return true;
}

void hook_show_info(void* self, const MethodInfo* method) {
    if (g_press_depth > 0) {
        g_press_refused = true;
        if (should_log(g_clan_logs)) {
            LOGI("craft-16.1.0: dead-clan hint suppressed during craft press "
                 "(stock state %d)",
                 g_last_availability.load(std::memory_order_relaxed));
        }
        return;
    }
    g_show_info(self, method);
}

void hook_craft_press(void* self, ManagedString* item_id, void* bank_panel,
                      void* instant_parent, const MethodInfo* method) {
    const bool outermost = g_press_depth == 0;
    PressScope scope;
    if (outermost) g_press_refused = false;
    g_craft_press(self, item_id, bank_panel, instant_parent, method);
    if (outermost && g_press_refused) {
        g_press_refused = false;
        start_craft_directly(item_id);
    }
}

bool resolve_call(const hook::ManagedMethod& target, void** out,
                  const MethodInfo** out_mi) {
    void* info = il2cpp::find_method_info(target.namespaze, target.klass,
                                          target.method, target.args_count);
    void* pointer = il2cpp::method_pointer(info);
    if (info == nullptr || pointer == nullptr) {
        LOGE("craft-16.1.0: cannot resolve %s.%s/%d", target.klass,
             target.method, target.args_count);
        return false;
    }
    *out = pointer;
    *out_mi = info;
    return true;
}

bool resolve_direct_start() {
    bool ok = true;
    ok &= resolve_call({"", u8"丝业一丌专上丁丘丙", u8"丞下丗专丐丌业东与", 1},
                       reinterpret_cast<void**>(&g_item_by_tag),
                       &g_mi_item_by_tag);
    ok &= resolve_call({"", u8"与丟丈丅上七不业丛", u8"丟丟丐丂丝丂丕丕丙", 0},
                       reinterpret_cast<void**>(&g_prefab_name),
                       &g_mi_prefab_name);
    ok &= resolve_call({"", "BalanceController", u8"上丛七丆东丗不丐世", 1},
                       reinterpret_cast<void**>(&g_craft_seconds),
                       &g_mi_craft_seconds);
    ok &= resolve_call({"", "WeaponManager", u8"丌丒丟丞丛丛丈丛世", 2},
                       reinterpret_cast<void**>(&g_start_craft),
                       &g_mi_start_craft);
    ok &= resolve_call({"", "WeaponManager", u8"丛丑丑丏丐丟东丐丙", 0},
                       reinterpret_cast<void**>(&g_current_craft),
                       &g_mi_current_craft);
    void* server_pointer = nullptr;
    ok &= resolve_call({"", "FriendsController", u8"不丕专丈丄丄丆丞七", 0},
                       &server_pointer, &g_mi_server_time);
    return ok;
}

} // namespace detail

inline bool install_hooks() {
    const bool server_time = hook::install(
        {"", "FriendsController", u8"不丕专丈丄丄丆丞七", 0},
        detail::replacement(&detail::hook_server_time),
        detail::original_slot(&detail::g_server_time), true);
    const bool required = hook::install(
        {"", "BalanceController", u8"丕东丁丌丞丂丕丕与", 1},
        detail::replacement(&detail::hook_required_details),
        detail::original_slot(&detail::g_required_details), true);
    const bool gate = hook::install(
        {"Rilisoft", u8"不丅专丆与且丛丅丑", u8"三业丛丘丁丝万丛丞", 2},
        detail::replacement(&detail::hook_enough_details),
        detail::original_slot(&detail::g_enough_details), true);
    const bool owned = hook::install(
        {"Rilisoft", u8"不丅专丆与且丛丅丑", u8"丛丆万七丘丈业丅丏", 1},
        detail::replacement(&detail::hook_owned_details),
        detail::original_slot(&detail::g_owned_details), true);

    const bool availability = hook::install(
        {"", u8"上丞丈与世丕丛丈下", u8"专丘三丆丞丏且丕丂", 0},
        detail::replacement(&detail::hook_availability),
        detail::original_slot(&detail::g_availability), true);
    const bool medals = hook::install(
        {"", "BalanceController", u8"七丌丆东丛七三专一", 1},
        detail::replacement(&detail::hook_clan_medals),
        detail::original_slot(&detail::g_clan_medals), true);
    const bool part_count = hook::install(
        {"", "ClansController", u8"丙丆业东一丙丏丗下", 2},
        detail::replacement(&detail::hook_part_count),
        detail::original_slot(&detail::g_part_count), true);
    const bool any_part = hook::install(
        {"", "ClansController", u8"世丗三丙丗世丗丑七", 2},
        detail::replacement(&detail::hook_any_part),
        detail::original_slot(&detail::g_any_part), true);
    const bool info = hook::install(
        {"", "ShopNGUIController", u8"上丙专下丂不丑丁三", 0},
        detail::replacement(&detail::hook_show_info),
        detail::original_slot(&detail::g_show_info), true);
    const bool press = hook::install(
        {"", "ShopNGUIController", u8"丑东丝世丟丝丄丞丟", 3},
        detail::replacement(&detail::hook_craft_press),
        detail::original_slot(&detail::g_craft_press), true);

    detail::g_direct_start_ready = detail::resolve_direct_start();
    const bool ok = server_time && required && gate && owned && availability &&
                    medals && part_count && any_part && info && press &&
                    detail::g_direct_start_ready;
    if (!ok) {
        LOGE("craft-16.1.0: incomplete (time=%d required=%d gate=%d owned=%d "
             "availability=%d medals=%d stock=%d/%d info=%d "
             "press=%d direct=%d)",
             server_time ? 1 : 0, required ? 1 : 0, gate ? 1 : 0,
             owned ? 1 : 0, availability ? 1 : 0, medals ? 1 : 0,
             any_part ? 1 : 0, part_count ? 1 : 0,
             info ? 1 : 0, press ? 1 : 0,
             detail::g_direct_start_ready ? 1 : 0);
        return false;
    }
    LOGI("craft-16.1.0: local upgrade time, free weapon details and "
         "dead-clan craft workaround armed");
    return true;
}

} // namespace crafting_1610
