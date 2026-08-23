# Port 23.1.3 — crafting, clan stock, lobby catalogue and weapon modules

This document describes the second gameplay port onto the `23.1.3` branch, after
[`PORT_23_1_3_PROGRESSION.md`](PORT_23_1_3_PROGRESSION.md) landed offline
currency and level progression.

Five behaviours from the `16.1.0` branch are brought forward:

| # | Behaviour | Header |
|---|-----------|--------|
| 1 | Weapon crafting (free craft details) | `crafting_2313.h` |
| 2 | Dead-clan workaround (clan stock always available) | `crafting_2313.h` |
| 3 | Lobby craft catalogue grant | `lobby_catalog_2313.h` |
| 4 | "No connection" workaround inside the craft screens | `crafting_2313.h` |
| 5 | All weapon modules unlocked at level 10 | `weapon_modules_2313.h` |

All three headers are header-only and wired from `main.cpp`. `CMakeLists.txt`
does not enumerate headers, so no build-system change was needed.

## Why 23.1.3 needed this

23.1.3 shipped as an always-online build. Three separate subsystems fail closed
when the original backend is unreachable:

* **Craft timers.** `FortsManager` and `LobbyItemsController` each expose a
  `Nullable<long>` "what time is it on the server" accessor. Offline it returns
  an empty `Nullable`, and every craft slot renders as *no connection*.
* **Clan stock.** `ClansController`'s static part-count / has-part pair is fed
  by the clan service. With the service gone they answer `0` / `false`, so every
  clan recipe is permanently locked.
* **Lobby catalogue.** `LobbyItemsController`'s craft list is populated by the
  backend, so offline the lobby craft screen is simply empty.

Weapon modules are a fourth case: the player only owns what the progression
service granted, and the two `AddAll…DEV()` helpers that used to unlock the set
were stripped to bare `RET` stubs in this build.

## 1, 2, 4 — `crafting_2313.h`

| Role | Class | Kind | RVA |
|------|-------|------|-----|
| Fort/clan craft clock | `Rilisoft.FortsManager` | hook, static → `Nullable<long>` | `0x03B81348` |
| Lobby craft clock | `Rilisoft.LobbyItemsController` | hook, static → `Nullable<long>` | `0x03F7B588` |
| Fort connection banner | `Rilisoft.FortCraftController` | hook → no-op, instance | `0x03E41010` |
| Lobby connection banner | `Rilisoft.LobbyCraftController` | hook → no-op, instance | `0x02730254` |
| Local UTC clock | global-namespace time helper | resolve only, static `long` | `0x04CA9798` |
| Detail gate | `Rilisoft` detail inventory | hook → `true`, static `bool(string,string)` | `0x0227F8B0` |
| Owned details | `Rilisoft` detail inventory | hook → `max(orig, 99999)`, static `int(string)` | `0x0227F954` |
| Clan part count | `ClansController` | hook → `max(orig, 99)`, static `int(string, ClanItemType)` | `0x03C2A078` |
| Clan has part | `ClansController` | hook → `true`, static `bool(string, ClanItemType)` | `0x03C2A12C` |

The two clock hooks only substitute when the original returns an *empty*
`Nullable`, so a real server timestamp always wins. The local clock prefers the
game's own helper over `::time()` so the unit can never drift from the managed
side.

The two counter hooks use `max(original, synthetic)` and therefore can only ever
raise a value, never lower one.

### Explicitly not ported

16.1.0 also patched the weapon **upgrade** timestamp path. On 23.1.3 weapon
upgrades already work, so the following are deliberately left untouched:

* `WeaponManager` upgrade slot `0x014245FC`
* `PixelTime` time helper `0x03D5E394`
* `FriendsController` time helper `0x01D8BB34`

Those three are shared by non-craft screens; hooking them would have been a
regression risk for a problem this build does not have.

The cheat-banner save shield is **not** duplicated here — `progression_2313.h`
already neutralises `CheatDetectedBanner` (`0x04B40164` `PlayerPrefs.DeleteAll`
and `0x04B400D0` forced disconnect) process-wide.

## 3 — `lobby_catalog_2313.h`

The controller's own local catalogue is walked and each entry is offered back to
the controller through the very same add path the online flow uses, so nothing
is fabricated and prices/recipes stay internally consistent.

| Role | Kind | RVA |
|------|------|-----|
| Readiness gate | static `bool` | `0x03F768E4` |
| Catalogue list | instance → `List<item>` | `0x03F7F390` |
| Add item | instance `bool(item, bool, bool, object, bool)` | `0x03F8AC04` |
| Driver | instance `Update` (hooked) | `0x03F8E354` |

The grant is cursor-based and spread across frames: `kGrantsPerTick = 3`,
`kMaxPasses = 8`, `kMaxFailures = 32`, `kRecheckTicks = 1800` (~30 s at 60 fps).
It disarms itself after the pass budget or after a run of consecutive failures,
so a layout mismatch degrades to a no-op instead of a per-frame spin.

`List<T>` is a generic instantiation, so `get_Count` / `get_Item` are resolved
off the concrete object with `il2cpp_object_get_class` +
`il2cpp_class_get_method_from_name` rather than by namespace/name.

## 5 — `weapon_modules_2313.h`

The weapon and armor module unlock is documented separately in
[`PORT_23_1_3_MODULES.md`](PORT_23_1_3_MODULES.md). The important correction is
that the `ModulesController` lists at `+0x30/+0x38` are materialized definition
lists, not proof of ownership. Appending a second 42-object static catalog only
created reference duplicates.

The corrected implementation leaves the stock catalog untouched and hooks the
module inventory-count and current-level read paths. It promotes only a zero
count to one and clamps the reported level to X. It does not modify crafting,
Progress/profile values, module sets, or per-item equipped-module storage.

## Metadata name generation

Every obfuscated CJK identifier in these headers is machine-generated. `t2.py`
resolves each target **by RVA** out of `methods.json` (161,400 methods extracted
from `dump2313.cs`) into `tokens.json`, and `gen_craft.py` substitutes them into
`*.tpl` templates. The generator refuses to emit a file if any placeholder is
left, if a name no longer matches the dumped metadata, or if the output is not
round-trippable UTF-8.

This is deliberate: hand-transcribing these names is unreliable — several of
them differ only by a single CJK codepoint in the U+4E00–U+4E1F range.

## Verification

All 23 resolved targets (13 for features 1–4, 10 for feature 5) matched
`methods.json` byte for byte at generation time.

Runtime tags to look for in `logcat -s OPG3D`:

```
23.1.3-crafting: weapon craft + clan stock + offline craft clock armed (details=99999 clan=99)
23.1.3-lobby-catalog: local craft catalogue grant armed (3/tick, 8 sweeps max)
23.1.3-modules: all weapon modules unlocked at level 10
23.1.3-modules: serving full module catalogue (N entries, was M)
```

Symbolize crashes with:

```
python3 tools/symbolize_log.py --dump dump2313.cs --log logcat.txt
```

## Artifact provenance

* `libil2cpp.so` — `f0a130c4e8d9487059eab4b0f08462f6aa7d057510cada0fd6fb3043c77deb5c`
  (ELF64 AArch64, Build ID `57fcc18d2db06212416d480d53c0f881ee47c52a`)
* `global-metadata.dat` — `28b8bddf53a8ebdaf70aec1e672d3bdea6e46ca2b2e478f1b7e66e69884c99dd`
* `dump2313.cs` — `803371a6246bdeb6f230ea54dbbbf77108ce088cdfbbcd0f6843a45185398830`
