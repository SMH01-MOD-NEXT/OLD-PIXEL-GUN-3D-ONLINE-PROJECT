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

**5. Gate-driven construction (v2).** This worked — up to the last step. The
capture of Aug 26 16:29 shows the whole chain succeeding:

```
#000181 +003619ms pixelpass: gate C was asked before a season existed; building one now
#000182 +003624ms pixelpass: season authored (4341 json bytes, 50 tiers, 17 skin ids, graffiti tiers 10)
#000183 +003625ms pixelpass: parsing the season with the game's own Newtonsoft
#000184 +003751ms pixelpass: the season parsed cleanly
#000185 +003751ms pixelpass: allocating the pass service
#000187 +003935ms pixelpass: handing the pass service to the manager
#000188 +003936ms E pixelpass: the pass manager kept the service but still reports no season
#000205 +004492ms pixelpass: the pass manager answered gate A = false while the season is absent
```

Both v2 defects were genuinely fixed: the gates now drive construction, and the
latch no longer swallows transient misses. A real 4341-byte season with 50 tiers
and 17 skin ids is authored, the game's own Newtonsoft accepts it, the service
is constructed and the manager keeps it. Only the **read-back** fails.

The `#000188` line above states a guess — that the manager reads its season from
a "pass state holder" — and disassembly disproved it. Two separate reads are at
fault, and neither one looks at the service:

*The manager's season getter does not read the manager.* `丒不丏一丂丈丙东丟`
(`0x1A08038`) decodes to:

```csharp
var season = 东丈与专专丈丘七丄<丐丑业丒丈丅丐专丅>.丒不丏一丂丈丙东丟(123);  // static config store
var other  = 东丈与专专丈丘七丄<T140>.丒不丏一丂丈丙东丟(140);
if (season != null && other != null) season.f_0x50 = other.f_0x10;
return season;
```

It reads a **static, `this`-free config store keyed by ConfigId**, through the
generic-shared accessor at `0x2606038`. Writing the service could never satisfy
it. Offline that store is empty for both 123 and 140, which also means the
`+0x50` merge is a no-op on the stock path and needs no reproducing. Note this
also corrects the older claim that "the config cache is never asked for" the
season — only the *loader* entry points this module hooks are never asked; this
separate generic accessor is.

*Gate A never looks at the season at all.* `丈丁上一丟丈丗七业` (`0x1A0811C`) is:

```csharp
return service != null && service.东与丞且丘丈专东丆() && service.一丒丄丘不七与丁万();
```

and both predicates fail offline, for unrelated reasons:

| Predicate | RVA | What it really does | Why it fails offline |
| --- | --- | --- | --- |
| `东与丞且丘丈专东丆` | `0x18EEC4C` | forwards to `与下丗丆丛丕丂丈丌(default)` (`0x18EEC58`): `now >= start && now < end` | bounds are **int32** unix seconds (`0x18EE4A0`/`0x18EE5B0`), so the old `2099-01-01` end (4 070 908 800 s) wrapped to −224 058 496; and the server clock `0x3D5E394` returns **−1** until synchronised, which it never is offline |
| `一丒丄丘不七与丁万` | `0x18EEF7C` | `世丁丒专东专丛一且::一丈丞丞万丐与丏业(丂丁不丙丅下不丐下)` over the pass's ExpOpenSystem row (`0x1A0D51C`) | that **entry overload lives at `0x20DF334`**, past the prologue `live_content_2313` patches at `0x20DF308`, so it reaches stock code, finds no row in the empty offline table and answers `false` |

The second row is the mechanical reason gate A stayed shut. The content-gate
module's own header already documented `0x20DF334` as an unhooked entry point;
what was new is that the pass depends on exactly that entry point.

**Current design, below:** the gates build the season, only entering managed
construction is irreversible, and all three decisive reads are answered.

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

That read hook is a safety net, not the delivery route. The season actually
reaches the game as a constructed object handed to the pass manager, and v2
proved that handing it over is only half the job — it also has to be *readable*
back. So v3 answers the three reads that decide the lobby entry.

### Answering the three decisive reads

| Hook | RVA | Behaviour |
| --- | --- | --- |
| `万丈丏丈丙丑万万丙::丒不丏一丂丈丙东丟` | `0x1A08038` | stock first; if it returns null, return the season held by the service the manager is carrying |
| `三丄三丂丈七业丁丞::东与丞且丘丈专东丆` | `0x18EEC4C` | stock first; report the window open once a season is installed |
| `三丄三丂丈七业丁丞::一丒丄丘不七与丁万` | `0x18EEF7C` | stock first; report the pass unlocked once a season is installed |

Three properties hold for all three:

* **Stock always wins.** Each hook calls the original first and only substitutes
  an answer when the stock one is null/false. A build that has a real season
  from a real backend behaves exactly as shipped.
* **Nothing is forced before an install succeeds.** All three are gated on
  `g_install_succeeded` — a season that was authored, parsed by the game's own
  Newtonsoft, and wrapped in a real service. If the season path does not verify
  on a given `libil2cpp.so`, the module is inert rather than lying.
* **No managed pointer is cached.** The season getter walks the game's own
  object graph on demand — manager → service → season field, resolved by field
  metadata rather than by the `+0x10` offset. The manager roots the service and
  the service roots the season, so this is GC-safe without a GC handle, which is
  the same rule the rest of the module follows.

Because the two predicates are answered at the source, gate A's *stock*
implementation now succeeds on its own. `kForceGatesWhenSeasonExists` remains as
a backstop, but it is now keyed on `g_install_succeeded` rather than on
`manager_season() != nullptr` — the latter made the whole branch dead code in
v2, since the getter it consulted could never report a season.

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

The build tag is now `lobby gate v7 + ... + pixel pass v3 (season read-back +
gate A predicates)`. If `#000005 init:` still says `lobby gate v6` or `v5`, the
old `.so` is running and nothing below applies.

```
23.1.3-pixelpass: season read-back armed (manager season getter=1, season window=1, pass unlock=1)
23.1.3-pixelpass: pass manager armed (gates A=1 B=1 C=1, lobby view OnEnable=1, season construction=1)
23.1.3-pixelpass: gate C was asked before a season existed; building one now
23.1.3-pixelpass: no season could be built yet (the local weapon skin catalogue is still empty); this is retried, not fatal
23.1.3-pixelpass: season authored (N json bytes, 50 tiers, M skin ids, graffiti tiers 10)
23.1.3-pixelpass: the season parsed cleanly
23.1.3-pixelpass: the manager's season getter reads the static config store by id, which is empty offline; answering it from the service this module installed instead
23.1.3-pixelpass: the pass manager now holds a service and reports its season; 50 tiers over 5 page(s) are live
23.1.3-pixelpass: the season window read as closed (the bounds are int32 unix seconds and the server clock is -1 offline); reporting the installed season as current
23.1.3-pixelpass: the pass has no row in the offline ExpOpenSystem table and its entry overload bypasses the content gate hook; reporting the installed season as unlocked
```

The three lines that are new in v3 are the ones that matter. `answering it from
the service` proves the config-store read-back is live; the `window` and
`unlock` lines prove the two gate A predicates are being answered. After them,
gate A should report **`= true`** on its own — the `the season exists but gate A
was shut` backstop line should now be *absent*, because the stock gate
implementation succeeds by itself.

If `season read-back armed` reports `manager season getter=0`, the season will
never be visible no matter how cleanly it parses — that is the v2 failure mode
and it is now called out explicitly at arming time rather than three seconds
later.

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
