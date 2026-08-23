#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) weapon module grant
//
// Unlocks every weapon module and reports each of them at its maximum level.
//
// -----------------------------------------------------------------------------
// Why the first revision of this file changed nothing on the device
// -----------------------------------------------------------------------------
// It hooked the two owned-inventory getters plus the per-module level getter:
//
//     ModulesController::七丄丛丕业丂专丞东()  RVA 0x0281473C -> List<module>
//     ModulesController::一且三不丁万丅上丑()  RVA 0x02814784 -> List<moduleSet>
//     module::丌丏业丁丅丑与丆丕()              RVA 0x024B13C8 -> int  (slot 30)
//
// All three resolve in metadata and all three inline hooks install, so the log
// looked healthy - and still nothing changed in game.
//
// IL2CPP translates managed code to C++ and then lets clang optimise it. Both
// list getters are single-field reads, so clang inlined them into every caller.
// Decoding all 2,262,118 BL instructions in libil2cpp.so and resolving their
// targets against the 23.1.3 method table gives:
//
//     0x0281473C  owned modules getter ......  0 direct call sites
//     0x02814784  owned sets getter .........  0 direct call sites
//     0x03048A5C  static catalogue getter ...  0 direct call sites
//     0x024B13C8  current level (slot 30) ...  8 direct call sites
//     0x024B0BB0  owned duplicate count ..... 24 direct call sites
//
// A hook on a method with zero call sites can never fire - the game reads the
// backing fields directly. That is why no module was ever added. The level hook
// did fire, but only on the 8 surviving call sites (InventoryItemView,
// StorePromotionOffersView, ...); the modules screen reads its own inlined copy,
// which is why the few owned modules kept reporting level 1.
//
// -----------------------------------------------------------------------------
// What this revision does instead
// -----------------------------------------------------------------------------
// 1. It writes the data, not the accessor. The owned inventory lives in
//
//        private List<module>    丅与世丕业丘不丂丈;  // +0x30
//        private List<moduleSet> 下丘丌一丞丛丂三丗;  // +0x38
//
//    and every inlined call site reads exactly those fields, so merging the
//    static catalogue into them is observed everywhere: UI, stat maths, saves.
//
// 2. The merge is driven from methods that really are called, with the
//    controller instance as `this`:
//
//        ModulesController::丞业丝丁丆丑丑丕丟()  0x02815668  <- MainMenuController.Awake()
//        ModulesController::上丂丁丙丛万丐万丗()  0x02815F94  <- 9 UI call sites
//
// 3. Levels are raised at their source. The current level is
//    countToLevel(ownedDuplicates) and the duplicate counter
//    七且丐东丒丆丑丈万() (0x024B0BB0) has 24 live call sites - it is even called by
//    the level getter itself (BL at 0x024B13D4). Reporting the module's
//    configured maximum there makes level, upgrade progress and "is maxed"
//    agree everywhere. That maximum comes from 与丏一丗七丝一七丏() (0x024B0C38),
//    which reads the balance-config singleton and never calls back into the
//    duplicate counter, so the substitution cannot recurse.
//
// 4. The level getter hook is kept as a belt-and-braces measure for its 8 live
//    call sites, still clamped to the module's own maximum level.
//
// Merging never removes anything: entries already present are skipped through
// List<T>.Contains, so a real inventory can only ever grow. Nothing is written
// to Progress, so no save round-trip and no cheat-detection churn is added; the
// grant is process-scoped and reverts by launching the game without the mod.
//
// Deliberately NOT used: ModulesController::AddAllModulesDEV() (0x028178E4) and
// AddAllMaxModulesDEV() (0x028178E8). Both are dead single-RET stubs on 23.1.3
// (the four bytes at each offset are C0 03 5F D6).
//
// Every managed identifier below was verified by hand against the 23.1.3 dump
// (class bodies at dump2313.cs:466848, :469613 and :476015).
// -----------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

namespace weapon_modules_2313 {
namespace detail {

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

// ------------------------------------------------------------------ tunables

// Requested module level for the fallback level hook. Clamped per module to
// that module's own maximum so a module that tops out below 10 reports its real
// ceiling instead of a value the upgrade tables cannot describe.
constexpr int32_t kTargetModuleLevel = 10;

// Upper bound for one merge pass; the 23.1.3 catalogue holds ~42 modules.
constexpr int32_t kMaxCatalogEntries = 1024;

// The UI entry point fires often, so only re-check the inventory periodically.
constexpr uint64_t kRegrantEvery = 24;

// Hard stop so a pathological UI loop cannot turn the merge into a hot path.
constexpr uint64_t kMaxGrantPasses = 96;

// Throttle for the repeating level hook so logcat stays readable.
constexpr uint64_t kLogEvery = 240;

// ----------------------------------------------------------- metadata names

constexpr const char* kNs = "PGCompany";

// Static catalogue: its type initialiser materialises every module.
constexpr const char* kCatalogClass = "丐丞丒专且丁丈丌业";
constexpr const char* kCatalogModules = "丞七丌业丛丂丙上丝";  // static/0 -> List<module>
constexpr const char* kCatalogSets = "丂丟世丅丛丙业丛专";      // static/0 -> List<moduleSet>

// Controller that owns the player's inventory.
constexpr const char* kControllerClass = "ModulesController";
constexpr const char* kOwnedModulesField = "丅与世丕业丘不丂丈";  // List<module>    +0x30
constexpr const char* kOwnedSetsField = "下丘丌一丞丛丂三丗";      // List<moduleSet> +0x38
constexpr const char* kControllerReload = "丞业丝丁丆丑丑丕丟";     // instance/0, from MainMenuController.Awake
constexpr const char* kControllerSelected = "上丂丁丙丛万丐万丗";   // instance/0, 9 UI call sites

// Per-module accessors.
constexpr const char* kModuleClass = "丐三七世丝丗与丛上";
constexpr const char* kModuleOwnedCount = "七且丐东丒丆丑丈万";  // instance/0 -> int, 24 call sites
constexpr const char* kModuleMaxCount = "与丏一丗七丝一七丏";    // instance/0 -> int, from config
constexpr const char* kModuleLevel = "丌丏业丁丅丑与丆丕";        // instance/0 -> int, slot 30
constexpr const char* kModuleMaxLevel = "丗丂丙上丏丏专上专";    // instance/0 -> int, slot 31

// ------------------------------------------------------------- managed ABI

using StaticObjFn = void* (*)(void* method);
using InstanceObjFn = void* (*)(void* self, void* method);
using InstanceVoidFn = void (*)(void* self, void* method);
using InstanceIntFn = int32_t (*)(void* self, void* method);
using ListCountFn = int32_t (*)(void* list, void* method);
using ListItemFn = void* (*)(void* list, int32_t index, void* method);
using ListContainsFn = bool (*)(void* list, void* item, void* method);
using ListAddFn = void (*)(void* list, void* item, void* method);

struct Managed {
    void* info = nullptr;
    void* ptr = nullptr;
    explicit operator bool() const noexcept { return info != nullptr && ptr != nullptr; }
};

inline bool bind(Managed& out, const char* namespaze, const char* klass,
                 const char* method, int args_count) {
    void* info = il2cpp::find_method_info(namespaze, klass, method, args_count);
    if (info == nullptr) {
        LOGE("23.1.3-modules: %s::%s/%d not found in metadata", klass, method, args_count);
        return false;
    }
    void* ptr = il2cpp::method_pointer(info);
    if (ptr == nullptr) {
        LOGE("23.1.3-modules: %s::%s/%d has no compiled body", klass, method, args_count);
        return false;
    }
    out.info = info;
    out.ptr = ptr;
    return true;
}

// List<T> is a generic instantiation, so its members are resolved off the live
// object rather than through the metadata name lookup.
struct ListApi {
    void* count_info = nullptr;
    void* count_ptr = nullptr;
    void* item_info = nullptr;
    void* item_ptr = nullptr;
    void* contains_info = nullptr;
    void* contains_ptr = nullptr;
    void* add_info = nullptr;
    void* add_ptr = nullptr;

    bool readable() const noexcept { return count_ptr != nullptr && item_ptr != nullptr; }
    bool writable() const noexcept { return count_ptr != nullptr && add_ptr != nullptr; }
};

inline bool resolve_list_api(void* list, ListApi& api) {
    if (list == nullptr || il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_method_from_name == nullptr) {
        return false;
    }
    void* klass = il2cpp::object_get_class(list);
    if (klass == nullptr) {
        return false;
    }
    const auto pick = [&](const char* name, int argc, void*& info, void*& ptr) {
        info = il2cpp::class_get_method_from_name(klass, name, argc);
        ptr = info != nullptr ? il2cpp::method_pointer(info) : nullptr;
    };
    pick("get_Count", 0, api.count_info, api.count_ptr);
    pick("get_Item", 1, api.item_info, api.item_ptr);
    pick("Contains", 1, api.contains_info, api.contains_ptr);
    pick("Add", 1, api.add_info, api.add_ptr);
    return api.count_ptr != nullptr;
}

inline int32_t list_count(void* list) {
    ListApi api;
    if (!resolve_list_api(list, api)) {
        return -1;
    }
    return reinterpret_cast<ListCountFn>(api.count_ptr)(list, api.count_info);
}

// ------------------------------------------------------------------- state

inline Managed g_catalog_modules{};
inline Managed g_catalog_sets{};
inline Managed g_max_level{};
inline Managed g_max_count{};

inline void* g_field_owned_modules = nullptr;
inline void* g_field_owned_sets = nullptr;

inline InstanceVoidFn g_orig_reload = nullptr;
inline InstanceObjFn g_orig_selected = nullptr;
inline InstanceIntFn g_orig_owned_count = nullptr;
inline InstanceIntFn g_orig_level = nullptr;

inline uint64_t g_selected_calls = 0;
inline uint64_t g_level_calls = 0;
inline uint64_t g_grant_passes = 0;
inline bool g_announced_modules = false;
inline bool g_announced_sets = false;

// The catalogue type initialiser builds every module the first time it is
// touched, and that construction path can walk back into the controller. Plain
// re-entrancy latches keep the hooks pass-through while it runs.
inline thread_local bool g_in_catalog = false;
inline thread_local bool g_in_grant = false;

inline void* fetch_catalog(const Managed& source) {
    if (!source || g_in_catalog) {
        return nullptr;
    }
    g_in_catalog = true;
    void* list = reinterpret_cast<StaticObjFn>(source.ptr)(source.info);
    g_in_catalog = false;
    return list;
}

// ------------------------------------------------------------ field access

inline void* read_object_field(void* self, void* field) {
    if (self == nullptr || field == nullptr || il2cpp::field_get_value == nullptr) {
        return nullptr;
    }
    void* value = nullptr;
    il2cpp::field_get_value(self, field, &value);
    return value;
}

inline void write_object_field(void* self, void* field, void* value) {
    if (self == nullptr || field == nullptr || value == nullptr ||
        il2cpp::field_set_value == nullptr) {
        return;
    }
    il2cpp::field_set_value(self, field, &value);
}

// ------------------------------------------------------------------- merge

// Copies every catalogue entry the inventory does not have yet. Returns the
// number of appended entries, or -1 when the lists could not be walked.
inline int32_t merge_into(void* owned, void* full) {
    ListApi source;
    ListApi target;
    if (!resolve_list_api(full, source) || !source.readable()) {
        return -1;
    }
    if (!resolve_list_api(owned, target) || !target.writable()) {
        return -1;
    }

    const int32_t total =
        reinterpret_cast<ListCountFn>(source.count_ptr)(full, source.count_info);
    if (total <= 0) {
        return 0;
    }
    const int32_t limit = total < kMaxCatalogEntries ? total : kMaxCatalogEntries;

    int32_t added = 0;
    for (int32_t index = 0; index < limit; ++index) {
        void* item =
            reinterpret_cast<ListItemFn>(source.item_ptr)(full, index, source.item_info);
        if (item == nullptr) {
            continue;
        }
        if (target.contains_ptr != nullptr &&
            reinterpret_cast<ListContainsFn>(target.contains_ptr)(owned, item,
                                                                  target.contains_info)) {
            continue;
        }
        reinterpret_cast<ListAddFn>(target.add_ptr)(owned, item, target.add_info);
        ++added;
    }
    return added;
}

inline void grant_one(void* self, void* field, const Managed& catalog,
                      bool& announced, const char* what) {
    void* full = fetch_catalog(catalog);
    if (full == nullptr) {
        return;
    }

    void* owned = read_object_field(self, field);
    if (owned == nullptr) {
        // The controller has not allocated its inventory yet: publish the
        // catalogue itself. Same element type, same list class.
        write_object_field(self, field, full);
        if (!announced) {
            announced = true;
            LOGI("23.1.3-modules: published the full %s catalogue (%d entries) into an empty inventory",
                 what, list_count(full));
        }
        return;
    }
    if (owned == full) {
        return;
    }

    const int32_t added = merge_into(owned, full);
    if (added > 0) {
        LOGI("23.1.3-modules: added %d %s entries, inventory now holds %d", added, what,
             list_count(owned));
        announced = true;
    } else if (added < 0 && !announced) {
        announced = true;
        LOGE("23.1.3-modules: could not walk the %s lists (owned=%p catalogue=%p)", what,
             owned, full);
    }
}

inline void grant(void* self) {
    if (self == nullptr || g_in_grant || g_grant_passes >= kMaxGrantPasses) {
        return;
    }
    g_in_grant = true;
    ++g_grant_passes;
    grant_one(self, g_field_owned_modules, g_catalog_modules, g_announced_modules, "module");
    grant_one(self, g_field_owned_sets, g_catalog_sets, g_announced_sets, "module set");
    g_in_grant = false;
}

// --------------------------------------------------------------- hook bodies

// Runs once from MainMenuController.Awake(), i.e. right after the profile has
// been applied to the controller.
inline void reload_hook(void* self, void* method) {
    if (g_orig_reload != nullptr) {
        g_orig_reload(self, method);
    }
    grant(self);
}

// Live UI entry point: keeps the inventory topped up if the game rebuilds it
// (profile reload, backend response, scene change).
inline void* selected_hook(void* self, void* method) {
    void* result = nullptr;
    if (g_orig_selected != nullptr) {
        result = g_orig_selected(self, method);
    }
    if ((g_selected_calls++ % kRegrantEvery) == 0) {
        grant(self);
    }
    return result;
}

// Duplicate counter. Reporting the configured maximum makes the derived level
// and every "is maxed" check agree, including at the inlined call sites.
inline int32_t owned_count_hook(void* self, void* method) {
    int32_t original = 0;
    if (g_orig_owned_count != nullptr) {
        original = g_orig_owned_count(self, method);
    }
    if (!g_max_count || self == nullptr) {
        return original;
    }
    const int32_t cap =
        reinterpret_cast<InstanceIntFn>(g_max_count.ptr)(self, g_max_count.info);
    return cap > original ? cap : original;
}

// Fallback for the 8 call sites that still call the level getter for real.
inline int32_t module_level_hook(void* self, void* method) {
    int32_t original = 0;
    if (g_orig_level != nullptr) {
        original = g_orig_level(self, method);
    }

    int32_t target = kTargetModuleLevel;
    if (g_max_level && self != nullptr) {
        const int32_t cap =
            reinterpret_cast<InstanceIntFn>(g_max_level.ptr)(self, g_max_level.info);
        if (cap > 0 && target > cap) {
            target = cap;
        }
    }

    if ((g_level_calls++ % kLogEvery) == 0) {
        LOGI("23.1.3-modules: module level %d -> %d (call %llu)", original, target,
             static_cast<unsigned long long>(g_level_calls));
    }
    return original >= target ? original : target;
}

// ------------------------------------------------------------------ install

inline bool install() {
    bool resolved = true;
    resolved &= bind(g_catalog_modules, kNs, kCatalogClass, kCatalogModules, 0);
    resolved &= bind(g_catalog_sets, kNs, kCatalogClass, kCatalogSets, 0);
    if (!resolved) {
        LOGE("23.1.3-modules: static module catalogue unavailable, module disabled");
        return false;
    }

    g_field_owned_modules = il2cpp::find_field(kNs, kControllerClass, kOwnedModulesField);
    g_field_owned_sets = il2cpp::find_field(kNs, kControllerClass, kOwnedSetsField);
    if (g_field_owned_modules == nullptr || g_field_owned_sets == nullptr) {
        LOGE("23.1.3-modules: owned-inventory fields not found (modules=%p sets=%p), module disabled",
             g_field_owned_modules, g_field_owned_sets);
        return false;
    }

    // Resolve-only, never hooked. Without them the counter is left untouched
    // and the level hook falls back to the unclamped target level.
    if (!bind(g_max_count, kNs, kModuleClass, kModuleMaxCount, 0)) {
        LOGE("23.1.3-modules: max duplicate count unavailable, levels stay as the game computes them");
    }
    if (!bind(g_max_level, kNs, kModuleClass, kModuleMaxLevel, 0)) {
        LOGE("23.1.3-modules: max-level getter unavailable, clamping disabled");
    }

    bool ok = true;
    ok &= hook::install({kNs, kControllerClass, kControllerReload, 0},
                        reinterpret_cast<void*>(&reload_hook),
                        reinterpret_cast<void**>(&g_orig_reload), true);
    ok &= hook::install({kNs, kControllerClass, kControllerSelected, 0},
                        reinterpret_cast<void*>(&selected_hook),
                        reinterpret_cast<void**>(&g_orig_selected), true);
    ok &= hook::install({kNs, kModuleClass, kModuleOwnedCount, 0},
                        reinterpret_cast<void*>(&owned_count_hook),
                        reinterpret_cast<void**>(&g_orig_owned_count), true);
    ok &= hook::install({kNs, kModuleClass, kModuleLevel, 0},
                        reinterpret_cast<void*>(&module_level_hook),
                        reinterpret_cast<void**>(&g_orig_level), true);

    if (ok) {
        LOGI("23.1.3-modules: grant armed, every module unlocked at up to level %d",
             kTargetModuleLevel);
    } else {
        LOGE("23.1.3-modules: install incomplete, module grant disabled");
    }
    return ok;
}

}  // namespace detail

inline bool install_hooks() { return detail::install(); }

}  // namespace weapon_modules_2313
