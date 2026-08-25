#pragma once

#include <cinttypes>
#include <cstdint>
#include <string>

#include "hidden_items_2313.h"
#include "hook.h"
#include "il2cpp.h"
#include "live_content_2313.h"
#include "log.h"
#include "season_2313.h"
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
constexpr int32_t kExpGrantPerTick = 9999999;
constexpr int32_t kExperienceTarget = 900000000;
constexpr uint64_t kLevelIntervalFrames = 5;
constexpr uint64_t kCurrencyIntervalFrames = 120;
constexpr uint64_t kWarmupFrames = 60;

constexpr const char* kGlobalNs = "";
constexpr const char* kProgressNs = "Progress";
constexpr const char* kServiceClass = "\u4e1c\u4e1d\u4e02\u4e04\u4e1a\u4e15\u4e14\u4e19\u4e11";
constexpr const char* kServiceInstance = "\u4e1e\u4e0f\u4e1a\u4e10\u4e12\u4e0e\u4e1a\u4e17\u4e0e";
constexpr const char* kAddCurrency = "\u4e04\u4e1d\u4e04\u4e19\u4e14\u4e1d\u4e1f\u4e0a\u4e12";
constexpr const char* kWalletHolderClass = "\u4e0e\u4e05\u4e1f\u4e08\u4e15\u4e0a\u4e1c\u4e1f\u4e01";
constexpr const char* kWalletInstance = "\u4e07\u4e12\u4e17\u4e05\u4e06\u4e17\u4e17\u4e0b\u4e09";
constexpr const char* kWalletClass = "\u4e01\u4e1e\u4e07\u4e0a\u4e13\u4e0a\u4e07\u4e1e\u4e02";
constexpr const char* kCoins = "\u4e04\u4e1a\u4e1b\u4e09\u4e12\u4e0c\u4e13\u4e08\u4e16";
constexpr const char* kGems = "\u4e17\u4e1b\u4e03\u4e1d\u4e13\u4e04\u4e1a\u4e0d\u4e02";
constexpr const char* kWalletKeyed = "\u4e10\u4e16\u4e1c\u4e11\u4e0a\u4e19\u4e17\u4e15\u4e01";
constexpr const char* kExperienceClass = "ExperienceController";
constexpr const char* kLevel = "\u4e16\u4e10\u4e19\u4e06\u4e1a\u4e00\u4e04\u4e19\u4e12";
constexpr const char* kExperience = "\u4e15\u4e09\u4e19\u4e0a\u4e0f\u4e0e\u4e0b\u4e0e\u4e1f";
constexpr const char* kAddExperience = "\u4e1c\u4e19\u4e11\u4e07\u4e14\u4e13\u4e1e\u4e16\u4e02";
constexpr const char* kSharedController = "sharedController";
constexpr const char* kBannerClass = "CheatDetectedBanner";
constexpr const char* kBannerWipe = "\u4e0f\u4e07\u4e14\u4e1d\u4e0a\u4e19\u4e10\u4e0b\u4e17";
constexpr const char* kBannerKick = "\u4e08\u4e14\u4e01\u4e1e\u4e1b\u4e05\u4e04\u4e03\u4e0a";
constexpr const char* kMenuClass = "MainMenuController";
constexpr const char* kMenuUpdate = "Update";

using StaticObjFn = void* (*)(void* method);
using InstanceIntFn = int32_t (*)(void* self, void* method);
using InstanceKeyedIntFn = int32_t (*)(void* self, void* key, void* method);
using StaticIntFn = int32_t (*)(void* method);
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
inline void* g_shared_field = nullptr;
inline void* g_wallet_keyed_orig = nullptr;
inline void* g_menu_update_orig = nullptr;
inline void* g_banner_wipe_orig = nullptr;
inline void* g_banner_kick_orig = nullptr;
inline bool g_installed = false;
inline bool g_keys_ready = false;
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
    if (amount <= 0) return;
    void* controller = shared_controller();
    if (controller == nullptr) return;
    reinterpret_cast<AddExperienceFn>(g_add_experience.ptr)(
        controller, amount, 0, nullptr, g_add_experience.info);
}

// Level ramp *and* the experience counter itself.
//
// Below maxLevel the stock add-experience routine (0x01C7AC28) treats the
// stored experience as a per-level remainder: it subtracts the level
// threshold on every level-up and then, at 0x01C7AFF4, executes
//
//     bl   0x01C79A50        ; \u4e16\u4e10\u4e19\u4e06\u4e1a\u4e00\u4e04\u4e19\u4e12()  -> level
//     cmp  w0, #0x41         ; maxLevel (65)
//     csel w26, w26, wzr, lt ; level < 65 ? remainder : 0
//
// so the very level-up that reaches 65 deliberately persists experience as
// **zero**. That is why a level-only pump left the profile at level 65 with
// xp 0.
//
// At maxLevel the same entry point instead tail-branches to the max-level
// overload \u4e0f\u4e09\u4e07\u4e15\u4e02\u4e1a\u4e13\u4e0c\u4e0f (0x01C7B374), which reads the stored experience,
// adds the requested amount and persists the sum through the Progress
// service (0x01B4FD5C) with no level-up bookkeeping and no zeroing. So the
// counter is filled by continuing to drive the *same* stock routine once the
// cap is reached, rather than by writing the backing field directly.
//
// The top-up is self-limiting: it requests exactly the deficit, so after one
// successful grant the experience equals kExperienceTarget and no further
// managed call is made. kExperienceTarget stays well below INT32_MAX because
// the max-level overload computes `experience + amount` as a signed int and
// legitimate post-grant gains keep accruing on top of it.
inline void pump_experience() {
    const int32_t level = current_level();
    if (level < 0) return;

    if (level < kLevelCap) {
        add_experience(kExpGrantPerTick);
        return;
    }

    const int32_t experience = current_experience();
    if (experience < 0 || experience >= kExperienceTarget) return;

    add_experience(kExperienceTarget - experience);
    LOGI("23.1.3-progression: max level reached; experience topped up "
         "%" PRId32 " -> %" PRId32, experience, kExperienceTarget);
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
             "level=%" PRId32 " exp=%" PRId32,
             g_coin_key.c_str(), g_gem_key.c_str(), level, experience);
    }

    if ((g_frames % kCurrencyIntervalFrames) == 0u) top_up_currency();
    if ((g_frames % kLevelIntervalFrames) == 0u) pump_experience();
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
    // Same slot, last in the chain and by far the rarest writer: the offline
    // season hands out one weapon skin or graffiti at a time on a monotonic
    // clock, so the cosmetics arrive as the player keeps playing instead of
    // all at once.
    season_2313::pump_from_main_menu();
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
    resolved &= bind(g_add_experience, kGlobalNs, kExperienceClass,
                     kAddExperience, 3);
    if (!resolved) {
        LOGE("23.1.3-progression: metadata does not match the expected "
             "23.1.3 build; nothing was hooked");
        return false;
    }

    g_shared_field =
        il2cpp::find_field(kGlobalNs, kExperienceClass, kSharedController);
    if (g_shared_field == nullptr) {
        LOGE("23.1.3-progression: ExperienceController.%s not found",
             kSharedController);
        return false;
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
         ", level cap %" PRId32 ", experience target %" PRId32 ")",
         kCurrencyTarget, kLevelCap, kExperienceTarget);
    return true;
}

} // namespace detail

inline bool install_hooks() { return detail::install(); }

} // namespace progression_2313
