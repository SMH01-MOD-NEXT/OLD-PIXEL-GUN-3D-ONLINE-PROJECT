# CustomHunger delayed-punishment guard

## Runtime evidence

The v12.2 device log ended with:

```text
net: PhotonNetwork.Disconnect requested state=9 server='80.93.214.186:5056'
pc=libil2cpp.so+0x1acca2c
```

`0x1ACCA2C` is the return address immediately after the branch at
`CustomHungerBase.set_isShow(bool) + 0x8C`. The branch target is
`GameConnect.Disconnect()`. The same setter then tail-calls
`SceneManager.LoadScene()`.

The caller graph has only three direct setter callers in 13.2.1:

- `NotificationController.Update()` at `0x7F3158`;
- `NotificationController.Update()` at `0x7F3198`;
- `Switcher.<InitializeSwitcher>c__Iterator1.MoveNext()` at `0xEF0568`.

The first update call follows `OldFrameSelect.IsShow()`. The second uses
`NotificationController.playTime`, `savedPlayTime`, `startPlay` and
`customPanelShowed`, which explains a delayed appearance after several matches
rather than a deterministic second-match trigger.

Photon operation responses immediately before the event were successful
(`return=0`). Runtime entries from the Photon, FriendsController and Storager
hooks prove that ShadowHook is executing installed trampolines; this was not an
engine-install false positive.

## Destructive second stage

`CustomHungerBase.set_isTableUpdated(true)` contains all of the following in
its stock body:

- `PlayerPrefs.DeleteAll()` and `PlayerPrefs.Save()`;
- `Storager.setInt(...)` and `Storager.setString(...)`;
- `CloudSyncController.ApplyChanges(...)`;
- the `UpdateInfo()` coroutine, which builds a legacy WWW request.

Both destructive methods begin with an exact `value == true` gate.

## Fix

`custom_hunger_guard.h` installs metadata-resolved, required hooks for:

- `CustomHungerBase.set_isShow(bool)`;
- `CustomHungerBase.set_isTableUpdated(bool)`.

`true` is refused. `false` is forwarded unchanged. No global Photon disconnect,
normal scene load, NotificationController update, PlayerPrefs API or ordinary
Storager key is patched.

## Expected device proof

At startup:

```text
custom-hunger-guard: armed (...)
```

At the old delayed trigger:

```text
custom-hunger-guard: blocked CustomHungerBase.set_isShow(true); ...
```

There must be no following disconnect line whose caller is
`libil2cpp.so+0x1acca2c`, and the punishment scene must not appear.
