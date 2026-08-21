#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Local "CHEAT DETECTED" punishment blocked at the source (armory v12.1).
//
// Full evidence graph: docs/CHEAT_BANNER_SUPPRESSION.md. Short version, all of
// it verified against the 13.2.1 dump and two whole-binary scans of the ARM
// .text section.
//
// 1. THE VERDICT
//    Switcher.get_AbuseMethod()                             0xEE737C
//      reads the Storager "AbuseMethod" flags (enum AbuseMetod)
//    Rilisoft.AdsConfigManager.GetCheatingMethods(memento)   0xE50FEC
//      +0x78  CheaterConfigMemento.get_CheckSignatureTampering 0x130536C
//      +0xBC  Switcher.get_AbuseMethod
//      +0x144 get_CoinThreshold 0x1305374 vs the Storager coin balance
//      +0x1C8 get_GemThreshold  0x130537C vs the Storager gem balance
//    A granted 999,999,999 wallet trips the cached CheaterDetectParameters
//    thresholds on every launch, which is why the banner came back after v9.
//
// 2. THE DELIVERY (obfuscated, runs in the first loading scene)
//    AppsMenu.GetAbuseKey_53232de5 / _21493d18   0x1BB3530 / 0x1BB3640
//    AppsMenu.GetTerminalSceneName_4de1          0x1BB3750
//    AppsMenu.MeetTheCoroutine(scene, abuseTicks, nowTicks)  0x1BB3468
//    AppsMenu.<MeetTheCoroutine>c__Iterator0.MoveNext        0x1BB81E0
//      Random + WaitForSeconds, then SceneManager.LoadScene of the scene that
//      carries CheatDetectedBanner, deferred to a later launch.
//    Sibling key builders: Initializer 0xE36C90, MainMenuController 0xF78C70.
//
// 3. THE BANNER, METHOD BY METHOD (TypeDefIndex 3342, global namespace)
//    .ctor                 0x12CAE7C  left stock, see the note below
//    ShowAndClearProgress  0x12CAE84  refused
//    ClearAllProgress      0x12CAF4C  refused
//    Awake                 0x12CB2C4  intercepted, banner object destroyed
//    Update                0x12CB624  refused, its tail call is the wipe
//    RemoveObjects         0x12CB448  refused, only caller is Awake+0x50
//    OnExitButtonClick     0x12CB6EC  refused, tail call is Application.Quit
//    SendCheatTypeOnServer 0x12CB238  factory left stock; the work happens in
//      <SendCheatTypeOnServer>c__Iterator0.MoveNext 0x12CB7D4, which is
//      stopped instead, so the WWWForm abuse report (FriendsController.Hash +
//      Storager.getInt + Tools.CreateWww) is never sent. Only caller of the
//      factory: ClearAllProgress+0x274.
//    .cctor                0x12CB700  left stock, see the note below
//
//    Note on .ctor and .cctor: they only build the component and initialise
//    the static accID string. Refusing a constructor leaves a half-built
//    object and a null static behind for anything that touches the type, and
//    it buys nothing, because every method that could act is already refused.
//    A managed IEnumerator factory is left stock for the same reason: handing
//    back null would make StartCoroutine throw inside the caller.
//
// 4. NAMESPACES (not cosmetic)
//    il2cpp_class_from_name matches the namespace exactly. CheatDetectedBanner,
//    Switcher, AppsMenu, Initializer, MainMenuController and
//    PremiumAccountController are in the global namespace; AdsConfigManager,
//    CheaterConfigMemento and DevToDevFacade are in Rilisoft. Every target is
//    installed with its verified namespace first and the other one as a
//    fallback, so a differently generated metadata build cannot silently
//    disarm a required hook.
//
// This module still never writes save state: no Storager or PlayerPrefs write,
// no CloudSyncController push, no ownership, currency or level change. The
// persisted marks are read once, for the log. Detection inputs (balances,
// ownership, the package signature hash) stay stock.
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
using IntInstanceFn = int32_t (*)(void* self, const MethodInfo* method);
using SetBoolInstanceFn = void (*)(void* self, bool value,
                                   const MethodInfo* method);
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
using StoragerGetIntFn = int32_t (*)(void* static_context, void* key,
                                     bool suppress_editor_warnings,
                                     bool direct_read_migration,
                                     bool direct_read_migration_v2,
                                     const MethodInfo* method);

inline constexpr const char* kGlobalNs = "";
inline constexpr const char* kRilisoftNs = "Rilisoft";

inline constexpr const char* kHackDetectedKey = "HackDetected";
inline constexpr const char* kAbuseMethodKey = "AbuseMethod";

// Inert replacements for the obfuscated slot names. Nothing in the client
// knows these keys, so a stale mark in the stock slot is simply never read.
inline constexpr const char* kInertKeyAppsMenuA = "opg3d_inert_slot_a";
inline constexpr const char* kInertKeyAppsMenuB = "opg3d_inert_slot_b";
inline constexpr const char* kInertKeyInitializer = "opg3d_inert_slot_c";
inline constexpr const char* kInertKeyMainMenu = "opg3d_inert_slot_d";

// AbuseMetod.None and CheatingMethods.None are both 0. The threshold ceiling
// is int.MaxValue, so no balance this client can hold reaches it.
inline constexpr int32_t kNoAbuse = 0;
inline constexpr int32_t kNoCheating = 0;
inline constexpr int32_t kThresholdCeiling = 2147483647;

inline VoidInstanceFn g_awake = nullptr;
inline VoidInstanceFn g_update = nullptr;
inline VoidInstanceFn g_remove_objects = nullptr;
inline VoidInstanceFn g_exit_button = nullptr;
inline VoidStaticFn g_clear_all_progress = nullptr;
inline VoidStaticFn g_show_and_clear_progress = nullptr;
inline BoolInstanceFn g_report_move_next = nullptr;

inline BoolInstanceFn g_meet_move_next = nullptr;
inline IteratorFactoryFn g_meet_factory = nullptr;
inline EnumStaticFn g_abuse_method = nullptr;
inline EnumStaticArgFn g_cheating_methods = nullptr;
inline BoolInstanceFn g_signature_getter = nullptr;
inline SetBoolInstanceFn g_signature_setter = nullptr;
inline IntInstanceFn g_coin_threshold = nullptr;
inline IntInstanceFn g_gem_threshold = nullptr;
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
inline std::atomic<uint32_t> g_logged_exit{0u};
inline std::atomic<uint32_t> g_logged_report{0u};
inline std::atomic<uint32_t> g_logged_move_next{0u};
inline std::atomic<uint32_t> g_logged_factory{0u};
inline std::atomic<uint32_t> g_logged_abuse{0u};
inline std::atomic<uint32_t> g_logged_cheating{0u};
inline std::atomic<uint32_t> g_logged_signature{0u};
inline std::atomic<uint32_t> g_logged_thresholds{0u};
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

const char* alternate_namespace(const char* primary) {
    return (primary != nullptr && primary[0] == '\0') ? kRilisoftNs : kGlobalNs;
}

// Installs a hook using the namespace the dump proves, then the other one.
// hook::install is always called in optional mode so a first-attempt miss is a
// warning; the error line for a required target is emitted here instead.
bool install_managed(const char* primary_ns, const char* klass,
                     const char* method, int args_count, void* replacement_fn,
                     void** original, bool required) {
    if (hook::install({primary_ns, klass, method, args_count}, replacement_fn,
                      original)) {
        return true;
    }
    const char* fallback = alternate_namespace(primary_ns);
    if (hook::install({fallback, klass, method, args_count}, replacement_fn,
                      original)) {
        LOGW("cheat-guard: %s.%s/%d resolved in the fallback namespace '%s'",
             klass, method, args_count, fallback);
        return true;
    }
    if (required) {
        LOGE("cheat-guard: REQUIRED %s.%s/%d was not found in any known "
             "namespace", klass, method, args_count);
    }
    return false;
}

bool resolve_optional(const char* primary_ns, const char* klass,
                      const char* method, int args_count, void** out_fn,
                      const MethodInfo** out_mi) {
    void* info = il2cpp::find_method_info(primary_ns, klass, method,
                                         args_count);
    if (info == nullptr) {
        info = il2cpp::find_method_info(alternate_namespace(primary_ns), klass,
                                        method, args_count);
    }
    void* pointer = il2cpp::method_pointer(info);
    if (info == nullptr || pointer == nullptr) {
        LOGW("cheat-guard: optional call %s.%s/%d is unavailable", klass,
             method, args_count);
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

// Second line of defence for the same verdict: even if the call above is ever
// lost, its three inputs are answered with values that cannot convict.
// get_CheckSignatureTampering has exactly one caller, GetCheatingMethods+0x78.
bool hook_signature_getter(void* self, const MethodInfo* method) {
    (void)self;
    (void)method;
    if (should_log(g_logged_signature, kMaxLoggedEvents)) {
        LOGW("cheat-guard: CheaterConfigMemento.get_CheckSignatureTampering() "
             "forced to false; the package signature hash itself is left "
             "stock, only the demand to compare it is dropped");
    }
    return false;
}

// The private setter is what the parsed CheaterDetectParameters config would
// use to arm the signature comparison. Swallowing it keeps the backing field
// false without touching anything else in the memento.
void hook_signature_setter(void* self, bool value, const MethodInfo* method) {
    (void)self;
    (void)method;
    if (should_log(g_logged_signature, kMaxLoggedEvents)) {
        LOGW("cheat-guard: CheaterConfigMemento.set_CheckSignatureTampering"
             "(%d) swallowed; the flag stays false", value ? 1 : 0);
    }
}

int32_t hook_coin_threshold(void* self, const MethodInfo* method) {
    (void)self;
    (void)method;
    if (should_log(g_logged_thresholds, kMaxLoggedEvents)) {
        LOGW("cheat-guard: CheaterConfigMemento.get_CoinThreshold() raised to "
             "int.MaxValue; a granted coin balance can no longer exceed it");
    }
    return kThresholdCeiling;
}

int32_t hook_gem_threshold(void* self, const MethodInfo* method) {
    (void)self;
    (void)method;
    if (should_log(g_logged_thresholds, kMaxLoggedEvents)) {
        LOGW("cheat-guard: CheaterConfigMemento.get_GemThreshold() raised to "
             "int.MaxValue; a granted gem balance can no longer exceed it");
    }
    return kThresholdCeiling;
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
// Layer 3: every method of the banner itself
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

// Stock body is a tail call into Application.Quit(). Refusing it means a
// banner that somehow reached the screen can no longer close the game; the
// Awake interception above removes it instead.
void hook_exit_button(void* self, const MethodInfo* method) {
    (void)self;
    (void)method;
    if (should_log(g_logged_exit, kMaxLoggedEvents)) {
        LOGW("cheat-guard: CheatDetectedBanner.OnExitButtonClick() refused; "
             "Application.Quit() is not reached and the session stays alive");
    }
}

// The coroutine body behind SendCheatTypeOnServer: WWWForm with app_version,
// uniq_id, auth and block_id, FriendsController.Hash, then Tools.CreateWww to
// the update_abuse_info endpoint. Stopping the first tick keeps the report
// entirely local; the factory itself is left stock on purpose.
bool hook_report_move_next(void* self, const MethodInfo* method) {
    (void)self;
    (void)method;
    if (should_log(g_logged_report, kMaxLoggedEvents)) {
        LOGW("cheat-guard: <SendCheatTypeOnServer>c__Iterator0.MoveNext() "
             "stopped; the abuse report POST is never sent");
    }
    return false;
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

// Compiler-generated iterators are nested types, and different IL2CPP
// metadata builds expose nested names differently, so every known spelling is
// tried instead of guessing one.
bool install_iterator_block(const char* const* spellings, size_t count,
                            void* replacement_fn, void** original,
                            const char* what) {
    for (size_t i = 0; i < count; ++i) {
        if (install_managed(kGlobalNs, spellings[i], "MoveNext", 0,
                            replacement_fn, original, false)) {
            LOGI("cheat-guard: %s is blocked (%s.MoveNext)", what,
                 spellings[i]);
            return true;
        }
    }
    return false;
}

bool install_terminal_scene_block() {
    static const char* const kSpellings[] = {
        "AppsMenu/<MeetTheCoroutine>c__Iterator0",
        "<MeetTheCoroutine>c__Iterator0",
        "AppsMenu.<MeetTheCoroutine>c__Iterator0",
    };
    return install_iterator_block(kSpellings, 3u,
                                  replacement(&hook_meet_move_next),
                                  original_slot(&g_meet_move_next),
                                  "the CHEAT DETECTED scene trigger");
}

bool install_report_block() {
    static const char* const kSpellings[] = {
        "CheatDetectedBanner/<SendCheatTypeOnServer>c__Iterator0",
        "<SendCheatTypeOnServer>c__Iterator0",
        "CheatDetectedBanner.<SendCheatTypeOnServer>c__Iterator0",
    };
    return install_iterator_block(kSpellings, 3u,
                                  replacement(&hook_report_move_next),
                                  original_slot(&g_report_move_next),
                                  "the abuse report POST");
}

} // namespace detail

inline bool install_hooks() {
    // Optional calls used by the interceptions below are resolved first, so
    // the very first intercepted event can already log the persisted marks.
    if (!detail::resolve_optional(
            "", "Storager", "getInt", 4,
            reinterpret_cast<void**>(&detail::g_storager_get_int),
            &detail::g_mi_storager_get_int)) {
        detail::g_storager_get_int = nullptr;
    }
    if (!detail::resolve_optional(
            "UnityEngine", "Component", "get_gameObject", 0,
            reinterpret_cast<void**>(&detail::g_get_game_object),
            &detail::g_mi_get_game_object)) {
        detail::g_get_game_object = nullptr;
    }
    if (!detail::resolve_optional(
            "UnityEngine", "Object", "Destroy", 1,
            reinterpret_cast<void**>(&detail::g_destroy),
            &detail::g_mi_destroy)) {
        detail::g_destroy = nullptr;
    }

    // Mandatory: the two verdict functions and the four banner entry points
    // that can act on their own (Awake arms the tick, Update runs it, and the
    // two progress calls are the wipe itself).
    bool armed = detail::install_managed(
        detail::kGlobalNs, "Switcher", "get_AbuseMethod", 0,
        detail::replacement(&detail::hook_abuse_method),
        detail::original_slot(&detail::g_abuse_method), true);
    armed &= detail::install_managed(
        detail::kRilisoftNs, "AdsConfigManager", "GetCheatingMethods", 1,
        detail::replacement(&detail::hook_cheating_methods),
        detail::original_slot(&detail::g_cheating_methods), true);
    armed &= detail::install_managed(
        detail::kGlobalNs, "CheatDetectedBanner", "Awake", 0,
        detail::replacement(&detail::hook_awake),
        detail::original_slot(&detail::g_awake), true);
    armed &= detail::install_managed(
        detail::kGlobalNs, "CheatDetectedBanner", "Update", 0,
        detail::replacement(&detail::hook_update),
        detail::original_slot(&detail::g_update), true);
    armed &= detail::install_managed(
        detail::kGlobalNs, "CheatDetectedBanner", "ClearAllProgress", 0,
        detail::replacement(&detail::hook_clear_all_progress),
        detail::original_slot(&detail::g_clear_all_progress), true);
    armed &= detail::install_managed(
        detail::kGlobalNs, "CheatDetectedBanner", "ShowAndClearProgress", 0,
        detail::replacement(&detail::hook_show_and_clear_progress),
        detail::original_slot(&detail::g_show_and_clear_progress), true);
    if (!armed) {
        LOGE("cheat-guard: the local punishment path could not be "
             "neutralised; treat this build as unsafe for a real save");
        return false;
    }

    // The hook that keeps the banner off the screen. Losing it still leaves
    // the save protected, so it must not fail the module, but it is loud
    // because the overlay itself can then still appear.
    if (!detail::install_terminal_scene_block()) {
        LOGE("cheat-guard: the delayed CHEAT DETECTED scene load could not be "
             "hooked under any known nested class name; the overlay can still "
             "appear, but it can no longer erase anything");
    }
    if (!detail::install_report_block()) {
        LOGW("cheat-guard: the abuse report iterator could not be hooked; a "
             "report could still be sent if the wipe path is ever reached");
    }

    // Diagnostics for the same trigger, forwarded unchanged.
    if (!detail::install_managed(
            detail::kGlobalNs, "AppsMenu", "MeetTheCoroutine", 3,
            detail::replacement(&detail::hook_meet_factory),
            detail::original_slot(&detail::g_meet_factory), false)) {
        LOGW("cheat-guard: AppsMenu.MeetTheCoroutine could not be hooked; the "
             "log will not show when the client arms the banner");
    }

    // Obfuscated slot names, so a mark persisted by an earlier session is not
    // read any more. Best effort: each one that fails only means the stock
    // slot stays in use, and the tick above still refuses to show anything.
    if (!detail::install_managed(
            detail::kGlobalNs, "AppsMenu", "GetAbuseKey_53232de5", 1,
            detail::replacement(&detail::hook_abuse_key_apps_a),
            detail::original_slot(&detail::g_abuse_key_apps_a), false)) {
        LOGW("cheat-guard: AppsMenu.GetAbuseKey_53232de5 could not be "
             "redirected");
    }
    if (!detail::install_managed(
            detail::kGlobalNs, "AppsMenu", "GetAbuseKey_21493d18", 1,
            detail::replacement(&detail::hook_abuse_key_apps_b),
            detail::original_slot(&detail::g_abuse_key_apps_b), false)) {
        LOGW("cheat-guard: AppsMenu.GetAbuseKey_21493d18 could not be "
             "redirected");
    }
    if (!detail::install_managed(
            detail::kGlobalNs, "Initializer", "GetAbuseKey_d4d3cbab", 1,
            detail::replacement(&detail::hook_abuse_key_initializer),
            detail::original_slot(&detail::g_abuse_key_initializer), false)) {
        LOGW("cheat-guard: Initializer.GetAbuseKey_d4d3cbab could not be "
             "redirected");
    }
    if (!detail::install_managed(
            detail::kGlobalNs, "MainMenuController", "GetAbuseKey_f1a4329e", 1,
            detail::replacement(&detail::hook_abuse_key_main_menu),
            detail::original_slot(&detail::g_abuse_key_main_menu), false)) {
        LOGW("cheat-guard: MainMenuController.GetAbuseKey_f1a4329e could not "
             "be redirected");
    }

    // The remaining banner methods. None of them can act once Awake and
    // Update are refused, but each one is a separate way to touch the screen
    // or the process, so each one is refused too.
    if (!detail::install_managed(
            detail::kGlobalNs, "CheatDetectedBanner", "RemoveObjects", 0,
            detail::replacement(&detail::hook_remove_objects),
            detail::original_slot(&detail::g_remove_objects), false)) {
        LOGW("cheat-guard: CheatDetectedBanner.RemoveObjects could not be "
             "hooked; a shown banner may still destroy other scene objects");
    }
    if (!detail::install_managed(
            detail::kGlobalNs, "CheatDetectedBanner", "OnExitButtonClick", 0,
            detail::replacement(&detail::hook_exit_button),
            detail::original_slot(&detail::g_exit_button), false)) {
        LOGW("cheat-guard: CheatDetectedBanner.OnExitButtonClick could not be "
             "hooked; a shown banner could still call Application.Quit");
    }

    // The three inputs of the verdict, neutralised as a second line of
    // defence. The setter has no direct caller in this build, so a miss here
    // is expected on some metadata layouts and is not a problem.
    if (!detail::install_managed(
            detail::kRilisoftNs, "CheaterConfigMemento",
            "get_CheckSignatureTampering", 0,
            detail::replacement(&detail::hook_signature_getter),
            detail::original_slot(&detail::g_signature_getter), false)) {
        LOGW("cheat-guard: CheaterConfigMemento.get_CheckSignatureTampering "
             "could not be hooked; the forced-clean verdict above still "
             "covers it");
    }
    if (!detail::install_managed(
            detail::kRilisoftNs, "CheaterConfigMemento",
            "set_CheckSignatureTampering", 1,
            detail::replacement(&detail::hook_signature_setter),
            detail::original_slot(&detail::g_signature_setter), false)) {
        LOGW("cheat-guard: CheaterConfigMemento.set_CheckSignatureTampering "
             "could not be hooked (it has no direct caller in 13.2.1)");
    }
    if (!detail::install_managed(
            detail::kRilisoftNs, "CheaterConfigMemento", "get_CoinThreshold",
            0, detail::replacement(&detail::hook_coin_threshold),
            detail::original_slot(&detail::g_coin_threshold), false)) {
        LOGW("cheat-guard: CheaterConfigMemento.get_CoinThreshold could not be "
             "hooked");
    }
    if (!detail::install_managed(
            detail::kRilisoftNs, "CheaterConfigMemento", "get_GemThreshold", 0,
            detail::replacement(&detail::hook_gem_threshold),
            detail::original_slot(&detail::g_gem_threshold), false)) {
        LOGW("cheat-guard: CheaterConfigMemento.get_GemThreshold could not be "
             "hooked");
    }

    // Same machinery, different consequences.
    if (!detail::install_managed(
            detail::kRilisoftNs, "DevToDevFacade", "set_UserIsCheater", 1,
            detail::replacement(&detail::hook_user_is_cheater),
            detail::original_slot(&detail::g_user_is_cheater), false)) {
        LOGW("cheat-guard: DevToDevFacade.set_UserIsCheater could not be "
             "hooked; the analytics cheater flag stays stock");
    }
    if (!detail::install_managed(
            detail::kGlobalNs, "PremiumAccountController", "CheckTimeHack", 0,
            detail::replacement(&detail::hook_check_time_hack),
            detail::original_slot(&detail::g_check_time_hack), false)) {
        LOGW("cheat-guard: PremiumAccountController.CheckTimeHack could not be "
             "hooked; a clock difference can still stop a rented account");
    }

    LOGI("cheat-guard: armed (verdicts=forced clean, banner scene=never "
         "loaded, every banner method=refused, abuse report=blocked, abuse "
         "slots=inert, PlayerPrefs/Storager/CloudSync writes=none, detection "
         "inputs=stock)");
    return true;
}

} // namespace cheat_guard
