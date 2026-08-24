# 23.1.3 hidden weapon, wear and gadget unlock

## Scope

Every weapon, wear piece (armor, hat, boots, cape, mask) and gadget the
23.1.3 build ships is granted to the local profile, including the items that
cannot be reached at all on a private server:

* they are not offered in the shop,
* they have no reachable craft recipe (the craft screen either hides them or
  their recipe was retired with the backend),
* and the events that used to hand them out no longer exist.

Ultimatum and Locator (weapons) and the Harpoon (gadget) are the reference
cases from the request; the same treatment covers every other hidden or
non-craftable definition of those types, so the list does not have to be
maintained by hand.

Skins (character, weapon and armor) are cosmetics with a much larger
catalogue and are **not** part of this unlock. They are left behind a
compile-time switch (`kIncludeSkins`, default `false`) in
`opg3d/src/main/cpp/hidden_items_2313.h`.

## Why the items are unreachable, and what this port does instead

The obvious approach - patching an `isHidden` / `isCraftable` read path - was
rejected. Those flags only decide what a UI list renders; the loadout slots,
the equipped storage, the profile payload and the craft screen all read
ownership from the item registry instead, so a flag patch produces rows that
cannot be equipped and that vanish on the next save round-trip.

This port therefore grants **real ownership** through the very same stock
item-inventory transaction that the module unlock already uses
(`docs/PORT_23_1_3_MODULES.md`). Nothing on the read side is touched: the
Armory shows what the game itself materialized from the registry.

## Verified targets

All offsets are RVAs in the supplied 23.1.3 ARM64 `libil2cpp.so`
(ELF build id `57fcc18d2db06212416d480d53c0f881ee47c52a`), cross-checked
against `dump2313.cs`.

### Ownership (identical to the module unlock)

| Managed target | RVA | Role |
| --- | --- | --- |
| `PGCompany.上丞丅三业丙世不丙::下丌丑丁下丟丛丘上()` | `0x3046000` | item registry singleton |
| `丙丛业丐丐七丛不丂(key, Nullable<丙与不与丟丂一东丟>)` | `0x304F634` | owned count for one item key |
| `丘上丄三业丏丙不且(key, Nullable<与专丂丕丌丅东丂东>, Action)` | `0x3062B08` | grant one item |

### Enumeration: two independent stock sources

| Managed target | RVA | Role |
| --- | --- | --- |
| `丈丂丆丙丂一七丞丌(OfferItemType)` | `0x3060030` | registry items of a type, `List<三丛丐丙丈丌丈专万>`, no owned filter |
| `PGCompany.丄丝丘丆丈丆丝丆丄::三与七丆丅丆丕丒业(OfferItemType, CategoryNames)` | `0x305C074` | static catalogue per category, `List<丒专与三七丁丌丟丆>` |
| `丌丄丛丈与丝丑世丆(丒专与三七丁丌丟丆)` | `0x30479D0` | catalogue entry -> item key `丑一丘与丁丄专专专` |
| `丁丒丕丌丂丌且丙且(CategoryNames)` | `0x305C50C` | category -> `OfferItemType` |
| `Progress.东丝丂丄业丕且丙丑::丞丏业丐丒与业丗与()` | `0x01B3BA40` | must exist before the first grant |

### Item fields (`PGCompany.三丛丐丙丈丌丈专万`, the base class of every registry item)

| Field | Offset | Use |
| --- | --- | --- |
| `<世下丐不丞与丞七丄>k__BackingField` | `+0x10` | display name, logging only |
| `<丅丘三丈专丝下丈不>k__BackingField` | `+0x20` | `OfferItemType` |
| `<下丕三上丂三丝丅丐>k__BackingField` | `+0x28` | item key (`丑一丘与丁丄专专专`, a reference type) |

### Swept item types and categories

`Rilisoft.OfferItemType`: `Weapon=10`, `Armor=20`, `Mask=30`, `Hat=40`,
`Boots=50`, `Cape=60`, `Gadget=70` (plus `Skin=65` only when
`kIncludeSkins` is enabled).

`CategoryNames` sweep list: `PrimaryCategory=0` … `PremiumCategory=5`,
`HatsCategory=6`, `ArmorCategory=7`, `SkinsCategory=8`, `CapesCategory=9`,
`BootsCategory=10`, `GearCategory=11`, `MaskCategory=12`,
`ThrowingCategory=12500`, `ToolsCategoty=13000`, `SupportCategory=13500`,
`BestWeapons=35000`, `BestWear=40000`, `BestGadgets=45000`,
`WeaponCraftCategory=110000`, `EventCraftCategory=135000`,
`SetsCraftCategory=140000`. Every category is resolved to a type through the
stock mapper first, and anything outside the target type set is skipped, so
adding a category to that list can never widen the unlock.

## Implementation

`opg3d/src/main/cpp/hidden_items_2313.h` (header-only, like every other
23.1.3 feature port), armed from `main.cpp` and pumped from the
`MainMenuController.Update` hook that `progression_2313.h` already owns.

One sweep is two stages, each with a persistent cursor:

1. **Registry stage** - for each targeted `OfferItemType`, walk
   `丈丂丆丙丂一七丞丌(type)`, read each item's key from `+0x28`.
2. **Catalogue stage** - for each category, map it to a type, walk
   `三与七丆丅丆丕丒业(type, category)`, convert each catalogue entry to a key
   through `丌丄丛丈与丝丑世丆`.

Both stages end in the same idempotent step:

```
count = 丙丛业丐丐七丛不丂(key, null)
if count >= 1 -> already owned, nothing is written
if count == 0 -> 丘上丄三业丏丙不且(key, null, null); re-read count; must now be >= 1
```

The two sources overlap on purpose: an item omitted by one is still reached
by the other, and the ownership check keeps the overlap free of duplicate
grants.

ABI notes: enum arguments are plain `int32`; per AAPCS64 both `Nullable<>`
arguments are larger than 16 bytes and are therefore passed indirectly, and
an all-zero `Nullable<>` is a null optional (`has_value` at offset 0), so the
callee substitutes its own defaults. `List<T>` accessors are resolved off the
concrete object because the two stages walk two different instantiations.
Managed list pointers are never cached across frames: each tick re-fetches
the list and indexes it with the stored cursor.

Tunables (top of the header): `kWarmupFrames=300`, `kGrantsPerTick=2`,
`kMaxPasses=3`, `kRecheckFrames=1800`, `kMaxConsecutiveFailures=48`,
`kMaxListEntries=8192`.

## Safety properties

* **Fail-closed.** Four unambiguous metadata targets must resolve to exactly
  `base + RVA` before the three overloaded registry entry points (which
  differ only in argument type, so metadata name plus argument count cannot
  select them) are taken by RVA. On any mismatch nothing is armed and the
  bootstrap reports `hidden-items=0`.
* **Idempotent.** A grant only runs when the owned count is `0`, and it is
  verified by re-reading the count. Restarts, cloud merges and manual
  purchases cannot produce duplicates.
* **No spikes.** Two definitions per main-menu frame, warm-up after the
  module sweep, so the two ports never drive the stock transaction on the
  same frame.
* **Self-disarming.** Sweeps stop as soon as a full pass grants nothing, and
  the port disarms after `kMaxConsecutiveFailures` consecutive failures, so a
  layout mismatch degrades to a no-op instead of a per-frame spin.
* **Read paths untouched.** No hidden/craftable flag, no UI list, no craft
  recipe and no per-item storage entry is patched. If `0x3060030` had turned
  out to be an owned-items view, that stage would simply have found every
  entry owned and done nothing.
* **Menu only.** Nothing runs in battle: the pump is driven exclusively from
  the main-menu `Update` slot.

## Runtime diagnostics

`adb logcat -s OPG3D` on a successful run:

```
23.1.3-hidden-items: armed: every hidden weapon, wear item and gadget the build ships is granted through the stock item inventory (2 per menu frame, 3 sweeps max, skins excluded)
23.1.3-hidden-items: granted weapon 'Ultimatum' (registry stage, count 0 -> 1)
23.1.3-hidden-items: granted gadget 'HarpoonGun_1' (registry stage, count 0 -> 1)
23.1.3-hidden-items: pass 1 complete (definitions seen=... granted=... already owned=... failed=0)
23.1.3-hidden-items: hidden weapon, wear and gadget inventory complete (... definitions owned this pass, ... granted in total)
```

The first 12 grants are logged in full, then every 16th, so a large first
sweep cannot flood the log. A refused arm is a single `LOGE` naming the
target that did not line up.

## Validation checklist

1. Install and reach the main menu; confirm the `armed` line, then the
   `pass 1 complete` line about five seconds later.
2. Armory -> weapons: **Ultimatum** and **Locator** are present, equippable
   and stay equipped after a menu round-trip.
3. Armory -> gadgets: **Harpoon** is present and equippable.
4. Wear tabs (hats, armor, boots, capes, masks): previously missing pieces
   are owned.
5. Restart the app: the items are still owned and the log reports
   `granted=0` for the first pass (nothing is re-granted).
6. Enter a match with a newly unlocked weapon and a newly unlocked gadget:
   both work, and no cheat banner appears (the banner suppression from
   `docs/PORT_23_1_3_PROGRESSION.md` stays in place).
7. `hidden-items=1` in the bootstrap summary; `hidden-items=0` must be
   treated as a port failure and investigated, not ignored.

## Artifact provenance

Every target above was proven against the artifacts supplied with the
request:

| Artifact | SHA-256 |
| --- | --- |
| `libil2cpp.so` | `f0a130c4e8d9487059eab4b0f08462f6aa7d057510cada0fd6fb3043c77deb5c` |
| `global-metadata.dat` | `28b8bddf53a8ebdaf70aec1e672d3bdea6e46ca2b2e478f1b7e66e69884c99dd` |
| `dump2313.cs` | `803371a6246bdeb6f230ea54dbbbf77108ce088cdfbbcd0f6843a45185398830` |

ELF build id: `57fcc18d2db06212416d480d53c0f881ee47c52a`.

## Explicitly not ported

* Skins (`Skin`, `WeaponSkin`, `ArmorSkin`, `WearSkin`) - opt-in switch only.
* Pets, eggs, lobby and fort items, graffiti, characters, details, craft sets
  and currencies - outside the request and each one has its own economy.
* Craft recipes themselves. Crafting keeps working exactly as
  `docs/PORT_23_1_3_CRAFTING.md` describes; this port only makes the
  unreachable results owned.
