# 23.1.3 — bots: why they are weak, and what can actually be changed

Reported: bots are weak, always carry the starter rifle, and exist only in some
modes. Requested: stronger bots, bots in Duel and Escort, and mode-aware
behaviour (Duel = aggressive but not cheater-like; Escort = attackers escort the
ram, defenders push it back).

This document records what the dump actually supports, because the answer
changes the plan significantly.

## The bot tuning surface exists and is large

The player was right that bots can be much stronger. Two parallel
configuration surfaces exist.

### 1. Server-driven balance (`PGCompany.DataObjects`)

`dump2313.cs` line 533595, `[MessagePackObject(False)]` with `JsonProperty`
names, reached as `IntBotsLoadout : Dictionary<int, <botConfig>>` from the
loadout root (line 533558, `[MessagePackObject]`, `defaultBotClass`,
`defaultWeaponRarity`, ...):

| Field | JSON key | Field | JSON key |
| --- | --- | --- | --- |
| `BotIsEnable` | `be` | `MinRandomValueForAim` | `mnfa` |
| `MinBotsNumber` | `mnn` | `MaxRandomValueForAim` | `mxfa` |
| `MaxBotsNumber` | `mxn` | `PauseBetweenShots` | `pbs` |
| `WeaponDamage` | `wd` | `SwitchTarget` | `ctw` |
| `Speed` | `s` | `SwitchTargetWhenBeenAttacked` | `cta` |
| `Health` | `h` | `ChangeWeaponWhenOutOfAmmo` | `cwo` |
| `Armor` | `a` | `ChangeWeaponToMeleeInCloseRange` | `cwc` |
| `Equip` (`List<string>`) | `e` | `MinTurnRate` / `MaxTurnRate` | `tr` / `mxtr` |
| `PlayerBotSpawnEquip` | `se` | `RotateWhenStand` | `rws` |
| `GadgetBotUsage` | `gu` | `AggroRadius` | `ard` |
| `FakeShot` | `fs` | `ChaisingTime` | `ct` |
| `PlayerBotChaising` | `pc` | `DodgeInMelee` / `DodgeInRange` | `dim` / `dir` |
| `ChanceToChoosePlayer` | `st` | `JumpInMelee` / `JumpInRange` | `jim` / `jod` |
| `ZoneBotSpawnTime` | `zst` | `MinSpawnRadius` / `MaxSpawnRadius` | `misr` / `masr` |

The bot difficulty *class* key is an enum (line 533750): `Default`, `Courier`,
`Average`, `Trickster`.

### 2. Client-side AI levels (`<aiConfig>.AILevelSettings`)

`dump2313.cs` line 238316. Keyed by an integer AI level, and it carries the
weapon list itself:

`weapons : List<string>`, `minRandomValueForAim`, `maxRandomValueForAim`,
`attackRange`, `rotationSpeedMinSkirmish`, `rotationSpeedMaxSkirmish`, and the
percentage chances `dodge`, `dodgeWithMelee`, `jumpOnDodje`,
`jumpOnDodjeWithMelee`, `straightDodje`, `switchOnTarget`,
`switchOnTargetWhenHaveTarget`, `takeMeleeWeapon`, `goForBonus`,
`rotateWhenStand`, `choosePlayerAsTarget`, `doDamageOnShoot`,
`changeWeaponOnReload`, `pauseOnShoot`, `throwGrenade`.

The provider (line 238498) keeps two `Dictionary<int, AILevelSettings>` tables
and builds them by parsing JSON at runtime (`<parse>(string)` `0x4555DF4`,
`<parse>(Dictionary<string, object>)` `0x4556678`, both reached only from the
initializer `0x4555B5C`).

`PlayerBotsManager.SavedAiLevels` (line 187257) holds the per-match assignment:
`teammatesAiLevels`, `enemiesAiLevels`, `dmAiLevels`, applied via
`<apply>(int[], int[], int[])` `0x23A1030`.

The runtime AI itself (`AIBotController`, line 270205) is genuinely capable:
`meleeSqrRange`, `cycleTime`, `shootcCycleTiime`, `rotationSpeedMin/Max`,
`dodgingTime`, `dodgingSpeed`, `maxMoveDistance`, `timerForChangeWeaponFromMelee`,
`tepmFixPrediction`, `get_MinRandomValueForAim`, `get_maxRandomValueForAim`,
plus a NavMesh agent and a target detector. Behaviour is a 4-state machine:
`None`, `Walking`, `Searching`, `Skirmish`.

## Why they are weak in practice

Both tables are **data loaded at runtime**, and the balance table is a
server-delivered MessagePack/JSON payload from the retired backend. With no
payload, the client falls back to whatever default level exists — which is
exactly the "starter rifle, poor aim" bot being observed.

This is why no buff is committed in this pass. What cannot be determined
statically:

- how many AI levels this build actually ships (the dictionary is filled from
  JSON at runtime);
- which level is currently handed out;
- whether `weapons` in the shipped payload is non-empty at all.

Raising the AI level blindly would either do nothing or index a level that does
not exist. Fabricating a weapon list risks naming items this build does not
have. Both are guesses, and guessing is what produced the useless 14.1.1
post-match port.

## Shipped now: `bots_trace_2313.h`

A passive trace that answers exactly the open questions, changing nothing:

| Hook | Reports |
| --- | --- |
| `PlayerBotsManager.<spawnWithEquip>(string,string,string)` `0x239E0EC` | the equip ids a bot spawns with |
| `AIBotController.<setAiLevel>(int)` `0x2143514` | the AI level assigned to each bot |
| `PlayerBotsManager/SavedAiLevels.<apply>(int[],int[],int[])` `0x23A1030` | whether the level tables are applied at all |
| `AIBotController.<setBehavior>(enum)` `0x2145C68` | behaviour transitions (throttled) |
| `<aiConfig>.<aiLevelSettings>()` `0x455566C` | whether the per-level table is non-null |

The arrays are deliberately not decoded: the il2cpp array memory layout is not
part of this project's verified contract, and presence already answers the
question. Nested `PlayerBotsManager/SavedAiLevels` resolves through the
nested-type walk in `il2cpp::find_class()` (see `IL2CPP_NESTED_TYPES.md`).

## Duel and Escort: what the dump says about the request

Partially supported, and the naming in the request does not map cleanly.

- Duel exists as a full subsystem: `DuelController` (line 22955), `DuelUIController`,
  `DuelPlayersPosition`, `CountKillsDuelLabel`, `InGameGUIPart_Duel`, plus
  `TeamDuelController` and `InGameGUIPart_TeamDuel`.
- Escort is **not** `Siege`. `SiegeController` (line 233884) is the clan siege
  mode. `ramEscort = 27` and `escortDestroyGate = 46` are members of a
  *quest/challenge* enum in namespace `PGCompany`, not a mode enum, so they do
  not identify the escort mode controller. The ram object and the escort mode
  controller still have to be located.
- Bot spawning goes through Photon: `PlayerBotController.<create>(PhotonView)`
  `0x3C6CF3C` has 4 call sites, and `PlayerBotsManager` drives spawn/despawn
  from coroutines. Bots are room-master owned entities, so adding them to a
  mode means driving that manager under the mode's own start conditions.
- The requested gates (`30 s` for one duel bot, `+1 every 10 s` in escort up to
  the mode's own minimum) need the real minimum-player thresholds. Only two
  candidates exist in the dump (`mMinPlayersToStart` line 309264 and
  `MinPlayersForRegistration` line 429081), and neither has been tied to Duel
  or Escort yet.

So the correct order is: land the trace, read one duel/escort session from a
device, then implement gating and behaviour against real values.

## Behaviour design, once the data is known

Recorded now so the intent is not lost:

- **Duel** — aggressive but not cheater-like. Do not zero the aim spread; keep
  `minRandomValueForAim`/`maxRandomValueForAim` above zero and keep
  `pauseOnShoot` non-zero, while raising `choosePlayerAsTarget`,
  `switchOnTarget`, `dodge` and `attackRange`. A bot with zero spread and zero
  shot pause reads instantly as a cheat.
- **Escort, attackers** — target selection biased to a point near the ram so
  the bots stay with it and defend it, since proximity is what moves it.
- **Escort, defenders** — engage enemies and advance toward the ram, which is
  what pushes it back.

Both map onto the existing `Walking`/`Searching`/`Skirmish` machine plus the
target detector, so no new AI is needed — only mode-aware target and
destination selection.

## Validation

No local build is possible here (no Android SDK/NDK); only CI can build.

```
adb logcat -c; adb logcat --pid=$(adb shell pidof com.pixel.gun3d) > pg3d.log
```

Expected:

```
23.1.3-bots-trace: installed 5/5 hooks (spawn-equip=OK ai-level=OK)
23.1.3-bots: AI level settings table first queried: present
23.1.3-bots: AI level tables applied (teammates=present enemies=present deathmatch=present)
23.1.3-bots: bot 0x... assigned AI level <N>
23.1.3-bots: spawn #1 equip='<id>' / '<id>' / '<id>'
```

### Checklist

- [ ] CI build succeeds.
- [ ] `installed 5/5 hooks` appears.
- [ ] One deathmatch played with bots; AI level and equip ids captured.
- [ ] If the settings table logs `NULL`, the buff must supply the whole table,
      not just a higher level index.
- [ ] Duel/Escort session captured to identify the mode controllers and the
      real minimum-player thresholds.
