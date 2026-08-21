# Second-match false cheat detection: abuse-slot persistence sink

## Symptom

The first battle completes normally, but entering the second battle in the same client process can show `CHEAT DETECTED`.

## Root cause

The v12.1 guard redirected four obfuscated anti-abuse key builders to fixed, recognisable names:

- `opg3d_inert_slot_a`
- `opg3d_inert_slot_b`
- `opg3d_inert_slot_c`
- `opg3d_inert_slot_d`

Those names were assumed to be inert because no unmodified game code knows them. That assumption misses the repeated call through the hook itself: the same key builder returns the same replacement name on the next launch, lobby entry, or battle-scene load.

ARM32 direct-call analysis of the 13.2.1 IL2CPP binary confirms all three live owners persist and later read their redirected key:

| Owner | Key-builder call site | Storage flow |
|---|---:|---|
| `AppsMenu.<Start>c__Iterator1.<>m__0` | `0x1BB9558` | `hasKey -> getString -> setString -> MeetTheCoroutine` |
| `Initializer.Awake` | `0xE34EAC` | `hasKey -> getString -> setString` (two write sites) |
| `MainMenuController.<Start>c__Iterator2.MoveNext` | `0xAD72C0` | `hasKey -> getString` and the downstream abuse path |

The relevant `Initializer.Awake` method runs again when the next battle scene is created. A fixed redirected key can therefore carry the first battle's timestamp into the second battle. The key is unknown to stock callers, but it is not unknown to the hooked caller on its next invocation.

## Fix

`abuse_slot_sink.h` virtualizes only the four redirected names at the common `Storager` boundary:

- `Storager.hasKey(key)` returns `false`.
- `Storager.getString(key)` returns an empty managed string.
- `Storager.setString(key, value)` is discarded.
- Every other key is forwarded to the original method unchanged.

This keeps normal player saves, cloud state, settings, and all unrelated storage operations stock. It also ignores stale replacement entries left by v12.1 and avoids creating new persistent garbage keys.

All three hooks are required. If any one cannot be installed, phase 0 is reported as incomplete instead of claiming the guard is fully armed.

## Validation

The new module was syntax-checked with C++17, `-Wall -Wextra -Werror`.

Runtime validation must use one uninterrupted process:

1. Install the new APK/library and clear logcat (`adb logcat -c`).
2. Launch the client once.
3. Enter and finish battle 1.
4. Return to the lobby and enter battle 2 without restarting the app.
5. Save the full logcat through the beginning of battle 2.

Expected startup log:

```text
cheat-guard: virtual abuse-slot sink armed (hasKey=false, getString=empty, setString=no-op for opg3d_inert_slot_a..d; all other Storager keys are stock)
```

Expected transition logs include a reported-absent read and/or a discarded write for `opg3d_inert_slot_[a-d]`. `CHEAT DETECTED` should not appear in battle 2. Existing `HackDetected` and `AbuseMethod` checks should remain zero.
