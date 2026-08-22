# 23.1.3 ARM64 - the 90% loading freeze

This note documents why the ported 23.1.3 build stopped at 90% on the loading
screen and what `opg3d/src/main/cpp/loading_stall_guard_2313.h` does about it.

## What the capture shows

Source: `logcat_2026-08-22_16-37-38.txt`, build tag `23.1.3 ARM64 lobby gate v3`,
package `com.pixel.gun3d`, pid 3710, Unity main thread 3772, arm64-v8a.

| Event | Time | Line |
| --- | --- | --- |
| AppID selector passthrough | +010307 ms | `23.1.3-photon: AppID selector passthrough` |
| Photon secret missing | +010310 ms | `23.1.3-photon[Switcher.SetUpPhoton/end]: PHOTON_APP_ID is empty` |
| Loading step 33 done | +010688 ms | `23.1.3-swt: InitSwitcher state=33 progress=3.000 ranchoOk=0 result=1` |
| Loading step 34 done | +010705 ms | `23.1.3-swt: InitSwitcher state=34 progress=3.000 ranchoOk=0 result=1` |
| Last managed activity | +011.2 s | `DontDestroyOnLoad only works for root GameObjects...` with `InfoWindowController:Instance()` called from the InitializeSwitcher iterator `MoveNext()` |
| Rest of the capture | ~64 s | complete silence from the process |

`PHOTON_APP_ID is empty` is **not** the cause. It is reported 400 ms before the
freeze, the artifact build intentionally ships without the secret, and Photon is
only used after the main menu exists.

## Why this is a blocked step, not a stalled wait

The heartbeat in `startup_trace_2313.h` logs **after** the original `MoveNext`
returns. Two consequences:

* the call entered with `<>1__state == 34` returned normally (`result=1`);
* the following call (entry state 35) emitted the Unity warning above and never
  produced a heartbeat.

If the coroutine had merely been suspended on a yield instruction, `MoveNext`
would have returned and the heartbeat would keep printing every 2 s. Instead the
entire process stops logging (no Unity, AppsFlyer, GC or JNI output for the
remaining minute), which is what a game thread stuck inside one managed step
looks like.

## Static cross-check

`dump2313.cs` + `libil2cpp.so`
(SHA-256 `f0a130c4e8d9487059eab4b0f08462f6aa7d057510cada0fd6fb3043c77deb5c`,
Build ID `57fcc18d2db06212416d480d53c0f881ee47c52a`):

* `Switcher` InitializeSwitcher iterator `MoveNext` - RVA `0x217B040`.
* The **only** call to `InfoWindowController.Instance()` (RVA `0x28F7428`)
  inside that method is at RVA `0x217B8A4`; that basic block ends by writing
  `<>1__state = 36` at RVA `0x217B8F8`, so it is the state-35 body - exactly the
  step the log stops in.
* The no-internet panel setter hooked in `startup_guards_2313.h`
  (RVA `0x28F8860`) has exactly one caller in the whole binary,
  `ConnectionLostChecker` at RVA `0x3DCC24C`. `ConnectionLostChecker.Update` is
  already suppressed and the capture contains no
  `suppressed false no-internet modal` line, so that guard never runs and cannot
  be responsible for the freeze.
* `<ranchoComplete>5__2` stayed `0` for every state, i.e. the retired 23.1.3
  game backend never answered - the condition under which this tail step was
  never exercised by the original developers.

## The fix

`loading_stall_guard_2313.h`:

1. **Bypass.** When the InitializeSwitcher iterator is entered at
   `<>1__state >= 35`, the stock body is not called. The guard writes the Roslyn
   "iterator finished" marker (`<>1__state = -1`) and reports completion, so the
   `foreach` inside `Switcher.Start` ends normally and the loading coroutine
   continues into `LoadMainMenu()`. Disposal in that state is a no-op, so nothing
   is left half-torn-down. Only the retired-service decoration of the loading
   screen is skipped.
2. **Watchdog.** A detached native thread stays armed until `LoadMainMenu` runs
   and reports any remaining stall once per 15 s, distinguishing:
   * `23.1.3-stall: blocked ...` - `MoveNext` was entered and has not returned;
   * `23.1.3-stall: not pumped ...` - `MoveNext` returns but is no longer called.

   That single distinction is what the original capture could not provide.

Build-time switches:

* `-DOPG3D_2313_INIT_BYPASS_STATE=<n>` - move the bypass to another state.
* `-DOPG3D_2313_DISABLE_INIT_BYPASS=1` - keep stock behaviour, watchdog only.

## How to verify a build

```sh
adb logcat -c
adb shell monkey -p com.pixel.gun3d -c android.intent.category.LAUNCHER 1
adb logcat --pid=$(adb shell pidof com.pixel.gun3d) | grep -E "23.1.3-swt|23.1.3-stall|MAIN MENU"
```

Expected sequence:

```
23.1.3-stall: loading watchdog armed (bypass enabled at state>=35, ...)
23.1.3-swt: InitSwitcher state=34 ... result=1
23.1.3-swt: InitSwitcher state=35 ... result=0 (stall guard finished the iterator)
23.1.3-swt: LoadMainMenu state=... result=1
23.1.3-stall: LoadMainMenu is running (1 bypassed step(s)); loading watchdog disarmed
23.1.3: MAIN MENU REACHED
```

If the game still stops, the next capture will contain a `23.1.3-stall:` line
naming the state and the failure mode; raise
`OPG3D_2313_INIT_BYPASS_STATE` or hand that line over for the next pass.
