#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) weapon-module grant
//
// Makes the complete built-in catalogue (42 modules, 42 module-set entries)
// visible everywhere the game reads the player's module storage, and reports
// every module at level X (10).
//
// Root cause of revision 2 showing level X but never more modules
// ---------------------------------------------------------------
// Revision 2 fixed the level hook (confirmed on device: the module screen now
// prints X) but kept granting into the wrong container. It appended catalogue
// entries to ModulesController's owned-module List<> at +0x30 and then dropped
// the controller's storage-model caches at +0x40/+0x48 to force a rebuild.
// The module screen never reads any of that.
//
// The real read path, recovered from a full A64 direct-BL call graph of the
// supplied libil2cpp.so (RVA == file offset):
//
//   ModuleStorageView.上专丅丑丘丟丙东与()      0x028DCBB4
//     -> 丟丅丁不丄丟不丑丁.丟丄丅丆丐不下丟且()      0x028DBF68   (BL at +0x220)
//        -> 丟丅丁不丄丟不丑丁.七丌丑世丂丟丞丞丟()   0x028DB524   (BL at +0x154)
//           -> 与丅丟丈丕上东丟丁.丟且丗下丁上一专下()  0x026FDD74  : 专丂丄丈一丂世丑丏
//           -> 专丂丄丈一丂世丑丏.丆丌丌丆且丙七丌丞(int) 0x0171F9D8 : List<string>
//
// The module inventory is therefore materialised on demand from the saved
// profile object 专丂丄丈一丂世丑丏 (TypeDefIndex 6598), which keeps owned entries as
// 丙业丅丑丒丘丕丂丘<List<丅丏丏丛丕丁丟上丞>> at +0x10 and hands them out as a list of
// module names. ModulesController's List<> at +0x30 is not consulted at any
// point in that chain, which is exactly why the count never moved.
//
// The fix
// -------
// Hook the single choke point 丟丅丁不丄丟不丑丁.七丌丑世丂丟丞丞丟() and merge the built-in
// catalogue into the list it returns. Measured properties of that target:
//
//   * 880-byte body (0x028DB524..0x028DB894), 34 direct BL call sites, so it is
//     genuinely reachable and provably not inlined away.
//   * Every consumer of the module inventory funnels through it: the storage
//     grid, ModuleArmoryInfoScreen.上专丅丑丘丟丙东与 (0x023B6680),
//     ModuleStoragePropertiesView, ModuleInsertPanel, ModuleInsertPropertiesView,
//     the ModuleContextClues* helpers, ModulesController's own
//     下且与丏丛业丈业与 / 与丕专丐专丐七丗丛 / 丛七丂丙丛世丁不业 / 专丟不且丁丅丏上丏, and the model's
//     own 丐丐丆丈丒丒丙丆东 / 与丅与业丐上且不丝 / 丟万世与丌且丒且丒 / 丟丄丅丆丐不下丟且 / 业丝一一丂万丒不丝.
//   * Module sets need no separate grant: ModulesController.丐上丙业与且丛丂丆 derives
//     the set from a module list, so it follows the merged list automatically.
//
// This is a read-side graft. Nothing is written into the saved profile, so the
// player's own progress object is never rewritten and a bad merge cannot
// corrupt a save. Entries already owned keep their identity: catalogue objects
// are the singletons built once by the catalogue .cctor, so List<T>.Contains
// matches by reference and the merge is idempotent.
//
// Verified facts (measured, not assumed)
// --------------------------------------
// * module::七且丐东丒丆丑丈万() (0x024B0BB0, 132 bytes, 24 call sites) is the
//   current-level source. Confirmed live: "hook: installed
//   丐三七世丝丗与丛上.七且丐东丒丆丑丈万/0" and level X in the UI. Left untouched.
// * 0x024B13C8 / 0x024B140C / 0x024B14EC are the next-level requirement, the
//   cumulative parts threshold and the in-level progress. Never clamped.
// * The catalogue .cctor (0x02EF431C, 284308 bytes) makes exactly 42 module and
//   42 module-set objects, so nothing is fabricated.
// * Dead ends kept out on purpose: the controller's list getters (0x0281473C,
//   0x02814784) and both catalogue getters (0x03048A5C, 0x03048AB4) have zero
//   direct BL call sites (inlined; the catalogue getters are called through
//   their method pointers instead), AddAllModulesDEV (0x028178E4) and
//   AddAllMaxModulesDEV (0x028178E8) are 4-byte bare RET stubs, and
//   ModulesController.下且与丏丛业丈业与 has a single caller behind a UI button.
// * 丑丅丟丟丞与东丙丑.不下与丗丄下且上丛 (0x028196BC) is the game's own "add module to a
//   storage model" helper and the only caller of the model's add method. It is
//   deliberately not used: it fires a profile-mutating notification through
//   东丝丂丄业丕且丙丑, which is exactly the kind of save write this port avoids.
//
// All mutations are process-local.
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
constexpr int32_t kMaxStorageEntries = 4096;
constexpr uint64_t kLevelLogEvery = 240;
constexpr uint64_t kListLogEvery = 120;

constexpr const char* kNs = "PGCompany";
constexpr const char* kCatalogClass = "丐丞丒专且丁丈丌业";
constexpr const char* kCatalogModules = "丞七丌业丛丂丙上丝";
constexpr const char* kCatalogSets = "丂丟世丅丛丙业丛专";
constexpr const char* kModuleClass = "丐三七世丝丗与丛上";
constexpr const char* kStorageViewClass = "ModuleStorageView";
constexpr const char* kInfoScreenClass = "ModuleArmoryInfoScreen";
constexpr const char* kRefresh = "上专丅丑丘丟丙东与";

// module::七且丐东丒丆丑丈万() @ RVA 0x024B0BB0 - the current-level source.
constexpr const char* kCurrentLevel = "七且丐东丒丆丑丈万";

// 丟丅丁不丄丟不丑丁 @ TypeDefIndex 12005 - the storage model the UI renders from.
constexpr const char* kStorageModelClass = "丟丅丁不丄丟不丑丁";

// 丟丅丁不丄丟不丑丁::七丌丑世丂丟丞丞丟() @ RVA 0x028DB524 - the inventory choke point.
constexpr const char* kStorageModelList = "七丌丑世丂丟丞丞丟";

// Regression guard. Revision 1 shipped U+5341 in place of U+4E10 and U+4E12 in
// kCurrentLevel, an identifier that does not exist in this build's metadata, and
// the fail-closed wrapper switched the whole feature off on device. U+5341 does
// not occur once in the dump, which proves that spelling was never valid. Pin
// the exact UTF-8 bytes of every obfuscated name this file cannot work without,
// so a mangled identifier breaks the build instead of the device.
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
                "\xE4\xB8\x90"   // U+4E10 丐  (revision 1 corrupted this to U+5341)
                "\xE4\xB8\x9C"   // U+4E1C 东
                "\xE4\xB8\x92"   // U+4E12 丒  (revision 1 corrupted this to U+5341)
                "\xE4\xB8\x86"   // U+4E06 丆
                "\xE4\xB8\x91"   // U+4E11 丑
                "\xE4\xB8\x88"   // U+4E08 丈
                "\xE4\xB8\x87"), // U+4E07 万
    "kCurrentLevel must stay byte-identical to module::0x024B0BB0 as spelled in "
    "the 23.1.3 metadata; a corrupted identifier silently disables the module");

static_assert(
    equal_bytes(kStorageModelClass,
                "\xE4\xB8\x9F"   // U+4E1F 丟
                "\xE4\xB8\x85"   // U+4E05 丅
                "\xE4\xB8\x81"   // U+4E01 丁
                "\xE4\xB8\x8D"   // U+4E0D 不
                "\xE4\xB8\x84"   // U+4E04 丄
                "\xE4\xB8\x9F"   // U+4E1F 丟
                "\xE4\xB8\x8D"   // U+4E0D 不
                "\xE4\xB8\x91"   // U+4E11 丑
                "\xE4\xB8\x81"), // U+4E01 丁
    "kStorageModelClass must stay byte-identical to TypeDefIndex 12005");

static_assert(
    equal_bytes(kStorageModelList,
                "\xE4\xB8\x83"   // U+4E03 七
                "\xE4\xB8\x8C"   // U+4E0C 丌
                "\xE4\xB8\x91"   // U+4E11 丑
                "\xE4\xB8\x96"   // U+4E16 世
                "\xE4\xB8\x82"   // U+4E02 丂
                "\xE4\xB8\x9F"   // U+4E1F 丟
                "\xE4\xB8\x9E"   // U+4E1E 丞
                "\xE4\xB8\x9E"   // U+4E1E 丞
                "\xE4\xB8\x9F"), // U+4E1F 丟
    "kStorageModelList must stay byte-identical to the inventory getter at "
    "RVA 0x028DB524; this is the only route that changes the module count");

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
    explicit operator bool() const noexcept {
        return info != nullptr && ptr != nullptr;
    }
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
        return count_info != nullptr && count_ptr != nullptr &&
               item_info != nullptr && item_ptr != nullptr;
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
inline InstanceObjFn g_orig_storage_list = nullptr;
inline InstanceIntFn g_orig_current_level = nullptr;
inline InstanceVoidFn g_orig_storage_view_refresh = nullptr;
inline InstanceVoidFn g_orig_screen_refresh = nullptr;
inline uint64_t g_list_calls = 0;
inline uint64_t g_level_calls = 0;
inline uint64_t g_view_refreshes = 0;
inline uint64_t g_screen_refreshes = 0;
inline uint64_t g_merge_failures = 0;
inline bool g_installed = false;
inline bool g_catalog_reported = false;
inline bool g_first_merge_reported = false;
inline bool g_complete_reported = false;
inline bool g_failure_reported = false;
inline thread_local bool g_in_catalog = false;
inline thread_local bool g_in_merge = false;

// Re-entrancy latch. 丟丄丅丆丐不下丟且 and 业丝一一丂万丒不丝 both call the hooked getter,
// and List<T>.Contains/Add can run managed code, so the merge must never be
// able to nest into itself.
class Latch {
public:
    explicit Latch(bool& flag) noexcept : flag_(flag) { flag_ = true; }
    ~Latch() noexcept { flag_ = false; }
    Latch(const Latch&) = delete;
    Latch& operator=(const Latch&) = delete;

private:
    bool& flag_;
};

inline void* fetch_catalog(const Managed& source) {
    if (!source || g_in_catalog) return nullptr;
    Latch latch(g_in_catalog);
    return reinterpret_cast<StaticObjFn>(source.ptr)(source.info);
}

struct MergeResult {
    int32_t source = -1;
    int32_t before = -1;
    int32_t added = 0;
    int32_t after = -1;
    bool ok = false;
    const char* failure = nullptr;
};

// Adds every catalogue module the list does not already hold. The list object
// itself is returned to the caller untouched in identity, so the model keeps
// whatever caching semantics it had.
inline MergeResult merge_catalog_into(void* list) {
    MergeResult result;

    void* full = fetch_catalog(g_catalog_modules);
    if (full == nullptr) {
        result.failure = "catalogue getter returned null";
        return result;
    }
    ListApi full_api;
    if (!resolve_list_api(full, full_api) || !full_api.readable()) {
        result.failure = "catalogue List<T> API unavailable";
        return result;
    }
    result.source = reinterpret_cast<ListCountFn>(full_api.count_ptr)(
        full, full_api.count_info);
    if (result.source <= 0 || result.source > kMaxCatalogEntries) {
        result.failure = "catalogue count outside safety bounds";
        return result;
    }

    // The model handed us the catalogue list itself: already complete.
    if (list == full) {
        result.before = result.source;
        result.after = result.source;
        result.ok = true;
        return result;
    }

    ListApi own_api;
    if (!resolve_list_api(list, own_api) || !own_api.writable()) {
        result.failure = "storage List<T> API not writable";
        return result;
    }
    result.before = reinterpret_cast<ListCountFn>(own_api.count_ptr)(
        list, own_api.count_info);
    if (result.before < 0) {
        result.failure = "storage list count unavailable";
        return result;
    }
    if (result.before > kMaxStorageEntries) {
        result.failure = "storage list longer than safety bound";
        return result;
    }

    for (int32_t index = 0; index < result.source; ++index) {
        void* item = reinterpret_cast<ListItemFn>(full_api.item_ptr)(
            full, index, full_api.item_info);
        if (item == nullptr) continue;
        const bool present = reinterpret_cast<ListContainsFn>(
            own_api.contains_ptr)(list, item, own_api.contains_info);
        if (present) continue;
        reinterpret_cast<ListAddFn>(own_api.add_ptr)(list, item,
                                                    own_api.add_info);
        ++result.added;
    }

    result.after = reinterpret_cast<ListCountFn>(own_api.count_ptr)(
        list, own_api.count_info);
    result.ok = result.after >= result.source;
    if (!result.ok) result.failure = "merged list shorter than catalogue";
    return result;
}

inline void report_merge(const MergeResult& result) {
    if (!result.ok) {
        ++g_merge_failures;
        if (!g_failure_reported) {
            g_failure_reported = true;
            LOGE("23.1.3-modules: inventory merge failed: %s "
                 "(source=%d before=%d after=%d)",
                 result.failure != nullptr ? result.failure : "unknown",
                 result.source, result.before, result.after);
        }
        return;
    }
    if (!g_first_merge_reported) {
        g_first_merge_reported = true;
        LOGI("23.1.3-modules: inventory merge live: source=%d owned=%d +%d -> %d",
             result.source, result.before, result.added, result.after);
    }
    if (!g_complete_reported && result.after >= kExpectedModules) {
        g_complete_reported = true;
        LOGI("23.1.3-modules: module inventory complete (%d/%d)", result.after,
             kExpectedModules);
    }
    if ((g_list_calls % kListLogEvery) == 1u) {
        LOGI("23.1.3-modules: inventory read #%llu -> %d entries (+%d)",
             static_cast<unsigned long long>(g_list_calls), result.after,
             result.added);
    }
}

// ---- critical route: the inventory the UI actually reads --------------------
inline void* storage_list_hook(void* self, void* method) {
    void* list = g_orig_storage_list != nullptr
                     ? g_orig_storage_list(self, method)
                     : nullptr;
    if (list == nullptr || g_in_merge) return list;
    ++g_list_calls;
    Latch latch(g_in_merge);
    report_merge(merge_catalog_into(list));
    return list;
}

// ---- critical route: displayed module level ---------------------------------
inline int32_t current_level_hook(void* self, void* method) {
    int32_t original = 0;
    if (g_orig_current_level != nullptr) {
        original = g_orig_current_level(self, method);
    }
    ++g_level_calls;
    const int32_t level =
        original >= kTargetModuleLevel ? original : kTargetModuleLevel;
    if ((g_level_calls % kLevelLogEvery) == 1u) {
        LOGI("23.1.3-modules: displayed level %d -> %d (call %llu)", original,
             level, static_cast<unsigned long long>(g_level_calls));
    }
    return level;
}

// ---- optional routes: diagnostics only -------------------------------------
// These exist so a device log can prove the screen was reached and how many
// entries the merged inventory produced. They must never gate anything.
inline void storage_view_refresh_hook(void* self, void* method) {
    ++g_view_refreshes;
    if (g_view_refreshes == 1u) {
        LOGI("23.1.3-modules: module storage grid rebuilding");
    }
    if (g_orig_storage_view_refresh != nullptr) {
        g_orig_storage_view_refresh(self, method);
    }
}

inline void screen_refresh_hook(void* self, void* method) {
    ++g_screen_refreshes;
    if (g_screen_refreshes == 1u) {
        LOGI("23.1.3-modules: module armory screen refreshing");
    }
    if (g_orig_screen_refresh != nullptr) {
        g_orig_screen_refresh(self, method);
    }
}

// One-shot sanity check that the shipped catalogue really is 42/42.
inline void verify_catalog() {
    if (g_catalog_reported) return;
    void* modules = fetch_catalog(g_catalog_modules);
    void* sets = fetch_catalog(g_catalog_sets);
    if (modules == nullptr && sets == nullptr) return;
    g_catalog_reported = true;
    const int32_t module_count = list_count(modules);
    const int32_t set_count = list_count(sets);
    if (module_count == kExpectedModules && set_count == kExpectedModuleSets) {
        LOGI("23.1.3-modules: verified built-in catalogue: %d modules, "
             "%d module sets",
             module_count, set_count);
    } else {
        LOGE("23.1.3-modules: unexpected catalogue size: %d modules "
             "(expected %d), %d module sets (expected %d)",
             module_count, kExpectedModules, set_count, kExpectedModuleSets);
    }
}

inline bool install() {
    if (g_installed) return true;

    // The catalogue getters are inlined at every managed call site, so they are
    // called through their method pointers rather than hooked.
    if (!bind(g_catalog_modules, kNs, kCatalogClass, kCatalogModules, 0)) {
        LOGE("23.1.3-modules: built-in catalogue unavailable, module disabled");
        return false;
    }
    (void)bind(g_catalog_sets, kNs, kCatalogClass, kCatalogSets, 0);

    // ---- critical routes ----------------------------------------------------
    // Without these two the feature cannot work, so both are required and any
    // miss disables the module loudly instead of pretending to be armed.
    if (!hook::install({kNs, kStorageModelClass, kStorageModelList, 0},
                       reinterpret_cast<void*>(&storage_list_hook),
                       reinterpret_cast<void**>(&g_orig_storage_list), true)) {
        LOGE("23.1.3-modules: inventory source %s::%s/0 could not be hooked, "
             "module disabled", kStorageModelClass, kStorageModelList);
        return false;
    }
    if (!hook::install({kNs, kModuleClass, kCurrentLevel, 0},
                       reinterpret_cast<void*>(&current_level_hook),
                       reinterpret_cast<void**>(&g_orig_current_level), true)) {
        LOGE("23.1.3-modules: level source %s::%s/0 could not be hooked, "
             "module disabled", kModuleClass, kCurrentLevel);
        return false;
    }

    // ---- optional routes ----------------------------------------------------
    struct Route {
        const char* label;
        bool ok;
    };
    const Route routes[] = {
        {"storage grid refresh",
         hook::install({kNs, kStorageViewClass, kRefresh, 0},
                       reinterpret_cast<void*>(&storage_view_refresh_hook),
                       reinterpret_cast<void**>(&g_orig_storage_view_refresh))},
        {"armory screen refresh",
         hook::install({kNs, kInfoScreenClass, kRefresh, 0},
                       reinterpret_cast<void*>(&screen_refresh_hook),
                       reinterpret_cast<void**>(&g_orig_screen_refresh))},
    };
    constexpr int kRouteCount = static_cast<int>(sizeof(routes) /
                                                sizeof(routes[0]));
    int installed_routes = 0;
    for (int index = 0; index < kRouteCount; ++index) {
        if (routes[index].ok) {
            ++installed_routes;
        } else {
            LOGE("23.1.3-modules: optional route '%s' unavailable, continuing",
                 routes[index].label);
        }
    }
    LOGI("23.1.3-modules: optional diagnostic routes installed %d/%d",
         installed_routes, kRouteCount);

    g_installed = true;
    LOGI("23.1.3-modules: armed: level X guaranteed, full catalogue merged into "
         "the storage inventory the UI reads (expect %d modules, "
         "%d module sets)",
         kExpectedModules, kExpectedModuleSets);
    return true;
}

// The pump is no longer load-bearing: the inventory is grafted on read, so it
// only performs the one-shot catalogue sanity check. It stays cheap because
// progression_2313.h calls it from MainMenuController.Update every frame.
inline void pump(const char* label) {
    if (!g_installed || g_catalog_reported) return;
    verify_catalog();
    if (g_catalog_reported) {
        LOGI("23.1.3-modules: catalogue verified from %s", label);
    }
}

} // namespace detail

inline bool install_hooks() { return detail::install(); }

inline void pump_from_main_menu() { detail::pump("main menu"); }

inline void pump_from_startup() { detail::pump("startup"); }

} // namespace weapon_modules_2313
