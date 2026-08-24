#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) hidden weapon, wear and gadget unlock
//
// A number of items ship inside the 23.1.3 build but can never be obtained on
// a private server: they are not offered in the shop, they have no reachable
// craft recipe, and the retired backend events that used to hand them out are
// gone. Ultimatum and Locator (weapons) or the Harpoon (gadget) are the well
// known examples, and the same is true for several wear pieces (hats, armor,
// boots, capes, masks).
//
// This port deliberately does NOT patch an "is hidden" / "can craft" read path
// and it does not fabricate UI rows. It grants real ownership through the very
// same stock item-inventory transaction that weapon_modules_2313.h already
// uses for modules, so the Armory, the loadout slots, the equipped storage,
// the Progress profile and the save payload stay internally consistent:
// everything the player sees is materialized by the game itself from the
// registry.
//
// Ownership source of truth (identical to the module unlock, see
// docs/PORT_23_1_3_MODULES.md):
//
//   registry singleton  PGCompany.上丞丅三业丙世不丙::下丌丑丁下丟丛丘上()   0x3046000
//   owned count         丙丛业丐丐七丛不丂(key, Nullable<filter>)          0x304F634
//   grant               丘上丄三业丏丙不且(key, Nullable<cause>, Action)   0x3062B08
//
// "Everything the build ships" is enumerated from two independent stock
// sources, so a definition that one of them omits is still reached by the
// other:
//
//   1. registry items of a type
//        丈丂丆丙丂一七丞丌(OfferItemType) -> List<三丛丐丙丈丌丈专万>    0x3060030
//      (no owned-filter argument, unlike 万下丘丗丈万业世世 at 0x305C330)
//   2. static catalogue per category
//        三与七丆丅丆丕丒业(OfferItemType, CategoryNames)
//                                     -> List<丒专与三七丁丌丟丆>       0x305C074
//        丌丄丛丈与丝丑世丆(entry)          -> 丑一丘与丁丄专专专 (item key)  0x30479D0
//        丁丒丕丌丂丌且丙且(CategoryNames)  -> OfferItemType             0x305C50C
//
// Both stages end in the same idempotent step: read the item key, ask the
// registry for the owned count and, only when that count is zero, run the
// stock grant and re-read the count to verify it registered. Nothing is
// written for an item the player already owns, and if one of the two sources
// ever returned owned items only, that stage degrades to a no-op instead of
// doing something unexpected.
//
// ARM64 ABI reminder: generated managed methods take their explicit arguments
// followed by MethodInfo*; instance methods take `this` first. Enum arguments
// (OfferItemType, CategoryNames) are plain int32. Per AAPCS64 a composite
// argument larger than 16 bytes (both Nullable<> arguments below) is passed
// indirectly as a pointer to a caller-owned copy, and an all-zero Nullable<>
// is a null optional (has_value lives at offset 0), so the callee substitutes
// its own defaults.
// -----------------------------------------------------------------------------

#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <string>

#include "il2cpp.h"
#include "log.h"

namespace hidden_items_2313 {
namespace detail {

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

// ------------------------------------------------------------------ tunables

// weapon_modules_2313 starts its own sweep at menu frame 120 and needs about
// 25 frames for its 42 definitions. This port starts later on purpose: both
// drive the same stock transaction and must not overlap on a single frame.
constexpr uint64_t kWarmupFrames = 300;

// Definitions processed per main-menu frame. Deliberately small: a grant is a
// full stock inventory transaction and must never land as one frame spike.
constexpr int32_t kGrantsPerTick = 2;

// Full sweeps attempted in total (late registry population included).
constexpr int32_t kMaxPasses = 3;

// Frames between two sweeps (~30 s at 60 fps).
constexpr uint64_t kRecheckFrames = 1800;

// Consecutive failed grants after which the port disarms itself, so a layout
// mismatch degrades to a no-op instead of a per-frame spin.
constexpr int32_t kMaxConsecutiveFailures = 48;

// Sanity bound for a single managed list.
constexpr int32_t kMaxListEntries = 8192;

// Log the first kLogBurst grants in full, then every kLogPeriod-th one.
constexpr uint64_t kLogBurst = 12u;
constexpr uint64_t kLogPeriod = 16u;

// Character, weapon and armor skins are cosmetics with a much larger
// catalogue and are not part of the "hidden and impossible to craft" set, so
// they stay opt-in.
constexpr bool kIncludeSkins = false;

// --------------------------------------------------------- item type tables
//
// Rilisoft.OfferItemType (dump2313.cs, TypeDefIndex 8931).
constexpr int32_t kTypeWeapon = 10;
constexpr int32_t kTypeArmor = 20;
constexpr int32_t kTypeMask = 30;
constexpr int32_t kTypeHat = 40;
constexpr int32_t kTypeBoots = 50;
constexpr int32_t kTypeCape = 60;
constexpr int32_t kTypeSkin = 65;
constexpr int32_t kTypeGadget = 70;

constexpr int32_t kSweptTypes[] = {
    kTypeWeapon, kTypeArmor, kTypeMask,   kTypeHat,
    kTypeBoots,  kTypeCape,  kTypeGadget, kTypeSkin,
};
constexpr int32_t kSweptTypeCount =
    static_cast<int32_t>(sizeof(kSweptTypes) / sizeof(kSweptTypes[0]));

// CategoryNames (dump2313.cs, TypeDefIndex 5416). The stock category -> type
// mapper decides what each one resolves to and anything outside the target
// set is skipped, so listing a category here can never widen the unlock.
constexpr int32_t kCategories[] = {
    0,      1,     2,     3,     4,     5,   // Primary … Premium weapons
    6,      7,     9,     10,    12,         // Hats, Armor, Capes, Boots, Masks
    8,                                       // Skins (filtered unless enabled)
    11,     12500, 13000, 13500,             // Gear, Throwing, Tools, Support
    35000,  40000, 45000,                    // Best weapons / wear / gadgets
    110000, 135000, 140000,                  // weapon / event / set craft lists
};
constexpr int32_t kCategoryCount =
    static_cast<int32_t>(sizeof(kCategories) / sizeof(kCategories[0]));

inline bool is_target_type(int32_t type) {
    if (type == kTypeSkin) return kIncludeSkins;
    for (int32_t i = 0; i < kSweptTypeCount; ++i) {
        if (kSweptTypes[i] == type) return true;
    }
    return false;
}

inline const char* type_label(int32_t type) {
    switch (type) {
        case kTypeWeapon: return "weapon";
        case kTypeArmor: return "armor";
        case kTypeMask: return "mask";
        case kTypeHat: return "hat";
        case kTypeBoots: return "boots";
        case kTypeCape: return "cape";
        case kTypeSkin: return "skin";
        case kTypeGadget: return "gadget";
        default: return "item";
    }
}

// ----------------------------------------------------------- metadata names

constexpr const char* kNamespace = "PGCompany";
constexpr const char* kProgressNs = "Progress";

// Item registry: the ownership source of truth.
constexpr const char* kRegistryClass = "上丞丅三业丙世不丙";
constexpr const char* kRegistryInstance = "下丌丑丁下丟丛丘上";  // static, 0 args

// Base item class of every registry item: carries the key and the item name.
constexpr const char* kItemBaseClass = "三丛丐丙丈丌丈专万";
constexpr const char* kItemKeyField = "<下丕三上丂三丝丅丐>k__BackingField";
constexpr const char* kItemNameField = "<世下丐不丞与丞七丄>k__BackingField";

// Static catalogue helper (extension class).
constexpr const char* kCatalogClass = "丄丝丘丆丈丆丝丆丄";
constexpr const char* kCatalogByCategory = "三与七丆丅丆丕丒业";  // static, 2 args
constexpr const char* kEntryKey = "丌丄丛丈与丝丑世丆";            // static, 1 arg
constexpr const char* kCategoryType = "丁丒丕丌丂丌且丙且";        // static, 1 arg

// Progress service: the grant transaction notifies it, so it must exist first.
constexpr const char* kProgressService = "东丝丂丄业丕且丙丑";
constexpr const char* kProgressInstance = "丞丏业丐丒与业丗与";  // static, 0 args

// ---------------------------------------------------------- verified offsets
//
// The three registry entry points below are overloaded by argument type only,
// so metadata name plus argument count cannot select the right overload. They
// are taken by RVA from the verified 23.1.3 ARM64 libil2cpp.so (ELF build id
// 57fcc18d2db06212416d480d53c0f881ee47c52a) and the base address is proven
// first: four unambiguous metadata targets must resolve to exactly
// base + their own RVA. If any check fails, nothing is armed.
constexpr uintptr_t kRegistryInstanceRva = 0x3046000u;
constexpr uintptr_t kCatalogByCategoryRva = 0x305C074u;
constexpr uintptr_t kEntryKeyRva = 0x30479D0u;
constexpr uintptr_t kCategoryTypeRva = 0x305C50Cu;
constexpr uintptr_t kRegistryItemsOfTypeRva = 0x3060030u;
constexpr uintptr_t kRegistryCountRva = 0x304F634u;
constexpr uintptr_t kRegistryGrantRva = 0x3062B08u;

// sizeof(Nullable<T>) for the two stock optional arguments. IL2CPP lays
// Nullable<T> out as { bool has_value; T value; }, so an all-zero buffer is a
// null optional and the callee substitutes its own default.
constexpr size_t kOwnedFilterSize = 24u;   // Nullable<丙与不与丟丂一东丟>
constexpr size_t kObtainCauseSize = 104u;  // Nullable<与专丂丕丌丅东丂东>

// ------------------------------------------------------------- managed ABI

using StaticObjFn = void* (*)(void* method);
using ListCountFn = int32_t (*)(void* list, void* method);
using ListItemFn = void* (*)(void* list, int32_t index, void* method);

// (this, OfferItemType, MethodInfo*) -> List<item>
using RegistryItemsOfTypeFn = void* (*)(void* self, int32_t type, void* method);
// (this, key, Nullable<owned-filter>*, MethodInfo*) -> owned count
using RegistryCountFn = int32_t (*)(void* self, void* key, void* filter,
                                    void* method);
// (this, key, Nullable<obtain-cause>*, Action, MethodInfo*) -> granted item
using RegistryGrantFn = void* (*)(void* self, void* key, void* cause,
                                  void* callback, void* method);

// (OfferItemType, CategoryNames, MethodInfo*) -> List<catalogue entry>
using CategoryItemsFn = void* (*)(int32_t type, int32_t category, void* method);
// (catalogue entry, MethodInfo*) -> item key
using EntryKeyFn = void* (*)(void* entry, void* method);
// (CategoryNames, MethodInfo*) -> OfferItemType
using CategoryTypeFn = int32_t (*)(int32_t category, void* method);

struct Managed {
    void* info = nullptr;
    void* ptr = nullptr;
    explicit operator bool() const noexcept {
        return info != nullptr && ptr != nullptr;
    }
};

inline bool bind(Managed& out, const char* namespaze, const char* klass,
                 const char* method, int args_count) {
    void* info = il2cpp::find_method_info(namespaze, klass, method, args_count);
    if (info == nullptr) {
        LOGE("23.1.3-hidden-items: %s::%s/%d not found in metadata", klass,
             method, args_count);
        return false;
    }
    void* ptr = il2cpp::method_pointer(info);
    if (ptr == nullptr) {
        LOGE("23.1.3-hidden-items: %s::%s/%d has no compiled body", klass,
             method, args_count);
        return false;
    }
    out.info = info;
    out.ptr = ptr;
    return true;
}

// List<T> is a generic instantiation, so its accessors are resolved off the
// concrete object instead of by namespace and name. The two stages walk two
// different instantiations (items and catalogue entries), so each one keeps
// its own resolved accessors.
struct ListApi {
    void* count_info = nullptr;
    void* count_ptr = nullptr;
    void* item_info = nullptr;
    void* item_ptr = nullptr;

    explicit operator bool() const noexcept {
        return count_ptr != nullptr && item_ptr != nullptr;
    }
};

inline bool resolve_list_api(void* list, ListApi& api) {
    if (list == nullptr || il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_method_from_name == nullptr) {
        return false;
    }
    void* klass = il2cpp::object_get_class(list);
    if (klass == nullptr) return false;

    api.count_info = il2cpp::class_get_method_from_name(klass, "get_Count", 0);
    api.item_info = il2cpp::class_get_method_from_name(klass, "get_Item", 1);
    if (api.count_info == nullptr || api.item_info == nullptr) return false;

    api.count_ptr = il2cpp::method_pointer(api.count_info);
    api.item_ptr = il2cpp::method_pointer(api.item_info);
    return static_cast<bool>(api);
}

// ------------------------------------------------------------------- state

enum class Stage : uint8_t { Registry, Category, Idle };

inline Managed g_registry_instance{};
inline Managed g_progress_service{};
inline Managed g_category_items{};
inline Managed g_entry_key{};
inline Managed g_category_type{};

inline RegistryItemsOfTypeFn g_items_of_type = nullptr;
inline RegistryCountFn g_registry_count = nullptr;
inline RegistryGrantFn g_registry_grant = nullptr;

inline void* g_key_field = nullptr;
inline void* g_name_field = nullptr;

inline ListApi g_item_list{};   // List<三丛丐丙丈丌丈专万>
inline ListApi g_entry_list{};  // List<丒专与三七丁丌丟丆>

inline uint64_t g_frames = 0;
inline uint64_t g_next_sweep = 0;
inline uint64_t g_grant_log = 0;
inline Stage g_stage = Stage::Registry;
inline int32_t g_slot = 0;    // index into kSweptTypes / kCategories
inline int32_t g_cursor = 0;  // index inside the current managed list
inline int32_t g_pass = 0;
inline int32_t g_seen = 0;
inline int32_t g_granted = 0;
inline int32_t g_already_owned = 0;
inline int32_t g_failed = 0;
inline int32_t g_total_granted = 0;
inline int32_t g_consecutive_failures = 0;
inline bool g_armed = false;
inline bool g_installed = false;

// ------------------------------------------------------------- diagnostics

inline bool should_log(uint64_t counter) {
    return counter <= kLogBurst || (counter % kLogPeriod) == 0u;
}

inline std::string item_name(void* item) {
    if (item == nullptr || g_name_field == nullptr ||
        il2cpp::field_get_value == nullptr) {
        return "<unknown>";
    }
    void* managed = nullptr;
    il2cpp::field_get_value(item, g_name_field, &managed);
    return il2cpp::to_utf8(managed, 48u);
}

// ------------------------------------------------------------ grant driver

inline void* item_key(void* item) {
    if (item == nullptr || g_key_field == nullptr ||
        il2cpp::field_get_value == nullptr) {
        return nullptr;
    }
    void* key = nullptr;
    il2cpp::field_get_value(item, g_key_field, &key);
    return key;
}

inline int32_t owned_count(void* registry, void* key) {
    if (g_registry_count == nullptr || registry == nullptr || key == nullptr) {
        return -1;
    }
    // A null Nullable<> keeps the stock "any owned instance" semantics.
    alignas(8) unsigned char filter[kOwnedFilterSize] = {};
    return g_registry_count(registry, key, filter, nullptr);
}

// Grants one missing definition. Returns true when the definition is owned
// after the call, which also covers definitions that were already owned.
inline bool ensure_owned(void* registry, void* key, const char* source,
                         int32_t type, const std::string& name) {
    if (key == nullptr || g_registry_grant == nullptr) {
        ++g_failed;
        ++g_consecutive_failures;
        return false;
    }

    const int32_t before = owned_count(registry, key);
    if (before >= 1) {
        ++g_already_owned;
        g_consecutive_failures = 0;
        return true;
    }
    if (before < 0) {
        ++g_failed;
        ++g_consecutive_failures;
        return false;
    }

    // A null obtain cause makes the stock transaction use its own default,
    // and no completion callback is needed.
    alignas(8) unsigned char cause[kObtainCauseSize] = {};
    g_registry_grant(registry, key, cause, nullptr, nullptr);

    const int32_t after = owned_count(registry, key);
    if (after < 1) {
        ++g_failed;
        ++g_consecutive_failures;
        LOGW("23.1.3-hidden-items: grant did not register %s '%s' (%s stage,"
             " count %" PRId32 " -> %" PRId32 ")",
             type_label(type), name.empty() ? "<key>" : name.c_str(), source,
             before, after);
        return false;
    }

    ++g_granted;
    ++g_total_granted;
    g_consecutive_failures = 0;
    ++g_grant_log;
    if (should_log(g_grant_log)) {
        LOGI("23.1.3-hidden-items: granted %s '%s' (%s stage, count 0 -> %"
             PRId32 ")",
             type_label(type), name.empty() ? "<key>" : name.c_str(), source,
             after);
    }
    return true;
}

// ------------------------------------------------------------ sweep driver

inline void* registry_instance() {
    if (!g_registry_instance) return nullptr;
    return reinterpret_cast<StaticObjFn>(g_registry_instance.ptr)(
        g_registry_instance.info);
}

inline bool progress_ready() {
    if (!g_progress_service) return false;
    return reinterpret_cast<StaticObjFn>(g_progress_service.ptr)(
               g_progress_service.info) != nullptr;
}

inline void begin_pass() {
    g_stage = Stage::Registry;
    g_slot = 0;
    g_cursor = 0;
    g_seen = 0;
    g_granted = 0;
    g_already_owned = 0;
    g_failed = 0;
}

inline void finish_pass() {
    ++g_pass;
    LOGI("23.1.3-hidden-items: pass %" PRId32 " complete (definitions seen=%"
         PRId32 " granted=%" PRId32 " already owned=%" PRId32 " failed=%"
         PRId32 ")",
         g_pass, g_seen, g_granted, g_already_owned, g_failed);

    const bool nothing_left = (g_granted == 0 && g_seen > 0);
    if (nothing_left || g_pass >= kMaxPasses) {
        LOGI("23.1.3-hidden-items: hidden weapon, wear and gadget inventory"
             " complete (%" PRId32 " definitions owned this pass, %" PRId32
             " granted in total)",
             g_already_owned + g_granted, g_total_granted);
        g_armed = false;
        return;
    }

    g_stage = Stage::Idle;
    g_next_sweep = g_frames + kRecheckFrames;
}

inline void advance_slot(int32_t slot_count) {
    g_cursor = 0;
    ++g_slot;
    if (g_slot < slot_count) return;

    g_slot = 0;
    if (g_stage == Stage::Registry) {
        g_stage = Stage::Category;
        return;
    }
    finish_pass();
}

// Returns the managed list for the current registry slot, or nullptr when the
// slot has nothing to sweep.
inline void* registry_stage_list(void* registry, int32_t& type) {
    type = kSweptTypes[g_slot];
    if (!is_target_type(type) || g_items_of_type == nullptr) return nullptr;
    return g_items_of_type(registry, type, nullptr);
}

// Returns the managed catalogue list for the current category slot, or
// nullptr when the category does not map to a targeted item type.
inline void* category_stage_list(int32_t& type) {
    const int32_t category = kCategories[g_slot];
    if (!g_category_type || !g_category_items) return nullptr;

    type = reinterpret_cast<CategoryTypeFn>(g_category_type.ptr)(
        category, g_category_type.info);
    if (!is_target_type(type)) return nullptr;

    return reinterpret_cast<CategoryItemsFn>(g_category_items.ptr)(
        type, category, g_category_items.info);
}

inline void run_sweep() {
    if (!g_armed) return;
    if (g_frames < kWarmupFrames) return;

    if (g_stage == Stage::Idle) {
        if (g_frames < g_next_sweep) return;
        begin_pass();
    }

    // The stock grant transaction notifies the Progress service, so it has to
    // be alive before the first write.
    if (!progress_ready()) return;

    void* registry = registry_instance();
    if (registry == nullptr) return;

    const bool registry_stage = (g_stage == Stage::Registry);
    const int32_t slots = registry_stage ? kSweptTypeCount : kCategoryCount;

    int32_t type = 0;
    void* list = registry_stage ? registry_stage_list(registry, type)
                                : category_stage_list(type);
    if (list == nullptr) {
        advance_slot(slots);
        return;
    }

    ListApi& api = registry_stage ? g_item_list : g_entry_list;
    if (!api && !resolve_list_api(list, api)) return;

    const int32_t total =
        reinterpret_cast<ListCountFn>(api.count_ptr)(list, api.count_info);
    if (total <= 0 || total > kMaxListEntries) {
        if (total > kMaxListEntries) {
            LOGW("23.1.3-hidden-items: %s list for %s reports %" PRId32
                 " entries; skipped as implausible",
                 registry_stage ? "registry" : "catalogue", type_label(type),
                 total);
        }
        advance_slot(slots);
        return;
    }

    int32_t processed = 0;
    while (g_cursor < total && processed < kGrantsPerTick) {
        void* entry =
            reinterpret_cast<ListItemFn>(api.item_ptr)(list, g_cursor,
                                                       api.item_info);
        ++g_seen;
        ++g_cursor;
        ++processed;

        if (registry_stage) {
            ensure_owned(registry, item_key(entry), "registry", type,
                         item_name(entry));
        } else {
            void* key = entry == nullptr
                            ? nullptr
                            : reinterpret_cast<EntryKeyFn>(g_entry_key.ptr)(
                                  entry, g_entry_key.info);
            ensure_owned(registry, key, "catalogue", type, std::string());
        }

        if (g_consecutive_failures >= kMaxConsecutiveFailures) {
            LOGW("23.1.3-hidden-items: %" PRId32 " consecutive grants failed;"
                 " disarming (granted=%" PRId32 " total)",
                 g_consecutive_failures, g_total_granted);
            g_armed = false;
            return;
        }
    }

    if (g_cursor >= total) advance_slot(slots);
}

// ------------------------------------------------------------- installation

// Proves that `base` really is the verified 23.1.3 libil2cpp.so image: an
// unambiguous target resolved through metadata must land on its recorded RVA.
// Only then may the overloaded registry entry points be taken by RVA.
inline bool verify_image(uintptr_t base, const Managed& target, uintptr_t rva,
                         const char* label) {
    const auto expected = reinterpret_cast<void*>(base + rva);
    if (target.ptr == expected) return true;
    LOGE("23.1.3-hidden-items: %s is at %p but RVA 0x%08" PRIxPTR " maps to"
         " %p; this is not the verified 23.1.3 ARM64 image",
         label, target.ptr, rva, expected);
    return false;
}

inline bool install(uintptr_t il2cpp_base) {
    if (g_installed) return true;

    bool resolved = true;
    resolved &= bind(g_registry_instance, kNamespace, kRegistryClass,
                     kRegistryInstance, 0);
    resolved &= bind(g_progress_service, kProgressNs, kProgressService,
                     kProgressInstance, 0);
    resolved &= bind(g_category_items, kNamespace, kCatalogClass,
                     kCatalogByCategory, 2);
    resolved &= bind(g_entry_key, kNamespace, kCatalogClass, kEntryKey, 1);
    resolved &= bind(g_category_type, kNamespace, kCatalogClass, kCategoryType,
                     1);
    if (!resolved) {
        LOGE("23.1.3-hidden-items: metadata does not match the expected 23.1.3"
             " build; nothing was armed");
        return false;
    }

    g_key_field = il2cpp::find_field(kNamespace, kItemBaseClass, kItemKeyField);
    g_name_field =
        il2cpp::find_field(kNamespace, kItemBaseClass, kItemNameField);
    if (g_key_field == nullptr) {
        LOGE("23.1.3-hidden-items: the base item key field is missing; nothing"
             " was armed");
        return false;
    }

    if (il2cpp_base == 0u) {
        LOGE("23.1.3-hidden-items: libil2cpp.so base address is unknown; the"
             " inventory grant cannot be armed");
        return false;
    }
    if (!verify_image(il2cpp_base, g_registry_instance, kRegistryInstanceRva,
                      "the item registry singleton") ||
        !verify_image(il2cpp_base, g_category_items, kCatalogByCategoryRva,
                      "the category catalogue getter") ||
        !verify_image(il2cpp_base, g_entry_key, kEntryKeyRva,
                      "the catalogue entry key accessor") ||
        !verify_image(il2cpp_base, g_category_type, kCategoryTypeRva,
                      "the category type mapper")) {
        return false;
    }

    g_items_of_type = reinterpret_cast<RegistryItemsOfTypeFn>(
        reinterpret_cast<void*>(il2cpp_base + kRegistryItemsOfTypeRva));
    g_registry_count = reinterpret_cast<RegistryCountFn>(
        reinterpret_cast<void*>(il2cpp_base + kRegistryCountRva));
    g_registry_grant = reinterpret_cast<RegistryGrantFn>(
        reinterpret_cast<void*>(il2cpp_base + kRegistryGrantRva));

    begin_pass();
    g_armed = true;
    g_installed = true;
    LOGI("23.1.3-hidden-items: armed: every hidden weapon, wear item and gadget"
         " the build ships is granted through the stock item inventory (%"
         PRId32 " per menu frame, %" PRId32 " sweeps max, skins %s)",
         kGrantsPerTick, kMaxPasses, kIncludeSkins ? "included" : "excluded");
    return true;
}

inline void pump() {
    if (!g_installed) return;
    ++g_frames;
    run_sweep();
}

}  // namespace detail

// Arms the hidden weapon / wear / gadget unlock. `il2cpp_base` is the load
// address of libil2cpp.so.
inline bool install_hooks(uintptr_t il2cpp_base) {
    return detail::install(il2cpp_base);
}

// Driven once per main-menu frame from progression_2313's MainMenuController
// .Update hook: the grant needs a game thread and a live main menu, and the
// menu Update slot is the only such point that is already owned by this port.
inline void pump_from_main_menu() { detail::pump(); }

}  // namespace hidden_items_2313
