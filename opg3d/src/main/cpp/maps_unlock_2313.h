#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) map and mode menu unlock
//
// What actually hides a map in this build
// --------------------------------------
// The map catalogue is entirely local. Nothing about it comes from the retired
// backend, so this is not a config-response problem:
//
//   SceneInfo               TypeDefIndex 4550, MonoBehaviour, one per map
//     List<mode> avaliableInModes   0x20   modes this map declares itself for
//     int        indexMap           0x38   the id the netcode uses
//     bool       isPreloading       0x3C
//     bool       isPremium          0x3D
//     string     keyTranslateName   0x40
//     string     maxAvaliableVersion 0x70  version window, event maps use it
//     string     minAvaliableVersion 0x78
//
//   AllScenesForMode        TypeDefIndex 4551, one per game mode
//     List<SceneInfo>            avaliableScenes  0x10  <- the map grid reads this
//     List<SceneInfo>            mapsForVote      0x18
//     Dictionary<SceneInfo,int>  unlockedAtLevel  0x20  <- "unlocks at level N"
//     List<SceneInfo>            lockedByLevel    0x28  <- hidden behind level
//     mode                       mode             0x30
//
//   SceneInfoController     TypeDefIndex 4555, MonoBehaviour singleton
//     List<SceneInfo>            allScenes        0x18  <- every map the build has
//     List<AllScenesForMode>     modeInfo         0x38
//     丂丙丛万下与丑丝下(mode) -> AllScenesForMode  the per-mode bucket getter
//
// So a map is missing from the picker for exactly four local reasons: it is in
// lockedByLevel, it is not in avaliableInModes for the selected mode, it is
// isPremium, or it falls outside its min/max version window. All four are
// decided on device, which is why this can be fixed without touching the
// backend emulator.
//
// How this module works
// --------------------
// The per-mode bucket getter is hooked (both overloads). After the stock call
// builds its bucket, the bucket is opened up:
//
//   1. everything in lockedByLevel moves into avaliableScenes, then the locked
//      list is emptied;
//   2. unlockedAtLevel is cleared, so no leftover "unlocks at level N" badge is
//      rendered for a map that is now selectable;
//   3. every map in the controller's allScenes that the bucket does not already
//      contain is appended, which is what covers the premium and version-window
//      cases and any map excluded from a mode outright.
//
// Nothing is patched and no UI row is fabricated: the game's own List<SceneInfo>
// objects are mutated through their own managed Add/Contains/Clear, and the
// stock grid then renders them exactly as it renders any other map.
//
// Deliberately not touched: mapsForVote. MapVoteController ships a hard limit
// of 5 vote slots (private const int 丘丂上丞丕丒丘丁丂 = 5) and its grid is laid out
// for that, so stuffing the full map list into the vote list would overflow the
// panel rather than unlock anything. Map *selection* is what was asked for.
//
// Known consequence, accepted on request: maps stream from asset bundles
// (MapHint carries AssetBundlesStateMonitorView plus enableIfBundleLoaded /
// disableIfBundleLoaded, and the metadata has assetBundles-v2 and
// assetBundleHashFromConfig). The bundle CDN is gone, so a map whose bundle is
// not in the OBB or the local assets payload can be selected but will not load.
// Bundle presence is intentionally not checked here; every map is offered and
// the first map index of each bucket is logged so a missing one is easy to
// identify from logcat.
//
// ARM64 ABI: generated managed methods take `this` first (instance methods),
// then their explicit arguments, then MethodInfo* last. Enum arguments are
// plain int32. List<T> and Dictionary<K,V> are generic instantiations, so their
// accessors are resolved off the concrete object rather than by name, and the
// resolved MethodInfo* is passed through as the trailing argument.
// -----------------------------------------------------------------------------

#include <cinttypes>
#include <cstdint>
#include <string>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

namespace maps_unlock_2313 {
namespace detail {

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

// ------------------------------------------------------------------ tunables

// Move lockedByLevel into avaliableScenes.
constexpr bool kDrainLockedByLevel = true;

// Clear the per-map "unlocks at level N" dictionary once its maps are open.
constexpr bool kClearUnlockLevels = true;

// Top every mode's bucket up from SceneInfoController.allScenes. This is what
// reaches premium maps, maps outside their version window and maps a mode does
// not list at all.
constexpr bool kOpenEveryMode = true;

// Leave the 5-slot vote list alone (see the header comment).
constexpr bool kAddToVoteList = false;

// Sanity bound for a single managed list.
constexpr int32_t kMaxListEntries = 4096;

// Upper bound on additions in one call, so a pathological list can never turn
// a UI callback into a long stall.
constexpr int32_t kMaxAddsPerCall = 512;

// Buckets remembered so an already-opened mode costs one get_Count call.
constexpr int32_t kMaxMemoBuckets = 64;

// Log the first kLogBurst opened buckets in full, then every kLogPeriod-th one.
constexpr uint64_t kLogBurst = 8u;
constexpr uint64_t kLogPeriod = 32u;

// Map indices printed per opened bucket, to identify a map whose asset bundle
// is missing.
constexpr int32_t kLogMapsPerBucket = 12;

// ----------------------------------------------------------- metadata names

// SceneInfoController, AllScenesForMode and SceneInfo all live in the global
// namespace, which IL2CPP metadata spells as the empty string.
constexpr const char* kGlobalNs = "";
constexpr const char* kControllerClass = "SceneInfoController";

// The per-mode bucket getter. Obfuscated in this build; the bytes below are
// taken verbatim from the shipped global-metadata.dat, so no glyph can be lost
// in transcription. Reads as 丂丙丛万下与丑丝下 and has two overloads:
//   (mode)                            -> AllScenesForMode   RVA 0x33E9DC4
//   (mode, List<AllScenesForMode>)    -> AllScenesForMode   RVA 0x33E9EA8
// The RVAs are documentation only: nothing here is taken by address.
constexpr const char* kBucketByMode =
    "\xe4\xb8\x82"
    "\xe4\xb8\x99"
    "\xe4\xb8\x9b"
    "\xe4\xb8\x87"
    "\xe4\xb8\x8b"
    "\xe4\xb8\x8e"
    "\xe4\xb8\x91"
    "\xe4\xb8\x9d"
    "\xe4\xb8\x8b";

// Field names that survived obfuscation, resolved off the live objects.
constexpr const char* kAllScenesField = "allScenes";
constexpr const char* kAvailableField = "avaliableScenes";
constexpr const char* kVoteField = "mapsForVote";
constexpr const char* kUnlockLevelField = "unlockedAtLevel";
constexpr const char* kLockedField = "lockedByLevel";
constexpr const char* kModeField = "mode";
constexpr const char* kSceneIndexField = "indexMap";
constexpr const char* kSceneNameField = "keyTranslateName";

// ------------------------------------------------------------- managed ABI

using BucketByModeFn = void* (*)(void* self, int32_t mode, void* method);
using BucketByModeListFn = void* (*)(void* self, int32_t mode, void* list,
                                     void* method);

using ListCountFn = int32_t (*)(void* list, void* method);
using ListItemFn = void* (*)(void* list, int32_t index, void* method);
using ListAddFn = void (*)(void* list, void* item, void* method);
using ListContainsFn = bool (*)(void* list, void* item, void* method);
using ListClearFn = void (*)(void* list, void* method);

struct ListApi {
    void* count_info = nullptr;
    void* count_ptr = nullptr;
    void* item_info = nullptr;
    void* item_ptr = nullptr;
    void* add_info = nullptr;
    void* add_ptr = nullptr;
    void* contains_info = nullptr;
    void* contains_ptr = nullptr;
    void* clear_info = nullptr;
    void* clear_ptr = nullptr;

    explicit operator bool() const noexcept {
        return count_ptr != nullptr && item_ptr != nullptr &&
               add_ptr != nullptr && contains_ptr != nullptr &&
               clear_ptr != nullptr;
    }
};

inline bool bind_method(void* klass, const char* name, int args, void*& info,
                        void*& ptr) {
    info = il2cpp::class_get_method_from_name(klass, name, args);
    if (info == nullptr) return false;
    ptr = il2cpp::method_pointer(info);
    return ptr != nullptr;
}

// List<SceneInfo> is a generic instantiation, so its accessors are resolved
// off the concrete object instead of by namespace and name.
inline bool resolve_list_api(void* list, ListApi& api) {
    if (list == nullptr || il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_method_from_name == nullptr) {
        return false;
    }
    void* klass = il2cpp::object_get_class(list);
    if (klass == nullptr) return false;

    bool ok = true;
    ok &= bind_method(klass, "get_Count", 0, api.count_info, api.count_ptr);
    ok &= bind_method(klass, "get_Item", 1, api.item_info, api.item_ptr);
    ok &= bind_method(klass, "Add", 1, api.add_info, api.add_ptr);
    ok &= bind_method(klass, "Contains", 1, api.contains_info,
                      api.contains_ptr);
    ok &= bind_method(klass, "Clear", 0, api.clear_info, api.clear_ptr);
    return ok && static_cast<bool>(api);
}

inline int32_t list_count(const ListApi& api, void* list) {
    if (!api || list == nullptr) return -1;
    return reinterpret_cast<ListCountFn>(api.count_ptr)(list, api.count_info);
}

inline void* list_item(const ListApi& api, void* list, int32_t index) {
    if (!api || list == nullptr) return nullptr;
    return reinterpret_cast<ListItemFn>(api.item_ptr)(list, index,
                                                      api.item_info);
}

inline void list_add(const ListApi& api, void* list, void* item) {
    if (!api || list == nullptr || item == nullptr) return;
    reinterpret_cast<ListAddFn>(api.add_ptr)(list, item, api.add_info);
}

inline bool list_contains(const ListApi& api, void* list, void* item) {
    if (!api || list == nullptr || item == nullptr) return true;
    return reinterpret_cast<ListContainsFn>(api.contains_ptr)(
        list, item, api.contains_info);
}

inline void list_clear(const ListApi& api, void* list) {
    if (!api || list == nullptr) return;
    reinterpret_cast<ListClearFn>(api.clear_ptr)(list, api.clear_info);
}

// ------------------------------------------------------------ field helpers

inline void* field_of(void* obj, const char* name) {
    if (obj == nullptr || name == nullptr ||
        il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_field_from_name == nullptr) {
        return nullptr;
    }
    void* klass = il2cpp::object_get_class(obj);
    if (klass == nullptr) return nullptr;
    return il2cpp::class_get_field_from_name(klass, name);
}

inline void* read_ref(void* obj, void* field) {
    if (obj == nullptr || field == nullptr ||
        il2cpp::field_get_value == nullptr) {
        return nullptr;
    }
    void* value = nullptr;
    il2cpp::field_get_value(obj, field, &value);
    return value;
}

inline int32_t read_i32(void* obj, void* field, int32_t fallback) {
    if (obj == nullptr || field == nullptr ||
        il2cpp::field_get_value == nullptr) {
        return fallback;
    }
    int32_t value = fallback;
    il2cpp::field_get_value(obj, field, &value);
    return value;
}

// ------------------------------------------------------------------- state

struct Memo {
    void* bucket = nullptr;
    int32_t settled_count = -1;
};

inline BucketByModeFn g_orig_one = nullptr;
inline BucketByModeListFn g_orig_two = nullptr;

inline ListApi g_scene_list{};   // List<SceneInfo>
inline ListApi g_level_dict{};   // Dictionary<SceneInfo,int>, Clear only
inline bool g_level_dict_tried = false;

inline void* g_f_available = nullptr;
inline void* g_f_vote = nullptr;
inline void* g_f_locked = nullptr;
inline void* g_f_levels = nullptr;
inline void* g_f_mode = nullptr;
inline void* g_f_all_scenes = nullptr;
inline void* g_f_scene_index = nullptr;
inline void* g_f_scene_name = nullptr;
inline bool g_bucket_fields_ready = false;
inline bool g_bucket_fields_failed = false;

inline uint64_t g_opened = 0;
inline int32_t g_total_unlocked = 0;
inline bool g_inside = false;
inline bool g_installed = false;

inline Memo g_memo[kMaxMemoBuckets];

inline Memo& memo_for(void* bucket) {
    const auto slot = static_cast<int32_t>(
        (reinterpret_cast<uintptr_t>(bucket) >> 4) %
        static_cast<uintptr_t>(kMaxMemoBuckets));
    Memo& memo = g_memo[slot];
    if (memo.bucket != bucket) {
        memo.bucket = bucket;
        memo.settled_count = -1;
    }
    return memo;
}

inline bool should_log() {
    return g_opened <= kLogBurst || (g_opened % kLogPeriod) == 0u;
}

// --------------------------------------------------------------- the unlock

inline bool resolve_bucket_fields(void* bucket) {
    if (g_bucket_fields_ready) return true;
    if (g_bucket_fields_failed) return false;

    g_f_available = field_of(bucket, kAvailableField);
    g_f_vote = field_of(bucket, kVoteField);
    g_f_locked = field_of(bucket, kLockedField);
    g_f_levels = field_of(bucket, kUnlockLevelField);
    g_f_mode = field_of(bucket, kModeField);

    if (g_f_available == nullptr || g_f_locked == nullptr) {
        g_bucket_fields_failed = true;
        LOGE("23.1.3-maps: AllScenesForMode does not expose %s/%s; the map"
             " unlock stays inert",
             kAvailableField, kLockedField);
        return false;
    }
    g_bucket_fields_ready = true;
    return true;
}

inline void clear_unlock_levels(void* bucket) {
    if (!kClearUnlockLevels || g_f_levels == nullptr) return;
    void* dict = read_ref(bucket, g_f_levels);
    if (dict == nullptr) return;
    if (!g_level_dict && !g_level_dict_tried) {
        g_level_dict_tried = true;
        void* klass = il2cpp::object_get_class != nullptr
                          ? il2cpp::object_get_class(dict)
                          : nullptr;
        if (klass != nullptr) {
            bind_method(klass, "Clear", 0, g_level_dict.clear_info,
                        g_level_dict.clear_ptr);
        }
    }
    if (g_level_dict.clear_ptr == nullptr) return;
    reinterpret_cast<ListClearFn>(g_level_dict.clear_ptr)(
        dict, g_level_dict.clear_info);
}

inline void log_bucket(void* bucket, void* available, int32_t mode,
                       int32_t from_locked, int32_t from_all, int32_t before,
                       int32_t after) {
    LOGI("23.1.3-maps: mode %" PRId32 ": %" PRId32 " -> %" PRId32
         " selectable maps (+%" PRId32 " level locked, +%" PRId32
         " not offered for this mode)",
         mode, before, after, from_locked, from_all);

    if (g_f_scene_index == nullptr || after <= 0) return;
    const int32_t limit = after < kLogMapsPerBucket ? after : kLogMapsPerBucket;
    for (int32_t i = 0; i < limit; ++i) {
        void* scene = list_item(g_scene_list, available, i);
        if (scene == nullptr) continue;
        const int32_t index_map = read_i32(scene, g_f_scene_index, -1);
        const std::string name =
            g_f_scene_name != nullptr
                ? il2cpp::to_utf8(read_ref(scene, g_f_scene_name), 32u)
                : std::string();
        LOGI("23.1.3-maps:   map index %" PRId32 " '%s'", index_map,
             name.empty() ? "<unnamed>" : name.c_str());
    }
    (void)bucket;
}

// Opens one per-mode bucket. Idempotent: an already-opened bucket costs a
// single get_Count call, so this is safe to run from a UI callback that the
// game may invoke every frame.
inline void open_bucket(void* self, void* bucket) {
    if (bucket == nullptr || g_inside) return;
    if (!resolve_bucket_fields(bucket)) return;

    void* available = read_ref(bucket, g_f_available);
    if (available == nullptr) return;
    if (!g_scene_list && !resolve_list_api(available, g_scene_list)) {
        LOGE("23.1.3-maps: List<SceneInfo> accessors could not be resolved;"
             " the map unlock stays inert");
        g_bucket_fields_failed = true;
        g_bucket_fields_ready = false;
        return;
    }

    const int32_t before = list_count(g_scene_list, available);
    if (before < 0 || before > kMaxListEntries) return;

    Memo& memo = memo_for(bucket);
    if (memo.settled_count == before) return;  // already opened, nothing new

    g_inside = true;
    int32_t added_from_locked = 0;
    int32_t added_from_all = 0;
    int32_t budget = kMaxAddsPerCall;

    if (kDrainLockedByLevel) {
        void* locked = read_ref(bucket, g_f_locked);
        const int32_t locked_total = list_count(g_scene_list, locked);
        if (locked_total > 0 && locked_total <= kMaxListEntries) {
            for (int32_t i = 0; i < locked_total && budget > 0; ++i) {
                void* scene = list_item(g_scene_list, locked, i);
                if (scene == nullptr) continue;
                if (list_contains(g_scene_list, available, scene)) continue;
                list_add(g_scene_list, available, scene);
                if (kAddToVoteList && g_f_vote != nullptr) {
                    list_add(g_scene_list, read_ref(bucket, g_f_vote), scene);
                }
                ++added_from_locked;
                --budget;
            }
            list_clear(g_scene_list, locked);
        }
        clear_unlock_levels(bucket);
    }

    if (kOpenEveryMode && self != nullptr) {
        if (g_f_all_scenes == nullptr) {
            g_f_all_scenes = field_of(self, kAllScenesField);
        }
        void* all_scenes = read_ref(self, g_f_all_scenes);
        const int32_t all_total = list_count(g_scene_list, all_scenes);
        if (all_total > 0 && all_total <= kMaxListEntries) {
            for (int32_t i = 0; i < all_total && budget > 0; ++i) {
                void* scene = list_item(g_scene_list, all_scenes, i);
                if (scene == nullptr) continue;
                if (list_contains(g_scene_list, available, scene)) continue;
                list_add(g_scene_list, available, scene);
                ++added_from_all;
                --budget;
            }
        }
    }

    const int32_t after = list_count(g_scene_list, available);
    memo.settled_count = after;
    g_inside = false;

    const int32_t added = added_from_locked + added_from_all;
    if (added <= 0) return;

    g_total_unlocked += added;
    ++g_opened;

    if (g_f_scene_index == nullptr) {
        void* probe = list_item(g_scene_list, available, 0);
        if (probe != nullptr) {
            g_f_scene_index = field_of(probe, kSceneIndexField);
            g_f_scene_name = field_of(probe, kSceneNameField);
        }
    }

    if (should_log()) {
        const int32_t mode = read_i32(bucket, g_f_mode, -1);
        log_bucket(bucket, available, mode, added_from_locked, added_from_all,
                   before, after);
    }
}

// ------------------------------------------------------------------- hooks

inline void* bucket_by_mode_hook(void* self, int32_t mode, void* method) {
    void* bucket =
        g_orig_one != nullptr ? g_orig_one(self, mode, method) : nullptr;
    open_bucket(self, bucket);
    return bucket;
}

inline void* bucket_by_mode_list_hook(void* self, int32_t mode, void* list,
                                      void* method) {
    void* bucket = g_orig_two != nullptr
                       ? g_orig_two(self, mode, list, method)
                       : nullptr;
    open_bucket(self, bucket);
    return bucket;
}

// ------------------------------------------------------------- installation

inline bool install() {
    if (g_installed) return true;

    const bool one = hook::install(
        {kGlobalNs, kControllerClass, kBucketByMode, 1},
        reinterpret_cast<void*>(&bucket_by_mode_hook),
        reinterpret_cast<void**>(&g_orig_one), false);
    const bool two = hook::install(
        {kGlobalNs, kControllerClass, kBucketByMode, 2},
        reinterpret_cast<void*>(&bucket_by_mode_list_hook),
        reinterpret_cast<void**>(&g_orig_two), false);

    if (!one && !two) {
        LOGE("23.1.3-maps: neither overload of the per-mode map bucket getter"
             " could be hooked; the map picker keeps its stock gates");
        return false;
    }

    g_installed = true;
    LOGI("23.1.3-maps: armed (bucket getter overloads: 1-arg %s, 2-arg %s);"
         " level locked maps are released%s, and asset bundle presence is not"
         " checked",
         one ? "hooked" : "absent", two ? "hooked" : "absent",
         kOpenEveryMode ? " and every map the build ships is offered in every"
                          " mode"
                        : "");
    return true;
}

}  // namespace detail

// Makes every map the build ships selectable in the map and mode menus.
inline bool install_hooks() { return detail::install(); }

// Total maps added across every mode bucket, for diagnostics.
inline int32_t unlocked_count() { return detail::g_total_unlocked; }

}  // namespace maps_unlock_2313
