# PG3D 23.1.3 ARM64 Photon Cloud port

## Scope

This ports the proven 16.1.0 PUN 1.91 online route to the supplied 23.1.3
ARM64 build. It is deliberately limited to online transport and compatibility:

- configured Photon AppID selection;
- Photon Cloud routing pinned to EU;
- clearing PUN `offlineMode` before connection;
- protection from the retired `FriendsController` backend tick while Photon is
  connecting or connected;
- removal of the legacy custom room-plugin request so a normal Photon Cloud
  application can create rooms with the Default plugin;
- passive status, operation-response and room callback diagnostics;
- suppression of the obsolete 25.x update prompt and false retired-backend
  no-internet modal before Photon startup.

Matchmaking, room creation/join, RPC dispatch and disconnect behavior remain
stock PUN. This change does not yet port 16.1.0 progression, crafting, lobby
catalog or battle-UI modules.

## Exact artifacts analyzed

| Artifact | 16.1.0 | 23.1.3 |
| --- | --- | --- |
| `libil2cpp.so` | SHA-256 `2aab620cb58a597e86975a78ab20987e71685b507456707ed42fa63fad54032b`; ELF32 ARM; Build ID `0fd9946b14fe013039ece2af653c5e9dc083b1ed` | SHA-256 `f0a130c4e8d9487059eab4b0f08462f6aa7d057510cada0fd6fb3043c77deb5c`; ELF64 AArch64; Build ID `57fcc18d2db06212416d480d53c0f881ee47c52a` |
| metadata | SHA-256 `b709931396332f27c79a8ef0e696e66fcd4aefd5d8217dca361741cee404eca6` | SHA-256 `28b8bddf53a8ebdaf70aec1e672d3bdea6e46ca2b2e478f1b7e66e69884c99dd` |
| dump | SHA-256 `6cbaff1fdbb21b1a93fc1c444689f9778a3e0a68b2204aaf9f5b3e59ed05a719` | SHA-256 `803371a6246bdeb6f230ea54dbbbf77108ce088cdfbbcd0f6843a45185398830` |

## Obfuscation-safe mapping

Names were not copied between versions. Each target was mapped from stable
PUN structure, ordered signatures and native evidence.

| Role | 16.1.0 metadata | 23.1.3 metadata | Evidence |
| --- | --- | --- | --- |
| PUN static class | `上丁丈与丘丟丈专丄` | `丟丝专丄丑世丞世丒` | PUN `1.91` constant; `PhotonServerSettings`; nearly identical field/method sequence |
| ServerSettings field | `且丄上丘丌丕丏丘丟` | `丁丟丄业东东业不且` | Same `ServerSettings` field after `PhotonServerSettings` constant |
| ClientState getter | `专丑三丛不东丛丕丝()` | `丝三丒丙丛丄丂丟丒()` | Same ordered `ClientState` getter; RVA `0x4463A90` |
| offline getter/setter | `丙丆丐七与丈上下丘` / `与七丅一丑丈丅丘丅` | `丑丂丞下世东丆丝不` / `丟不业丏七丗丈丌丁` | Same adjacent bool property pair; RVAs `0x44665E4` / `0x446663C` |
| ConnectUsingSettings | `丟丙下世丒不丐丘丛(string)` | `丏东丁丕专世丈丄上(string)` | Same first connection overload in ordered PUN API; RVA `0x4468528` |
| Disconnect | `丑丑万丂上丅丌世丝()` | `丟丙与丙丞七三丙丗()` | Same ordered no-argument disconnect entry; RVA `0x44633D4` |
| Switcher setup | `丝且丟世专丕丟丐丝(HiddenSettings)` | `丒与下丐丕丏东丆不(HiddenSettings)` | Unique void/HiddenSettings method; RVA `0x2179448` |
| AppID selector | `丞不与丗丏丕丄七三(HiddenSettings)` | `丄丟丒丏丈丘不丁丁(HiddenSettings)` | Unique string/HiddenSettings method; RVA `0x21795E4`; direct A64 `BL` from setup at `0x21794F0` |
| ServerSettings.UseCloud | `丛丙丄世一丁业丟专` | `一不丘世上专丞丛世` | Matching one- and two-argument overload pair |
| NetworkingPeer | `丛丗丄与丐与三丐丄` | `丈专丑丛丕丁丏丗丟` | Same inheritance and 67-field/131-method PUN peer shape; stable callbacks |
| LoadBalancingPeer | `丏丗上丂丗世与万丅` | `丅丙丆三丒丞丈且世` | Same PhotonPeer-derived 1-field/23-method shape |
| RoomOptions serializer | `丏丙丞一专不万万世` | `丕丐丈丈东丟不与三` | Unique `(Dictionary<byte, object>, RoomOptions)` method; RVA `0x4B1C950` |
| RoomOptions | `不丈丏一丏万丅丂下` | `丏上丆与下业不丄丈` | Same 12-field/29-method property layout |
| Plugins field | `丂下丒不丞业专不丏` | `丟世丘一丗丄万不下` | Second `string[]` property in the matching RoomOptions layout |

The PUN enum values are unchanged: EU is `CloudRegionCode 0`, Photon Cloud is
`HostType 1`, and `ClientState.Disconnected` is `15`.

## Early IL2CPP readiness crash

The first ARM64 bootstrap called `il2cpp_domain_get()` immediately after the
exports became resolvable. In this Unity build that export is not a safe
pre-initialization poll: while the root-domain singleton is still null it
enters a lazy path and crashes at `libil2cpp+0x1299108` with `x0 == 0`
(`fault +0x132`). The observed native chain was
`0x12DE5FC -> 0x12B463C -> 0x1299108`.

The fixed bootstrap validates the exact A64 instructions used by
`il2cpp_domain_get()` and waits for its root-domain slot at RVA `0x06C74618`
to be published by `il2cpp_init`. It never calls the unsafe lazy path and does
not rely on an arbitrary sleep.

## Frozen-client startup guards

The live service now advertises 25.x to the frozen 23.1.3 client, producing a
mandatory update overlay, while the retired backend causes
`ConnectionLostChecker` to show a false no-internet modal. Both overlays leave
the loading screen visually stuck at 90% before Auth/Photon starts.

`startup_guards_2313.h` keeps the stock AppsMenu coroutine but disables the
live `UpdatesChecker.Start` request, global version-block predicates, all three
update-banner presentation paths, the retired `ConnectionLostChecker.Update`
poll, and the exact `InfoWindowController` no-internet panel setter. Other
information windows and real Photon errors remain untouched.

## ARM64 ABI change

The old ARM32 generated static methods carried an unused leading static-context
argument. The 23.1.3 AArch64 method pointers use only explicit managed
arguments followed by `MethodInfo`. All static hook typedefs were changed
accordingly; instance hooks retain `this` as their first argument.

## Build configuration

PR builds intentionally do not receive GitHub secrets. They validate the
binary but log that `PHOTON_APP_ID` is empty and pass through the stock
selector. To test a real Photon Cloud application, use either:

```bash
gradle :opg3d:assembleRelease \
  -PPHOTON_APP_ID='<your Photon Realtime App ID>'
```

or a trusted branch/workflow run where `PHOTON_APP_ID` is available as the
repository secret.

Never paste the AppID into source control or device logs. Logs contain only its
length and FNV-1a fingerprint.

## Device test checklist

Capture the complete filtered log from a clean launch through creating and
joining a room:

```bash
adb logcat -c
adb logcat | grep -E "23.1.3|photon|Photon|OnConnectedToMaster|OnCreatedRoom|OnJoinedRoom"
```

Expected sequence:

1. `IL2CPP root domain published` appears; there is no crash at `0x1299108`.
2. Startup compatibility reports `installed 8/8 guards`; neither the update
   nor no-internet modal appears and loading advances past 90%.
3. All three Photon modules report successful hook installation.
4. `AuthSceneController.Awake` and the mapped local completion path appear.
5. `mapped Switcher AppID selector -> configured credential`.
6. Cloud route verification reports `host=1 region=0 ... ready=1`.
7. `ConnectUsingSettings returned 1`.
8. State/status reaches `ConnectedToMaster` (`16`).
9. Room creation logs `OnCreatedRoom`, without return code `32751`.
10. Room join logs `OnJoinedRoom` and state `Joined` (`9`).
11. A second client using the same AppID/version can discover or join the room.

If installation fails, an operation response is nonzero, or the game requests
a disconnect, preserve the lines immediately before and after that event. The
trace layer delegates every callback and will expose the real PUN state and
server return code.
