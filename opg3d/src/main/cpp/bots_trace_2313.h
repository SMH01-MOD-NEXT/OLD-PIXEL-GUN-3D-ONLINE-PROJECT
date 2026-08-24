#pragma once

#include <cinttypes>
#include <cstdint>
#include <mutex>
#include <string>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Runtime diagnostics and high-rank loadout repair for 23.1.3 multiplayer
// bots. The trace proves which creation path runs; the repair assigns the
// highest AI tier shipped by this exact client so regular bots select the
// weapon table associated with a high-level player profile.
//
// Revision 2, driven by a real device log. The first revision proved two
// things, both of which are corrected here:
//
//   1. AIBotController is NOT a global-namespace type. The dump declares it as
//        // Namespace: PlayerBot
//        internal class AIBotController : MonoBehaviour
//      so resolving it with an empty namespace fails, and both AI-level hooks
//      were reported as "optional method not found". The method names and arity
//      were correct; only the namespace was wrong.
//
//   2. Over a full online match not a single bot event fired: no spawn, no AI
//      level table application, and the per-level settings getter was never
//      queried. Hooking the equip helper alone cannot distinguish "no bots were
//      created" from "bots were created through another path". This revision
//      therefore instruments the whole creation chain instead of its tail:
//
//        PlayerBotsManager.Awake / Start        -> does the manager exist?
//        PlayerBotController.<factory>(PhotonView) -> is a bot object created?
//        PlayerBotsManager.<register>(bot, int) -> is it registered + at what?
//        PlayerBotsManager.<instantiate>(dict)  -> is a prefab instantiated?
//        AIBotController.Awake                  -> does the AI component wake?
//        AIBotController.<setAiLevel>(int)       -> which level is handed out?
//        AIBotController.<setBehavior>(enum)     -> does the AI actually tick?
//
// The complete bot tuning model is still data-driven. This matters for future
// work on health, aim and movement, but the latest device trace proves that
// standard bots already spawn and run; only their local AI tier assignment is
// skipped. The retired backend model is:
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
// BotIsEnable and MinBotsNumber/MaxBotsNumber can still affect other modes,
// but they are not the cause in the reported deathmatch: six bot prefabs and
// AIBotController.Awake calls were observed while the AI-level setter remained
// completely silent.
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
// Revision 3 resolves the remaining ambiguity from the device trace and the
// A64 caller graph. PlayerBotsManager clamps the stock value to 3 before it
// calls AIBotController.<setAiLevel>, and the weapon-list selector indexes its
// local AILevelSettings dictionary with that field. The reported match never
// called the setter, leaving every bot at default tier 0. Tier 3 is therefore
// the verified high-rank client tier; 65 would be an invalid index here.
//
// RVAs for review only; everything resolves by metadata name at runtime:
//   PlayerBotsManager.Awake                                    0x239D6E4
//   PlayerBotsManager.Start                                    0x239D9B0
//   PlayerBotsManager.<register>(PlayerBotController,int)       0x239DCBC
//   PlayerBotsManager.<instantiate>(Dictionary<string,object>)  0x239E53C
//   PlayerBotsManager.<spawnWithEquip>(string,string,string)    0x239E0EC
//   PlayerBotsManager/SavedAiLevels.<apply>(int[],int[],int[])  0x23A1030
//   PlayerBotController.<factory>(PhotonView)  [static]         0x3C6CF3C
//   PlayerBot.AIBotController.Awake                            0x2143C48
//   PlayerBot.AIBotController.<setAiLevel>(int)                 0x2143514
//   PlayerBot.AIBotController.<setBehavior>(behaviorEnum)       0x2145C68
//   <aiConfig>.<aiLevelSettings>()                              0x455566C
// Behavior enum: None=0, Walking=1, Searching=2, Skirmish=3.
namespace bots_trace_2313 {
namespace detail {

using MethodInfo = void;
using ManagedString = void;

using InstanceVoidFn = void (*)(void*, const MethodInfo*);
using ThreeStringFn = void (*)(void*, ManagedString*, ManagedString*,
                              ManagedString*, const MethodInfo*);
using ThreeObjectFn = void (*)(void*, void*, void*, void*, const MethodInfo*);
using InstanceIntFn = void (*)(void*, int32_t, const MethodInfo*);
using InstanceObjFn = void* (*)(void*, const MethodInfo*);
using RegisterFn = void (*)(void*, void*, int32_t, const MethodInfo*);
using InstantiateFn = void* (*)(void*, void*, const MethodInfo*);
// Static factory: no implicit self argument.
using StaticFactoryFn = void* (*)(void*, const MethodInfo*);

inline constexpr const char* kGlobalNs = "";
// AIBotController lives in the PlayerBot namespace; resolving it globally
// silently fails. Verified against dump2313.cs.
inline constexpr const char* kBotNs = "PlayerBot";

inline constexpr const char* kBotsManager = "PlayerBotsManager";
inline constexpr const char* kSavedAiLevels = "PlayerBotsManager/SavedAiLevels";
inline constexpr const char* kBotController = "PlayerBotController";
inline constexpr const char* kAiBotController = "AIBotController";

// Obfuscated metadata names, copied verbatim from dump2313.cs.
inline constexpr const char* kSpawnWithEquip = u8"丑丁下万丐三业且下";
inline constexpr const char* kApplyAiLevels = u8"与专丈丐七七丝下丌";
inline constexpr const char* kRegisterBot = u8"丁丌不丝丏丑丏东专";
inline constexpr const char* kInstantiateBot = u8"丄丈丝丐丁万丟丑万";
inline constexpr const char* kBotFactory = u8"丌业上丛万丐丑东丅";
inline constexpr const char* kSetAiLevel = u8"丆且业丞丒丌丈丆丞";
inline constexpr const char* kSetBehavior = u8"丌丝丒丏不丌丕丂且";
inline constexpr const char* kAiConfigClass = u8"丘丄丅丞丘东业且七";
inline constexpr const char* kAiLevelSettings = u8"万东上不丑且七与丑";

inline constexpr size_t kMaxStringChars = 128u;
// Behavior changes are frequent; keep the log readable.
inline constexpr uint32_t kBehaviorLogInterval = 25u;
// Bot AI tiers are 0..3 in 23.1.3. This is not the player rank scale.
inline constexpr int32_t kHighRankAiLevel = 3;

inline InstanceVoidFn g_manager_awake = nullptr;
inline InstanceVoidFn g_manager_start = nullptr;
inline InstanceVoidFn g_ai_awake = nullptr;
inline ThreeStringFn g_spawn_with_equip = nullptr;
inline ThreeObjectFn g_apply_ai_levels = nullptr;
inline RegisterFn g_register_bot = nullptr;
inline InstantiateFn g_instantiate_bot = nullptr;
inline StaticFactoryFn g_bot_factory = nullptr;
inline InstanceIntFn g_set_ai_level = nullptr;
inline const MethodInfo* g_set_ai_level_info = nullptr;
inline InstanceIntFn g_set_behavior = nullptr;
inline InstanceObjFn g_ai_level_settings = nullptr;

inline std::mutex g_mutex;
inline uint32_t g_spawns = 0u;
inline uint32_t g_registrations = 0u;
inline uint32_t g_instantiations = 0u;
inline uint32_t g_factory_calls = 0u;
inline uint32_t g_ai_awakes = 0u;
inline uint32_t g_behavior_changes = 0u;
inline uint32_t g_settings_queries = 0u;

inline uint32_t bump(uint32_t* counter) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return ++(*counter);
}

inline const char* behavior_name(int32_t value) {
    switch (value) {
        case 0: return "None";
        case 1: return "Walking";
        case 2: return "Searching";
        case 3: return "Skirmish";
        default: return "<unknown>";
    }
}

void manager_awake_hook(void* self, const MethodInfo* method) {
    LOGI("23.1.3-bots: PlayerBotsManager.Awake self=%p (the bot subsystem "
         "exists in this scene)", self);
    if (g_manager_awake == nullptr) {
        LOGE("23.1.3-bots: manager Awake has no saved original");
        return;
    }
    g_manager_awake(self, method);
}

void manager_start_hook(void* self, const MethodInfo* method) {
    LOGI("23.1.3-bots: PlayerBotsManager.Start self=%p", self);
    if (g_manager_start == nullptr) {
        LOGE("23.1.3-bots: manager Start has no saved original");
        return;
    }
    g_manager_start(self, method);
}

void* bot_factory_hook(void* photon_view, const MethodInfo* method) {
    const uint32_t index = bump(&g_factory_calls);
    if (g_bot_factory == nullptr) {
        LOGE("23.1.3-bots: bot factory has no saved original");
        return nullptr;
    }
    void* created = g_bot_factory(photon_view, method);
    LOGI("23.1.3-bots: bot object created #%" PRIu32 " view=%p bot=%p",
         index, photon_view, created);
    return created;
}

void register_bot_hook(void* self, void* bot, int32_t value,
                       const MethodInfo* method) {
    const uint32_t index = bump(&g_registrations);
    LOGI("23.1.3-bots: bot registered #%" PRIu32 " bot=%p value=%" PRId32,
         index, bot, value);
    if (g_register_bot == nullptr) {
        LOGE("23.1.3-bots: register hook has no saved original");
        return;
    }
    g_register_bot(self, bot, value, method);
}

void* instantiate_bot_hook(void* self, void* parameters,
                           const MethodInfo* method) {
    const uint32_t index = bump(&g_instantiations);
    if (g_instantiate_bot == nullptr) {
        LOGE("23.1.3-bots: instantiate hook has no saved original");
        return nullptr;
    }
    void* created = g_instantiate_bot(self, parameters, method);
    LOGI("23.1.3-bots: bot prefab instantiated #%" PRIu32 " params=%s obj=%p",
         index, parameters != nullptr ? "present" : "null", created);
    return created;
}

void ai_awake_hook(void* self, const MethodInfo* method) {
    const uint32_t index = bump(&g_ai_awakes);
    LOGI("23.1.3-bots: AIBotController.Awake #%" PRIu32 " self=%p",
         index, self);
    if (g_ai_awake == nullptr) {
        LOGE("23.1.3-bots: AI Awake has no saved original");
        return;
    }
    g_ai_awake(self, method);

    // The retired loadout transaction never reaches the stock manager call
    // that assigns this field. Apply the verified maximum tier immediately
    // after Awake, before the bot begins Walking/Searching/Skirmish updates.
    if (self != nullptr && g_set_ai_level != nullptr &&
        g_set_ai_level_info != nullptr) {
        g_set_ai_level(self, kHighRankAiLevel, g_set_ai_level_info);
        LOGW("23.1.3-bots: bot %p forced to high-rank AI tier %" PRId32
             " after Awake #%" PRIu32,
             self, kHighRankAiLevel, index);
    } else {
        LOGE("23.1.3-bots: cannot assign high-rank AI tier after Awake #%"
             PRIu32, index);
    }
}

void spawn_with_equip_hook(void* self, ManagedString* first,
                           ManagedString* second, ManagedString* third,
                           const MethodInfo* method) {
    const uint32_t index = bump(&g_spawns);
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
    if (g_set_ai_level == nullptr) {
        LOGE("23.1.3-bots: AI level setter has no saved original");
        return;
    }
    if (level != kHighRankAiLevel) {
        LOGW("23.1.3-bots: bot %p AI tier %" PRId32 " -> %" PRId32
             " (high-rank weapon table)",
             self, level, kHighRankAiLevel);
    } else {
        LOGI("23.1.3-bots: bot %p retained high-rank AI tier %" PRId32,
             self, kHighRankAiLevel);
    }
    g_set_ai_level(self, kHighRankAiLevel, method);
}

void set_behavior_hook(void* self, int32_t behavior,
                       const MethodInfo* method) {
    const uint32_t index = bump(&g_behavior_changes);
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

    const uint32_t index = bump(&g_settings_queries);
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
    else LOGW("23.1.3-bots: could not hook %s%s%s.%s",
              method.namespaze, (method.namespaze[0] != '\0') ? "." : "",
              method.klass, method.method);
    return ok;
}

} // namespace detail

// Installs the bot trace and high-rank AI-tier repair. Readiness requires the
// factory trace and the setter whose saved original is used after every Awake.
inline bool install_hooks() {
#if defined(__ANDROID__)
    static_assert(sizeof(void*) == 8,
                  "PG3D 23.1.3 target must be arm64-v8a");
#endif
    using namespace detail;
    int installed = 0;

    // Creation chain, in the order it runs.
    add({kGlobalNs, kBotsManager, "Awake", 0},
        reinterpret_cast<void*>(&manager_awake_hook),
        reinterpret_cast<void**>(&g_manager_awake), &installed);
    add({kGlobalNs, kBotsManager, "Start", 0},
        reinterpret_cast<void*>(&manager_start_hook),
        reinterpret_cast<void**>(&g_manager_start), &installed);

    const bool factory = add({kGlobalNs, kBotController, kBotFactory, 1},
                             reinterpret_cast<void*>(&bot_factory_hook),
                             reinterpret_cast<void**>(&g_bot_factory),
                             &installed);

    add({kGlobalNs, kBotsManager, kRegisterBot, 2},
        reinterpret_cast<void*>(&register_bot_hook),
        reinterpret_cast<void**>(&g_register_bot), &installed);
    add({kGlobalNs, kBotsManager, kInstantiateBot, 1},
        reinterpret_cast<void*>(&instantiate_bot_hook),
        reinterpret_cast<void**>(&g_instantiate_bot), &installed);
    add({kGlobalNs, kBotsManager, kSpawnWithEquip, 3},
        reinterpret_cast<void*>(&spawn_with_equip_hook),
        reinterpret_cast<void**>(&g_spawn_with_equip), &installed);
    add({kGlobalNs, kSavedAiLevels, kApplyAiLevels, 3},
        reinterpret_cast<void*>(&apply_ai_levels_hook),
        reinterpret_cast<void**>(&g_apply_ai_levels), &installed);

    // Tuning surface. Namespace "PlayerBot", not global.
    const bool ai_awake = add({kBotNs, kAiBotController, "Awake", 0},
                              reinterpret_cast<void*>(&ai_awake_hook),
                              reinterpret_cast<void**>(&g_ai_awake),
                              &installed);
    g_set_ai_level_info = reinterpret_cast<const MethodInfo*>(
        il2cpp::find_method_info(kBotNs, kAiBotController, kSetAiLevel, 1));
    const bool level = add({kBotNs, kAiBotController, kSetAiLevel, 1},
                           reinterpret_cast<void*>(&set_ai_level_hook),
                           reinterpret_cast<void**>(&g_set_ai_level),
                           &installed);
    if (level && g_set_ai_level_info == nullptr) {
        LOGE("23.1.3-bots: AI level MethodInfo was not retained");
    }
    add({kBotNs, kAiBotController, kSetBehavior, 1},
        reinterpret_cast<void*>(&set_behavior_hook),
        reinterpret_cast<void**>(&g_set_behavior), &installed);
    add({kGlobalNs, kAiConfigClass, kAiLevelSettings, 0},
        reinterpret_cast<void*>(&ai_level_settings_hook),
        reinterpret_cast<void**>(&g_ai_level_settings), &installed);

    LOGI("23.1.3-bots: installed %d/11 hooks (bot-factory=%s "
         "ai-awake=%s ai-level=%s forced-tier=%" PRId32 ")",
         installed, factory ? "OK" : "FAILED",
         ai_awake ? "OK" : "FAILED", level ? "OK" : "FAILED",
         kHighRankAiLevel);
    return factory && ai_awake && level && g_set_ai_level_info != nullptr;
}

} // namespace bots_trace_2313
