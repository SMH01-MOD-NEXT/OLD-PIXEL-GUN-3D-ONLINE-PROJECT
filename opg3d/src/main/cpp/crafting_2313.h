#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) crafting port
//
// Ports three behaviours that shipped on the 16.1.0 branch (crafting_1610.h and
// clan_craft.h) to the 23.1.3 IL2CPP layout:
//
//   1. Weapon crafting      - the recipe detail gate always opens and the owned
//                             detail counter reports a synthetic local stock, so
//                             craft recipes never stall on "not enough details".
//   2. Dead-clan workaround - clan part queries are answered from a synthetic
//                             local stock, so clan crafting keeps working now
//                             that the clan backend is gone.
//   3. "No connection" fix  - the fort and lobby craft screens get a local clock
//                             instead of a server timestamp, and both craft
//                             connection-error banners are swallowed.
//
// Deliberately NOT ported from 16.1.0: the weapon *upgrade* timestamp fallback.
// Upgrades already work on 23.1.3, so WeaponManager's upgrade slot
// (RVA 0x014245FC) and the shared PixelTime / FriendsController timestamp
// helpers (0x03D5E394 / 0x01D8BB34) are left completely untouched.
//
// The cheat-banner save shield is NOT duplicated here: progression_2313.h
// already neutralises CheatDetectedBanner for the whole process.
//
// Every managed identifier below is generated from the 23.1.3 global-metadata
// by gen_craft.py and verified byte for byte against the method table.
// -----------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <ctime>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

namespace crafting_2313 {
namespace detail {

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

// ------------------------------------------------------------------ tunables

// Reported stock for craft details. Large enough that no recipe can ever be
// short, small enough to stay far away from int32 overflow in the UI maths.
constexpr int32_t kSyntheticOwnedDetails = 99999;

// Reported stock for clan craft parts. Clan recipes ask for single digits.
constexpr int32_t kSyntheticClanParts = 99;

// Throttle for the repeating hooks so logcat stays readable.
constexpr uint64_t kLogEvery = 120;

// ----------------------------------------------------------- metadata names

constexpr const char* kFortsNs = "Rilisoft";
constexpr const char* kFortsClass = "FortsManager";
constexpr const char* kFortsTime = "丑丅丗万不丏丌丗专";        // static, 0 args -> Nullable<long>
constexpr int kFortsTimeArgs = 0;

constexpr const char* kLobbyItemsNs = "Rilisoft";
constexpr const char* kLobbyItemsClass = "LobbyItemsController";
constexpr const char* kLobbyItemsTime = "丑丅丗万不丏丌丗专";  // static, 0 args -> Nullable<long>
constexpr int kLobbyItemsTimeArgs = 0;

constexpr const char* kFortCraftNs = "Rilisoft";
constexpr const char* kFortCraftClass = "FortCraftController";
constexpr const char* kFortCraftBanner = "ShowConnectionErrorBanner";  // instance, 0 args -> void
constexpr int kFortCraftBannerArgs = 0;

constexpr const char* kLobbyCraftNs = "Rilisoft";
constexpr const char* kLobbyCraftClass = "LobbyCraftController";
constexpr const char* kLobbyCraftBanner = "丏丌东丙与丁丅丟丘";  // instance, 0 args -> void
constexpr int kLobbyCraftBannerArgs = 0;

// Global namespace helper that already returns the device UTC clock in the very
// unit the craft screens expect. Resolve-only: never hooked.
constexpr const char* kClockNs = "";
constexpr const char* kClockClass = "世专七丘上丙三一丝";
constexpr const char* kClockNow = "丑东丟丗下丄丐三丈";        // static, 0 args -> long
constexpr int kClockNowArgs = 0;

constexpr const char* kDetailsNs = "Rilisoft";
constexpr const char* kDetailsClass = "东丌上万丆三丐丆丝";
constexpr const char* kDetailsGate = "七世丛万丞丕丑不丈";     // static, 2 args -> bool
constexpr int kDetailsGateArgs = 2;
constexpr const char* kDetailsOwned = "丁丘丐丂丗万丕下丑";   // static, 1 arg  -> int
constexpr int kDetailsOwnedArgs = 1;

constexpr const char* kClansNs = "";
constexpr const char* kClansClass = "ClansController";
constexpr const char* kClanPartCount = "上丙七丟丐上丘丆丛";    // static, 2 args -> int
constexpr int kClanPartCountArgs = 2;
constexpr const char* kClanHasPart = "丛与上丁丝下丟业世";        // static, 2 args -> bool
constexpr int kClanHasPartArgs = 2;

// ------------------------------------------------------------------ managed ABI

// System.Nullable<long> is a 16 byte POD returned in x0/x1 on AArch64.
struct NullableInt64 {
    uint8_t has_value;
    uint8_t pad[7];
    int64_t value;
};
static_assert(sizeof(NullableInt64) == 16, "Nullable<long> must be a 16 byte POD");
static_assert(offsetof(NullableInt64, value) == 8, "Nullable<long> payload sits at +8");

// AArch64 IL2CPP calling convention: statics take no leading context argument
// and every managed method receives its MethodInfo* as the final parameter.
using NullableTimeFn = NullableInt64 (*)(void* method);
using StaticLongFn = int64_t (*)(void* method);
using InstanceVoidFn = void (*)(void* self, void* method);
using StaticBoolStringsFn = bool (*)(void* a, void* b, void* method);
using StaticIntStringFn = int32_t (*)(void* id, void* method);
using StaticIntStringEnumFn = int32_t (*)(void* id, int32_t kind, void* method);
using StaticBoolStringEnumFn = bool (*)(void* id, int32_t kind, void* method);

struct Managed {
    void* info = nullptr;
    void* ptr = nullptr;
    explicit operator bool() const noexcept { return info != nullptr && ptr != nullptr; }
};

inline bool bind(Managed& out, const char* namespaze, const char* klass,
                 const char* method, int args_count) {
    void* info = il2cpp::find_method_info(namespaze, klass, method, args_count);
    if (info == nullptr) {
        LOGE("23.1.3-crafting: %s::%s/%d not found in metadata", klass, method, args_count);
        return false;
    }
    void* ptr = il2cpp::method_pointer(info);
    if (ptr == nullptr) {
        LOGE("23.1.3-crafting: %s::%s/%d has no compiled body", klass, method, args_count);
        return false;
    }
    out.info = info;
    out.ptr = ptr;
    return true;
}

// ------------------------------------------------------------------- state

inline Managed g_clock{};

inline NullableTimeFn g_orig_fort_time = nullptr;
inline NullableTimeFn g_orig_lobby_time = nullptr;
inline InstanceVoidFn g_orig_fort_banner = nullptr;
inline InstanceVoidFn g_orig_lobby_banner = nullptr;
inline StaticBoolStringsFn g_orig_detail_gate = nullptr;
inline StaticIntStringFn g_orig_detail_owned = nullptr;
inline StaticIntStringEnumFn g_orig_clan_count = nullptr;
inline StaticBoolStringEnumFn g_orig_clan_has = nullptr;

inline uint64_t g_fort_time_calls = 0;
inline uint64_t g_lobby_time_calls = 0;
inline uint64_t g_banner_calls = 0;
inline uint64_t g_detail_calls = 0;
inline uint64_t g_clan_calls = 0;

// Returns the current wall clock in the unit the craft screens expect. Prefers
// the game's own helper so the unit can never drift from the managed side, and
// only falls back to POSIX time when that helper could not be resolved.
inline int64_t local_now() {
    if (g_clock) {
        return reinterpret_cast<StaticLongFn>(g_clock.ptr)(g_clock.info);
    }
    return static_cast<int64_t>(::time(nullptr));
}

inline NullableInt64 local_timestamp() {
    NullableInt64 out{};
    out.has_value = 1;
    out.value = local_now();
    return out;
}

// --------------------------------------------------------------- hook bodies

// Fort / clan craft screens ask the backend what time it is. Offline that query
// returns an empty Nullable and every craft slot renders as "no connection".
inline NullableInt64 fort_time_hook(void* method) {
    NullableInt64 out{};
    if (g_orig_fort_time != nullptr) {
        out = g_orig_fort_time(method);
    }
    if (out.has_value != 0) {
        return out;
    }
    if ((g_fort_time_calls++ % kLogEvery) == 0) {
        LOGI("23.1.3-crafting: fort craft clock served locally (call %llu)",
             static_cast<unsigned long long>(g_fort_time_calls));
    }
    return local_timestamp();
}

inline NullableInt64 lobby_time_hook(void* method) {
    NullableInt64 out{};
    if (g_orig_lobby_time != nullptr) {
        out = g_orig_lobby_time(method);
    }
    if (out.has_value != 0) {
        return out;
    }
    if ((g_lobby_time_calls++ % kLogEvery) == 0) {
        LOGI("23.1.3-crafting: lobby craft clock served locally (call %llu)",
             static_cast<unsigned long long>(g_lobby_time_calls));
    }
    return local_timestamp();
}

// Both banners are pure UI: swallowing them leaves the craft screen interactive
// instead of parking a modal "no connection" overlay on top of it.
inline void fort_banner_hook(void* self, void* method) {
    (void)self;
    (void)method;
    if ((g_banner_calls++ % kLogEvery) == 0) {
        LOGI("23.1.3-crafting: suppressed fort craft connection banner (call %llu)",
             static_cast<unsigned long long>(g_banner_calls));
    }
}

inline void lobby_banner_hook(void* self, void* method) {
    (void)self;
    (void)method;
    if ((g_banner_calls++ % kLogEvery) == 0) {
        LOGI("23.1.3-crafting: suppressed lobby craft connection banner (call %llu)",
             static_cast<unsigned long long>(g_banner_calls));
    }
}

// Recipe gate: "can this detail be spent on this recipe". The original result is
// still evaluated so any lazy inventory initialisation inside it keeps running.
inline bool detail_gate_hook(void* a, void* b, void* method) {
    if (g_orig_detail_gate != nullptr) {
        (void)g_orig_detail_gate(a, b, method);
    }
    if ((g_detail_calls++ % kLogEvery) == 0) {
        LOGI("23.1.3-crafting: detail gate forced open (call %llu)",
             static_cast<unsigned long long>(g_detail_calls));
    }
    return true;
}

// Owned detail counter. Never lowers a real count, only raises it.
inline int32_t detail_owned_hook(void* id, void* method) {
    int32_t original = 0;
    if (g_orig_detail_owned != nullptr) {
        original = g_orig_detail_owned(id, method);
    }
    return original >= kSyntheticOwnedDetails ? original : kSyntheticOwnedDetails;
}

// Clan stock. The clan backend is dead on 23.1.3, so these two always answered
// zero / false and every clan recipe was permanently locked.
inline int32_t clan_count_hook(void* id, int32_t kind, void* method) {
    int32_t original = 0;
    if (g_orig_clan_count != nullptr) {
        original = g_orig_clan_count(id, kind, method);
    }
    if ((g_clan_calls++ % kLogEvery) == 0) {
        LOGI("23.1.3-crafting: clan stock served locally (call %llu)",
             static_cast<unsigned long long>(g_clan_calls));
    }
    return original >= kSyntheticClanParts ? original : kSyntheticClanParts;
}

inline bool clan_has_hook(void* id, int32_t kind, void* method) {
    if (g_orig_clan_has != nullptr) {
        (void)g_orig_clan_has(id, kind, method);
    }
    return true;
}

// ------------------------------------------------------------------ install

inline bool install() {
    // Resolve-only. A missing clock is not fatal: local_now() degrades to the
    // POSIX clock, which is the same wall time in seconds.
    if (!bind(g_clock, kClockNs, kClockClass, kClockNow, kClockNowArgs)) {
        LOGE("23.1.3-crafting: managed UTC clock unavailable, using ::time() instead");
    }

    bool ok = true;

    ok &= hook::install({kFortsNs, kFortsClass, kFortsTime, kFortsTimeArgs},
                        reinterpret_cast<void*>(&fort_time_hook),
                        reinterpret_cast<void**>(&g_orig_fort_time), true);
    ok &= hook::install({kLobbyItemsNs, kLobbyItemsClass, kLobbyItemsTime, kLobbyItemsTimeArgs},
                        reinterpret_cast<void*>(&lobby_time_hook),
                        reinterpret_cast<void**>(&g_orig_lobby_time), true);

    ok &= hook::install({kFortCraftNs, kFortCraftClass, kFortCraftBanner, kFortCraftBannerArgs},
                        reinterpret_cast<void*>(&fort_banner_hook),
                        reinterpret_cast<void**>(&g_orig_fort_banner), true);
    ok &= hook::install({kLobbyCraftNs, kLobbyCraftClass, kLobbyCraftBanner, kLobbyCraftBannerArgs},
                        reinterpret_cast<void*>(&lobby_banner_hook),
                        reinterpret_cast<void**>(&g_orig_lobby_banner), true);

    ok &= hook::install({kDetailsNs, kDetailsClass, kDetailsGate, kDetailsGateArgs},
                        reinterpret_cast<void*>(&detail_gate_hook),
                        reinterpret_cast<void**>(&g_orig_detail_gate), true);
    ok &= hook::install({kDetailsNs, kDetailsClass, kDetailsOwned, kDetailsOwnedArgs},
                        reinterpret_cast<void*>(&detail_owned_hook),
                        reinterpret_cast<void**>(&g_orig_detail_owned), true);

    ok &= hook::install({kClansNs, kClansClass, kClanPartCount, kClanPartCountArgs},
                        reinterpret_cast<void*>(&clan_count_hook),
                        reinterpret_cast<void**>(&g_orig_clan_count), true);
    ok &= hook::install({kClansNs, kClansClass, kClanHasPart, kClanHasPartArgs},
                        reinterpret_cast<void*>(&clan_has_hook),
                        reinterpret_cast<void**>(&g_orig_clan_has), true);

    if (ok) {
        LOGI("23.1.3-crafting: weapon craft + clan stock + offline craft clock armed "
             "(details=%d clan=%d)", kSyntheticOwnedDetails, kSyntheticClanParts);
    } else {
        LOGE("23.1.3-crafting: install incomplete, crafting port disabled");
    }
    return ok;
}

}  // namespace detail

inline bool install_hooks() { return detail::install(); }

}  // namespace crafting_2313
