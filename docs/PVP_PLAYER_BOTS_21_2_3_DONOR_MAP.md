# 21.2.3 player-bot donor map

## Purpose

This document records the verified multiplayer player-bot subsystem in the supplied 21.2.3 IL2CPP files and defines how it may be used as a behavioral donor for the 16.1.0 port.

It does **not** make 21.2.3 native RVAs or object layouts valid in 16.1.0. The target build must use its own resolved classes, methods, event payloads and field layouts.

## Donor identity

| File | SHA-256 |
| --- | --- |
| `dump.cs` | `d794831b6d00d70ba9f0905836832ad4bb574061f78f2c3761738442ae2f954f` |
| `global-metadata.dat` | `4b63bb1752eeaf7f2f20ea77cbd06e6f57c17c47627c15be7dced013b4c5479e` |
| `libil2cpp.so` | `3606ee58a834c6310a43e49e3924ffb0483a66953f6a120af8fbab42a68de784` |

`libil2cpp.so` is ARM32 little-endian EABI5, with build ID `09f7f4867b622b885b236c5935f9ceef6922eaf1`. Its architecture matches the target device class, but the Unity/game ABI is different and direct binary transplantation is prohibited.

## Suitability result

The donor contains the genuine multiplayer player-bot stack that is absent from 16.1.0:

- `PlayerBotPath` — TypeDefIndex 10990
- `PlayerBotPathPoint` — TypeDefIndex 10991
- `PlayerBotPathsGroup` — TypeDefIndex 10993
- `PlayerBotPathsManager` — TypeDefIndex 10994
- `PlayerBotPathWayPoint` — TypeDefIndex 10995
- `PlayerBotController` — TypeDefIndex 10996
- `PlayerBotEffects` — TypeDefIndex 10997
- `PlayerBotEntity` — TypeDefIndex 10998
- `PlayerBotInstance` — TypeDefIndex 11001
- `PlayerBotMovement` — TypeDefIndex 11003
- `PlayerBotsManager` — TypeDefIndex 11005
- `PlayerBotWear` — TypeDefIndex 11012
- `AIBotController` — TypeDefIndex 12590
- `AIBotTargetDetector` — TypeDefIndex 12595

This is a player-shaped, Photon-aware bot system rather than the campaign/co-op `RilisoftBot` mob stack.

## Verified responsibilities

### Network/player integration

`PlayerBotController` contains a `PhotonView`, `PlayerBotInstance`, player GameObject, `PlayerBotEntity`, `PlayerBotWear`, `PlayerBotMovement` and `AIBotController`. It subscribes to network events and has player-connect and destruction paths.

`PlayerBotInstance` contains `NetworkStartTable`, the controller reference, a controller-prefab string, bot/team/index state and AI-level state. It also has `OnPhotonPlayerConnected` and `OnMasterClientSwitched` handlers, confirming explicit reconnect and authority-migration handling.

### Roster and AI levels

`PlayerBotsManager` owns:

- active `PlayerBotInstance` and `PlayerBotController` lists;
- controller-state dictionaries;
- `ignorePlayer` and `equalityMode` controls;
- `SavedAiLevels` with separate arrays for teammates, enemies and deathmatch;
- creation candidates taking a network dictionary, integer index or prefab string;
- update, registration, removal and destruction paths.

The remembered “level spoof” behavior is plausible in this generation: separate teammate/enemy/deathmatch AI-level arrays and per-instance/scenario AI-level fields are present. This does not yet prove which public/account-level value old mods changed; the exact setter and persistence path still require native call-graph mapping.

### Fair combat behavior

`PlayerBotEntity` is connected to normal player systems through `Player_move_c`, `PhotonView`, weapon descriptors and the standard projectile/damage paths. It includes scenario weapon sets, health, damage multiplier and respawn-time inputs. Weapon selection and current-weapon state are part of the entity rather than a fixed mob projectile.

`AIBotController` exposes behavior states `None`, `Walking`, `Searching` and `Skirmish`. Its serialized/runtime state includes:

- minimum and maximum random aim values;
- melee and shooting cycle timing;
- weapon-change timing;
- movement distance and speed;
- dodging time and speed;
- prediction tuning.

`AIBotTargetDetector` contains separate target-check and line-of-sight intervals, effective field of view, notice distance and awareness-loss distance. These are the correct control points for reaction time, LOS enforcement and non-cheating aim.

### Navigation data

`PlayerBotPathPoint` stores position, rotation, timestamp, jump flag and movement speed. A waypoint stores radius, weight, defence intent and outgoing paths. `PlayerBotPathsManager` owns the serialized waypoint list.

This confirms that map-authored bot paths are assets. Native code alone cannot reproduce the donor's navigation quality without equivalent map data or a validated runtime navigation fallback.

## Native anchors for deeper mapping

The following 21.2.3 RVAs are donor-only anchors:

| Area | RVA |
| --- | --- |
| `PlayerBotsManager` singleton getter | `0x21315BC` |
| `PlayerBotsManager.Awake` | `0x213AF80` |
| `PlayerBotsManager.Start` | `0x213B250` |
| manager create-from-network-dictionary candidate | `0x213BB04` |
| manager create-from-index candidate | `0x213BCA4` |
| manager create-from-prefab-name candidate | `0x213BE6C` |
| manager register instance candidate | `0x213C00C` |
| manager remove controller candidate | `0x2135070` |
| `PlayerBotInstance.OnMasterClientSwitched` | `0x21353BC` |
| `PlayerBotMovement.OnMasterClientSwitched` | `0x2139240` |
| `AIBotController.Awake` | `0x2EE72C4` |
| `AIBotController.Update` | `0x2EE739C` |
| `AIBotTargetDetector.Awake` | `0x2EECDFC` |

Obfuscated methods labelled “candidate” must be confirmed by disassembly and call sites before implementation.

## Port constraints

1. Do not call donor RVAs from the 16.1.0 process.
2. Do not copy donor field offsets into 16.1.0.
3. Use the donor only to recover lifecycle, behavior, event and asset contracts.
4. Resolve equivalent player, Photon, scoreboard, team and objective APIs from the 16.1.0 metadata/binary.
5. Keep every mode fail-closed until its roster, scoring, objective and map navigation checks pass.
6. Preserve the approved five-second paired-bot and ten-second roster-filler policies from the design document.
7. Keep stock weapon damage, ammo, reload and player hit-registration paths; do not introduce bot-only damage multipliers.

## Remaining asset input

A full donor OBB is not required immediately. First obtain a file listing, for example:

```sh
7z l main.*.obb > obb-list.txt
```

The listing will be used to identify only the bundles or level files that contain:

- the bot controller prefab referenced by `controllerPrefab`;
- `PlayerBotPathsManager`, waypoint and path-group data;
- supported PvP map path sets;
- bot wear/avatar dependencies not already available in 16.1.0.

## Next implementation gate

Before enabling runtime spawning, map and validate:

- manager creation/removal call graphs and network dictionary keys;
- Photon event codes and payload formats;
- controller-prefab resource path and ownership mode;
- AI-level clamping and equality-mode semantics;
- master migration and late-join reconstruction;
- 16.1.0 equivalents for player creation, team/FFA slots, scoreboard and objectives;
- per-map path availability or a safe fallback.
