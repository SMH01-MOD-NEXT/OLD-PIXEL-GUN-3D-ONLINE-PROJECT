# Photon Cloud routing and dead-backend guard

## Confirmed disconnect chain

Extended tracing on 12.5.0 showed the same chain on every attempt:

```text
ConnectUsingSettings -> StatusCode.Connect -> ClientState.Authenticating
-> FriendsController.Update+0x310 -> PhotonNetwork.Disconnect
```

`FriendsController.Update+0x310` was located via `dump1250.cs` from the runtime
call site `libil2cpp.so+0xAA23FC`. This is a local game call, not a server-side
disconnect.

At the same time, `ServerSettings` stayed in `SelfHosted` mode after
`Switcher.SetUpPhoton`, pointing at `rilisoft-us.exitgamescloud.com:5055`. The
`PHOTON_MODE=cloud` build mode used to change only the AppID and wrongly kept
the game-selected route.

## Fixed region

`dump1250.cs` confirms:

```text
ServerSettings.HostingOption.PhotonCloud = 1
CloudRegionCode.eu = 0
CloudRegionCode.none = 4
ServerSettings.UseCloud(string, CloudRegionCode) // RVA 0x8FF47C
```

`BestRegion` is not used: it depends on an asynchronous ping/cache and can both
delay the first connect and spread clients across different regional room
pools. For a shared 12.5.0 online environment all clients are pinned to EU. On
the Photon Cloud side, EU should also remain the only allowed region for the
corresponding AppID.

## What the guard does

1. Before every known connection entry point it calls the stock PUN overload
   `ServerSettings.UseCloud(PHOTON_APP_ID, CloudRegionCode.eu)`.
2. It reads back three invariants: `HostType == PhotonCloud (1)`,
   `PreferredRegion == eu (0)`, and the stored AppID matches the configured
   one.
3. If the method is missing or the read-back does not match, the connection
   is blocked. The guard never guesses offsets and never reports a fake
   success.
4. While PUN is in an active or transitioning state, it does not run
   `FriendsController.Update`. This isolates only the owner of the dead
   HTTP/social backend and provably removes the Disconnect call site. In the
   `PeerCreated`/`Disconnected` states the original Update still runs, so
   local initialization and UI state are preserved.
5. Manual `PhotonNetwork.Disconnect`, server-side disconnect reasons, Photon
   callbacks, rooms, RPC and sync are never replaced.

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

Then a successful Authenticate and a master/game-server transition are
expected:

```text
photon-op: code=230 return=0
ui: ConnectionControl.OnConnectedToMaster ...
```

The old `ServerAddress` value may remain serialized in the asset, but with
`HostType=PhotonCloud` the SelfHosted branch never uses it.
