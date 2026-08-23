# Pixel Gun 3D 23.1.3 ARM64 — weapon and armor modules

This document describes only the weapon and armor module unlock in
`weapon_modules_2313.h`. Module crafting, weapon crafting, clan crafting, and
per-item equipped-module state are outside this change.

## Scope

The 23.1.3 `ModuleData.ModuleCategory` values are:

| Value | Category |
|---:|---|
| 1 | Primary |
| 2 | Backup |
| 3 | Melee |
| 4 | Special |
| 5 | Sniper |
| 6 | Premium |
| 7 | Armor |

All seven categories use the same `PGCompany` module class, so one code path
covers weapon modules and armor modules alike. The static catalogue ships 42
module definitions.

## Verified target

The implementation is branch-specific and was verified against the installed
23.1.3 ARM64 package:

| Artifact | SHA-256 |
|---|---|
| `libil2cpp.so` | `f0a130c4e8d9487059eab4b0f08462f6aa7d057510cada0fd6fb3043c77deb5c` |
| `global-metadata.dat` | `28b8bddf53a8ebdaf70aec1e672d3bdea6e46ca2b2e478f1b7e66e69884c99dd` |
| ELF Build ID | `57fcc18d2db06212416d480d53c0f881ee47c52a` |

The pre-existing `analys2313/dump1321.cs` identifies itself as 13.2.1, and the
pre-existing `analys2313/libil2cpp.so` is a 32-bit ARM ELF. Neither matches the
23.1.3 ARM64 target. The exact installed APK was therefore extracted from the
connected test device and used to regenerate the 23.1.3 dump. No address or
managed identifier from the 13.2.1 artifacts is used by this fix.

## Two falsified approaches

Both earlier attempts patched the *read* side of the module system and both were
disproved by device logs. They are recorded here so they are never retried.

### 1. Appending the static catalogue to the controller lists

`ModulesController` holds these fields:

```text
+0x30  List<module>
+0x38  List<moduleSet>
```

`ModulesController.OnInstanceCreated` materializes them from the module
container, so they already hold 42 entries. The static catalogue holds 42
*different* managed instances, and `List<T>.Contains` compares by reference, so
appending it only logged

```text
modules 42 +42 -> 84, sets 42 +42 -> 84
```

That is duplicated catalogue data, not ownership, and the old `>= 42`
completion check reported a false success.

### 2. Promoting the module inventory counter

The second attempt hooked the module inventory counter and turned `0` into `1`.
The promotion never fired even once, while the level clamp in the same header
logged normally:

```text
23.1.3-modules: level 1 -> 10 (call=1)      <- fires
23.1.3-modules: inventory count 0 -> 1      <- never logged
```

The reason is structural: the counter is an instance method on a module object,
and a module object only exists once the item is owned. Missing modules are
never queried at all, so no read-path hook can create them. The dev shortcuts
`ModulesController.AddAllModulesDEV` (`0x028178E4`) and `AddAllMaxModulesDEV`
(`0x028178E8`) cannot help either: in this build both are stripped to a single
`ret`.

## Authoritative ownership path

Disassembly of the module inventory counter shows what actually decides whether
a module exists:

```text
module -> base item key   (field <key>k__BackingField, +0x28)
       -> registry singleton  PGCompany.<item registry>::<instance>()  0x03046000
       -> registry lookup     <find item>(key, Nullable<...>)          0x030603D4
       -> owned count         virtual slot +0x1C8
```

Relevant 23.1.3 entry points:

| Role | Managed identifier | RVA |
|---|---|---:|
| Static module catalogue | `PGCompany.丐丞丒专且丁丈丌业::丞七丌业丛丂丙上丝()` | `0x03048A5C` |
| Item registry singleton | `PGCompany.上丞丅三业丙世不丙::下丌丑丁下丟丛丘上()` | `0x03046000` |
| Owned count for a key | `丙丛业丐丐七丛不丂(key, Nullable<丙与不与丟丂一东丟>)` | `0x0304F634` |
| Grant a key | `丘上丄三业丏丙不且(key, Nullable<与专丂丕丌丅东丂东>, Action)` | `0x03062B08` |
| Receive-items transaction | `下万丗世丑万丌东东(spend, receive, context)` | `0x03061DB0` |
| Module inventory count | `PGCompany.丐三七世丝丗与丛上::与丏一丗七丝一七丏()` | `0x024B0C38` |
| Module current level | `PGCompany.丐三七世丝丗与丛上::七且丐东丒丆丑丈万()` | `0x024B0BB0` |
| Progress service singleton | `Progress.东丝丂丄业丕且丙丑::丞丏业丐丒与业丗与()` | `0x01B3BA40` |

The grant wrapper copies the 104-byte `Nullable<obtain cause>`, wraps the key
into a list, and forwards it to the list overload (`0x03061C20`), which reads
`HasValue` at offset 0 and then runs the stock receive-items transaction with
an empty spend list. That transaction is entirely local: list and LINQ work,
the registry's own private mutators, the item-changed event, Progress service
notifications and telemetry. Its call graph contains no network transport, so
it works while the retired 23.1.3 backend is offline.

`ModulesController.OnInstanceCreated` (`0x02814810`) subscribes to those
Progress notifications and rebuilds its module lists from them
(`0x02814B08`), which is why no UI list has to be touched by hand.

## Implemented fix

`weapon_modules_2313.h` now grants ownership instead of faking reads:

1. **Image proof.** The two unambiguous getters (module catalogue, registry
   singleton) are resolved through metadata and must land exactly on
   `libil2cpp.so base + recorded RVA`. If either check fails, nothing is armed.
   This is required because the count and grant methods are overloaded by
   argument *type* only, so metadata name plus argument count cannot select the
   right overload; they are therefore taken by RVA from the verified image.
2. **Bounded sweep.** From the existing `MainMenuController.Update` hook the
   port walks the static catalogue two definitions per frame, after a 120-frame
   warm-up and only while the Progress service and the registry exist.
3. **Idempotent grant.** For each definition it reads the base item key from
   `<key>k__BackingField`, asks the registry for the owned count with a null
   `Nullable<>` filter, and only when the count is below one calls the stock
   grant with a null obtain cause and no callback. The count is re-read
   afterwards, so every grant is verified rather than assumed.
4. **Level clamp.** The one proven-working hook is kept: the module level
   getter clamps values below `10` to `10`.

Up to three sweeps run (~15 s apart) so late registry initialisation is still
covered; the port disarms itself once every definition is owned.

### Safety properties

- Nothing is written unless the registry reports the module as not owned.
- Already-owned modules, higher owned counts and levels above 10 are preserved.
- An all-zero `Nullable<T>` is a null optional (`HasValue` lives at offset 0),
  so the stock code substitutes its own defaults — the same values it uses for
  an unspecified obtain cause.
- Per-AAPCS64 both `Nullable<>` arguments (24 and 104 bytes) are passed
  indirectly, which is exactly what the disassembly of both callees expects.
- No managed call is made from the bootstrap thread; grants run on the game
  thread from the menu `Update` slot, at most two per frame.
- The controller lists, the static catalogue, module sets and per-item
  equipped-module storage are never modified.
- Crafting paths are untouched.
- Metadata resolution, the field lookup, the image proof and the level hook are
  all required: any failure logs and arms nothing.

## Runtime diagnostics

Expected installation lines:

```text
hook: installed 丐三七世丝丗与丛上.七且丐东丒丆丑丈万/0 @ 0x...
23.1.3-modules: armed: every weapon and armor module definition is granted through the stock item inventory (2 per menu frame, 3 sweeps max) and displayed at level 10
```

Expected grant lines (first eight in full, then every eighth):

```text
23.1.3-modules: granted '<item name>' (7/42, count 0 -> 1)
23.1.3-modules: pass 1 complete (definitions=42 granted=N already owned=M failed=0)
23.1.3-modules: weapon and armor module inventory complete (42/42 definitions owned, levels shown as 10)
```

A failed transaction is reported per definition:

```text
23.1.3-modules: grant did not register '<item name>' (7/42, count 0 -> 0)
```

Retired patterns that must never appear again:

```text
inventory grant ... 42 +42 -> 84
23.1.3-modules: inventory count 0 -> 1
```

## Validation checklist

1. Build branch `23.1.3` for `arm64-v8a` with the Photon AppID supplied, so the
   online route is unchanged by the module test build.
2. Confirm the image proof passes and the armed line above is logged.
3. Reach the main menu and wait a few seconds, then read the grant summary.
4. Open the Armory module screens and verify every weapon category and the
   armor category are populated.
5. Verify each module reports level X.
6. Equip weapon and armor modules, reopen the screens, and restart the game to
   verify the ownership survives.
7. Confirm no `42 -> 84` append and no inventory-count promotion is logged.
8. Confirm no crafting or per-item module-storage path changed in the diff.
