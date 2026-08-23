#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) weapon-module grant
//
// Makes the complete built-in catalogue (42 modules and 42 module-set entries)
// available in ModulesController and reports every module at level X (10).
//
// Root cause of the previous revision being dead on device
// --------------------------------------------------------
// The level-source constant had been corrupted by a bad search/replace: two of
// its CJK code points (U+4E10 and U+4E12) had been rewritten to U+5341. The
// resulting identifier does not exist anywhere in this build's metadata, so the
// fail-closed hook wrapper refused to patch anything and the whole feature was
// switched off at startup:
//
//   hook: REQUIRED method not found: 丐三七世丝丗与丛上.七且十东十丆丑丈万/0
//   23.1.3-modules: install incomplete, module disabled
//   init: 23.1.3 port incomplete: ... lobby-catalog=1 modules=0
//
// U+5341 does not occur a single time in the supplied dump, which is proof that
// the spelling was never valid. The verified name is 七且丐东丒丆丑丈万.
// A static_assert below pins its exact UTF-8 bytes so this class of corruption
// can never reach a build again.
//
// Second defect: every hook was folded into one `ok` flag, so a single miss on
// a purely auxiliary route disabled the guaranteed level fix as well. Hooks are
// now split into one critical route and several optional ones.
//
// Verified facts (measured on the supplied libil2cpp.so, RVA == file offset)
// -------------------------------------------------------------------------
// * module::七且丐东丒丆丑丈万() (0x024B0BB0) is the real current-level source:
//   132-byte body with 24 direct BL call sites, so it is genuinely reachable
//   and cannot have been inlined away. The modules UI renders its result as a
//   Roman numeral. An earlier revision hooked 0x024B0C38, which is the total
//   owned-parts count; that is why the UI showed XL/XX/XXV/LXXX instead of X.
// * 0x024B13C8 (8 call sites), 0x024B140C (4) and 0x024B14EC (7) are the
//   next-level requirement, the cumulative parts threshold and the progress
//   inside the level. They must not be clamped or replaced.
// * The catalogue .cctor (0x02EF431C, 284308 bytes) contains exactly 42 calls
//   to module::.ctor (0x024B1150) and 42 calls to moduleSet::.ctor
//   (0x024B3278). The complete catalogue is already shipped, so no managed
//   objects are fabricated.
// * ModulesController's trivial list getters (0x0281473C, 0x02814784) and both
//   catalogue getters (0x03048A5C, 0x03048AB4) have zero direct BL call sites:
//   IL2CPP/clang inlined them. Hooking those bodies installs successfully but
//   never fires, which is why the very first revision changed nothing. We call
//   the catalogue getters ourselves through their method pointers instead.
// * AddAllModulesDEV (0x028178E4) and AddAllMaxModulesDEV (0x028178E8) are
//   4-byte bare RET stubs (D65F03C0). They are dead and cannot be used.
// * The auxiliary UI routes are weak entry points, so none of them may be
//   load-bearing: ModuleStorageView setter 0x028DCB90 is a 0x24-byte body with
//   only 4 call sites, its refresh 0x028DCBB4 has 0, ModuleArmoryInfoScreen
//   Awake 0x023B5C08 and open 0x023B5DB8 have 0 direct BL (runtime/delegate
//   dispatch) and prebuild 0x023B6554 has 1.
// * The guaranteed grant driver is therefore MainMenuController.Update, which
//   progression_2313.h hooks as a required target and which calls
//   pump_from_main_menu() every main-menu frame.
//
// All mutations are process-local. Nothing is written directly to Progress.
// Entries already present are retained; verified full-list publication is the
// fallback.
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
constexpr uint64_t kMainMenuRecheckEvery = 60;
constexpr uint64_t kPumpNullLogEvery = 300;

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
constexpr const char* kControllerInstance = "get_Instance";
constexpr const char* kStorageViewClass = "ModuleStorageView";
constexpr const char* kStorageViewSet = "丝万不丘下丄丄三丟";
constexpr const char* kStorageViewRefresh = "上专丅丑丘丟丙东与";
constexpr const char* kStorageViewModelField = "丁业丈东丝丆丅丞与";
constexpr const char* kInfoScreenClass = "ModuleArmoryInfoScreen";
constexpr const char* kInfoScreenAwake = "Awake";
constexpr const char* kInfoScreenShow = "东不丁丁丟丂丝不丁";
constexpr const char* kInfoScreenPrebuild = "丏丁丗东且三世丄丏";
constexpr const char* kModuleClass = "丐三七世丝丗与丛上";

// module::七且丐东丒丆丑丈万() @ RVA 0x024B0BB0 - the current-level source.
constexpr const char* kCurrentLevel = "七且丐东丒丆丑丈万";

// Regression guard for the corruption that disabled this module: the previous
// revision shipped U+5341 in place of U+4E10 and U+4E12, an identifier that
// does not exist in the 23.1.3 metadata. Pin the exact UTF-8 byte sequence of
// the verified name so a mangled spelling fails the build instead of the
// device.
constexpr bool equal_bytes(const char* a, const char* b) {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static_assert(
    equal_bytes(kCurrentLevel,
                "\xE4\xB8\x83"   // U+4E03 七
                "\xE4\xB8\x94"   // U+4E14 且
                "\xE4\xB8\x90"   // U+4E10 丐  (was corrupted to U+5341)
                "\xE4\xB8\x9C"   // U+4E1C 东
                "\xE4\xB8\x92"   // U+4E12 丒  (was corrupted to U+5341)
                "\xE4\xB8\x86"   // U+4E06 丆
                "\xE4\xB8\x91"   // U+4E11 丑
                "\xE4\xB8\x88"   // U+4E08 丈
                "\xE4\xB8\x87"), // U+4E07 万
    "kCurrentLevel must stay byte-identical to module::0x024B0BB0 as spelled in "
    "the 23.1.3 metadata; a corrupted identifier silently disables the module");

using StaticObjFn = void* (*)(void* method);
using InstanceObjFn = void* (*)(void* self, void* method);
using InstanceVoidFn = void (*)(void* self, void* method);
using InstanceIntFn = int32_t (*)(void* self, void* method);
using InstanceObjArgVoidFn = void (*)(void* self, void* value, void* method);
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
inline Managed g_controller_instance{};
inline Managed g_selected_method{};
inline void* g_field_owned_modules = nullptr;
inline void* g_field_owned_sets = nullptr;
inline void* g_field_storage_by_name = nullptr;
inline void* g_field_storage_by_index = nullptr;
inline void* g_field_storage_view_model = nullptr;
inline InstanceVoidFn g_orig_on_instance_created = nullptr;
inline InstanceVoidFn g_orig_reload = nullptr;
inline InstanceObjFn g_orig_selected = nullptr;
inline InstanceIntFn g_orig_current_level = nullptr;
inline InstanceObjArgVoidFn g_orig_storage_view_set = nullptr;
inline InstanceVoidFn g_orig_storage_view_refresh = nullptr;
inline InstanceVoidFn g_orig_screen_awake = nullptr;
inline InstanceObjArgVoidFn g_orig_screen_show = nullptr;
inline InstanceVoidFn g_orig_screen_prebuild = nullptr;
inline void* g_controller = nullptr;
inline uint64_t g_grant_attempts = 0;
inline uint64_t g_selected_calls = 0;
inline uint64_t g_level_calls = 0;
inline uint64_t g_main_menu_frames = 0;
inline uint64_t g_startup_frames = 0;
inline uint64_t g_pump_nulls = 0;
inline bool g_installed = false;
inline bool g_catalog_size_reported = false;
inline bool g_modules_complete_reported = false;
inline bool g_sets_complete_reported = false;
inline bool g_modules_error_reported = false;
inline bool g_sets_error_reported = false;
inline bool g_storage_reset_reported = false;
inline bool g_storage_reset_done = false;
inline bool g_storage_view_rebuilt = false;
inline bool g_storage_view_entry_reported = false;
inline bool g_controller_null_reported = false;
inline bool g_pump_reached_reported = false;
inline thread_local bool g_in_catalog = false;
inline thread_local bool g_in_grant = false;
inline thread_local bool g_in_prepare = false;

inline void* controller_instance() {
    if (!g_controller_instance) return nullptr;
    return reinterpret_cast<StaticObjFn>(g_controller_instance.ptr)(
        g_controller_instance.info);
}

// Calls the real storage getter. When our hook is installed the trampoline is
// the only safe entry (calling the patched method pointer would re-enter the
// hook); when the optional hook could not be installed the original method
// pointer is still callable directly.
inline void* call_original_selected(void* controller) {
    if (controller == nullptr) return nullptr;
    if (g_orig_selected != nullptr && g_selected_method.info != nullptr) {
        return g_orig_selected(controller, g_selected_method.info);
    }
    if (g_selected_method) {
        return reinterpret_cast<InstanceObjFn>(g_selected_method.ptr)(
            controller, g_selected_method.info);
    }
    return nullptr;
}

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
        // Diagnostic only. The grant publishes whatever the build ships, so an
        // unexpected size must not stop the inventory from being completed.
        LOGE("23.1.3-modules: unexpected catalogue size: modules=%d (expected %d), "
             "sets=%d (expected %d); granting the reported counts anyway",
             modules, kExpectedModules, sets, kExpectedModuleSets);
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
    // Completion means "every entry the build ships is now owned". It is
    // deliberately not tied to the hard-coded 42/42 expectation: if this build
    // ever shipped a different catalogue size, the UI must still be refreshed
    // instead of silently never rebuilding.
    status.complete = modules.ok && sets.ok;
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

inline void* prepare_storage_model(const char* reason) {
    if (g_in_prepare) return nullptr;
    g_in_prepare = true;
    void* model = nullptr;
    void* controller = controller_instance();
    if (controller == nullptr) {
        if (!g_controller_null_reported) {
            g_controller_null_reported = true;
            LOGE("23.1.3-modules: %s: ModulesController.Instance is null", reason);
        }
        g_in_prepare = false;
        return nullptr;
    }
    if (!g_storage_view_rebuilt) g_storage_reset_done = false;
    const GrantStatus status = grant(controller, reason);
    if (status.changed) g_storage_reset_done = false;
    if (!status.complete) {
        g_in_prepare = false;
        return nullptr;
    }
    reset_storage_models(controller, reason);
    model = call_original_selected(controller);
    if (model == nullptr) {
        LOGE("23.1.3-modules: %s: rebuilt storage model is null", reason);
        g_in_prepare = false;
        return nullptr;
    }
    if (!g_storage_view_rebuilt) {
        LOGI("23.1.3-modules: %s: replaced ModuleStorageView model after full grant",
             reason);
    }
    g_storage_view_rebuilt = true;
    g_in_prepare = false;
    return model;
}

// Guaranteed grant driver.
//
// Every controller/UI route measured on this build is a weak entry point (see
// the file header), so the grant is driven from a path the device logs prove
// executes: MainMenuController.Update, hooked as a required target in
// progression_2313.h. pump() is idempotent, self-throttling and always logs its
// outcome once, so a failure can no longer be silent.
inline void pump(const char* reason) {
    void* controller = controller_instance();
    if (controller == nullptr) {
        ++g_pump_nulls;
        if (g_pump_nulls == 1u || (g_pump_nulls % kPumpNullLogEvery) == 0u) {
            LOGE("23.1.3-modules: %s: ModulesController.Instance is not available yet "
                 "(attempt %llu)", reason,
                 static_cast<unsigned long long>(g_pump_nulls));
        }
        return;
    }

    if (!g_pump_reached_reported) {
        g_pump_reached_reported = true;
        LOGI("23.1.3-modules: %s: reached live ModulesController %p", reason,
             controller);
    }

    const GrantStatus status = grant(controller, reason);
    if (status.changed) g_storage_reset_done = false;
    if (status.complete) reset_storage_models(controller, reason);
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
    g_storage_view_rebuilt = false;
    const GrantStatus status = grant(self, "OnInstanceCreated");
    if (status.changed) g_storage_reset_done = false;
    if (status.complete) reset_storage_models(self, "OnInstanceCreated");
}

inline void reload_hook(void* self, void* method) {
    if (g_orig_reload != nullptr) g_orig_reload(self, method);
    g_storage_reset_done = false;
    g_storage_view_rebuilt = false;
    const GrantStatus status = grant(self, "profile reload");
    if (status.changed) g_storage_reset_done = false;
    if (status.complete) reset_storage_models(self, "profile reload");
}

inline void* selected_hook(void* self, void* method) {
    ++g_selected_calls;
    const GrantStatus status = grant(self, "module UI prebuild");
    if (status.changed) g_storage_reset_done = false;
    if (status.complete) reset_storage_models(self, "module UI prebuild");
    return g_orig_selected != nullptr ? g_orig_selected(self, method) : nullptr;
}

inline void storage_view_set_hook(void* self, void* model, void* method) {
    if (!g_storage_view_entry_reported) {
        g_storage_view_entry_reported = true;
        LOGI("23.1.3-modules: ModuleStorageView setter reached");
    }
    void* rebuilt = prepare_storage_model("storage-view setter");
    if (rebuilt != nullptr) model = rebuilt;
    if (g_orig_storage_view_set != nullptr) {
        g_orig_storage_view_set(self, model, method);
    }
}

inline void storage_view_refresh_hook(void* self, void* method) {
    void* rebuilt = prepare_storage_model("storage-view refresh");
    if (rebuilt != nullptr) {
        write_object_field(self, g_field_storage_view_model, rebuilt);
    }
    if (g_orig_storage_view_refresh != nullptr) {
        g_orig_storage_view_refresh(self, method);
    }
}

// ModuleArmoryInfoScreen is the screen the player opens. Granting here happens
// immediately before the screen reads the controller. Awake (0x023B5C08) and
// open (0x023B5DB8) have no direct BL call sites and prebuild (0x023B6554) has
// one, so these are opportunistic routes only - never the guarantee.
inline void screen_awake_hook(void* self, void* method) {
    pump("module screen awake");
    if (g_orig_screen_awake != nullptr) g_orig_screen_awake(self, method);
}

inline void screen_show_hook(void* self, void* argument, void* method) {
    pump("module screen open");
    if (g_orig_screen_show != nullptr) {
        g_orig_screen_show(self, argument, method);
    }
}

inline void screen_prebuild_hook(void* self, void* method) {
    pump("module screen prebuild");
    if (g_orig_screen_prebuild != nullptr) {
        g_orig_screen_prebuild(self, method);
    }
}

inline int32_t current_level_hook(void* self, void* method) {
    int32_t original = 0;
    if (g_orig_current_level != nullptr) {
        original = g_orig_current_level(self, method);
    }
    ++g_level_calls;
    if (g_controller == nullptr && !g_in_grant) {
        g_controller = controller_instance();
    }
    if (g_controller != nullptr && !g_in_grant &&
        (g_level_calls % kLevelRecheckEvery) == 1u) {
        (void)grant(g_controller, "level fallback");
    }
    const int32_t level = original >= kTargetModuleLevel
                              ? original
                              : kTargetModuleLevel;
    if ((g_level_calls % kLevelLogEvery) == 1u) {
        LOGI("23.1.3-modules: displayed level %d -> %d (call %llu)",
             original, level, static_cast<unsigned long long>(g_level_calls));
    }
    return level;
}

inline bool install() {
    if (g_installed) return true;

    bool resolved = true;
    resolved &= bind(g_catalog_modules, kNs, kCatalogClass, kCatalogModules, 0);
    resolved &= bind(g_catalog_sets, kNs, kCatalogClass, kCatalogSets, 0);
    resolved &= bind(g_controller_instance, kNs, kControllerClass,
                     kControllerInstance, 0);
    resolved &= bind(g_selected_method, kNs, kControllerClass,
                     kControllerSelected, 0);
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
    g_field_storage_view_model = il2cpp::find_field(
        kNs, kStorageViewClass, kStorageViewModelField);
    if (g_field_owned_modules == nullptr || g_field_owned_sets == nullptr ||
        g_field_storage_by_name == nullptr || g_field_storage_by_index == nullptr ||
        g_field_storage_view_model == nullptr) {
        LOGE("23.1.3-modules: controller fields unavailable "
             "(modules=%p sets=%p by-name=%p by-index=%p view-model=%p), module disabled",
             g_field_owned_modules, g_field_owned_sets, g_field_storage_by_name,
             g_field_storage_by_index, g_field_storage_view_model);
        return false;
    }

    // ---- critical route -----------------------------------------------------
    // The level source is the only hook the feature cannot work without, and
    // it is installed first so nothing below can affect it. This is exactly the
    // hook the previous revision lost to a corrupted identifier.
    if (!hook::install({kNs, kModuleClass, kCurrentLevel, 0},
                       reinterpret_cast<void*>(&current_level_hook),
                       reinterpret_cast<void**>(&g_orig_current_level), true)) {
        LOGE("23.1.3-modules: level source %s::%s/0 could not be hooked, "
             "module disabled", kModuleClass, kCurrentLevel);
        return false;
    }

    // ---- optional routes ----------------------------------------------------
    // Extra grant/refresh entry points. Each one is best-effort: a metadata or
    // patch miss here degrades refresh latency but must never disable the
    // guaranteed level fix or the main-menu grant pump again.
    struct Route {
        const char* label;
        bool ok;
    };
    const Route routes[] = {
        {kOnInstanceCreated,
         hook::install({kNs, kControllerClass, kOnInstanceCreated, 0},
                       reinterpret_cast<void*>(&on_instance_created_hook),
                       reinterpret_cast<void**>(&g_orig_on_instance_created))},
        {"profile reload",
         hook::install({kNs, kControllerClass, kControllerReload, 0},
                       reinterpret_cast<void*>(&reload_hook),
                       reinterpret_cast<void**>(&g_orig_reload))},
        {"storage getter",
         hook::install({kNs, kControllerClass, kControllerSelected, 0},
                       reinterpret_cast<void*>(&selected_hook),
                       reinterpret_cast<void**>(&g_orig_selected))},
        {"storage-view setter",
         hook::install({kNs, kStorageViewClass, kStorageViewSet, 1},
                       reinterpret_cast<void*>(&storage_view_set_hook),
                       reinterpret_cast<void**>(&g_orig_storage_view_set))},
        {"storage-view refresh",
         hook::install({kNs, kStorageViewClass, kStorageViewRefresh, 0},
                       reinterpret_cast<void*>(&storage_view_refresh_hook),
                       reinterpret_cast<void**>(&g_orig_storage_view_refresh))},
        {"screen awake",
         hook::install({kNs, kInfoScreenClass, kInfoScreenAwake, 0},
                       reinterpret_cast<void*>(&screen_awake_hook),
                       reinterpret_cast<void**>(&g_orig_screen_awake))},
        {"screen open",
         hook::install({kNs, kInfoScreenClass, kInfoScreenShow, 1},
                       reinterpret_cast<void*>(&screen_show_hook),
                       reinterpret_cast<void**>(&g_orig_screen_show))},
        {"screen prebuild",
         hook::install({kNs, kInfoScreenClass, kInfoScreenPrebuild, 0},
                       reinterpret_cast<void*>(&screen_prebuild_hook),
                       reinterpret_cast<void**>(&g_orig_screen_prebuild))},
    };
    constexpr int kRouteCount = static_cast<int>(sizeof(routes) / sizeof(routes[0]));
    int available = 0;
    for (int index = 0; index < kRouteCount; ++index) {
        if (routes[index].ok) {
            ++available;
        } else {
            LOGE("23.1.3-modules: optional route '%s' unavailable; continuing "
                 "(level fix and main-menu grant are unaffected)",
                 routes[index].label);
        }
    }
    LOGI("23.1.3-modules: optional refresh routes installed %d/%d", available,
         kRouteCount);

    g_installed = true;
    LOGI("23.1.3-modules: armed: level X guaranteed, catalogue grant pumped from "
         "main menu + module screen (expect %d modules, %d module sets)",
         kExpectedModules, kExpectedModuleSets);
    return true;
}

inline void pump_from_startup() {
    ++g_startup_frames;
    if (g_modules_complete_reported && g_sets_complete_reported &&
        g_storage_reset_done) {
        return;
    }
    pump("startup pump");
}

inline void pump_from_main_menu() {
    ++g_main_menu_frames;
    if ((g_main_menu_frames % kMainMenuRecheckEvery) != 1u) return;
    pump("main-menu pump");
}

}  // namespace detail

inline bool install_hooks() { return detail::install(); }
inline void pump_from_main_menu() { detail::pump_from_main_menu(); }
inline void pump_from_startup() { detail::pump_from_startup(); }

}  // namespace weapon_modules_2313
