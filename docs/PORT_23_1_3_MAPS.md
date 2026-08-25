# 23.1.3 — map and mode menu unlock

## First, the myth

There is **no `Paladin Castle` map in 23.1.3**. Extracting tokens from the
shipped `global-metadata.dat` returns `paladin` only three times, never as a
map:

- `Weapon339_paladin` — a weapon skin
- `avatar_light_paladin` — an avatar

Everything “castle” in this build is something else: `Arena_Castle` (an arena
name), `Castle_campaign3` (a campaign level), `Castle_grass`, and large
families of clan-fort and lobby building ids (`terrain_castle_1..3`,
`base_castle_1..3`, `wall_castle_1..3`, `gate_castle_1..3`, `fort_santacastle`,
`lobby_terrain_castle_*`). `Arena_Castle` does not even appear in the code dump,
only in metadata/localisation.

There is also **no dedicated dev map**. The scene names the build ships are
`arena_scene`, `arena2_scene`, `_ruins_scene`, `fortattack_scene`,
`fortdefence_scene`, `clan_fortress01_scene`, `speedrun_scene` and
`connect_scene`. What does exist is a developer *console*
(`DeveloperConsoleController`, `MiniDeveloperConsoleButton`,
`InfoWindowController...DeveloperConsoleMini = 7`), which is a separate topic
and is not touched by this module.

What *is* real: sandbox maps (`aqua_park_sandbox`, `sky_islands_sandbox`,
`pizza_sandbox`), premium maps, event maps gated by a version window, and a
pile of maps hidden behind player level. Those are what this module releases.

## Why maps are hidden, and why it is a local problem

The map catalogue never touches the backend, so the emulator in
`backend_emu_routes.h` is irrelevant here.

| Type | TypeDefIndex | What matters |
| --- | --- | --- |
| `SceneInfo` | 4550 | `avaliableInModes`, `indexMap`, `isPremium`, `isPreloading`, `minAvaliableVersion`, `maxAvaliableVersion`, `keyTranslateName` |
| `AllScenesForMode` | 4551 | `avaliableScenes`, `mapsForVote`, `unlockedAtLevel`, `lockedByLevel`, `mode` |
| `SceneInfoController` | 4555 | `allScenes`, `modeInfo`, `jsonConfig`, the per-mode bucket getter |

`SceneInfoController` sorts every map into a per-mode `AllScenesForMode`
bucket, and the picker grid renders `avaliableScenes`. A map is therefore
absent for exactly four local reasons:

1. it sits in `lockedByLevel` (player level too low),
2. its `avaliableInModes` does not list the selected mode,
3. `isPremium`,
4. it is outside its `minAvaliableVersion` / `maxAvaliableVersion` window.

Game *modes* are gated separately, by `GameModeUnlocks` (progress key `mgsl`)
and `MiniGameCell`'s `lockedState` / `levelRequiredLabels`. Writing that pref
is not an option on 23.1.3 — prefs are encrypted (`AEAD:<base64>`), which is
the same wall `identity_2313.h` hit — so mode unlocking has to hook the
availability predicate. This module covers maps; modes are follow-up work.

## What the module does

`maps_unlock_2313.h` hooks both overloads of the per-mode bucket getter
(`丂丙丛万下与丑丝下`, RVA `0x33E9DC4` for `(mode)` and `0x33E9EA8` for
`(mode, List<AllScenesForMode>)` — documentation only, nothing is taken by
address). After the stock call has built its bucket:

1. every entry of `lockedByLevel` is moved into `avaliableScenes`, then the
   locked list is emptied;
2. `unlockedAtLevel` is cleared, so no stale “unlocks at level N” badge is drawn
   for a map that is now selectable;
3. every map in `SceneInfoController.allScenes` that the bucket does not already
   contain is appended — this is what reaches premium maps, version-window maps
   and maps a mode does not list at all.

The game's own `List<SceneInfo>` objects are mutated through their own managed
`Add` / `Contains` / `Clear`. No memory is patched and no UI row is fabricated:
the stock grid renders the added maps exactly as it renders any other map.

### Deliberately not touched: the vote list

`mapsForVote` is left alone (`kAddToVoteList = false`). `MapVoteController`
ships a hard `private const int ... = 5` vote-slot limit and its grid is laid
out for five entries, so pushing the full map list in would overflow the panel
instead of unlocking anything. Map *selection* was the request.

### Cost control

The getter can be called from UI code every frame, so the unlock is memoised
per bucket by its settled `avaliableScenes` count: an already-opened bucket
costs one `get_Count` call. Additions are capped at `kMaxAddsPerCall = 512`
and any list reporting more than `kMaxListEntries = 4096` entries is ignored as
implausible. Re-entrancy is blocked with a plain flag, since all of this runs on
the Unity main thread.

## Asset bundles: the one real caveat

Maps stream from asset bundles. `MapHint` carries an
`AssetBundlesStateMonitorView` plus `enableIfBundleLoaded` /
`disableIfBundleLoaded`, and the metadata has `assetBundles-v2` and
`assetBundleHashFromConfig`. The bundle CDN is gone, so **a map whose bundle is
not in the OBB or the local assets payload can be selected but will not load.**

Bundle presence is intentionally not checked (requested explicitly): every map
is offered. To make a missing bundle easy to identify, the first
`kLogMapsPerBucket = 12` maps of each opened bucket are logged with their
`indexMap` and `keyTranslateName`. Cross-check a map that hangs on load against
`OBB_PROVISIONING.md` and `assets_data_2313.h`.

If that turns out to be common in practice, the clean follow-up is to gate
additions on the bundle monitor rather than to remove the unlock.

## Verifying on device

```sh
adb logcat -s OPG3D | grep -E '23\.1\.3-maps'
```

Expected:

- `armed (bucket getter overloads: 1-arg hooked, 2-arg hooked); level locked maps are released and every map the build ships is offered in every mode, and asset bundle presence is not checked`
- per mode: `mode <N>: <before> -> <after> selectable maps (+<k> level locked, +<m> not offered for this mode)`
- followed by `map index <i> '<key>'` lines

If instead you see `neither overload of the per-mode map bucket getter could be
hooked`, the metadata lookup failed. The global namespace is passed as the empty
string (`kGlobalNs = ""`), which is how IL2CPP metadata spells it; that constant
is the first thing to check.

If you see `AllScenesForMode does not expose avaliableScenes/lockedByLevel`,
the field names changed and the module disarms itself instead of guessing.

## Safety model

- Fail closed: if neither overload hooks, or the bucket fields do not resolve,
  the module stays inert and the picker keeps its stock gates.
- No patched memory, no fabricated UI rows, only the game's own managed calls.
- Idempotent: re-running on an already-opened bucket adds nothing.

## Follow-up work

- Game modes: hook the `GameModeUnlocks` availability predicate (`mgsl`
  progress) so locked modes stop rendering `lockedSprite` and their level
  requirement labels.
- Developer console: `DeveloperConsoleController` ships with
  `HandleBeginFromStart`, `HandleBeginFromStart6LevelAllItems`, `HandleEnableX10`,
  `SetServer` / `ChangeServer` and dev UI toggles, gated by a static predicate.
  Enabling `MiniDeveloperConsoleButton` would expose all of it.
- Bundle-aware mode: gate additions on the asset bundle monitor if missing
  bundles turn out to be common.
