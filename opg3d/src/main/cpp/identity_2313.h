#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 authoritative local player identity
//
// The plaintext account key first enters Rilisoft's storage facade and may
// then flow through the lower CryptoPlayerPrefs facade. Hook both layers: the
// high-level facade is the stable game-facing identity surface, while the
// lower bridge keeps direct callers coherent and lets the stock code perform
// its own hashing/encryption/persistence.
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

constexpr const char* kStorageNamespace = "Rilisoft";
constexpr const char* kStorageClass = u8"丐不专丛丄一丕丌丆";
constexpr const char* kCryptoPrefsClass = u8"丒丁专与丏丈丙丈世";
constexpr const char* kHasKey = u8"七丌丁丝丏丝丐丑丘";
constexpr const char* kSetString = u8"丘丞丝丂三丌专丑丒";
constexpr const char* kGetString = u8"下丁丏且丕与下丑丗";
constexpr const char* kPlayerIdKey = "main_player_id";

using HasKeyFn = bool (*)(void* key, void* method);
using SetStringFn = void (*)(void* key, void* value, void* method);
using GetString1Fn = void* (*)(void* key, void* method);
using GetString2Fn = void* (*)(void* key, void* fallback, void* method);

inline HasKeyFn g_storage_has_key = nullptr;
inline SetStringFn g_storage_set_string = nullptr;
inline GetString1Fn g_storage_get_string = nullptr;
inline void* g_storage_set_info = nullptr;
inline void* g_storage_get_info = nullptr;

inline HasKeyFn g_crypto_has_key = nullptr;
inline SetStringFn g_crypto_set_string = nullptr;
inline GetString2Fn g_crypto_get_string = nullptr;
inline void* g_crypto_set_info = nullptr;
inline void* g_crypto_get_info = nullptr;

inline bool g_installed = false;
inline bool g_seeding = false;
inline bool g_seeded = false;
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

inline void note_read(const char* layer) {
    ++g_reads;
    if (g_reads <= 4u || (g_reads % 64u) == 0u) {
        LOGI("23.1.3-identity: served authoritative player id %s through %s "
             "(read #%" PRIu64 ")", backend_emu_store::player_id(), layer,
             g_reads);
    }
}

inline void note_replaced_write(void* value, const char* layer) {
    if (managed_equals(value, backend_emu_store::player_id())) return;
    ++g_blocked_writes;
    LOGW("23.1.3-identity: replaced a foreign/empty %s player-id write with "
         "authoritative id %s (write #%" PRIu64 ")", layer,
         backend_emu_store::player_id(), g_blocked_writes);
}

// Persist through the real game-facing facade on its first access. This runs
// on the Unity thread, after the storage singleton and crypto settings exist.
inline void seed_storage_identity() {
    if (g_seeded || g_seeding || g_storage_set_string == nullptr ||
        g_storage_get_string == nullptr || g_storage_set_info == nullptr ||
        g_storage_get_info == nullptr || il2cpp::string_new == nullptr) {
        return;
    }
    g_seeding = true;
    void* key = il2cpp::string_new(kPlayerIdKey);
    void* value = managed_local_id();
    if (key != nullptr && value != nullptr) {
        g_storage_set_string(key, value, g_storage_set_info);
        void* read_back = g_storage_get_string(key, g_storage_get_info);
        const std::string verified = il2cpp::to_utf8(read_back, 32u);
        if (verified == backend_emu_store::player_id()) {
            g_seeded = true;
            LOGI("23.1.3-identity: authoritative local id %s was written and "
                 "verified through the Rilisoft identity store",
                 backend_emu_store::player_id());
        } else {
            LOGE("23.1.3-identity: Rilisoft identity verification failed "
                 "(expected '%s', read '%s')", backend_emu_store::player_id(),
                 verified.c_str());
        }
    }
    g_seeding = false;
}

inline bool storage_has_key_hook(void* key, void* method) {
    if (managed_equals(key, kPlayerIdKey)) {
        seed_storage_identity();
        return true;
    }
    return g_storage_has_key != nullptr ? g_storage_has_key(key, method) : false;
}

inline void* storage_get_string_hook(void* key, void* method) {
    if (managed_equals(key, kPlayerIdKey)) {
        seed_storage_identity();
        void* value = managed_local_id();
        if (value != nullptr) {
            note_read("Rilisoft storage");
            return value;
        }
    }
    return g_storage_get_string != nullptr ? g_storage_get_string(key, method)
                                           : nullptr;
}

inline void storage_set_string_hook(void* key, void* value, void* method) {
    if (managed_equals(key, kPlayerIdKey)) {
        void* authoritative = managed_local_id();
        if (authoritative == nullptr) return;
        note_replaced_write(value, "Rilisoft");
        if (g_storage_set_string != nullptr) {
            g_storage_set_string(key, authoritative, method);
            g_seeded = true;
        }
        return;
    }
    if (g_storage_set_string != nullptr) g_storage_set_string(key, value, method);
}

inline bool crypto_has_key_hook(void* key, void* method) {
    if (managed_equals(key, kPlayerIdKey)) return true;
    return g_crypto_has_key != nullptr ? g_crypto_has_key(key, method) : false;
}

inline void* crypto_get_string_hook(void* key, void* fallback, void* method) {
    if (managed_equals(key, kPlayerIdKey)) {
        void* value = managed_local_id();
        if (value != nullptr) {
            note_read("CryptoPlayerPrefs");
            return value;
        }
    }
    return g_crypto_get_string != nullptr
               ? g_crypto_get_string(key, fallback, method)
               : fallback;
}

inline void crypto_set_string_hook(void* key, void* value, void* method) {
    if (managed_equals(key, kPlayerIdKey)) {
        void* authoritative = managed_local_id();
        if (authoritative == nullptr) return;
        note_replaced_write(value, "CryptoPlayerPrefs");
        if (g_crypto_set_string != nullptr) {
            g_crypto_set_string(key, authoritative, method);
        }
        return;
    }
    if (g_crypto_set_string != nullptr) g_crypto_set_string(key, value, method);
}

inline bool install() {
    if (g_installed) return true;

    g_storage_set_info = il2cpp::find_method_info(
        kStorageNamespace, kStorageClass, kSetString, 2);
    g_storage_get_info = il2cpp::find_method_info(
        kStorageNamespace, kStorageClass, kGetString, 1);
    g_crypto_set_info =
        il2cpp::find_method_info("", kCryptoPrefsClass, kSetString, 2);
    g_crypto_get_info =
        il2cpp::find_method_info("", kCryptoPrefsClass, kGetString, 2);
    if (g_storage_set_info == nullptr || g_storage_get_info == nullptr ||
        g_crypto_set_info == nullptr || g_crypto_get_info == nullptr) {
        LOGE("23.1.3-identity: identity facade metadata is incomplete");
        return false;
    }

    bool armed = hook::install(
        {"", kCryptoPrefsClass, kHasKey, 1},
        reinterpret_cast<void*>(&crypto_has_key_hook),
        reinterpret_cast<void**>(&g_crypto_has_key), true);
    armed = hook::install(
                {"", kCryptoPrefsClass, kSetString, 2},
                reinterpret_cast<void*>(&crypto_set_string_hook),
                reinterpret_cast<void**>(&g_crypto_set_string), true) && armed;
    armed = hook::install(
                {"", kCryptoPrefsClass, kGetString, 2},
                reinterpret_cast<void*>(&crypto_get_string_hook),
                reinterpret_cast<void**>(&g_crypto_get_string), true) && armed;
    armed = hook::install(
                {kStorageNamespace, kStorageClass, kHasKey, 1},
                reinterpret_cast<void*>(&storage_has_key_hook),
                reinterpret_cast<void**>(&g_storage_has_key), true) && armed;
    armed = hook::install(
                {kStorageNamespace, kStorageClass, kSetString, 2},
                reinterpret_cast<void*>(&storage_set_string_hook),
                reinterpret_cast<void**>(&g_storage_set_string), true) && armed;
    armed = hook::install(
                {kStorageNamespace, kStorageClass, kGetString, 1},
                reinterpret_cast<void*>(&storage_get_string_hook),
                reinterpret_cast<void**>(&g_storage_get_string), true) && armed;
    if (!armed) {
        LOGE("23.1.3-identity: authoritative identity hooks failed");
        return false;
    }

    g_installed = true;
    LOGI("23.1.3-identity: Rilisoft + CryptoPlayerPrefs identity bridges armed "
         "before auth; the backend-store id is authoritative");
    return true;
}

}  // namespace detail

inline bool install_hooks() { return detail::install(); }

}  // namespace identity_2313
