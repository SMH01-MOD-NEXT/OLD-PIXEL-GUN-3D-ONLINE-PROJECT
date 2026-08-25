# 23.1.3 live content: battle pass, lotteries and events

## Scope

| Area | State after this change |
| --- | --- |
| PixelPass (23.1.3's battle pass) entry points | Reachable: lobby button, banner and store tab are no longer reported "closed" |
| Lotteries / card roulette / ad spins | Reachable |
| Three-chests, ads chest | Reachable |
| Task book, eggs delivery, piggy bank, ad campaign | Reachable |
| Gallery, pets, craft, modules, trader, loadout slots | Reachable |
| Clans, squad, friends, private matches, mailbox, tournament, brawl, subscription | Untouched (stock verdict) |
| Game mode / map unlock ids | Untouched (owned by `lobby_catalog_2313`) |
| Populated PixelPass season and lottery reward tables | **Not yet** — needs a payload from the emulated backend, see [Remaining work](#remaining-work) |

Implemented by `opg3d/src/main/cpp/live_content_2313.h`, installed from
`main.cpp` and pumped from the main-menu slot that `progression_2313` already
owns.

## Verified target

Everything below was proven from the supplied 23.1.3 ARM64 artifacts, not from
any public API or from another build.

| Artifact | SHA-256 |
| --- | --- |
| `libil2cpp.so` (113,595,792 B) | `f0a130c4e8d9487059eab4b0f08462f6aa7d057510cada0fd6fb3043c77deb5c` |
| `global-metadata.dat` (20,702,072 B) | `28b8bddf53a8ebdaf70aec1e672d3bdea6e46ca2b2e478f1b7e66e69884c99dd` |
| `dump2313.cs` (44,156,870 B) | `803371a6246bdeb6f230ea54dbbbf77108ce088cdfbbcd0f6843a45185398830` |

ELF build id: `57fcc18d2db06212416d480d53c0f881ee47c52a`. For this image
`RVA == file offset`, so every offset below can be re-checked directly in the
supplied `libil2cpp.so`.

## Symptom

Offline (and on the emulated backend) the lobby is empty: no battle pass, no
lottery, no chests, no task book, no event windows. None of that content is
missing from the APK — the classes, prefabs and localisation are all present
(`PixelPassView`, `PixelPassTaskView`, `LootBoxRewardsView_Roulette`,
`EventHubView`, `ChestController`, ...). Only their switchboard is missing.

## Authoritative path

One predicate decides all of it: `世丁丒专东专丛一且::一丈丞丞万丐与丏业`
(global namespace, TypeDefIndex 494, `dump2313.cs:21631`). The feature ids are
plain string constants in that same class, so the mapping is unambiguous:

| Constant | Value |
| --- | --- |
| `专丄丕丅丐与丞丛且` | `feature.pixelpass` |
| `七丂丁丗丗丗世东丛` | `feature.battlepass` |
| `一丒与丌与丝丌世与` | `feature.AdsPixelPassStore` |
| `丄丐三丅丑不业丒丅` | `feature.roulette` |
| `一业业丝丌丈丈丏丐` | `feature.rouletteads` |
| `丏丈丏丘下丏东业东` | `feature.cardroulette` |
| `丛丗丆丛七不上丝丛` | `feature.threechests` |
| `丟丘上丝丛丂万丒丛` | `feature.adschest` |
| `丗丏东万丏下专上东` | `feature.taskbook` |
| `丙丝丗三上与上丐丒` | `feature.eggsdelivery` |

…and so on for `feature.piggy`, `feature.craft`, `feature.modules`,
`feature.gallery`, `feature.pets`, `feature.trader`, `feature.clans`, etc.

Four overloads share the name, all with exactly one argument:

| Overload | RVA | Dump line |
| --- | --- | --- |
| `(string)` | `0x20DF308` | 21684 |
| `(ExpOpenSystem.丂丁不丙丅下不丐下 entry)` | `0x20DF334` | 21690 |
| `(enum)` | `0x20DF354` | 21687 |
| `(int level)` | `0x20DF364` | 21693 |

The string overload is not a local flag — it is a table lookup plus a level
comparison, and the four entry points fall through into each other:

```text
020DF308  mov  x19, x0
020DF314  bl   0x3664E34  ; ExpOpenSystem.上丘丏丏世丆丌不三::下丌丑丁下丟丛丘上()  (singleton)
020DF318  cbz  x0, ...    ; no table at all                       -> false
020DF324  bl   0x3669480  ; 丂七且丐丗丗一且丛(id) -> 丂丁不丙丅下不丐下 entry
020DF32C  b    0x20DF334  ; falls into the entry overload
020DF334  cbz  x0, 0x20DF350   ; the id has no entry in the table -> false
020DF344  bl   0x3F9DAC8  ; entry.Progress.丞丏三丁丅丗丕三丝() -> unlock level
020DF34C  b    0x20DF364  ; falls into the int overload
020DF3AC  bl   0x1C79B10  ; ExperienceController::三世丒丄丘下丘丝丅()  (player level)
020DF3B0  cmp  w0, w19    ; player level >= unlock level ?
```

The enum overload at `0x20DF354` only builds a name (`丗丟东世丑且且丏丙`,
`0x20DEC38`) and then branches to `0x20DF308`, so enum-based checks take the
same path.

`ExpOpenSystem.上丘丏丏世丆丌不三` (`dump2313.cs:316693`) is a pure content
registry: a `List<丂丁不丙丅下不丐下>` at `+0x10` and a
`Dictionary<string, 丂丁不丙丅下不丐下>` at `+0x18`, filled from the config
payload. Its entries are MessagePack/JSON DTOs
(`[Key(0)] Progress`, `[Key(1)] Tag`, `[Key(2)] ViewInProgressRoad`,
`[Key(3)] List<...> Rewards`).

**Root cause.** The emulated backend answers config endpoints with `{}` (see
`docs/PORT_23_1_3_BACKEND_EMULATION.md`), so the registry stays empty, every
lookup returns null and every feature reports "closed" before the level
comparison is even reached. The lobby is not gated by progression — it is
gated by a table that no longer arrives.

## Falsified approaches

* **Raising the player level.** `progression_2313` already caps the profile at
  level 65, and the level comparison is only reached when an entry exists. A
  level pump cannot open anything here.
* **Hooking the predicate by metadata name and arity alone.** All four
  overloads take one argument, so the resolved overload depends on metadata
  declaration order, which is not a contract. Hooking the wrong one would
  either patch a level comparison (`(int)`) or an entry test
  (`(丂丁不丙丅下不丐下)`) and silently change unrelated behaviour. The module
  therefore requires pointer equality with `base + 0x20DF308` and arms nothing
  otherwise.
* **Synthesising registry entries natively.** Every entry is an obfuscated
  serialisation DTO holding another obfuscated reward list; building those
  objects through the runtime means guessing constructor and collection
  semantics with no verified reference. Rejected as unverifiable.
* **Hooking `PixelPassView::丘丑丘丈丅业丄世丒` (`0x3D5B7C8`) as an "is premium"
  flag.** Disassembly shows `Object.op_Inequality` -> `get_gameObject` ->
  `get_activeSelf` / `get_activeInHierarchy`: it is a GameObject-active check,
  not a premium flag. Not used.

## Implemented fix

`live_content_2313.h` hooks the verified string overload. Order of operations
per call:

1. call the stock predicate; if it says "open", return that unchanged;
2. otherwise read the managed feature id (capped at 64 chars);
3. if the id is in the curated content list, return `true` and log the id once;
4. otherwise return the stock verdict.

The curated list contains only content that is fully contained in the APK and
in this port's local systems: the PixelPass battle pass and its store tab, the
roulette / card lottery and their ad spins, the three-chest and ads-chest
screens, the task book, eggs delivery, the piggy bank, the ad campaign, and
the gallery / pets / craft / modules / trader / loadout screens this port
already grants locally.

Deliberately **not** in the list, because they need real server-side state and
would only produce empty or failing windows: `feature.clans`,
`feature.squad`, `feature.friends`, `feature.private`, `feature.mailbox`,
`feature.tournament`, `feature.brawl`, `feature.subscription20`. The
`gamemode` and `map` id families are also excluded — they belong to
`lobby_catalog_2313`.

## Safety properties

* **Fail-closed.** No libil2cpp base, no metadata match, no compiled body, or a
  resolved pointer that is not `base + 0x20DF308` → nothing is hooked and
  `install_hooks()` returns `false`, which `main.cpp` reports in the
  incomplete-port line.
* **No absolute-RVA calls.** The RVA is used only as an equality proof; the
  patched target and the original are resolved through metadata and
  ShadowHook.
* **Additive only.** The hook can turn a `false` into a `true` for a fixed set
  of ids and can never turn a stock `true` into `false`.
* **Untouched entry points.** ShadowHook rewrites the prologue at
  `0x20DF308`; the entry overload (`0x20DF334`) and the int overload
  (`0x20DF364`) start past it and keep running stock code, so callers that use
  a table entry or a raw level still get stock behaviour.
* **No writes.** The module never touches the profile, the wallet or the
  registry; it only answers a predicate.

## Runtime diagnostics

Logcat tag `OPG3D`:

```text
23.1.3-content: armed: 21 content features (PixelPass battle pass, lotteries and card roulette, chests, task book and event content) report open when the offline ExpOpenSystem table has no entry for them
23.1.3-content: 'feature.pixelpass' is closed in the offline ExpOpenSystem table; opened locally
23.1.3-content: the ExpOpenSystem table opens 0/21 curated content features on its own (gate queries=..., opened locally=...)
23.1.3-content: gate queries=... stock-open=... opened locally=... distinct ids opened=...
```

The third line is the root-cause probe: it asks the **stock** predicate about
every curated id once, roughly four seconds after the main menu appears. `0/21`
confirms the registry is empty; a non-zero count means a config payload is
now arriving and the corresponding ids no longer need to be forced.

## Validation checklist

1. Build and install; watch for `23.1.3-content: armed: 21 content features`.
   Its absence, or `live-content=0` in the `init: 23.1.3 port incomplete` line,
   means the image did not match and nothing was patched.
2. On the main menu, confirm the battle pass, lottery / roulette, chest and
   task entry points are present instead of hidden.
3. Confirm `the ExpOpenSystem table opens 0/21 ...` appears once.
4. Open each unlocked screen and check for managed exceptions in logcat.
5. Confirm nothing regressed in the areas owned by other modules: game modes
   and maps (`lobby_catalog_2313`), module unlocks (`weapon_modules_2313`),
   hidden items (`hidden_items_2313`).

## Remaining work

Opening the gate makes the content reachable. Filling it with a season still
needs data, and the exact targets are already mapped:

* `PixelPassView::丄丂不丟上一与一丈` (`0x3D5C340`) reads the season model at
  field `+0xF0` (`PGCompany.PixelPass.三丄三丂丈七业丁丞`, `dump2313.cs:523664`)
  and tail-calls `不丝丒丘三专专一丅` (`0x18EF98C`). With a null model the view
  calls its disable path `东丞丑专丁与丙一丑` (`0x3D5CD18`), so the pass screen
  opens and then closes itself until a season payload exists.
* The season model is built from a config DTO at field `+0x10`, i.e. it is the
  same config payload that feeds the ExpOpenSystem registry — the natural place
  for it is `backend_emu_routes.h`, together with the reward tables the
  lotteries read (`PGCompany.LootBoxSystem.丘丟丘一丟丝东丛且`, `0x190243C`).
* Lottery ids are enumerated as constants in `Rilisoft.专丌丗与丟七丈业丆`
  (`super_lottery_basic`, `hero_lottery`, `pg_birthday_2021_lottery`, ...) and
  event ids as `pg_birthday`, `4july`, `multi_chests`, `homecoming`,
  `halloween`; a served season should reuse those ids so the shipped
  localisation and art resolve.
* `ExpOpenSystem.上丘丏丏世丆丌不三::丝丘丄与丅一丄丗丁` (`0x3668F54`, `int`, no
  arguments) is a candidate entry-count accessor for a future report; its
  semantics are not verified yet, so the probe above uses the predicate itself
  instead.
* Battle-pass currency keys (`"BattlePassLevel"`, `"BattlePassExp"`,
  `"BattlePassCurrency"`, `"EventCurrency"`) exist as constants, but the keyed
  wallet setter throws on unknown keys and the owning class is not confirmed,
  so no wallet writes are attempted in this change.
