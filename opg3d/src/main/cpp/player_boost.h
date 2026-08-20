#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Persisted release progression grant for PG3D 13.2.1.
//
// The game deliberately consumes at most one level per AddExperience() call,
// regardless of how large the increment is. The grant therefore advances one
// level at a time until the stock level cap is reached. The ExpController UI
// listener is suppressed only while these synthetic level-up calls run, so no
// level-up panel/coroutine is created and the main menu remains responsive.
// Rewards, level calculation and persistence still run inside the original
// ExperienceController code.
//
// Currency is also written through the original BankController methods. This
// IL2CPP build uses the old ARM32 static-method ABI: r0 is a hidden null slot,
// while the first managed argument starts in r1. Omitting that slot makes
// AddCoins/AddGems receive count=0. The implementation below models that ABI
// explicitly and discovers the real currency keys by observing the getInt()
// call made by BankController itself; no guessed "Coins"/"Gems" keys are used.
namespace player_boost {
namespace detail {

using MethodInfo = void;
using ManagedString = void;

// Instance methods use: self, managed args..., MethodInfo*.
using UpdateFn = void (*)(void* self, const MethodInfo* method);
using AddExperienceFn = void (*)(void* self, int32_t increment,
                                 const MethodInfo* method);
using ProgressUiFn = void (*)(void* self, void* progress,
                              const MethodInfo* method);

// Static methods in this metadata-v22 ARM32 build use a hidden null r0 before
// their managed arguments. The final MethodInfo* follows all managed args.
using GetLevelFn = int32_t (*)(void* static_context,
                               const MethodInfo* method);
using AddCurrencyFn = void (*)(void* static_context, int32_t count,
                               bool need_indication, int32_t accrual_type,
                               const MethodInfo* method);
using StoragerGetIntFn = int32_t (*)(void* static_context, ManagedString* key,
                                     bool suppress_warnings, bool direct_read,
                                     bool direct_read_v2,
                                     const MethodInfo* method);

inline UpdateFn g_menu_update = nullptr;
inline ProgressUiFn g_progress_ui = nullptr;
inline GetLevelFn g_get_level = nullptr;
inline const MethodInfo* g_mi_get_level = nullptr;
inline AddExperienceFn g_add_experience = nullptr;
inline const MethodInfo* g_mi_add_experience = nullptr;
inline AddCurrencyFn g_add_coins = nullptr;
inline const MethodInfo* g_mi_add_coins = nullptr;
inline AddCurrencyFn g_add_gems = nullptr;
inline const MethodInfo* g_mi_add_gems = nullptr;
inline StoragerGetIntFn g_storager_get_int = nullptr;
inline const MethodInfo* g_mi_get_int = nullptr;
inline void* g_shared_controller_field = nullptr;
inline void* g_max_exp_levels_field = nullptr;
inline void* g_health_by_level_field = nullptr;

inline constexpr int32_t kCurrencyTarget = 999999999;

// 38 is the last real player level in this release. The experience table is
// indexed from level 0, so its length is 39 and must never be used as a level
// target: asking for 39 makes the grant loop forever on a level that cannot
// be reached. Every computed cap is clamped to this value.
inline constexpr int32_t kLevelCap = 38;
inline constexpr int32_t kFallbackExpGrant = 9999999;
inline constexpr int32_t kMaxExpGrant = 0x3FFFFFFF;
inline constexpr uint32_t kLevelIntervalFrames = 5;
inline constexpr uint32_t kCurrencyIntervalFrames = 120;

struct CurrencyState {
    const char* label;
    ManagedString* key = nullptr;
    bool discovery_failed = false;
    bool unchanged_logged = false;
};

inline CurrencyState g_coins{"coins"};
inline CurrencyState g_gems{"gems"};
inline uint32_t g_frames = 0;
inline bool g_level_done = false;
inline int32_t g_last_level = -1;
inline uint32_t g_level_stalls = 0;
inline bool g_ui_suppression_logged = false;

// Currency discovery and AddExperience callbacks are synchronous on the main
// thread. thread_local keeps unrelated Storager calls on worker threads from
// being mistaken for our probe.
inline thread_local CurrencyState* g_capture_currency = nullptr;
inline thread_local bool g_suppress_level_ui = false;

void* read_static_field(void* field) {
    if (field == nullptr || il2cpp::field_static_get_value == nullptr) {
        return nullptr;
    }
    void* value = nullptr;
    il2cpp::field_static_get_value(field, &value);
    return value;
}

int32_t read_array_length(void* array) {
    if (array == nullptr) return -1;
    int32_t length = -1;
    std::memcpy(&length, static_cast<const char*>(array) + 0xC,
                sizeof(length));
    return length;
}

// MaxExpLevelsDefault contains entries for levels 0..38, so a length of 39
// still means the final valid player level is 38. HealthByLevel is used as a
// safety cross-check because gameplay indexes it with the current level, and
// the result is finally clamped to kLevelCap.
bool exp_table_info(int32_t* out_cap, int32_t* out_grant) {
    void* exp_array = read_static_field(g_max_exp_levels_field);
    const int32_t exp_length = read_array_length(exp_array);
    const int32_t health_length =
        read_array_length(read_static_field(g_health_by_level_field));
    if (exp_length <= 1 || exp_length > 4096) return false;

    int32_t safe_length = exp_length;
    if (health_length > 1 && health_length < safe_length) {
        safe_length = health_length;
    }
    int32_t cap = safe_length - 1;
    if (cap > kLevelCap) cap = kLevelCap;
    *out_cap = cap;

    int64_t total = 0;
    const char* elements = static_cast<const char*>(exp_array) + 0x10;
    for (int32_t i = 0; i < exp_length; ++i) {
        int32_t threshold = 0;
        std::memcpy(&threshold, elements + static_cast<size_t>(i) * 4u,
                    sizeof(threshold));
        if (threshold > 0) total += threshold;
    }
    if (total <= 0) return false;
    *out_grant = total > kMaxExpGrant ? kMaxExpGrant
                                      : static_cast<int32_t>(total);
    return true;
}

int32_t hook_storager_get_int(void* static_context, ManagedString* key,
                              bool suppress_warnings, bool direct_read,
                              bool direct_read_v2,
                              const MethodInfo* method) {
    const int32_t value = g_storager_get_int(
        static_context, key, suppress_warnings, direct_read, direct_read_v2,
        method);
    if (g_capture_currency != nullptr &&
        g_capture_currency->key == nullptr && key != nullptr) {
        // BankController reads its canonical balance key before invoking
        // analytics; keep the first read so nested analytics reads cannot
        // overwrite the discovered currency key.
        g_capture_currency->key = key;
    }
    return value;
}

void hook_progress_ui(void* self, void* progress, const MethodInfo* method) {
    if (g_suppress_level_ui) {
        if (!g_ui_suppression_logged) {
            g_ui_suppression_logged = true;
            LOGI("boost: automatic level-up UI suppression active");
        }
        return;
    }
    g_progress_ui(self, progress, method);
}

int32_t current_level() {
    return g_get_level(nullptr, g_mi_get_level);
}

void grant_level() {
    if (g_level_done) return;

    int32_t cap = kLevelCap;
    int32_t grant = kFallbackExpGrant;
    const bool have_table = exp_table_info(&cap, &grant);
    if (cap > kLevelCap) cap = kLevelCap;
    const int32_t before = current_level();
    if (before >= cap) {
        g_level_done = true;
        LOGI("boost: player level complete (%d/%d)", before, cap);
        return;
    }

    void* controller = read_static_field(g_shared_controller_field);
    if (controller == nullptr) return;

    // ExperienceController performs the actual save, level reward and state
    // transition. Its ExpController subscriber is UI-only; suppressing that
    // subscriber prevents the modal/coroutine without skipping persistence.
    g_suppress_level_ui = true;
    g_add_experience(controller, grant, g_mi_add_experience);
    g_suppress_level_ui = false;

    const int32_t after = current_level();
    if (after > before) {
        g_level_stalls = 0;
        if (after == cap || after % 5 == 0 || g_last_level < 0) {
            LOGI("boost: player level %d -> %d (target %d, +%d exp per stock "
                 "one-level step, table %s)",
                 before, after, cap, grant, have_table ? "read" : "fallback");
        }
        g_last_level = after;
        if (after >= cap) {
            g_level_done = true;
            LOGI("boost: final player level reached and saved (%d)", after);
        }
    } else {
        ++g_level_stalls;
        if (g_level_stalls == 1 || g_level_stalls % 60 == 0) {
            LOGW("boost: AddExperience did not advance level %d; retrying "
                 "without an attempt limit (stall=%u)",
                 before, g_level_stalls);
        }
    }
}

bool discover_currency_key(CurrencyState* state, AddCurrencyFn add,
                           const MethodInfo* add_mi) {
    if (state->key != nullptr) return true;
    if (state->discovery_failed) return false;

    // AddCoins/AddGems(0) still executes their canonical Storager.getInt ->
    // setInt path. The getInt hook captures the exact static key used by this
    // build without changing the balance.
    g_capture_currency = state;
    add(nullptr, 0, false, 0, add_mi);
    g_capture_currency = nullptr;

    if (state->key == nullptr) {
        state->discovery_failed = true;
        LOGE("boost: %s key discovery failed; currency grant disabled",
             state->label);
        return false;
    }
    const std::string key_name = il2cpp::to_utf8(state->key, 80);
    LOGI("boost: discovered canonical %s key '%s' through BankController",
         state->label, key_name.c_str());
    return true;
}

int32_t read_currency(const CurrencyState& state) {
    return g_storager_get_int(nullptr, state.key, false, false, false,
                              g_mi_get_int);
}

void grant_currency(CurrencyState* state, AddCurrencyFn add,
                    const MethodInfo* add_mi) {
    if (!discover_currency_key(state, add, add_mi)) return;

    const int32_t before = read_currency(*state);
    if (before >= kCurrencyTarget) return;

    int64_t delta64 = static_cast<int64_t>(kCurrencyTarget) - before;
    if (delta64 <= 0) return;
    if (delta64 > INT32_MAX) delta64 = INT32_MAX;
    const int32_t delta = static_cast<int32_t>(delta64);

    // Correct old-IL2CPP static ABI: hidden null r0, count in r1, indication
    // in r2, accrual type in r3, MethodInfo on the stack.
    add(nullptr, delta, false, 0, add_mi);
    const int32_t after = read_currency(*state);

    if (after <= before) {
        if (!state->unchanged_logged) {
            state->unchanged_logged = true;
            LOGE("boost: %s did not move after BankController(+%d): %d -> %d",
                 state->label, delta, before, after);
        }
        return;
    }
    state->unchanged_logged = false;
    LOGI("boost: %s %d -> %d via BankController (target %d)", state->label,
         before, after, kCurrencyTarget);
}

void maybe_grant() {
    ++g_frames;
    if (!g_level_done && g_frames % kLevelIntervalFrames == 1) {
        grant_level();
    }
    if (g_frames % kCurrencyIntervalFrames == 1) {
        grant_currency(&g_coins, g_add_coins, g_mi_add_coins);
        grant_currency(&g_gems, g_add_gems, g_mi_add_gems);
    }
}

void hook_menu_update(void* self, const MethodInfo* method) {
    g_menu_update(self, method);
    maybe_grant();
}

template <typename Fn>
void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

bool resolve_call(const hook::ManagedMethod& target, void** out_fn,
                  const MethodInfo** out_mi) {
    void* info = il2cpp::find_method_info(target.namespaze, target.klass,
                                          target.method, target.args_count);
    void* pointer = il2cpp::method_pointer(info);
    if (info == nullptr || pointer == nullptr) {
        LOGE("boost: cannot resolve %s.%s/%d", target.klass, target.method,
             target.args_count);
        return false;
    }
    *out_fn = pointer;
    *out_mi = info;
    return true;
}

} // namespace detail

inline bool install_hooks() {
    bool ok = true;
    ok &= detail::resolve_call({"", "ExperienceController", "GetCurrentLevel", 0},
                               reinterpret_cast<void**>(&detail::g_get_level),
                               &detail::g_mi_get_level);
    ok &= detail::resolve_call({"", "ExperienceController", "AddExperience", 1},
                               reinterpret_cast<void**>(&detail::g_add_experience),
                               &detail::g_mi_add_experience);
    ok &= detail::resolve_call({"", "BankController", "AddCoins", 3},
                               reinterpret_cast<void**>(&detail::g_add_coins),
                               &detail::g_mi_add_coins);
    ok &= detail::resolve_call({"", "BankController", "AddGems", 3},
                               reinterpret_cast<void**>(&detail::g_add_gems),
                               &detail::g_mi_add_gems);
    ok &= detail::resolve_call({"", "Storager", "getInt", 4},
                               reinterpret_cast<void**>(&detail::g_storager_get_int),
                               &detail::g_mi_get_int);

    detail::g_shared_controller_field =
        il2cpp::find_field("", "ExperienceController", "sharedController");
    detail::g_max_exp_levels_field =
        il2cpp::find_field("", "ExperienceController", "MaxExpLevelsDefault");
    detail::g_health_by_level_field =
        il2cpp::find_field("", "ExperienceController", "HealthByLevel");
    if (detail::g_shared_controller_field == nullptr) {
        LOGE("boost: ExperienceController.sharedController field not found");
        ok = false;
    }
    if (detail::g_max_exp_levels_field == nullptr) {
        LOGW("boost: MaxExpLevelsDefault not found; level cap will use %d",
             detail::kLevelCap);
    }
    if (!ok) {
        LOGE("boost: progression targets incomplete; grant disabled");
        return false;
    }

    const bool currency_capture = hook::install(
        {"", "Storager", "getInt", 4},
        detail::replacement(&detail::hook_storager_get_int),
        detail::original_slot(&detail::g_storager_get_int), true);
    const bool level_ui = hook::install(
        {"", "ExpController", "ExperienceControllerOnPlayerProgressChanged", 1},
        detail::replacement(&detail::hook_progress_ui),
        detail::original_slot(&detail::g_progress_ui), true);
    if (!currency_capture || !level_ui) {
        LOGE("boost: currency-key capture or level-up UI suppression hook failed");
        return false;
    }

    static const hook::ManagedMethod kTriggers[] = {
        {"", "MainMenuController", "Update", 0},
        {"", "BankController", "Update", 0},
        {"", "ExpController", "Update", 0},
    };
    const char* chosen = nullptr;
    for (const auto& trigger : kTriggers) {
        if (hook::install(trigger, detail::replacement(&detail::hook_menu_update),
                          detail::original_slot(&detail::g_menu_update), false)) {
            chosen = trigger.klass;
            break;
        }
    }
    if (chosen == nullptr) {
        LOGE("boost: no menu Update hook target available; grant disabled");
        return false;
    }

    LOGI("boost: persisted grant armed (trigger=%s.Update, level target=%d, "
         "currency target=%d, level-up UI=skipped)",
         chosen, detail::kLevelCap, detail::kCurrencyTarget);
    return true;
}

} // namespace player_boost
