#pragma once

#include <cinttypes>
#include <cstdint>
#include <string>

#include "hidden_items_2313.h"
#include "hook.h"
#include "il2cpp.h"
#include "live_content_2313.h"
#include "log.h"
#include "pixel_pass_2313.h"
#include "weapon_modules_2313.h"

// Offline progression for the exact supplied 23.1.3 ARM64 libil2cpp.so:
// currency (coins + gems) and player level.
//
// This is the 23.1.3 counterpart of progression_1610.h. The 16.1.0 module
// cannot be reused: that build is ELF32 ARM and its whole currency
// architecture (BankController / Storager / CheaterConfigMemento) no longer
// exists in 23.1.3. See docs/PORT_23_1_3_PROGRESSION.md for the full mapping
// and for how every target below was proven from the supplied artifacts.
//
// ARM64 ABI reminder (same rule as the Photon port): generated managed
// methods take their explicit arguments followed by MethodInfo*. Instance
// methods additionally take `this` first. The old ARM32 leading
// static-context argument does NOT exist here.
namespace progression_2313 {
namespace detail {

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

constexpr int32_t kCurrencyTarget = 999999999;
constexpr int32_t kLevelCap = 65;
// Master switch for the one-shot level grant. The level is not faked in a
// getter and it is not pumped: it is granted the same way the game grants it,
// once per process, and the amount is computed from the game's own table.
//
//   * 世丐丙丆业一丄丙丒() (0x01C79A50) and 丕三丙上丏与下与丟() (0x01C79AB0) read the
//     level and the experience remainder out of the persisted profile, so a
//     level written through the stock routine is still there after a restart.
//   * Draining experience is refused by construction (see add_experience):
//     a negative amount runs the stock level-up loop with a shrinking
//     accumulator and takes real levels away with it, which is exactly what
//     wiped level 65 before.
//   * The veteran chest cannot come back from this. That window is the
//     max-level presentation raised by the overload 丏三万丕丂业专丌丏
//     (0x01C7B374), and the entry point only reaches it when the profile is
//     ALREADY at 65. The grant below exits before touching anything in that
//     case, whatever the experience counter says, so the only presentation it
//     can cause is the ordinary level-up window on the way up.
constexpr bool kGrantLevel = true;
// Grant rounds are bounded. Every round recomputes the deficit from the live
// level, so a short landing is corrected instead of guessed at, and the whole
// thing stops for good the moment the level reads 65.
constexpr int32_t kMaxLevelGrantRounds = 4;
constexpr uint64_t kLevelIntervalFrames = 5;
constexpr uint64_t kCurrencyIntervalFrames = 120;
constexpr uint64_t kWarmupFrames = 60;

constexpr const char* kGlobalNs = "";
constexpr const char* kProgressNs = "Progress";
constexpr const char* kServiceClass = "东丝丂丄业丕且丙丑";
// Instance getter of the Progress service, dump line 284773 / RVA 0x1B3BA40.
// This name is nine metadata characters long. A shortened seven-character
// spelling occurs nowhere in 23.1.3 metadata, and using it made bind() fail
// on the first target and took the whole module -- including the
// MainMenuController.Update slot every other pump depends on -- offline.
constexpr const char* kServiceInstance = "丞丏业丐丒与业丗与";
constexpr const char* kAddCurrency = "丄丝丄丙且丝丟上丒";
constexpr const char* kWalletHolderClass = "与丅丟丈丕上东丟丁";
constexpr const char* kWalletInstance = "万丒丗丅丆丗丗下三";
constexpr const char* kWalletClass = "丁丞万上专上万丞丂";
constexpr const char* kCoins = "丄业丛三丒丌专丈世";
constexpr const char* kGems = "丗丛七丝专丄业不丂";
constexpr const char* kWalletKeyed = "丐世东丑上丙丗丕丁";
constexpr const char* kExperienceClass = "ExperienceController";
constexpr const char* kLevel = "世丐丙丆业一丄丙丒";
constexpr const char* kExperience = "丕三丙上丏与下与丟";
constexpr const char* kAddExperience = "东丙丑万且专丞世丂";
constexpr const char* kSharedController = "sharedController";
// Per-level experience threshold table the stock level-up loop consults:
// ExperienceController.丘一不丒丐东不世丗 (static field +0x48) is a 丂丘丅世丏世东丗丄
// wrapper around a salted-int array, and 丕与丏丅丆丕专万丟(int level)
// (RVA 0x03BFB9D8) decodes entry `level` of it.
constexpr const char* kLevelTableClass = "丂丘丅世丏世东丗丄";
constexpr const char* kLevelTableField = "丘一不丒丐东不世丗";
constexpr const char* kLevelThreshold = "丕与丏丅丆丕专万丟";
// Layout the accessor itself proves at 0x03BFB9DC / 0x03BFB9E4: the wrapper
// keeps its array at +0x10 and il2cpp keeps the array length at +0x18.
constexpr uintptr_t kTableArrayOffset = 0x10u;
constexpr uintptr_t kArrayLengthOffset = 0x18u;
constexpr const char* kBannerClass = "CheatDetectedBanner";
constexpr const char* kBannerWipe = "丏万且丝上丙丐下丗";
constexpr const char* kBannerKick = "丈且丁丞丛丅丄七上";
constexpr const char* kMenuClass = "MainMenuController";
constexpr const char* kMenuUpdate = "Update";

using StaticObjFn = void* (*)(void* method);
using InstanceIntFn = int32_t (*)(void* self, void* method);
using InstanceKeyedIntFn = int32_t (*)(void* self, void* key, void* method);
using StaticIntFn = int32_t (*)(void* method);
using InstanceIntArgFn = int32_t (*)(void* self, int32_t arg, void* method);
using InstanceVoidFn = void (*)(void* self, void* method);
using AddCurrencyFn = void (*)(void* self, void* key, int32_t amount,
                               int32_t accrual, bool indicate, bool silent,
                               void* method);
using AddExperienceFn = void (*)(void* self, int32_t amount, int32_t reason,
                                 void* payload, void* method);

struct Managed {
    void* info = nullptr;
    void* ptr = nullptr;
    explicit operator bool() const noexcept {
        return info != nullptr && ptr != nullptr;
    }
};

inline bool bind(Managed& out, const char* namespaze, const char* klass,
                 const char* method, int args_count) {
    out.info = il2cpp::find_method_info(namespaze, klass, method, args_count);
    if (out.info == nullptr) {
        LOGE("23.1.3-progression: %s::%s/%d not found in metadata", klass,
             method, args_count);
        return false;
    }
    out.ptr = il2cpp::method_pointer(out.info);
    if (out.ptr == nullptr) {
        LOGE("23.1.3-progression: %s::%s/%d has no method pointer", klass,
             method, args_count);
        return false;
    }
    return true;
}

inline Managed g_service{};
inline Managed g_add_currency{};
inline Managed g_wallet{};
inline Managed g_coins{};
inline Managed g_gems{};
inline Managed g_level{};
inline Managed g_experience{};
inline Managed g_add_experience{};
inline Managed g_level_threshold{};
inline void* g_shared_field = nullptr;
inline void* g_level_table_field = nullptr;
inline void* g_wallet_keyed_orig = nullptr;
inline void* g_menu_update_orig = nullptr;
inline void* g_banner_wipe_orig = nullptr;
inline void* g_banner_kick_orig = nullptr;
inline bool g_installed = false;
inline bool g_keys_ready = false;
inline bool g_level_grant_done = false;
inline int32_t g_level_grant_rounds = 0;
inline uint64_t g_frames = 0u;
inline std::string g_coin_key;
inline std::string g_gem_key;

enum class Capture { None, Coin, Gem };
inline Capture g_capture = Capture::None;

inline void banner_wipe_hook(void* /*method*/) {
    LOGI("23.1.3-progression: suppressed CheatDetectedBanner save wipe");
}

inline void banner_kick_hook(void* /*method*/) {
    LOGI("23.1.3-progression: suppressed CheatDetectedBanner disconnect");
}

inline int32_t wallet_keyed_hook(void* self, void* key, void* method) {
    if (g_capture != Capture::None && key != nullptr) {
        const std::string observed = il2cpp::to_utf8(key, 64);
        if (!observed.empty()) {
            if (g_capture == Capture::Coin && g_coin_key.empty()) {
                g_coin_key = observed;
            } else if (g_capture == Capture::Gem && g_gem_key.empty()) {
                g_gem_key = observed;
            }
        }
    }
    return reinterpret_cast<InstanceKeyedIntFn>(g_wallet_keyed_orig)(
        self, key, method);
}

inline void* wallet_instance() {
    if (!g_wallet) return nullptr;
    return reinterpret_cast<StaticObjFn>(g_wallet.ptr)(g_wallet.info);
}

inline void* service_instance() {
    if (!g_service) return nullptr;
    return reinterpret_cast<StaticObjFn>(g_service.ptr)(g_service.info);
}

inline bool capture_keys() {
    void* wallet = wallet_instance();
    if (wallet == nullptr) return false;

    g_capture = Capture::Coin;
    (void)reinterpret_cast<InstanceIntFn>(g_coins.ptr)(wallet, g_coins.info);
    g_capture = Capture::Gem;
    (void)reinterpret_cast<InstanceIntFn>(g_gems.ptr)(wallet, g_gems.info);
    g_capture = Capture::None;

    if (g_coin_key.empty() || g_gem_key.empty()) return false;
    if (g_coin_key == g_gem_key) {
        LOGE("23.1.3-progression: coin and gem keys are identical ('%s'); "
             "refusing to grant", g_coin_key.c_str());
        g_coin_key.clear();
        g_gem_key.clear();
        return false;
    }
    return true;
}

inline void add_currency(void* service, const std::string& key,
                         int32_t amount) {
    if (service == nullptr || amount <= 0) return;
    void* managed_key = il2cpp::string_new(key.c_str());
    if (managed_key == nullptr) return;
    reinterpret_cast<AddCurrencyFn>(g_add_currency.ptr)(
        service, managed_key, amount, 0, false, false, g_add_currency.info);
}

inline void top_up_currency() {
    void* service = service_instance();
    void* wallet = wallet_instance();
    if (service == nullptr || wallet == nullptr) return;

    const int32_t coins =
        reinterpret_cast<InstanceIntFn>(g_coins.ptr)(wallet, g_coins.info);
    if (coins >= 0 && coins < kCurrencyTarget) {
        add_currency(service, g_coin_key, kCurrencyTarget - coins);
    }

    const int32_t gems =
        reinterpret_cast<InstanceIntFn>(g_gems.ptr)(wallet, g_gems.info);
    if (gems >= 0 && gems < kCurrencyTarget) {
        add_currency(service, g_gem_key, kCurrencyTarget - gems);
    }
}

inline void* shared_controller() {
    if (g_shared_field == nullptr ||
        il2cpp::field_static_get_value == nullptr) {
        return nullptr;
    }
    void* value = nullptr;
    il2cpp::field_static_get_value(g_shared_field, &value);
    return value;
}

inline int32_t current_level() {
    return reinterpret_cast<StaticIntFn>(g_level.ptr)(g_level.info);
}

inline int32_t current_experience() {
    return reinterpret_cast<StaticIntFn>(g_experience.ptr)(g_experience.info);
}

inline void add_experience(int32_t amount) {
    // Zero is allowed on purpose: it still drives the stock level-up loop
    // over a remainder that already covers the thresholds. Negative amounts
    // are refused, because the same loop then runs with a shrinking
    // accumulator and takes earned levels away -- the drain that cost this
    // profile its level 65.
    if (amount < 0) return;
    void* controller = shared_controller();
    if (controller == nullptr) return;
    reinterpret_cast<AddExperienceFn>(g_add_experience.ptr)(
        controller, amount, 0, nullptr, g_add_experience.info);
}

// Level grant: exactly the experience the profile still needs to walk from
// the level it has now up to 65, requested in one go from the stock routine.
//
// Below the cap the stock add-experience entry point 东丙丑万且专丞世丂
// (0x01C7AC28) treats the stored experience as a per-level remainder. Its
// loop (0x01C7ADF4 .. 0x01C7AF20) runs, per iteration:
//
//     bl   0x01C79A50        ; level; > 0x40 (64) -> leave the loop
//     ldr  x28, [x8, #0x48]  ; ExperienceController.丘一不丒丐东不世丗
//     bl   0x03BFB9D8        ; 丕与丏丅丆丕专万丟(level) -> threshold
//     cmp  w26, w0           ; accumulator < threshold -> leave the loop
//     bl   0x01B4FBD8        ; Progress service: persist level + 1
//     sub  w26, w26, w28     ; accumulator -= threshold
//
// and on the way out 0x01C7B0CC persists what is left of the accumulator
// through the Progress service (0x01B4FD5C). At 0x01C7AFF4 it also executes
// `cmp w0,#0x41; csel w26,w26,wzr,lt`, so the level-up that reaches 65 stores
// the remainder as 0 by design -- that, not a missing grant, is why a capped
// profile shows xp 0.
//
// Two consequences carry this module:
//   * the level is a real persisted value written by the game's own service
//     call, so granting it this way survives a restart; nothing is faked in a
//     getter and nothing has to be re-granted every session;
//   * the amount is not a guess -- it is the sum of 丕与丏丅丆丕专万丟(level) for
//     every level from the current one through 64, minus the remainder the
//     profile already holds.
inline void* level_table() {
    if (g_level_table_field == nullptr ||
        il2cpp::field_static_get_value == nullptr) {
        return nullptr;
    }
    void* value = nullptr;
    il2cpp::field_static_get_value(g_level_table_field, &value);
    return value;
}

// Experience the stock loop demands to leave `level` behind, read from the
// game's own table. Returns -1 when the table is not usable yet. The index is
// range-checked exactly the way the accessor does it at 0x03BFB9E4, so a
// shorter table can never turn into a managed IndexOutOfRange throw.
inline int32_t level_threshold(int32_t level) {
    if (!g_level_threshold || level < 0) return -1;

    void* table = level_table();
    if (table == nullptr) return -1;

    void* array = *reinterpret_cast<void**>(
        reinterpret_cast<char*>(table) + kTableArrayOffset);
    if (array == nullptr) return -1;

    const uint32_t length = *reinterpret_cast<uint32_t*>(
        reinterpret_cast<char*>(array) + kArrayLengthOffset);
    if (static_cast<uint32_t>(level) >= length) return -1;

    return reinterpret_cast<InstanceIntArgFn>(g_level_threshold.ptr)(
        table, level, g_level_threshold.info);
}

// Sum of the thresholds from `level` up to the cap, minus what the profile
// already holds. Returns false when any threshold is unreadable, so the caller
// retries instead of inventing a number.
inline bool experience_to_cap(int32_t level, int32_t remainder,
                              int32_t* out_needed) {
    int64_t total = 0;
    for (int32_t step = level; step < kLevelCap; ++step) {
        const int32_t threshold = level_threshold(step);
        if (threshold <= 0) return false;
        total += threshold;
    }

    if (remainder > 0) total -= remainder;
    if (total < 0) total = 0;
    if (total > INT32_MAX) return false;

    *out_needed = static_cast<int32_t>(total);
    return true;
}

// One-shot, level-driven grant. The experience counter is never a reason to
// act: if the profile already reads 65 this returns immediately and touches
// nothing, which is what keeps the max-level overload -- and with it the
// veteran chest -- permanently out of reach.
inline void grant_level_to_cap() {
    if (!kGrantLevel || g_level_grant_done) return;
    if (!g_level || !g_experience || !g_add_experience || !g_level_threshold) {
        g_level_grant_done = true;
        return;
    }

    const int32_t level = current_level();
    if (level < 0) return;

    if (level >= kLevelCap) {
        g_level_grant_done = true;
        LOGI("23.1.3-progression: level %" PRId32 " already meets the cap; "
             "no experience is granted and none is taken away", level);
        return;
    }

    if (g_level_grant_rounds >= kMaxLevelGrantRounds) {
        g_level_grant_done = true;
        LOGE("23.1.3-progression: level is still %" PRId32 " after %" PRId32
             " grant round(s); leaving the profile exactly as it is",
             level, g_level_grant_rounds);
        return;
    }

    const int32_t remainder = current_experience();
    int32_t needed = 0;
    if (!experience_to_cap(level, remainder, &needed)) {
        if ((g_frames % 300u) == 0u) {
            LOGE("23.1.3-progression: level threshold table is not readable "
                 "yet; level %" PRId32 " left untouched", level);
        }
        return;
    }

    void* controller = shared_controller();
    if (controller == nullptr) return;

    ++g_level_grant_rounds;
    LOGI("23.1.3-progression: granting %" PRId32 " experience to carry level "
         "%" PRId32 " -> %" PRId32 " (stored remainder %" PRId32
         ", round %" PRId32 ")",
         needed, level, kLevelCap, remainder, g_level_grant_rounds);

    add_experience(needed);

    const int32_t new_level = current_level();
    const int32_t new_remainder = current_experience();
    if (new_level >= kLevelCap) {
        g_level_grant_done = true;
        LOGI("23.1.3-progression: level granted and persisted %" PRId32
             " -> %" PRId32 " (remainder %" PRId32 ")",
             level, new_level, new_remainder);
    } else {
        LOGW("23.1.3-progression: level moved %" PRId32 " -> %" PRId32
             " (remainder %" PRId32 "); recomputing the deficit next tick",
             level, new_level, new_remainder);
    }
}

inline void maybe_grant() {
    ++g_frames;
    if (g_frames < kWarmupFrames) return;

    if (!g_keys_ready) {
        if (!capture_keys()) {
            if ((g_frames % 300u) == 0u) {
                LOGE("23.1.3-progression: wallet keys not captured yet");
            }
            return;
        }
        g_keys_ready = true;
        const int32_t level =
            reinterpret_cast<StaticIntFn>(g_level.ptr)(g_level.info);
        const int32_t experience =
            reinterpret_cast<StaticIntFn>(g_experience.ptr)(g_experience.info);
        LOGI("23.1.3-progression: armed; coin key='%s' gem key='%s' "
             "level=%" PRId32 " exp=%" PRId32 " (level grant %s)",
             g_coin_key.c_str(), g_gem_key.c_str(), level, experience,
             kGrantLevel ? "on" : "off");
    }

    if ((g_frames % kCurrencyIntervalFrames) == 0u) top_up_currency();
    // Level only, and only while the profile reads below the cap. Once
    // grant_level_to_cap() has settled it never calls into the stock
    // add-experience routine again, so nothing here can offer the veteran
    // chest on a loop the way the old experience pump did.
    if (!g_level_grant_done && (g_frames % kLevelIntervalFrames) == 0u) {
        grant_level_to_cap();
    }
}

inline void menu_update_hook(void* self, void* method) {
    reinterpret_cast<InstanceVoidFn>(g_menu_update_orig)(self, method);
    maybe_grant();
    // The module inventory grant runs stock managed transactions, so it
    // needs a game thread and a live main menu: this Update slot is the
    // only such point this port already owns.
    weapon_modules_2313::pump_from_main_menu();
    // Same slot, same reason for the hidden weapon / wear / gadget grant.
    // It warms up later than the module sweep on purpose, so the two never
    // drive the stock item inventory on the same frame.
    hidden_items_2313::pump_from_main_menu();
    // Same slot again, read-only: the content gate reports which live
    // content the offline ExpOpenSystem table still keeps closed.
    live_content_2313::pump_from_main_menu();
    // Read-only counters for the PixelPass season. The season itself is NOT
    // driven from here any more: it is served from the config-cache read
    // path inside pixel_pass_2313, so a failure in this module can no longer
    // take the battle pass down with it.
    pixel_pass_2313::pump_from_main_menu();
}

inline bool install() {
    if (g_installed) return true;

    bool resolved = true;
    resolved &= bind(g_service, kProgressNs, kServiceClass, kServiceInstance, 0);
    resolved &= bind(g_add_currency, kProgressNs, kServiceClass, kAddCurrency, 5);
    resolved &= bind(g_wallet, kProgressNs, kWalletHolderClass, kWalletInstance, 0);
    resolved &= bind(g_coins, kProgressNs, kWalletClass, kCoins, 0);
    resolved &= bind(g_gems, kProgressNs, kWalletClass, kGems, 0);
    resolved &= bind(g_level, kGlobalNs, kExperienceClass, kLevel, 0);
    resolved &= bind(g_experience, kGlobalNs, kExperienceClass, kExperience, 0);
    // The add-experience entry point, the per-level threshold accessor and
    // the shared controller are only used by the level grant. With the grant
    // off none of them is resolved, so a metadata change on that path can no
    // longer take down the MainMenuController.Update slot every other pump in
    // this port depends on.
    if (kGrantLevel) {
        resolved &= bind(g_add_experience, kGlobalNs, kExperienceClass,
                         kAddExperience, 3);
        resolved &= bind(g_level_threshold, kGlobalNs, kLevelTableClass,
                         kLevelThreshold, 1);
    }
    if (!resolved) {
        LOGE("23.1.3-progression: metadata does not match the expected "
             "23.1.3 build; nothing was hooked");
        return false;
    }

    if (kGrantLevel) {
        g_shared_field =
            il2cpp::find_field(kGlobalNs, kExperienceClass, kSharedController);
        if (g_shared_field == nullptr) {
            LOGE("23.1.3-progression: ExperienceController.%s not found",
                 kSharedController);
            return false;
        }
        g_level_table_field =
            il2cpp::find_field(kGlobalNs, kExperienceClass, kLevelTableField);
        if (g_level_table_field == nullptr) {
            LOGE("23.1.3-progression: the ExperienceController level "
                 "threshold table field was not found");
            return false;
        }
    }

    const bool shield_wipe = hook::install(
        {kGlobalNs, kBannerClass, kBannerWipe, 0},
        reinterpret_cast<void*>(&banner_wipe_hook), &g_banner_wipe_orig, true);
    const bool shield_kick = hook::install(
        {kGlobalNs, kBannerClass, kBannerKick, 0},
        reinterpret_cast<void*>(&banner_kick_hook), &g_banner_kick_orig, true);
    if (!shield_wipe || !shield_kick) {
        LOGE("23.1.3-progression: save shield unavailable (wipe=%d kick=%d); "
             "refusing to grant anything", shield_wipe ? 1 : 0,
             shield_kick ? 1 : 0);
        return false;
    }

    if (!hook::install({kProgressNs, kWalletClass, kWalletKeyed, 1},
                       reinterpret_cast<void*>(&wallet_keyed_hook),
                       &g_wallet_keyed_orig, true)) {
        LOGE("23.1.3-progression: keyed wallet getter could not be hooked");
        return false;
    }

    if (!hook::install({kGlobalNs, kMenuClass, kMenuUpdate, 0},
                       reinterpret_cast<void*>(&menu_update_hook),
                       &g_menu_update_orig, true)) {
        LOGE("23.1.3-progression: MainMenuController.Update could not be hooked");
        return false;
    }

    g_installed = true;
    LOGI("23.1.3-progression: installed (currency target %" PRId32
         ", level cap %" PRId32 ", level grant %s)",
         kCurrencyTarget, kLevelCap,
         kGrantLevel
             ? "on; the deficit up to the cap is granted once, and only while "
               "the profile reads below it"
             : "off; the level is left exactly as the profile has it");
    return true;
}

} // namespace detail

inline bool install_hooks() { return detail::install(); }

} // namespace progression_2313
