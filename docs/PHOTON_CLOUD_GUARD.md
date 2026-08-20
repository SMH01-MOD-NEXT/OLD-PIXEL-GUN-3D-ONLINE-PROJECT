# Photon Cloud routing and dead-backend guard

## Confirmed failure causes

### Dead backend + local disconnect

Extended tracing on 12.5.0 showed the same chain on every attempt:

```text
ConnectUsingSettings -> StatusCode.Connect -> ClientState.Authenticating
-> FriendsController.Update -> PhotonNetwork.Disconnect
```

On 12.5.0 the call site was located via `dump.cs` from the runtime address
`libil2cpp.so+0xAA23FC` as `FriendsController.Update+0x310`. Absolute offsets
are build-specific; in 13.2.1 they differ, so the guard hooks the entire
`FriendsController.Update` method and does not depend on offsets. This is a
local game call, not a server-side disconnect.

At the same time, on 12.5.0 `ServerSettings` stayed in `SelfHosted` mode after
`Switcher.SetUpPhoton`, pointing at the dead `rilisoft-us.exitgamescloud.com:5055`
address. That string is gone from the 13.2.1 binary — the default route is
different but just as dead.

### BestRegion cold start in 13.2.1

On the first launch the `UseCloudBestRegion` path runs an asynchronous region
selection. Before its ping coroutine writes `PUNCloudBestRegion` into
PlayerPrefs, the client already sends Authenticate with `region=none`:

```text
photon-op: code=220 return=0
photon-op: code=230 return=32756
             debug='Cloud public / Region none is not available.'
```

About 20–30 seconds later the best region is cached and the next attempt
succeeds, which is why the "check your connection" error only appeared on the
first launches.

`dump1321.cs` confirms:

```text
ServerSettings.HostingOption.PhotonCloud = 1
CloudRegionCode.eu = 0
CloudRegionCode.none = 4
```

## What the guard does

1. Before every known connection entry point it calls the stock PUN overload
   `ServerSettings.UseCloud(PHOTON_APP_ID, CloudRegionCode.eu)`.
2. It reads back three invariants: `HostType == PhotonCloud (1)`,
   `PreferredRegion == eu (0)`, and the stored AppID matches the configured
   one. If the SDK method is unavailable or the result was overwritten, a
   limited field fallback applies the same values directly. No fake success
   is ever reported.
3. While PUN is in an active or transitioning state, it does not run
   `FriendsController.Update`. This isolates only the owner of the dead
   HTTP/social backend and provably removes the Disconnect call site. In the
   `PeerCreated`/`Disconnected` states the original Update still runs, so
   local initialization and UI state are preserved.
4. Manual `PhotonNetwork.Disconnect`, server-side disconnect reasons, Photon
   callbacks, rooms, RPC and sync are never replaced.

The fixed EU region removes the cold-start ping/cache entirely and guarantees
that all clients with the same AppID and AppVersion land in the same regional
room pool. On the Photon Cloud side, EU should remain the only allowed region
for this application.

## Expected logs

Before connecting:

```text
trace: ServerSettings.UseCloud(region=0) ...
settings[UseCloud(region)/end]: host=1(PhotonCloud) ... region=0 ...
cloud-force[...]: ... host=1(PhotonCloud expected=1) region=0(eu expected=0) ... ready=1
```

During the handshake these lines are normal:

```text
backend-guard: skipped FriendsController.Update ... state=20(Authenticating)
```

Authenticate should then succeed without the region error:

```text
photon-op: code=230 return=0
ui: ConnectionControl.OnConnectedToMaster ...
```

`code=220` (GetRegions) is no longer needed for a normal connection, and a
`32756 / Region none` response after this change counts as a regression.

The old `ServerAddress` value may remain serialized in the asset, but with
`HostType=PhotonCloud` the SelfHosted branch never uses it.
