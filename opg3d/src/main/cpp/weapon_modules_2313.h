#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) weapon and armor module unlock
//
// Rewritten from scratch: the module display path is no longer patched at all.
// Instead the mod grants every module definition the build already ships
// through the stock item-inventory grant API, exactly like a module chest
// reward does. The game itself then materializes the module objects, so the
// Armory, module storage, insert panels and module sets stay internally
// consistent.
//
// Why the previous read-path approach could never work (both attempts are
// documented in docs/PORT_23_1_3_MODULES.md):
//
//   * Appending the static catalog objects into ModulesController's own lists
//     only duplicated references. List<T>.Contains compares by reference, so
//     the lists grew from 42 to 84 entries without a single new owned module.
//   * Forcing the module inventory counter to report >= 1 never fired at all
//     for missing modules: PGCompany.<module>.<inventory count>() is only
//     called for module objects that already exist, and a module object only
//     exists once the item is owned. Runtime proof: the level clamp below logs
//     on every armory refresh, while the counter promotion never logged once.
//
// The ownership source of truth is the item registry
// PGCompany.上丞丅三业丙世不丙. Disassembly of the module inventory counter
// (RVA 0x24B0C38) shows the chain that decides whether a module exists:
//
//   module -> base item key (field <key>k__BackingField, +0x28)
//          -> registry singleton 下丌丑丁下丟丛丘上()      (RVA 0x3046000)
//          -> registry lookup 业上世且且丕丛三丘(key, ...)  (RVA 0x30603D4)
//          -> item instance count (virtual, vtable +0x1C8)
//
// So this port writes into that very registry:
//
//   丙丛业丐丐七丛不丂(key, Nullable<owned-filter>)          RVA 0x304F634 -> int
//   丘上丄三业丏丙不且(key, Nullable<obtain-cause>, Action)  RVA 0x3062B08 -> item
//
// The grant overload wraps the key into a list and forwards it to the stock
// "receive items" transaction (RVA 0x3061DB0) with an empty spend list, which
// is purely local: list/LINQ work, the registry's own private mutators, the
// item-changed event, and Progress service notifications. No backend call is
// involved, so it works offline.
//
// ModulesController.OnInstanceCreated (RVA 0x2814810) subscribes to those
// Progress notifications and rebuilds its module lists from them, which is why
// no UI list has to be touched by hand any more.
//
// ARM64 ABI reminder: generated managed methods take their explicit arguments
// followed by MethodInfo*; instance methods take `this` first. Per AAPCS64 a
// composite argument larger than 16 bytes (both Nullable<> arguments below) is
// passed indirectly, as a pointer to a caller-owned copy, and an all-zero
// Nullable<> is exactly a null optional (has_value == false).
// -----------------------------------------------------------------------------

#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <string>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

namespace weapon_modules_2313 {
namespace detail {

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

// ------------------------------------------------------------------ tunables

// Displayed module level for every module (the stock cap for module level X).
constexpr int32_t kUnlockedLevel = 10;

// Frames to wait after the main menu appears before the first grant, so the
// profile, the item registry and the Progress service are all live.
constexpr uint64_t kWarmupFrames = 120;

// Definitions processed per main-menu frame. Deliberately small: a grant is a
// full stock inventory transaction and must never land as one frame spike.
constexpr int32_t kGrantsPerTick = 2;

// Full catalogue sweeps attempted in total (late registry arrivals included).
constexpr int32_t kMaxPasses = 3;

// Frames between two sweeps (~15 s at 60 fps).
constexpr uint64_t kRecheckFrames = 900;

// Log the first kLogBurst grants in full, then every kLogPeriod-th one.
constexpr uint64_t kLogBurst = 8u;
constexpr uint64_t kLogPeriod = 8u;

// ----------------------------------------------------------- metadata names

constexpr const char* kNamespace = "PGCompany";
constexpr const char* kProgressNs = "Progress";

// Module class (all seven module categories, weapons and armor alike).
constexpr const char* kModuleClass = "丐三七世丝丗与丛上";
constexpr const char* kCurrentLevel = "七且丐东丒丆丑丈万";  // instance, 0 args -> int

// Static module catalogue: every module definition the build ships.
constexpr const char* kCatalogClass = "丐丞丒专且丁丈丌业";
constexpr const char* kCatalogModules = "丞七丌业丛丂丙上丝";  // static, 0 args -> List<module>

// Item registry: the ownership source of truth.
constexpr const char* kRegistryClass = "上丞丅三业丙世不丙";
constexpr const char* kRegistryInstance = "下丌丑丁下丟丛丘上";  // static, 0 args -> registry

// Base item class of every module: carries the registry key and the item name.
constexpr const char* kItemBaseClass = "三丛丐丙丈丌丈专万";
constexpr const char* kItemKeyField = "<下丕三上丂三丝丅丐>k__BackingField";
constexpr const char* kItemNameField = "<世下丐不丞与丞七丄>k__BackingField";

// Progress service: the grant transaction notifies it, so it must exist first.
constexpr const char* kProgressService = "东丝丂丄业丕且丙丑";
constexpr const char* kProgressInstance = "丞丏业丐丒与业丗与";  // static, 0 args -> service

// ---------------------------------------------------------- verified offsets
//
// The two registry entry points below are overloaded by argument type only
// (key / int / list), so metadata name plus argument count cannot select the
// right one. They are therefore taken by RVA from the verified 23.1.3 ARM64
// libil2cpp.so (ELF build id 57fcc18d2db06212416d480d53c0f881ee47c52a) and the
// base address is proven first: the two unambiguous getters must resolve, via
// metadata, to exactly base + their own RVA. If any check fails, nothing is
// armed.
constexpr uintptr_t kCatalogModulesRva = 0x3048A5Cu;
constexpr uintptr_t kRegistryInstanceRva = 0x3046000u;
constexpr uintptr_t kRegistryCountRva = 0x304F634u;
constexpr uintptr_t kRegistryGrantRva = 0x3062B08u;

// sizeof(Nullable<T>) for the two stock optional arguments. IL2CPP lays
// Nullable<T> out as { bool has_value; T value; }, so an all-zero buffer is a
// null optional and the callee substitutes its own default.
constexpr size_t kOwnedFilterSize = 24u;   // Nullable<丙与不与丟丂一东丟>
constexpr size_t kObtainCauseSize = 104u;  // Nullable<与专丂丕丌丅东丂东>

// ------------------------------------------------------------- managed ABI

using StaticObjFn = void* (*)(void* method);
using InstanceIntFn = int32_t (*)(void* self, void* method);
using ListCountFn = int32_t (*)(void* list, void* method);
using ListItemFn = void* (*)(void* list, int32_t index, void* method);

// (this, key, Nullable<owned-filter>*, MethodInfo*) -> owned count
using RegistryCountFn = int32_t (*)(void* self, void* key, void* filter,
                                   void* method);
// (this, key, Nullable<obtain-cause>*, Action, MethodInfo*) -> granted item
using RegistryGrantFn = void* (*)(void* self, void* key, void* cause,
                                  void* callback, void* method);

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

inline Managed g_catalog_modules{};
inline Managed g_registry_instance{};
inline Managed g_progress_service{};

inline RegistryCountFn g_registry_count = nullptr;
inline RegistryGrantFn g_registry_grant = nullptr;

inline void* g_key_field = nullptr;
inline void* g_name_field = nullptr;

inline InstanceIntFn g_orig_current_level = nullptr;

inline ListApi g_list{};

inline uint64_t g_frames = 0;
inline uint64_t g_next_sweep = 0;
inline uint64_t g_level_calls = 0;
inline int32_t g_cursor = 0;
inline int32_t g_pass = 0;
inline int32_t g_granted = 0;
inline int32_t g_already_owned = 0;
inline int32_t g_failed = 0;
inline bool g_sweeping = true;
inline bool g_grant_armed = false;
inline bool g_installed = false;

// ------------------------------------------------------------- diagnostics

inline bool should_log(uint64_t counter) {
    return counter <= kLogBurst || (counter % kLogPeriod) == 0u;
}

inline std::string item_name(void* module) {
    if (module == nullptr || g_name_field == nullptr ||
        il2cpp::field_get_value == nullptr) {
        return "<unknown>";
    }
    void* managed = nullptr;
    il2cpp::field_get_value(module, g_name_field, &managed);
    return il2cpp::to_utf8(managed, 48u);
}

// ------------------------------------------------------------- grant driver

inline void* module_key(void* module) {
    if (module == nullptr || g_key_field == nullptr ||
        il2cpp::field_get_value == nullptr) {
        return nullptr;
    }
    void* key = nullptr;
    il2cpp::field_get_value(module, g_key_field, &key);
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

// Grants one missing definition. Returns true when the definition is owned
// after the call, which also covers definitions that were already owned.
inline bool ensure_owned(void* registry, void* module, int32_t index,
                         int32_t total) {
    void* key = module_key(module);
    if (key == nullptr) {
        ++g_failed;
        return false;
    }

    const int32_t before = owned_count(registry, key);
    if (before >= 1) {
        ++g_already_owned;
        return true;
    }
    if (before < 0) {
        ++g_failed;
        return false;
    }

    // A null obtain cause makes the stock transaction use its own default,
    // and no completion callback is needed.
    alignas(8) unsigned char cause[kObtainCauseSize] = {};
    g_registry_grant(registry, key, cause, nullptr, nullptr);

    const int32_t after = owned_count(registry, key);
    if (after < 1) {
        ++g_failed;
        LOGW("23.1.3-modules: grant did not register '%s' (%" PRId32 "/%" PRId32
             ", count %" PRId32 " -> %" PRId32 ")",
             item_name(module).c_str(), index + 1, total, before, after);
        return false;
    }

    ++g_granted;
    if (should_log(static_cast<uint64_t>(g_granted))) {
        LOGI("23.1.3-modules: granted '%s' (%" PRId32 "/%" PRId32
             ", count 0 -> %" PRId32 ")",
             item_name(module).c_str(), index + 1, total, after);
    }
    return true;
}

inline void run_sweep() {
    if (!g_grant_armed || g_pass >= kMaxPasses) return;
    if (g_frames < kWarmupFrames) return;
    if (!g_sweeping) {
        if (g_frames < g_next_sweep) return;
        g_sweeping = true;
        g_cursor = 0;
        g_granted = 0;
        g_already_owned = 0;
        g_failed = 0;
    }

    // The stock grant transaction notifies the Progress service, so it has to
    // be alive before the first write.
    if (reinterpret_cast<StaticObjFn>(g_progress_service.ptr)(
            g_progress_service.info) == nullptr) {
        return;
    }

    void* registry = reinterpret_cast<StaticObjFn>(g_registry_instance.ptr)(
        g_registry_instance.info);
    if (registry == nullptr) return;

    void* modules = reinterpret_cast<StaticObjFn>(g_catalog_modules.ptr)(
        g_catalog_modules.info);
    if (modules == nullptr) return;
    if (!g_list && !resolve_list_api(modules, g_list)) return;

    const int32_t total =
        reinterpret_cast<ListCountFn>(g_list.count_ptr)(modules, g_list.count_info);
    if (total <= 0) return;

    int32_t processed = 0;
    while (g_cursor < total && processed < kGrantsPerTick) {
        void* module = reinterpret_cast<ListItemFn>(g_list.item_ptr)(
            modules, g_cursor, g_list.item_info);
        ensure_owned(registry, module, g_cursor, total);
        ++g_cursor;
        ++processed;
    }

    if (g_cursor < total) return;

    ++g_pass;
    const int32_t owned = g_granted + g_already_owned;
    LOGI("23.1.3-modules: pass %" PRId32 " complete (definitions=%" PRId32
         " granted=%" PRId32 " already owned=%" PRId32 " failed=%" PRId32 ")",
         g_pass, total, g_granted, g_already_owned, g_failed);

    if (owned >= total || g_pass >= kMaxPasses) {
        LOGI("23.1.3-modules: weapon and armor module inventory complete"
             " (%" PRId32 "/%" PRId32 " definitions owned, levels shown as %"
             PRId32 ")", owned, total, kUnlockedLevel);
        g_grant_armed = false;
        return;
    }

    g_sweeping = false;
    g_next_sweep = g_frames + kRecheckFrames;
}

// --------------------------------------------------------------- level hook
//
// Owned modules start at module level 1 and the stock level getter reads the
// per-module Progress counter. Clamping the displayed value keeps every module
// at the unlocked level without touching persistent per-item progress.
inline int32_t current_level_hook(void* self, void* method) {
    const int32_t stock = g_orig_current_level != nullptr
                              ? g_orig_current_level(self, method)
                              : 0;
    if (stock >= kUnlockedLevel) return stock;

    ++g_level_calls;
    if (should_log(g_level_calls)) {
        LOGI("23.1.3-modules: level %" PRId32 " -> %" PRId32 " (call=%" PRIu64
             ")", stock, kUnlockedLevel, g_level_calls);
    }
    return kUnlockedLevel;
}

// ------------------------------------------------------------- installation

// Proves that `base` really is the verified 23.1.3 libil2cpp.so image: the two
// unambiguous getters resolved through metadata must land on their recorded
// RVAs. Only then may the overloaded registry entry points be taken by RVA.
inline bool verify_image(uintptr_t base, const Managed& target, uintptr_t rva,
                        const char* label) {
    const auto expected = reinterpret_cast<void*>(base + rva);
    if (target.ptr == expected) return true;
    LOGE("23.1.3-modules: %s is at %p but RVA 0x%08" PRIxPTR " maps to %p;"
         " this is not the verified 23.1.3 ARM64 image",
         label, target.ptr, rva, expected);
    return false;
}

inline bool install(uintptr_t il2cpp_base) {
    if (g_installed) return true;

    bool resolved = true;
    resolved &= bind(g_catalog_modules, kNamespace, kCatalogClass,
                     kCatalogModules, 0);
    resolved &= bind(g_registry_instance, kNamespace, kRegistryClass,
                     kRegistryInstance, 0);
    resolved &= bind(g_progress_service, kProgressNs, kProgressService,
                     kProgressInstance, 0);
    if (!resolved) {
        LOGE("23.1.3-modules: metadata does not match the expected 23.1.3"
             " build; nothing was armed");
        return false;
    }

    g_key_field = il2cpp::find_field(kNamespace, kItemBaseClass, kItemKeyField);
    g_name_field = il2cpp::find_field(kNamespace, kItemBaseClass, kItemNameField);
    if (g_key_field == nullptr) {
        LOGE("23.1.3-modules: the base item key field is missing; nothing was"
             " armed");
        return false;
    }

    if (il2cpp_base == 0u) {
        LOGE("23.1.3-modules: libil2cpp.so base address is unknown; the"
             " inventory grant cannot be armed");
        return false;
    }
    if (!verify_image(il2cpp_base, g_catalog_modules, kCatalogModulesRva,
                      "the module catalogue getter") ||
        !verify_image(il2cpp_base, g_registry_instance, kRegistryInstanceRva,
                      "the item registry singleton")) {
        return false;
    }

    g_registry_count = reinterpret_cast<RegistryCountFn>(
        reinterpret_cast<void*>(il2cpp_base + kRegistryCountRva));
    g_registry_grant = reinterpret_cast<RegistryGrantFn>(
        reinterpret_cast<void*>(il2cpp_base + kRegistryGrantRva));

    if (!hook::install({kNamespace, kModuleClass, kCurrentLevel, 0},
                       reinterpret_cast<void*>(&current_level_hook),
                       reinterpret_cast<void**>(&g_orig_current_level), true)) {
        LOGE("23.1.3-modules: the module level getter could not be hooked");
        return false;
    }

    g_grant_armed = true;
    g_installed = true;
    LOGI("23.1.3-modules: armed: every weapon and armor module definition is"
         " granted through the stock item inventory (%" PRId32
         " per menu frame, %" PRId32 " sweeps max) and displayed at level %"
         PRId32, kGrantsPerTick, kMaxPasses, kUnlockedLevel);
    return true;
}

inline void pump() {
    if (!g_installed) return;
    ++g_frames;
    run_sweep();
}

}  // namespace detail

// Arms the module unlock. `il2cpp_base` is the load address of libil2cpp.so.
inline bool install_hooks(uintptr_t il2cpp_base) {
    return detail::install(il2cpp_base);
}

// Driven once per main-menu frame from progression_2313's MainMenuController
// .Update hook: the grant needs a game thread and a live main menu, and the
// menu Update slot is the only such point that is already owned by this port.
inline void pump_from_main_menu() { detail::pump(); }

}  // namespace weapon_modules_2313
