#pragma once

#include <atomic>
#include <cinttypes>
#include <cstdint>

#include "hook.h"
#include "log.h"

// Passive comparison trace for the intermittent 23.1.3 anomaly cluster.
// Every hook delegates to stock code and records only inputs/results. A log
// from a healthy launch can therefore be diffed against an affected launch.
namespace anomaly_trace_2313 {
namespace detail {
using MethodInfo = void;
using StaticBoolFn = bool (*)(const MethodInfo*);
using StaticSetBoolFn = void (*)(bool, const MethodInfo*);
using InstanceVoidFn = void (*)(void*, const MethodInfo*);
using InstanceIntFn = int32_t (*)(void*, const MethodInfo*);

constexpr const char* kSettingsClass = u8"丙一业三丅万丂丙丞";
constexpr const char* kOfflineGet = u8"且一丁丐丐丈丌不丑"; // 0x2B79FB4
constexpr const char* kOfflineSet = u8"丆三丕三万丅丄一下"; // 0x2B7A00C
constexpr const char* kOfflineVerdict = u8"丐一丆丈世丆七丟丘";
constexpr const char* kPunClass = u8"丟丝专丄丑世丞世丒";
constexpr const char* kPunOfflineGet = u8"丑丂丞下世东丆丝不";
constexpr const char* kPunOfflineSet = u8"丟不业丏七丗丈丌丁";
constexpr const char* kVeteranState = u8"丗丒丆丘丌一丄上三";
constexpr const char* kVeteranComputedState = u8"世专一丁万丌七上丟";
constexpr const char* kVeteranAvailable = u8"且丕不三丛万丒丐丙";

inline StaticBoolFn g_offline_get = nullptr;
inline StaticSetBoolFn g_offline_set = nullptr;
inline StaticBoolFn g_offline_verdict = nullptr;
inline StaticBoolFn g_pun_offline_get = nullptr;
inline StaticSetBoolFn g_pun_offline_set = nullptr;
inline InstanceVoidFn g_account_enable = nullptr;
inline InstanceIntFn g_veteran_state = nullptr;
inline InstanceIntFn g_veteran_listener_state = nullptr;
inline InstanceIntFn g_veteran_roulette_state = nullptr;
inline StaticBoolFn g_veteran_available = nullptr;
inline InstanceVoidFn g_veteran_enable = nullptr;

inline std::atomic<int32_t> g_last_offline{-1};
inline std::atomic<int32_t> g_last_pun_offline{-1};
inline std::atomic<int32_t> g_last_offline_verdict{-1};
inline std::atomic<uint32_t> g_snapshot{0};

template <typename Fn> void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}
template <typename Fn> void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

void log_snapshot(const char* point) {
    const int offline = g_offline_get ? (g_offline_get(nullptr) ? 1 : 0) : -1;
    const int pun = g_pun_offline_get ? (g_pun_offline_get(nullptr) ? 1 : 0) : -1;
    // Do not invoke the compound OfflineMod predicate just for diagnostics;
    // report its most recent stock result so tracing cannot initialize or
    // advance unrelated account/auth services.
    const int verdict = g_last_offline_verdict.load();
    const uint32_t n = g_snapshot.fetch_add(1u) + 1u;
    LOGI("23.1.3-anomaly[#%u %s]: accountOffline=%d punOffline=%d "
         "offlineModVerdict=%d", n, point, offline, pun, verdict);
}

bool hook_offline_get(const MethodInfo* method) {
    const bool value = g_offline_get ? g_offline_get(method) : false;
    const int previous = g_last_offline.exchange(value ? 1 : 0);
    if (previous != (value ? 1 : 0)) {
        LOGW("23.1.3-anomaly: account offline getter changed %d -> %d",
             previous, value ? 1 : 0);
    }
    return value;
}

void hook_offline_set(bool value, const MethodInfo* method) {
    const bool before = g_offline_get ? g_offline_get(nullptr) : false;
    LOGW("23.1.3-anomaly: account offline setter requested=%d before=%d",
         value ? 1 : 0, before ? 1 : 0);
    if (g_offline_set) g_offline_set(value, method);
    const bool after = g_offline_get ? g_offline_get(nullptr) : value;
    g_last_offline.store(after ? 1 : 0);
    LOGW("23.1.3-anomaly: account offline setter completed after=%d",
         after ? 1 : 0);
}

bool hook_pun_offline_get(const MethodInfo* method) {
    const bool value = g_pun_offline_get ? g_pun_offline_get(method) : false;
    const int previous = g_last_pun_offline.exchange(value ? 1 : 0);
    if (previous != (value ? 1 : 0)) {
        LOGW("23.1.3-anomaly: PUN offlineMode changed %d -> %d",
             previous, value ? 1 : 0);
    }
    return value;
}

void hook_pun_offline_set(bool value, const MethodInfo* method) {
    const bool before = g_pun_offline_get ? g_pun_offline_get(nullptr) : false;
    LOGW("23.1.3-anomaly: PUN offlineMode setter requested=%d before=%d",
         value ? 1 : 0, before ? 1 : 0);
    if (g_pun_offline_set) g_pun_offline_set(value, method);
    const bool after = g_pun_offline_get ? g_pun_offline_get(nullptr) : value;
    g_last_pun_offline.store(after ? 1 : 0);
    LOGW("23.1.3-anomaly: PUN offlineMode setter completed after=%d",
         after ? 1 : 0);
}

bool hook_offline_verdict(const MethodInfo* method) {
    const bool value = g_offline_verdict ? g_offline_verdict(method) : false;
    const int previous = g_last_offline_verdict.exchange(value ? 1 : 0);
    if (previous != (value ? 1 : 0)) {
        LOGW("23.1.3-anomaly: OfflineModController verdict changed %d -> %d",
             previous, value ? 1 : 0);
    }
    return value;
}

void hook_account_enable(void* self, const MethodInfo* method) {
    log_snapshot("SettingsTabAccount.OnEnable/enter");
    if (g_account_enable) g_account_enable(self, method);
    log_snapshot("SettingsTabAccount.OnEnable/return");
}

int32_t trace_veteran_state(InstanceIntFn original, void* self,
                            const MethodInfo* method, const char* point) {
    const int32_t state = original ? original(self, method) : -1;
    LOGI("23.1.3-anomaly: %s state=%d (0=None 1=CanOpen 2=CantOpen "
         "3=Unavailable)", point, state);
    log_snapshot(point);
    return state;
}
int32_t hook_veteran_state(void* self, const MethodInfo* method) {
    return trace_veteran_state(g_veteran_state, self, method,
                               "VeteranlLootBoxUI");
}
int32_t hook_veteran_listener_state(void* self, const MethodInfo* method) {
    return trace_veteran_state(g_veteran_listener_state, self, method,
                               "VeteranLootBoxUI_Listener");
}
int32_t hook_veteran_roulette_state(void* self, const MethodInfo* method) {
    return trace_veteran_state(g_veteran_roulette_state, self, method,
                               "VeteranLootBoxUI_RouletteListener");
}
bool hook_veteran_available(const MethodInfo* method) {
    const bool value = g_veteran_available ? g_veteran_available(method) : false;
    LOGI("23.1.3-anomaly: VeteranLootBoxUI available=%d", value ? 1 : 0);
    return value;
}
void hook_veteran_enable(void* self, const MethodInfo* method) {
    log_snapshot("VeteranLootBoxUI.OnEnable/enter");
    if (g_veteran_enable) g_veteran_enable(self, method);
    log_snapshot("VeteranLootBoxUI.OnEnable/return");
}

bool add(const hook::ManagedMethod& target, void* hook_fn, void** original,
         bool required, int* count) {
    const bool ok = hook::install(target, hook_fn, original, required);
    if (ok) ++*count;
    return ok;
}
} // namespace detail

inline bool install_hooks() {
    int count = 0;
    bool core = detail::add(
        {"", detail::kSettingsClass, detail::kOfflineGet, 0},
        detail::replacement(&detail::hook_offline_get),
        detail::original_slot(&detail::g_offline_get), true, &count);
    core &= detail::add(
        {"", detail::kSettingsClass, detail::kOfflineSet, 1},
        detail::replacement(&detail::hook_offline_set),
        detail::original_slot(&detail::g_offline_set), true, &count);
    core &= detail::add(
        {"", "OfflineModController", detail::kOfflineVerdict, 0},
        detail::replacement(&detail::hook_offline_verdict),
        detail::original_slot(&detail::g_offline_verdict), true, &count);
    core &= detail::add(
        {"", detail::kPunClass, detail::kPunOfflineGet, 0},
        detail::replacement(&detail::hook_pun_offline_get),
        detail::original_slot(&detail::g_pun_offline_get), true, &count);
    core &= detail::add(
        {"", detail::kPunClass, detail::kPunOfflineSet, 1},
        detail::replacement(&detail::hook_pun_offline_set),
        detail::original_slot(&detail::g_pun_offline_set), true, &count);

    detail::add({"", "SettingsTabAccount", "OnEnable", 0},
        detail::replacement(&detail::hook_account_enable),
        detail::original_slot(&detail::g_account_enable), false, &count);
    detail::add({"", "VeteranlLootBoxUI", detail::kVeteranState, 0},
        detail::replacement(&detail::hook_veteran_state),
        detail::original_slot(&detail::g_veteran_state), false, &count);
    detail::add({"", "VeteranLootBoxUI_Listener", detail::kVeteranComputedState, 0},
        detail::replacement(&detail::hook_veteran_listener_state),
        detail::original_slot(&detail::g_veteran_listener_state), false, &count);
    detail::add({"", "VeteranLootBoxUI_RouletteListener", detail::kVeteranComputedState, 0},
        detail::replacement(&detail::hook_veteran_roulette_state),
        detail::original_slot(&detail::g_veteran_roulette_state), false, &count);
    detail::add({"", "VeteranLootBoxUI", detail::kVeteranAvailable, 0},
        detail::replacement(&detail::hook_veteran_available),
        detail::original_slot(&detail::g_veteran_available), false, &count);
    detail::add({"", "VeteranLootBoxUI", "OnEnable", 0},
        detail::replacement(&detail::hook_veteran_enable),
        detail::original_slot(&detail::g_veteran_enable), false, &count);

    LOGI("23.1.3-anomaly: comparison trace armed %d/11 "
         "(account-offline, PUN-offline, OfflineMod verdict, account UI, "
         "Veteran chest states); passive stock passthrough", count);
    return core;
}
} // namespace anomaly_trace_2313
