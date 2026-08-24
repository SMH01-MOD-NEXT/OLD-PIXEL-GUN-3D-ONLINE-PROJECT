# 23.1.3 forced online state

## Symptom

On the frozen 23.1.3 client the game behaves as if the device had no internet
access even while Photon is fully joined:

* the in-battle arsenal entry is unavailable;
* end-of-match widgets are present but not presented;
* craft screens show a connection-error banner;
* asset-bundle traffic is refused before a request is made.

The device log contradicts the offline verdict: `op=252/op=253 return=0(Ok)`
with `state=9(Joined)`, so the transport is healthy and only the client-side
offlineness bookkeeping is wrong.

## Root cause map

Every decision point below was resolved from `analys2313/dump2313.cs` and
`analys2313/libil2cpp.so` with `tools/find_callers.py` and
`tools/resolve_rva.py`. Nothing here is guessed.

| Decision point | RVA | Consumers |
| --- | --- | --- |
| `UnityEngine.Application.get_internetReachability` | `0x4441FA0` | 19 call sites: `PGCompany.AssetBundles_v3` (8), `PrivateGamesPanel.Update` (5), `PrivateGamesPanelMiniGame.Update` (5), `FlurryAgentAndroid..cctor` (1) |
| `IosBundleDownloader.HasLossOfWiFiConnection` | `0x20A2970` | bundle download gate |
| `IosBundleDownloader.HasLossOfWiFiConnectionWrapper` | `0x20A2978` | static shim for the same gate |
| `丑丝丕上丐丏万丑丒.HasLossOfWiFiConnection` | dump 540533 | sibling downloader |
| `丛丕丆丝丄丘丕丌丕.HasLossOfWiFiConnection` | dump 544570 | sibling downloader |
| `InternetChecker.丕丌业丐丕丞七丒丑()` | `0x1A22F50` | synchronous HTTP probe, publishes a static verdict |
| `Rilisoft.DisableIfOfflineMode.OnEnable` | `0x3F8215C` | deactivates its own GameObject when offline |
| `OfflineModController.丑万不丘与丆七丗丙(bool)` | `0x4C1FB28` | offline button-group gate |
| `Rilisoft.FortCraftConnectionErrorBanner.OnEnable` | `0x3E3FBCC` | fort craft error banner |
| `Rilisoft.LobbyCraftConnectionErrorBanner.OnEnable` | `0x272930C` | lobby craft error banner |

Key findings that falsified earlier assumptions:

* `Application.internetReachability` does **not** gate the lobby or the
  arsenal. Only bundles, the private-games panels and Flurry read it, so
  forcing reachability alone can never restore the arsenal.
* `InternetChecker.丕丌业丐丕丞七丒丑()` has **zero** `bl` call sites; it runs from a
  background thread. Its body calls `WebRequest.Create` and compares the
  response against a marker, writing its static bool `东丐丐丐丄丑丁七专`
  (offset `0x0`). Polarity confirmed by disassembly: `1` means online
  (`strb w9,[x8]` at `0x1A23008`), `0` means offline (`strb wzr,[x8]` at
  `0x1A23010`).
* `Rilisoft.DisableIfOfflineMode.OnEnable` has no branches worth keeping: it
  reads a static offline flag and tail-calls `GameObject.SetActive(false)` on
  its own object. This is the component that strips backend-dependent UI.
* `OfflineModController.丐一丆丈世丆七丟丘()` is **not** a connectivity check. Its
  body calls `ExperienceController` and `Progress` helpers, so it is a
  progress gate and must be left alone.
* The 16.1.0 port hooks the same button-group gate under the obfuscated name
  `丁丅三七丆丙丛不丈` (`battle_ui_1610.h`). That name does not exist in 23.1.3;
  the equivalent is `丑万不丘与丆七丗丙(bool)`.

## Fix

`opg3d/src/main/cpp/online_state_2313.h` installs ten metadata-resolved hooks
(no absolute RVAs in code, fail-closed through `hook::install`):

1. `get_internetReachability` returns `ReachableViaLocalAreaNetwork` (`2`),
   which covers all 19 call sites at once.
2. The three `HasLossOfWiFiConnection` overrides and the static wrapper report
   `false`.
3. The `InternetChecker` probe is short-circuited: no blocking request against
   a dead host, and the connected verdict is written straight into the static
   field. The write happens only inside the hook, where the declaring class is
   guaranteed to be initialized, so no static storage is touched too early.
4. `DisableIfOfflineMode.OnEnable` becomes a no-op, keeping offline-gated
   objects alive.
5. The `OfflineModController` button-group gate is always called with
   `offline = false`.
6. Both craft connection-error banners are hidden through
   `Component.get_gameObject` + `GameObject.SetActive(false)`, the same way the
   startup guards hide retired modals.

Readiness is reported as `reachability && offline-disable`; the remaining hooks
are optional and only downgrade behaviour when absent.

## Deliberately left stock

* `InfoWindowController` - its no-internet panel is already forced hidden by
  `startup_guards_2313.h`. shadowhook is unique per target, so it must not be
  hooked twice.
* `InGameConnection` (`0x2ED1290`, `0x2ED12E0`, `0x2ED1CF4`, `Update`
  `0x2ED2FD8`) - reconnect bookkeeping with unknown polarity. Photon already
  owns the real connection state; revisit only if a device log still shows the
  arsenal blocked after this change.
* `ConnectionControl.SetConnectPanel` (`0x15CAFF0`) - candidate overlay, not
  yet proven to participate.

## Log markers

Prefix `23.1.3-online`:

```
23.1.3-online: installed 10/10 forced-online hooks (reachability=1 offline-disable=1 ...)
23.1.3-online: reported reachability=2 (local area network) #1; ...
23.1.3-online: short-circuited InternetChecker HTTP probe #1; ...
23.1.3-online: kept an offline-disabled object alive #1
23.1.3-online: forced the offline button group online #1
23.1.3-online: hid retired FortCraftConnectionErrorBanner #1
```

`init:` now also reports `online-state=%d` when the port is incomplete.

## Verification

1. Build:
   `./gradlew :opg3d:assembleDebug :opg3d:assembleRelease -PPHOTON_APP_ID=... -PPHOTON_MODE=cloud -PCMAKE_VERSION=3.22.1 --no-daemon`
2. Confirm `23.1.3-online: installed 10/10 forced-online hooks` at startup.
3. Enter a battle and open the arsenal; no offline banner should appear.
4. Confirm craft screens no longer show a connection-error banner.
