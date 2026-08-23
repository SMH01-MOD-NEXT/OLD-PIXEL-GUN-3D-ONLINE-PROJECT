# 23.1.3 weapon modules — verified catalogue and UI-prebuild grant

## Device-test results

The tests exposed three separate states:

1. The original implementation installed hooks but left the account's small level-I inventory unchanged.
2. The first correction showed `XL`, `XX`, `XXV` and `LXXX`. Those numbers were owned-part totals accidentally returned as levels.
3. The 13:05 device retest confirmed that the real level hook is now correct: every existing module renders as **X / Max.** However, only modules already owned by the profile were visible.

The third result isolates the remaining bug to catalogue publication/UI list construction. The level fix is retained unchanged.

## Complete built-in catalogue

LLVM 15 AArch64 disassembly of `PGCompany.丐丞丒专且丁丈丌业::.cctor` (`0x02EF431C`) finds exactly **42** calls to module `.ctor(ItemRecord)` (`0x024B1150`) and **42** calls to module-set `.ctor(ItemRecord)` (`0x024B3278`). The independent definition table at `0x0281AA04` confirms 42 `ModuleData`, 42 `ModulePointData` and 210 legacy `ModuleDataV1` records.

The build therefore already contains the complete catalogue. No fabricated managed objects or backend response is required.

## Why the level worked while missing modules stayed missing

`ModulesController.OnInstanceCreated()` (`0x02814810`) rebuilds profile-backed state and clears/repopulates the raw module lists at `+0x30/+0x38`. It can run before the native hooks are installed, so relying on that lifecycle callback alone is insufficient.

The previous UI fallback also used the wrong order:

```text
original storage getter -> grant catalogue
```

`ModulesController.上丂丁丙丛万丐万丗()` (`0x02815F94`) lazily gets or creates the storage model used by `ModuleArmoryInfoScreen`. Calling the original first allowed the UI model/cache to be built from the old short profile list. Adding catalogue entries afterwards was too late for that screen instance.

## Corrected UI-prebuild order

The open PR now performs this sequence on every storage-getter entry:

```text
grant/verify 42 modules + 42 sets
-> invalidate controller storage caches (+0x40 and +0x48)
-> call original storage getter
```

Additional safeguards:

- `List<T>.Contains`/`Add` preserves existing profile objects and prevents duplicate references.
- If the destination list API is unavailable or the verified result still has fewer than 42 entries, the build-owned complete list is published directly into `+0x30/+0x38`.
- Modules and module sets have independent fail-visible diagnostics; one failure cannot hide the other.
- `OnInstanceCreated` and profile reload remain early grant paths.
- Catalogue mutation is guarded against recursion and succeeds only after verified `42/42` counts.

## Correct level semantics

| method | RVA | actual meaning |
| --- | --- | --- |
| `七且丐东丒丆丑丈万()` | `0x024B0BB0` | **current level**, rendered as a Roman numeral |
| `与丏一丗七丝一七丏()` | `0x024B0C38` | total owned parts |
| `上丟七丝丒七丝不丈(level)` | `0x024B140C` | cumulative parts threshold |
| `丌丏业丁丅丑与丆丕()` | `0x024B13C8` | parts required for the next level |
| `丗丂丙上丏丏专上专()` | `0x024B14EC` | progress inside the current level |

The current-level hook returns `max(original, 10)`. Part totals, thresholds and progress remain untouched.

## Device verification

Perform a full process restart, open the modules screen, and capture only relevant lines:

```sh
adb logcat -c
adb logcat -s OPG3D | grep 23.1.3-modules
```

Expected lines include:

```text
23.1.3-modules: corrected prebuild grant armed (expect 42 modules at level X)
23.1.3-modules: verified built-in catalogue: 42 modules, 42 module sets
23.1.3-modules: module UI prebuild: module source=42 owned=N +M -> 42
23.1.3-modules: module inventory complete (42/42)
23.1.3-modules: module-set inventory complete (42/42)
23.1.3-modules: module UI prebuild: invalidated cached storage models before UI build
23.1.3-modules: displayed level N -> 10
```

If any stage cannot complete, the log now names the exact failing stage (null catalogue, unavailable list API, unsafe count, publication failure, or storage-cache invalidation failure).

## Scope

Weapon crafting is intentionally untouched because it works on the device. Lobby customization remains a separate, lower-priority follow-up in `lobby_catalog_2313.h` after the 42-module path is confirmed.