# 23.1.3 startup SIGSEGV in InitializeSwitcher (2026-08-25)

Two consecutive launches on a freshly wiped profile died about 5.5-6 s after
process start, i.e. while the loading bar was still on screen. This note records
what the tombstone proves, what it does not, and why `maps_unlock_2313` is no
longer installed.

## Tombstone facts

```
signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x65005f007401a6
x0  0065005f00740073   x1  <stack>   x2  0000000000000018   x3  0
x7  e481b8e486b8e400
pid 10486, tid 10551, name UnityMain
```

Two register observations matter:

* `x0 = 0x0065005f00740073` is not a plausible pointer. Read as UTF-16LE it is
  the text `st_e`, and the faulting address is `x0 + 0x133`. Something
  dereferenced a managed object whose memory now holds string bytes.
* `x7 = 0xe481b8e486b8e400` is UTF-8 continuation bytes from the obfuscated
  CJK metadata names (`... U+4E01 U+4E06 NUL`), i.e. the surrounding memory is
  a name/literal region rather than a live object graph.

Both are the signature of a **dangling or already-released managed object being
used as a live reference**, not of a null dereference (a null would surface as a
managed exception, not `SEGV_MAPERR`).

## Symbolized stack

`libil2cpp.so` frames resolved against `dump2313.cs` by nearest preceding RVA.
Frames `#0`-`#13` were not captured, so the exact fault instruction is unknown.

| # | Module | Symbol |
| --- | --- | --- |
| 33-43 | libunity | player loop / JNI entry |
| 31-32 | libil2cpp | unresolved (RVA gap) |
| 30 | libil2cpp | `UnityEngine.SetupCoroutine.InvokeMoveNext + 0xd4` |
| 29 | **libopg3d** | `startup_trace_2313::hook_sw_start + 200` |
| 28 | libil2cpp | `Switcher.<Start iterator>.MoveNext + 0x194` |
| 27 | **libopg3d** | `startup_trace_2313::hook_sw_init + 320` |
| 26 | libil2cpp | `Switcher.<InitializeSwitcher iterator>.MoveNext + 0x2b88` |
| 25 | libil2cpp | unresolved (RVA gap) |
| 24 | libil2cpp | `UnityEngine.Object.Instantiate(Object, Vector3, Quaternion) + 0x104` |
| 23 | libil2cpp | `UnityEngine.Object.Internal_InstantiateSingle + 0x84` |
| 14-22 | libunity | `Instantiate` internals (clone / serialization walk) |

### What this proves

1. The crash is inside `Object.Instantiate`, called by the stock body of the
   `InitializeSwitcher` coroutine. The source object handed to `Instantiate` is
   garbage by the time libunity walks it.
2. `hook_sw_init + 320` is the call-through site in `startup_trace_2313.h`
   (`g_sw_init_next(self, method)`), so the trace hook ran the original. The
   stall-guard skip branch returns *without* calling the original and therefore
   cannot be on this stack. The tracing module is a passenger here, not the
   cause.
3. `hidden_items_2313` is **not** on the stack and cannot be. It performs no
   managed work at install time and is pumped from
   `MainMenuController.Update` starting at menu frame `kWarmupFrames`, which is
   reached only after the main menu scene exists. The weapon sweep never ran.

### What this does not prove

The specific object that went stale. Frames `#0`-`#13` are missing, no
`OPG3D`-tagged lines were captured alongside the tombstone, and method bodies
cannot be inspected (`dump2313.cs` is declaration-only and no aarch64
disassembler is available), so the culprit is reasoned about from what the step
consumes rather than observed.

## Why maps_unlock_2313 is no longer installed

The maps work was withdrawn after it crashed the game, but only the pull request
was withdrawn: `maps_unlock_2313::install_hooks()` was still called from
`main.cpp` on `23.1.3`, so every build cut from that branch still ran it.

That module hooks the `SceneInfoController` per-mode bucket getters and, with
`kOpenEveryMode = true`, appends every entry of `allScenes` into every mode's
`avaliableScenes`. Startup walks those lists and instantiates per-map objects
from them. Scenes that a mode never advertised can carry asset references that
were never resolved or whose bundle is not loaded, which is exactly how a
released native object reaches `Instantiate`. That makes it the leading suspect
for this tombstone, consistent with the earlier report that the maps build
crashed.

The include and the install call are removed. `maps_unlock_2313.h` stays in the
tree as reference material; nothing compiles it, because it is header-only and
no translation unit includes it.

## Confirming on device

After this change the loading bar should complete. If it still dies at the same
place, the next build needs two things the current capture lacks:

```sh
# 1. the OPG3D log around the crash -- the last line pins the exact step
adb logcat -s OPG3D | grep -E '23\.1\.3-swt:'

# 2. the full tombstone including frames #0-#13
adb shell ls /data/tombstones
adb shell cat /data/tombstones/tombstone_XX
```

The last `23.1.3-swt: InitSwitcher state=N` line before the fault names the
state-machine position that crashed, which narrows the step from "somewhere in
InitializeSwitcher" to a single yield. The remaining candidates to bisect, in
order, are `assets_data_2313` (it unpacks an in-APK assets/data payload into the
game's resource root, and a wiped profile re-runs that unpack), `obb_provisioner`
(same reasoning for the OBB), and the `loading_stall_guard_2313` skip of the
state-35 step, which by design leaves part of `InitializeSwitcher` unexecuted.
