# 23.1.3 — the auth completion transaction (the remaining "90%" freeze)

This document covers the freeze that is left **after** the loading stall guard
does its job. For the `InitializeSwitcher` state 35 stall itself see
`PORT_23_1_3_LOADING_STALL.md`.

## The loading screen is no longer the problem

`logcat_2026-08-22_18-10-20.txt` (build tag `23.1.3 ARM64 lobby gate v3`) shows
the whole boot pipeline completing:

| Time | Event |
| --- | --- |
| +001265 ms | watchdog armed (`mode=1`, skip `35`), `8/8 guards`, `4/4 coroutine heartbeats`, `2/2 AppsMenu gates` |
| +009235 ms | `skipping InitializeSwitcher state=35 #1 (ranchoOk=0 bar=0.450)` |
| +009268 ms | Unity: `ParseFullSlotConfig: versionDict not contains version 23.1.3` |
| +009464 ms | `InitializeSwitcher finished on its own at state=45` (bar=0.750) |
| +011893 ms | `LoadMainMenu tail completed (state 1 -> 2)` — no v1 `NullReferenceException` |
| +011917 ms | `AuthSceneController.Awake` |
| +012044 ms | `completion scheduled; state=0/Initial ready=1` |
| +014636 ms | `auth scene destroyed before a ready session was observed` (2592 ms / 98 frames) |
| +014721 ms | `auth scene restarted (instance #2)` |
| +024550 ms | `completion still pending after 300 frames (state=0/Initial ready=1)` |

So the progress the player watches freeze is the auth/session phase that runs
after the Switcher hands over, not `state=35`.

## The decisive evidence is negative

The direct `AuthSceneState` setter (`0x3DC0968`) is hooked from +001265 ms and
**never fires once in the entire capture**. The state is not advancing slowly —
it never moves at all. The scheduled completion machine parks on its very first
await, and `publish_if_ready()` requires

```
ready && (state == FullySynchronized(3) || state == Empty(4))
```

which can therefore never become true, no matter how long the scene lives.
`ready` is already `1` at frame 0, which is why the mismatch is easy to misread
as "almost done".

Supporting managed-side evidence that the remote data backing this step is dead:

- `ParseFullSlotConfig: versionDict not contains version 23.1.3`
- `parse client version info fail: nullable image url`
- `[ConfigSystem.OnWebS_ocketCallback] Unknown configId: None`
- `TierMatchmakingController config: Unknow mode: Christmas2023`, `map not found: Tournament`

The live config service answers, but it has no 23.1.3 entries at all.

## Why the parked iterator could not simply be assumed

`AuthSceneController` has 20 generated iterator classes. Six of them await a
`Task` that only the retired backend can complete, and the awaited field is at a
different offset in each — while three of them share the same field *name*:

| TypeDefIndex | generated class | awaited field | offset | task type | MoveNext RVA |
| --- | --- | --- | --- | --- | --- |
| 2030 | `丑丐丘三且且世丆丕` | `<getRemoteSlots>5__2` | 0x28 | `Task<HashSet<string>>` | `0x3DC80B4` |
| 2031 | `丈丄丒且丏万丝丁丆` | `<getRemoteSlotsTask>5__2` | 0x28 | `Task<HashSet<string>>` | `0x3DC8574` |
| 2032 | `丝丗东丟业丕丅丂丈` | `<task>5__2` | 0x28 | `Task<HashSet<string>>` | `0x3DC89F4` |
| 2035 | `丈丈业丛丈丒不丂丏` | `<task>5__2` | 0x40 | `Task<Dictionary<string, object>>` | `0x3DC99A8` |
| 2036 | `丌与世丐丐丘三一东` | `<task>5__2` | 0x30 | `Task<Dictionary<string, object>>` | `0x3DCA248` |
| 2037 | `丈丛世万业东丌丆不` | `<task>5__2` | 0x30 | `Task<Dictionary<string, object>>` | `0x3DCA650` |

Neither an offset nor a name identifies the scheduled one on its own, and the
factory stub `万丕丂丑丄世丈丁丌()` (`0x3DC3EE0`) is not adjacent to any of these
classes' constructors, so the usual IL2CPP adjacency heuristic does not resolve
it statically either.

## What the build now logs

`backend_local_2313.h` keeps the pointer to the exact `IEnumerator` instance it
hands to `StartCoroutine_Auto` and reads its identity out of the live object.
Every field is resolved **by metadata name** through IL2CPP, never by offset,
and every read is fail-soft.

New lines, all tagged `23.1.3-local-backend`:

- at schedule time — `stock completion coroutine accepted (...); iterator <class> (<ptr>)`
- a chain dump at schedule time, at frame 300, every 900 frames while parked,
  on scene destroy and on fail-closed timeout:

  ```
  <where> chain[<depth>] <generated class> state=<\<\>1__state> after <n> frame(s);
      awaits <field> (<task class>) status=<...> flags=0x........ completed=<0|1>
  ```

- `iterator <class> advanced <a> -> <b>` whenever `<>1__state` actually moves
  (silent while parked, so a stuck machine costs no log spam)

The chain walk follows `<>2__current` up to 4 levels, so a completion iterator
that is parked on a *child* iterator reports the child too. `status` decodes
`System.Threading.Tasks.Task.m_stateFlags`
(`RanToCompletion` / `Faulted` / `Canceled` / `WaitingForActivation` /
`Running` / `NotStarted`).

No behaviour is changed by this commit.

## Reading the next capture

Run the build, let the auth scene sit for at least 90 seconds (the fail-closed
timeout is `kTimeoutFrames = 3600`, roughly 60 s) and **do not background the
app** — the previous capture ended at `onPause` after only 27 s, which is why
no timeout verdict was recorded.

Then read `chain[0]`:

- **`awaits ... status=WaitingForActivation` / `Running`, `completed=0`** —
  confirmed: the transaction is blocked on a remote lookup the retired backend
  will never answer. Fix belongs in the awaited task: hook the method that
  produces it and return an already-completed task carrying a local result. The
  stock machine then drives `Initial -> ... -> FullySynchronized` itself.
- **`... is null, the lookup was never started`** — the parked step is earlier
  than the await; the blocker predicate or a preceding yield is the cause.
- **`status=Faulted`** — the task *did* finish, and the coroutine is swallowing
  the exception. Fix belongs in the continuation, not in the transport.
- **`this class awaits no Task`** — the completion iterator is not one of the
  six above; the chain dump names which class it really is.

## What must not be done

Forcing the state with the direct setter (`0x3DC0968`) to `FullySynchronized`
looks tempting and is the same mistake as the v1 loading guard: the skipped
steps never create the singletons the menu dereferences later, `libopg3d` is
built `-fno-exceptions`, and the resulting managed `NullReferenceException`
unwinds through hook frames with no usable stack. Complete the awaited work
instead of skipping it.
