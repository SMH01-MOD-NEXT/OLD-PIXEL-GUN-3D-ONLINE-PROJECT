#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) lobby craft catalogue port
//
// Port of lobby_catalog_1610.h. The lobby craft screen is populated by the
// backend: offline it stays empty, so no lobby craft can ever be started.
//
// This module walks LobbyItemsController's own local catalogue and feeds every
// entry back into the controller through the very same add path the online
// flow uses. Nothing is fabricated - only items the build already ships are
// granted, which keeps the UI, prices and recipes internally consistent.
//
// The work is spread over many frames from the controller's own Update slot:
// a cursor grants a handful of entries per tick, backs off after repeated
// failures, and re-checks periodically so late catalogue additions are picked
// up too.
//
// Every managed identifier below is generated from the 23.1.3 global-metadata
// by gen_craft.py and verified byte for byte against the method table.
// -----------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

namespace lobby_catalog_2313 {
namespace detail {

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

// ------------------------------------------------------------------ tunables

// Grants attempted per Update tick. Small so a large catalogue never lands as
// a single frame spike.
constexpr int32_t kGrantsPerTick = 3;

// How many full sweeps of the catalogue are attempted before giving up.
constexpr int32_t kMaxPasses = 8;

// Consecutive add failures tolerated before the module disarms itself.
constexpr int32_t kMaxFailures = 32;

// Ticks between two sweeps (~30 s at 60 fps) so late arrivals are picked up.
constexpr uint64_t kRecheckTicks = 1800;

// ----------------------------------------------------------- metadata names

constexpr const char* kLobbyNs = "Rilisoft";
constexpr const char* kLobbyClass = "LobbyItemsController";

constexpr const char* kLobbyReady = "且下丆专专丄丟丑丑";      // static,   0 args -> bool
constexpr int kLobbyReadyArgs = 0;
constexpr const char* kLobbyCatalog = "丈丟丄不丞三且一与";  // instance, 0 args -> List<item>
constexpr int kLobbyCatalogArgs = 0;
constexpr const char* kLobbyAdd = "丈丗东丏丟丏丕七丆";          // instance, 5 args -> bool
constexpr int kLobbyAddArgs = 5;
constexpr const char* kLobbyUpdate = "Update";    // instance, 0 args -> void
constexpr int kLobbyUpdateArgs = 0;

// ------------------------------------------------------------- managed ABI

using StaticBoolFn = bool (*)(void* method);
using InstanceObjFn = void* (*)(void* self, void* method);
using InstanceVoidFn = void (*)(void* self, void* method);
// (item, bool, bool, object, bool) -> bool
using LobbyAddFn = bool (*)(void* self, void* item, bool a, bool b, void* c, bool d,
                            void* method);
using ListCountFn = int32_t (*)(void* list, void* method);
using ListItemFn = void* (*)(void* list, int32_t index, void* method);

struct Managed {
    void* info = nullptr;
    void* ptr = nullptr;
    explicit operator bool() const noexcept { return info != nullptr && ptr != nullptr; }
};

inline bool bind(Managed& out, const char* namespaze, const char* klass,
                 const char* method, int args_count) {
    void* info = il2cpp::find_method_info(namespaze, klass, method, args_count);
    if (info == nullptr) {
        LOGE("23.1.3-lobby-catalog: %s::%s/%d not found in metadata", klass, method, args_count);
        return false;
    }
    void* ptr = il2cpp::method_pointer(info);
    if (ptr == nullptr) {
        LOGE("23.1.3-lobby-catalog: %s::%s/%d has no compiled body", klass, method, args_count);
        return false;
    }
    out.info = info;
    out.ptr = ptr;
    return true;
}

// List<T> is a generic instantiation, so its accessors are resolved off the
// concrete object rather than by namespace/name.
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
    if (klass == nullptr) {
        return false;
    }
    api.count_info = il2cpp::class_get_method_from_name(klass, "get_Count", 0);
    api.item_info = il2cpp::class_get_method_from_name(klass, "get_Item", 1);
    if (api.count_info == nullptr || api.item_info == nullptr) {
        return false;
    }
    api.count_ptr = il2cpp::method_pointer(api.count_info);
    api.item_ptr = il2cpp::method_pointer(api.item_info);
    return static_cast<bool>(api);
}

// ------------------------------------------------------------------- state

inline Managed g_ready{};
inline Managed g_catalog{};
inline Managed g_add{};

inline InstanceVoidFn g_orig_update = nullptr;

inline int32_t g_cursor = 0;
inline int32_t g_pass = 0;
inline int32_t g_failures = 0;
inline int32_t g_granted = 0;
inline uint64_t g_ticks = 0;
inline uint64_t g_next_sweep = 0;
inline bool g_disarmed = false;
inline bool g_sweeping = true;

// -------------------------------------------------------------- grant driver

inline void run_grants(void* self) {
    if (g_disarmed || self == nullptr || !g_catalog || !g_add) {
        return;
    }

    // The controller refuses adds until its own bootstrap has completed.
    if (g_ready && !reinterpret_cast<StaticBoolFn>(g_ready.ptr)(g_ready.info)) {
        return;
    }

    if (!g_sweeping) {
        if (g_ticks < g_next_sweep) {
            return;
        }
        g_sweeping = true;
        g_cursor = 0;
    }

    void* list = reinterpret_cast<InstanceObjFn>(g_catalog.ptr)(self, g_catalog.info);
    if (list == nullptr) {
        return;
    }

    ListApi api;
    if (!resolve_list_api(list, api)) {
        LOGE("23.1.3-lobby-catalog: List<> accessors unavailable, disarming");
        g_disarmed = true;
        return;
    }

    const int32_t total = reinterpret_cast<ListCountFn>(api.count_ptr)(list, api.count_info);
    if (total <= 0) {
        return;
    }

    for (int32_t done = 0; done < kGrantsPerTick; ++done) {
        if (g_cursor >= total) {
            g_sweeping = false;
            g_next_sweep = g_ticks + kRecheckTicks;
            ++g_pass;
            LOGI("23.1.3-lobby-catalog: sweep %d finished (%d/%d entries granted)",
                 g_pass, g_granted, total);
            if (g_pass >= kMaxPasses) {
                LOGI("23.1.3-lobby-catalog: reached %d sweeps, standing down", kMaxPasses);
                g_disarmed = true;
            }
            return;
        }

        void* item = reinterpret_cast<ListItemFn>(api.item_ptr)(list, g_cursor, api.item_info);
        ++g_cursor;
        if (item == nullptr) {
            continue;
        }

        // (item, silent, persist, context, refresh). The cursor guarantees each
        // entry is offered exactly once per sweep, so no owned-item predicate is
        // needed - the controller itself rejects duplicates.
        const bool added = reinterpret_cast<LobbyAddFn>(g_add.ptr)(
            self, item, false, true, nullptr, true, g_add.info);

        if (added) {
            ++g_granted;
            g_failures = 0;
        } else if (++g_failures >= kMaxFailures) {
            LOGE("23.1.3-lobby-catalog: %d consecutive add failures, disarming", g_failures);
            g_disarmed = true;
            return;
        }
    }
}

inline void update_hook(void* self, void* method) {
    if (g_orig_update != nullptr) {
        g_orig_update(self, method);
    }
    ++g_ticks;
    run_grants(self);
}

// ------------------------------------------------------------------ install

inline bool install() {
    bool resolved = true;
    resolved &= bind(g_catalog, kLobbyNs, kLobbyClass, kLobbyCatalog, kLobbyCatalogArgs);
    resolved &= bind(g_add, kLobbyNs, kLobbyClass, kLobbyAdd, kLobbyAddArgs);
    if (!resolved) {
        LOGE("23.1.3-lobby-catalog: catalogue API unavailable, module disabled");
        return false;
    }

    // Optional readiness gate: without it grants simply start one bootstrap
    // later, which the add path already tolerates.
    if (!bind(g_ready, kLobbyNs, kLobbyClass, kLobbyReady, kLobbyReadyArgs)) {
        LOGE("23.1.3-lobby-catalog: readiness gate unavailable, granting unguarded");
    }

    const bool ok = hook::install({kLobbyNs, kLobbyClass, kLobbyUpdate, kLobbyUpdateArgs},
                                  reinterpret_cast<void*>(&update_hook),
                                  reinterpret_cast<void**>(&g_orig_update), true);
    if (ok) {
        LOGI("23.1.3-lobby-catalog: local craft catalogue grant armed "
             "(%d/tick, %d sweeps max)", kGrantsPerTick, kMaxPasses);
    } else {
        LOGE("23.1.3-lobby-catalog: could not hook the controller update slot");
    }
    return ok;
}

}  // namespace detail

inline bool install_hooks() { return detail::install(); }

}  // namespace lobby_catalog_2313
