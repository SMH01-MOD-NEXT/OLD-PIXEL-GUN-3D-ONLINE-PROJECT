# 23.1.3 weapon modules — verified grant and level-X fix

## User-visible failures

Two revisions exposed two different bugs:

1. The original `weapon_modules_2313.h` installed cleanly but added no modules; the account kept its small level-I inventory.
2. The first attempted correction marked existing modules as `Max.` but rendered `XL`, `XX`, `XXV` and `LXXX`, and still did not add the complete catalogue.

The device screenshot is decisive: those Roman values are 40/20/25/80 **owned parts**, not module levels. The correction hooked the wrong integer source.

## Complete catalogue: exactly 42 modules

LLVM 15 AArch64 disassembly of `PGCompany.丐丞丒专且丁丈丌业::.cctor` (`0x02EF431C`) finds exactly **42** direct calls to module `.ctor(ItemRecord)` (`0x024B1150`) and **42** calls to module-set `.ctor(ItemRecord)` (`0x024B3278`). The independent definition table at `0x0281AA04` confirms 42 `ModuleData`, 42 `ModulePointData` and 210 legacy `ModuleDataV1` records.

No fabrication or backend response is needed: the build ships the complete 42-entry catalogue.

## Why inventory stayed incomplete

The original implementation hooked trivial list getters with zero direct `BL` call sites; IL2CPP/clang inlined those reads. The first field-writing correction used live methods, but not the lifecycle point that finishes the profile-backed lists.

`ModulesController.OnInstanceCreated()` (`0x02814810`) initializes the dictionaries and lists at `+0x30/+0x38`. `Singleton<T>` reaches it indirectly, so a direct-call scan naturally shows zero callers even though the method executes. The corrected grant hooks it and merges after the original returns. Profile/UI methods remain as later re-checks.

The merge uses `List<T>.Contains` before `Add`, preserves existing entries and logs source/before/added/after counts. Success now means an explicit `42/42`, not merely that hooks installed.

## Correct level semantics

| method | RVA | actual meaning |
| --- | --- | --- |
| `七且丐东丒丆丑丈万()` | `0x024B0BB0` | **current level**, printed by UI as a Roman numeral |
| `与丏一丗七丝一七丏()` | `0x024B0C38` | total owned parts |
| `上丟七丝丒七丝不丈(level)` | `0x024B140C` | cumulative parts threshold |
| `丌丏业丁丅丑与丆丕()` | `0x024B13C8` | parts required for the next level |
| `丗丂丙上丏丏专上专()` | `0x024B14EC` | progress inside the current level |

The disassembly gives:

```text
0x024B13C8 = threshold(currentLevel + 1) - threshold(currentLevel)
0x024B14EC = max(totalOwnedParts - threshold(currentLevel), 0)
```

The failed correction returned `totalOwnedParts` from the current-level method, so 40 became `XL`, 20 became `XX`, and 80 became `LXXX`. The fixed code hooks only the real current-level source and returns `max(original, 10)`. Part count, threshold and progress remain untouched, so the UI renders `X`.

## Device verification

```sh
adb logcat -c
adb logcat -s OPG3D | grep 23.1.3-modules
```

Expected:

```text
23.1.3-modules: corrected grant armed (expect 42 modules at level X)
23.1.3-modules: verified built-in catalogue: 42 modules, 42 module sets
23.1.3-modules: OnInstanceCreated: module source=42 owned=N +M -> 42
23.1.3-modules: module inventory complete (42/42)
23.1.3-modules: module-set inventory complete (42/42)
23.1.3-modules: displayed level 1 -> 10
```

## Lobby crafting scope

The incomplete crafting report concerns **lobby customization** (`LobbyItemsController`), not weapon crafting. `crafting_2313.h` is intentionally untouched because weapon crafting works. `lobby_catalog_2313.h` currently feeds only the controller's already-populated local list back into its add path; an incomplete source list cannot unlock missing lobby objects. That is a separate, lower-priority follow-up.
