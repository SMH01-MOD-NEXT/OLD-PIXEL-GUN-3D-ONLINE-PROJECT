#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) weapon-module grant
//
// Makes the complete built-in catalogue (42 modules and 42 module-set entries)
// available in ModulesController and reports every module at level X (10).
//
// The implementation deliberately patches live data and the real level source:
//
// * ModulesController's trivial list getters have zero direct BL call sites;
//   IL2CPP/clang inlined them. Hooking those bodies installs successfully but
//   never changes the inventory.
// * The catalogue .cctor contains exactly 42 calls to module::.ctor(ItemRecord)
//   and 42 calls to moduleSet::.ctor(ItemRecord). These are the complete objects
//   already shipped by this build, so no managed objects are fabricated.
// * ModulesController::OnInstanceCreated builds the profile-backed lists. It is
//   invoked indirectly by Singleton<T>, so a direct-BL-only scan cannot see it.
//   A post-initialization merge covers newly created controllers. For a singleton
//   created before hook installation, the live UI storage getter is intercepted
//   before it builds its filtered cache, so all 42 entries are present in time.
// * module::七且丐东丒丆丑丈万() (0x024B0BB0) is the actual current-level source.
//   The modules UI prints its result as a Roman numeral. The earlier revision
//   incorrectly returned 0x024B0C38, which is the total owned-parts count; that
//   is exactly why the UI showed XL/XX/LXXX instead of X.
// * 0x024B13C8, 0x024B140C and 0x024B14EC are respectively the next-level
//   requirement, cumulative parts threshold and progress inside the level.
//   They must not be clamped or replaced.
//
// All mutations are process-local. Nothing is written directly to Progress.
// Entries already present are retained; verified full-list publication is the fallback.
// -----------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

namespace weapon_modules_2313 {
namespace detail {

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

constexpr int32_t kTargetModuleLevel = 10;
constexpr int32_t kExpectedModules = 42;
constexpr int32_t kExpectedModuleSets = 42;
constexpr int32_t kMaxCatalogEntries = 1024;
constexpr uint64_t kLevelRecheckEvery = 240;
constexpr uint64_t kLevelLogEvery = 240;

constexpr const char* kNs = "PGCompany";
constexpr const char* kCatalogClass = "丐丞丒专且丁丈丌业";
constexpr const char* kCatalogModules = "丞七丌业丛丂丙上丝";
constexpr const char* kCatalogSets = "丂丟世丅丛丙业丛专";
constexpr const char* kControllerClass = "ModulesController";
constexpr const char* kOwnedModulesField = "丅与世丕业丘不丂丈";
constexpr const char* kOwnedSetsField = "下丘丌一丞丛丂三丗";
constexpr const char* kStorageByNameField = "丞丘丁一一上与且业";
constexpr const char* kStorageByIndexField = "丙丕一东丛万丕丄丘";
constexpr const char* kOnInstanceCreated = "OnInstanceCreated";
constexpr const char* kControllerReload = "丞业丝丁丆丑丑丕丟";
constexpr const char* kControllerSelected = "上丂丁丙丛万丐万丗";
constexpr const char* kModuleClass = "丐三七世丝丗与丛上";
constexpr const char* kCurrentLevel = "七且丐东丒丆丑丈万";

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
        LOGE("23.1.3-modules: %s::%s/%d not found in metadata", klass, method,
             args_count);
        return false;
    }
    void* ptr = il2cpp::method_pointer(info);
    if (ptr == nullptr) {
        LOGE("23.1.3-modules: %s::%s/%d has no compiled body", klass, method,
             args_count);
        return false;
    }
    out.info = info;
    out.ptr = ptr;
    return true;
}

struct ListApi {
    void* count_info = nullptr;
    void* count_ptr = nullptr;
    void* item_info = nullptr;
    void* item_ptr = nullptr;
    void* contains_info = nullptr;
    void* contains_ptr = nullptr;
    void* add_info = nullptr;
    void* add_ptr = nullptr;

    bool readable() const noexcept {
        return count_info != nullptr && count_ptr != nullptr && item_info != nullptr &&
               item_ptr != nullptr;
    }
    bool writable() const noexcept {
        return count_info != nullptr && count_ptr != nullptr &&
               contains_info != nullptr && contains_ptr != nullptr &&
               add_info != nullptr && add_ptr != nullptr;
    }
};

inline bool resolve_list_api(void* list, ListApi& api) {
    if (list == nullptr || il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_method_from_name == nullptr) {
        return false;
    }
    void* klass = il2cpp::object_get_class(list);
    if (klass == nullptr) return false;
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
    if (!resolve_list_api(list, api) || api.count_ptr == nullptr) return -1;
    return reinterpret_cast<ListCountFn>(api.count_ptr)(list, api.count_info);
}

inline Managed g_catalog_modules{};
inline Managed g_catalog_sets{};
inline void* g_field_owned_modules = nullptr;
inline void* g_field_owned_sets = nullptr;
inline void* g_field_storage_by_name = nullptr;
inline void* g_field_storage_by_index = nullptr;
inline InstanceVoidFn g_orig_on_instance_created = nullptr;
inline InstanceVoidFn g_orig_reload = nullptr;
inline InstanceObjFn g_orig_selected = nullptr;
inline InstanceIntFn g_orig_current_level = nullptr;
inline void* g_controller = nullptr;
inline uint64_t g_grant_attempts = 0;
inline uint64_t g_selected_calls = 0;
inline uint64_t g_level_calls = 0;
inline bool g_catalog_size_reported = false;
inline bool g_modules_complete_reported = false;
inline bool g_sets_complete_reported = false;
inline bool g_modules_error_reported = false;
inline bool g_sets_error_reported = false;
inline bool g_storage_reset_reported = false;
inline bool g_storage_reset_done = false;
inline thread_local bool g_in_catalog = false;
inline thread_local bool g_in_grant = false;

inline void* fetch_catalog(const Managed& source) {
    if (!source || g_in_catalog) return nullptr;
    g_in_catalog = true;
    void* result = reinterpret_cast<StaticObjFn>(source.ptr)(source.info);
    g_in_catalog = false;
    return result;
}

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

struct MergeResult {
    int32_t source = -1;
    int32_t before = -1;
    int32_t added = 0;
    int32_t after = -1;
    bool ok = false;
    bool replaced = false;
    const char* failure = nullptr;
};

inline void publish_full_list(void* self, void* field, void* full,
                              MergeResult& result) {
    write_object_field(self, field, full);
    void* published = read_object_field(self, field);
    result.replaced = published == full;
    result.after = list_count(published);
    const int32_t baseline = result.before > 0 ? result.before : 0;
    result.added = result.after > baseline ? result.after - baseline : 0;
    result.ok = published != nullptr && result.after == result.source;
    if (!result.ok) result.failure = "could not publish full built-in list";
}

inline MergeResult merge_catalog(void* self, void* field, const Managed& source) {
    MergeResult result;
    void* full = fetch_catalog(source);
    if (full == nullptr) {
        result.failure = "catalog getter returned null";
        return result;
    }
    ListApi full_api;
    if (!resolve_list_api(full, full_api) || !full_api.readable()) {
        result.failure = "catalog List<T> API unavailable";
        return result;
    }
    result.source = reinterpret_cast<ListCountFn>(full_api.count_ptr)(
        full, full_api.count_info);
    if (result.source <= 0 || result.source > kMaxCatalogEntries) {
        result.failure = "catalog count outside safety bounds";
        return result;
    }

    void* owned = read_object_field(self, field);
    result.before = list_count(owned);
    if (owned == nullptr) {
        publish_full_list(self, field, full, result);
        return result;
    }
    if (owned == full) {
        result.after = result.before;
        result.ok = result.after == result.source;
        if (!result.ok) result.failure = "published catalogue count changed";
        return result;
    }

    ListApi owned_api;
    if (!resolve_list_api(owned, owned_api) || !owned_api.writable()) {
        publish_full_list(self, field, full, result);
        return result;
    }
    for (int32_t index = 0; index < result.source; ++index) {
        void* item = reinterpret_cast<ListItemFn>(full_api.item_ptr)(
            full, index, full_api.item_info);
        if (item == nullptr) continue;
        const bool present = reinterpret_cast<ListContainsFn>(owned_api.contains_ptr)(
            owned, item, owned_api.contains_info);
        if (present) continue;
        reinterpret_cast<ListAddFn>(owned_api.add_ptr)(owned, item, owned_api.add_info);
        ++result.added;
    }
    result.after = reinterpret_cast<ListCountFn>(owned_api.count_ptr)(
        owned, owned_api.count_info);
    result.ok = result.after >= result.source;
    if (!result.ok) publish_full_list(self, field, full, result);
    return result;
}

inline void report_catalog_sizes(int32_t modules, int32_t sets) {
    if (g_catalog_size_reported || modules <= 0 || sets <= 0) return;
    g_catalog_size_reported = true;
    if (modules == kExpectedModules && sets == kExpectedModuleSets) {
        LOGI("23.1.3-modules: verified built-in catalogue: %d modules, %d module sets",
             modules, sets);
    } else {
        LOGE("23.1.3-modules: unexpected catalogue size: modules=%d (expected %d), "
             "sets=%d (expected %d)", modules, kExpectedModules, sets,
             kExpectedModuleSets);
    }
}

inline void report_merge(const char* reason, const char* what,
                         const MergeResult& result, bool& complete_reported,
                         bool& error_reported) {
    if (result.added > 0 || result.replaced) {
        LOGI("23.1.3-modules: %s: %s source=%d owned=%d +%d -> %d%s", reason,
             what, result.source, result.before, result.added, result.after,
             result.replaced ? " (published full list)" : "");
    }
    if (result.ok && !complete_reported) {
        complete_reported = true;
        LOGI("23.1.3-modules: %s inventory complete (%d/%d)", what,
             result.after, result.source);
    } else if (!result.ok && !error_reported) {
        error_reported = true;
        LOGE("23.1.3-modules: %s: %s grant failed at '%s' "
             "(source=%d before=%d added=%d after=%d)", reason, what,
             result.failure != nullptr ? result.failure : "unknown",
             result.source, result.before, result.added, result.after);
    }
}

struct GrantStatus {
    bool complete = false;
    bool changed = false;
};

inline GrantStatus grant(void* self, const char* reason) {
    GrantStatus status;
    if (self == nullptr || g_in_grant) return status;
    g_in_grant = true;
    g_controller = self;
    ++g_grant_attempts;
    const MergeResult modules = merge_catalog(
        self, g_field_owned_modules, g_catalog_modules);
    const MergeResult sets = merge_catalog(
        self, g_field_owned_sets, g_catalog_sets);
    report_catalog_sizes(modules.source, sets.source);
    report_merge(reason, "module", modules, g_modules_complete_reported,
                 g_modules_error_reported);
    report_merge(reason, "module-set", sets, g_sets_complete_reported,
                 g_sets_error_reported);
    status.complete = modules.ok && sets.ok &&
                      modules.source == kExpectedModules &&
                      sets.source == kExpectedModuleSets;
    status.changed = modules.added > 0 || sets.added > 0 ||
                     modules.replaced || sets.replaced;
    g_in_grant = false;
    return status;
}

inline bool clear_collection_field(void* self, void* field) {
    void* collection = read_object_field(self, field);
    if (collection == nullptr) return true;
    if (il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_method_from_name == nullptr) return false;
    void* klass = il2cpp::object_get_class(collection);
    if (klass == nullptr) return false;
    void* info = il2cpp::class_get_method_from_name(klass, "Clear", 0);
    void* ptr = info != nullptr ? il2cpp::method_pointer(info) : nullptr;
    if (ptr == nullptr) return false;
    reinterpret_cast<InstanceVoidFn>(ptr)(collection, info);
    return true;
}

inline void reset_storage_models(void* self, const char* reason) {
    if (g_storage_reset_done) return;
    const bool by_name = clear_collection_field(self, g_field_storage_by_name);
    const bool by_index = clear_collection_field(self, g_field_storage_by_index);
    if (by_name && by_index) {
        g_storage_reset_done = true;
        if (!g_storage_reset_reported) {
            g_storage_reset_reported = true;
            LOGI("23.1.3-modules: %s: invalidated cached storage models before UI build",
                 reason);
        }
    } else {
        LOGE("23.1.3-modules: %s: could not invalidate storage models "
             "(by-name=%d by-index=%d)", reason, by_name ? 1 : 0,
             by_index ? 1 : 0);
    }
}

inline void on_instance_created_hook(void* self, void* method) {
    if (g_orig_on_instance_created != nullptr) {
        g_orig_on_instance_created(self, method);
    }
    g_modules_complete_reported = false;
    g_sets_complete_reported = false;
    g_modules_error_reported = false;
    g_sets_error_reported = false;
    g_storage_reset_done = false;
    const GrantStatus status = grant(self, "OnInstanceCreated");
    if (status.complete) reset_storage_models(self, "OnInstanceCreated");
}

inline void reload_hook(void* self, void* method) {
    if (g_orig_reload != nullptr) g_orig_reload(self, method);
    g_storage_reset_done = false;
    const GrantStatus status = grant(self, "profile reload");
    if (status.complete) reset_storage_models(self, "profile reload");
}

inline void* selected_hook(void* self, void* method) {
    ++g_selected_calls;
    const GrantStatus status = grant(self, "module UI prebuild");
    if (status.complete) reset_storage_models(self, "module UI prebuild");
    return g_orig_selected != nullptr ? g_orig_selected(self, method) : nullptr;
}

inline int32_t current_level_hook(void* self, void* method) {
    int32_t original = 0;
    if (g_orig_current_level != nullptr) {
        original = g_orig_current_level(self, method);
    }
    ++g_level_calls;
    if (g_controller != nullptr && !g_in_grant &&
        (g_level_calls % kLevelRecheckEvery) == 1) {
        (void)grant(g_controller, "level fallback");
    }
    const int32_t level = original >= kTargetModuleLevel
                              ? original
                              : kTargetModuleLevel;
    if ((g_level_calls % kLevelLogEvery) == 1) {
        LOGI("23.1.3-modules: displayed level %d -> %d (call %llu)",
             original, level, static_cast<unsigned long long>(g_level_calls));
    }
    return level;
}

inline bool install() {
    bool resolved = true;
    resolved &= bind(g_catalog_modules, kNs, kCatalogClass, kCatalogModules, 0);
    resolved &= bind(g_catalog_sets, kNs, kCatalogClass, kCatalogSets, 0);
    if (!resolved) {
        LOGE("23.1.3-modules: built-in catalogue unavailable, module disabled");
        return false;
    }
    g_field_owned_modules = il2cpp::find_field(
        kNs, kControllerClass, kOwnedModulesField);
    g_field_owned_sets = il2cpp::find_field(
        kNs, kControllerClass, kOwnedSetsField);
    g_field_storage_by_name = il2cpp::find_field(
        kNs, kControllerClass, kStorageByNameField);
    g_field_storage_by_index = il2cpp::find_field(
        kNs, kControllerClass, kStorageByIndexField);
    if (g_field_owned_modules == nullptr || g_field_owned_sets == nullptr ||
        g_field_storage_by_name == nullptr || g_field_storage_by_index == nullptr) {
        LOGE("23.1.3-modules: controller fields unavailable "
             "(modules=%p sets=%p by-name=%p by-index=%p), module disabled",
             g_field_owned_modules, g_field_owned_sets, g_field_storage_by_name,
             g_field_storage_by_index);
        return false;
    }

    bool ok = true;
    ok &= hook::install({kNs, kControllerClass, kOnInstanceCreated, 0},
                        reinterpret_cast<void*>(&on_instance_created_hook),
                        reinterpret_cast<void**>(&g_orig_on_instance_created), true);
    ok &= hook::install({kNs, kControllerClass, kControllerReload, 0},
                        reinterpret_cast<void*>(&reload_hook),
                        reinterpret_cast<void**>(&g_orig_reload), true);
    ok &= hook::install({kNs, kControllerClass, kControllerSelected, 0},
                        reinterpret_cast<void*>(&selected_hook),
                        reinterpret_cast<void**>(&g_orig_selected), true);
    ok &= hook::install({kNs, kModuleClass, kCurrentLevel, 0},
                        reinterpret_cast<void*>(&current_level_hook),
                        reinterpret_cast<void**>(&g_orig_current_level), true);
    if (ok) {
        LOGI("23.1.3-modules: corrected prebuild grant armed (expect %d modules at level X)",
             kExpectedModules);
    } else {
        LOGE("23.1.3-modules: install incomplete, module disabled");
    }
    return ok;
}

}  // namespace detail

inline bool install_hooks() { return detail::install(); }

}  // namespace weapon_modules_2313
