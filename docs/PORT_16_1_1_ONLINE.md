# PG3D 16.1.1 Photon online port

This branch targets the supplied obfuscated ARMv7 IL2CPP build and ports only the Photon multiplayer connection path. The offline **В бой** button and the older progression/catalogue/gameplay modules remain out of scope for this pass.

## Analysis inputs

- `libil2cpp.so` SHA-256: `2aab620cb58a597e86975a78ab20987e71685b507456707ed42fa63fad54032b`
- `global-metadata.dat` SHA-256: `b709931396332f27c79a8ef0e696e66fcd4aefd5d8217dca361741cee404eca6`
- 16.1.1 `dump.cs` SHA-256: `6cbaff1fdbb21b1a93fc1c444689f9778a3e0a68b2204aaf9f5b3e59ed05a719`
- comparison 14.1.1 `dump1411.cs` SHA-256: `484a6041d330b587aaed702232c58423be59d495557ba84fdef64bf6a8ca24d1`

The dumps, metadata and game binary are analysis inputs and are not committed.

## Obfuscation mapping

The 16.1.1 dump preserves the PUN type layout and signatures but obfuscates the central class and most method names. `photon_1610.h` contains the mapping for this exact binary only:

- `Switcher.SetUpPhoton(HiddenSettings)` → `Switcher.丝且丟世专丕丟丐丝/1`
- `Switcher.SelectPhotonAppId(HiddenSettings)` → `Switcher.丞不与丗丏丕丄七三/1`
- `PhotonNetwork` → `上丁丈与丘丟丈专丄`
- `PhotonNetwork.PhotonServerSettings` → `且丄上丘丌丕丏丘丟`
- `PhotonNetwork.get_connectionStateDetailed()` → `专丑三丛不东丛丕丝/0`
- `PhotonNetwork.get_offlineMode()` → `丙丆丐七与丈上下丘/0`
- `PhotonNetwork.set_offlineMode(bool)` → `与七丅一丑丈丅丘丅/1`
- `PhotonNetwork.ConnectUsingSettings(string)` → `丟丙下世丒不丐丘丛/1`
- `ServerSettings.UseCloud(string, CloudRegionCode)` → `ServerSettings.丛丙丄世一丁业丟专/2`

Mappings were established by matching the unchanged class field order, method order, parameter/return signatures and the PUN 1.91 layout against 14.1.1. No guessed raw RVA is used by the online module.

## Runtime behavior

1. The obfuscated Switcher AppID selector returns the AppID supplied through the existing `PHOTON_APP_ID` build secret. Logs contain only its character count and FNV-1a fingerprint.
2. After `SetUpPhoton`, and again immediately before `ConnectUsingSettings`, the stock `ServerSettings.UseCloud(appId, eu)` method is called. Host type, region and stored AppID are then read back and repaired through metadata fields only if the SDK call did not leave the expected values.
3. The auth-scene fallback still opens the menu through the game's stock offline transition. Immediately before a real PUN connection, `PhotonNetwork.offlineMode` is cleared through its own setter so the stock connect method does not reject online play.
4. `FriendsController.Update` is forwarded normally until a Photon connection starts. While PUN is connecting or connected, that dead social/backend tick is quarantined so it cannot run the legacy local disconnect path. Photon callbacks, rooms, RPCs, matchmaking and manual disconnects are untouched.

## Build and device validation

Build from branch `16.1.1-test` with the repository Actions secret `PHOTON_APP_ID`, install the resulting `libopg3d.so`, and capture:

```bash
adb logcat -s OPG3D
```

Expected startup lines:

```text
init: libopg3d build 16.1.1 Photon online port v1 ...
16.1.1-photon: installed 4 hooks (appid=OK connect=OK backend-guard=OK; ...)
init: 16.1.1 online port ready ...
```

On pressing the online battle button:

```text
16.1.1-photon: PUN offlineMode was set by the auth-scene fallback; clearing it before online connect
16.1.1-photon[PUN.ConnectUsingSettings]: ... host=1 region=0 ... ready=1
16.1.1-photon: ConnectUsingSettings returned 1 ...
```

Then verify master connection, room creation/join, a two-device match, RPC synchronization, match exit, and a second consecutive match. A `PHOTON_APP_ID is empty`, `route verification failed`, or `online port incomplete` line means the build must not be treated as a successful online test.
