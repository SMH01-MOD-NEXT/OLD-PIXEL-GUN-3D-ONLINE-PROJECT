# 23.1.3 weapon modules - why the grant was a no-op and how it is fixed

## Symptom

On 23.1.3 the modules screen kept showing only the handful of modules the
account really owns, all of them at level 1, even though
`weapon_modules_2313::install_hooks()` reported success and `main.cpp` arms it
unconditionally.

## Root cause: IL2CPP inlined the hooked accessors

IL2CPP translates managed code to C++ and then lets clang optimise it. Trivial
field-reading accessors are inlined into their callers, and once that happens
the method body still exists in `.text` - it simply is never called. An inline
hook on such a body installs cleanly and never fires.

That is exactly what happened to the two owned-inventory getters.

### Measurement

`objdump` in the analysis container has no AArch64 backend, so the call graph
was built directly from the machine code: every 32-bit word with
`(word >> 26) == 0x25` is a `BL`, its signed 26-bit immediate times four is the
target, and the target is resolved against the RVA table parsed out of
`dump2313.cs`. 2,262,118 `BL` sites were decoded over the whole library.

| target | RVA | direct call sites |
| --- | --- | --- |
| `ModulesController::七丄丛丕业丂专丞东()` owned modules | `0x0281473C` | **0** |
| `ModulesController::一且三不丁万丅上丑()` owned sets | `0x02814784` | **0** |
| catalogue `丞七丌业丛丂丙上丝()` | `0x03048A5C` | **0** |
| catalogue `丂丟世丅丛丙业丛专()` | `0x03048AB4` | **0** |
| module `丌丏业丁丅丑与丆丕()` current level (slot 30) | `0x024B13C8` | 8 |
| module `丗丂丙上丏丏专上专()` max level (slot 31) | `0x024B14EC` | 7 |
| module `七且丐东丒丆丑丈万()` owned duplicates | `0x024B0BB0` | **24** |
| module `与丏一丗七丝一七丏()` configured max count | `0x024B0C38` | 5 |
| `ModulesController::丞业丝丁丆丑丑丕丟()` | `0x02815668` | 1 (`MainMenuController.Awake`) |
| `ModulesController::上丂丁丙丛万丐万丗()` | `0x02815F94` | 9 |

Two conclusions:

* the list hooks were dead on arrival - the game reads the backing fields
  directly, so no module could ever be added;
* the level hook only covered 8 call sites (`InventoryItemView`,
  `StorePromotionOffersView`, ...). The modules screen uses its own inlined
  copy, hence "still level 1".

Side results from the same pass:

* `AddAllModulesDEV()` `0x028178E4` and `AddAllMaxModulesDEV()` `0x028178E8` are
  four bytes each and contain `C0 03 5F D6` - a bare `RET`. Confirmed dead.
* the catalogue type initialiser `0x02EF431C` spans 284 KB and the built-in
  module table `.cctor` `0x0281AA04` spans 56 KB, so the module definitions are
  compiled into the binary and do not depend on a backend response.

## The fix

1. **Write the data, not the accessor.** The inventory lives in
   `ModulesController.丅与世丕业丘不丂丈` (`List<module>`, `+0x30`) and
   `ModulesController.下丘丌一丞丛丂三丗` (`List<moduleSet>`, `+0x38`). The catalogue
   is merged into those fields with `List<T>.Contains` / `List<T>.Add`, so the
   inventory can only grow. If the field is still null the catalogue list is
   published directly.
2. **Drive it from live entry points.** `丞业丝丁丆丑丑丕丟()` runs from
   `MainMenuController.Awake()`; `上丂丁丙丛万丐万丗()` has 9 UI call sites and keeps
   the inventory topped up after a profile reload. Both receive the controller
   as `this`, so no singleton lookup is needed.
3. **Raise levels at their source.** Current level is
   `countToLevel(ownedDuplicates)` - the level getter itself calls the duplicate
   counter (`BL` at `0x024B13D4`). The counter has 24 live call sites, so
   reporting `与丏一丗七丝一七丏()` (the configured ceiling, read from the balance
   config singleton) makes level, upgrade progress and "is maxed" agree even at
   the inlined sites. The ceiling method never calls back into the counter, so
   the substitution cannot recurse.
4. The old level hook is kept as a fallback for its 8 live call sites, still
   clamped to the module's own maximum.

Nothing is written to `Progress`, so there is no save round-trip and no
cheat-detection churn.

## Verifying on device

```sh
adb logcat -c && adb logcat -s OPG3D | grep 23.1.3-modules
```

Expected, in order:

```
23.1.3-modules: grant armed, every module unlocked at up to level 10
23.1.3-modules: added 42 module entries, inventory now holds 42
23.1.3-modules: added N module set entries, inventory now holds N
```

If `added 0` appears the inventory already contained the catalogue; if
`could not walk the ... lists` appears the `List<T>` members failed to resolve
and the raw pointers are logged. Symbolise any crash with:

```sh
python3 tools/symbolize_log.py --dump dump2313.cs --log logcat.txt
```

## Crafting: state of the port after the same measurement

The crafting hooks were checked the same way. Unlike the module getters they are
live, so `crafting_2313.h` is not affected by the inlining problem:

| hook target | RVA | direct call sites |
| --- | --- | --- |
| detail gate | `0x0227F8B0` | 12 |
| owned details | `0x0227F954` | 47 |
| FortsManager craft clock | `0x03B81348` | 19 |
| LobbyItemsController craft clock | `0x03F7B588` | 17 |
| FortCraftController banner | `0x03E41010` | 7 |
| LobbyCraftController banner | `0x02730254` | 1 |
| clan part count | `0x03C2A078` | 2 |
| clan "has part" | `0x03C2A12C` | **0** (inlined, hook is a no-op) |
| lobby catalogue ready / list / add | `0x03F768E4` / `0x03F7F390` / `0x03F8AC04` | 5 / 7 / 11 |
| `LobbyItemsController.Update` | `0x03F8E354` | 0 (engine-driven, expected) |

What is still missing compared with `crafting_1610.h` - these five 16.1.0
behaviours were never ported and are the reason not every craft item is
unlocked:

1. required details forced to `0` (`BalanceController`, 1610
   `hook_required_details`);
2. clan craft section availability forced to `kAvailable = 3`;
3. clan medal price forced to `0`;
4. dead-clan hint suppression on `ShopNGUIController`;
5. the craft-press wrapper with the `start_craft_directly()` fallback
   (`PressScope`, `g_press_depth`, `g_press_refused`, `craft_slot_busy()`).

Porting them needs the 23.1.3 obfuscated names, which do not carry over from
16.1.0. Narrowing pass already done - `BalanceController` exposes these
`int(string)` candidates:

| candidate | RVA | callers |
| --- | --- | --- |
| `丐专丘业且丞丈下丛(string)` | `0x04717CE0` | 49 (`WeaponManager`, `ClanStockWindow`, offers) |
| `丅丏且丘丕丌丗七上(string)` | `0x04717B64` | 3 (speed-up window) |
| `丁丆不丆业丆且丛丕(string)` | `0x04716C50` | 1 |
| `与七丐丟丂丝丅不业(string)` / `一丅丁万丕丅且不不(string)` / `丙丝丗东七丈东三丏(string)` | `0x047180E0` / `0x047181CC` / `0x04734850` | 0 (inlined) |

`0x04717CE0` is the strongest candidate for the price/required-details lookup,
but it is reached from the weapon shop and the offer system as well, so forcing
it to zero without first confirming the semantics would change far more than
crafting. That confirmation is deliberately left out of this change.
