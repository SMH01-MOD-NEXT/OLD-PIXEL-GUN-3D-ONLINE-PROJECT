#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 authoritative local player identity
//
// This build does not pass plaintext preference names to Unity PlayerPrefs.
// 丒丁专与丏丈丙丈世 (the CryptoPlayerPrefs facade) hashes/encrypts keys first.
// The old PlayerPrefs hook therefore never saw "main_player_id" and could not
// initialize a wiped/spoofed device. This module now operates at the facade,
// where the stock key is still plaintext, and uses the backend store as the
// single persistent source of truth.
// -----------------------------------------------------------------------------

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "backend_emu_store.h"
#include "hook.h"
#include "il2cpp.h"
#include "log.h"

namespace identity_2313 {
namespace detail {

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

constexpr const char* kCryptoPrefsClass = u8"丒丁专与丏丈丙丈世";
constexpr const char* kHasKey = u8"七丌丁丝丏丝丐丑丘";      // bool(string)
constexpr const char* kSetString = u8"丘丞丝丂三丌专丑丒";   // void(string,string)
constexpr const char* kGetString = u8"下丁丏且丕与下丑丗";   // string(string,string)
constexpr const char* kPlayerIdKey = "main_player_id";

using HasKeyFn = bool (*)(void* key, void* method);
using SetStringFn = void (*)(void* key, void* value, void* method);
using GetStringFn = void* (*)(void* key, void* fallback, void* method);

inline HasKeyFn g_orig_has_key = nullptr;
inline SetStringFn g_orig_set_string = nullptr;
inline GetStringFn g_orig_get_string = nullptr;
inline void* g_has_key_info = nullptr;
inline void* g_set_string_info = nullptr;
inline void* g_get_string_info = nullptr;
inline bool g_ready = false;
inline bool g_installed = false;
inline bool g_seeding = false;
inline uint64_t g_reads = 0u;
inline uint64_t g_blocked_writes = 0u;

inline bool managed_equals(void* managed, const char* ascii) {
    if (managed == nullptr || ascii == nullptr ||
        il2cpp::string_length == nullptr || il2cpp::string_chars == nullptr) {
        return false;
    }
    const int32_t length = il2cpp::string_length(managed);
    const size_t expected = std::strlen(ascii);
    if (length < 0 || static_cast<size_t>(length) != expected) return false;
    const uint16_t* chars = il2cpp::string_chars(managed);
    if (chars == nullptr) return false;
    for (size_t i = 0u; i < expected; ++i) {
        if (chars[i] != static_cast<uint16_t>(ascii[i])) return false;
    }
    return true;
}

inline void* managed_local_id() {
    return il2cpp::string_new != nullptr
               ? il2cpp::string_new(backend_emu_store::player_id())
               : nullptr;
}

inline bool seed_stock_identity();

inline bool has_key_hook(void* key, void* method) {
    if (managed_equals(key, kPlayerIdKey)) {
        if (!g_ready && !g_seeding) (void)seed_stock_identity();
        return true;
    }
    return g_orig_has_key != nullptr ? g_orig_has_key(key, method) : false;
}

inline void* get_string_hook(void* key, void* fallback, void* method) {
    if (managed_equals(key, kPlayerIdKey)) {
        if (!g_ready && !g_seeding) (void)seed_stock_identity();
        void* value = managed_local_id();
        if (value != nullptr) {
            ++g_reads;
            if (g_reads <= 4u || (g_reads % 64u) == 0u) {
                LOGI("23.1.3-identity: served authoritative player id %s "
                     "through CryptoPlayerPrefs (read #%" PRIu64 ")",
                     backend_emu_store::player_id(), g_reads);
            }
            return value;
        }
    }
    return g_orig_get_string != nullptr
               ? g_orig_get_string(key, fallback, method)
               : fallback;
}

inline void set_string_hook(void* key, void* value, void* method) {
    if (managed_equals(key, kPlayerIdKey)) {
        void* authoritative = managed_local_id();
        if (authoritative == nullptr) return;
        if (!managed_equals(value, backend_emu_store::player_id())) {
            ++g_blocked_writes;
            LOGW("23.1.3-identity: replaced a foreign/empty player-id write "
                 "with authoritative id %s (write #%" PRIu64 ")",
                 backend_emu_store::player_id(), g_blocked_writes);
        }
        if (g_orig_set_string != nullptr) {
            g_orig_set_string(key, authoritative, method);
            g_ready = true;
        }
        return;
    }
    if (g_orig_set_string != nullptr) g_orig_set_string(key, value, method);
}

inline bool seed_stock_identity() {
    if (g_ready) return true;
    if (g_seeding) return false;
    if (g_orig_set_string == nullptr || g_orig_get_string == nullptr ||
        g_set_string_info == nullptr || g_get_string_info == nullptr ||
        il2cpp::string_new == nullptr) {
        return false;
    }
    g_seeding = true;
    void* key = il2cpp::string_new(kPlayerIdKey);
    void* value = managed_local_id();
    void* empty = il2cpp::string_new("");
    if (key == nullptr || value == nullptr || empty == nullptr) {
        g_seeding = false;
        return false;
    }

    // Call the unhooked stock facade so it performs its own hashing,
    // encryption, cache update and PlayerPrefs persistence.
    g_orig_set_string(key, value, g_set_string_info);
    void* read_back = g_orig_get_string(key, empty, g_get_string_info);
    const std::string verified = il2cpp::to_utf8(read_back, 32u);
    if (verified != backend_emu_store::player_id()) {
        LOGE("23.1.3-identity: CryptoPlayerPrefs verification failed "
             "(expected '%s', read '%s')", backend_emu_store::player_id(),
             verified.c_str());
        g_seeding = false;
        return false;
    }
    g_ready = true;
    g_seeding = false;
    LOGI("23.1.3-identity: authoritative local id %s was written and verified "
         "through the stock encrypted identity store",
         backend_emu_store::player_id());
    return true;
}

inline bool install() {
    if (g_installed) return true;

    g_has_key_info =
        il2cpp::find_method_info("", kCryptoPrefsClass, kHasKey, 1);
    g_set_string_info =
        il2cpp::find_method_info("", kCryptoPrefsClass, kSetString, 2);
    g_get_string_info =
        il2cpp::find_method_info("", kCryptoPrefsClass, kGetString, 2);
    if (g_has_key_info == nullptr || g_set_string_info == nullptr ||
        g_get_string_info == nullptr) {
        LOGE("23.1.3-identity: CryptoPlayerPrefs facade metadata is incomplete");
        return false;
    }

    bool armed = hook::install(
        {"", kCryptoPrefsClass, kHasKey, 1},
        reinterpret_cast<void*>(&has_key_hook),
        reinterpret_cast<void**>(&g_orig_has_key), true);
    armed = hook::install(
                {"", kCryptoPrefsClass, kSetString, 2},
                reinterpret_cast<void*>(&set_string_hook),
                reinterpret_cast<void**>(&g_orig_set_string), true) && armed;
    armed = hook::install(
                {"", kCryptoPrefsClass, kGetString, 2},
                reinterpret_cast<void*>(&get_string_hook),
                reinterpret_cast<void**>(&g_orig_get_string), true) && armed;
    if (!armed) {
        LOGE("23.1.3-identity: authoritative encrypted identity hooks failed");
        return false;
    }

    g_installed = true;
    LOGI("23.1.3-identity: encrypted identity bridge armed before auth; the "
         "authoritative id will be persisted on the first stock identity access");
    return true;
}

}  // namespace detail

inline bool install_hooks() { return detail::install(); }

}  // namespace identity_2313
