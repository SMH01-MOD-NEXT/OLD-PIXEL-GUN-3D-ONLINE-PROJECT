#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <string>

#include "elf_sym.h"
#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Local replacements for legacy services that disappeared with the original
// PG3D backend. Stock purchase, inventory, upgrade, event and save routines
// remain responsible for all state changes; this module only supplies the
// missing tutorial completion, craft-weapon price, and wall-clock input.
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
using NumDetailsFn = int32_t (*)(void* static_context, ManagedString* item_id,
                                 const MethodInfo* method);
using GetItemCategoryFn = int32_t (*)(void* static_context,
                                      ManagedString* item_id,
                                      const MethodInfo* method);
using GetItemPriceFn = void* (*)(void* static_context, ManagedString* item_id,
                                 int32_t category, bool upgrade_not_buy,
                                 bool use_discounts,
                                 bool item_id_is_first_unbought,
                                 const MethodInfo* method);
using ItemPriceCtorFn = void (*)(void* self, int32_t price,
                                 ManagedString* currency,
                                 const MethodInfo* method);
using HandleCraftFn = void (*)(void* self, ManagedString* item_id,
                               void* main_panel_for_bank,
                               void* parent_for_instant_window,
                               const MethodInfo* method);
using BuyOrUpgradeFn = void (*)(void* self, bool is_upgrade,
                                ManagedString* item_id, void* main_panel,
                                bool from_upgrade_recommendations,
                                const MethodInfo* method);
using ObjectNewFn = void* (*)(void* klass);
using GcHandleNewFn = uint32_t (*)(void* object, bool pinned);
using GcHandleGetTargetFn = void* (*)(uint32_t handle);

inline TrainingCompletedFn g_training_completed = nullptr;
inline SetTrainingStageFn g_set_training_stage = nullptr;
inline const MethodInfo* g_mi_set_training_stage = nullptr;
inline StoragerSetIntFn g_storager_set_int = nullptr;
inline const MethodInfo* g_mi_storager_set_int = nullptr;
inline ServerTimeFn g_server_time = nullptr;
inline NumDetailsFn g_num_details = nullptr;
inline const MethodInfo* g_mi_num_details = nullptr;
inline GetItemCategoryFn g_get_item_category = nullptr;
inline const MethodInfo* g_mi_get_item_category = nullptr;
inline GetItemPriceFn g_get_item_price = nullptr;
inline ItemPriceCtorFn g_item_price_ctor = nullptr;
inline const MethodInfo* g_mi_item_price_ctor = nullptr;
inline HandleCraftFn g_handle_craft = nullptr;
inline BuyOrUpgradeFn g_buy_or_upgrade = nullptr;
inline const MethodInfo* g_mi_buy_or_upgrade = nullptr;
inline ObjectNewFn g_object_new = nullptr;
inline GcHandleNewFn g_gchandle_new = nullptr;
inline GcHandleGetTargetFn g_gchandle_get_target = nullptr;

inline void* g_item_price_class = nullptr;
inline ManagedString* g_gems_currency = nullptr;
inline ManagedString* g_shop_tutorial_key = nullptr;
inline uint32_t g_gems_currency_handle = 0;
inline uint32_t g_shop_tutorial_key_handle = 0;

inline std::atomic<bool> g_tutorial_persisted{false};
inline std::atomic<bool> g_time_fallback_logged{false};
inline std::atomic<int32_t> g_last_time{0};
inline thread_local bool g_persisting_tutorial = false;

// ItemPrice only contains price/currency, so one rooted object can be reused
// for every weapon with the same details requirement. A fixed table avoids
// unbounded managed allocation from armory UI refreshes.
struct PriceSlot {
    int32_t price = 0;
    uint32_t handle = 0;
};
inline std::array<PriceSlot, 128> g_price_slots{};
inline size_t g_price_slot_count = 0;

inline constexpr int32_t kFirstMatchCompleted = 3;
inline constexpr const char* kShopTutorialKey =
    "shop_tutorial_state_passed_VER_12_1";
inline constexpr const char* kGemsCurrency = "GemsCurrency";

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

bool is_weapon_category(int32_t category) {
    // ItemDb returns the six ordinary weapon categories even when the armory
    // is currently displaying WeaponCraft/EventCraft/SetsCraft.
    return category >= 0 && category <= 5;
}

int32_t required_details(ManagedString* item_id) {
    if (item_id == nullptr || g_num_details == nullptr) return 0;
    return g_num_details(nullptr, item_id, g_mi_num_details);
}

int32_t item_category(ManagedString* item_id) {
    if (item_id == nullptr || g_get_item_category == nullptr) return -1;
    return g_get_item_category(nullptr, item_id, g_mi_get_item_category);
}

void* craft_weapon_price(int32_t required) {
    if (required <= 0 || g_item_price_class == nullptr ||
        g_item_price_ctor == nullptr || g_gems_currency == nullptr ||
        g_object_new == nullptr || g_gchandle_new == nullptr ||
        g_gchandle_get_target == nullptr) {
        return nullptr;
    }

    // Clear economy rule: one formerly required detail now costs one Gem.
    // Keep corrupt balance data from creating an invalid negative/huge price.
    int32_t price = required;
    if (price > 1000000) price = 1000000;

    for (size_t i = 0; i < g_price_slot_count; ++i) {
        if (g_price_slots[i].price == price) {
            return g_gchandle_get_target(g_price_slots[i].handle);
        }
    }

    auto* currency = static_cast<ManagedString*>(
        g_gchandle_get_target(g_gems_currency_handle));
    if (currency == nullptr) return nullptr;
    void* value = g_object_new(g_item_price_class);
    if (value == nullptr) return nullptr;
    g_item_price_ctor(value, price, currency, g_mi_item_price_ctor);

    const uint32_t handle = g_gchandle_new(value, false);
    if (handle == 0) return nullptr;
    if (g_price_slot_count < g_price_slots.size()) {
        g_price_slots[g_price_slot_count++] = {price, handle};
    }
    return g_gchandle_get_target(handle);
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

    // Unix seconds still fit in signed 32-bit for the target game's lifetime;
    // keeping the atomic 32-bit also avoids a libatomic dependency on ARMv7.
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

void* hook_get_item_price(void* static_context, ManagedString* item_id,
                          int32_t category, bool upgrade_not_buy,
                          bool use_discounts,
                          bool item_id_is_first_unbought,
                          const MethodInfo* method) {
    const int32_t required = required_details(item_id);
    const int32_t base_category = item_category(item_id);
    if (!upgrade_not_buy && required > 0 &&
        is_weapon_category(base_category)) {
        void* price = craft_weapon_price(required);
        if (price != nullptr) return price;
    }
    return g_get_item_price(static_context, item_id, category, upgrade_not_buy,
                            use_discounts, item_id_is_first_unbought, method);
}

void hook_handle_craft(void* self, ManagedString* item_id,
                       void* main_panel_for_bank,
                       void* parent_for_instant_window,
                       const MethodInfo* method) {
    const int32_t required = required_details(item_id);
    const int32_t category = item_category(item_id);
    if (required > 0 && is_weapon_category(category)) {
        LOGI("legacy: routing detail weapon '%s' through stock Gem purchase "
             "(price=%d)",
             il2cpp::to_utf8(item_id, 96).c_str(), required);
        g_buy_or_upgrade(self, false, item_id, main_panel_for_bank, false,
                         g_mi_buy_or_upgrade);
        return;
    }
    g_handle_craft(self, item_id, main_panel_for_bank,
                   parent_for_instant_window, method);
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
    ok &= detail::resolve_call(
        {"", "BalanceController", "NumOfDetailsForCraft", 1},
        reinterpret_cast<void**>(&detail::g_num_details),
        &detail::g_mi_num_details);
    ok &= detail::resolve_call(
        {"", "ItemDb", "GetItemCategory", 1},
        reinterpret_cast<void**>(&detail::g_get_item_category),
        &detail::g_mi_get_item_category);
    ok &= detail::resolve_call(
        {"", "ItemPrice", ".ctor", 2},
        reinterpret_cast<void**>(&detail::g_item_price_ctor),
        &detail::g_mi_item_price_ctor);
    ok &= detail::resolve_call(
        {"", "ShopNGUIController", "BuyOrUpgradeWeapon", 4},
        reinterpret_cast<void**>(&detail::g_buy_or_upgrade),
        &detail::g_mi_buy_or_upgrade);

    detail::g_object_new = reinterpret_cast<detail::ObjectNewFn>(
        elfsym::find_symbol("libil2cpp.so", "il2cpp_object_new"));
    detail::g_gchandle_new = reinterpret_cast<detail::GcHandleNewFn>(
        elfsym::find_symbol("libil2cpp.so", "il2cpp_gchandle_new"));
    detail::g_gchandle_get_target =
        reinterpret_cast<detail::GcHandleGetTargetFn>(
            elfsym::find_symbol("libil2cpp.so",
                                "il2cpp_gchandle_get_target"));
    detail::g_item_price_class = il2cpp::find_class("", "ItemPrice");
    detail::g_gems_currency = static_cast<detail::ManagedString*>(
        il2cpp::string_new(detail::kGemsCurrency));
    detail::g_shop_tutorial_key = static_cast<detail::ManagedString*>(
        il2cpp::string_new(detail::kShopTutorialKey));
    if (detail::g_item_price_class == nullptr ||
        detail::g_gems_currency == nullptr ||
        detail::g_shop_tutorial_key == nullptr ||
        detail::g_object_new == nullptr ||
        detail::g_gchandle_new == nullptr ||
        detail::g_gchandle_get_target == nullptr) {
        LOGE("legacy: managed allocation/rooting API or gameplay constants "
             "unavailable");
        ok = false;
    }
    if (ok) {
        detail::g_gems_currency_handle =
            detail::g_gchandle_new(detail::g_gems_currency, false);
        detail::g_shop_tutorial_key_handle =
            detail::g_gchandle_new(detail::g_shop_tutorial_key, false);
        if (detail::g_gems_currency_handle == 0 ||
            detail::g_shop_tutorial_key_handle == 0) {
            LOGE("legacy: failed to root managed gameplay constants");
            ok = false;
        }
    }
    if (!ok) {
        LOGE("legacy: local tutorial/shop/upgrade targets incomplete");
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
    const bool craft_price = hook::install(
        {"", "ShopNGUIController", "GetItemPrice", 5},
        detail::replacement(&detail::hook_get_item_price),
        detail::original_slot(&detail::g_get_item_price), true);
    const bool craft_route = hook::install(
        {"", "ShopNGUIController", "HandleCraftButton_NoInfo", 3},
        detail::replacement(&detail::hook_handle_craft),
        detail::original_slot(&detail::g_handle_craft), true);

    if (!tutorial || !server_time || !craft_price || !craft_route) {
        LOGE("legacy: one or more gameplay hooks failed");
        return false;
    }

    LOGI("legacy: tutorial auto-skip, Gem-priced detail weapons, and local "
         "upgrade/crafting time armed");
    return true;
}

} // namespace legacy_gameplay
