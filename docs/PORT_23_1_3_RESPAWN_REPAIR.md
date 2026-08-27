# 23.1.3 respawn interface repair

## Symptom

After a bot kills the local player the kill camera turns to the killer and
nothing else appears: no respawn button, no killer weapon panel, no loadout.
The player can only watch the camera until the match ends. In the healthy
(rare) case the same window builds completely.

## Evidence

The passive chain trace from `battle_flow_trace_2313.h` (`log_battle.txt`,
PID 32401, 15:50:04) stops mid-chain:

```
-> local death #1 self=0x75675d2000; the respawn coroutine is started from here
respawn coroutine -> state 0
-> respawn camera setup killerInfo=present
<- respawn camera setup returned
-> RespawnWindow show self=0x75e1f337e0 killerInfo=present
-> killer weapon panel killerInfo=present dps=42.00/10.00/61.00
<- local death #1 returned
```

There is no `<- killer weapon panel returned`, no `<- RespawnWindow show
returned`, no further coroutine state and no `SetRespawnButtonActive` line.
The window object itself is healthy (`RespawnWindow.OnEnable` and
`respawn window handle -> present` appear earlier), and the killer payload is
present. So the managed exception is raised inside

```
WeaponInfoInRespawnWindow.上上三丝上不丂东东   0x4340DE8
```

and it unwinds through `RespawnWindow.丐丆丙专一丒丗上且` (`0x1394144`, call site
`+0xB4C`) and the iterator `MoveNext` (`0x3432154`). The states that would
enable the buttons (`SetRespawnButtonActive` at `MoveNext +0xC5C` and `+0xCE4`)
never run, which is exactly the reported picture: the camera work is done, the
interface is not.

## Why the stock method raises

The last instruction of the method is `bl 0x1291FD8` at `0x4341560`, the il2cpp
null-reference thrower, and every `cbz` in the body jumps there. On the branch
taken for a killer with a positive efficiency value all of these must be
non-null:

| Site | Dependency |
| --- | --- |
| `0x4340ED4` | `killerInfo` argument |
| `0x4340F30` | `Component.get_gameObject()` result |
| `0x434110C` / `0x4341130` | `丂丞世万丅下万丌业()` `0x4340D74`, the lazily cached `itemImage` |
| `0x43410BC` | `eventItemLabel` `+0x80` |
| `0x43411E0` | `itemNameLabel` `+0x20` |
| `0x4341204` | `barPanel` `+0x28` (only when efficiency > 0) |
| `0x4341220` | `barSprite` `+0x30` (only when efficiency > 0) |
| `0x4341234` | `arrowDown` `+0x38` (only when efficiency > 0) |
| `0x4341258` | `arrowUp` `+0x40` (only when efficiency > 0) |
| `0x43412E8` | `headerLabel` `+0x88` (inside its own Unity null test, so safe) |
| `0x4341360` | `WeaponManager.丐丈丁丒丏丗丈一丐()` `0x141E150`, the event weapon set |
| `0x4341024` / `0x4341040` | `不丂丏不与专丌丁东.下丌丑丁下丟丛丘上()` `0x3D14BD8`, only inside the offer-item branch |

The managed exception cannot be caught in the hook: this library is built with
`-fno-exceptions` on purpose (see `CMakeLists.txt`; two incompatible stack
unwinders live in one process and the crash happens during the handler search
phase, before any `catch` could run). The fix therefore has to prevent the
throw, not catch it.

## The fix - `respawn_repair_2313.h`

Two layers, both delegating to stock code whenever the data is healthy.

1. **Precondition guard** on `WeaponInfoInRespawnWindow.上上三丝上不丂东东/4`.
   Before the stock body runs, exactly the values it dereferences are read
   (`get_gameObject()`, the sprite getter, `eventItemLabel`, `itemNameLabel`,
   and, for a positive efficiency, `barPanel`, `barSprite`, `arrowDown`,
   `arrowUp`, plus the `WeaponManager` event weapon set). If one of them is
   missing, the panel is hidden the way the stock empty-weapon path hides it
   (`SetActive(false)` on its own game object), the stock call is skipped and
   the name of the missing dependency is logged. `RespawnWindow.Show` then
   returns normally, the coroutine keeps running, and the buttons and loadout
   appear. An unknown field name fails open, i.e. stock code still runs.
2. **Watchdog** on `RespawnWindow.Update/0`. The guard marks the stock fill as
   in flight and clears the mark when it returns. If the mark survives to the
   next frame the stock fill raised for a reason outside the list above, so the
   watchdog re-arms the respawn button once with
   `SetRespawnButtonActive(true, false)`. The player is never left with only a
   rotating camera.

The killer payload, the respawn delay, the camera setup and every healthy panel
keep running stock code unchanged.

## Log lines

```
adb logcat -s OPG3D | grep -E "23.1.3-(respawn-repair|battle-flow):"
```

| Line | Meaning |
| --- | --- |
| `guard=OK watchdog=OK (...)` | install summary; `MISSING` entries name what could not be resolved |
| `-> killer weapon panel #N dps=..` / `<- .. returned` | healthy panel, stock code ran to completion |
| `killer weapon panel skipped #N (X is null)` | the guard fired; `X` is the exact missing dependency |
| `stock killer weapon panel raised .. re-arming the respawn button` | the watchdog recovered a throw outside the guarded list |

A fixed run shows either the enter/exit pair or a `skipped` line, and in both
cases the coroutine continues, so `<- RespawnWindow show returned` and
`SetRespawnButtonActive` follow. If a `re-arming` line appears, send the log:
the remaining candidate is the offer-item branch singleton `0x3D14BD8`.

## Switch

`feature_config::gameplay::respawn_repair` in `main.cpp`. `false` restores the
stock (throwing) behaviour. The chain trace stays on its own switch,
`feature_config::gameplay::battle_flow_trace`; the panel hook itself moved out
of the trace module, because one method can only carry one hook.
