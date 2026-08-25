#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) offline season and lottery: weapon skins and graffiti
//
// Two separate problems are solved here, and they are the same problem.
//
// 1. There is nothing left to earn. progression_2313 grants max level and
//    999,999,999 of both currencies, weapon_modules_2313 unlocks every
//    module and hidden_items_2313 grants every weapon, every wear piece and
//    the gadgets outright. That was the right call for an offline port of a
//    dead online game -- but it also means no reward loop survives: neither
//    currency nor level nor rarity can gate anything any more.
//
// 2. Two cosmetic families are granted by nothing at all: weapon skins
//    (OfferItemType.WeaponSkin = 1170) and graffiti (OfferItemType.Graffiti
//    = 1470). hidden_items_2313 sweeps item types 10..70 and deliberately
//    keeps kIncludeSkins = false; neither of these two types is in that
//    range in the first place. Their stock sources are config-driven
//    (ConfigId.BalanceWeaponSkins "BalanceWeaponSkins_v22", ConfigId.Graffiti
//    "graffiti-v2", ConfigId.LootBoxes "loot-boxes-v7") and the emulated
//    backend answers config endpoints with {}, so the shop, the roulette and
//    the pass reward tables that would normally hand them out are empty.
//
// So the arsenal is complete, the wardrobe is complete, and the only content
// the player could still want is exactly the content nothing delivers. This
// module makes that content the reward loop: the cosmetics arrive one at a
// time, on a wall clock, as season tiers and lottery spins.
//
// ------------------------------------------------------- what made it possible
//
// hidden_items_2313.h records the blocker verbatim: "il2cpp.h exposes no
// object_new and no generic-instantiation helper, so a managed
// List<key> cannot be constructed from native code", and the same argument
// applied to a single key -- which is why every grant in this port so far had
// to *borrow* a key from a list the game had already built.
//
// That is not true of the key type. The inventory key and the reward payload
// are the same class, PGCompany.-- (dump2313.cs:469871), and it ships static
// factories that allocate the object themselves:
//
//   0x24B370C  static -- --(OfferItemType, string id)
//   0x24B39D8  static -- --(OfferItemType, string id, int amount)
//   0x24B4260  static -- --(string serialised)
//
// A static factory needs no object_new on our side: the managed body does the
// allocation and returns the finished object. So native code can mint an
// inventory key for any (type, id, amount) triple and feed it straight into
// the stock grant transaction that hidden_items_2313 already proves works on
// the target hardware:
//
//   --(key, Nullable<cause>, Action)  0x3062B08   grant
//   --(key, Nullable<filter>)         0x304F634   owned count
//
// Corroboration that a WeaponSkin key really is the currency of that system:
// --::--(--) at 0x2133C48 resolves a List<WeaponSkinSettings> *from a key*.
//
// Graffiti needs even less: PGCompany.GraffitiSystem.-- ships its own static
// key builders, --(int) at 0x15077D0 and --(string) at 0x1507908, so graffiti
// keys are taken from the game rather than synthesised, and the id strings
// ("graffiti_<n>", with "graffiti_-1" as the shipped "none") never have to be
// guessed at.
//
// ------------------------------------------------- the catalogue and its risk
//
// Rilisoft.-- (dump2313.cs:380764) is the weapon-skin catalogue and exposes
// three 0-argument List<WeaponSkin> getters:
//
//   0x35B4DD4  --()
//   0x35B5558  --()
//   0x35B56A4  --()
//
// Their first 30 instructions are byte-identical in shape (static cctor guard,
// string-literal init block, static cache load), so static analysis cannot say
// which one is "everything the build ships" and which one is "owned" or "buyable
// right now". Rather than guess, all three are read and unioned, deduplicated by
// WeaponSkin::get_Id() (0x35B36A4), and every candidate is then filtered through
// the stock owned count before anything is granted. A wrong guess about any one
// list can only make the pool smaller; it can never grant the same skin twice
// and it can never grant something the player already has.
//
// ------------------------------------------------------------ why a drip feed
//
// hidden_items_2313 grants its whole sweep as fast as the frame budget allows,
// on purpose: an arsenal is playable content and withholding it is just an
// obstacle. Cosmetics are the opposite -- they are only worth anything if
// receiving them is an event. Since currency, level and rarity are all
// meaningless in this port, the only scarce resource left is time, so the
// schedule is measured on CLOCK_MONOTONIC rather than in menu frames.
//
// That distinction matters: the pump runs from MainMenuController.Update, so a
// frame counter would only advance while the player sits in the menu, and
// playing a match would count for nothing. A monotonic clock keeps running
// during matches, so time spent playing is exactly what unlocks the next tier.
//
// kOpeningTiers items are handed out immediately after the catalogue scan so
// that the feature is visibly alive on the first launch, then one tier every
// kTierIntervalSec, plus a lottery spin every kSpinIntervalSec that picks at
// random instead of in order.
//
// ------------------------------------------------ what this is deliberately not
//
// This is not a season inside the stock PixelPass screen. That screen reads a
// season model (PGCompany.PixelPass.--, view check at 0x3D5C340) which is built
// from the "pixel-pass-v6" config, exactly like the roulette reward tables come
// from "loot-boxes-v7" and the graffiti picker list from "graffiti-v2". Filling
// those needs a config payload, which is tracked as follow-up work in
// docs/PORT_23_1_3_SEASON_COSMETICS.md; live_content_2313 already makes all of
// those screens reachable. Until a payload exists, this module is the reward
// loop, and it does not need one: it talks to the inventory directly.
//
// No backend, no self-hosted service, no HTTP route: every call below is a
// stock managed entry point in the game's own libil2cpp.so.
//
// Safety model, unchanged from the rest of the port: fail closed. Six
// unambiguous metadata targets must resolve to exactly base + their recorded
// RVA before the four overloaded entry points are taken by RVA; nothing is
// patched, no game memory is written, every grant is preceded and followed by
// the stock owned count, and repeated failures disarm the module instead of
// spinning.
// -----------------------------------------------------------------------------

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "il2cpp.h"
#include "log.h"

namespace season_2313 {
namespace detail {

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

// ------------------------------------------------------------------ tunables

// hidden_items_2313 starts its bulk sweep at menu frame 180 and can hold the
// stock inventory transaction for several seconds. This module starts well
// after that: its grants are rare, but they must never share a frame with that
// sweep.
constexpr uint64_t kWarmupFrames = 900u;

// One season tier every three minutes of wall clock, one lottery spin every
// quarter of an hour. Both clocks keep running while the player is in a match,
// which is the whole point of using a monotonic clock instead of menu frames.
constexpr uint64_t kTierIntervalSec = 180u;
constexpr uint64_t kSpinIntervalSec = 900u;

// Handed out immediately after the catalogue scan, so the feature proves
// itself on the first launch instead of three minutes later.
constexpr int32_t kOpeningTiers = 3;

// Never two grants in quick succession, whatever the clocks say.
constexpr uint64_t kGrantCooldownFrames = 30u;

// Candidates inspected per grant attempt before giving up for this tick. A
// candidate is skipped without spending the tier when the player already owns
// it, which is how progress survives a restart: ownership *is* the save.
constexpr int32_t kMaxAttemptsPerTick = 6;

// Consecutive failed grants after which the module disarms itself.
constexpr int32_t kMaxConsecutiveFailures = 12;

// Catalogue scan pacing (read-only work: list access, id read, string copy).
constexpr int32_t kScanSkinsPerFrame = 24;
constexpr int32_t kScanGraffitiPerFrame = 8;

// Graffiti ids are dense small integers ("graffiti_-1" is the shipped "none"),
// so the pool is built by probing indexes rather than by reading the config
// list that arrives empty offline.
constexpr int32_t kGraffitiMaxIndex = 128;

// Sanity bounds.
constexpr int32_t kMaxListEntries = 4096;
constexpr int32_t kMaxPool = 512;
constexpr size_t kMaxIdLength = 63u;

// Periodic one-line state report.
constexpr uint64_t kReportPeriodFrames = 3600u;

// Rilisoft.OfferItemType (dump2313.cs, TypeDefIndex 8931).
constexpr int32_t kTypeWeaponSkin = 1170;

// sizeof(Nullable<T>) for the two stock optional arguments, as established by
// hidden_items_2313: an all-zero buffer is a null optional, so the callee
// substitutes its own defaults.
constexpr size_t kOwnedFilterSize = 24u;
constexpr size_t kObtainCauseSize = 104u;

// ----------------------------------------------------------- metadata names

constexpr const char* kPgNs = "PGCompany";
constexpr const char* kRilisoftNs = "Rilisoft";
constexpr const char* kProgressNs = "Progress";
constexpr const char* kGraffitiNs = "PGCompany.GraffitiSystem";

// Item registry: the ownership source of truth.
constexpr const char* kRegistryClass = "\u4e0a\u4e1e\u4e05\u4e09\u4e1a\u4e19\u4e16\u4e0d\u4e19";
constexpr const char* kRegistryInstance = "\u4e0b\u4e0c\u4e11\u4e01\u4e0b\u4e1f\u4e1b\u4e18\u4e0a";  // static, 0 args

// Static catalogue helper; bound only to prove the image, never called.
constexpr const char* kCatalogClass = "\u4e04\u4e1d\u4e18\u4e06\u4e08\u4e06\u4e1d\u4e06\u4e04";
constexpr const char* kCatalogByCategory = "\u4e09\u4e0e\u4e03\u4e06\u4e05\u4e06\u4e15\u4e12\u4e1a";  // static, 2 args

// Progress service: the grant transaction notifies it, so it must be alive
// before the first write.
constexpr const char* kProgressService = "\u4e1c\u4e1d\u4e02\u4e04\u4e1a\u4e15\u4e14\u4e19\u4e11";
constexpr const char* kProgressInstance = "\u4e1e\u4e0f\u4e1a\u4e10\u4e12\u4e0e\u4e1a\u4e17\u4e0e";  // static, 0 args

// Reward / inventory key DTO. Only an unambiguous 0-argument member is bound,
// to prove the image around the overloaded static factory below.
constexpr const char* kRewardClass = "\u4e11\u4e00\u4e18\u4e0e\u4e01\u4e04\u4e13\u4e13\u4e13";
constexpr const char* kRewardAnchor = "\u4e06\u4e04\u4e05\u4e02\u4e04\u4e0a\u4e04\u4e07\u4e05";  // instance, 0 args

// Weapon-skin catalogue and the skin id accessor.
constexpr const char* kSkinCatalogClass = "\u4e0e\u4e16\u4e14\u4e00\u4e01\u4e06\u4e08\u4e04\u4e08";
constexpr const char* kSkinListPrimary = "\u4e01\u4e17\u4e0d\u4e0f\u4e12\u4e0d\u4e12\u4e1d\u4e00";   // static, 0 args
constexpr const char* kSkinListSecond = "\u4e01\u4e18\u4e03\u4e04\u4e12\u4e11\u4e19\u4e0d\u4e1f";    // static, 0 args
constexpr const char* kSkinListThird = "\u4e14\u4e16\u4e18\u4e1b\u4e0b\u4e17\u4e10\u4e1c\u4e0a";     // static, 0 args
constexpr const char* kWeaponSkinClass = "WeaponSkin";
constexpr const char* kWeaponSkinId = "get_Id";  // instance, 0 args

// Graffiti system.
constexpr const char* kGraffitiClass = "\u4e10\u4e14\u4e06\u4e16\u4e1b\u4e0b\u4e0f\u4e12\u4e0f";
constexpr const char* kGraffitiInstance = "\u4e0b\u4e0c\u4e11\u4e01\u4e0b\u4e1f\u4e1b\u4e18\u4e0a";  // static, 0 args
constexpr const char* kGraffitiOwned = "\u4e16\u4e02\u4e1e\u4e19\u4e0f\u4e0e\u4e04\u4e04\u4e0f";     // instance, 1 arg (int)
constexpr const char* kGraffitiIdOfIndex = "\u4e19\u4e17\u4e14\u4e15\u4e1a\u4e16\u4e1d\u4e02\u4e0c"; // static, 1 arg (int)

// ---------------------------------------------------------- verified offsets
//
// Anchors: unambiguous by metadata name and argument count, and therefore
// usable as proof that the loaded image is the verified 23.1.3 ARM64
// libil2cpp.so (ELF build id 57fcc18d2db06212416d480d53c0f881ee47c52a).
constexpr uintptr_t kRegistryInstanceRva = 0x3046000u;
constexpr uintptr_t kCatalogByCategoryRva = 0x305C074u;
constexpr uintptr_t kRewardAnchorRva = 0x24B3AC4u;
constexpr uintptr_t kSkinListPrimaryRva = 0x35B4DD4u;
constexpr uintptr_t kWeaponSkinIdRva = 0x35B36A4u;
constexpr uintptr_t kGraffitiInstanceRva = 0x1506D04u;

// Overloaded by argument type only, so metadata name plus argument count
// cannot select them: taken by RVA once the anchors above check out.
//
// -- static --(OfferItemType, string, int) -> key
constexpr uintptr_t kRewardKeyRva = 0x24B39D8u;
// -- --(key, Nullable<filter>) -> owned count
constexpr uintptr_t kOwnedCountRva = 0x304F634u;
// -- --(key, Nullable<cause>, Action) -> item
constexpr uintptr_t kGrantRva = 0x3062B08u;
// -- static --(int) -> key
constexpr uintptr_t kGraffitiKeyRva = 0x15077D0u;

// ------------------------------------------------------------- managed ABI

using StaticObjFn = void* (*)(void* method);
using InstanceObjFn = void* (*)(void* self, void* method);
using ListCountFn = int32_t (*)(void* list, void* method);
using ListItemFn = void* (*)(void* list, int32_t index, void* method);
using GraffitiOwnedFn = bool (*)(void* self, int32_t index, void* method);
using StaticStringOfIntFn = void* (*)(int32_t index, void* method);

// (OfferItemType, string id, int amount, MethodInfo*) -> key
using RewardKeyFn = void* (*)(int32_t type, void* id, int32_t amount,
                              void* method);
// (this, key, Nullable<filter>*, MethodInfo*) -> owned count
using OwnedCountFn = int32_t (*)(void* self, void* key, void* filter,
                                 void* method);
// (this, key, Nullable<cause>*, Action, MethodInfo*) -> granted item
using GrantFn = void* (*)(void* self, void* key, void* cause, void* callback,
                          void* method);
// (int index, MethodInfo*) -> key
using GraffitiKeyFn = void* (*)(int32_t index, void* method);

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
        LOGE("23.1.3-season: %s::%s/%d not found in metadata", klass, method,
             args_count);
        return false;
    }
    void* ptr = il2cpp::method_pointer(info);
    if (ptr == nullptr) {
        LOGE("23.1.3-season: %s::%s/%d has no compiled body", klass, method,
             args_count);
        return false;
    }
    out.info = info;
    out.ptr = ptr;
    return true;
}

// List<T> is a generic instantiation, so its accessors are resolved off a
// concrete instance instead of by namespace and name.
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

enum class Kind : uint8_t { WeaponSkin, Graffiti };

struct Entry {
    Kind kind = Kind::WeaponSkin;
    bool taken = false;
    int32_t index = -1;  // graffiti index; -1 for weapon skins
    char id[kMaxIdLength + 1] = {};
};

enum class Stage : uint8_t { Warmup, ScanSkins, ScanGraffiti, Season, Idle };

inline Managed g_registry_instance{};
inline Managed g_progress_service{};
inline Managed g_catalog_anchor{};  // image proof only
inline Managed g_reward_anchor{};   // image proof only
inline Managed g_skin_list[3]{};
inline Managed g_weapon_skin_id{};
inline Managed g_graffiti_instance{};
inline Managed g_graffiti_owned{};
inline Managed g_graffiti_id{};

inline RewardKeyFn g_reward_key = nullptr;
inline OwnedCountFn g_owned_count = nullptr;
inline GrantFn g_grant = nullptr;
inline GraffitiKeyFn g_graffiti_key = nullptr;

inline ListApi g_skin_list_api{};

inline Entry g_pool[kMaxPool]{};
inline int32_t g_pool_count = 0;
inline int32_t g_skins_in_pool = 0;
inline int32_t g_graffiti_in_pool = 0;

inline Stage g_stage = Stage::Warmup;
inline int32_t g_scan_list = 0;    // index into g_skin_list
inline int32_t g_scan_cursor = 0;  // cursor inside the current managed list
inline int32_t g_scan_probe = 0;   // graffiti index probe
inline int32_t g_cursor = 0;       // next sequential pool slot
inline int32_t g_opening_left = kOpeningTiers;
inline int32_t g_tier = 0;
inline int32_t g_spins = 0;
inline int32_t g_granted = 0;
inline int32_t g_already_owned = 0;
inline int32_t g_failed = 0;
inline int32_t g_consecutive_failures = 0;
inline uint64_t g_frames = 0u;
inline uint64_t g_last_grant_frame = 0u;
inline uint64_t g_next_tier_us = 0u;
inline uint64_t g_next_spin_us = 0u;
inline uint64_t g_rng = 0u;
inline bool g_armed = false;
inline bool g_installed = false;
inline bool g_reported_catalogue = false;

// ------------------------------------------------------------- diagnostics

inline uint64_t now_us() {
    struct timespec ts {};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0u;
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ull +
           static_cast<uint64_t>(ts.tv_nsec) / 1000ull;
}

inline uint64_t next_random() {
    // xorshift64*: no allocation, no libc state, deterministic per launch.
    g_rng ^= g_rng >> 12;
    g_rng ^= g_rng << 25;
    g_rng ^= g_rng >> 27;
    return g_rng * 2685821657736338717ull;
}

inline const char* kind_label(Kind kind) {
    return kind == Kind::Graffiti ? "graffiti" : "weapon skin";
}

// ------------------------------------------------------------ pool building

inline bool pool_contains(const char* id) {
    for (int32_t i = 0; i < g_pool_count; ++i) {
        if (std::strcmp(g_pool[i].id, id) == 0) return true;
    }
    return false;
}

inline bool pool_add(Kind kind, const char* id, int32_t index) {
    if (g_pool_count >= kMaxPool) return false;
    if (id == nullptr || id[0] == '\0') return false;
    if (pool_contains(id)) return false;

    Entry& entry = g_pool[g_pool_count];
    entry.kind = kind;
    entry.taken = false;
    entry.index = index;
    std::snprintf(entry.id, sizeof(entry.id), "%s", id);
    ++g_pool_count;
    if (kind == Kind::Graffiti) {
        ++g_graffiti_in_pool;
    } else {
        ++g_skins_in_pool;
    }
    return true;
}

inline void* registry_instance() {
    if (!g_registry_instance) return nullptr;
    return reinterpret_cast<StaticObjFn>(g_registry_instance.ptr)(
        g_registry_instance.info);
}

inline void* graffiti_instance() {
    if (!g_graffiti_instance) return nullptr;
    return reinterpret_cast<StaticObjFn>(g_graffiti_instance.ptr)(
        g_graffiti_instance.info);
}

inline bool progress_ready() {
    if (!g_progress_service) return false;
    return reinterpret_cast<StaticObjFn>(g_progress_service.ptr)(
               g_progress_service.info) != nullptr;
}

inline std::string weapon_skin_id(void* skin) {
    if (skin == nullptr || !g_weapon_skin_id) return std::string();
    void* managed = reinterpret_cast<InstanceObjFn>(g_weapon_skin_id.ptr)(
        skin, g_weapon_skin_id.info);
    return il2cpp::to_utf8(managed, 64u);
}

// One shipped list per visit, kScanSkinsPerFrame entries per frame. All three
// getters are unioned because their filtering semantics cannot be established
// statically; duplicates are dropped by id and ownership is checked later, at
// grant time, so over-collecting is harmless.
inline void scan_weapon_skins() {
    if (g_scan_list >= 3) {
        g_stage = Stage::ScanGraffiti;
        g_scan_probe = 0;
        return;
    }
    if (!g_skin_list[g_scan_list]) {
        ++g_scan_list;
        g_scan_cursor = 0;
        return;
    }

    void* list = reinterpret_cast<StaticObjFn>(g_skin_list[g_scan_list].ptr)(
        g_skin_list[g_scan_list].info);
    if (list == nullptr) {
        ++g_scan_list;
        g_scan_cursor = 0;
        return;
    }
    if (!g_skin_list_api && !resolve_list_api(list, g_skin_list_api)) {
        ++g_scan_list;
        g_scan_cursor = 0;
        return;
    }

    const int32_t total = reinterpret_cast<ListCountFn>(
        g_skin_list_api.count_ptr)(list, g_skin_list_api.count_info);
    if (total <= 0 || total > kMaxListEntries) {
        if (total > kMaxListEntries) {
            LOGW("23.1.3-season: weapon skin list %" PRId32 " reports %" PRId32
                 " entries; skipped as implausible",
                 g_scan_list, total);
        }
        ++g_scan_list;
        g_scan_cursor = 0;
        return;
    }

    int32_t checked = 0;
    while (g_scan_cursor < total && checked < kScanSkinsPerFrame) {
        void* skin = reinterpret_cast<ListItemFn>(g_skin_list_api.item_ptr)(
            list, g_scan_cursor, g_skin_list_api.item_info);
        const std::string id = weapon_skin_id(skin);
        if (!id.empty() && id.size() <= kMaxIdLength) {
            pool_add(Kind::WeaponSkin, id.c_str(), -1);
        }
        ++g_scan_cursor;
        ++checked;
    }

    if (g_scan_cursor >= total) {
        ++g_scan_list;
        g_scan_cursor = 0;
    }
}

// Graffiti definitions normally come from the "graffiti-v2" config, which is
// empty offline, so the pool is built by probing indexes against the shipped
// static id builder instead. An index the build does not know produces no id
// and is skipped.
inline void scan_graffiti() {
    if (g_graffiti_id.ptr == nullptr || g_scan_probe > kGraffitiMaxIndex) {
        g_stage = Stage::Season;
        return;
    }

    int32_t probed = 0;
    while (g_scan_probe <= kGraffitiMaxIndex && probed < kScanGraffitiPerFrame) {
        const int32_t index = g_scan_probe;
        ++g_scan_probe;
        ++probed;

        void* managed = reinterpret_cast<StaticStringOfIntFn>(g_graffiti_id.ptr)(
            index, g_graffiti_id.info);
        const std::string id = il2cpp::to_utf8(managed, 64u);
        if (id.empty() || id.size() > kMaxIdLength) continue;
        pool_add(Kind::Graffiti, id.c_str(), index);
    }

    if (g_scan_probe > kGraffitiMaxIndex) g_stage = Stage::Season;
}

// ------------------------------------------------------------ grant plumbing

inline void* build_key(const Entry& entry) {
    if (entry.kind == Kind::Graffiti) {
        if (g_graffiti_key == nullptr) return nullptr;
        return g_graffiti_key(entry.index, nullptr);
    }
    if (g_reward_key == nullptr || il2cpp::string_new == nullptr) return nullptr;
    void* id = il2cpp::string_new(entry.id);
    if (id == nullptr) return nullptr;
    return g_reward_key(kTypeWeaponSkin, id, 1, nullptr);
}

inline int32_t owned_count(void* registry, void* key) {
    if (g_owned_count == nullptr || registry == nullptr || key == nullptr) {
        return -1;
    }
    // A null Nullable<> keeps the stock "any owned instance" semantics.
    alignas(8) unsigned char filter[kOwnedFilterSize] = {};
    return g_owned_count(registry, key, filter, nullptr);
}

// The graffiti system keeps its own owned set, so it gets a second opinion
// that does not depend on the synthesised-key path at all.
inline bool graffiti_already_owned(int32_t index) {
    if (!g_graffiti_owned) return false;
    void* instance = graffiti_instance();
    if (instance == nullptr) return false;
    return reinterpret_cast<GraffitiOwnedFn>(g_graffiti_owned.ptr)(
        instance, index, g_graffiti_owned.info);
}

// Returns true when the tier was actually spent on a new item.
inline bool grant_entry(int32_t slot, const char* reason) {
    Entry& entry = g_pool[slot];
    entry.taken = true;

    if (entry.kind == Kind::Graffiti && graffiti_already_owned(entry.index)) {
        ++g_already_owned;
        return false;
    }

    void* registry = registry_instance();
    if (registry == nullptr) return false;

    void* key = build_key(entry);
    if (key == nullptr) {
        ++g_failed;
        ++g_consecutive_failures;
        LOGW("23.1.3-season: no inventory key could be built for %s '%s'",
             kind_label(entry.kind), entry.id);
        return false;
    }

    const int32_t before = owned_count(registry, key);
    if (before > 0) {
        ++g_already_owned;
        return false;
    }
    if (before < 0) {
        ++g_failed;
        ++g_consecutive_failures;
        return false;
    }

    // A null obtain cause makes the stock transaction use its own default and
    // no completion callback is needed.
    alignas(8) unsigned char cause[kObtainCauseSize] = {};
    g_grant(registry, key, cause, nullptr, nullptr);

    const int32_t after = owned_count(registry, key);
    if (after < 1) {
        ++g_failed;
        ++g_consecutive_failures;
        LOGW("23.1.3-season: grant did not register %s '%s' (count 0 -> %" PRId32
             ")",
             kind_label(entry.kind), entry.id, after);
        return false;
    }

    ++g_granted;
    g_consecutive_failures = 0;
    g_last_grant_frame = g_frames;
    LOGI("23.1.3-season: %s: %s '%s' unlocked (%" PRId32 " of %" PRId32
         " in the pool, %" PRId32 " granted so far)",
         reason, kind_label(entry.kind), entry.id, slot + 1, g_pool_count,
         g_granted);
    return true;
}

inline int32_t next_sequential_slot() {
    for (int32_t i = 0; i < g_pool_count; ++i) {
        const int32_t slot = (g_cursor + i) % g_pool_count;
        if (!g_pool[slot].taken) {
            g_cursor = (slot + 1) % g_pool_count;
            return slot;
        }
    }
    return -1;
}

inline int32_t next_random_slot() {
    for (int32_t attempt = 0; attempt < 8; ++attempt) {
        const int32_t slot =
            static_cast<int32_t>(next_random() % static_cast<uint64_t>(
                                                     g_pool_count));
        if (!g_pool[slot].taken) return slot;
    }
    return next_sequential_slot();
}

// One tier or one spin, retrying past items the player already owns so that a
// restart resumes where ownership left off instead of stalling on the first
// already-owned entry.
inline bool award(bool random_pick, const char* reason) {
    for (int32_t attempt = 0; attempt < kMaxAttemptsPerTick; ++attempt) {
        const int32_t slot =
            random_pick ? next_random_slot() : next_sequential_slot();
        if (slot < 0) {
            LOGI("23.1.3-season: every one of the %" PRId32
                 " pooled cosmetics is owned; the season is complete",
                 g_pool_count);
            g_stage = Stage::Idle;
            g_armed = false;
            return false;
        }
        if (grant_entry(slot, reason)) return true;
        if (g_consecutive_failures >= kMaxConsecutiveFailures) {
            LOGW("23.1.3-season: %" PRId32
                 " consecutive failures; disarming (granted=%" PRId32 ")",
                 g_consecutive_failures, g_granted);
            g_armed = false;
            return false;
        }
    }
    return false;
}

// ------------------------------------------------------------ season driver

inline void report_catalogue() {
    g_reported_catalogue = true;
    LOGI("23.1.3-season: catalogue: %" PRId32 " weapon skins (union of the three"
         " shipped lists) and %" PRId32 " graffiti ids, %" PRId32
         " entries pooled; one tier every %" PRIu64 " s, one lottery spin every"
         " %" PRIu64 " s, %" PRId32 " handed out immediately",
         g_skins_in_pool, g_graffiti_in_pool, g_pool_count, kTierIntervalSec,
         kSpinIntervalSec, kOpeningTiers);
    if (g_pool_count == 0) {
        LOGW("23.1.3-season: neither the weapon skin catalogue nor the graffiti"
             " ids produced a single entry; nothing will be handed out");
        g_armed = false;
    }
}

inline void run_season() {
    if (g_pool_count == 0) return;

    const uint64_t now = now_us();
    if (g_next_tier_us == 0u) {
        g_next_tier_us = now + kTierIntervalSec * 1000000ull;
        g_next_spin_us = now + kSpinIntervalSec * 1000000ull;
        g_rng = now ^ (static_cast<uint64_t>(g_pool_count) << 32) ^
                0x9E3779B97F4A7C15ull;
    }

    if (g_frames - g_last_grant_frame < kGrantCooldownFrames) return;

    if (g_opening_left > 0) {
        --g_opening_left;
        ++g_tier;
        char reason[64] = {};
        std::snprintf(reason, sizeof(reason), "season tier %" PRId32, g_tier);
        award(false, reason);
        return;
    }

    if (now >= g_next_spin_us) {
        g_next_spin_us = now + kSpinIntervalSec * 1000000ull;
        ++g_spins;
        char reason[64] = {};
        std::snprintf(reason, sizeof(reason), "lottery spin %" PRId32, g_spins);
        award(true, reason);
        return;
    }

    if (now >= g_next_tier_us) {
        g_next_tier_us = now + kTierIntervalSec * 1000000ull;
        ++g_tier;
        char reason[64] = {};
        std::snprintf(reason, sizeof(reason), "season tier %" PRId32, g_tier);
        award(false, reason);
    }
}

inline void report_state() {
    LOGI("23.1.3-season: pool=%" PRId32 " granted=%" PRId32 " already owned=%"
         PRId32 " tiers=%" PRId32 " spins=%" PRId32 " failed=%" PRId32,
         g_pool_count, g_granted, g_already_owned, g_tier, g_spins, g_failed);
}

inline void pump() {
    if (!g_installed || !g_armed) return;
    ++g_frames;

    if (g_stage == Stage::Warmup) {
        if (g_frames < kWarmupFrames) return;
        g_stage = Stage::ScanSkins;
        return;
    }

    // Every grant runs a stock transaction that notifies the Progress
    // service, so it has to be alive first.
    if (!progress_ready()) return;

    switch (g_stage) {
        case Stage::ScanSkins:
            scan_weapon_skins();
            break;
        case Stage::ScanGraffiti:
            scan_graffiti();
            break;
        case Stage::Season:
            if (!g_reported_catalogue) report_catalogue();
            run_season();
            break;
        default:
            break;
    }

    if (kReportPeriodFrames != 0u && (g_frames % kReportPeriodFrames) == 0u) {
        report_state();
    }
}

// ------------------------------------------------------------- installation

// Proves that `base` really is the verified 23.1.3 libil2cpp.so image: a
// target resolved through metadata must land on its recorded RVA. Only then
// may the overloaded entry points be taken by RVA.
inline bool verify_image(uintptr_t base, const Managed& target, uintptr_t rva,
                         const char* label) {
    const auto expected = reinterpret_cast<void*>(base + rva);
    if (target.ptr == expected) return true;
    LOGE("23.1.3-season: %s is at %p but RVA 0x%08" PRIxPTR " maps to %p; this"
         " is not the verified 23.1.3 ARM64 image",
         label, target.ptr, rva, expected);
    return false;
}

inline bool install(uintptr_t il2cpp_base) {
    if (g_installed) return true;

    bool resolved = true;
    resolved &= bind(g_registry_instance, kPgNs, kRegistryClass,
                     kRegistryInstance, 0);
    resolved &= bind(g_progress_service, kProgressNs, kProgressService,
                     kProgressInstance, 0);
    resolved &= bind(g_catalog_anchor, kPgNs, kCatalogClass, kCatalogByCategory,
                     2);
    resolved &= bind(g_reward_anchor, kPgNs, kRewardClass, kRewardAnchor, 0);
    resolved &= bind(g_skin_list[0], kRilisoftNs, kSkinCatalogClass,
                     kSkinListPrimary, 0);
    resolved &= bind(g_skin_list[1], kRilisoftNs, kSkinCatalogClass,
                     kSkinListSecond, 0);
    resolved &= bind(g_skin_list[2], kRilisoftNs, kSkinCatalogClass,
                     kSkinListThird, 0);
    resolved &= bind(g_weapon_skin_id, kRilisoftNs, kWeaponSkinClass,
                     kWeaponSkinId, 0);
    resolved &= bind(g_graffiti_instance, kGraffitiNs, kGraffitiClass,
                     kGraffitiInstance, 0);
    resolved &= bind(g_graffiti_owned, kGraffitiNs, kGraffitiClass,
                     kGraffitiOwned, 1);
    resolved &= bind(g_graffiti_id, kGraffitiNs, kGraffitiClass,
                     kGraffitiIdOfIndex, 1);
    if (!resolved) {
        LOGE("23.1.3-season: metadata does not match the expected 23.1.3 build;"
             " nothing was armed");
        return false;
    }

    if (il2cpp_base == 0u) {
        LOGE("23.1.3-season: libil2cpp.so base address is unknown; the season"
             " cannot be armed");
        return false;
    }
    if (!verify_image(il2cpp_base, g_registry_instance, kRegistryInstanceRva,
                      "the item registry singleton") ||
        !verify_image(il2cpp_base, g_catalog_anchor, kCatalogByCategoryRva,
                      "the category catalogue getter") ||
        !verify_image(il2cpp_base, g_reward_anchor, kRewardAnchorRva,
                      "the inventory key accessor") ||
        !verify_image(il2cpp_base, g_skin_list[0], kSkinListPrimaryRva,
                      "the weapon skin catalogue") ||
        !verify_image(il2cpp_base, g_weapon_skin_id, kWeaponSkinIdRva,
                      "the weapon skin id accessor") ||
        !verify_image(il2cpp_base, g_graffiti_instance, kGraffitiInstanceRva,
                      "the graffiti system singleton")) {
        return false;
    }

    g_reward_key = reinterpret_cast<RewardKeyFn>(
        reinterpret_cast<void*>(il2cpp_base + kRewardKeyRva));
    g_owned_count = reinterpret_cast<OwnedCountFn>(
        reinterpret_cast<void*>(il2cpp_base + kOwnedCountRva));
    g_grant = reinterpret_cast<GrantFn>(
        reinterpret_cast<void*>(il2cpp_base + kGrantRva));
    g_graffiti_key = reinterpret_cast<GraffitiKeyFn>(
        reinterpret_cast<void*>(il2cpp_base + kGraffitiKeyRva));

    g_armed = true;
    g_installed = true;
    LOGI("23.1.3-season: armed: weapon skins and graffiti are handed out one at"
         " a time through the stock item inventory, starting at menu frame %"
         PRIu64 " (%" PRId32 " opening tiers, then one every %" PRIu64
         " s plus a lottery spin every %" PRIu64
         " s, measured on a monotonic clock so time spent in matches counts)",
         kWarmupFrames, kOpeningTiers, kTierIntervalSec, kSpinIntervalSec);
    return true;
}

}  // namespace detail

// Arms the offline cosmetics season. `il2cpp_base` is the load address of
// libil2cpp.so.
inline bool install_hooks(uintptr_t il2cpp_base) {
    return detail::install(il2cpp_base);
}

// Driven once per main-menu frame from progression_2313's
// MainMenuController.Update hook: the grants run stock managed transactions,
// so they need a game thread and a live main menu.
inline void pump_from_main_menu() { detail::pump(); }

}  // namespace season_2313
