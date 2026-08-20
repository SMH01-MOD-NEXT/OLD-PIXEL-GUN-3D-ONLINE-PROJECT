#pragma once

#include <cstdint>
#include <cstring>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Release progression grant for PG3D 13.2.1.
//
// The project runs entirely on its own Photon Cloud application: the official
// backend is dead, there is no economy to protect and no server-side
// anti-cheat. For the public release every client gets the maximum player
// level and a 999 999 999 balance for both soft currencies (Coins and Gems),
// so all weapons/items are playable out of the box on equal terms.
//
// Instead of pinning reads (which never persists and desynced game state in
// the earlier revision), the mod calls the game's own write paths from the
// main menu:
//   - ExperienceController.AddExperience(...) levels the profile up through
//     the normal progression code, which computes the level itself and saves
//     it to Storager;
//   - BankController.AddCoins/AddGems(...) top the balance up to
//     kCurrencyTarget through the normal bank code, again persisted by the
//     game itself.
// Because the values are written by the game, they survive process restarts
// and library removal, and no gameplay code can observe an impossible level.
//
// The grant runs from a per-frame menu Update hook on the main thread,
// throttled to about one check per second. The level part converges after a
// single call (the game persists the new level immediately, so later
// launches skip it); the currency part re-tops the balance whenever it drops
// below the target, which is what makes it effectively infinite. A read-back
// after each top-up verifies the storage key actually moved; if it did not
// (currency storage layout changed), the mod marks its own Storager flag and
// never touches the balance again instead of piling up invisible grants.
namespace player_boost {
namespace detail {

using MethodInfo = void;
using ManagedString = void;

// dump1321.cs:
//   MainMenuController.Update()                       RVA 0xF7A584 (instance)
//   BankController.Update()                           RVA 0xCD5214 (instance, fallback)
//   ExpController.Update()                            RVA 0x14E38D4 (instance, fallback)
//   ExperienceController.GetCurrentLevel()            RVA 0x14E788C (static)
//   ExperienceController.AddExperience(int)           RVA 0x14E8B0C (instance)
//   ExperienceController.sharedController             static field 0x44
//   ExperienceController.MaxExpLevelsDefault : int[]  static field 0x20
//   BankController.AddCoins(int, bool, AccrualType)   RVA 0xCD84B8 (static)
//   BankController.AddGems(int, bool, AccrualType)    RVA 0xCD8634 (static)
//   Storager.getInt(string, bool, bool, bool)         RVA 0xEB6CD0
//   Storager.setInt(string, int, bool, bool)          RVA 0xEB74A0
using UpdateFn = void (*)(void* self, const MethodInfo* method);
using GetLevelFn = int32_t (*)(const MethodInfo* method);
using AddExperienceFn = void (*)(void* self, int32_t increment,
                                 const MethodInfo* method);
using AddCurrencyFn = void (*)(int32_t count, bool need_indication,
                               int32_t accrual_type, const MethodInfo* method);
using StoragerGetIntFn = int32_t (*)(ManagedString* key, bool suppress_warnings,
                                     bool direct_read, bool direct_read_v2,
                                     const MethodInfo* method);
using StoragerSetIntFn = void (*)(ManagedString* key, int32_t value,
                                  bool direct_write, bool direct_write_v2,
                                  const MethodInfo* method);

inline UpdateFn g_menu_update = nullptr;

inline GetLevelFn g_get_level = nullptr;
inline const MethodInfo* g_mi_get_level = nullptr;
inline AddExperienceFn g_add_experience = nullptr;
inline const MethodInfo* g_mi_add_experience = nullptr;
inline AddCurrencyFn g_add_coins = nullptr;
inline const MethodInfo* g_mi_add_coins = nullptr;
inline AddCurrencyFn g_add_gems = nullptr;
inline const MethodInfo* g_mi_add_gems = nullptr;
inline StoragerGetIntFn g_get_int = nullptr;
inline const MethodInfo* g_mi_get_int = nullptr;
inline StoragerSetIntFn g_set_int = nullptr;
inline const MethodInfo* g_mi_set_int = nullptr;
inline void* g_shared_controller_field = nullptr;
inline void* g_max_exp_levels_field = nullptr;

inline constexpr int32_t kCurrencyTarget = 999999999;
inline constexpr int32_t kFallbackLevelCap = 38;   // AddExperience() early-exits at 38 on this build
inline constexpr int32_t kFallbackExpGrant = 0x3FFFFFFF;
inline constexpr int32_t kMaxExpGrant = 0x3FFFFFFF;  // overflow guard for the exp-table sum
inline constexpr uint32_t kCheckIntervalFrames = 60;  // ~1 s between grant checks
inline constexpr int kMaxLevelAttempts = 3;
inline constexpr const char* kBrokenProbeFlag = "OPG3D_BoostBrokenProbe";

inline uint32_t g_frames = 0;
inline bool g_level_done = false;
inline int g_level_attempts = 0;
inline bool g_coins_probe_broken = false;
inline bool g_gems_probe_broken = false;

int32_t storager_get_int(const char* key) {
    if (il2cpp::string_new == nullptr) return 0;
    ManagedString* managed = il2cpp::string_new(key);
    if (managed == nullptr) return 0;
    return g_get_int(managed, false, false, false, g_mi_get_int);
}

void storager_set_int(const char* key, int32_t value) {
    if (il2cpp::string_new == nullptr) return;
    ManagedString* managed = il2cpp::string_new(key);
    if (managed == nullptr) return;
    g_set_int(managed, value, false, false, g_mi_set_int);
}

void* read_static_field(void* field) {
    void* value = nullptr;
    il2cpp::field_static_get_value(field, &value);
    return value;
}

// Il2CppArray layout in this runtime (metadata v22, ARM32): klass 0x0,
// monitor 0x4, bounds 0x8 (NULL for SZArray), max_length 0xC, elements 0x10.
// The exp table total is enough to reach the final level under either
// semantics (per-level costs or cumulative thresholds).
bool exp_table_info(int32_t* out_cap, int32_t* out_total_exp) {
    void* array = read_static_field(g_max_exp_levels_field);
    if (array == nullptr) return false;
    int32_t length = -1;
    std::memcpy(&length, static_cast<const char*>(array) + 0xC, sizeof(length));
    if (length <= 0 || length > 4096) return false;
    int64_t total = 0;
    const char* elements = static_cast<const char*>(array) + 0x10;
    for (int32_t i = 0; i < length; ++i) {
        int32_t threshold = 0;
        std::memcpy(&threshold, elements + static_cast<size_t>(i) * 4u,
                    sizeof(threshold));
        if (threshold > 0) total += threshold;
    }
    if (total <= 0) return false;
    *out_cap = length;
    *out_total_exp =
        total > kMaxExpGrant ? kMaxExpGrant : static_cast<int32_t>(total);
    return true;
}

void grant_level() {
    if (g_level_done || g_level_attempts >= kMaxLevelAttempts) return;

    const int32_t level = g_get_level(g_mi_get_level);
    int32_t cap = kFallbackLevelCap;
    int32_t grant = kFallbackExpGrant;
    const bool have_table = exp_table_info(&cap, &grant);
    if (level >= cap) {
        g_level_done = true;
        LOGI("boost: player level already at cap (%d >= %d); nothing to grant",
             level, cap);
        return;
    }

    void* controller = read_static_field(g_shared_controller_field);
    if (controller == nullptr) return;  // controller not up yet; retry later

    ++g_level_attempts;
    // AddExperience runs the stock level-up loop: it walks MaxExpLevels,
    // fires the level-up events and writes both the exp and the level keys
    // to Storager itself, so the result persists in the save.
    g_add_experience(controller, grant, g_mi_add_experience);
    const int32_t after = g_get_level(g_mi_get_level);
    if (after >= cap) {
        g_level_done = true;
        LOGI("boost: player level %d -> %d via AddExperience(+%d exp, cap %d, "
             "exp table %s)",
             level, after, grant, cap, have_table ? "read" : "fallback");
    } else {
        LOGW("boost: AddExperience(+%d exp) moved level %d -> %d, cap %d not "
             "reached (attempt %d/%d)",
             grant, level, after, cap, g_level_attempts, kMaxLevelAttempts);
    }
}

void grant_currency(const char* key, AddCurrencyFn add,
                    const MethodInfo* add_mi, const char* label,
                    bool* probe_broken) {
    if (*probe_broken) return;

    const int32_t before = storager_get_int(key);
    if (before >= kCurrencyTarget) return;
    if (storager_get_int(kBrokenProbeFlag) != 0) {
        // A previous run proved the probe key does not track the real
        // balance; the one-time flat grant already landed then, so never
        // add again (otherwise every launch would add another ~1e9).
        *probe_broken = true;
        return;
    }

    // BankController resolves the real storage key internally and persists
    // the result through Storager; needIndication=false keeps the UI quiet.
    add(kCurrencyTarget - before, false, 0, add_mi);

    const int32_t after = storager_get_int(key);
    if (after <= before) {
        // The read-back key is not the one the bank writes (13.2.1 migrated
        // currency storage). The grant itself still landed through the game's
        // own method, so mark it and stop touching the balance.
        storager_set_int(kBrokenProbeFlag, 1);
        *probe_broken = true;
        LOGW("boost: %s read-back did not move (%d -> %d); flat grant applied "
             "once, top-up disabled",
             label, before, after);
        return;
    }
    LOGI("boost: %s %d -> %d via BankController (target %d)", label, before,
         after, kCurrencyTarget);
}

void maybe_grant() {
    if (++g_frames % kCheckIntervalFrames != 1) return;
    grant_level();
    grant_currency("Coins", g_add_coins, g_mi_add_coins, "coins",
                   &g_coins_probe_broken);
    grant_currency("Gems", g_add_gems, g_mi_add_gems, "gems",
                   &g_gems_probe_broken);
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
                               reinterpret_cast<void**>(&detail::g_get_int),
                               &detail::g_mi_get_int);
    ok &= detail::resolve_call({"", "Storager", "setInt", 4},
                               reinterpret_cast<void**>(&detail::g_set_int),
                               &detail::g_mi_set_int);

    detail::g_shared_controller_field =
        il2cpp::find_field("", "ExperienceController", "sharedController");
    detail::g_max_exp_levels_field =
        il2cpp::find_field("", "ExperienceController", "MaxExpLevelsDefault");
    if (detail::g_shared_controller_field == nullptr) {
        LOGE("boost: ExperienceController.sharedController field not found");
        ok = false;
    }
    if (detail::g_max_exp_levels_field == nullptr) {
        LOGW("boost: MaxExpLevelsDefault field not found; "
             "level grant will use fallback constants");
    }
    if (!ok) {
        LOGE("boost: cannot resolve the game's progression methods; "
             "grant disabled");
        return false;
    }

    // Any always-alive menu-scene Update works as the trigger; the grant is
    // idempotent, so the first one that resolves wins.
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
    LOGI("boost: progression grant armed (trigger=%s.Update, currency target=%d, "
         "level via AddExperience)",
         chosen, detail::kCurrencyTarget);
    return true;
}

} // namespace player_boost
