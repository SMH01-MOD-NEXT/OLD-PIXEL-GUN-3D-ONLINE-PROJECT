#pragma once

// Pixel Gun 3D 23.1.3 (arm64-v8a) - weapon modules.
//
// Two guarantees:
//   1. every weapon module reads as level 10 (X) in the UI;
//   2. the whole built-in module catalogue (42 modules + 42 module sets) ends
//      up in the player's module inventory.
//
// Revision history, because every wrong turn here was expensive:
//
//   r1  Hooked the module level getter. Two identifiers were mangled by a
//       find/replace (U+4E10 and U+4E12 became U+5341), the required hook
//       failed to resolve and the fail-closed wrapper disabled the feature.
//       Fixed by pinning the exact UTF-8 bytes with static_assert.
//
//   r2  Wrote the catalogue into ModulesController +0x40 / +0x48
//       (Dictionary<string, model> and the int-keyed model map) and reset
//       them. Those fields hold per-item storage models, not inventory, so
//       the change was a no-op on screen.
//
//   r3  Merged the catalogue into 丟丅丁不丄丟不丑丁::七丌丑世丂丟丞丞丟() @ 0x028DB524.
//       The merge worked, but the target was wrong in a very visible way.
//       丟丅丁不丄丟不丑丁 is the module storage model OF A SINGLE ITEM:
//
//           int                              <id>       @ 0x10
//           ModuleData.ModuleRarity          <rarity>   @ 0x14
//           ModuleData.ModuleCategory        <category> @ 0x18
//           Dictionary<string, module>       slots      @ 0x20
//           丅丏丏丛丕丁丟上丞 itemRecord()                (0x028DB2F8)
//
//       and 七丌丑世丂丟丞丞丟() is "the modules installed on THIS item": it is the
//       only caller in the whole image of 专丂丄丈一丂世丑丏::丆丌丌丆且丙七丌丞(int)
//       @ 0x0171F9D8, the per-item profile record lookup. Returning the full
//       catalogue from it told the game that every item wears all 42 modules:
//       the armour screen stacked 42 sets of effects on top of each other and
//       every module in the storage grid greyed out with "Same mod type
//       already in use", while the inventory count never moved.
//
//   r4  Inventory-side grant. The catalogue is appended to the two lists the
//       storage readers actually enumerate:
//
//           ModulesController.丅与世丕业丘不丂丈  @ 0x30  List<module>
//           ModulesController.下丘丌一丞丛丂三丗  @ 0x38  List<moduleSet>
//
//       Their getters (0x0281473C / 0x02814784) have zero call sites because
//       the compiler inlined every read, so the fields themselves are the
//       only stable interception point - hence a field-level grant that is
//       re-asserted from the main menu and from each storage refresh instead
//       of a single hook. Per-item models are never written again.
//
//   r5  (this file) r4 killed the game on the splash screen. install() ended
//       with a verify step that read the catalogue's static list, and that
//       first read forces the catalogue class's static constructor, which
//       pulls module definitions through PGCompany.AssetBundles_v3 -> config
//       lookup. At install time (~950 ms after process start) that layer is
//       not up yet: it logged "Settings are null." and then walked into a JNI
//       path on the bootstrap thread, which is attached to the IL2CPP runtime
//       but never to the JVM. Null JNIEnv -> SIGSEGV at 0x68 inside libart.so.
//
//       Rule enforced from here on: install() only resolves metadata and
//       patches code. Every managed call - reading the catalogue, counting a
//       list, appending to it - runs on a game thread from one of the hooks or
//       from the main-menu pump, and is refused outright if it is ever reached
//       from the bootstrap thread. r3 already proved that reading the
//       catalogue from a UI-thread hook is safe: its merge succeeded on device
//       (very visibly, on the armour screen).

#include <cstddef>
#include <cstdint>

#include <pthread.h>

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
constexpr int32_t kMaxInventoryEntries = 4096;
constexpr uint64_t kLevelLogEvery = 240;
constexpr uint64_t kGrantLogEvery = 60;
constexpr uint64_t kPumpEvery = 30;

constexpr const char* kNs = "PGCompany";

// 丐丞丒专且丁丈丌业 - the static built-in catalogue (.cctor @ 0x02EF431C builds
// 42 modules and 42 module sets).
constexpr const char* kCatalogClass = "丐丞丒专且丁丈丌业";
constexpr const char* kCatalogModules = "丞七丌业丛丂丙上丝";  // 0x03048A5C
constexpr const char* kCatalogSets = "丂丟世丅丛丙业丛专";     // 0x03048AB4

// 丐三七世丝丗与丛上 - a single weapon module.
constexpr const char* kModuleClass = "丐三七世丝丗与丛上";

// 丐三七世丝丗与丛上::七且丐东丒丆丑丈万() @ 0x024B0BB0 - the current-level source,
// 24 call sites, every level label in the module UI goes through it.
constexpr const char* kCurrentLevel = "七且丐东丒丆丑丈万";

// ModulesController @ TypeDefIndex 11986 - the singleton that owns the
// player's module inventory.
constexpr const char* kControllerClass = "ModulesController";
constexpr const char* kControllerReady = "OnInstanceCreated";  // 0x02814810
constexpr const char* kInventoryModules = "丅与世丕业丘不丂丈";      // field @ 0x30
constexpr const char* kInventorySets = "下丘丌一丞丛丂三丗";         // field @ 0x38

// Storage readers. Granting before their refresh runs means the grid is built
// from the enlarged list, and it repairs the list if the game rebuilt it.
constexpr const char* kStorageViewClass = "ModuleStorageView";
constexpr const char* kInfoScreenClass = "ModuleArmoryInfoScreen";
constexpr const char* kInsertPanelClass = "ModuleInsertPanel";
constexpr const char* kRefresh = "上专丅丑丘丟丙东与";        // 0x028DCBB4 / 0x023B6680
constexpr const char* kInsertRefresh = "上丘与丛丝业丆万丁";  // 0x023BA03C

// ---------------------------------------------------------------------------
// Regression guard.
//
// Revision 1 shipped U+5341 in place of U+4E10 and U+4E12 inside kCurrentLevel.
// U+5341 does not occur once in the 23.1.3 dump, which proves that spelling was
// never valid, yet the corruption preserved the byte length and only surfaced on
// device. Pin the exact bytes of every obfuscated name this file cannot work
// without, and reject the mojibake byte sequence outright, so a mangled
// identifier breaks the build instead of the device.
// ---------------------------------------------------------------------------

constexpr bool equal_bytes(const char* a, const char* b) {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

constexpr size_t byte_len(const char* s) {
    size_t n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

constexpr bool contains_bytes(const char* haystack, const char* needle) {
    const size_t h = byte_len(haystack);
    const size_t n = byte_len(needle);
    if (n == 0 || n > h) {
        return false;
    }
    for (size_t i = 0; i + n <= h; ++i) {
        size_t j = 0;
        while (j < n && haystack[i + j] == needle[j]) {
            ++j;
        }
        if (j == n) {
            return true;
        }
    }
    return false;
}

// U+5341 十, the character revision 1 substituted for 丐 and 丒.
constexpr const char* kMojibake = "\xE5\x8D\x81";

static_assert(!contains_bytes(kCurrentLevel, kMojibake) &&
                  !contains_bytes(kModuleClass, kMojibake) &&
                  !contains_bytes(kCatalogClass, kMojibake) &&
                  !contains_bytes(kCatalogModules, kMojibake) &&
                  !contains_bytes(kCatalogSets, kMojibake) &&
                  !contains_bytes(kInventoryModules, kMojibake) &&
                  !contains_bytes(kInventorySets, kMojibake) &&
                  !contains_bytes(kRefresh, kMojibake) &&
                  !contains_bytes(kInsertRefresh, kMojibake),
              "an obfuscated identifier contains U+5341, which never occurs in "
              "the 23.1.3 metadata: this is the revision 1 find/replace bug");

static_assert(
    equal_bytes(kCurrentLevel,
                "\xE4\xB8\x83"   // U+4E03 七
                "\xE4\xB8\x94"   // U+4E14 且
                "\xE4\xB8\x90"   // U+4E10 丐  (revision 1 corrupted this)
                "\xE4\xB8\x9C"   // U+4E1C 东
                "\xE4\xB8\x92"   // U+4E12 丒  (revision 1 corrupted this)
                "\xE4\xB8\x86"   // U+4E06 丆
                "\xE4\xB8\x91"   // U+4E11 丑
                "\xE4\xB8\x88"   // U+4E08 丈
                "\xE4\xB8\x87"), // U+4E07 万
    "kCurrentLevel must stay byte-identical to 0x024B0BB0 as spelled in the "
    "23.1.3 metadata; a corrupted identifier silently disables the module");

static_assert(
    equal_bytes(kInventoryModules,
                "\xE4\xB8\x85"   // U+4E05 丅
                "\xE4\xB8\x8E"   // U+4E0E 与
                "\xE4\xB8\x96"   // U+4E16 世
                "\xE4\xB8\x95"   // U+4E15 丕
                "\xE4\xB8\x9A"   // U+4E1A 业
                "\xE4\xB8\x98"   // U+4E18 丘
                "\xE4\xB8\x8D"   // U+4E0D 不
                "\xE4\xB8\x82"   // U+4E02 丂
                "\xE4\xB8\x88"), // U+4E08 丈
    "kInventoryModules must stay byte-identical to ModulesController +0x30, "
    "the List<module> every storage reader enumerates");

static_assert(
    equal_bytes(kInventorySets,
                "\xE4\xB8\x8B"   // U+4E0B 下
                "\xE4\xB8\x98"   // U+4E18 丘
                "\xE4\xB8\x8C"   // U+4E0C 丌
                "\xE4\xB8\x80"   // U+4E00 一
                "\xE4\xB8\x9E"   // U+4E1E 丞
                "\xE4\xB8\x9B"   // U+4E1B 丛
                "\xE4\xB8\x82"   // U+4E02 丂
                "\xE4\xB8\x89"   // U+4E09 三
                "\xE4\xB8\x97"), // U+4E17 丗
    "kInventorySets must stay byte-identical to ModulesController +0x38");

// ---------------------------------------------------------------------------
// Managed plumbing
// ---------------------------------------------------------------------------

using StaticObjFn = void* (*)(void* method);
using InstanceVoidFn = void (*)(void* self, void* method);
using InstanceIntFn = int32_t (*)(void* self, void* method);
using ListCountFn = int32_t (*)(void* list, void* method);
using ListItemFn = void* (*)(void* list, int32_t index, void* method);
using ListContainsFn = bool (*)(void* list, void* item, void* method);
using ListAddFn = void (*)(void* list, void* item, void* method);

struct Managed {
    void* info = nullptr;
    void* ptr = nullptr;

    bool ok() const { return info != nullptr && ptr != nullptr; }
};

inline bool bind(Managed& out, const char* namespaze, const char* klass,
                 const char* method, int args_count) {
    out.info = il2cpp::find_method_info(namespaze, klass, method, args_count);
    out.ptr = (out.info != nullptr) ? il2cpp::method_pointer(out.info) : nullptr;
    return out.ok();
}

struct ListApi {
    Managed count{};
    Managed item{};
    Managed contains{};
    Managed add{};
    bool ok = false;
};

inline bool resolve_list_api(void* list, ListApi& api) {
    api = ListApi{};
    if (list == nullptr) {
        return false;
    }
    void* klass = il2cpp::object_get_class(list);
    if (klass == nullptr) {
        return false;
    }
    api.count.info = il2cpp::class_get_method_from_name(klass, "get_Count", 0);
    api.item.info = il2cpp::class_get_method_from_name(klass, "get_Item", 1);
    api.contains.info = il2cpp::class_get_method_from_name(klass, "Contains", 1);
    api.add.info = il2cpp::class_get_method_from_name(klass, "Add", 1);

    Managed* const all[] = {&api.count, &api.item, &api.contains, &api.add};
    for (Managed* entry : all) {
        if (entry->info == nullptr) {
            return false;
        }
        entry->ptr = il2cpp::method_pointer(entry->info);
        if (entry->ptr == nullptr) {
            return false;
        }
    }
    api.ok = true;
    return true;
}

inline int32_t list_count(const ListApi& api, void* list) {
    if (!api.ok || list == nullptr) {
        return -1;
    }
    return reinterpret_cast<ListCountFn>(api.count.ptr)(list, api.count.info);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

inline Managed g_catalog_modules{};
inline Managed g_catalog_sets{};

inline void* g_controller = nullptr;
inline void* g_field_modules = nullptr;
inline void* g_field_sets = nullptr;

inline void* g_orig_level = nullptr;
inline void* g_orig_controller_ready = nullptr;
inline void* g_orig_storage_refresh = nullptr;
inline void* g_orig_screen_refresh = nullptr;
inline void* g_orig_insert_refresh = nullptr;

inline bool g_installed = false;
inline bool g_reported_complete = false;
inline uint64_t g_level_calls = 0;
inline uint64_t g_grant_calls = 0;
inline uint64_t g_pump_calls = 0;
inline uint64_t g_failure_reports = 0;
inline uint64_t g_refusals = 0;

inline thread_local bool g_in_grant = false;

inline pthread_t g_install_thread{};
inline bool g_install_thread_valid = false;
inline bool g_catalog_verified = false;

class Latch {
public:
    explicit Latch(bool& flag) : flag_(flag), entered_(!flag) {
        if (entered_) {
            flag_ = true;
        }
    }

    ~Latch() {
        if (entered_) {
            flag_ = false;
        }
    }

    Latch(const Latch&) = delete;
    Latch& operator=(const Latch&) = delete;

    bool entered() const { return entered_; }

private:
    bool& flag_;
    bool entered_;
};

// ---------------------------------------------------------------------------
// Thread safety and lazy catalogue verification
// ---------------------------------------------------------------------------

// The bootstrap thread that runs install() is attached to the IL2CPP runtime
// but not to the JVM, and it runs long before the config/AssetBundles layer is
// alive. A managed call from it can reach JNI with a null JNIEnv and take the
// process down inside libart, which is exactly how r4 died. Only the game's own
// threads may call into managed code.
inline bool managed_calls_allowed(const char* label) {
    if (!g_install_thread_valid ||
        pthread_equal(pthread_self(), g_install_thread) == 0) {
        return true;
    }
    if (g_refusals < 3) {
        ++g_refusals;
        LOGE("23.1.3-modules: managed call refused (%s): the bootstrap thread "
             "is not a safe caller", label);
    }
    return false;
}

// Diagnostics only, and deliberately lazy: the first read of the catalogue is
// what triggers its static constructor, so it has to happen on a game thread.
inline void verify_catalog_once() {
    if (g_catalog_verified || !g_catalog_modules.ok()) {
        return;
    }
    g_catalog_verified = true;

    void* modules = reinterpret_cast<StaticObjFn>(g_catalog_modules.ptr)(
        g_catalog_modules.info);
    ListApi api{};
    if (!resolve_list_api(modules, api)) {
        LOGE("23.1.3-modules: catalogue unreadable");
        return;
    }
    const int32_t count = list_count(api, modules);

    int32_t sets = -1;
    if (g_catalog_sets.ok()) {
        void* set_list = reinterpret_cast<StaticObjFn>(g_catalog_sets.ptr)(
            g_catalog_sets.info);
        ListApi set_api{};
        if (resolve_list_api(set_list, set_api)) {
            sets = list_count(set_api, set_list);
        }
    }

    if (count == kExpectedModules && sets == kExpectedModuleSets) {
        LOGI("23.1.3-modules: catalogue verified (%d modules, %d sets)", count,
             sets);
    } else {
        LOGE("23.1.3-modules: catalogue mismatch (%d modules, %d sets; "
             "expected %d and %d)",
             count, sets, kExpectedModules, kExpectedModuleSets);
    }
}

// ---------------------------------------------------------------------------
// Inventory grant
// ---------------------------------------------------------------------------

struct GrantResult {
    const char* failure = nullptr;
    int32_t source = 0;
    int32_t before = 0;
    int32_t added = 0;
    int32_t after = 0;
};

inline void* read_object_field(void* instance, void* field) {
    if (instance == nullptr || field == nullptr ||
        il2cpp::field_get_value == nullptr) {
        return nullptr;
    }
    void* value = nullptr;
    il2cpp::field_get_value(instance, field, &value);
    return value;
}

// Append every catalogue entry the target list does not already hold. The list
// object itself is never replaced: the game keeps its own reference to it, and
// swapping it would leave every inlined reader pointing at the old instance.
inline GrantResult grant_into(void* target, const Managed& source) {
    GrantResult result{};

    if (!source.ok()) {
        result.failure = "catalogue route missing";
        return result;
    }
    if (target == nullptr) {
        result.failure = "inventory list is null";
        return result;
    }

    void* catalog = reinterpret_cast<StaticObjFn>(source.ptr)(source.info);
    if (catalog == nullptr) {
        result.failure = "catalogue getter returned null";
        return result;
    }

    ListApi src{};
    ListApi dst{};
    if (!resolve_list_api(catalog, src)) {
        result.failure = "catalogue list api incomplete";
        return result;
    }
    if (!resolve_list_api(target, dst)) {
        result.failure = "inventory list api incomplete";
        return result;
    }

    result.source = list_count(src, catalog);
    result.before = list_count(dst, target);
    result.after = result.before;

    if (result.source <= 0) {
        result.failure = "catalogue is empty";
        return result;
    }
    if (result.source > kMaxCatalogEntries) {
        result.failure = "catalogue is implausibly large";
        return result;
    }
    if (result.before < 0) {
        result.failure = "inventory count unavailable";
        return result;
    }

    for (int32_t i = 0; i < result.source; ++i) {
        void* entry =
            reinterpret_cast<ListItemFn>(src.item.ptr)(catalog, i, src.item.info);
        if (entry == nullptr) {
            continue;
        }
        if (reinterpret_cast<ListContainsFn>(dst.contains.ptr)(
                target, entry, dst.contains.info)) {
            continue;
        }
        if (list_count(dst, target) >= kMaxInventoryEntries) {
            result.failure = "inventory cap reached";
            break;
        }
        reinterpret_cast<ListAddFn>(dst.add.ptr)(target, entry, dst.add.info);
        ++result.added;
    }

    result.after = list_count(dst, target);
    return result;
}

inline void grant_inventory(const char* label) {
    if (!g_installed || !managed_calls_allowed(label)) {
        return;
    }

    Latch latch(g_in_grant);
    if (!latch.entered()) {
        return;
    }

    verify_catalog_once();

    ++g_grant_calls;
    const bool verbose = g_grant_calls <= 3 || (g_grant_calls % kGrantLogEvery) == 0;

    if (g_controller == nullptr) {
        if (verbose) {
            LOGE("23.1.3-modules: inventory grant skipped (%s): modules "
                 "controller instance not seen yet", label);
        }
        return;
    }
    if (g_field_modules == nullptr) {
        if (verbose) {
            LOGE("23.1.3-modules: inventory grant skipped (%s): inventory field "
                 "missing", label);
        }
        return;
    }

    const GrantResult modules =
        grant_into(read_object_field(g_controller, g_field_modules),
                   g_catalog_modules);
    const GrantResult sets =
        (g_field_sets != nullptr)
            ? grant_into(read_object_field(g_controller, g_field_sets),
                         g_catalog_sets)
            : GrantResult{"module set field missing", 0, 0, 0, 0};

    if (modules.failure != nullptr) {
        ++g_failure_reports;
        if (g_failure_reports <= 5 || verbose) {
            LOGE("23.1.3-modules: inventory grant failed (%s): %s "
                 "(source=%d before=%d after=%d)",
                 label, modules.failure, modules.source, modules.before,
                 modules.after);
        }
        return;
    }

    if (modules.added > 0 || sets.added > 0 || verbose) {
        LOGI("23.1.3-modules: inventory grant (%s): modules %d +%d -> %d, "
             "sets %d +%d -> %d",
             label, modules.before, modules.added, modules.after, sets.before,
             sets.added, sets.after);
    }

    if (sets.failure != nullptr && (sets.added > 0 || verbose)) {
        LOGE("23.1.3-modules: module set grant failed (%s): %s", label,
             sets.failure);
    }

    if (!g_reported_complete && modules.after >= kExpectedModules) {
        g_reported_complete = true;
        LOGI("23.1.3-modules: module inventory complete (%d/%d modules, "
             "%d/%d sets)",
             modules.after, kExpectedModules, sets.after, kExpectedModuleSets);
    }
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

inline int32_t current_level_hook(void* self, void* method) {
    (void)self;
    (void)method;

    ++g_level_calls;
    if (g_level_calls == 1 || (g_level_calls % kLevelLogEvery) == 0) {
        LOGI("23.1.3-modules: displayed module level -> %d (call %llu)",
             kTargetModuleLevel,
             static_cast<unsigned long long>(g_level_calls));
    }
    return kTargetModuleLevel;
}

// The singleton builds its inventory lists inside OnInstanceCreated, so capture
// the instance and grant after the original has run.
inline void controller_ready_hook(void* self, void* method) {
    if (self != nullptr && g_controller != self) {
        g_controller = self;
        LOGI("23.1.3-modules: modules controller captured");
    }
    if (g_orig_controller_ready != nullptr) {
        reinterpret_cast<InstanceVoidFn>(g_orig_controller_ready)(self, method);
    }
    grant_inventory("controller ready");
}

inline void storage_view_refresh_hook(void* self, void* method) {
    grant_inventory("storage grid");
    if (g_orig_storage_refresh != nullptr) {
        reinterpret_cast<InstanceVoidFn>(g_orig_storage_refresh)(self, method);
    }
}

inline void screen_refresh_hook(void* self, void* method) {
    grant_inventory("armory screen");
    if (g_orig_screen_refresh != nullptr) {
        reinterpret_cast<InstanceVoidFn>(g_orig_screen_refresh)(self, method);
    }
}

inline void insert_panel_refresh_hook(void* self, void* method) {
    grant_inventory("insert panel");
    if (g_orig_insert_refresh != nullptr) {
        reinterpret_cast<InstanceVoidFn>(g_orig_insert_refresh)(self, method);
    }
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------

inline bool install() {
    if (g_installed) {
        return true;
    }

    // Remember who we are: nothing managed may ever run on this thread.
    g_install_thread = pthread_self();
    g_install_thread_valid = true;

    if (!il2cpp::resolve()) {
        LOGE("23.1.3-modules: il2cpp api unavailable, module disabled");
        return false;
    }

    if (!hook::install({kNs, kModuleClass, kCurrentLevel, 0},
                       reinterpret_cast<void*>(&current_level_hook),
                       &g_orig_level, true)) {
        LOGE("23.1.3-modules: install incomplete, module disabled");
        return false;
    }

    const bool catalog_modules =
        bind(g_catalog_modules, kNs, kCatalogClass, kCatalogModules, 0);
    const bool catalog_sets =
        bind(g_catalog_sets, kNs, kCatalogClass, kCatalogSets, 0);
    if (!catalog_modules) {
        LOGE("23.1.3-modules: catalogue route missing, level stays X but the "
             "inventory cannot be filled");
    }
    if (!catalog_sets) {
        LOGE("23.1.3-modules: module set catalogue route missing");
    }

    g_field_modules = il2cpp::find_field(kNs, kControllerClass, kInventoryModules);
    g_field_sets = il2cpp::find_field(kNs, kControllerClass, kInventorySets);
    if (g_field_modules == nullptr) {
        LOGE("23.1.3-modules: ModulesController inventory field not found");
    }
    if (g_field_sets == nullptr) {
        LOGE("23.1.3-modules: ModulesController module set field not found");
    }

    struct Route {
        const char* klass;
        const char* method;
        void* replacement;
        void** original;
    };

    const Route routes[] = {
        {kControllerClass, kControllerReady,
         reinterpret_cast<void*>(&controller_ready_hook),
         &g_orig_controller_ready},
        {kStorageViewClass, kRefresh,
         reinterpret_cast<void*>(&storage_view_refresh_hook),
         &g_orig_storage_refresh},
        {kInfoScreenClass, kRefresh,
         reinterpret_cast<void*>(&screen_refresh_hook), &g_orig_screen_refresh},
        {kInsertPanelClass, kInsertRefresh,
         reinterpret_cast<void*>(&insert_panel_refresh_hook),
         &g_orig_insert_refresh},
    };

    constexpr int kRouteCount =
        static_cast<int>(sizeof(routes) / sizeof(routes[0]));
    int installed_routes = 0;
    for (const Route& route : routes) {
        if (hook::install({kNs, route.klass, route.method, 0},
                          route.replacement, route.original, false)) {
            ++installed_routes;
        }
    }
    LOGI("23.1.3-modules: inventory routes installed %d/%d", installed_routes,
         kRouteCount);

    g_installed = true;

    // Nothing managed runs here. The catalogue is read, verified and merged the
    // first time a game thread reaches one of the routes above.
    LOGI("23.1.3-modules: armed: level X guaranteed, catalogue granted into "
         "the ModulesController inventory lists from game threads only "
         "(expect %d modules, %d module sets); per-item module storage is left "
         "untouched",
         kExpectedModules, kExpectedModuleSets);
    return true;
}

inline void pump(const char* label) {
    if (!g_installed) {
        return;
    }
    ++g_pump_calls;
    if ((g_pump_calls % kPumpEvery) != 1) {
        return;
    }
    grant_inventory(label);
}

}  // namespace detail

inline bool install_hooks() { return detail::install(); }

inline void pump_from_main_menu() { detail::pump("main menu"); }

inline void pump_from_startup() { detail::pump("startup"); }

}  // namespace weapon_modules_2313
