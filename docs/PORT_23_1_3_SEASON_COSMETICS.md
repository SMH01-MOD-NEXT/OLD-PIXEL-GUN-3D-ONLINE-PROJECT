# 23.1.3 offline season and lottery: weapon skins and graffiti

## Scope

Weapon skins and graffiti are the only content families in 23.1.3 that no part
of this port grants, and they are also the only ones left that could still be
worth earning. This module makes them the reward loop: they are handed out one
at a time, on a wall clock, as season tiers and lottery spins, entirely inside
`libopg3d.so`.

No backend, no self-hosted service, no HTTP route and no config payload are
involved. Every call is a stock managed entry point in the game's own
`libil2cpp.so`.

Implemented in `opg3d/src/main/cpp/season_2313.h`, pumped from the
`MainMenuController.Update` chain in `progression_2313.h`, installed from
`main.cpp` after `hidden_items_2313`.

## Verified target

| Artifact | SHA-256 |
| --- | --- |
| `libil2cpp.so` | `f0a130c4e8d9487059eab4b0f08462f6aa7d057510cada0fd6fb3043c77deb5c` |
| `global-metadata.dat` | `28b8bddf53a8ebdaf70aec1e672d3bdea6e46ca2b2e478f1b7e66e69884c99dd` |
| `dump2313.cs` | `803371a6246bdeb6f230ea54dbbbf77108ce088cdfbbcd0f6843a45185398830` |

ELF build id `57fcc18d2db06212416d480d53c0f881ee47c52a`, ARM64, `RVA == file
offset` for this image.

## Symptom

Every weapon and every wear piece the build ships is already owned (see
`docs/PORT_23_1_3_HIDDEN_ITEMS.md` if present, otherwise `hidden_items_2313.h`),
the level is capped at 65 and both currencies sit at 999,999,999. The skins for
those weapons, and graffiti, are still missing and cannot be bought:

* `hidden_items_2313` sweeps `OfferItemType` 10..70 and keeps `kIncludeSkins =
  false`; `WeaponSkin = 1170` and `Graffiti = 1470` are not in that range at all.
* The shop, roulette and pass tables that would normally sell or award them are
  built from config (`BalanceWeaponSkins_v22`, `graffiti-v2`, `loot-boxes-v7`),
  and the emulated backend answers config endpoints with `{}`.

So the player owns everything that matters for playing and nothing that is worth
playing for.

## What made a native grant possible

`hidden_items_2313.h` recorded the blocker: `il2cpp.h` exposes no `object_new`
and no generic-instantiation helper, so a managed inventory key could only be
*borrowed* from a list the game had already built.

That does not apply to the key type. The inventory key and the reward payload
are the same class, `PGCompany.\u4e11\u4e00\u4e18\u4e0e\u4e01\u4e04\u4e13\u4e13\u4e13` (`dump2313.cs:469871`), and it
ships static factories that allocate the object themselves:

| RVA | Signature |
| --- | --- |
| `0x24B370C` | `static \u4e1c\u4e00\u4e17\u4e0f\u4e11\u4e04\u4e07\u4e04\u4e0f(OfferItemType, string id)` |
| `0x24B39D8` | `static \u4e1c\u4e00\u4e17\u4e0f\u4e11\u4e04\u4e07\u4e04\u4e0f(OfferItemType, string id, int amount)` |
| `0x24B4260` | `static \u4e05\u4e13\u4e07\u4e09\u4e19\u4e1a\u4e17\u4e1f\u4e00(string serialised)` |

A static factory needs no `object_new` on our side, so native code can mint an
inventory key for any `(type, id, amount)` triple and feed it into the stock
transaction that this port already exercises on real hardware:

| RVA | Role |
| --- | --- |
| `0x3062B08` | `\u4e18\u4e0a\u4e04\u4e09\u4e1a\u4e0f\u4e19\u4e0d\u4e14(key, Nullable<cause>, Action)` grant |
| `0x304F634` | `\u4e19\u4e1b\u4e1a\u4e10\u4e10\u4e03\u4e1b\u4e0d\u4e02(key, Nullable<filter>)` owned count |
| `0x3046000` | item registry singleton |

Corroboration that a `WeaponSkin` key is the shipped currency of that system:
`\u4e13\u4e19\u4e0d\u4e12\u4e01\u4e01\u4e1c\u4e09\u4e0d::\u4e19\u4e17\u4e0f\u4e0f\u4e0c\u4e07\u4e17\u4e06\u4e04(key)` at `0x2133C48` resolves a
`List<WeaponSkinSettings>` **from a key**.

## Catalogue sources

### Weapon skins

`Rilisoft.\u4e0e\u4e16\u4e14\u4e00\u4e01\u4e06\u4e08\u4e04\u4e08` (`dump2313.cs:380764`) is the catalogue and exposes
three 0-argument `List<WeaponSkin>` getters:

| RVA | Method |
| --- | --- |
| `0x35B4DD4` | `\u4e01\u4e17\u4e0d\u4e0f\u4e12\u4e0d\u4e12\u4e1d\u4e00()` |
| `0x35B5558` | `\u4e01\u4e18\u4e03\u4e04\u4e12\u4e11\u4e19\u4e0d\u4e1f()` |
| `0x35B56A4` | `\u4e14\u4e16\u4e18\u4e1b\u4e0b\u4e17\u4e10\u4e1c\u4e0a()` |

Their prologues are identical in shape (static `cctor` guard, string-literal
init block, static cache load), so static analysis cannot say which one is
"everything the build ships" and which is "owned" or "buyable right now".
Rather than guess, the module reads all three, unions them, deduplicates by
`Rilisoft.WeaponSkin::get_Id()` (`0x35B36A4`) and then filters every candidate
through the stock owned count. A wrong guess about any one list can only make
the pool smaller; it can never grant a duplicate and never grants something the
player already owns.

### Graffiti

`PGCompany.GraffitiSystem.\u4e10\u4e14\u4e06\u4e16\u4e1b\u4e0b\u4e0f\u4e12\u4e0f` ships everything needed, so no id
is synthesised:

| RVA | Method |
| --- | --- |
| `0x1506D04` | singleton `\u4e0b\u4e0c\u4e11\u4e01\u4e0b\u4e1f\u4e1b\u4e18\u4e0a()` |
| `0x15077D0` | `static \u4e1f\u4e0f\u4e01\u4e1f\u4e08\u4e02\u4e02\u4e11\u4e14(int index)` -> inventory key |
| `0x1507908` | `static \u4e1f\u4e0f\u4e01\u4e1f\u4e08\u4e02\u4e02\u4e11\u4e14(string id)` -> inventory key |
| `0x1507474` | `static \u4e19\u4e17\u4e14\u4e15\u4e1a\u4e16\u4e1d\u4e02\u4e0c(int index)` -> id string |
| `0x1508848` | `\u4e16\u4e02\u4e1e\u4e19\u4e0f\u4e0e\u4e04\u4e04\u4e0f(int index)` -> owned |
| `0x1507DC4` | `\u4e00\u4e19\u4e13\u4e01\u4e00\u4e0f\u4e02\u4e03\u4e0c(string id)` equip |

The graffiti definition list itself comes from `ConfigId.Graffiti`
(`graffiti-v2`) and is empty offline, so the pool is built by probing indexes
`0..kGraffitiMaxIndex` against the shipped id builder: an index the build does
not know produces no id and is skipped. The shipped "none" value is
`graffiti_-1` and the equipped slot is stored locally under
`MISC.GRAFFITI_EQUIPPED_KEY`.

## Item type numbers

From `Rilisoft.OfferItemType` (`dump2313.cs:364118`), for reference and for the
follow-up work:

| Value | Name | Used here |
| --- | --- | --- |
| 65 | `Skin` (character / wear) | no, see below |
| 1170 | `WeaponSkin` | yes |
| 1470 | `Graffiti` | yes, via the stock key builder |
| 1120 | `GoldenSkin` | no |
| 1090 / 1100 / 1110 | `BattlePassLevel` / `BattlePassExp` / `BattlePassCurrency` | no |
| 1040 | `GachaFreeSpin` | no |
| 1050 | `EventCurrency` | no |
| 1130 / 1160 | `EventChest` / `ModuleChest` | no |

`Skin = 65` is deliberately left out: it is inside the range
`hidden_items_2313` already sweeps, so enabling it there is a one-line change
(`kIncludeSkins = true`) and duplicating that machinery here would only create
two writers for the same item type.

## Schedule

| Constant | Value | Why |
| --- | --- | --- |
| `kWarmupFrames` | 900 | `hidden_items_2313` starts its bulk sweep at menu frame 180; this must never share a frame with it |
| `kOpeningTiers` | 3 | the feature proves itself on the first launch instead of three minutes later |
| `kTierIntervalSec` | 180 | one season tier |
| `kSpinIntervalSec` | 900 | one lottery spin, picked at random instead of in order |
| `kGrantCooldownFrames` | 30 | never two grants in quick succession |
| `kGraffitiMaxIndex` | 128 | index probe bound |
| `kMaxPool` | 512 | pooled cosmetics |

The clocks are `CLOCK_MONOTONIC`, not menu frames. That is deliberate: the pump
only runs while the main menu is alive, so a frame counter would reward sitting
in the menu and ignore time spent in matches. A monotonic clock keeps counting
during a match, so playing is what unlocks the next tier.

Progress needs no save file of its own: every candidate is filtered by the
stock owned count, so ownership *is* the save, and a restart resumes where the
inventory left off.

## Safety properties

* **Fail-closed image proof.** Six unambiguous metadata targets (registry
  singleton, category catalogue getter, inventory-key accessor, weapon skin
  catalogue, `WeaponSkin::get_Id`, graffiti singleton) must resolve to exactly
  `base + RVA` before the four overloaded entry points are taken by RVA. Any
  mismatch logs and arms nothing.
* **No patching.** Nothing is hooked and no game memory is written; the module
  only calls stock public methods.
* **Ownership-checked.** Every grant is preceded *and* followed by the stock
  owned count, so a grant can never duplicate an item and a silent failure is
  reported rather than assumed to have worked.
* **Second opinion for graffiti.** Graffiti ownership is also checked through
  the graffiti system's own owned set, which does not depend on the
  synthesised-key path.
* **Self-disarming.** `kMaxConsecutiveFailures` failures, or an exhausted pool,
  disarm the module instead of spinning once per frame.
* **No wallet or profile writes.** Currency, level and experience remain
  `progression_2313`'s business.

## Runtime diagnostics

Logcat tag `OPG3D`:

```text
23.1.3-season: armed: weapon skins and graffiti are handed out one at a time ...
23.1.3-season: catalogue: 214 weapon skins (union of the three shipped lists) and 37 graffiti ids, 251 entries pooled; ...
23.1.3-season: season tier 1: weapon skin 'skin_id' unlocked (1 of 251 in the pool, 1 granted so far)
23.1.3-season: lottery spin 1: graffiti 'graffiti_12' unlocked (...)
23.1.3-season: pool=251 granted=7 already owned=3 tiers=4 spins=1 failed=0
```

The catalogue line is the one to read first: it states exactly how much content
the pool found, which is also the honest answer to "did the three-list union
work on this device".

## Validation checklist

1. Launch, stay in the main menu ~20 s: `23.1.3-season: armed` then
   `catalogue:` with non-zero counts.
2. Three opening unlocks appear within seconds of the catalogue line.
3. Open the weapon Armory: the granted skins are selectable on their weapons.
4. Open the graffiti picker: the granted graffiti can be equipped.
5. Play a match, return to the menu: a further tier lands without the menu
   having been open for the whole interval (proof the monotonic clock, not a
   frame counter, drives the schedule).
6. Restart: previously granted items are reported as owned and skipped, not
   granted twice.

## Follow-up: populating the stock season and roulette screens

This module is the reward loop, not a season inside the stock PixelPass screen.
That screen reads a season model (`PGCompany.PixelPass.\u4e09\u4e04\u4e09\u4e02\u4e08\u4e03\u4e1a\u4e01\u4e1e`,
view check at `0x3D5C340`, disable path `0x3D5CD18`) built from a config
payload, exactly like the roulette tables and the graffiti picker list.
`live_content_2313` already makes all of those screens reachable; filling them
needs a payload on the config route, which stays inside the library through
`backend_emu_routes.h`.

The config registry is `ConfigId` (`dump2313.cs:449175`) and the payload names
are the string constants of `PGCompany.\u4e17\u4e14\u4e09\u4e15\u4e0a\u4e1a\u4e10\u4e15\u4e04`. Pairing below is by
name, not proven from code:

| ConfigId | Payload name |
| --- | --- |
| `PixelPass = 123` | `pixel-pass-v6` |
| `PixelPassOffers = 140` | `pixel-pass-offers-v6` |
| `ExpOpen = 116` | `exp-open-v11` |
| `LootBoxes = 119` | `loot-boxes-v7` |
| `CardRoulette = 168` | - |
| `RouletteAds = 159` | - |
| `Graffiti = 133` | `graffiti-v2` |
| `WearSkins = 160` | - |
| - | `battlepass-v38`, `events-v43`, `event-hub-v1`, `trophy_road-v3`, `taskbook-v9`, `gatcha-v6`, `BalanceWeaponSkins_v22` |

The payload DTOs are MessagePack objects with JSON aliases
(`[MessagePackObject(False)]`, `[Key(n)]`, `[JsonProperty("...")]`), for example
the graffiti config `\u4e1e\u4e09\u4e07\u4e1a\u4e07\u4e17\u4e17\u4e00\u4e0e { [Key(0)] List<...> GraffitiSettings }`
with per-entry `{ Id, LifeTime, Cooldown }`, so a payload can be written in
either encoding once the request envelope is confirmed. The reward entries in
those payloads use the same `\u4e11\u4e00\u4e18\u4e0e\u4e01\u4e04\u4e13\u4e13\u4e13` DTO this module already mints
natively, which is why the two approaches converge rather than compete.

Also worth recording: the lottery ids the build ships (`Rilisoft.\u4e13\u4e0c\u4e17\u4e0e\u4e1f\u4e03\u4e08\u4e1a\u4e06`)
are `super_lottery_basic`, `hero_lottery`, `oriental_lottery_v3`,
`toystory_lottery`, `summer_lottery`, `knight_lottery`, `alien_lottery`,
`egg_easter_lottery`, `dino_lottery`, `mafia_lottery`, `4th_july_lottery` and
about twenty more, with event ids `pg_birthday`, `4july`, `multi_chests`,
`homecoming`, `halloween`. Using those exact ids in a future payload is what
makes the shipped art and localisation resolve instead of showing placeholders.
