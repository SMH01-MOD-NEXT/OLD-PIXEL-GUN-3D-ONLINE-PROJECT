#pragma once

#include <atomic>
#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Local "CHEAT DETECTED" punishment blocked at the source (armory v12).
//
// v9 of this module intercepted the banner itself. That stopped the wipe body
// from running, but the banner still appeared, because the client carries a
// second, deliberately obfuscated layer that lives in the very first loading
// scene and never mentions the word "cheat". Two whole-binary scans of the ARM
// .text section plus the 13.2.1 metadata pin the whole graph down.
//
// 1. THE VERDICT (data driven, and guaranteed to fire on a private server)
//
//   internal enum AbuseMetod { None=0, UpgradeFromVulnerableVersion=1,
//       Coins=2, Gems=4, Expendables=8, Weapons=16,
//       AndroidPackageSignature=32, health=64 }
//
//   Switcher                              (internal sealed MonoBehaviour)
//     internal const string AbuseMethodKey = "AbuseMethod"
//     private static Nullable<AbuseMetod> _abuseMethod
//     internal static AbuseMetod get_AbuseMethod()        RVA 0xEE737C
//       +0x108 bl  Storager.getInt(...)                   RVA 0xEB6CD0
//
//   AdsConfigManager.GetCheatingMethods(AdsConfigMemento) RVA 0xE50FEC
//       +0x78  bl  CheaterConfigMemento.get_CheckSignatureTampering 0x130536C
//       +0xBC  bl  Switcher.get_AbuseMethod                        0xEE737C
//       +0x12C bl  Storager.getInt                ; coin balance
//       +0x144 bl  CheaterConfigMemento.get_CoinThreshold          0x1305374
//       +0x1B0 bl  Storager.getInt                ; gem balance
//       +0x1C8 bl  CheaterConfigMemento.get_GemThreshold           0x130537C
//     single caller: GetPlayerCategory+0x118
//
//   Rilisoft.CheatingMethods { None=0, SignatureTampering=1, CoinThreshold=2,
//                              GemThreshold=4 }
//
//   The granted balance is compared against the cached
//   "CheaterDetectParameters" thresholds, so a 999,999,999 wallet is a
//   permanent, self-renewing verdict. That is why the banner came back.
//
// 2. THE DELIVERY (obfuscated, and it runs before the game does)
//
//   AppsMenu                              (internal sealed MonoBehaviour)
//     public string intendedSignatureHash
//     private const string _suffix = "Scene"
//     private static string GetAbuseKey_53232de5(uint pad)  RVA 0x1BB3530
//     private static string GetAbuseKey_21493d18(uint pad)  RVA 0x1BB3640
//     private static string GetTerminalSceneName_4de1(uint) RVA 0x1BB3750
//     private static IEnumerator MeetTheCoroutine(string sceneName,
//                            long abuseTicks, long nowTicks) RVA 0x1BB3468
//
//     AppsMenu.<MeetTheCoroutine>c__Iterator0.MoveNext()    RVA 0x1BB81E0
//       +0x74  bl  SceneManager.LoadScene(string)           RVA 0x1DB0D4C
//       +0xFC  bl  TimeSpan.FromTicks(long)
//       +0x148 bl  Defs.get_IsDeveloperBuild()
//       +0x1C0 bl  Random..ctor(int seed)
//       +0x220 bl  WaitForSeconds..ctor(float)
//
//   So the punishment is not shown where it is decided. A mark carrying ticks
//   is written into an obfuscated Storager slot, and a later launch waits a
//   randomised delay in the loading scene before it loads the scene that
//   carries CheatDetectedBanner. That is exactly the "playing along and BAM,
//   half the screen" behaviour, and it also means a mark written before this
//   build was installed can still fire once.
//
//   Sibling key builders live in Initializer (GetAbuseKey_d4d3cbab, 0xE36C90)
//   and MainMenuController (GetAbuseKey_f1a4329e, 0xF78C70).
//
// 3. WHAT THIS MODULE DOES NOW
//
//   * forces Switcher.get_AbuseMethod() to AbuseMetod.None
//   * forces AdsConfigManager.GetCheatingMethods() to CheatingMethods.None
//   * stops the delayed terminal-scene coroutine on its first tick, so the
//     banner scene is never loaded
//   * redirects the four obfuscated abuse-key builders to inert keys, so a
//     mark that an earlier session already persisted is no longer read
//   * keeps every v9 banner interception as the last line of defence
//   * swallows the analytics cheater flag and the premium clock check
//
// It still never writes save state: no Storager or PlayerPrefs write, no
// CloudSyncController push, no ownership, currency or level change. The
// persisted marks are read exactly once, for the log, so a device report can
// show whether an earlier wipe left them behind. Detection inputs (balances,
// ownership, package signature) stay stock, and no other client code path
// loses a function it legitimately uses.
namespace cheat_guard {
namespace detail {

using MethodInfo = void;

// Old-IL2CPP ARM32 ABI: instance methods take the object in r0, static
// generated methods take a hidden null context there instead; managed
// arguments follow, then MethodInfo*. 64-bit arguments use aligned register
// pairs, which is why the iterator factory below can be forwarded as declared.
using VoidInstanceFn = void (*)(void* self, const MethodInfo* method);
using VoidStaticFn = void (*)(void* static_context, const MethodInfo* method);
using BoolInstanceFn = bool (*)(void* self, const MethodInfo* method);
using GetGameObjectFn = void* (*)(void* self, const MethodInfo* method);
using DestroyFn = void (*)(void* static_context, void* object,
                           const MethodInfo* method);
using EnumStaticFn = int32_t (*)(void* static_context,
                                 const MethodInfo* method);
using EnumStaticArgFn = int32_t (*)(void* static_context, void* argument,
                                    const MethodInfo* method);
using StringFromUintFn = void* (*)(void* static_context, uint32_t pad,
                                   const MethodInfo* method);
using IteratorFactoryFn = void* (*)(void* static_context, void* scene_name,
                                    int64_t abuse_ticks, int64_t now_ticks,
                                    const MethodInfo* method);
using SetBoolInstanceFn = void (*)(void* self, bool value,
                                   const MethodInfo* method);
using StoragerGetIntFn = int32_t (*)(void* static_context, void* key,
                                     bool suppress_editor_warnings,
                                     bool direct_read_migration,
                                     bool direct_read_migration_v2,
                                     const MethodInfo* method);

inline constexpr const char* kHackDetectedKey = "HackDetected";
inline constexpr const char* kAbuseMethodKey = "AbuseMethod";

// Inert replacements for the obfuscated slot names. Nothing in the client
// knows these keys, so a stale mark in the stock slot is simply never read.
inline constexpr const char* kInertKeyAppsMenuA = "opg3d_inert_slot_a";
inline constexpr const char* kInertKeyAppsMenuB = "opg3d_inert_slot_b";
inline constexpr const char* kInertKeyInitializer = "opg3d_inert_slot_c";
inline constexpr const char* kInertKeyMainMenu = "opg3d_inert_slot_d";

// AbuseMetod.None and CheatingMethods.None are both 0.
inline constexpr int32_t kNoAbuse = 0;
inline constexpr int32_t kNoCheating = 0;

inline VoidInstanceFn g_awake = nullptr;
inline VoidInstanceFn g_update = nullptr;
inline VoidInstanceFn g_remove_objects = nullptr;
inline VoidStaticFn g_clear_all_progress = nullptr;
inline VoidStaticFn g_show_and_clear_progress = nullptr;

inline BoolInstanceFn g_meet_move_next = nullptr;
inline IteratorFactoryFn g_meet_factory = nullptr;
inline EnumStaticFn g_abuse_method = nullptr;
inline EnumStaticArgFn g_cheating_methods = nullptr;
inline StringFromUintFn g_abuse_key_apps_a = nullptr;
inline StringFromUintFn g_abuse_key_apps_b = nullptr;
inline StringFromUintFn g_abuse_key_initializer = nullptr;
inline StringFromUintFn g_abuse_key_main_menu = nullptr;
inline SetBoolInstanceFn g_user_is_cheater = nullptr;
inline VoidInstanceFn g_check_time_hack = nullptr;

inline GetGameObjectFn g_get_game_object = nullptr;
inline const MethodInfo* g_mi_get_game_object = nullptr;
inline DestroyFn g_destroy = nullptr;
inline const MethodInfo* g_mi_destroy = nullptr;
inline StoragerGetIntFn g_storager_get_int = nullptr;
inline const MethodInfo* g_mi_storager_get_int = nullptr;

// Several of these run every frame or on every menu transition, so every log
// site is capped. The first lines are the interesting ones: they prove which
// entry point the client actually tried to use.
inline constexpr uint32_t kMaxLoggedEvents = 8;

inline std::atomic<uint32_t> g_logged_update{0u};
inline std::atomic<uint32_t> g_logged_clear{0u};
inline std::atomic<uint32_t> g_logged_show{0u};
inline std::atomic<uint32_t> g_logged_awake{0u};
inline std::atomic<uint32_t> g_logged_remove{0u};
inline std::atomic<uint32_t> g_logged_move_next{0u};
inline std::atomic<uint32_t> g_logged_factory{0u};
inline std::atomic<uint32_t> g_logged_abuse{0u};
inline std::atomic<uint32_t> g_logged_cheating{0u};
inline std::atomic<uint32_t> g_logged_keys{0u};
inline std::atomic<uint32_t> g_logged_analytics{0u};
inline std::atomic<uint32_t> g_logged_clock{0u};
inline std::atomic<bool> g_marks_probed{false};

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

bool resolve_optional(const hook::ManagedMethod& target, void** out_fn,
                      const MethodInfo** out_mi) {
    void* info = il2cpp::find_method_info(target.namespaze, target.klass,
                                          target.method, target.args_count);
    void* pointer = il2cpp::method_pointer(info);
    if (info == nullptr || pointer == nullptr) {
        LOGW("cheat-guard: optional call %s.%s/%d is unavailable",
             target.klass, target.method, target.args_count);
        return false;
    }
    *out_fn = pointer;
    *out_mi = info;
    return true;
}

int32_t read_mark(const char* key) {
    if (g_storager_get_int == nullptr || il2cpp::string_new == nullptr) {
        return -1;
    }
    void* managed_key = il2cpp::string_new(key);
    if (managed_key == nullptr) return -1;
    return g_storager_get_int(nullptr, managed_key, false, false, false,
                             g_mi_storager_get_int);
}

// Read-only, once per process, on the Unity thread that already runs the
// intercepted code. Tells a device report whether an earlier wipe left marks
// behind. Nothing is written back either way.
void probe_persisted_marks() {
    bool expected = false;
    if (!g_marks_probed.compare_exchange_strong(expected, true,
                                                std::memory_order_acq_rel)) {
        return;
    }
    if (g_storager_get_int == nullptr || il2cpp::string_new == nullptr) {
        LOGW("cheat-guard: Storager.getInt is unavailable; the persisted "
             "marks were not read (nothing is written either way)");
        return;
    }
    LOGI("cheat-guard: persisted marks: '%s'=%d, '%s'=%d (read-only; this "
         "module never writes save state)", kHackDetectedKey,
         read_mark(kHackDetectedKey), kAbuseMethodKey,
         read_mark(kAbuseMethodKey));
}

// ---------------------------------------------------------------------------
// Layer 1: the verdict
// ---------------------------------------------------------------------------

// Stock body reads Storager "AbuseMethod" and caches it in a Nullable. Forcing
// None starves every consumer of the local abuse mark without touching it.
int32_t hook_abuse_method(void* static_context, const MethodInfo* method) {
    (void)static_context;
    (void)method;
    if (should_log(g_logged_abuse, kMaxLoggedEvents)) {
        LOGW("cheat-guard: Switcher.get_AbuseMethod() forced to "
             "AbuseMetod.None; the stock read of Storager key '%s' is bypassed "
             "and the value on disk is left untouched", kAbuseMethodKey);
    }
    probe_persisted_marks();
    return kNoAbuse;
}

// Stock body compares the wallet against the cached CheaterDetectParameters
// thresholds and folds in the signature check. On a private server that is a
// guaranteed hit, so the verdict is forced clean instead.
int32_t hook_cheating_methods(void* static_context, void* config,
                              const MethodInfo* method) {
    (void)static_context;
    (void)config;
    (void)method;
    if (should_log(g_logged_cheating, kMaxLoggedEvents)) {
        LOGW("cheat-guard: AdsConfigManager.GetCheatingMethods() forced to "
             "CheatingMethods.None; the coin/gem threshold and "
             "signature-tampering verdicts never fire (balances themselves "
             "are untouched)");
    }
    return kNoCheating;
}

// ---------------------------------------------------------------------------
// Layer 2: the delivery
// ---------------------------------------------------------------------------

// The frame tick that eventually calls SceneManager.LoadScene on the banner
// scene. Returning false ends the coroutine on its first tick, so the load
// never happens and no other scene work is disturbed.
bool hook_meet_move_next(void* self, const MethodInfo* method) {
    (void)self;
    (void)method;
    if (should_log(g_logged_move_next, kMaxLoggedEvents)) {
        LOGW("cheat-guard: AppsMenu.<MeetTheCoroutine>c__Iterator0.MoveNext() "
             "stopped on its first tick; the delayed SceneManager.LoadScene of "
             "the CHEAT DETECTED scene never runs");
    }
    probe_persisted_marks();
    return false;
}

// Diagnostics only: forwarded unchanged so the log can show that the client
// tried to arm the delayed banner, and under which scene name.
void* hook_meet_factory(void* static_context, void* scene_name,
                        int64_t abuse_ticks, int64_t now_ticks,
                        const MethodInfo* method) {
    if (should_log(g_logged_factory, kMaxLoggedEvents)) {
        LOGW("cheat-guard: AppsMenu.MeetTheCoroutine('%s', abuseTicks=%lld, "
             "nowTicks=%lld) was armed by the client; its first tick will be "
             "refused", il2cpp::to_utf8(scene_name, 96).c_str(),
             static_cast<long long>(abuse_ticks),
             static_cast<long long>(now_ticks));
    }
    if (g_meet_factory == nullptr) return nullptr;
    return g_meet_factory(static_context, scene_name, abuse_ticks, now_ticks,
                          method);
}

// The obfuscated slot-name builders. Handing back an inert key means a mark
// written by an earlier session is no longer found, and a fresh write lands in
// a key nothing else reads. No existing save value is modified or deleted.
void* redirect_abuse_key(StringFromUintFn original, void* static_context,
                         uint32_t pad, const MethodInfo* method,
                         const char* inert_key, const char* origin) {
    if (il2cpp::string_new != nullptr) {
        void* replaced = il2cpp::string_new(inert_key);
        if (replaced != nullptr) {
            if (should_log(g_logged_keys, kMaxLoggedEvents)) {
                LOGW("cheat-guard: %s(pad=%u) redirected to the inert key "
                     "'%s'; the stock abuse slot is neither read nor written",
                     origin, pad, inert_key);
            }
            return replaced;
        }
    }
    if (should_log(g_logged_keys, kMaxLoggedEvents)) {
        LOGW("cheat-guard: %s could not be redirected (string_new "
             "unavailable); the stock abuse slot is still in use", origin);
    }
    if (original == nullptr) return nullptr;
    return original(static_context, pad, method);
}

void* hook_abuse_key_apps_a(void* static_context, uint32_t pad,
                            const MethodInfo* method) {
    return redirect_abuse_key(g_abuse_key_apps_a, static_context, pad, method,
                              kInertKeyAppsMenuA,
                              "AppsMenu.GetAbuseKey_53232de5");
}

void* hook_abuse_key_apps_b(void* static_context, uint32_t pad,
                            const MethodInfo* method) {
    return redirect_abuse_key(g_abuse_key_apps_b, static_context, pad, method,
                              kInertKeyAppsMenuB,
                              "AppsMenu.GetAbuseKey_21493d18");
}

void* hook_abuse_key_initializer(void* static_context, uint32_t pad,
                                 const MethodInfo* method) {
    return redirect_abuse_key(g_abuse_key_initializer, static_context, pad,
                              method, kInertKeyInitializer,
                              "Initializer.GetAbuseKey_d4d3cbab");
}

void* hook_abuse_key_main_menu(void* static_context, uint32_t pad,
                               const MethodInfo* method) {
    return redirect_abuse_key(g_abuse_key_main_menu, static_context, pad,
                              method, kInertKeyMainMenu,
                              "MainMenuController.GetAbuseKey_f1a4329e");
}

// ---------------------------------------------------------------------------
// Layer 3: the banner itself (unchanged from armory v9)
// ---------------------------------------------------------------------------

void hook_update(void* self, const MethodInfo* method) {
    (void)self;
    (void)method;
    if (should_log(g_logged_update, kMaxLoggedEvents)) {
        LOGW("cheat-guard: CheatDetectedBanner.Update() suppressed; its tail "
             "call into ClearAllProgress (PlayerPrefs.DeleteAll + Storager "
             "marks + cloud push + abuse report) never runs");
    }
    probe_persisted_marks();
}

void hook_clear_all_progress(void* static_context, const MethodInfo* method) {
    (void)static_context;
    (void)method;
    if (should_log(g_logged_clear, kMaxLoggedEvents)) {
        LOGW("cheat-guard: CheatDetectedBanner.ClearAllProgress() refused; "
             "local progress, Storager marks and CloudSyncController are left "
             "exactly as they were");
    }
    probe_persisted_marks();
}

void hook_show_and_clear_progress(void* static_context,
                                  const MethodInfo* method) {
    (void)static_context;
    (void)method;
    if (should_log(g_logged_show, kMaxLoggedEvents)) {
        LOGW("cheat-guard: CheatDetectedBanner.ShowAndClearProgress() "
             "refused; the Photon disconnect and the banner scene load are "
             "both skipped");
    }
    probe_persisted_marks();
}

void hook_awake(void* self, const MethodInfo* method) {
    (void)method;
    const bool log_this = should_log(g_logged_awake, kMaxLoggedEvents);
    if (log_this) {
        LOGW("cheat-guard: CheatDetectedBanner.Awake() intercepted; the stock "
             "body would tear down the rest of the scene and arm the wipe "
             "tick");
    }
    probe_persisted_marks();

    if (self == nullptr || g_get_game_object == nullptr ||
        g_destroy == nullptr) {
        if (log_this) {
            LOGW("cheat-guard: the banner object could not be removed "
                 "(Component.get_gameObject / Object.Destroy unavailable); "
                 "the overlay may stay visible until the next scene load, but "
                 "no progress is erased");
        }
        return;
    }

    void* game_object = g_get_game_object(self, g_mi_get_game_object);
    if (game_object == nullptr) {
        if (log_this) {
            LOGW("cheat-guard: the banner has no GameObject to remove; the "
                 "overlay may stay visible, but no progress is erased");
        }
        return;
    }

    g_destroy(nullptr, game_object, g_mi_destroy);
    if (log_this) {
        LOGI("cheat-guard: banner object destroyed on Awake; the session "
             "continues untouched");
    }
}

void hook_remove_objects(void* self, const MethodInfo* method) {
    (void)self;
    (void)method;
    if (should_log(g_logged_remove, kMaxLoggedEvents)) {
        LOGW("cheat-guard: CheatDetectedBanner.RemoveObjects() refused; no "
             "scene object is destroyed");
    }
}

// ---------------------------------------------------------------------------
// Side effects that are not the banner but belong to the same machinery
// ---------------------------------------------------------------------------

void hook_user_is_cheater(void* self, bool value, const MethodInfo* method) {
    (void)self;
    (void)method;
    if (should_log(g_logged_analytics, kMaxLoggedEvents)) {
        LOGW("cheat-guard: DevToDevFacade.set_UserIsCheater(%d) swallowed; the "
             "analytics cheater flag is never raised", value ? 1 : 0);
    }
}

void hook_check_time_hack(void* self, const MethodInfo* method) {
    (void)self;
    (void)method;
    if (should_log(g_logged_clock, kMaxLoggedEvents)) {
        LOGW("cheat-guard: PremiumAccountController.CheckTimeHack() refused; "
             "StopAccountsWork is not reached, so a clock difference cannot "
             "revoke a rented account");
    }
}

// The iterator is a compiler-generated nested type. Different IL2CPP
// metadata builds expose nested names differently, so every known spelling is
// tried instead of guessing one.
bool install_terminal_scene_block() {
    static const char* const kSpellings[] = {
        "AppsMenu/<MeetTheCoroutine>c__Iterator0",
        "<MeetTheCoroutine>c__Iterator0",
        "AppsMenu.<MeetTheCoroutine>c__Iterator0",
    };
    for (const char* klass : kSpellings) {
        if (hook::install({"", klass, "MoveNext", 0},
                          replacement(&hook_meet_move_next),
                          original_slot(&g_meet_move_next))) {
            LOGI("cheat-guard: the CHEAT DETECTED scene trigger is blocked "
                 "(%s.MoveNext)", klass);
            return true;
        }
    }
    return false;
}

} // namespace detail

inline bool install_hooks() {
    // Optional calls used by the interceptions below are resolved first, so
    // the very first intercepted event can already log the persisted marks.
    if (!detail::resolve_optional(
            {"", "Storager", "getInt", 4},
            reinterpret_cast<void**>(&detail::g_storager_get_int),
            &detail::g_mi_storager_get_int)) {
        detail::g_storager_get_int = nullptr;
    }
    if (!detail::resolve_optional(
            {"UnityEngine", "Component", "get_gameObject", 0},
            reinterpret_cast<void**>(&detail::g_get_game_object),
            &detail::g_mi_get_game_object)) {
        detail::g_get_game_object = nullptr;
    }
    if (!detail::resolve_optional(
            {"UnityEngine", "Object", "Destroy", 1},
            reinterpret_cast<void**>(&detail::g_destroy),
            &detail::g_mi_destroy)) {
        detail::g_destroy = nullptr;
    }

    // Mandatory: the two verdict functions and the three banner entry points.
    bool armed = hook::install(
        {"", "Switcher", "get_AbuseMethod", 0},
        detail::replacement(&detail::hook_abuse_method),
        detail::original_slot(&detail::g_abuse_method), true);
    armed &= hook::install(
        {"", "AdsConfigManager", "GetCheatingMethods", 1},
        detail::replacement(&detail::hook_cheating_methods),
        detail::original_slot(&detail::g_cheating_methods), true);
    armed &= hook::install(
        {"", "CheatDetectedBanner", "Update", 0},
        detail::replacement(&detail::hook_update),
        detail::original_slot(&detail::g_update), true);
    armed &= hook::install(
        {"", "CheatDetectedBanner", "ClearAllProgress", 0},
        detail::replacement(&detail::hook_clear_all_progress),
        detail::original_slot(&detail::g_clear_all_progress), true);
    armed &= hook::install(
        {"", "CheatDetectedBanner", "ShowAndClearProgress", 0},
        detail::replacement(&detail::hook_show_and_clear_progress),
        detail::original_slot(&detail::g_show_and_clear_progress), true);
    if (!armed) {
        LOGE("cheat-guard: the local punishment path could not be "
             "neutralised; treat this build as unsafe for a real save");
        return false;
    }

    // The single hook that keeps the banner off the screen. Losing it still
    // leaves the save protected, so it must not fail the module, but it is
    // loud because the overlay itself can then still appear.
    if (!detail::install_terminal_scene_block()) {
        LOGE("cheat-guard: the delayed CHEAT DETECTED scene load could not be "
             "hooked under any known nested class name; the overlay can still "
             "appear, but it can no longer erase anything");
    }

    // Diagnostics for the same trigger, forwarded unchanged.
    if (!hook::install({"", "AppsMenu", "MeetTheCoroutine", 3},
                       detail::replacement(&detail::hook_meet_factory),
                       detail::original_slot(&detail::g_meet_factory))) {
        LOGW("cheat-guard: AppsMenu.MeetTheCoroutine could not be hooked; the "
             "log will not show when the client arms the banner");
    }

    // Obfuscated slot names, so a mark persisted by an earlier session is not
    // read any more. Best effort: each one that fails only means the stock
    // slot stays in use, and the tick above still refuses to show anything.
    if (!hook::install({"", "AppsMenu", "GetAbuseKey_53232de5", 1},
                       detail::replacement(&detail::hook_abuse_key_apps_a),
                       detail::original_slot(&detail::g_abuse_key_apps_a))) {
        LOGW("cheat-guard: AppsMenu.GetAbuseKey_53232de5 could not be "
             "redirected");
    }
    if (!hook::install({"", "AppsMenu", "GetAbuseKey_21493d18", 1},
                       detail::replacement(&detail::hook_abuse_key_apps_b),
                       detail::original_slot(&detail::g_abuse_key_apps_b))) {
        LOGW("cheat-guard: AppsMenu.GetAbuseKey_21493d18 could not be "
             "redirected");
    }
    if (!hook::install(
            {"", "Initializer", "GetAbuseKey_d4d3cbab", 1},
            detail::replacement(&detail::hook_abuse_key_initializer),
            detail::original_slot(&detail::g_abuse_key_initializer))) {
        LOGW("cheat-guard: Initializer.GetAbuseKey_d4d3cbab could not be "
             "redirected");
    }
    if (!hook::install(
            {"", "MainMenuController", "GetAbuseKey_f1a4329e", 1},
            detail::replacement(&detail::hook_abuse_key_main_menu),
            detail::original_slot(&detail::g_abuse_key_main_menu))) {
        LOGW("cheat-guard: MainMenuController.GetAbuseKey_f1a4329e could not "
             "be redirected");
    }

    // Cosmetics and scene safety.
    if (!hook::install({"", "CheatDetectedBanner", "Awake", 0},
                       detail::replacement(&detail::hook_awake),
                       detail::original_slot(&detail::g_awake))) {
        LOGW("cheat-guard: CheatDetectedBanner.Awake could not be hooked; the "
             "overlay can still appear, but it can no longer wipe anything");
    }
    if (!hook::install({"", "CheatDetectedBanner", "RemoveObjects", 0},
                       detail::replacement(&detail::hook_remove_objects),
                       detail::original_slot(&detail::g_remove_objects))) {
        LOGW("cheat-guard: CheatDetectedBanner.RemoveObjects could not be "
             "hooked; a shown banner may still destroy other scene objects");
    }

    // Same machinery, different consequences.
    if (!hook::install({"", "DevToDevFacade", "set_UserIsCheater", 1},
                       detail::replacement(&detail::hook_user_is_cheater),
                       detail::original_slot(&detail::g_user_is_cheater))) {
        LOGW("cheat-guard: DevToDevFacade.set_UserIsCheater could not be "
             "hooked; the analytics cheater flag stays stock");
    }
    if (!hook::install({"", "PremiumAccountController", "CheckTimeHack", 0},
                       detail::replacement(&detail::hook_check_time_hack),
                       detail::original_slot(&detail::g_check_time_hack))) {
        LOGW("cheat-guard: PremiumAccountController.CheckTimeHack could not be "
             "hooked; a clock difference can still stop a rented account");
    }

    LOGI("cheat-guard: armed (verdicts=forced clean, banner scene=never "
         "loaded, abuse slots=inert, wipe path=blocked, "
         "PlayerPrefs/Storager/CloudSync writes=none, detection inputs=stock)");
    return true;
}

} // namespace cheat_guard
