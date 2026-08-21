#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "custom_hunger_guard.h"
#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// The obfuscated anti-abuse callers are redirected by cheat_guard.h to four
// recognisable keys: opg3d_inert_slot_a .. _d. A fixed replacement key is not
// inert by itself: AppsMenu, Initializer and MainMenuController call the same
// key builder again on the next launch/scene, so a timestamp written during
// the first match is visible during the second match.
//
// This module turns those four names into a real virtual sink at the narrowest
// shared boundary. Only for the four replacement keys:
//   Storager.hasKey   -> false
//   Storager.getString-> empty
//   Storager.setString-> no-op
// Every other Storager key and operation is forwarded unchanged. Existing
// stale sink entries are ignored, and no new sink entry is persisted.
namespace abuse_slot_sink {
namespace detail {

using MethodInfo = void;
using HasKeyFn = bool (*)(void* static_context, void* key,
                          const MethodInfo* method);
using GetStringFn = void* (*)(void* static_context, void* key,
                              const MethodInfo* method);
using SetStringFn = void (*)(void* static_context, void* key, void* value,
                             const MethodInfo* method);

inline HasKeyFn g_has_key = nullptr;
inline GetStringFn g_get_string = nullptr;
inline SetStringFn g_set_string = nullptr;

inline constexpr char kSinkPrefix[] = "opg3d_inert_slot_";
inline constexpr size_t kSinkPrefixLength = sizeof(kSinkPrefix) - 1u;
inline constexpr int32_t kSinkKeyLength =
    static_cast<int32_t>(kSinkPrefixLength + 1u);
inline constexpr uint32_t kMaxLoggedReads = 12u;
inline constexpr uint32_t kMaxLoggedWrites = 12u;

inline std::atomic<uint32_t> g_logged_reads{0u};
inline std::atomic<uint32_t> g_logged_writes{0u};

template <typename Fn>
void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

bool should_log(std::atomic<uint32_t>& counter, uint32_t budget) {
    return counter.fetch_add(1u, std::memory_order_relaxed) < budget;
}

// Compare the managed UTF-16 key without allocating or converting every
// ordinary Storager access. All four sink names have the same ASCII prefix and
// a one-character suffix.
bool is_sink_key(void* key, char* suffix_out = nullptr) {
    if (key == nullptr || il2cpp::string_length == nullptr ||
        il2cpp::string_chars == nullptr) {
        return false;
    }
    const int32_t length = il2cpp::string_length(key);
    if (length != kSinkKeyLength) return false;

    const uint16_t* chars = il2cpp::string_chars(key);
    if (chars == nullptr) return false;
    for (size_t i = 0; i < kSinkPrefixLength; ++i) {
        if (chars[i] != static_cast<uint16_t>(
                            static_cast<unsigned char>(kSinkPrefix[i]))) {
            return false;
        }
    }

    const uint16_t suffix = chars[kSinkPrefixLength];
    if (suffix < static_cast<uint16_t>('a') ||
        suffix > static_cast<uint16_t>('d')) {
        return false;
    }
    if (suffix_out != nullptr) *suffix_out = static_cast<char>(suffix);
    return true;
}

bool hook_has_key(void* static_context, void* key, const MethodInfo* method) {
    char suffix = '?';
    if (is_sink_key(key, &suffix)) {
        if (should_log(g_logged_reads, kMaxLoggedReads)) {
            LOGW("cheat-guard: virtual abuse slot 'opg3d_inert_slot_%c' "
                 "reported absent; a timestamp from an earlier launch or "
                 "match cannot arm the next one", suffix);
        }
        return false;
    }
    return g_has_key != nullptr ? g_has_key(static_context, key, method) : false;
}

void* hook_get_string(void* static_context, void* key,
                      const MethodInfo* method) {
    char suffix = '?';
    if (is_sink_key(key, &suffix)) {
        if (should_log(g_logged_reads, kMaxLoggedReads)) {
            LOGW("cheat-guard: read from virtual abuse slot "
                 "'opg3d_inert_slot_%c' returned empty", suffix);
        }
        return il2cpp::string_new != nullptr ? il2cpp::string_new("") : nullptr;
    }
    return g_get_string != nullptr
               ? g_get_string(static_context, key, method)
               : nullptr;
}

void hook_set_string(void* static_context, void* key, void* value,
                     const MethodInfo* method) {
    char suffix = '?';
    if (is_sink_key(key, &suffix)) {
        (void)value;
        if (should_log(g_logged_writes, kMaxLoggedWrites)) {
            LOGW("cheat-guard: discarded write to virtual abuse slot "
                 "'opg3d_inert_slot_%c'; the first match cannot seed the "
                 "second match", suffix);
        }
        return;
    }
    if (g_set_string != nullptr) {
        g_set_string(static_context, key, value, method);
    }
}

} // namespace detail

inline bool install_hooks() {
    if (il2cpp::string_length == nullptr || il2cpp::string_chars == nullptr) {
        LOGE("cheat-guard: managed string accessors are unavailable; the "
             "virtual abuse-slot sink cannot be installed");
        return false;
    }

    bool installed = hook::install(
        {"", "Storager", "hasKey", 1},
        detail::replacement(&detail::hook_has_key),
        detail::original_slot(&detail::g_has_key), true);
    installed &= hook::install(
        {"", "Storager", "getString", 1},
        detail::replacement(&detail::hook_get_string),
        detail::original_slot(&detail::g_get_string), true);
    installed &= hook::install(
        {"", "Storager", "setString", 2},
        detail::replacement(&detail::hook_set_string),
        detail::original_slot(&detail::g_set_string), true);

    if (!installed) {
        LOGE("cheat-guard: the virtual abuse-slot sink is incomplete; a "
             "fixed inert key could still persist from match one to match "
             "two");
    } else {
        LOGI("cheat-guard: virtual abuse-slot sink armed (hasKey=false, "
             "getString=empty, setString=no-op for opg3d_inert_slot_a..d; all "
             "other Storager keys are stock)");
    }

    // CustomHungerBase is a separate delayed punishment scene, not part of the
    // four redirected persistence slots. Install its two gates here so the
    // existing main-module success condition remains fail-closed without
    // coupling its implementation to the much larger CheatDetectedBanner
    // guard.
    const bool custom_hunger_installed = custom_hunger_guard::install_hooks();
    return installed && custom_hunger_installed;
}

} // namespace abuse_slot_sink
