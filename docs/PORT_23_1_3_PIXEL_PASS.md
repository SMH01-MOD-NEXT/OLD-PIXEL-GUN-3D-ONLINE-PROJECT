# 23.1.3 — PixelPass (battle pass)

This note covers the offline PixelPass season for the exact supplied 23.1.3
ARM64 build (`libil2cpp.so`, ELF build id
`57fcc18d2db06212416d480d53c0f881ee47c52a`). Every symbol below was read out of
the supplied `dump2313.cs` / `global-metadata.dat` pair; nothing is inherited
from the 12.5.0 / 13.2.1 / 14.1.1 / 16.1.0 ports, which do not have this
system at all.

Module: `opg3d/src/main/cpp/pixel_pass_2313.h`.
Log tag: `23.1.3-pixelpass:`.

```
adb logcat -s OPG3D | grep pixelpass
```

## Why the lobby had no pass button

`PGCompany.PixelPassLobbyView` (TypeDefIndex 12069) owns the lobby entry point.
It has a state container for every situation:

| Field | Offset |
| --- | --- |
| `_holder` | `0x48` |
| `_lockContainer` | `0x70` |
| `_unLockContainer` | `0x78` |
| `_comingSoonContainer` | `0x80` |
| `_needLevelContainer` | `0x88` |
| `_tutorialContainer` | `0xA0` |
| pass service (`三丄三丂丈七业丁丞`) | `0x110` |

Every one of those containers lives under the single `_holder`, and the view
switches that holder off wholesale when the pass service has no season. So
without a season there is no button at all — not even a coming-soon state.
That is the observable symptom.

The season is pure configuration. `PGCompany.PixelPass.丐丑业丒丈丅丐专丅`
(TypeDefIndex 13225) is tagged `[不丙三且丅上丞丙丏(123, 1, True)]` plus
`[JsonObject(1)]` and carries the whole pass. `ConfigId.PixelPass = 123`
(`ConfigId`, TypeDefIndex 11085). The retired backend never *computed* that
payload, it only shipped it — so it can be supplied locally.

Note that the feature flag is a **separate** gate and is already handled
elsewhere: `feature.pixelpass` is opened by `live_content_2313`, which hooks
`世丁丒专东专丛一且::一丈丞丞万丐与丏业/1`. An open flag with no season still yields no
button, which is why both modules are needed.

## Four attempts, and what each one got wrong

**1. Grant cosmetics natively.** No season was ever created, so the lobby had
nothing to lay out. Wrong layer.

**2. Seed the on-device cache from a main-menu frame.** This built a real
season and wrote it into the stock cache from the `MainMenuController.Update`
slot owned by `progression_2313`. It never executed once. The supplied logcat
shows why:

```
#000083 E  23.1.3-progression: 东丝丂丄业丕且丙丑::丞丏业丐丒与业/0 not found in metadata
#000084 E  23.1.3-progression: metadata does not match the expected 23.1.3 build; nothing was hooked
#000101 I  23.1.3-pixelpass: armed (config id 123, 50 tiers, 10 per page)
#000158 E  init: 23.1.3 port incomplete: ... progression=0 ...
```

The Progress service instance getter is **nine** metadata characters,
`丞丏业丐丒与业丗与` (dump line 284773, RVA `0x1B3BA40`). It had been written as a
seven-character `丞丏业丐丒与业`, a string that occurs **zero** times anywhere in
23.1.3 metadata. `progression_2313::install()` therefore failed on its very
first `bind()` and returned *before* installing any hook — including
`MainMenuController.Update`.

That Update slot was the only caller of
`pixel_pass_2313::pump_from_main_menu()`. Line `#000101` is only the bind
phase; neither the success line nor the `giving up after N attempts` line
appears anywhere in the log, which proves the seeder body never ran. The same
failure also silenced the weapon-module, hidden-item and live-content pumps.

So the season logic was never actually exercised — and a second, latent defect
would have bitten immediately afterwards. The tier `IsFree` field (`"f"`) is
`Rilisoft.丅丏丏丛丕丁丟上丞`, the salted int (TypeDefIndex 9203, tagged
`[JsonConverter(typeof(七不不丐专世丝丄上))]`), **not** a `bool`. It was being emitted
as `true`, which that converter cannot read.

**3. Serve the season from the cache read path.** The device then proved the
cache is never asked for `ConfigId 123` at all, so this route was retired in
favour of handing a constructed season straight to the pass manager, built from
`PixelPassLobbyView.OnEnable`.

**4. Build the season from the view's `OnEnable`.** Armed perfectly and still
produced no button. Two defects, and they masked each other:

*Ordering deadlock.* `handle_gate()` was a pure observer: it read the season,
logged it, and returned the stock verdict. Nothing built anything. But the
gates are answered on the **loading screen**, ~3.6 s in, from `AppsMenu.Start`:

```
#000181 (t+4132ms) pixelpass: the pass manager answered gate C = true  while the season is absent
#000182                       swt: AppsMenu.Start state=3 result=1
#000197 (t+4986ms) pixelpass: the pass manager answered gate A = false while the season is absent
```

Gate A answers `false`, so the lobby never creates the pass entry, so
`PixelPassLobbyView.OnEnable` never runs — it appears **nowhere** in the
capture — so `install_season()` is never called, so gate A keeps answering
`false`. A closed loop. `kForceGatesWhenSeasonExists` could not break it either,
because it is gated on a season that does not exist yet.

*The one-shot latch was set before the work.* `install_season()` did:

```cpp
g_install_attempted = true;          // <- latched here
void* season = build_season_object();
if (season == nullptr) return false; // <- never retried
```

`build_season_object()` returns `nullptr` for reasons that are purely
**transient** and happen before any managed call: `ensure_season_text()` fails
while the local `Rilisoft.与世且一丁丆丈丄丈.丛上丌丏丟丒东丂且()` weapon-skin catalogue is
still empty — exactly its state at t+4 s — or `il2cpp::string_new` is null.
With the latch already set, `install_season()` could never be entered again,
which also made the `kMaxBuildAttempts` retry loop dead code.

The two had to be fixed together: repairing only the ordering would have made
the gate the earliest caller, at the moment the catalogue is *guaranteed* to be
empty, and poisoned the module harder.

**Current design, below:** the gates build the season, and only entering managed
construction is treated as irreversible.

## Current design

23.1.3 ships its own on-device config cache, `PGCompany.丅丝业七三丈丝丑丏`
(TypeDefIndex 11078) — the class holding the `BinaryConfigStorage.Key` marker.
Rather than writing into it, the module hooks the **read**:

```
internal bool 东丗与丏丟丛丂三丞(ConfigId, out byte[], out string)   // RVA 0x249D670
```

and answers `ConfigId 123` with the local season whenever the stock lookup
comes back empty.

Why this is better than seeding:

* **No timing bet.** The season arrives exactly when the config pipeline asks
  for it, whatever point in startup that turns out to be, instead of at a
  guessed frame number.
* **No cross-module dependency.** The module installs its own hook. A failure
  anywhere else in the port can no longer take the battle pass down with it,
  which is precisely what happened in attempt 2.
* **Nothing is persisted, so nothing can rot.** The old code skipped writing
  whenever a payload of ≥ 3 bytes was already cached, so one malformed season
  would have been cached permanently and would have blocked its own repair.
* **Real content always wins.** A stock payload of ≥ 3 bytes is returned
  untouched; the hook only fills a hole.

### Overload safety

The metadata name `东丗与丏丟丛丂三丞` occurs **exactly once** in the whole dump, so
name + argument count selects it unambiguously. This is unlike the feature gate
in `live_content_2313`, where four one-argument overloads share a name and an
RVA equality check is required. The sibling loader `与丌下丑丝丁丄丏丛/3`
(`0x249E064`) is a different name and is left untouched.

ARM64 ABI: generated managed methods take their explicit arguments followed by
`MethodInfo*`; instance methods take `this` first. Both `out` parameters arrive
as pointers, so the native signature is
`bool(void* self, int32_t configId, void** payload, void** error, void* method)`.

## Season contents

50 tiers, 5 pages of 10, 100 exp per tier, every tier free. Tiers 1–40 award a
weapon skin (`OfferItemType.WeaponSkin = 1170`), tiers 41–50 award graffiti
(`OfferItemType.Graffiti = 1470`). Season window is fixed at
`2020-01-01` → `2099-01-01` so it is active whatever the device clock says.

Skin ids are never invented: they are read at runtime from
`Rilisoft.与世且一丁丆丈丄丈.丛上丌丏丟丒东丂且()`, which is backed by the local
`WeaponSkins` resource and needs no network. Any id containing a quote,
backslash, colon or control character is dropped rather than escaped.

### Reward token shape

Rewards are strings. `PGCompany.丏不丏丂丙丐专丏丅.ReadJson` (`0x33494A4`) hands the
token to `DataSystem.DataCollectors.丒丗丘万一七与丟丕.丌丄丛丈与丝丑世丆` (`0x2B005A4`), and
`丑一丘与丁丄专专专.丅专万三丙业丗丟一` (`0x24B4260`) splits on `':'` (`movz w1, #0x3A`) and
int-parses the first field, matching the `(OfferItemType, string id, int amount)`
constructor at `0x24B39D8`. So a reward is `"<type>:<id>:<amount>"`, e.g.
`"1170:<skin id>:1"`.

### JSON key map

Only keys whose shape is proven from the DTOs are emitted; anything ambiguous
(prices, premium flags, elite-task previews) is omitted so it keeps its
default. A guessed key can fail the whole season parse and put the lobby back
to having no pass.

Common — `且丟上世一丞丆丅三`, TypeDefIndex 13224:

| Key | Field | Type | Emitted as |
| --- | --- | --- | --- |
| `i` | `SeasonId` | salted int | number |
| `sn` | `SeasonName` | string | string |
| `s` | `StartDate` | `DateTime` | ISO-8601 string |
| `e` | `EndDate` | `DateTime` | ISO-8601 string |
| `vc` | `VideoDailyCount` | salted int | number |
| `hc` | `HintCooldown` | plain `int` | number |
| `etr` / `etp` / `tp` | lists | list | `[]` |

Tier — `丁丏丟丏丂丈丙世丌`, TypeDefIndex 13234:

| Key | Field | Type | Emitted as |
| --- | --- | --- | --- |
| `l` | `Level` | salted int | number |
| `t` | `Type` | enum 13227 | number |
| `p` | `NumPage` | salted int | number |
| `e` | `Exp` | salted int | number |
| `r` | `Rewards` | converted strings | array |
| `f` | `IsFree` | **salted int** | number `1` |
| `c` | `IsCool` | real `bool` | `false` |

The salted int is the trap worth repeating: it is tagged
`[JsonConverter(typeof(七不不丐专世丝丄上))]`, so in JSON it is a plain **number** and
the converter re-salts it on read — no salt is ever fabricated, so nothing
looks tampered with to the client. `f` is one of these, not a boolean.

Tier type enum `丕专上业上丑专世丗` (13227): `None=0, First=1, Regular=2, Last=3`.

## Marshalling the payload

The payload must reach managed code as a `byte[]`. This port's IL2CPP wrapper
has no array allocator, and `System.Text.Encoding.GetBytes` has two
single-argument overloads (`char[]` and `string`) that a name + argc lookup
cannot tell apart — picking the wrong one would hand a string to a `char[]`
parameter. `System.Convert.FromBase64String` has exactly one overload, so the
JSON is base64-encoded natively and decoded by the runtime.

The base64 **text** is cached, not the managed array: without a GC handle a
stored managed pointer can be moved or collected, so the `byte[]` is recreated
on every read.

## Fail-closed behaviour

If a metadata target is missing, if the skin catalogue is still empty, or if
the payload cannot be marshalled, the stock result is returned untouched and the
next gate query or menu frame tries again.

The distinction that matters is **what counts as irreversible**. Only two things
do:

* `g_managed_parse_started` — set immediately before the first managed call
  (`DeserializeObject`, or `object_new` + `PopulateObject`). Past that line a
  managed frame has run and the season validator may have thrown, which native
  code cannot catch, so the same payload must never be handed over twice.
* `g_install_disarmed` — the construction path did not verify against this
  `libil2cpp.so`. Retrying cannot help.

Everything before that first managed call is a **pre-flight check** and stays
retryable: an empty weapon-skin catalogue, a null `string_new`. This is the
difference between the old and the new code, and it is what makes the retry
budget real rather than decorative.

The budget is `kMaxBuildAttempts = 240` (raised from 32), and `pump()` spends it
at one attempt per `kInstallRetryFrames = 15` menu frames. One per frame would
have burned all 240 in about four seconds — long before the catalogue is
guaranteed to be populated.

## Expected log

The build tag is now `lobby gate v6 + ... + pixel pass v2 (gate-driven
season)`. If `#000005 init:` still says `lobby gate v5`, the old `.so` is
running and nothing below applies.

```
23.1.3-pixelpass: pass manager armed (gates A=1 B=1 C=1, lobby view OnEnable=1, season construction=1); the season is built from the first gate query that finds none
23.1.3-pixelpass: gate A was asked before a season existed; building one now
23.1.3-pixelpass: no season could be built yet (the local weapon skin catalogue is still empty); this is retried, not fatal
23.1.3-pixelpass: season authored (N json bytes, 50 tiers, M skin ids, graffiti tiers 10)
23.1.3-pixelpass: the pass manager now holds a service and reports its season; 50 tiers over 5 page(s) are live
23.1.3-pixelpass: the season exists but gate A was shut; opening it so the lobby pass button can appear
```

The middle line is expected and harmless — the first gate query happens on the
loading screen, before the skin catalogue exists. It should be followed by
`season authored` within a few menu frames.

The periodic counter now names the install state explicitly, so a capture is
self-diagnosing:

| Counter says | Meaning | Next step |
| --- | --- | --- |
| `season install=done` | Working. | Nothing. |
| `still waiting for a buildable season` | The skin catalogue never filled. | Chase `collect_skin_ids`, or set `kIncludeGraffiti = false` / widen the budget. |
| `the managed parse ran once and yielded no usable season` | The season was built and the managed validator rejected it. | The payload shape is wrong — check `f` (`IsFree`) is a number and the `丅丑世丈世七丈丂丁` rules. |
| `disarmed, the construction path did not verify` | Metadata/RVA mismatch. | Re-check the RVAs against `analys2313/dump2313.cs`. |

`progression=1` in the `init:` summary is no longer a prerequisite at all: the
gates drive construction themselves, so a failure in `progression_2313` can no
longer take the battle pass down with it.

## Known gaps

* Lottery and Sets still show coming-soon. Loot boxes are `ConfigId 119`; the
  Sets flag lives on `ShopNGUIController` at `+0x360`. Neither is handled here.
* The graffiti key shape `graffiti_<n>` is inferred from the system's own
  sentinel `PGCompany.GraffitiSystem.丐且丆世丛下丏丒丏.上东三业专丑三三丁 = "graffiti_-1"`,
  not read from a catalogue. If graffiti tiers come back empty, seeding
  `ConfigId 133` (`Graffiti`) is the next step; `kIncludeGraffiti = false`
  disables those tiers in the meantime.
* The hook engine cannot install hooks on generic-shared instantiations
  (`RVA -1`), which is why no generic collection method is hooked anywhere in
  this module — `List<T>` is reached through its own accessors instead.
