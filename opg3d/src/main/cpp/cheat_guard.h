#pragma once

#include <atomic>
#include <cstdint>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Local "CHEAT DETECTED" punishment blocked.
//
// Detaching the client from the dead backend does not disarm everything: the
// APK still ships a purely local punishment path that erases the save. It is a
// single MonoBehaviour, and every step of it is visible in the 13.2.1 dump:
//
//   CheatDetectedBanner : MonoBehaviour
//     const string HackDetectedKey = "HackDetected"
//     private bool skipFrame, progressCleared
//
//     Update()                       RVA 0x12CB624
//       +0xA0  b   ClearAllProgress()                    ; tail call
//
//     ClearAllProgress()             RVA 0x12CAF4C
//       +0x98  bl  Storager.getString(string)            RVA 0xEBD8D8
//       +0xEC  bl  PlayerPrefs.DeleteAll()               RVA 0x1DA5640
//       +0xF8  bl  PlayerPrefs.Save()                    RVA 0x1DA56D0
//       +0x15C bl  Storager.setInt(string,int,...)       RVA 0xEB74A0
//       +0x1B4 bl  Storager.setString(string,string)     RVA 0xEBD760
//       +0x248 bl  CloudSyncController.ApplyChanges(bool) RVA 0x132A5FC
//       +0x274 bl  SendCheatTypeOnServer()               RVA 0x12CB238
//
//     ShowAndClearProgress()         RVA 0x12CAE84
//       +0x80  bl  PhotonNetwork.Disconnect()            RVA 0x6E6344
//       +0xA4  b   SceneManager.LoadScene(string)        RVA 0x1DB0D4C
//
//     Awake()                        RVA 0x12CB2C4
//       +0x50  bl  RemoveObjects()                       RVA 0x12CB448
//       +0x90  bl  ConnectScene.MainLoadingTexture()     RVA 0x112E58C
//
//     RemoveObjects()                RVA 0x12CB448
//              bl  Object.Destroy(Object)                RVA 0x1D9F274
//              ; walks Transform.root of every found object and destroys it
//
//     OnExitButtonClick()            RVA 0x12CB6EC
//              b   Application.Quit()                    RVA 0x1B2A33C
//
//     SendCheatTypeOnServer()        RVA 0x12CB238  (iterator MoveNext
//              0x12CB7D4) builds a WWWForm with app_version / uniq_id /
//              auth (FriendsController.Hash) plus Storager.getInt and posts it
//              to FriendsController.actionAddress as an abuse report.
//
// Two whole-binary scans of the ARM .text section (every direct B/BL) pin the
// shape of that graph down:
//
//   * PlayerPrefs.DeleteAll has exactly six callers in the whole client, and
//     only one of them is cheat-driven: ClearAllProgress+0xEC. The other five
//     are ordinary wrappers (KeychainCleaner.Clear, P31Prefs.removeAll,
//     Save.DeleteAll, CryptoPlayerPrefs.DeleteAll, CustomHungerBase).
//   * ClearAllProgress has exactly one caller: Update()+0xA0.
//   * SendCheatTypeOnServer has exactly one caller: ClearAllProgress+0x274.
//   * ShowAndClearProgress has no direct caller at all; it is reached
//     indirectly, so it is neutralised as insurance rather than as the fix.
//
// So the punishment is intercepted at the banner, three methods deep, and
// nothing else in the client loses a code path it legitimately uses.
//
// Why the detector itself is left alone. The client's cheat criteria are
// server-driven data, not code we can safely invert:
//
//   Rilisoft.CheatingMethods { None=0, SignatureTampering=1, CoinThreshold=2,
//                              GemThreshold=4 }
//   Rilisoft.CheaterConfigMemento { CheckSignatureTampering, CoinThreshold,
//                                   GemThreshold }
//   AdsConfigManager.GetCheatingMethods(AdsConfigMemento)  RVA 0xE50FEC
//     single caller: GetPlayerCategory+0x118
//   FriendsController.NewCheaterDetectParametersAvailable  (Action<int,int,
//     int,int>, fed by the cached "CheaterDetectParameters" config)
//
// The coin/gem thresholds are exactly what a private server's granted balance
// trips, and the methods that read the balance are ordinary getters shared
// with the shop, the bank and the HUD. Forcing them would corrupt real game
// state, so this module lets detection think whatever it likes and removes
// only its ability to act.
//
// This module never writes save state. It does not clear the persisted
// "HackDetected" mark, it does not touch Storager, PlayerPrefs, ownership,
// currency or CloudSyncController; the mark is only read once, for the log, so
// a device report can show whether an earlier wipe already left it behind.
namespace cheat_guard {
namespace detail {

using MethodInfo = void;

// Old-IL2CPP ARM32 ABI: instance methods take the object in r0, static
// generated methods take a hidden null context there instead; managed
// arguments follow, then MethodInfo*.
using VoidInstanceFn = void (*)(void* self, const MethodInfo* method);
using VoidStaticFn = void (*)(void* static_context, const MethodInfo* method);
using GetGameObjectFn = void* (*)(void* self, const MethodInfo* method);
using DestroyFn = void (*)(void* static_context, void* object,
                           const MethodInfo* method);
using StoragerGetIntFn = int32_t (*)(void* static_context, void* key,
                                     bool suppress_editor_warnings,
                                     bool direct_read_migration,
                                     bool direct_read_migration_v2,
                                     const MethodInfo* method);

inline constexpr const char* kHackDetectedKey = "HackDetected";

inline VoidInstanceFn g_awake = nullptr;
inline VoidInstanceFn g_update = nullptr;
inline VoidInstanceFn g_remove_objects = nullptr;
inline VoidStaticFn g_clear_all_progress = nullptr;
inline VoidStaticFn g_show_and_clear_progress = nullptr;

inline GetGameObjectFn g_get_game_object = nullptr;
inline const MethodInfo* g_mi_get_game_object = nullptr;
inline DestroyFn g_destroy = nullptr;
inline const MethodInfo* g_mi_destroy = nullptr;
inline StoragerGetIntFn g_storager_get_int = nullptr;
inline const MethodInfo* g_mi_storager_get_int = nullptr;

// Update() runs every frame while the banner exists, so every log site is
// capped. The first lines are the interesting ones: they prove which entry
// point the client actually tried to use.
inline constexpr uint32_t kMaxLoggedEvents = 8;

inline std::atomic<uint32_t> g_logged_update{0u};
inline std::atomic<uint32_t> g_logged_clear{0u};
inline std::atomic<uint32_t> g_logged_show{0u};
inline std::atomic<uint32_t> g_logged_awake{0u};
inline std::atomic<uint32_t> g_logged_remove{0u};
inline std::atomic<bool> g_flag_probed{false};

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

// Read-only, once per process, on the Unity thread that already runs the
// banner. Tells a device report whether an earlier wipe left the mark set.
void probe_persisted_mark() {
    bool expected = false;
    if (!g_flag_probed.compare_exchange_strong(expected, true,
                                               std::memory_order_acq_rel)) {
        return;
    }
    if (g_storager_get_int == nullptr || il2cpp::string_new == nullptr) {
        LOGW("cheat-guard: Storager.getInt is unavailable; the persisted '%s' "
             "mark was not read (nothing is written either way)",
             kHackDetectedKey);
        return;
    }
    void* key = il2cpp::string_new(kHackDetectedKey);
    if (key == nullptr) return;
    const int32_t value = g_storager_get_int(nullptr, key, false, false, false,
                                             g_mi_storager_get_int);
    LOGI("cheat-guard: persisted '%s' mark reads %d (read-only; this module "
         "never writes save state)", kHackDetectedKey, value);
}

// The only caller of ClearAllProgress. Never forwarded.
void hook_update(void* self, const MethodInfo* method) {
    (void)self;
    (void)method;
    if (should_log(g_logged_update, kMaxLoggedEvents)) {
        LOGW("cheat-guard: CheatDetectedBanner.Update() suppressed; its tail "
             "call into ClearAllProgress (PlayerPrefs.DeleteAll + Storager "
             "marks + cloud push + abuse report) never runs");
    }
    probe_persisted_mark();
}

// Defence in depth: the wipe body itself, in case it is ever reached other
// than through Update().
void hook_clear_all_progress(void* static_context, const MethodInfo* method) {
    (void)static_context;
    (void)method;
    if (should_log(g_logged_clear, kMaxLoggedEvents)) {
        LOGW("cheat-guard: CheatDetectedBanner.ClearAllProgress() refused; "
             "local progress, Storager marks and CloudSyncController are left "
             "exactly as they were");
    }
    probe_persisted_mark();
}

// Disconnects Photon and loads the banner scene in stock code. No direct
// caller exists in this build, so this is insurance only.
void hook_show_and_clear_progress(void* static_context,
                                  const MethodInfo* method) {
    (void)static_context;
    (void)method;
    if (should_log(g_logged_show, kMaxLoggedEvents)) {
        LOGW("cheat-guard: CheatDetectedBanner.ShowAndClearProgress() "
             "refused; the Photon disconnect and the banner scene load are "
             "both skipped");
    }
    probe_persisted_mark();
}

// Stock Awake() wires the full-screen overlay and calls RemoveObjects(), which
// destroys the root of every other object it finds. Instead of running that,
// the banner instance removes itself so play continues.
void hook_awake(void* self, const MethodInfo* method) {
    (void)method;
    const bool log_this = should_log(g_logged_awake, kMaxLoggedEvents);
    if (log_this) {
        LOGW("cheat-guard: CheatDetectedBanner.Awake() intercepted; the stock "
             "body would tear down the rest of the scene and arm the wipe "
             "tick");
    }
    probe_persisted_mark();

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

// Only Awake() calls this in stock code, but it is the method that actually
// destroys unrelated scene objects, so it is neutralised as well.
void hook_remove_objects(void* self, const MethodInfo* method) {
    (void)self;
    (void)method;
    if (should_log(g_logged_remove, kMaxLoggedEvents)) {
        LOGW("cheat-guard: CheatDetectedBanner.RemoveObjects() refused; no "
             "scene object is destroyed");
    }
}

} // namespace detail

inline bool install_hooks() {
    // Mandatory: the frame tick that starts the wipe, the wipe body, and the
    // show-and-wipe entry point.
    bool armed = hook::install(
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
        LOGE("cheat-guard: the local progress-wipe path could not be "
             "neutralised; treat this build as unsafe for a real save");
        return false;
    }

    // Optional: cosmetics and scene safety. Failing these still leaves the
    // save protected, so they must not fail the module.
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

    // Optional calls used by the interceptions above.
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
    if (!detail::resolve_optional(
            {"", "Storager", "getInt", 4},
            reinterpret_cast<void**>(&detail::g_storager_get_int),
            &detail::g_mi_storager_get_int)) {
        detail::g_storager_get_int = nullptr;
    }

    LOGI("cheat-guard: armed (scope=CheatDetectedBanner only, "
         "PlayerPrefs/Storager/CloudSync writes=none, detection inputs=stock, "
         "persisted mark=read-only)");
    return true;
}

} // namespace cheat_guard
