#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Retired arsenal weapons back on the shop shelves.
//
// The weapons pulled from the arsenal by the 2015-06-15 content update are
// still complete in this client: ItemDb keeps their records, their prefabs,
// icons and upgrade chains ship with the APK, and the game still knows how to
// equip and render them. Only the shop refuses to list them.
//
// Why the obvious candidate is not used. ItemRecord.get_CanBuy() (RVA
// 0x535CF4) has no backing field in this build — it is computed — and it is
// consumed by ownership and pricing paths rather than by the shelf builder:
//
//   ItemDb.GetCanBuyWeapon()           RVA 0x5320A0
//   ItemDb.GetCanBuyWeaponTags()       RVA 0x532264
//   ItemDb.GetCanBuyWearTags(...)      RVA 0x5359D8
//   RespawnWindowItemToBuy.IsCanBuy()  RVA 0x18F27D8
//
// Forcing it would change what the client considers a valid purchasable item
// and could present an owned weapon as unowned. It is therefore never touched
// by this module.
//
// The hiding is a separate list, not a per-item flag:
//
//   WeaponManager._Removed150615_PrefabNAmes   static HashSet<string>, +0x130
//     filled once by InitializeRemoved150615Weapons()   RVA 0x98AB94
//       +0x90 -> BalanceController.RemovedWeaponNames()  RVA 0xC9E2D0
//     exposed by  get_Removed150615_PrefabNames()        RVA 0x98ACC0
//       +0xB4 -> InitializeRemoved150615Weapons()
//
// That getter is lazy, which rules out the cheapest trick: nulling the static
// field does not disable the filter, the next getter call simply rebuilds the
// list from the balance config.
//
// The shelf is refused inside the per-record shop builder:
//
//   WeaponManager._AddWeaponToShopListsIfNeeded(ItemRecord)  RVA 0x98ADE4
//     +0x474  bl  WeaponManager.get_Removed150615_PrefabNames()
//             bl  ItemRecord.get_PrefabName()
//                 HashSet<string>.Contains(prefab)
//             b   +0x71C        ; returns; the record is never appended
//
// It is called only from _InitShopCategoryLists (+0x9C0, +0x1090, +0x123C),
// which the shop rebuild drives. Nothing on that path reads or writes
// ownership, currency or saves: a filtered record is merely absent from a
// category list.
//
// The same list has fifteen readers, and three of them must keep seeing its
// real contents, because they substitute a retired weapon on another player's
// model instead of hiding anything:
//
//   CharacterInterface.SetWeapon(...)                            +0x664
//   CharacterView.SetWeaponAndSkin(..., replaceRemovedWeapons)   +0x948
//   Player_move_c.<SetWeaponRPC>c__Iterator2.MoveNext()          +0x138
//
// Emptying, clearing or replacing the list would reach those too, so this
// module does none of that. For the single record the shop builder is
// currently deciding about, that record's prefab name is unlisted right before
// the stock method runs and put back immediately after it returns, and the
// restore is verified. Every step uses the game's own HashSet<string> methods,
// resolved from the live instance, so no container layout is assumed.
//
// Prices stay stock. ItemDb.GetPriceByShopId (RVA 0x531448) consults
// BalanceController.pricesFromServer first and falls back to the local ItemDb
// records, so a re-listed weapon is bought through the normal shop flow at the
// price the client computes for it. No price is fabricated here.
//
// Every other shelf condition of that same method is deliberately left alone —
// campaign-only records, filter maps, the try-gun check, the player tier, the
// event/craft tag set and its level requirement — so an unhidden weapon still
// appears where the client itself would have put it.
namespace removed_arsenal {
namespace detail {

using MethodInfo = void;
using ManagedString = void;

// Old-IL2CPP ARM32 ABI: instance methods take the object in r0, static
// generated methods take a hidden null context there instead; managed
// arguments follow, then MethodInfo*.
using AddToShopListsFn = void (*)(void* self, void* item_record,
                                  const MethodInfo* method);
using RemovedSetFn = void* (*)(void* static_context, const MethodInfo* method);
using RecordStringFn = ManagedString* (*)(void* self,
                                          const MethodInfo* method);
// HashSet<string>.Contains / Remove / Add all share this shape.
using SetQueryFn = bool (*)(void* self, ManagedString* value,
                            const MethodInfo* method);

inline AddToShopListsFn g_add_to_shop = nullptr;

inline RemovedSetFn g_removed_set = nullptr;
inline const MethodInfo* g_mi_removed_set = nullptr;
inline RecordStringFn g_prefab_name = nullptr;
inline const MethodInfo* g_mi_prefab_name = nullptr;
inline RecordStringFn g_tag = nullptr;
inline const MethodInfo* g_mi_tag = nullptr;

inline SetQueryFn g_set_contains = nullptr;
inline const MethodInfo* g_mi_set_contains = nullptr;
inline SetQueryFn g_set_remove = nullptr;
inline const MethodInfo* g_mi_set_remove = nullptr;
inline SetQueryFn g_set_add = nullptr;
inline const MethodInfo* g_mi_set_add = nullptr;

inline std::atomic<bool> g_set_api_ready{false};
inline std::atomic<bool> g_set_api_failed{false};
inline std::atomic<bool> g_record_api_ready{false};

// A shop rebuild walks every weapon record, so logging is capped. The first
// items are the interesting ones: they name exactly which retired weapons the
// shelf received.
inline constexpr uint32_t kMaxLoggedItems = 48;

inline std::atomic<uint32_t> g_unhidden_total{0u};
inline std::atomic<uint32_t> g_logged_unhidden{0u};
inline std::atomic<uint32_t> g_logged_problems{0u};

// Depth of shop-builder calls currently executing on this thread. A nested
// call would already be running inside an unlisted window, so it is forwarded
// untouched instead of unlisting the same name twice.
inline thread_local int32_t g_builder_depth = 0;

template <typename Fn>
void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

bool should_log(std::atomic<uint32_t>& counter, uint32_t budget) {
    return counter.fetch_add(1u, std::memory_order_relaxed) < budget;
}

std::string text_of(ManagedString* value) {
    if (value == nullptr) return std::string("<null>");
    return il2cpp::to_utf8(value, 64);
}

bool resolve_call(const hook::ManagedMethod& target, void** out_fn,
                  const MethodInfo** out_mi) {
    void* info = il2cpp::find_method_info(target.namespaze, target.klass,
                                          target.method, target.args_count);
    void* pointer = il2cpp::method_pointer(info);
    if (info == nullptr || pointer == nullptr) {
        LOGE("removed-arsenal: cannot resolve %s.%s/%d", target.klass,
             target.method, target.args_count);
        return false;
    }
    *out_fn = pointer;
    *out_mi = info;
    return true;
}

// HashSet<string> is a generic instantiation, so its methods are taken from
// the class of the live set instead of being looked up by name in metadata.
// Resolved once, on the first shop record that is actually filtered.
bool resolve_set_api(void* removed_set) {
    if (g_set_api_ready.load(std::memory_order_acquire)) return true;
    if (g_set_api_failed.load(std::memory_order_acquire)) return false;
    if (removed_set == nullptr || il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_method_from_name == nullptr) {
        return false;
    }

    void* klass = il2cpp::object_get_class(removed_set);
    if (klass == nullptr) {
        g_set_api_failed.store(true, std::memory_order_release);
        LOGE("removed-arsenal: the retired-name list has no class; the shop "
             "filter is left stock");
        return false;
    }

    void* contains_info =
        il2cpp::class_get_method_from_name(klass, "Contains", 1);
    void* remove_info = il2cpp::class_get_method_from_name(klass, "Remove", 1);
    void* add_info = il2cpp::class_get_method_from_name(klass, "Add", 1);
    void* contains_ptr = il2cpp::method_pointer(contains_info);
    void* remove_ptr = il2cpp::method_pointer(remove_info);
    void* add_ptr = il2cpp::method_pointer(add_info);

    if (contains_ptr == nullptr || remove_ptr == nullptr ||
        add_ptr == nullptr) {
        g_set_api_failed.store(true, std::memory_order_release);
        LOGE("removed-arsenal: HashSet<string> Contains/Remove/Add could not "
             "be resolved (%d/%d/%d); retired weapons stay hidden",
             contains_ptr != nullptr ? 1 : 0, remove_ptr != nullptr ? 1 : 0,
             add_ptr != nullptr ? 1 : 0);
        return false;
    }

    g_set_contains = reinterpret_cast<SetQueryFn>(contains_ptr);
    g_mi_set_contains = contains_info;
    g_set_remove = reinterpret_cast<SetQueryFn>(remove_ptr);
    g_mi_set_remove = remove_info;
    g_set_add = reinterpret_cast<SetQueryFn>(add_ptr);
    g_mi_set_add = add_info;
    g_set_api_ready.store(true, std::memory_order_release);

    LOGI("removed-arsenal: retired-name list is live; shop shelf filter is "
         "now bypassed per record");
    return true;
}

// Wraps one stock shelf decision. Anything unexpected forwards the call
// unchanged, which keeps the stock behaviour (the weapon stays hidden) instead
// of guessing.
void hook_add_to_shop(void* self, void* item_record,
                      const MethodInfo* method) {
    if (item_record == nullptr || g_builder_depth > 0 ||
        !g_record_api_ready.load(std::memory_order_acquire)) {
        g_add_to_shop(self, item_record, method);
        return;
    }

    void* removed_set = g_removed_set(nullptr, g_mi_removed_set);
    if (removed_set == nullptr || !resolve_set_api(removed_set)) {
        g_add_to_shop(self, item_record, method);
        return;
    }

    ManagedString* prefab = g_prefab_name(item_record, g_mi_prefab_name);
    if (prefab == nullptr ||
        !g_set_contains(removed_set, prefab, g_mi_set_contains)) {
        g_add_to_shop(self, item_record, method);
        return;
    }

    ++g_builder_depth;
    const bool unlisted = g_set_remove(removed_set, prefab, g_mi_set_remove);
    g_add_to_shop(self, item_record, method);
    bool restored = true;
    if (unlisted) {
        g_set_add(removed_set, prefab, g_mi_set_add);
        restored = g_set_contains(removed_set, prefab, g_mi_set_contains);
    }
    --g_builder_depth;

    if (!unlisted) {
        if (should_log(g_logged_problems, kMaxLoggedItems)) {
            LOGW("removed-arsenal: '%s' is listed as retired but could not be "
                 "unlisted; it stays out of the shop",
                 text_of(prefab).c_str());
        }
        return;
    }

    if (!restored) {
        LOGE("removed-arsenal: '%s' was not put back into the retired-name "
             "list; other players' models may render it directly",
             text_of(prefab).c_str());
    }

    const uint32_t total = g_unhidden_total.fetch_add(
                               1u, std::memory_order_relaxed) + 1u;
    if (should_log(g_logged_unhidden, kMaxLoggedItems)) {
        const std::string tag =
            (g_tag != nullptr) ? text_of(g_tag(item_record, g_mi_tag))
                               : std::string("<tag-api-unavailable>");
        LOGI("removed-arsenal: retired weapon '%s' (prefab '%s') offered to "
             "the shop at its stock price; shelf decisions so far: %u",
             tag.c_str(), text_of(prefab).c_str(), total);
    }
}

} // namespace detail

inline bool install_hooks() {
    // Mandatory reads: the retired-name list itself and the prefab name the
    // stock filter compares against.
    bool resolved = detail::resolve_call(
        {"", "WeaponManager", "get_Removed150615_PrefabNames", 0},
        reinterpret_cast<void**>(&detail::g_removed_set),
        &detail::g_mi_removed_set);
    resolved &= detail::resolve_call(
        {"", "ItemRecord", "get_PrefabName", 0},
        reinterpret_cast<void**>(&detail::g_prefab_name),
        &detail::g_mi_prefab_name);
    if (!resolved) {
        LOGE("removed-arsenal: the retired-weapon list or the record prefab "
             "name could not be resolved; retired weapons stay hidden");
        return false;
    }
    detail::g_record_api_ready.store(true, std::memory_order_release);

    // Optional: item tags are used for log lines only.
    if (!detail::resolve_call({"", "ItemRecord", "get_Tag", 0},
                              reinterpret_cast<void**>(&detail::g_tag),
                              &detail::g_mi_tag)) {
        detail::g_tag = nullptr;
        LOGW("removed-arsenal: ItemRecord.get_Tag is unavailable; log lines "
             "will name prefabs only");
    }

    // Mandatory: the per-record shelf decision that currently drops them.
    const bool shelf = hook::install(
        {"", "WeaponManager", "_AddWeaponToShopListsIfNeeded", 1},
        detail::replacement(&detail::hook_add_to_shop),
        detail::original_slot(&detail::g_add_to_shop), true);
    if (!shelf) {
        LOGE("removed-arsenal: WeaponManager._AddWeaponToShopListsIfNeeded "
             "could not be hooked; retired weapons stay hidden");
        return false;
    }

    LOGI("removed-arsenal: armed (scope=shop shelf builder only, list=never "
         "cleared, CanBuy=untouched, prices=stock, tier/level/filter gates="
         "stock)");
    return true;
}

} // namespace removed_arsenal
