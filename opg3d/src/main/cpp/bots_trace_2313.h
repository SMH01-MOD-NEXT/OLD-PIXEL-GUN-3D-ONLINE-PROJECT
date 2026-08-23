#pragma once

#include <cinttypes>
#include <cstdint>
#include <mutex>
#include <string>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Passive diagnostics for 23.1.3 bots: how strong they are allowed to be, and
// what they are actually given.
//
// Why this is a trace and not yet a buff. The dump proves the bots are far more
// capable than what shows up in a match, but the numbers that decide it are
// DATA, not code, and the data source is the retired backend:
//
//   PGCompany.DataObjects.<loadout>            [MessagePackObject/JsonProperty]
//     .IntBotsLoadout : Dictionary<int, <botConfig>>
//
//   <botConfig> fields (all server-driven):
//     BotIsEnable, MinBotsNumber, MaxBotsNumber, WeaponDamage, Speed, Health,
//     Armor, Equip : List<string>, PlayerBotSpawnEquip, GadgetBotUsage,
//     FakeShot, PlayerBotChaising, ChanceToChoosePlayer, DodgeInMelee,
//     DodgeInRange, JumpInMelee, JumpInRange, MinRandomValueForAim,
//     MaxRandomValueForAim, PauseBetweenShots, SwitchTarget,
//     SwitchTargetWhenBeenAttacked, ChangeWeaponWhenOutOfAmmo,
//     ChangeWeaponToMeleeInCloseRange, MinTurnRate, MaxTurnRate,
//     RotateWhenStand, AggroRadius, ChaisingTime
//
// The client-side mirror is <aiConfig>.AILevelSettings, keyed by an integer AI
// level, and it carries the weapon list itself:
//
//   AILevelSettings: weapons : List<string>, minRandomValueForAim,
//     maxRandomValueForAim, attackRange, rotationSpeedMinSkirmish,
//     rotationSpeedMaxSkirmish, dodge, dodgeWithMelee, jumpOnDodje,
//     jumpOnDodjeWithMelee, straightDodje, switchOnTarget,
//     switchOnTargetWhenHaveTarget, takeMeleeWeapon, goForBonus,
//     rotateWhenStand, choosePlayerAsTarget, doDamageOnShoot,
//     changeWeaponOnReload, pauseOnShoot, throwGrenade
//
// So "bots always carry the starter rifle" is almost certainly the fallback AI
// level being selected, because the per-level table that would grant better
// weapons never arrives. What cannot be known statically is how many AI levels
// this build actually ships and which one is being handed out - the dictionary
// is populated at runtime from a JSON payload. Raising the level blindly would
// either do nothing or index a level that does not exist.
//
// This module therefore reports, per match: the AI level assigned to each bot,
// the behavior transitions, and the spawn parameters (including the weapon id).
// It changes nothing.
//
// RVAs for review only; everything resolves by metadata name at runtime:
//   PlayerBotsManager.<spawnWithEquip>(string,string,string)   0x239E0EC
//   PlayerBotsManager/SavedAiLevels.<apply>(int[],int[],int[]) 0x23A1030
//   AIBotController.<setAiLevel>(int)                          0x2143514
//   AIBotController.<setBehavior>(behaviorEnum)                0x2145C68
//   <aiConfig>.<aiLevelSettings>()                             0x455566C
// Behavior enum: None=0, Walking=1, Searching=2, Skirmish=3.
namespace bots_trace_2313 {
namespace detail {

using MethodInfo = void;
using ManagedString = void;

using ThreeStringFn = void (*)(void*, ManagedString*, ManagedString*,
                              ManagedString*, const MethodInfo*);
using ThreeObjectFn = void (*)(void*, void*, void*, void*, const MethodInfo*);
using InstanceIntFn = void (*)(void*, int32_t, const MethodInfo*);
using InstanceObjFn = void* (*)(void*, const MethodInfo*);

inline constexpr const char* kBotsManager = "PlayerBotsManager";
inline constexpr const char* kSavedAiLevels = "PlayerBotsManager/SavedAiLevels";
inline constexpr const char* kAiBotController = "AIBotController";

// Obfuscated metadata names, copied verbatim from dump2313.cs.
inline constexpr const char* kSpawnWithEquip = u8"丑丁下万丐三业且下";
inline constexpr const char* kApplyAiLevels = u8"与专丈丐七七丝下丌";
inline constexpr const char* kSetAiLevel = u8"丆且业丞丒丌丈丆丞";
inline constexpr const char* kSetBehavior = u8"丌丝丒丏不丌丕丂且";
inline constexpr const char* kAiConfigClass = u8"丘丄丅丞丘东业且七";
inline constexpr const char* kAiLevelSettings = u8"万东上不丑且七与丑";

inline constexpr size_t kMaxStringChars = 128u;
// Behavior changes are frequent; keep the log readable.
inline constexpr uint32_t kBehaviorLogInterval = 25u;

inline ThreeStringFn g_spawn_with_equip = nullptr;
inline ThreeObjectFn g_apply_ai_levels = nullptr;
inline InstanceIntFn g_set_ai_level = nullptr;
inline InstanceIntFn g_set_behavior = nullptr;
inline InstanceObjFn g_ai_level_settings = nullptr;

inline std::mutex g_mutex;
inline uint32_t g_spawns = 0u;
inline uint32_t g_behavior_changes = 0u;
inline uint32_t g_settings_queries = 0u;

inline const char* behavior_name(int32_t value) {
    switch (value) {
        case 0: return "None";
        case 1: return "Walking";
        case 2: return "Searching";
        case 3: return "Skirmish";
        default: return "<unknown>";
    }
}

void spawn_with_equip_hook(void* self, ManagedString* first,
                           ManagedString* second, ManagedString* third,
                           const MethodInfo* method) {
    uint32_t index = 0u;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        index = ++g_spawns;
    }
    const std::string a = il2cpp::to_utf8(first, kMaxStringChars);
    const std::string b = il2cpp::to_utf8(second, kMaxStringChars);
    const std::string c = il2cpp::to_utf8(third, kMaxStringChars);
    LOGI("23.1.3-bots: spawn #%" PRIu32 " equip='%s' / '%s' / '%s'",
         index, a.c_str(), b.c_str(), c.c_str());

    if (g_spawn_with_equip == nullptr) {
        LOGE("23.1.3-bots: spawn hook has no saved original; spawn dropped");
        return;
    }
    g_spawn_with_equip(self, first, second, third, method);
}

void apply_ai_levels_hook(void* self, void* teammates, void* enemies,
                          void* deathmatch, const MethodInfo* method) {
    // The arrays are intentionally not decoded: the il2cpp array layout is not
    // part of this project's verified contract, and presence is enough to tell
    // whether the level tables are ever applied.
    LOGI("23.1.3-bots: AI level tables applied (teammates=%s enemies=%s "
         "deathmatch=%s)",
         teammates != nullptr ? "present" : "null",
         enemies != nullptr ? "present" : "null",
         deathmatch != nullptr ? "present" : "null");

    if (g_apply_ai_levels == nullptr) {
        LOGE("23.1.3-bots: AI level hook has no saved original");
        return;
    }
    g_apply_ai_levels(self, teammates, enemies, deathmatch, method);
}

void set_ai_level_hook(void* self, int32_t level, const MethodInfo* method) {
    LOGI("23.1.3-bots: bot %p assigned AI level %" PRId32, self, level);
    if (g_set_ai_level == nullptr) {
        LOGE("23.1.3-bots: AI level setter has no saved original");
        return;
    }
    g_set_ai_level(self, level, method);
}

void set_behavior_hook(void* self, int32_t behavior,
                       const MethodInfo* method) {
    uint32_t index = 0u;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        index = ++g_behavior_changes;
    }
    if ((index % kBehaviorLogInterval) == 1u) {
        LOGI("23.1.3-bots: bot %p behavior -> %s (change #%" PRIu32 ")",
             self, behavior_name(behavior), index);
    }
    if (g_set_behavior == nullptr) {
        LOGE("23.1.3-bots: behavior setter has no saved original");
        return;
    }
    g_set_behavior(self, behavior, method);
}

void* ai_level_settings_hook(void* self, const MethodInfo* method) {
    if (g_ai_level_settings == nullptr) {
        LOGE("23.1.3-bots: AI settings getter has no saved original");
        return nullptr;
    }
    void* result = g_ai_level_settings(self, method);

    uint32_t index = 0u;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        index = ++g_settings_queries;
    }
    if (index == 1u) {
        LOGI("23.1.3-bots: AI level settings table first queried: %s",
             result != nullptr ? "present" : "NULL (no per-level data loaded)");
    }
    return result;
}

inline bool add(const hook::ManagedMethod& method, void* replacement_pointer,
                void** original_pointer, int* installed) {
    const bool ok = hook::install(method, replacement_pointer,
                                 original_pointer, false);
    if (ok) ++(*installed);
    else LOGW("23.1.3-bots: could not hook %s.%s", method.klass, method.method);
    return ok;
}

} // namespace detail

// Installs the passive bot trace. Returns true when the two hooks that answer
// the actual question - which AI level a bot gets, and what it is equipped
// with - are in place.
inline bool install_hooks() {
#if defined(__ANDROID__)
    static_assert(sizeof(void*) == 8,
                  "PG3D 23.1.3 target must be arm64-v8a");
#endif
    using namespace detail;
    int installed = 0;

    const bool spawn = add({"", kBotsManager, kSpawnWithEquip, 3},
                          reinterpret_cast<void*>(&spawn_with_equip_hook),
                          reinterpret_cast<void**>(&g_spawn_with_equip),
                          &installed);
    const bool level = add({"", kAiBotController, kSetAiLevel, 1},
                          reinterpret_cast<void*>(&set_ai_level_hook),
                          reinterpret_cast<void**>(&g_set_ai_level),
                          &installed);
    add({"", kSavedAiLevels, kApplyAiLevels, 3},
        reinterpret_cast<void*>(&apply_ai_levels_hook),
        reinterpret_cast<void**>(&g_apply_ai_levels), &installed);
    add({"", kAiBotController, kSetBehavior, 1},
        reinterpret_cast<void*>(&set_behavior_hook),
        reinterpret_cast<void**>(&g_set_behavior), &installed);
    add({"", kAiConfigClass, kAiLevelSettings, 0},
        reinterpret_cast<void*>(&ai_level_settings_hook),
        reinterpret_cast<void**>(&g_ai_level_settings), &installed);

    LOGI("23.1.3-bots-trace: installed %d/5 hooks (spawn-equip=%s "
         "ai-level=%s)",
         installed, spawn ? "OK" : "FAILED", level ? "OK" : "FAILED");
    return spawn && level;
}

} // namespace bots_trace_2313
