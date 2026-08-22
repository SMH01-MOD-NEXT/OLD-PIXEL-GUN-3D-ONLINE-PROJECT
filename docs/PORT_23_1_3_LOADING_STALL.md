# 23.1.3 ARM64 — loading stalls on the retired backend

Two different freezes on the way to the main menu, both caused by stock code
that was never meant to run without the 23.1.3 backend. Neither is related to
`PHOTON_APP_ID`: that warning is printed ~400 ms before the first freeze, and
Photon is only used once the menu exists.

Everything below is reproduced from the artifact of PR #31
(`libil2cpp.so` SHA-256 `f0a130c4…deb5c`, Unity 2021.3.14f1, IL2CPP,
arm64-v8a, package `com.pixel.gun3d`). In this binary `RVA == file offset`.

---

## 1. Freeze at 90% — `InitializeSwitcher`, entry state 35

### Evidence

`logcat_2026-08-22_16-37-38.txt`:

| time | line |
| --- | --- |
| `+010688ms` | `InitSwitcher state=33 ranchoOk=0 result=1` |
| `+010705ms` | `InitSwitcher state=34 ranchoOk=0 result=1` |
| `+011.2s` | `W/Unity DontDestroyOnLoad only works for root GameObjects` … `InfoWindowController:Instance()` ← `MoveNext()` |
| — | nothing at all for the remaining ~64 s |

The heartbeat is emitted **after** the original `MoveNext` returns. State 34
returned normally; the next call (entry state 35) produced that warning and
never returned. A coroutine parked on a `yield` keeps producing heartbeats, so
this is a blocked game thread, not a wait.

### Static cross-check

* `Switcher.<InitializeSwitcher iterator>.MoveNext` = `0x217B040`.
* Its only call to `InfoWindowController.Instance` (`0x28F7428`) is at
  `0x217B8A4`, inside the block that writes `<>1__state = 36` at `0x217B8F8` —
  the state-35 body. The same body calls a `0xB8`-byte wrapper at `0x4C9E4E4`
  (tail-calls `MonoBehaviour.StartCoroutine`) and `0x35AD0BC(float,float)`.
* The no-internet panel setter guarded in `startup_guards_2313.h`
  (`0x28F8860`) has exactly one caller in the binary,
  `ConnectionLostChecker` (`0x3DCC24C`), whose `Update` is already suppressed —
  so that guard is inert and was not the cause.
* `<ranchoComplete>5__2` is `0` in every heartbeat: the retired backend never
  answered, which is the state this tail step was never designed to survive.

---

## 2. Freeze at 45% — what the first fix broke

The first version of the guard ended the **whole** enumeration at state 35
(`<>1__state = -1`). `logcat_2026-08-22_17-32-48.txt`:

```
+009014ms 23.1.3-stall: skipping InitializeSwitcher state=35 #1
+009014ms 23.1.3-swt:   InitSwitcher state=35 result=0 (stall guard finished the iterator)
+009015ms 23.1.3-swt:   LoadMainMenu state=0 result=1
+009048ms 23.1.3-swt:   LoadMainMenu state=1 result=1
+011080ms 23.1.3-swt:   LoadMainMenu state=1 result=1      <- last heartbeat ever
+020398ms 23.1.3-startup: skipped retired ConnectionLostChecker.Update #600
```

Two facts fix the diagnosis:

1. The `Update` guard still counts at `+20 s`, so the process and the Unity
   main loop are **alive**.
2. The `LoadMainMenu` heartbeat is time-based (2 s) and stops after `+11.08 s`,
   so that iterator is **not pumped any more**. The coroutine died.

### What `LoadMainMenu` actually does

`Switcher.<LoadMainMenu iterator>.MoveNext` = `0x1F21610`, three states.

State 1 is a timed wait (`0x1F21758`):

```asm
BL    Stopwatch.get_ElapsedMilliseconds
LDR   S0, [X24,#0x128]        ; Switcher field 0x128 == 3.0
MOVZ  W8, #0x447A, lsl 16     ; 1000.0f
SCVTF S1, X0                  ; (float)elapsed
FDIV  S1, S1, S2              ; -> seconds
FCMP  S1, S0
B.mi  0x1F21984               ; seconds < 3.0 -> yield, stay in state 1
```

So field `0x128` is **not** the progress bar (the heartbeats printed `3.000`
all along) — it is the minimum time the loading screen must stay up. The bar
value is field `0x138`, which is what the iterators yield.

After ~3 s the tail runs once (`0x1F217B0`), then yields with state 2 and ends:

```
Stopwatch.Stop()
GameConnect.<static>(enum 0)                    0x14D048C
<static bool @0x3A0> = false
X20 = <static @0x230>        ; CBZ -> throw NullReferenceException (0x1F219DC)
X21 = [X20,#0x78]            ; CBZ -> throw NullReferenceException
WeaponManager.<method>(int)                     0x1428A1C
<static @0x0>.field_0x70 -> Component.get_gameObject() -> SetActive(false)
<scene loader>(string, bool, bool, …)           0x4573458
```

The wait expires around `+12 s` and the tail immediately walks singletons and
managers that the skipped states (36 and up) never created. `libopg3d` is
built with `-fno-exceptions` on purpose (see the long comment in
`CMakeLists.txt`), so a managed `NullReferenceException` unwinds straight
through the hook frame: Unity drops the coroutine, no post-call heartbeat is
ever emitted, and the bar stays at 45% with a live UI. That matches the
capture exactly.

---

## 3. Current policy — skip one step, keep the rest

`loading_stall_guard_2313.h`:

* **Skip, don't finish.** When `MoveNext` is entered at a listed state, the
  guard writes `<>1__state = state + 1` and the hook reports a yield without
  running the body. Every remaining step of `InitializeSwitcher` still runs,
  so the objects the `LoadMainMenu` tail needs are built as usual.
* **Watchdog over both iterators**, naming three failure modes:
  * `blocked` — `MoveNext` was entered and never returned (stuck thread);
  * `not pumped` — it returns but is not called again;
  * `aborted` — `LoadMainMenu` stopped before its tail ran, i.e. a managed
    exception escaped stock code.
* **Pre-call line for `LoadMainMenu` state changes.** With `-fno-exceptions`
  nothing can be caught, so the line printed *before* the call is the last
  evidence available if the stock tail throws.

### Build-time switches

| define | default | meaning |
| --- | --- | --- |
| `OPG3D_2313_INIT_SKIP_STATES` | `35` | comma-separated entry states to skip, e.g. `"35,37"` |
| `OPG3D_2313_INIT_BYPASS_MODE` | `1` | `0` = stock behaviour + watchdog, `1` = skip listed steps, `2` = finish the iterator (v1, breaks the menu tail) |
| `OPG3D_2313_DISABLE_INIT_BYPASS` | `0` | legacy alias for mode `0` |

---

## 4. Verifying a build

Capture **unfiltered** logs — Unity prints managed exception stacks under the
`Unity` tag, and a grep on `OPG3D` hides exactly the evidence needed when a
coroutine dies:

```sh
adb logcat -c
adb logcat --pid=$(adb shell pidof com.pixel.gun3d) > pg3d.log
```

Expected sequence:

```
23.1.3-stall: loading watchdog armed (mode=1, 1 skip state(s), first=35, …)
23.1.3-swt:   InitSwitcher state=34 … result=1
23.1.3-swt:   InitSwitcher state=35 … result=1 (stall guard skipped this step)
23.1.3-swt:   InitSwitcher state=36 … result=1
…
23.1.3-stall: InitializeSwitcher finished on its own at state=N (1 step(s) skipped)
23.1.3-swt:   LoadMainMenu entering state=0 …
23.1.3-swt:   LoadMainMenu entering state=1 …
23.1.3-swt:   LoadMainMenu entering state=2 …
23.1.3-stall: LoadMainMenu tail completed at state=2 (main menu scene requested)
23.1.3: MAIN MENU REACHED
```

Reading a failure:

| line | meaning | next move |
| --- | --- | --- |
| `23.1.3-stall: blocked -- InitializeSwitcher … state=N` | another step blocks the thread | add `N` to `OPG3D_2313_INIT_SKIP_STATES` |
| `23.1.3-stall: blocked -- LoadMainMenu … state=N` | the menu tail blocks | disassemble that state's block |
| `23.1.3-stall: aborted -- LoadMainMenu … state=N` | managed exception in the tail | read the `Unity` tag stack in the unfiltered log |
| `23.1.3-stall: not pumped …` | the coroutine was stopped by the game | check the owner `MonoBehaviour` lifecycle |
