#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) hidden weapon, wear and gadget unlock - v3, stall free
//
// v1 worked (Ultimatum, Locator and the hidden wear pieces show up and stay),
// but it granted every definition the build ships, one full stock inventory
// transaction at a time, and that locked the main menu up for minutes on the
// first launch. For a private server that is a real defect and not a nitpick:
// an ordinary player reads a frozen menu as malware and uninstalls.
//
// Where the time actually went (BL scan of the shipped ARM64 code):
//
//   丘上丄三业丏丙不且(key, Nullable<cause>, Action)             0x3062B08  <- v1 called this
//   └─ 丘上丄三业丏丙不且(List<key>, Nullable<cause>, Action)     0x3061C20
//      └─ 下万丗世丑万丌东东(List<key> give, List<key> take, cause)  0x3061DB0
//         ├─ Progress.东丝丂丄业丕且丙丑::丞丏业丐丒与业丗与()               0x1B3BA40
//         ├─ Progress.东丝丂丄业丕且丙丑::丂一丈东世业丆业丅(...)             0x1B44114
//         └─ Progress.东丝丂丄业丕且丙丑::丈且东丝丝东且丈专(Dictionary<string,object>)
//                                                             0x1B44230
//
// The single-key entry point is only a thin wrapper: it allocates a one-element
// list and runs the complete transaction, which appends a profile-update
// command and re-serialises the whole pending command queue (PrUpCmKey) every
// single time. The cost of item N therefore grows with N, so granting ~1500
// definitions is quadratic work: minutes of stalling. Pacing by frame count
// (v1 kGrantsPerTick) cannot help, because the cost lives inside one
// transaction rather than across frames.
//
// v2 attacked the transaction count instead: only definitions that no shop
// tab, craft list or event list offers were granted, which cut ~1500
// transactions down to a few dozen.
//
// v3 restores the full weapon sweep, because v2 solved the freeze by also
// dropping something that was wanted: every weapon the build ships should be
// in the player's hands on a private server, whether or not the shop happens
// to sell it. The freeze does not come back, because the cost is now bounded
// from two sides at once:
//
//  1. kGrantEveryWeapon grants every weapon definition, catalogue or not.
//     Wear, gadgets and skins keep the cheap v2 filter, so the extra work is
//     paid only where it was asked for.
//  2. Grants are budgeted in wall-clock time per frame (kGrantBudgetUs), not
//     per transaction. Several cheap grants may share one frame while the
//     budget lasts; the moment it is spent the sweep suspends with its cursor
//     in place and resumes on the next frame. A grant that blows the budget on
//     its own still triggers the exponential backoff, so the late (expensive)
//     part of the sweep degrades into a slow trickle rather than a freeze.
//
// That combination matters: v2's "at most one transaction per frame, whatever
// it cost" rule was safe for a few dozen grants, but across ~800 weapons the
// mandatory one-frame gap plus a saturating backoff would have stretched the
// sweep over an hour. Letting cheap grants share a frame keeps the early bulk
// fast, and the budget keeps the late, expensive grants from stuttering.
//
// Also kept from v2, for the missing harpoon: a targeted find-by-id probe
// 与丒丅丝丕丕丒丟丆(OfferItemType, string) at 0x3060088 reaches definitions that the
// per-type enumeration may not list at all, and the gadget definitions are
// dumped to logcat once. 23.1.3 metadata contains no harpoon item id: Harpoon
// only exists as member 2 of the movement-gadget kind enum 专丟且东丐三丟丕业 and as
// weapon config fields (harpoonImpulse, harpoonMaxDistance, isHarpoonProjectile,
// [...(Harpoon)] public bool harpoon), so the real id can only come off the
// device. The dump makes that a one-line change once we see it.
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
// 25 frames for its 42 definitions. This port starts later on purpose: both
// drive the same stock transaction and must not overlap on a single frame.
constexpr uint64_t kWarmupFrames = 300;

// Read-only checks per main-menu frame (list access, owned count, catalogue
// lookup). These are cheap: no transaction, no persist, no allocation.
constexpr int32_t kChecksPerTick = 24;

// Wall-clock grant budget per main-menu frame. Cheap grants share a frame
// until the budget is spent; a single grant that costs more than this on its
// own also arms the backoff below.
constexpr uint64_t kGrantBudgetUs = 10000u;  // 10 ms

// Hard cap on transactions per frame, so a device that reports implausibly
// cheap grants still cannot run an unbounded number of them in one frame.
constexpr int32_t kGrantsPerFrameCap = 4;

constexpr uint64_t kBackoffStartFrames = 6u;
constexpr uint64_t kBackoffMaxFrames = 90u;  // ~1.5 s at 60 fps

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
constexpr uint64_t kLogPeriod = 16u;

// Character, weapon and armor skins are cosmetics with a much larger
// catalogue and are not part of the "hidden and impossible to craft" set, so
// they stay opt-in.
constexpr bool kIncludeSkins = false;

// Every weapon definition the build ships is granted, whether or not a shop
// tab, craft list or event list offers it. This is the v1 behaviour for
// weapons specifically, restored on purpose: on a private server the point is
// to own the whole arsenal, not only the parts nothing else sells.
constexpr bool kGrantEveryWeapon = true;

// Set to true to restore the v1 behaviour for *every* item type (wear,
// gadgets and, if enabled, skins included), now paced. The driver also flips
// this on by itself for one retry if the unobtainable-only filter selects
// nothing at all, so a wrong assumption about the catalogue degrades into
// "slower but complete" instead of "does nothing".
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

constexpr int32_t kSweptTypes[] = {
    kTypeGadget, kTypeWeapon, kTypeArmor, kTypeMask,
    kTypeHat,    kTypeBoots,  kTypeCape,  kTypeSkin,
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
// to prove the image (see verify_image) and are never called by v2.
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

// Weapons are granted in full (v1 behaviour, restored on purpose). For every
// other type the catalogue filter stays: definitions the player can already
// buy or craft are left alone, because granting them is what made v1 take
// minutes and it was never the point for wear and gadgets.
inline bool grants_whole_type(int32_t type) {
    if (g_grant_everything) return true;
    return kGrantEveryWeapon && type == kTypeWeapon;
}

inline bool wants_grant(int32_t type, int32_t offered,
                        const std::string& name) {
    if (grants_whole_type(type)) return true;
    if (is_always_granted(name)) return true;
    return offered == 0;  // unknown (-1) is treated as offered: do nothing
}

// ------------------------------------------------------------ grant driver

inline bool grant_allowed_now() { return g_frames >= g_next_grant_frame; }

// True once this frame has spent its transaction budget, so the sweep must
// suspend and resume on the next frame with its cursor in place.
inline bool frame_budget_spent() {
    return g_frame_spent_us >= kGrantBudgetUs ||
           g_grants_this_frame >= kGrantsPerFrameCap;
}

inline void note_grant_cost(uint64_t cost_us) {
    if (cost_us > g_worst_grant_us) g_worst_grant_us = cost_us;
    g_frame_spent_us += cost_us;
    ++g_grants_this_frame;

    if (cost_us > kGrantBudgetUs) {
        // One transaction alone outgrew a whole frame: back off hard, and do
        // not attempt another grant until the backoff has elapsed.
        g_backoff_frames = (g_backoff_frames == 0u)
                               ? kBackoffStartFrames
                               : g_backoff_frames * 2u;
        if (g_backoff_frames > kBackoffMaxFrames) {
            g_backoff_frames = kBackoffMaxFrames;
        }
        g_next_grant_frame = g_frames + 1u + g_backoff_frames;
        return;
    }

    if (g_backoff_frames > 0u) {
        g_backoff_frames /= 2u;
        g_next_grant_frame = g_frames + 1u + g_backoff_frames;
        return;
    }

    // Cheap grant with no backoff pending: another one may run in this same
    // frame as long as frame_budget_spent() still says there is room.
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
    LOGI("23.1.3-hidden-items: pass %" PRId32 " complete (seen=%" PRId32
         " already owned=%" PRId32 " wanted=%" PRId32 " granted=%" PRId32
         " left to the shop=%" PRId32 " failed=%" PRId32 ", worst grant %"
         PRIu64 " us)",
         g_pass, g_seen, g_already_owned, g_candidates, g_granted,
         g_offered_skipped, g_failed, g_worst_grant_us);

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
         " inventory (%" PRIu64 " us budget and max %" PRId32 " transactions"
         " per menu frame, skins %s)",
         g_grant_everything
             ? "every definition the build ships"
             : (kGrantEveryWeapon
                    ? "every weapon the build ships, plus wear and gadgets no"
                      " shop or craft list offers"
                    : "definitions no shop or craft list offers"),
         kGrantBudgetUs, kGrantsPerFrameCap,
         kIncludeSkins ? "included" : "excluded");
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
