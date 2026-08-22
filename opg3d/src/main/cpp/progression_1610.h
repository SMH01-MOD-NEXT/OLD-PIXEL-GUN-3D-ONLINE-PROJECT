#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include "backend_local_1610.h"
#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Narrow local progression bridge for the supplied obfuscated PG3D 16.1.x
// ARMv7 build. This ports only the 14.1.1 currency, level and tutorial work:
// no weapon/catalogue ownership, lobby craft, timer or shop-price overrides.
//
// Every state change goes through the game's own 16.1.0 controllers:
//   - BankController AddCoins/AddGems -> Progress.Currency model/save;
//   - ExperienceController.AddExperience -> stock rewards and persistence;
//   - TrainingController stage setter + Storager shop-tutorial key.
// The obfuscated targets and their RVAs are documented in
// docs/PORT_16_1_1_PROGRESSION.md.
namespace progression_1610 {
namespace detail {

using MethodInfo = void;
using ManagedString = void;

using UpdateFn = void (*)(void* self, const MethodInfo* method);
using StaticVoidFn = void (*)(void* static_context,
                              const MethodInfo* method);
using StaticBoolFn = bool (*)(void* static_context,
                              const MethodInfo* method);
using StaticIntFn = int32_t (*)(void* static_context,
                                const MethodInfo* method);
using AddExperienceFn = void (*)(void* self, int32_t increment,
                                 const MethodInfo* method);
using ProgressUiFn = void (*)(void* self, void* progress,
                              const MethodInfo* method);
using AddCurrencyFn = void (*)(void* static_context, int32_t count,
                               bool need_indication, int32_t accrual_type,
                               const MethodInfo* method);
using WalletGetFn = int32_t (*)(void* self, ManagedString* key,
                                const MethodInfo* method);
using SetTrainingStageFn = void (*)(void* static_context, int32_t stage,
                                    const MethodInfo* method);
using StoragerSetIntFn = void (*)(void* static_context, ManagedString* key,
                                 int32_t value, bool direct_write,
                                 bool direct_write_v2,
                                 const MethodInfo* method);
using IntInstanceFn = int32_t (*)(void* self, const MethodInfo* method);
using BoolInstanceFn = bool (*)(void* self, const MethodInfo* method);

inline constexpr const char* kCurrentLevelMethod =
    u8"丝丞与东丏丂下丏丄";                 // RVA 0x02A15A98
inline constexpr const char* kAddExperienceMethod =
    u8"丅东丟丌七丙丝七丁";                 // RVA 0x02A1D47C
inline constexpr const char* kProgressUiMethod =
    u8"专东丒丁下不世世丂";                 // RVA 0x02A191D0
inline constexpr const char* kAddCoinsMethod =
    u8"丒与丒业不丆丆丐业";                 // RVA 0x02BE22E8
inline constexpr const char* kAddGemsMethod =
    u8"与丙万不丂丌丗业万";                 // RVA 0x02BE2520
inline constexpr const char* kWalletClass =
    u8"丁丌专丕且一丏与丌";                 // Progress.Currency model
inline constexpr const char* kWalletGetMethod =
    u8"不丂万丌丘丘世丛丛";                 // RVA 0x00DBBF4C
inline constexpr const char* kTrainingCompletedMethod =
    u8"丄丕东丆丌丞丒丁丄";                 // RVA 0x02D14A98
inline constexpr const char* kSetTrainingStageMethod =
    u8"丁丘丈丛世丅丟丘丙";                 // RVA 0x02D14FD0
inline constexpr const char* kStoragerClass =
    u8"丌丑丌丒丝万丏丘丄";
inline constexpr const char* kStoragerSetIntMethod =
    u8"七丕专丂丒丅丛丛丏";                 // RVA 0x0095EECC
inline constexpr const char* kMaxExpLevelsField =
    u8"与丗丟丑丈丈上且世";                 // static +0x20
inline constexpr const char* kHealthByLevelField =
    u8"三且业丗上丄丙丕丑";                 // static +0x28

// Local abuse verdict and wipe entry points. They are a dependency of the
// 999,999,999 wallet grant, not a general gameplay feature.
inline constexpr const char* kAbuseMethodGetter =
    u8"一东与丕且丆丕丆丒";                 // RVA 0x028E14E8
inline constexpr const char* kBannerShowAndClear =
    u8"与丛丏且丟丈丂三丒";                 // RVA 0x02A78158
inline constexpr const char* kBannerClearAll =
    u8"丘且业丘丑下丅丟丅";                 // RVA 0x02A781F4

inline constexpr int32_t kCurrencyTarget = 999999999;
inline constexpr int32_t kLevelCap = 45;
inline constexpr int32_t kFallbackExpGrant = 9999999;
inline constexpr int32_t kMaxExpGrant = 0x3FFFFFFF;
inline constexpr int32_t kFirstMatchCompleted = 3;
inline constexpr int32_t kThresholdCeiling = INT32_MAX;
inline constexpr uint32_t kLevelIntervalFrames = 5;
inline constexpr uint32_t kCurrencyIntervalFrames = 120;
inline constexpr const char* kShopTutorialKey =
    "shop_tutorial_state_passed_VER_12_1";

inline UpdateFn g_menu_update = nullptr;
inline StaticIntFn g_get_level = nullptr;
inline const MethodInfo* g_mi_get_level = nullptr;
inline AddExperienceFn g_add_experience = nullptr;
inline const MethodInfo* g_mi_add_experience = nullptr;
inline ProgressUiFn g_progress_ui = nullptr;
inline AddCurrencyFn g_add_coins = nullptr;
inline const MethodInfo* g_mi_add_coins = nullptr;
inline AddCurrencyFn g_add_gems = nullptr;
inline const MethodInfo* g_mi_add_gems = nullptr;
inline WalletGetFn g_wallet_get = nullptr;
inline const MethodInfo* g_mi_wallet_get = nullptr;
inline StaticBoolFn g_training_completed = nullptr;
inline SetTrainingStageFn g_set_training_stage = nullptr;
inline const MethodInfo* g_mi_set_training_stage = nullptr;
inline StoragerSetIntFn g_storager_set_int = nullptr;
inline const MethodInfo* g_mi_storager_set_int = nullptr;
inline void* g_shared_controller_field = nullptr;
inline void* g_max_exp_levels_field = nullptr;
inline void* g_health_by_level_field = nullptr;

// Original slots for the local save shield. They are retained for trampoline
// ownership, but the punishment/verdict bodies are intentionally not called.
inline StaticIntFn g_abuse_method = nullptr;
inline BoolInstanceFn g_signature_check = nullptr;
inline IntInstanceFn g_coin_threshold = nullptr;
inline IntInstanceFn g_gem_threshold = nullptr;
inline StaticVoidFn g_banner_show = nullptr;
inline StaticVoidFn g_banner_clear = nullptr;
inline UpdateFn g_banner_awake = nullptr;
inline UpdateFn g_banner_update = nullptr;

struct CurrencyState {
    const char* label;
    void* wallet = nullptr;
    ManagedString* key = nullptr;
    bool key_logged = false;
    bool unchanged_logged = false;
    uint32_t failed_probes = 0;
};

inline CurrencyState g_coins{"coins"};
inline CurrencyState g_gems{"gems"};
inline uint32_t g_frames = 0;
inline bool g_level_done = false;
inline int32_t g_last_level = -1;
inline uint32_t g_level_stalls = 0;
inline std::atomic<bool> g_tutorial_persisted{false};
inline std::atomic<bool> g_ui_suppression_logged{false};
inline std::atomic<bool> g_abuse_logged{false};
inline std::atomic<bool> g_signature_logged{false};
inline std::atomic<bool> g_threshold_logged{false};
inline std::atomic<bool> g_banner_logged{false};
inline thread_local CurrencyState* g_capture_currency = nullptr;
inline thread_local bool g_suppress_level_ui = false;
inline thread_local bool g_persisting_tutorial = false;

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
    void* pointer = info != nullptr ? il2cpp::method_pointer(info) : nullptr;
    if (info == nullptr || pointer == nullptr) {
        LOGE("16.1.x-progression: cannot resolve %s.%s/%d", target.klass,
             target.method, target.args_count);
        return false;
    }
    *out_fn = pointer;
    *out_mi = info;
    return true;
}

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
    std::memcpy(&length, static_cast<const char*>(array) + 0x0C,
                sizeof(length));
    return length;
}

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
    if (cap <= 0) return false;
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

int32_t hook_wallet_get(void* self, ManagedString* key,
                        const MethodInfo* method) {
    const int32_t value = g_wallet_get(self, key, method);
    CurrencyState* capture = g_capture_currency;
    if (capture != nullptr && capture->wallet == nullptr && self != nullptr &&
        key != nullptr) {
        capture->wallet = self;
        capture->key = key;
    }
    return value;
}

void hook_progress_ui(void* self, void* progress, const MethodInfo* method) {
    if (g_suppress_level_ui) {
        if (!g_ui_suppression_logged.exchange(true,
                                               std::memory_order_relaxed)) {
            LOGI("16.1.x-progression: synthetic level-up UI suppressed; stock "
                 "rewards and save remain active");
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
        LOGI("16.1.x-progression: player level complete (%d/%d)", before,
             cap);
        return;
    }

    void* controller = read_static_field(g_shared_controller_field);
    if (controller == nullptr) return;

    g_suppress_level_ui = true;
    g_add_experience(controller, grant, g_mi_add_experience);
    g_suppress_level_ui = false;

    const int32_t after = current_level();
    if (after > before) {
        g_level_stalls = 0;
        if (after == cap || after % 5 == 0 || g_last_level < 0) {
            LOGI("16.1.x-progression: level %d -> %d (target=%d, grant=%d, "
                 "table=%s)", before, after, cap, grant,
                 have_table ? "read" : "fallback");
        }
        g_last_level = after;
        if (after >= cap) {
            g_level_done = true;
            LOGI("16.1.x-progression: final level %d reached and saved", after);
        }
    } else {
        ++g_level_stalls;
        if (g_level_stalls == 1 || g_level_stalls % 120 == 0) {
            LOGW("16.1.x-progression: AddExperience did not advance level %d "
                 "(stall=%u); retrying through the stock controller", before,
                 g_level_stalls);
        }
    }
}

bool discover_currency(CurrencyState* state, AddCurrencyFn add,
                       const MethodInfo* add_mi) {
    if (state->wallet != nullptr && state->key != nullptr) return true;

    state->wallet = nullptr;
    state->key = nullptr;
    g_capture_currency = state;
    add(nullptr, 0, false, 0, add_mi);
    g_capture_currency = nullptr;

    if (state->wallet == nullptr || state->key == nullptr) {
        ++state->failed_probes;
        if (state->failed_probes == 1 || state->failed_probes % 30 == 0) {
            LOGW("16.1.x-progression: %s wallet probe not ready (attempt=%u); "
                 "no guessed storage key will be used", state->label,
                 state->failed_probes);
        }
        return false;
    }
    state->failed_probes = 0;
    if (!state->key_logged) {
        state->key_logged = true;
        const std::string key_name = il2cpp::to_utf8(state->key, 80);
        LOGI("16.1.x-progression: discovered canonical %s wallet key '%s' "
             "through BankController", state->label, key_name.c_str());
    }
    return true;
}

int32_t read_currency(const CurrencyState& state) {
    return g_wallet_get(state.wallet, state.key, g_mi_wallet_get);
}

void grant_currency(CurrencyState* state, AddCurrencyFn add,
                    const MethodInfo* add_mi) {
    if (!discover_currency(state, add, add_mi)) return;

    const int32_t before = read_currency(*state);
    if (before >= kCurrencyTarget) return;

    int64_t delta64 = static_cast<int64_t>(kCurrencyTarget) - before;
    if (delta64 <= 0) return;
    if (delta64 > INT32_MAX) delta64 = INT32_MAX;
    const int32_t delta = static_cast<int32_t>(delta64);

    add(nullptr, delta, false, 0, add_mi);
    const int32_t after = read_currency(*state);
    if (after <= before) {
        if (!state->unchanged_logged) {
            state->unchanged_logged = true;
            LOGE("16.1.x-progression: %s did not move after stock "
                 "BankController(+%d): %d -> %d", state->label, delta, before,
                 after);
        }
        return;
    }

    state->unchanged_logged = false;
    LOGI("16.1.x-progression: %s %d -> %d through BankController "
         "(target=%d)", state->label, before, after, kCurrencyTarget);
}

bool hook_training_completed(void* static_context,
                             const MethodInfo* method) {
    if (g_persisting_tutorial) return true;

    const bool was_completed = g_training_completed(static_context, method);
    if (!g_tutorial_persisted.load(std::memory_order_acquire)) {
        g_persisting_tutorial = true;
        g_set_training_stage(nullptr, kFirstMatchCompleted,
                             g_mi_set_training_stage);
        ManagedString* shop_key = static_cast<ManagedString*>(
            il2cpp::string_new(kShopTutorialKey));
        if (shop_key != nullptr) {
            g_storager_set_int(nullptr, shop_key, 1, false, false,
                               g_mi_storager_set_int);
            g_tutorial_persisted.store(true, std::memory_order_release);
            LOGI("16.1.x-progression: tutorial %s; stage 3 and shop tutorial "
                 "completion saved", was_completed ? "already complete" :
                                                   "skipped automatically");
        } else {
            LOGE("16.1.x-progression: could not allocate the shop tutorial "
                 "key; completion will be retried");
        }
        g_persisting_tutorial = false;
    }
    return true;
}

// -------------------------------------------------------------------------
// Minimal local-save shield required before a very large wallet is granted.
// -------------------------------------------------------------------------

int32_t hook_abuse_method(void* static_context, const MethodInfo* method) {
    (void)static_context;
    (void)method;
    if (!g_abuse_logged.exchange(true, std::memory_order_relaxed)) {
        LOGW("16.1.x-progression: local AbuseMethod verdict forced to None; "
             "persisted save values are not modified");
    }
    return 0;
}

bool hook_signature_check(void* self, const MethodInfo* method) {
    (void)self;
    (void)method;
    if (!g_signature_logged.exchange(true, std::memory_order_relaxed)) {
        LOGW("16.1.x-progression: ads-config signature verdict disabled for "
             "the re-signed local build");
    }
    return false;
}

int32_t hook_coin_threshold(void* self, const MethodInfo* method) {
    (void)self;
    (void)method;
    if (!g_threshold_logged.exchange(true, std::memory_order_relaxed)) {
        LOGI("16.1.x-progression: coin/gem abuse thresholds raised to "
             "int.MaxValue before the local wallet grant");
    }
    return kThresholdCeiling;
}

int32_t hook_gem_threshold(void* self, const MethodInfo* method) {
    (void)self;
    (void)method;
    return kThresholdCeiling;
}

void log_banner_block_once() {
    if (!g_banner_logged.exchange(true, std::memory_order_relaxed)) {
        LOGW("16.1.x-progression: CHEAT DETECTED banner/wipe route blocked; "
             "PlayerPrefs and local progression are left intact");
    }
}

void hook_banner_static(void* static_context, const MethodInfo* method) {
    (void)static_context;
    (void)method;
    log_banner_block_once();
}

void hook_banner_instance(void* self, const MethodInfo* method) {
    (void)self;
    (void)method;
    log_banner_block_once();
}

bool install_save_shield() {
    bool ok = hook::install(
        {"", "Switcher", kAbuseMethodGetter, 0},
        replacement(&hook_abuse_method), original_slot(&g_abuse_method), true);
    ok &= hook::install(
        {"Rilisoft", "CheaterConfigMemento", "get_CheckSignatureTampering", 0},
        replacement(&hook_signature_check),
        original_slot(&g_signature_check), true);
    ok &= hook::install(
        {"Rilisoft", "CheaterConfigMemento", "get_CoinThreshold", 0},
        replacement(&hook_coin_threshold), original_slot(&g_coin_threshold),
        true);
    ok &= hook::install(
        {"Rilisoft", "CheaterConfigMemento", "get_GemThreshold", 0},
        replacement(&hook_gem_threshold), original_slot(&g_gem_threshold),
        true);
    ok &= hook::install(
        {"", "CheatDetectedBanner", kBannerShowAndClear, 0},
        replacement(&hook_banner_static), original_slot(&g_banner_show), true);
    ok &= hook::install(
        {"", "CheatDetectedBanner", kBannerClearAll, 0},
        replacement(&hook_banner_static), original_slot(&g_banner_clear), true);
    ok &= hook::install(
        {"", "CheatDetectedBanner", "Awake", 0},
        replacement(&hook_banner_instance), original_slot(&g_banner_awake),
        true);
    ok &= hook::install(
        {"", "CheatDetectedBanner", "Update", 0},
        replacement(&hook_banner_instance), original_slot(&g_banner_update),
        true);
    if (!ok) {
        LOGE("16.1.x-progression: save shield incomplete; currency and level "
             "grants are fail-closed");
        return false;
    }
    LOGI("16.1.x-progression: local save shield armed (verdict=None, "
         "thresholds=int.MaxValue, banner/wipe entry points refused)");
    return true;
}

void maybe_grant() {
    if (!backend_local_1610::runtime_ready()) return;

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

} // namespace detail

inline bool install_hooks() {
#if defined(__ANDROID__)
    static_assert(sizeof(void*) == 4,
                  "PG3D 16.1.x progression target must be armeabi-v7a");
#endif

    // No grant may execute unless the local abuse/wipe route is neutralised.
    if (!detail::install_save_shield()) return false;

    bool resolved = detail::resolve_call(
        {"", "ExperienceController", detail::kCurrentLevelMethod, 0},
        reinterpret_cast<void**>(&detail::g_get_level),
        &detail::g_mi_get_level);
    resolved &= detail::resolve_call(
        {"", "ExperienceController", detail::kAddExperienceMethod, 1},
        reinterpret_cast<void**>(&detail::g_add_experience),
        &detail::g_mi_add_experience);
    resolved &= detail::resolve_call(
        {"", "BankController", detail::kAddCoinsMethod, 3},
        reinterpret_cast<void**>(&detail::g_add_coins),
        &detail::g_mi_add_coins);
    resolved &= detail::resolve_call(
        {"", "BankController", detail::kAddGemsMethod, 3},
        reinterpret_cast<void**>(&detail::g_add_gems),
        &detail::g_mi_add_gems);
    resolved &= detail::resolve_call(
        {"Progress", detail::kWalletClass, detail::kWalletGetMethod, 1},
        reinterpret_cast<void**>(&detail::g_wallet_get),
        &detail::g_mi_wallet_get);
    resolved &= detail::resolve_call(
        {"", "TrainingController", detail::kSetTrainingStageMethod, 1},
        reinterpret_cast<void**>(&detail::g_set_training_stage),
        &detail::g_mi_set_training_stage);
    resolved &= detail::resolve_call(
        {"", detail::kStoragerClass, detail::kStoragerSetIntMethod, 4},
        reinterpret_cast<void**>(&detail::g_storager_set_int),
        &detail::g_mi_storager_set_int);

    detail::g_shared_controller_field =
        il2cpp::find_field("", "ExperienceController", "sharedController");
    detail::g_max_exp_levels_field = il2cpp::find_field(
        "", "ExperienceController", detail::kMaxExpLevelsField);
    detail::g_health_by_level_field = il2cpp::find_field(
        "", "ExperienceController", detail::kHealthByLevelField);
    if (detail::g_shared_controller_field == nullptr) {
        LOGE("16.1.x-progression: ExperienceController.sharedController "
             "field unavailable");
        resolved = false;
    }
    if (detail::g_max_exp_levels_field == nullptr) {
        LOGW("16.1.x-progression: max-exp table unavailable; level grant will "
             "use the safe fallback and cap %d", detail::kLevelCap);
    }
    if (!resolved) {
        LOGE("16.1.x-progression: required controller metadata incomplete; "
             "grant disabled");
        return false;
    }

    const bool wallet_capture = hook::install(
        {"Progress", detail::kWalletClass, detail::kWalletGetMethod, 1},
        detail::replacement(&detail::hook_wallet_get),
        detail::original_slot(&detail::g_wallet_get), true);
    const bool level_ui = hook::install(
        {"", "ExpController", detail::kProgressUiMethod, 1},
        detail::replacement(&detail::hook_progress_ui),
        detail::original_slot(&detail::g_progress_ui), true);
    const bool tutorial = hook::install(
        {"", "TrainingController", detail::kTrainingCompletedMethod, 0},
        detail::replacement(&detail::hook_training_completed),
        detail::original_slot(&detail::g_training_completed), true);
    if (!wallet_capture || !level_ui || !tutorial) {
        LOGE("16.1.x-progression: transaction hooks incomplete "
             "(wallet=%d level-ui=%d tutorial=%d); grant disabled",
             wallet_capture ? 1 : 0, level_ui ? 1 : 0,
             tutorial ? 1 : 0);
        return false;
    }

    const bool trigger = hook::install(
        {"", "MainMenuController", "Update", 0},
        detail::replacement(&detail::hook_menu_update),
        detail::original_slot(&detail::g_menu_update), true);
    if (!trigger) {
        LOGE("16.1.x-progression: MainMenuController.Update trigger missing; "
             "grant disabled");
        return false;
    }

    LOGI("16.1.x-progression: armed (coins/gems=%d, level=%d, "
         "training=skipped, stock save/rewards, crafts/catalogue=untouched)",
         detail::kCurrencyTarget, detail::kLevelCap);
    return true;
}

} // namespace progression_1610
