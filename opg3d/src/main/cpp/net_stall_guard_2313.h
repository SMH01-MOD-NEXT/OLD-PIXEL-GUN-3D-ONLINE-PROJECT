#pragma once

#include <cinttypes>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Removes the blocking DNS lookup that causes the in-game micro-freezes.
//
// Evidence (recovered with tools/find_callers.py, which decodes every A64
// BL/B in libil2cpp.so, plus tools/resolve_rva.py):
//
//   PhotonHandler.<obfuscated>.MoveNext        (coroutine -> Unity game thread)
//     -> <wss-resolver>.<iterator>.MoveNext                  @ 0x44743EC
//       -> <wss-resolver>.<resolve>(string)                   @ 0x4473CCC
//         -> System.Net.Dns.GetHostAddresses                  @ 0x159FB44  (blocking)
//
// The declaring class holds the const string "wss://", i.e. it resolves the
// address of the retired WebSocket backend. Because the caller is a coroutine,
// the lookup runs on the Unity game thread, so every DNS timeout against the
// dead host is a dropped frame - worst during a match, where PhotonHandler is
// busiest.
//
// Rejected hypothesis, recorded so it is not re-investigated: synchronous .NET
// HTTP is NOT involved. HttpWebRequest.GetResponse (0x15ADB84) and
// WebRequest.GetResponse (0x1521FC8) both have zero call sites in this build.
//
// Strategy: memoize, never fabricate. The first lookup per host is delegated
// to the stock implementation and its outcome recorded verbatim, including the
// null case. Every later lookup for the same host replays that identical
// observed result without entering the resolver, so one unavoidable stall
// replaces an unbounded series of them.
//
// This deliberately does not disable name resolution: Photon Cloud is live in
// this port and must keep resolving normally. Only the repetition is removed.
namespace net_stall_guard_2313 {
namespace detail {

using MethodInfo = void;
using ManagedString = void;
// Dump signature: public static string <resolve>(string)
using ResolveFn = ManagedString* (*)(ManagedString*, const MethodInfo*);

inline constexpr const char* kResolverClass = u8"专一一万且丂上丄丈";
inline constexpr const char* kResolverMethod = u8"丆丐丕丕丈下丝业丈";

// A stall longer than one 60 fps frame is what the player actually perceives.
inline constexpr uint64_t kFrameBudgetMs = 16u;
// The game only ever resolves a handful of hosts; the bound stops this table
// from growing without limit if some caller ever passes arbitrary names.
inline constexpr size_t kMaxEntries = 32u;
inline constexpr size_t kMaxHostChars = 256u;
// Replays are the common path once warmed up; log them only occasionally.
inline constexpr uint32_t kReplayLogInterval = 30u;

struct Entry {
    std::string host;
    std::string value;
    bool had_result = false;
};

inline ResolveFn g_resolve = nullptr;
inline std::mutex g_mutex;
inline std::vector<Entry> g_cache;
inline uint32_t g_calls = 0u;
inline uint32_t g_replayed = 0u;
inline uint64_t g_stalled_ms = 0u;

ManagedString* hook_resolve(ManagedString* host, const MethodInfo* method) {
    if (g_resolve == nullptr) {
        LOGE("23.1.3-net-stall: resolver hook has no saved original");
        return host;
    }

    const std::string key = il2cpp::to_utf8(host, kMaxHostChars);

    bool cached = false;
    bool had_result = false;
    std::string value;
    uint32_t replayed = 0u;

    // Snapshot under the lock, then leave it. il2cpp_string_new can allocate
    // and trigger a GC, and holding a native lock across managed allocation is
    // how deadlocks get built.
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        ++g_calls;
        for (const Entry& entry : g_cache) {
            if (entry.host != key) continue;
            cached = true;
            had_result = entry.had_result;
            value = entry.value;
            replayed = ++g_replayed;
            break;
        }
    }

    if (cached) {
        if ((replayed % kReplayLogInterval) == 1u) {
            LOGI("23.1.3-net-stall: replayed memoized lookup of '%s' "
                 "(%" PRIu32 " blocking DNS calls avoided so far)",
                 key.c_str(), replayed);
        }
        if (!had_result) return nullptr;
        if (il2cpp::string_new == nullptr) {
            LOGE("23.1.3-net-stall: il2cpp_string_new unavailable; "
                 "delegating to the blocking resolver");
            return g_resolve(host, method);
        }
        return il2cpp::string_new(value.c_str());
    }

    const uint64_t started = opg3d_log::monotonic_ms();
    ManagedString* result = g_resolve(host, method);
    const uint64_t finished = opg3d_log::monotonic_ms();
    const uint64_t elapsed = finished >= started ? finished - started : 0u;

    const bool result_present = result != nullptr;
    const std::string resolved = il2cpp::to_utf8(result, kMaxHostChars);

    uint64_t total_stalled = 0u;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_stalled_ms += elapsed;
        total_stalled = g_stalled_ms;
        if (g_cache.size() < kMaxEntries) {
            g_cache.push_back(Entry{key, resolved, result_present});
        } else {
            LOGW("23.1.3-net-stall: memo table is full at %zu hosts; '%s' will "
                 "keep using the blocking resolver", kMaxEntries, key.c_str());
        }
    }

    if (elapsed >= kFrameBudgetMs) {
        LOGW("23.1.3-net-stall: blocking backend name lookup of '%s' froze the "
             "calling thread for %" PRIu64 " ms (result=%s, total stalled "
             "%" PRIu64 " ms); memoized, later lookups will not block",
             key.c_str(), elapsed,
             result_present ? resolved.c_str() : "<null>", total_stalled);
    } else {
        LOGI("23.1.3-net-stall: backend name lookup of '%s' took %" PRIu64
             " ms (result=%s); memoized",
             key.c_str(), elapsed,
             result_present ? resolved.c_str() : "<null>");
    }

    return result;
}

} // namespace detail

inline bool install_hooks() {
#if defined(__ANDROID__)
    static_assert(sizeof(void*) == 8,
                  "PG3D 23.1.3 target must be arm64-v8a");
#endif
    using namespace detail;
    const bool ok = hook::install(
        {"", kResolverClass, kResolverMethod, 1},
        reinterpret_cast<void*>(&hook_resolve),
        reinterpret_cast<void**>(&g_resolve), false);
    if (ok) {
        LOGI("23.1.3-net-stall: blocking backend name lookup is memoized; "
             "repeated in-match DNS stalls on the game thread are gone");
    } else {
        LOGW("23.1.3-net-stall: backend name resolver was not hooked; "
             "in-match micro-freezes from blocking DNS remain");
    }
    return ok;
}

} // namespace net_stall_guard_2313
