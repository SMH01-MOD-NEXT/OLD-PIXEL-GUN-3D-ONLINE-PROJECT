#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) local player identity
//
// Goal: the account id is minted on the device, once, without ever asking a
// backend for one. A private deployment must not depend on the retired
// official account service, and an id handed out by that service would tie
// every local profile to it.
//
// Where the id lives
// ------------------
// The stock build persists the account id under the PlayerPrefs key
// "main_player_id" (the literal sits in metadata next to "id_player", the name
// of the HTTP parameter the old backend received it with). The registration
// round-trip only happens while that key has no usable value, so seeding the
// key before the auth flow reads it removes the round-trip instead of patching
// it out after the fact.
//
// Why PlayerPrefs and not the obfuscated storage wrapper
// ------------------------------------------------------
// Every managed name in this build is obfuscated, the storage wrapper included,
// but that wrapper ultimately reads and writes UnityEngine.PlayerPrefs, whose
// names are stable. Hooking the PlayerPrefs entry points therefore covers the
// id no matter which obfuscated wrapper asks for it, and it needs no RVA at
// all.
//
// What the hooks do
// -----------------
//   * GetString(key) and GetString(key, fallback) return the local id for the
//     id key, minting and persisting it on the very first access;
//   * HasKey(key) reports the id key as present, so no caller can conclude the
//     account still has to be registered;
//   * SetString(key, value) refuses any write that carries a different id, so
//     even if a backend answer did arrive it could not take the identity over.
//
// The id is minted once and reused forever: it is also stored under this port's
// own marker key, and later launches adopt the marker. A new id is generated
// only when the player wipes the game data.
//
// Everything is deliberately lazy. The mint happens inside the first hooked
// call, which always runs on a game thread, so this module never issues a
// PlayerPrefs write from the port's own init thread.
// -----------------------------------------------------------------------------

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include <fcntl.h>
#include <unistd.h>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

namespace identity_2313 {
namespace detail {

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

// ----------------------------------------------------------- metadata names

constexpr const char* kUnityNs = "UnityEngine";
constexpr const char* kPrefsClass = "PlayerPrefs";

// The stock persistence key of the account id.
constexpr const char* kPlayerIdKey = "main_player_id";

// This port's own marker: proves the stored id was minted locally, and is what
// later launches adopt.
constexpr const char* kMarkerKey = "opg3d_local_player_id";

// ------------------------------------------------------------------ tunables

// Exactly nine digits, never leading zero: 100000000 .. 999999999.
constexpr size_t kIdDigits = 9u;
constexpr uint32_t kIdFirst = 100000000u;
constexpr uint32_t kIdSpan = 900000000u;

// Log the first few interceptions in full, then every kLogPeriod-th one.
constexpr uint64_t kLogBurst = 4u;
constexpr uint64_t kLogPeriod = 64u;

// Unmatched keys that look id-related are reported this many times, so the
// logs prove which key the build really uses without ever flooding logcat.
constexpr uint64_t kProbeLogLimit = 12u;

// ------------------------------------------------------------- managed ABI
//
// Generated managed methods take their explicit arguments followed by
// MethodInfo*; all four hooked methods are static, so there is no `this`.

using GetString1Fn = void* (*)(void* key, void* method);
using GetString2Fn = void* (*)(void* key, void* fallback, void* method);
using SetStringFn = void (*)(void* key, void* value, void* method);
using HasKeyFn = bool (*)(void* key, void* method);
using SaveFn = void (*)(void* method);

// Trampolines to the unhooked bodies. Our own reads and writes go through
// these, so they never re-enter the hooks below.
inline GetString1Fn g_orig_get_string_1 = nullptr;
inline GetString2Fn g_orig_get_string_2 = nullptr;
inline SetStringFn g_orig_set_string = nullptr;
inline HasKeyFn g_orig_has_key = nullptr;

// MethodInfo* is metadata, not code, so it stays valid across hooking and can
// be resolved once up front for the calls this module makes itself.
inline void* g_get_string_1_info = nullptr;
inline void* g_set_string_info = nullptr;
inline void* g_save_info = nullptr;
inline SaveFn g_save = nullptr;

// ------------------------------------------------------------------- state

inline char g_id[16] = {};
inline bool g_ready = false;
inline bool g_adopted = false;
inline bool g_installed = false;
inline bool g_first_call_logged = false;

inline uint64_t g_reads = 0u;
inline uint64_t g_blocked = 0u;
inline uint64_t g_probe_logs = 0u;

inline bool should_log(uint64_t counter) {
    return counter <= kLogBurst || (counter % kLogPeriod) == 0u;
}

// -------------------------------------------------------- managed strings

inline bool managed_equals(void* managed, const char* ascii) {
    if (managed == nullptr || ascii == nullptr) return false;
    if (il2cpp::string_length == nullptr || il2cpp::string_chars == nullptr) {
        return false;
    }
    const int32_t length = il2cpp::string_length(managed);
    if (length < 0 || static_cast<size_t>(length) != std::strlen(ascii)) {
        return false;
    }
    const uint16_t* chars = il2cpp::string_chars(managed);
    if (chars == nullptr) return false;
    for (int32_t i = 0; i < length; ++i) {
        if (chars[i] != static_cast<uint16_t>(ascii[i])) return false;
    }
    return true;
}

// The storage wrapper may prefix the key, so a suffix match is accepted too.
inline bool managed_ends_with(void* managed, const char* ascii) {
    if (managed == nullptr || ascii == nullptr) return false;
    if (il2cpp::string_length == nullptr || il2cpp::string_chars == nullptr) {
        return false;
    }
    const int32_t length = il2cpp::string_length(managed);
    const size_t needle = std::strlen(ascii);
    if (length < 0 || static_cast<size_t>(length) < needle) return false;
    const uint16_t* chars = il2cpp::string_chars(managed);
    if (chars == nullptr) return false;
    const size_t start = static_cast<size_t>(length) - needle;
    for (size_t i = 0u; i < needle; ++i) {
        if (chars[start + i] != static_cast<uint16_t>(ascii[i])) return false;
    }
    return true;
}

// Case-insensitive substring probe used only by the diagnostics below.
// `lowercase_needle` must be ASCII and already lowercase.
inline bool managed_contains_ci(void* managed, const char* lowercase_needle) {
    if (managed == nullptr || lowercase_needle == nullptr) return false;
    if (il2cpp::string_length == nullptr || il2cpp::string_chars == nullptr) {
        return false;
    }
    const int32_t length = il2cpp::string_length(managed);
    const size_t needle = std::strlen(lowercase_needle);
    if (length < 0 || needle == 0u ||
        static_cast<size_t>(length) < needle) {
        return false;
    }
    const uint16_t* chars = il2cpp::string_chars(managed);
    if (chars == nullptr) return false;

    const size_t limit = static_cast<size_t>(length) - needle;
    for (size_t offset = 0u; offset <= limit; ++offset) {
        bool match = true;
        for (size_t i = 0u; i < needle && match; ++i) {
            uint16_t unit = chars[offset + i];
            if (unit >= 'A' && unit <= 'Z') unit = static_cast<uint16_t>(unit + 32);
            match = unit == static_cast<uint16_t>(lowercase_needle[i]);
        }
        if (match) return true;
    }
    return false;
}

inline bool is_player_id_key(void* key) {
    return managed_equals(key, kPlayerIdKey) ||
           managed_ends_with(key, kPlayerIdKey);
}

// ---------------------------------------------------------------- minting

inline uint64_t random_seed() {
    uint64_t value = 0u;
    const int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        const ssize_t got = read(fd, &value, sizeof(value));
        close(fd);
        if (got == static_cast<ssize_t>(sizeof(value)) && value != 0u) {
            return value;
        }
    }
    // /dev/urandom is always there on Android; this is a belt-and-braces path.
    struct timespec now {};
    clock_gettime(CLOCK_REALTIME, &now);
    value = static_cast<uint64_t>(now.tv_sec) * 1000000000ull +
            static_cast<uint64_t>(now.tv_nsec);
    value ^= static_cast<uint64_t>(getpid()) << 32;
    return value != 0u ? value : 0x9E3779B97F4A7C15ull;
}

// splitmix64 finalizer: whitens the seed so consecutive launches on the same
// device cannot produce neighbouring ids.
inline void mint_id(char* out, size_t capacity) {
    uint64_t z = random_seed() + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z = z ^ (z >> 31);
    const uint32_t id = kIdFirst + static_cast<uint32_t>(z % kIdSpan);
    std::snprintf(out, capacity, "%" PRIu32, id);
}

inline bool is_local_id(const char* text) {
    if (text == nullptr) return false;
    if (std::strlen(text) != kIdDigits) return false;
    if (text[0] < '1' || text[0] > '9') return false;
    for (size_t i = 1u; i < kIdDigits; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
    }
    return true;
}

// Returns the local id, minting and persisting it on first use. Always called
// from a hooked PlayerPrefs entry point, i.e. from a game thread.
inline const char* local_id() {
    if (g_ready) return g_id;
    if (g_orig_get_string_1 == nullptr || g_orig_set_string == nullptr ||
        il2cpp::string_new == nullptr) {
        return nullptr;
    }

    void* marker_key = il2cpp::string_new(kMarkerKey);
    if (marker_key == nullptr) return nullptr;

    // Adopt the id a previous launch of this port already minted.
    void* stored = g_orig_get_string_1(marker_key, g_get_string_1_info);
    const std::string previous = il2cpp::to_utf8(stored, 32u);
    if (is_local_id(previous.c_str())) {
        std::snprintf(g_id, sizeof(g_id), "%s", previous.c_str());
        g_adopted = true;
    } else {
        mint_id(g_id, sizeof(g_id));
        g_adopted = false;
    }

    void* value = il2cpp::string_new(g_id);
    void* game_key = il2cpp::string_new(kPlayerIdKey);
    if (value == nullptr || game_key == nullptr) {
        LOGE("23.1.3-identity: could not allocate the managed id strings");
        return nullptr;
    }

    // Write through the unhooked bodies: the marker for our own bookkeeping,
    // the stock key so that nothing in the game considers the account new.
    g_orig_set_string(marker_key, value, g_set_string_info);
    g_orig_set_string(game_key, value, g_set_string_info);
    if (g_save != nullptr) g_save(g_save_info);

    g_ready = true;
    LOGI("23.1.3-identity: %s local player id %s (%zu digits, minted on device,"
         " no backend round-trip)",
         g_adopted ? "reusing" : "generated", g_id, kIdDigits);
    return g_id;
}

// --------------------------------------------------------------- diagnostics
//
// If this build ever asked for the id under a different key, the port would
// silently do nothing. These few log lines make that visible instead.
inline void note_probe(void* key) {
    if (g_probe_logs >= kProbeLogLimit) return;
    if (!managed_contains_ci(key, "player_id") &&
        !managed_contains_ci(key, "playerid") &&
        !managed_contains_ci(key, "user_id") &&
        !managed_contains_ci(key, "userid")) {
        return;
    }
    ++g_probe_logs;
    LOGI("23.1.3-identity: id-like key '%s' was read but is not the account id"
         " key ('%s'); no substitution was made",
         il2cpp::to_utf8(key, 64u).c_str(), kPlayerIdKey);
}

inline void note_first_call(void* key) {
    if (g_first_call_logged) return;
    g_first_call_logged = true;
    LOGI("23.1.3-identity: PlayerPrefs bridge is live (first key read: '%s')",
         il2cpp::to_utf8(key, 64u).c_str());
}

// -------------------------------------------------------------------- hooks

inline void* get_string_1_hook(void* key, void* method) {
    note_first_call(key);
    if (is_player_id_key(key)) {
        const char* id = local_id();
        if (id != nullptr && il2cpp::string_new != nullptr) {
            void* managed = il2cpp::string_new(id);
            if (managed != nullptr) {
                ++g_reads;
                if (should_log(g_reads)) {
                    LOGI("23.1.3-identity: served local player id %s"
                         " (read #%" PRIu64 ")", id, g_reads);
                }
                return managed;
            }
        }
    } else {
        note_probe(key);
    }
    return g_orig_get_string_1 != nullptr
               ? g_orig_get_string_1(key, method)
               : nullptr;
}

inline void* get_string_2_hook(void* key, void* fallback, void* method) {
    note_first_call(key);
    if (is_player_id_key(key)) {
        const char* id = local_id();
        if (id != nullptr && il2cpp::string_new != nullptr) {
            void* managed = il2cpp::string_new(id);
            if (managed != nullptr) {
                ++g_reads;
                if (should_log(g_reads)) {
                    LOGI("23.1.3-identity: served local player id %s"
                         " (read #%" PRIu64 ", defaulted call)", id, g_reads);
                }
                return managed;
            }
        }
    }
    return g_orig_get_string_2 != nullptr
               ? g_orig_get_string_2(key, fallback, method)
               : nullptr;
}

inline bool has_key_hook(void* key, void* method) {
    if (is_player_id_key(key) && local_id() != nullptr) return true;
    return g_orig_has_key != nullptr ? g_orig_has_key(key, method) : false;
}

inline void set_string_hook(void* key, void* value, void* method) {
    if (is_player_id_key(key)) {
        const char* id = local_id();
        if (id != nullptr && !managed_equals(value, id)) {
            ++g_blocked;
            if (should_log(g_blocked)) {
                LOGW("23.1.3-identity: refused a foreign player id write ('%s');"
                     " keeping the local id %s",
                     il2cpp::to_utf8(value, 48u).c_str(), id);
            }
            return;
        }
    }
    if (g_orig_set_string != nullptr) g_orig_set_string(key, value, method);
}

// ------------------------------------------------------------- installation

inline bool install() {
    if (g_installed) return true;

    g_get_string_1_info =
        il2cpp::find_method_info(kUnityNs, kPrefsClass, "GetString", 1);
    g_set_string_info =
        il2cpp::find_method_info(kUnityNs, kPrefsClass, "SetString", 2);
    if (g_get_string_1_info == nullptr || g_set_string_info == nullptr) {
        LOGE("23.1.3-identity: UnityEngine.PlayerPrefs accessors are missing"
             " from metadata; the local id was not armed");
        return false;
    }

    g_save_info = il2cpp::find_method_info(kUnityNs, kPrefsClass, "Save", 0);
    if (g_save_info != nullptr) {
        g_save = reinterpret_cast<SaveFn>(il2cpp::method_pointer(g_save_info));
    }
    if (g_save == nullptr) {
        LOGW("23.1.3-identity: PlayerPrefs.Save() is unavailable; the id will"
             " be flushed by the game's own next save");
    }

    // The read and the write path are both required: one serves the id, the
    // other keeps a backend answer from overwriting it.
    bool armed = hook::install(
        {kUnityNs, kPrefsClass, "GetString", 1},
        reinterpret_cast<void*>(&get_string_1_hook),
        reinterpret_cast<void**>(&g_orig_get_string_1), true);
    armed = hook::install({kUnityNs, kPrefsClass, "SetString", 2},
                          reinterpret_cast<void*>(&set_string_hook),
                          reinterpret_cast<void**>(&g_orig_set_string),
                          true) && armed;
    if (!armed) {
        LOGE("23.1.3-identity: the PlayerPrefs id path could not be hooked;"
             " nothing was armed");
        return false;
    }

    // Both remaining entry points only widen coverage, so a miss is a warning.
    if (!hook::install({kUnityNs, kPrefsClass, "GetString", 2},
                       reinterpret_cast<void*>(&get_string_2_hook),
                       reinterpret_cast<void**>(&g_orig_get_string_2))) {
        LOGW("23.1.3-identity: PlayerPrefs.GetString(key, default) was not"
             " hooked; defaulted id reads stay stock");
    }
    if (!hook::install({kUnityNs, kPrefsClass, "HasKey", 1},
                       reinterpret_cast<void*>(&has_key_hook),
                       reinterpret_cast<void**>(&g_orig_has_key))) {
        LOGW("23.1.3-identity: PlayerPrefs.HasKey() was not hooked; presence"
             " checks stay stock");
    }

    g_installed = true;
    LOGI("23.1.3-identity: armed: '%s' is served from a device-minted %zu-digit"
         " id and foreign writes to it are refused",
         kPlayerIdKey, kIdDigits);
    return true;
}

}  // namespace detail

// Arms the local identity. Must run after the IL2CPP metadata is available.
inline bool install_hooks() { return detail::install(); }

}  // namespace identity_2313
