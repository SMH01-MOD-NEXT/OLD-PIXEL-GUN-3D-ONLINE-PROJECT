#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) weapon module grant
//
// Unlocks every weapon module and reports each of them at level 10.
//
// How it works
// ------------
// ModulesController is a Singleton<ModulesController> that keeps the player's
// owned modules in two lists:
//
//     private List<module>  <owned modules>   // +0x30, returned by the getter
//     private List<moduleSet> <owned sets>    // +0x38, returned by the getter
//
// The build also ships a static catalogue class whose type initialiser
// constructs one instance of *every* module (42 of them on this build) and
// stores them in two static lists that carry the exact same obfuscated field
// names, and therefore the exact same element types, as the two controller
// fields above. That catalogue is what the stripped AddAllModulesDEV() helper
// used to publish before its body was removed.
//
// So instead of fabricating managed objects we simply serve the catalogue lists
// from the controller getters. The substitution is guarded by a length check,
// so a real inventory that is already larger is never replaced - the module can
// only ever add modules, never take them away.
//
// Level 10 is applied at the leaf accessor: the module type implements an
// IUpgradable-style pair of virtuals
//
//     slot 0  int <current level>()   RVA 0x024B13C8
//     slot 1  int <max level>()       RVA 0x024B14EC
//
// where the current level is derived from how many duplicates the player owns.
// Hooking the current-level getter and clamping it to the module's own maximum
// gives level 10 everywhere the UI, the stat maths and the analytics read it,
// without touching the duplicate counters. The max-level getter is resolved but
// never hooked, and it does not call back into the current-level getter, so the
// clamp cannot recurse.
//
// Nothing here writes to Progress, so no save round-trip and no cheat-detection
// churn is introduced; the grant is process-scoped and fully reversible by
// restarting the game without the mod.
//
// Deliberately NOT used: ModulesController::AddAllModulesDEV() (0x028178E4) and
// AddAllMaxModulesDEV() (0x028178E8). Both are dead single-RET stubs on 23.1.3.
//
// Every managed identifier below is generated from the 23.1.3 global-metadata
// by gen_craft.py and verified byte for byte against the method table.
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

// Requested module level. Clamped per module to that module's own maximum, so a
// module that tops out below 10 reports its real ceiling instead of a value the
// upgrade tables cannot describe.
constexpr int32_t kTargetModuleLevel = 10;

// Throttle for the repeating hooks so logcat stays readable.
constexpr uint64_t kLogEvery = 240;

// ----------------------------------------------------------- metadata names

// Static catalogue that materialises every module at type-init.
constexpr const char* kCatalogNs = "PGCompany";
constexpr const char* kCatalogClass = "丐丞丒专且丁丈丌业";
constexpr const char* kCatalogModules = "丞七丌业丛丂丙上丝";  // static, 0 args -> List<module>
constexpr int kCatalogModulesArgs = 0;
constexpr const char* kCatalogSets = "丂丟世丅丛丙业丛专";        // static, 0 args -> List<moduleSet>
constexpr int kCatalogSetsArgs = 0;

// Owned-inventory getters on the controller.
constexpr const char* kControllerNs = "PGCompany";
constexpr const char* kControllerClass = "ModulesController";
constexpr const char* kOwnedModules = "七丄丛丕业丂专丞东";   // instance, 0 args -> List<module>
constexpr int kOwnedModulesArgs = 0;
constexpr const char* kOwnedSets = "一且三不丁万丅上丑";         // instance, 0 args -> List<moduleSet>
constexpr int kOwnedSetsArgs = 0;

// Per-module level pair.
constexpr const char* kModuleNs = "PGCompany";
constexpr const char* kModuleClass = "丐三七世丝丗与丛上";
constexpr const char* kModuleLevel = "丌丏业丁丅丑与丆丕";           // instance, 0 args -> int
constexpr int kModuleLevelArgs = 0;
constexpr const char* kModuleMaxLevel = "丗丂丙上丏丏专上专";     // instance, 0 args -> int
constexpr int kModuleMaxLevelArgs = 0;

// ------------------------------------------------------------- managed ABI

using StaticObjFn = void* (*)(void* method);
using InstanceObjFn = void* (*)(void* self, void* method);
using InstanceIntFn = int32_t (*)(void* self, void* method);
using ListCountFn = int32_t (*)(void* list, void* method);

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

// List<T> is a generic instantiation, so Count is resolved off the object.
inline int32_t list_count(void* list) {
    if (list == nullptr || il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_method_from_name == nullptr) {
        return -1;
    }
    void* klass = il2cpp::object_get_class(list);
    if (klass == nullptr) {
        return -1;
    }
    void* info = il2cpp::class_get_method_from_name(klass, "get_Count", 0);
    if (info == nullptr) {
        return -1;
    }
    void* ptr = il2cpp::method_pointer(info);
    if (ptr == nullptr) {
        return -1;
    }
    return reinterpret_cast<ListCountFn>(ptr)(list, info);
}

// ------------------------------------------------------------------- state

inline Managed g_catalog_modules{};
inline Managed g_catalog_sets{};
inline Managed g_max_level{};

inline InstanceObjFn g_orig_owned_modules = nullptr;
inline InstanceObjFn g_orig_owned_sets = nullptr;
inline InstanceIntFn g_orig_level = nullptr;

inline uint64_t g_modules_calls = 0;
inline uint64_t g_sets_calls = 0;
inline uint64_t g_level_calls = 0;
inline bool g_announced_modules = false;
inline bool g_announced_sets = false;

// The catalogue type initialiser builds every module the first time it is
// touched, and that construction path can walk back into the controller. A
// plain re-entrancy latch keeps the hooks pass-through while it runs.
inline thread_local bool g_in_catalog = false;

inline void* fetch_catalog(const Managed& source) {
    if (!source || g_in_catalog) {
        return nullptr;
    }
    g_in_catalog = true;
    void* list = reinterpret_cast<StaticObjFn>(source.ptr)(source.info);
    g_in_catalog = false;
    return list;
}

// Serves the catalogue only when it is strictly richer than what the game would
// have returned, so the hook can add modules but never remove any.
inline void* widen(void* original, const Managed& source, bool& announced,
                   const char* what) {
    void* full = fetch_catalog(source);
    if (full == nullptr || full == original) {
        return original;
    }
    const int32_t have = list_count(original);
    const int32_t all = list_count(full);
    if (all <= 0 || all <= have) {
        return original;
    }
    if (!announced) {
        announced = true;
        LOGI("23.1.3-modules: serving full %s catalogue (%d entries, was %d)",
             what, all, have);
    }
    return full;
}

// --------------------------------------------------------------- hook bodies

inline void* owned_modules_hook(void* self, void* method) {
    void* original = nullptr;
    if (g_orig_owned_modules != nullptr) {
        original = g_orig_owned_modules(self, method);
    }
    ++g_modules_calls;
    return widen(original, g_catalog_modules, g_announced_modules, "module");
}

inline void* owned_sets_hook(void* self, void* method) {
    void* original = nullptr;
    if (g_orig_owned_sets != nullptr) {
        original = g_orig_owned_sets(self, method);
    }
    ++g_sets_calls;
    return widen(original, g_catalog_sets, g_announced_sets, "module set");
}

inline int32_t module_level_hook(void* self, void* method) {
    int32_t original = 0;
    if (g_orig_level != nullptr) {
        original = g_orig_level(self, method);
    }

    int32_t target = kTargetModuleLevel;
    if (g_max_level && self != nullptr) {
        const int32_t cap = reinterpret_cast<InstanceIntFn>(g_max_level.ptr)(self, g_max_level.info);
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
    resolved &= bind(g_catalog_modules, kCatalogNs, kCatalogClass, kCatalogModules,
                     kCatalogModulesArgs);
    resolved &= bind(g_catalog_sets, kCatalogNs, kCatalogClass, kCatalogSets, kCatalogSetsArgs);
    if (!resolved) {
        LOGE("23.1.3-modules: static module catalogue unavailable, module disabled");
        return false;
    }

    // Resolve-only, never hooked. Without it every module simply reports the
    // unclamped target level.
    if (!bind(g_max_level, kModuleNs, kModuleClass, kModuleMaxLevel, kModuleMaxLevelArgs)) {
        LOGE("23.1.3-modules: max-level getter unavailable, clamping disabled");
    }

    bool ok = true;
    ok &= hook::install({kControllerNs, kControllerClass, kOwnedModules, kOwnedModulesArgs},
                        reinterpret_cast<void*>(&owned_modules_hook),
                        reinterpret_cast<void**>(&g_orig_owned_modules), true);
    ok &= hook::install({kControllerNs, kControllerClass, kOwnedSets, kOwnedSetsArgs},
                        reinterpret_cast<void*>(&owned_sets_hook),
                        reinterpret_cast<void**>(&g_orig_owned_sets), true);
    ok &= hook::install({kModuleNs, kModuleClass, kModuleLevel, kModuleLevelArgs},
                        reinterpret_cast<void*>(&module_level_hook),
                        reinterpret_cast<void**>(&g_orig_level), true);

    if (ok) {
        LOGI("23.1.3-modules: all weapon modules unlocked at level %d", kTargetModuleLevel);
    } else {
        LOGE("23.1.3-modules: install incomplete, module grant disabled");
    }
    return ok;
}

}  // namespace detail

inline bool install_hooks() { return detail::install(); }

}  // namespace weapon_modules_2313
