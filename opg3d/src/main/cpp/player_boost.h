#pragma once

#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <cstring>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Release progression boost for PG3D 13.2.1.
//
// The project runs entirely on its own Photon Cloud application: the official
// backend is dead, there is no economy to protect and no server-side
// anti-cheat. For the public release every client gets the final player level
// and a pinned 999 999 999 balance for both soft currencies (Coins and Gems),
// so all weapons/items are playable out of the box on equal terms.
//
// Both pieces are read-side hooks over the game's own storage accessors:
// - Storager.getInt("Coins"/"Gems") always returns kCurrencyValue;
// - Storager.setInt("Coins"/"Gems", ...) is clamped to kCurrencyValue, so
//   spending or earning never moves the displayed balance and any cached
//   readers converge back to the pinned value after the next write;
// - the three ExperienceController level getters return the real level cap,
//   resolved at runtime from MaxExpLevelsDefault.Length (with a documented
//   fallback), not a hardcoded magic number.
//
// Nothing touches saved profiles directly beyond the pinned currency writes,
// and every pin is proven in logcat with a one-time OPG3D line.
namespace player_boost {
namespace detail {

using MethodInfo = void;
using ManagedString = void;

// dump1321.cs:
//   Storager.getInt(string, bool, bool, bool)  RVA 0xEB6CD0
//   Storager.setInt(string, int, bool, bool)   RVA 0xEB74A0
//   ExperienceController.get_PlayerLevel()     RVA 0x14E65C8 (static)
//   ExperienceController.GetCurrentLevel()     RVA 0x14E788C (static)
//   ExperienceController.get_currentLevel()    RVA 0x14E0C88 (instance)
using StoragerGetIntFn = int32_t (*)(ManagedString* key, bool suppress_warnings,
                                     bool direct_read, bool direct_read_v2,
                                     const MethodInfo* method);
using StoragerSetIntFn = void (*)(ManagedString* key, int32_t value,
                                  bool direct_write, bool direct_write_v2,
                                  const MethodInfo* method);
using GetStaticLevelFn = int32_t (*)(const MethodInfo* method);
using GetInstanceLevelFn = int32_t (*)(void* self, const MethodInfo* method);

inline StoragerGetIntFn g_storager_get_int = nullptr;
inline StoragerSetIntFn g_storager_set_int = nullptr;
inline GetStaticLevelFn g_get_player_level = nullptr;
inline GetStaticLevelFn g_get_current_level_static = nullptr;
inline GetInstanceLevelFn g_get_current_level = nullptr;

inline constexpr int32_t kCurrencyValue = 999999999;
inline constexpr int32_t kFallbackMaxLevel = 55; // известный level cap PG3D 13.x

inline std::atomic<int32_t> g_max_level{0};
inline std::atomic<bool> g_coins_logged{false};
inline std::atomic<bool> g_gems_logged{false};

bool key_equals(ManagedString* key, const char* ascii, int32_t length) {
    if (key == nullptr || il2cpp::string_length == nullptr ||
        il2cpp::string_chars == nullptr) {
        return false;
    }
    if (il2cpp::string_length(key) != length) return false;
    const uint16_t* chars = il2cpp::string_chars(key);
    if (chars == nullptr) return false;
    for (int32_t i = 0; i < length; ++i) {
        if (chars[i] != static_cast<uint16_t>(ascii[i])) return false;
    }
    return true;
}

// Storager keys для мягкой валюты в PG3D 12.5.0/13.2.1 — ровно "Coins" и
// "Gems" (подтверждены строками в global-metadata и годами моддинга этой
// ветки). Сравнение идёт по UTF-16 chars без аллокаций: getInt вызывается
// часто, поэтому сначала дешёво отсекаем по длине.
bool currency_key_name(ManagedString* key, const char** name) {
    if (key_equals(key, "Coins", 5)) {
        *name = "Coins";
        return true;
    }
    if (key_equals(key, "Gems", 4)) {
        *name = "Gems";
        return true;
    }
    return false;
}

bool should_log_pin(const char* name) {
    std::atomic<bool>& flag = (name[1] == 'o') ? g_coins_logged : g_gems_logged;
    return !flag.exchange(true);
}

// Il2CppArray layout в этом рантайме (metadata v22, ARM32): klass 0x0,
// monitor 0x4, bounds 0x8 (NULL для SZArray), max_length 0xC.
int32_t read_szarray_length(void* array) {
    if (array == nullptr) return -1;
    int32_t length = -1;
    std::memcpy(&length, static_cast<const char*>(array) + 0xC, sizeof(length));
    return length;
}

int32_t read_static_array_length(void* klass, const char* field_name) {
    if (klass == nullptr || il2cpp::class_get_field_from_name == nullptr ||
        il2cpp::field_static_get_value == nullptr) {
        return -1;
    }
    void* field = il2cpp::class_get_field_from_name(klass, field_name);
    if (field == nullptr) return -1;
    void* array = nullptr;
    il2cpp::field_static_get_value(field, &array);
    return read_szarray_length(array);
}

// Вызывается только из хуков методов ExperienceController, а IL2CPP
// гарантированно инициализирует класс (и его static fields) до первого вызова
// любого его метода — поэтому чтение static-массивов здесь безопасно.
int32_t max_level() {
    const int32_t cached = g_max_level.load(std::memory_order_acquire);
    if (cached > 0) return cached;

    int32_t exp_len = -1;
    int32_t health_len = -1;
    void* klass = il2cpp::find_class != nullptr
                      ? il2cpp::find_class("", "ExperienceController")
                      : nullptr;
    if (klass != nullptr) {
        exp_len = read_static_array_length(klass, "MaxExpLevelsDefault");
        health_len = read_static_array_length(klass, "HealthByLevel");
    }

    int32_t resolved = exp_len > 0 ? exp_len : health_len;
    if (resolved <= 0) {
        resolved = kFallbackMaxLevel;
        LOGW("boost: MaxExpLevelsDefault/HealthByLevel unreadable; "
             "falling back to known 13.x cap %d", resolved);
    } else if (health_len > 0 && health_len != exp_len) {
        LOGW("boost: MaxExpLevelsDefault.Length=%d differs from "
             "HealthByLevel.Length=%d; using %d", exp_len, health_len, resolved);
    }
    g_max_level.store(resolved, std::memory_order_release);
    LOGI("boost: player level pinned to cap %d "
         "(MaxExpLevelsDefault.Length=%d, HealthByLevel.Length=%d)",
         resolved, exp_len, health_len);
    return resolved;
}

int32_t hook_storager_get_int(ManagedString* key, bool suppress_warnings,
                              bool direct_read, bool direct_read_v2,
                              const MethodInfo* method) {
    const char* name = nullptr;
    if (currency_key_name(key, &name)) {
        if (should_log_pin(name)) {
            LOGI("boost: Storager.getInt(\"%s\") pinned to %d",
                 name, kCurrencyValue);
        }
        return kCurrencyValue;
    }
    return g_storager_get_int(key, suppress_warnings, direct_read,
                              direct_read_v2, method);
}

void hook_storager_set_int(ManagedString* key, int32_t value, bool direct_write,
                           bool direct_write_v2, const MethodInfo* method) {
    const char* name = nullptr;
    if (currency_key_name(key, &name)) {
        if (should_log_pin(name)) {
            LOGI("boost: Storager.setInt(\"%s\", %d) clamped to %d",
                 name, value, kCurrencyValue);
        }
        value = kCurrencyValue;
    }
    g_storager_set_int(key, value, direct_write, direct_write_v2, method);
}

int32_t hook_get_player_level(const MethodInfo* method) {
    (void)g_get_player_level;
    (void)method;
    return max_level();
}

int32_t hook_get_current_level_static(const MethodInfo* method) {
    (void)g_get_current_level_static;
    (void)method;
    return max_level();
}

int32_t hook_get_current_level(void* self, const MethodInfo* method) {
    (void)g_get_current_level;
    (void)self;
    (void)method;
    return max_level();
}

template <typename Fn>
void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

inline bool install_optional(const hook::ManagedMethod& target, void* replace,
                             void** original, int* installed) {
    if (hook::install(target, replace, original, false)) {
        ++(*installed);
        return true;
    }
    return false;
}

} // namespace detail

inline bool install_hooks() {
    int installed = 0;

    // Валюта — обязательная часть релизной фичи: без getInt-пина баланс не
    // будет бесконечным, поэтому fail-closed с явным логом.
    const bool currency_read = hook::install(
        {"", "Storager", "getInt", 4},
        detail::replacement(&detail::hook_storager_get_int),
        detail::original_slot(&detail::g_storager_get_int), true);
    if (currency_read) ++installed;
    detail::install_optional(
        {"", "Storager", "setInt", 4},
        detail::replacement(&detail::hook_storager_set_int),
        detail::original_slot(&detail::g_storager_set_int), &installed);

    const bool level = hook::install(
        {"", "ExperienceController", "get_PlayerLevel", 0},
        detail::replacement(&detail::hook_get_player_level),
        detail::original_slot(&detail::g_get_player_level), true);
    if (level) ++installed;
    detail::install_optional(
        {"", "ExperienceController", "GetCurrentLevel", 0},
        detail::replacement(&detail::hook_get_current_level_static),
        detail::original_slot(&detail::g_get_current_level_static), &installed);
    detail::install_optional(
        {"", "ExperienceController", "get_currentLevel", 0},
        detail::replacement(&detail::hook_get_current_level),
        detail::original_slot(&detail::g_get_current_level), &installed);

    LOGI("boost: installed %d hooks (currency=%s, level=%s; cap=%d, currency=%d)",
         installed, currency_read ? "OK" : "FAILED", level ? "OK" : "FAILED",
         detail::kFallbackMaxLevel, detail::kCurrencyValue);
    return currency_read && level;
}

} // namespace player_boost
