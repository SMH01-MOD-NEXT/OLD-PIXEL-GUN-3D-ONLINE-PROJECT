# 23.1.3 intermittent anomaly comparison trace

This passive trace is intended for side-by-side comparison of a healthy launch
and an affected launch. It does not override any returned value.

It records the three independent offline decisions: the account/settings flag
(`0x2B79FB4`/`0x2B7A00C`), PUN `offlineMode`, and
`OfflineModController.丐一丆丈世丆七丟丘()`. Setter logs contain before,
requested, and after values. Snapshots are also emitted around
`SettingsTabAccount.OnEnable`.

The same stream records all Veteran chest state calculators (`None`, `CanOpen`,
`CantOpen`, `Unavailable`), availability, and `OnEnable`. Existing modules
already trace Auth state/iterator progress, Player ID, Pixel Pass lifecycle,
post-match presentation, bot tier, Photon connection callbacks, and backend
requests. Together these markers provide one chronological comparison chain.

Filter with:

```text
23.1.3-anomaly|23.1.3-battle-flow|23.1.3-auth|23.1.3-local-backend|23.1.3-identity|23.1.3-pixelpass|23.1.3-post-match|23.1.3-bots|23.1.3-photon|23.1.3-backend
```

## Battle flow trace (missing respawn interface, endless "receiving data")

Two further reported anomalies are traced by the same comparison stream under
the `23.1.3-battle-flow` prefix. Both chains were reduced to a single driver
each with the A64 caller graph of this exact image, so every line below is a
yes/no answer instead of a guess. Nothing is modified: each hook calls the
stock method and returns the stock value.

### Death without a respawn interface

Verified chain, in runtime order:

| Event | Meaning |
| --- | --- |
| `-> local death #N` | `Player_move_c.世丐伟丂业东不上丑` ran; it is the only caller of the respawn coroutine factory (`0x3431B60`, one call site) |
| `respawn window handle -> NULL / present` | `RespawnWindowController.get_window` (`0x34316D8`) instantiates the window from `PrefabHandler`; a NULL handle means the next stock null check raises |
| `respawn coroutine -> state N` | the iterator (`RespawnWindowController/丂丂东丈伟且丈世丑.MoveNext`, `0x3432154`) drives the whole interface |
| `-> respawn show gate` / `<- ... returned` | show/hide gate (`0x3431A40`) |
| `-> respawn camera setup` | camera turn to the killer (`0x3431D9C`) |
| `-> RespawnWindow show` / `<- ... returned` | `0x1394144`, the single call site of the window show |
| `-> killer weapon panel` / `<- ... returned` | `WeaponInfoInRespawnWindow` fill (`0x4340DE8`), the first payload-dependent part |
| `respawn button active=..` | `RespawnWindow.SetRespawnButtonActive` (`0x1393F74`) |

Read it as follows. An entry line (`->`) without its matching exit line (`<-`)
is the state that raised: the camera work is already done at that point, which
is exactly the reported picture (a rotating camera, no buttons, no killer
weapon). If `local death` never appears, the death itself was not delivered
locally. If the coroutine states stop advancing, the stall is inside that
state.

### Profile Stats tab stuck on "receiving data"

| Event | Meaning |
| --- | --- |
| `-> profile request nickname=..` | `PlayerProfileGUI.丌丛一丏丛一上丌丕` (`0x412B754`) asked the backend |
| `-> profile open .. playerData=NULL/present` | `PlayerProfileViewController.千丆丙专一丒丗上且` (`0x412F2A4`), the only consumer of that request |
| `profile data gate -> 0/1` | `0x4131444`; while it answers 0 the pending caption is never hidden |
| `-> profile reveal node` | `0x413204C`, the only code path that disables `pendingDataGameObject` (`+0xD8`) |
| `-> profile views fill flag=..` | `0x413044C` |
| `-> stats tab fill playerData=..` | `PlayerProfileStatsView` fill (`0x412DE30`) |

The caption can only stay on screen for four reasons, and the stream separates
them: the request never runs, the open call carries no `PlayerData`, the reveal
node never fires (it has zero call sites and is invoked by name from the UI),
or its gate answers `0`.

### Comparison procedure

1. Reproduce an affected launch: enter a match, get killed by a bot, then open
   the profile Stats tab.
2. Reproduce a healthy launch with the same two steps.
3. Filter both logs with the pattern above and compare the two chains line by
   line; the first line that differs is the divergence point.

The trace can be switched off with `feature_config::gameplay::battle_flow_trace`
in `main.cpp`.

### Outcome: the respawn divergence point

`log_battle.txt` stopped at `-> killer weapon panel` with no exit line, which
placed the throw inside `WeaponInfoInRespawnWindow.上上三丝上不丂东东` (`0x4340DE8`).
The analysis and the fix live in `docs/PORT_23_1_3_RESPAWN_REPAIR.md`; that
method is now hooked by `respawn_repair_2313.h` instead of this trace, so its
enter/exit pair is printed with the `23.1.3-respawn-repair` prefix.
