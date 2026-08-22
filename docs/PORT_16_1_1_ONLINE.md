# PG3D 16.1.1 local backend session + Photon online port

This branch targets the supplied obfuscated ARMv7 IL2CPP build. It keeps the existing Photon Cloud route and replaces the retired Cubic Games authorization transport with the game's own successful, online-compatible completion path backed by local `Storager` / `PlayerPrefs` state.

The previous v5 path invoked `AuthInterfaceController.OnGoOfflineClick`. That opened the lobby and was enough to test Photon room entry, but it also left the process-wide backend-offline flag set. PUN could still be forced online, so a map loaded, while stock in-match initialization treated the account as offline: HUD, input and loadout initialization did not complete. v6 no longer enters the offline callback.

## Analysis inputs

- `libil2cpp.so` SHA-256: `2aab620cb58a597e86975a78ab20987e71685b507456707ed42fa63fad54032b`
- `global-metadata.dat` SHA-256: `b709931396332f27c79a8ef0e696e66fcd4aefd5d8217dca361741cee404eca6`
- 16.1.0 `dump.cs` SHA-256: `6cbaff1fdbb21b1a93fc1c444689f9778a3e0a68b2204aaf9f5b3e59ed05a719`
- comparison 14.1.1 `dump1411.cs` SHA-256: `484a6041d330b587aaed702232c58423be59d495557ba84fdef64bf6a8ca24d1`

The binary, metadata and dumps are analysis inputs and are not committed.

## Recovered backend-first state machine

`AuthSceneState` is preserved in the 16.1.0 dump:

- `0 Initial`
- `1 Authorizing`
- `2 Authorized`
- `3 FullySynchronized`
- `4 Empty`
- `5 CheckBindedId`
- `6 ChooseId`
- `7 ChooseProgress`
- `8 SendCachedCommands`
- `9 SynchronizeProgress`
- `10 CheckAppVersion`
- `11 Easy`
- `12 CheckConnection`
- `13 SendProgress`
- `14 WaitAsync`
- `15 TechnicalWorks`

The relevant mappings for this exact binary are:

- offline button: `AuthInterfaceController.OnGoOfflineClick/0`, RVA `0x00908364`
- offline callback registered by `AuthSceneController.Awake`: `AuthSceneController.不丏丛丝不上丐丂丂/0`, RVA `0x0090AB80`
- successful completion: `AuthSceneController.丐业丆一七专丌丝丑/0`, RVA `0x0090BFC8`
- completion blocker: `AuthSceneController.丕丈丁丝丈丑丛业不/0`, RVA `0x0090D5EC`
- state getter: `AuthSceneController.且丙丐丟丞丞丗世下/0`, RVA `0x00908624`
- session-ready getter: `AuthSceneController.丁丌丁丅丐丈丕丌丕/0`, RVA `0x009086F4`
- state application: `AuthSceneController.丑丐丛丁丕下七与三/1`, RVA `0x00909FC4`

A32 control-flow decoding of the supplied `libil2cpp.so` established the semantic difference:

1. The offline callback writes `1` to a static byte at offset `0x3DC`, performs the offline lobby setup and loads the menu. It does **not** publish `FullySynchronized`.
2. The stock successful completion first applies `CheckAppVersion`, then applies `FullySynchronized`, writes `0` to the same static byte at offset `0x3DC`, sets `AuthSceneController`'s session-ready flag at static offset `0xC`, invokes the synchronization-complete listeners, and enters the same stock menu loader.
3. `OfflineModController.Start` later reads that `0x3DC` byte and gates backend-dependent UI. Clearing only `PhotonNetwork.offlineMode`, as v5 did, cannot clear this independent backend/session state.

## Local backend behavior

`backend_local_1610.h` implements a narrow in-process session boundary:

1. `AuthSceneController.Awake` remains stock. Local serializers, model owners, listeners and existing persisted profile data are initialized normally.
2. `AuthSceneController.Start` is intercepted before it can launch the retired authorization HTTP route.
3. The game-owned successful completion method is invoked directly. No JSON payload, user ID, inventory, currency, HTTP status or WebSocket response is fabricated.
4. The obsolete backend-fed app-version predicate is bypassed only while that synchronous local completion transaction is running. Calls outside the transaction remain stock.
5. Completion is accepted only when the stock session-ready getter is true and the final state is `FullySynchronized` or `Empty` (the stock menu loader changes state 3 to state 4 after publishing completion). Otherwise the bridge fails closed and does not fall back to either the retired backend or the offline callback.
6. All profile changes continue through the original controllers and their `Storager` / `PlayerPrefs` save paths. The bridge owns no parallel save format and does not globally override inventory or ownership getters.

This is intentionally a backend **session emulator**, not a fake Cubic Games API server. It supplies the state transition the defunct service used to unlock while preserving the client as the authority for local data and gameplay objects.

## Photon mapping retained from v5

- `Switcher.SetUpPhoton(HiddenSettings)` → `Switcher.丝且丟世专丕丟丐丝/1`
- `Switcher.SelectPhotonAppId(HiddenSettings)` → `Switcher.丞不与丗丏丕丄七三/1`
- `PhotonNetwork` → `上丁丈与丘丟丈专丄`
- `PhotonNetwork.PhotonServerSettings` → `且丄上丘丌丕丏丘丟`
- `PhotonNetwork.get_connectionStateDetailed()` → `专丑三丛不东丛丕丝/0`
- `PhotonNetwork.get_offlineMode()` → `丙丆丐七与丈上下丘/0`
- `PhotonNetwork.set_offlineMode(bool)` → `与七丅一丑丈丅丘丅/1`
- `PhotonNetwork.ConnectUsingSettings(string)` → `丟丙下世丒不丐丘丛/1`
- `ServerSettings.UseCloud(string, CloudRegionCode)` → `ServerSettings.丛丙丄世一丁业丟专/2`
- room-options serializer → `丏丙丞一专不万万世/2`
- `RoomOptions.Plugins` → `不丈丏一丏万丅丂下.丂下丒不丞业专不丏`

The existing Photon module still clears PUN's own `offlineMode` defensively, applies the configured Photon AppID / EU route, omits the retired custom plugin only while room options are serialized, and leaves matchmaking, RPCs and room callbacks stock.

## Build and device validation

Build with the repository `PHOTON_APP_ID` Actions secret, install the resulting `libopg3d.so`, and capture:

```bash
adb logcat -s OPG3D
```

Expected startup and auth lines:

```text
init: libopg3d build 16.1.1 Local backend session v6 ...
16.1.x-local-backend: armed stock FullySynchronized completion from local Storager state ...
16.1.x-local-backend: suppressing AuthSceneController.Start network route ...
16.1.x-local-backend: ignored retired backend version gate for the local completion transaction
16.1.x-local-backend: local online-compatible session ready (state=4/Empty readyFlag=1 backendOffline=cleared by stock completion)
16.1.x-trace: MAIN MENU REACHED — MainMenuController.Awake returned
```

The following old line must **not** appear:

```text
16.1.x-auth: dead backend bypass — invoking stock AuthInterfaceController.OnGoOfflineClick
```

A successful gameplay test must verify all of the following on two devices:

1. the stock **В бой** route opens without an offline-only warning;
2. Photon authenticates, creates or joins a room, and loads the map;
3. HUD, joysticks/movement, firing, weapon switching and the initial loadout are present;
4. both players see movement, damage and RPC-driven actions;
5. leaving the match returns to a usable menu;
6. a second consecutive match works;
7. locally changed settings/progression survive a restart.

Any `local-backend: stock completion did not publish a usable session`, `PHOTON_APP_ID is empty`, `route verification failed`, `PluginMismatch`, or `online port incomplete` line is a failed test, not a condition to hide with another global getter override.
