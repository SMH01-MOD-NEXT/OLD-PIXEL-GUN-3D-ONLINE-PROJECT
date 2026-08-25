#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) hidden weapon, wear and gadget unlock - v5, wear included
//
// v1 granted every definition the build ships, one full stock inventory
// transaction at a time, and locked the main menu up for minutes on the first
// launch. v2 fixed the freeze by granting only definitions that no shop tab,
// craft list or event list offers, which quietly dropped every weapon the shop
// happens to sell. v3 restored the full weapon sweep behind a per-frame
// wall-clock budget.
//
// v3 did not freeze, but it was unusably slow, and the reason was its own
// pacing rather than the amount of work:
//
//   kGrantBudgetUs was 10 ms per frame, and note_grant_cost() treated *any*
//   grant costing more than that as a stall, doubling an exponential backoff
//   up to kBackoffMaxFrames = 90. A full inventory transaction always costs
//   more than 10 ms on real hardware, so the backoff saturated on the first
//   few grants and never came back down: the sweep settled at roughly one
//   weapon per 90 frames, i.e. 1.5 s each, i.e. about twenty minutes for ~800
//   weapons. The menu stayed perfectly responsive the whole time, which is
//   exactly how the defect was reported: not laggy, just insanely slow.
//
// So the budget was protecting a frame rate that was never in danger. v4 keeps
// the same mechanism and re-aims it:
//
//  1. The first pass is a bulk pass and is budgeted for throughput
//     (kBurstBudgetUs, kBurstGrantsPerFrame). Dropping the menu to ~8 fps for
//     a few seconds while the arsenal lands is the trade that was asked for.
//  2. The backoff is no longer armed by "this grant cost more than one frame".
//     It is armed only by kGrantStallUs, a genuinely pathological transaction
//     (a quarter of a second), and it is capped at kBackoffMaxFrames = 8 so it
//     can slow the sweep down without stopping it.
//  3. Later passes are re-checks over an inventory that is already full, so
//     they keep the gentle steady-state budget.
//  4. The sweep starts earlier (kWarmupFrames), so it overlaps the time the
//     player spends reading the main menu instead of starting after it.
//
// v5 extends that treatment from weapons to wear. Until now armor, masks,
// hats, boots and capes were still filtered by v2's rule "grant only what
// nothing offers", and the craft screen is exactly what that rule excludes: a
// wear piece priced at 250 gears *is* offered, by the craft list, so the sweep
// deliberately walked past it and left the player to pay. That is the same
// mistake the weapon filter made, so kGrantEveryWear now grants every wear
// definition the build ships, catalogue or not.
//
// Cost of that decision: wear is a wider catalogue than weapons (five types,
// much of it seasonal), so the bulk pass grows from roughly 800 transactions
// to a few thousand. At the 15-40 ms per transaction v4 measured, that is on
// the order of ten seconds of reduced frame rate on the main menu rather than
// the few seconds v4 needed. The progress log reports the real figure instead
// of this estimate.
//
// One thing gets cheaper per definition: a type that is granted whole needs no
// catalogue lookup at all, because the answer cannot change the decision. In
// v4 every wear definition paid for an is_offered() call whose only effect was
// to skip it.
//
// Gadgets keep the catalogue filter and skins stay opt-in (kIncludeSkins):
// neither was asked for, and both are a one-line flip if that changes.
//
// Where the time goes (BL scan of the shipped ARM64 code):
//
//   丘上丄三业丏丙不且(key, Nullable<cause>, Action)             0x3062B08  <- called here
//   └─ 丘上丄三业丏丙不且(List<key>, Nullable<cause>, Action)     0x3061C20
//      └─ 下万丗世丑万丌东东(List<key> give, List<key> take, cause)  0x3061DB0
//         ├─ Progress.东丝丂丄业丕且丙丑::丞丏业丐丒与业丗与()               0x1B3BA40
//         ├─ Progress.东丝丂丄业丕且丙丑::丂一丈东世业丆业丅(...)             0x1B44114
//         └─ Progress.东丝丂丄业丕且丙丑::丈且东丝丝东且丈专(Dictionary<string,object>)
//                                                             0x1B44230
//
// The single-key entry point is a thin wrapper: it allocates a one-element list
// and runs the complete transaction, which appends a profile-update command and
// re-serialises the whole pending command queue (PrUpCmKey) every time. The
// cost of item N therefore grows with N.
//
// The real end state is the batch entry point at 0x3061C20, which takes a
// List<key> and collapses the whole sweep into one transaction and one
// serialisation. It is still not called here, and the blocker is narrow and
// worth recording: il2cpp.h exposes no object_new and no generic-instantiation
// helper, so a managed List<丑一丘与丁丄专专专> cannot be constructed from native
// code. It can, however, be *borrowed*: several shipped methods return
// List<丑一丘与丁丄专专专> (for instance Progress.东丝丂丄业丕且丙丑::专与丁丞丂丁丐与丝(enum) at
// 0x1B4B0E0), and List<T>.GetRange(0, 0) on any of them yields a fresh, empty,
// correctly typed list that we own outright. That is the next step; it needs a
// device to validate the borrow source, so v4 deliberately shipped the pacing
// fix first, on code paths already known to work on the target hardware. v5
// keeps that decision, and widening the sweep to wear makes the batch call more
// valuable rather than less: it multiplies the number of transactions without
// changing what any one of them costs.
//
// Also kept: a targeted find-by-id probe 与丒丅丝丕丕丒丟丆(OfferItemType, string) at
// 0x3060088 reaches definitions the per-type enumeration may not list, and the
// gadget definitions are dumped to logcat once. 23.1.3 metadata contains no
// harpoon item id: Harpoon only exists as member 2 of the movement-gadget kind
// enum 专丟且东丐三丟丕业 and as weapon config fields (harpoonImpulse,
// harpoonMaxDistance, isHarpoonProjectile, [...(Harpoon)] public bool harpoon),
// so the real id can only come off the device. The dump makes that a one-line
// change once we see it.
//
// Unchanged safety model: fail closed. Nothing is patched, no game memory is
// written, only stock public calls are made, RVA-taken pointers are used only
// after the loaded libil2cpp.so is proven to be this exact build through four
// unambiguous metadata anchors, and every grant is guarded by an owned check so
// re-running the sweep can never duplicate an item.
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
#include <ctime>
#include <string>

#include "il2cpp.h"
#include "log.h"

namespace hidden_items_2313 {
namespace detail {

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

// ------------------------------------------------------------------ tunables

// weapon_modules_2313 starts its own sweep at menu frame 120 and needs about
// 25 frames for its 42 definitions. This port starts after that on purpose:
// both drive the same stock transaction and must not overlap on a single
// frame. 180 leaves a comfortable margin while still starting the bulk work
// inside the first few seconds of the menu.
constexpr uint64_t kWarmupFrames = 180;

// Read-only checks per main-menu frame (list access, owned count, catalogue
// lookup). These are cheap: no transaction, no persist, no allocation. This
// number also decides how fast a *second* launch skips over an inventory that
// is already complete.
constexpr int32_t kChecksPerTick = 64;

// Bulk pass (the first one): budgeted for throughput, not for frame rate. The
// menu is expected to drop to single-digit fps for a few seconds while the
// arsenal lands, which is the whole point.
constexpr uint64_t kBurstBudgetUs = 120000u;  // 120 ms per frame
constexpr int32_t kBurstGrantsPerFrame = 64;

// Steady state (re-check passes over an inventory that is already full).
constexpr uint64_t kGrantBudgetUs = 20000u;  // 20 ms per frame
constexpr int32_t kGrantsPerFrameCap = 8;

// A single transaction slower than this is pathological, not merely expensive,
// and is the only thing that arms the backoff. v3 armed it at the frame budget
// instead, which is why the backoff saturated and the sweep crawled.
constexpr uint64_t kGrantStallUs = 250000u;  // 250 ms

// The backoff may slow the sweep down; it may not stop it.
constexpr uint64_t kBackoffStartFrames = 2u;
constexpr uint64_t kBackoffMaxFrames = 8u;

// Full sweeps attempted in total (late registry population included).
constexpr int32_t kMaxPasses = 2;

// Frames between two sweeps (~60 s at 60 fps).
constexpr uint64_t kRecheckFrames = 3600;

// Consecutive failed grants after which the port disarms itself, so a layout
// mismatch degrades to a no-op instead of a per-frame spin.
constexpr int32_t kMaxConsecutiveFailures = 24;

// Sanity bound for a single managed list.
constexpr int32_t kMaxListEntries = 8192;

// Log the first kLogBurst grants in full, then every kLogPeriod-th one.
constexpr uint64_t kLogBurst = 12u;
constexpr uint64_t kLogPeriod = 64u;

// Running progress line every N successful grants, so the actual rate can be
// read off logcat instead of guessed at.
constexpr int32_t kRateLogEvery = 100;

// Character, weapon and armor skins are cosmetics with a much larger
// catalogue and are not part of the "hidden and impossible to craft" set, so
// they stay opt-in.
constexpr bool kIncludeSkins = false;

// Every weapon definition the build ships is granted, whether or not a shop
// tab, craft list or event list offers it. This is the v1 behaviour for
// weapons specifically, restored on purpose: on a private server the point is
// to own the whole arsenal, not only the parts nothing else sells.
constexpr bool kGrantEveryWeapon = true;

// The same for wear: armor, masks, hats, boots and capes. These are what the
// craft screen prices at 250 gears each, so the catalogue filter classified
// them as "offered, leave them to the shop" and the sweep walked straight past
// them -- which is why the arsenal filled up while the wardrobe stayed empty.
// On a private server owning the whole wardrobe is the point, exactly as it is
// for the whole arsenal.
//
// Gadgets are deliberately not included here: they were not asked for, and the
// gadget catalogue is also where the one-shot id dump below is still doing
// useful diagnostic work.
constexpr bool kGrantEveryWear = true;

// Set to true to restore the v1 behaviour for *every* item type (gadgets and,
// if enabled, skins included), now paced. The driver also flips this on by
// itself for one retry if the unobtainable-only filter selects nothing at all,
// so a wrong assumption about the catalogue degrades into "slower but
// complete" instead of "does nothing".
constexpr bool kGrantEverything = false;
constexpr int32_t kFallbackMinDefinitions = 64;

// One-shot dump of every gadget definition id to logcat. This is how the
// harpoon gets identified: 23.1.3 metadata has no harpoon item id, so the ids
// have to be read off a running device (adb logcat -s OPG3D).
constexpr bool kDumpGadgetIds = true;
constexpr int32_t kDumpLimit = 96;

// Ids that are always granted when they exist, whatever the catalogue says, so
// that items which already work for players today can never regress. Matched
// case-insensitively as a substring of the definition name.
constexpr const char* kAlwaysGrantIds[] = {
    "ultimatum",
    "locator",
    "harpoon",
};
constexpr int32_t kAlwaysGrantCount =
    static_cast<int32_t>(sizeof(kAlwaysGrantIds) / sizeof(kAlwaysGrantIds[0]));

// Ids probed directly through the registry find-by-id entry point, for
// definitions the per-type enumeration may not list. Cheap: a lookup is not a
// transaction.
constexpr const char* kProbeIds[] = {
    "Harpoon", "HarpoonGun", "Harpoon_1", "HarpoonGun_1", "harpoon",
};
constexpr int32_t kProbeIdCount =
    static_cast<int32_t>(sizeof(kProbeIds) / sizeof(kProbeIds[0]));
constexpr int32_t kMaxProbeGrants = 8;

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

// The five wear slots. A predicate rather than a table because it is asked
// once per definition inside the sweep loop.
inline bool is_wear_type(int32_t type) {
    return type == kTypeArmor || type == kTypeMask || type == kTypeHat ||
           type == kTypeBoots || type == kTypeCape;
}

// Weapons first, then the five wear slots: those are the two groups that are
// granted whole, so they are both the bulk of the work and the part the player
// checks first. Gadgets and skins follow because they are filtered (cheap)
// rather than granted whole.
constexpr int32_t kSweptTypes[] = {
    kTypeWeapon, kTypeArmor, kTypeMask,   kTypeHat,
    kTypeBoots,  kTypeCape,  kTypeGadget, kTypeSkin,
};
constexpr int32_t kSweptTypeCount =
    static_cast<int32_t>(sizeof(kSweptTypes) / sizeof(kSweptTypes[0]));

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

// Static catalogue helper (extension class). Two of its members are only bound
// to prove the image (see verify_image) and are never called here.
constexpr const char* kCatalogClass = "丄丝丘丆丈丆丝丆丄";
constexpr const char* kCatalogByCategory = "三与七丆丅丆丕丒业";  // static, 2 args
constexpr const char* kEntryKey = "丌丄丛丈与丝丑世丆";            // static, 1 arg
constexpr const char* kCategoryType = "丁丒丕丌丂丌且丙且";        // static, 1 arg

// Progress service: the grant transaction notifies it, so it must exist first.
constexpr const char* kProgressService = "东丝丂丄业丕且丙丑";
constexpr const char* kProgressInstance = "丞丏业丐丒与业丗与";  // static, 0 args

// ---------------------------------------------------------- verified offsets
//
// The entry points below are overloaded by argument type only, so metadata
// name plus argument count cannot select the right overload. They are taken by
// RVA from the verified 23.1.3 ARM64 libil2cpp.so (ELF build id
// 57fcc18d2db06212416d480d53c0f881ee47c52a) and the base address is proven
// first: four unambiguous metadata targets must resolve to exactly
// base + their own RVA. If any check fails, nothing is armed.
constexpr uintptr_t kRegistryInstanceRva = 0x3046000u;
constexpr uintptr_t kCatalogByCategoryRva = 0x305C074u;
constexpr uintptr_t kEntryKeyRva = 0x30479D0u;
constexpr uintptr_t kCategoryTypeRva = 0x305C50Cu;

// 丈丂丆丙丂一七丞丌(OfferItemType) -> List<item>, the unfiltered definition list
// (not 万下丘丗丈万业世世 at 0x305C330, which is owned-filtered).
constexpr uintptr_t kRegistryItemsOfTypeRva = 0x3060030u;
// 丙丛业丐丐七丛不丂(key, Nullable<filter>) -> owned count.
constexpr uintptr_t kRegistryCountRva = 0x304F634u;
// 丘上丄三业丏丙不且(key, Nullable<cause>, Action) -> item, the stock grant.
constexpr uintptr_t kRegistryGrantRva = 0x3062B08u;
// 丘上丄三业丏丙不且(List<key>, Nullable<cause>, Action) -> List<item>, the batch
// grant. Recorded for the follow-up work described in the header comment; not
// called yet, because a managed List<key> cannot be constructed from native
// code with the current il2cpp.h surface.
constexpr uintptr_t kRegistryGrantBatchRva = 0x3061C20u;
// 与丒丅丝丕丕丒丟丆(OfferItemType, string) -> item, find a definition by id.
constexpr uintptr_t kRegistryFindByIdRva = 0x3060088u;
// 与丅丟七与丌东丙丌(item) -> catalogue entry, null when nothing offers the item.
// (The 0x305C640 overload takes a key instead and is not used here.)
constexpr uintptr_t kCatalogEntryForItemRva = 0x305C6C0u;

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
// (this, OfferItemType, string id, MethodInfo*) -> item
using RegistryFindByIdFn = void* (*)(void* self, int32_t type, void* id,
                                     void* method);
// (item, MethodInfo*) -> catalogue entry
using CatalogEntryForItemFn = void* (*)(void* item, void* method);

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
// concrete object instead of by namespace and name.
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

enum class Stage : uint8_t { Probe, Sweep, Idle };

inline Managed g_registry_instance{};
inline Managed g_progress_service{};
inline Managed g_category_items{};  // image proof only
inline Managed g_entry_key{};       // image proof only
inline Managed g_category_type{};   // image proof only

inline RegistryItemsOfTypeFn g_items_of_type = nullptr;
inline RegistryCountFn g_registry_count = nullptr;
inline RegistryGrantFn g_registry_grant = nullptr;
inline RegistryFindByIdFn g_find_by_id = nullptr;
inline CatalogEntryForItemFn g_catalog_entry = nullptr;

inline void* g_key_field = nullptr;
inline void* g_name_field = nullptr;

inline ListApi g_item_list{};  // List<三丛丐丙丈丌丈专万>

inline uint64_t g_frames = 0;
inline uint64_t g_next_sweep = 0;
inline uint64_t g_grant_log = 0;
inline uint64_t g_next_grant_frame = 0;
inline uint64_t g_backoff_frames = 0;
inline uint64_t g_worst_grant_us = 0;
inline uint64_t g_frame_spent_us = 0;
inline uint64_t g_total_grant_us = 0;
inline uint64_t g_sweep_started_us = 0;
inline int32_t g_stalled_grants = 0;
inline int32_t g_grants_this_frame = 0;
inline Stage g_stage = Stage::Probe;
inline int32_t g_slot = 0;    // index into kSweptTypes
inline int32_t g_cursor = 0;  // index inside the current managed list
inline int32_t g_pass = 0;
inline int32_t g_seen = 0;
inline int32_t g_granted = 0;
inline int32_t g_already_owned = 0;
inline int32_t g_offered_skipped = 0;
inline int32_t g_candidates = 0;
inline int32_t g_failed = 0;
inline int32_t g_total_granted = 0;
inline int32_t g_consecutive_failures = 0;
inline int32_t g_type_seen = 0;
inline int32_t g_type_granted = 0;
inline int32_t g_type_owned = 0;
inline int32_t g_type_hidden = 0;
inline int32_t g_dumped = 0;
inline int32_t g_probe_grants = 0;
inline bool g_grant_everything = kGrantEverything;
inline bool g_armed = false;
inline bool g_installed = false;

// ------------------------------------------------------------- diagnostics

inline uint64_t now_us() {
    struct timespec ts {};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0u;
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ull +
           static_cast<uint64_t>(ts.tv_nsec) / 1000ull;
}

inline bool should_log(uint64_t counter) {
    return counter <= kLogBurst || (counter % kLogPeriod) == 0u;
}

inline std::string item_name(void* item) {
    if (item == nullptr || g_name_field == nullptr ||
        il2cpp::field_get_value == nullptr) {
        return std::string();
    }
    void* managed = nullptr;
    il2cpp::field_get_value(item, g_name_field, &managed);
    return il2cpp::to_utf8(managed, 48u);
}

inline const char* display_name(const std::string& name) {
    return name.empty() ? "<key>" : name.c_str();
}

inline bool contains_ci(const std::string& haystack, const char* needle) {
    const size_t needle_len = std::strlen(needle);
    if (needle_len == 0u || haystack.size() < needle_len) return false;
    for (size_t start = 0; start + needle_len <= haystack.size(); ++start) {
        size_t i = 0;
        for (; i < needle_len; ++i) {
            unsigned char a = static_cast<unsigned char>(haystack[start + i]);
            unsigned char b = static_cast<unsigned char>(needle[i]);
            if (a >= 'A' && a <= 'Z') a = static_cast<unsigned char>(a + 32);
            if (b >= 'A' && b <= 'Z') b = static_cast<unsigned char>(b + 32);
            if (a != b) break;
        }
        if (i == needle_len) return true;
    }
    return false;
}

inline bool is_always_granted(const std::string& name) {
    for (int32_t i = 0; i < kAlwaysGrantCount; ++i) {
        if (contains_ci(name, kAlwaysGrantIds[i])) return true;
    }
    return false;
}

// ------------------------------------------------------------ item helpers

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

// Is this definition offered anywhere the player can reach (shop tab, craft
// list, event list)? Returns 1 yes, 0 no, -1 unknown.
inline int32_t is_offered(void* item) {
    if (item == nullptr || g_catalog_entry == nullptr) return -1;
    return g_catalog_entry(item, nullptr) != nullptr ? 1 : 0;
}

// Weapons and wear are granted in full: the v1 behaviour, restored on purpose
// for both. Gadgets keep the catalogue filter, so definitions the player can
// already buy or craft are left alone there -- granting absolutely everything
// is what made v1 take minutes, and gadgets were not what was asked for.
//
// This predicate is also what makes the wider v5 sweep cheaper per definition
// than the filtered v4 one: a type that is granted whole needs no catalogue
// lookup, because the answer cannot change the decision.
inline bool grants_whole_type(int32_t type) {
    if (g_grant_everything) return true;
    if (kGrantEveryWeapon && type == kTypeWeapon) return true;
    if (kGrantEveryWear && is_wear_type(type)) return true;
    return false;
}

// Human-readable form of the above, for the arm line.
inline const char* grant_scope_label() {
    if (g_grant_everything) return "every definition the build ships";
    if (kGrantEveryWeapon && kGrantEveryWear) {
        return "every weapon and every wear piece the build ships, plus gadgets"
               " no shop or craft list offers";
    }
    if (kGrantEveryWeapon) {
        return "every weapon the build ships, plus wear and gadgets no shop or"
               " craft list offers";
    }
    if (kGrantEveryWear) {
        return "every wear piece the build ships, plus weapons and gadgets no"
               " shop or craft list offers";
    }
    return "definitions no shop or craft list offers";
}

inline bool wants_grant(int32_t type, int32_t offered,
                        const std::string& name) {
    if (grants_whole_type(type)) return true;
    if (is_always_granted(name)) return true;
    return offered == 0;  // unknown (-1) is treated as offered: do nothing
}

// ------------------------------------------------------------ grant driver

inline bool grant_allowed_now() { return g_frames >= g_next_grant_frame; }

// The first pass is bulk work and is budgeted for throughput; every later pass
// is a re-check over an inventory that should already be full, so it goes back
// to being invisible.
inline bool bulk_phase() { return g_pass == 0; }

inline uint64_t frame_budget_us() {
    return bulk_phase() ? kBurstBudgetUs : kGrantBudgetUs;
}

inline int32_t frame_grant_cap() {
    return bulk_phase() ? kBurstGrantsPerFrame : kGrantsPerFrameCap;
}

// True once this frame has spent its transaction budget, so the sweep must
// suspend and resume on the next frame with its cursor in place.
inline bool frame_budget_spent() {
    return g_frame_spent_us >= frame_budget_us() ||
           g_grants_this_frame >= frame_grant_cap();
}

inline void note_grant_cost(uint64_t cost_us) {
    if (cost_us > g_worst_grant_us) g_worst_grant_us = cost_us;
    g_frame_spent_us += cost_us;
    g_total_grant_us += cost_us;
    ++g_grants_this_frame;

    // Only a pathological transaction arms the backoff. An ordinary grant that
    // happens to be more expensive than one frame is normal for this API and
    // must not throttle the sweep: that mistake is what made v3 crawl.
    if (cost_us > kGrantStallUs) {
        ++g_stalled_grants;
        g_backoff_frames = (g_backoff_frames == 0u)
                               ? kBackoffStartFrames
                               : g_backoff_frames * 2u;
        if (g_backoff_frames > kBackoffMaxFrames) {
            g_backoff_frames = kBackoffMaxFrames;
        }
        g_next_grant_frame = g_frames + 1u + g_backoff_frames;
        return;
    }

    if (g_backoff_frames > 0u) g_backoff_frames /= 2u;

    // Another grant may run in this same frame for as long as
    // frame_budget_spent() still says there is room.
    g_next_grant_frame = g_frames;
}

// Runs the stock grant for one definition that is known to be missing, then
// re-reads the owned count to verify it registered.
inline bool grant_missing(void* registry, void* key, int32_t type,
                          const std::string& name) {
    if (key == nullptr || g_registry_grant == nullptr) {
        ++g_failed;
        ++g_consecutive_failures;
        return false;
    }

    // A null obtain cause makes the stock transaction use its own default,
    // and no completion callback is needed.
    alignas(8) unsigned char cause[kObtainCauseSize] = {};
    const uint64_t started = now_us();
    if (g_sweep_started_us == 0u) g_sweep_started_us = started;
    g_registry_grant(registry, key, cause, nullptr, nullptr);
    const uint64_t cost = now_us() - started;
    note_grant_cost(cost);

    const int32_t after = owned_count(registry, key);
    if (after < 1) {
        ++g_failed;
        ++g_consecutive_failures;
        LOGW("23.1.3-hidden-items: grant did not register %s '%s' (count 0 ->"
             " %" PRId32 ", %" PRIu64 " us)",
             type_label(type), display_name(name), after, cost);
        return false;
    }

    ++g_granted;
    ++g_type_granted;
    ++g_total_granted;
    g_consecutive_failures = 0;
    ++g_grant_log;
    if (should_log(g_grant_log)) {
        LOGI("23.1.3-hidden-items: granted %s '%s' (%" PRIu64 " us, %" PRId32
             " this frame, backoff %" PRIu64 " frames)",
             type_label(type), display_name(name), cost, g_grants_this_frame,
             g_backoff_frames);
    }

    if (kRateLogEvery > 0 && (g_total_granted % kRateLogEvery) == 0) {
        const uint64_t elapsed = now_us() - g_sweep_started_us;
        LOGI("23.1.3-hidden-items: progress: %" PRId32 " granted in %" PRIu64
             " ms wall clock (%" PRIu64 " ms inside transactions, avg %" PRIu64
             " us each, %" PRId32 " stalls)",
             g_total_granted, elapsed / 1000ull, g_total_grant_us / 1000ull,
             g_total_grant_us / static_cast<uint64_t>(g_total_granted),
             g_stalled_grants);
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
    g_stage = (g_pass == 0) ? Stage::Probe : Stage::Sweep;
    g_slot = 0;
    g_cursor = 0;
    g_seen = 0;
    g_granted = 0;
    g_already_owned = 0;
    g_offered_skipped = 0;
    g_candidates = 0;
    g_failed = 0;
    g_type_seen = 0;
    g_type_granted = 0;
    g_type_owned = 0;
    g_type_hidden = 0;
}

inline void finish_pass() {
    ++g_pass;
    const uint64_t elapsed =
        g_sweep_started_us == 0u ? 0u : (now_us() - g_sweep_started_us);
    LOGI("23.1.3-hidden-items: pass %" PRId32 " complete (seen=%" PRId32
         " already owned=%" PRId32 " wanted=%" PRId32 " granted=%" PRId32
         " left to the shop=%" PRId32 " failed=%" PRId32 ") in %" PRIu64
         " ms wall clock, %" PRIu64 " ms inside transactions, worst grant %"
         PRIu64 " us, %" PRId32 " stalls",
         g_pass, g_seen, g_already_owned, g_candidates, g_granted,
         g_offered_skipped, g_failed, elapsed / 1000ull,
         g_total_grant_us / 1000ull, g_worst_grant_us, g_stalled_grants);

    // Safety net: if not a single definition was selected even though the
    // build clearly ships plenty, the catalogue assumption is wrong for this
    // profile. Retry once granting everything, which is paced and no longer
    // freezes the menu, instead of silently doing nothing.
    if (!g_grant_everything && g_candidates == 0 &&
        g_seen >= kFallbackMinDefinitions) {
        LOGW("23.1.3-hidden-items: none of the %" PRId32 " definitions was"
             " selected for a grant; retrying with the full inventory sweep",
             g_seen);
        g_grant_everything = true;
        g_stage = Stage::Idle;
        g_next_sweep = g_frames + kRecheckFrames;
        return;
    }

    const bool nothing_left = (g_granted == 0 && g_seen > 0);
    if (nothing_left || g_pass >= kMaxPasses) {
        LOGI("23.1.3-hidden-items: weapon, wear and gadget inventory complete"
             " (%" PRId32 " granted in total, %" PRId32 " already owned)",
             g_total_granted, g_already_owned);
        g_armed = false;
        return;
    }

    g_stage = Stage::Idle;
    g_next_sweep = g_frames + kRecheckFrames;
}

inline void advance_slot() {
    if (g_type_seen > 0) {
        LOGI("23.1.3-hidden-items: %s: %" PRId32 " definitions, %" PRId32
             " already owned, %" PRId32 " wanted, %" PRId32 " granted",
             type_label(kSweptTypes[g_slot]), g_type_seen, g_type_owned,
             g_type_hidden, g_type_granted);
    }
    g_type_seen = 0;
    g_type_granted = 0;
    g_type_owned = 0;
    g_type_hidden = 0;

    g_cursor = 0;
    ++g_slot;
    if (g_slot >= kSweptTypeCount) {
        g_slot = 0;
        finish_pass();
    }
}

// One type per frame: ask the registry directly for the ids that the per-type
// enumeration may not list (the harpoon is the reason this exists).
inline void run_probe() {
    void* registry = registry_instance();
    if (registry == nullptr || g_find_by_id == nullptr ||
        il2cpp::string_new == nullptr) {
        g_stage = Stage::Sweep;
        g_slot = 0;
        return;
    }

    const int32_t type = kSweptTypes[g_slot];
    if (is_target_type(type)) {
        for (int32_t i = 0; i < kProbeIdCount; ++i) {
            void* id = il2cpp::string_new(kProbeIds[i]);
            if (id == nullptr) continue;
            void* item = g_find_by_id(registry, type, id, nullptr);
            if (item == nullptr) continue;

            const std::string name = item_name(item);
            void* key = item_key(item);
            const int32_t owned = owned_count(registry, key);
            LOGI("23.1.3-hidden-items: probe found %s id '%s' (name '%s', owned"
                 " %" PRId32 ")",
                 type_label(type), kProbeIds[i], display_name(name), owned);
            if (owned == 0 && g_probe_grants < kMaxProbeGrants) {
                ++g_probe_grants;
                ++g_candidates;
                grant_missing(registry, key, type, name);
            }
        }
    }

    ++g_slot;
    if (g_slot >= kSweptTypeCount) {
        g_slot = 0;
        g_stage = Stage::Sweep;
    }
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

    if (g_stage == Stage::Probe) {
        run_probe();
        return;
    }

    void* registry = registry_instance();
    if (registry == nullptr) return;

    const int32_t type = kSweptTypes[g_slot];
    if (!is_target_type(type) || g_items_of_type == nullptr) {
        advance_slot();
        return;
    }

    void* list = g_items_of_type(registry, type, nullptr);
    if (list == nullptr) {
        advance_slot();
        return;
    }
    if (!g_item_list && !resolve_list_api(list, g_item_list)) return;

    const int32_t total = reinterpret_cast<ListCountFn>(g_item_list.count_ptr)(
        list, g_item_list.count_info);
    if (total <= 0 || total > kMaxListEntries) {
        if (total > kMaxListEntries) {
            LOGW("23.1.3-hidden-items: registry list for %s reports %" PRId32
                 " entries; skipped as implausible",
                 type_label(type), total);
        }
        advance_slot();
        return;
    }

    const bool dump_type = kDumpGadgetIds && type == kTypeGadget;
    // A type that is granted whole needs no catalogue lookup at all: the
    // answer cannot change the decision.
    const bool whole_type = grants_whole_type(type);
    int32_t checks = 0;

    while (g_cursor < total && checks < kChecksPerTick) {
        void* item = reinterpret_cast<ListItemFn>(g_item_list.item_ptr)(
            list, g_cursor, g_item_list.item_info);
        void* key = item_key(item);
        const int32_t owned = owned_count(registry, key);

        const bool need_name = dump_type || owned == 0;
        const std::string name = need_name ? item_name(item) : std::string();
        const int32_t offered =
            (need_name && (dump_type || !whole_type)) ? is_offered(item) : -1;
        const bool grant_it = (owned == 0) && wants_grant(type, offered, name);

        // A grant is the only expensive step, so it waits for its budget. The
        // cursor stays put, and this definition is retried on a later frame.
        if (grant_it && (!grant_allowed_now() || frame_budget_spent())) return;

        ++g_seen;
        ++g_type_seen;
        ++g_cursor;
        ++checks;

        if (dump_type && g_dumped < kDumpLimit) {
            ++g_dumped;
            LOGI("23.1.3-hidden-items: gadget definition '%s' (%s, owned %"
                 PRId32 ")",
                 display_name(name),
                 offered == 1 ? "offered" : (offered == 0 ? "not offered"
                                                          : "catalogue unknown"),
                 owned);
        }

        if (owned >= 1) {
            ++g_already_owned;
            ++g_type_owned;
            g_consecutive_failures = 0;
            continue;
        }
        if (owned < 0) {
            ++g_failed;
            ++g_consecutive_failures;
            if (g_consecutive_failures >= kMaxConsecutiveFailures) break;
            continue;
        }
        if (!grant_it) {
            ++g_offered_skipped;
            continue;
        }

        ++g_candidates;
        ++g_type_hidden;
        grant_missing(registry, key, type, name);

        if (g_consecutive_failures >= kMaxConsecutiveFailures) break;

        // Cheap grants keep going inside this frame; the moment the frame
        // budget is gone the sweep suspends here and resumes next frame.
        if (frame_budget_spent()) return;
    }

    if (g_consecutive_failures >= kMaxConsecutiveFailures) {
        LOGW("23.1.3-hidden-items: %" PRId32 " consecutive failures; disarming"
             " (granted=%" PRId32 " total)",
             g_consecutive_failures, g_total_granted);
        g_armed = false;
        return;
    }

    if (g_cursor >= total) advance_slot();
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
    g_find_by_id = reinterpret_cast<RegistryFindByIdFn>(
        reinterpret_cast<void*>(il2cpp_base + kRegistryFindByIdRva));
    g_catalog_entry = reinterpret_cast<CatalogEntryForItemFn>(
        reinterpret_cast<void*>(il2cpp_base + kCatalogEntryForItemRva));

    begin_pass();
    g_armed = true;
    g_installed = true;
    LOGI("23.1.3-hidden-items: armed: %s are granted through the stock item"
         " inventory, starting at menu frame %" PRIu64 " (bulk budget %" PRIu64
         " us and max %" PRId32 " transactions per frame, steady state %"
         PRIu64 " us and %" PRId32 ", backoff only above %" PRIu64
         " us, skins %s)",
         grant_scope_label(), kWarmupFrames, kBurstBudgetUs,
         kBurstGrantsPerFrame, kGrantBudgetUs, kGrantsPerFrameCap,
         kGrantStallUs, kIncludeSkins ? "included" : "excluded");
    return true;
}

inline void pump() {
    if (!g_installed) return;
    ++g_frames;
    g_frame_spent_us = 0u;
    g_grants_this_frame = 0;
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
