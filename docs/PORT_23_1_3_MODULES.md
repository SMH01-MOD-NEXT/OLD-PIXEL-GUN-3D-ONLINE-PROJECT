# 23.1.3 weapon modules — verified catalogue and level-X grant

## Root cause: why the modules "crashed and switched off"

The supplied device log (`libopg3d build 23.1.3 ARM64 ... built Aug 23 2026 11:12:11`)
shows all eight controller/UI hooks installing successfully, then one hard
failure that took the whole feature down:

```text
hook: installed ModulesController.OnInstanceCreated/0 @ 0x6dc7c22810
hook: installed ModulesController.丞业丝丁丆丑丑丕丟/0 @ 0x6dc7c23668
hook: installed ModulesController.上丂丁丙丛万丐万丗/0 @ 0x6dc7c23f94
hook: installed ModuleStorageView.丝万不丘下丄丄三丟/1 @ 0x6dc7ceab90
hook: installed ModuleStorageView.上专丅丑丘丟丙东与/0 @ 0x6dc7ceabb4
hook: installed ModuleArmoryInfoScreen.Awake/0 @ 0x6dc77c3c08
hook: installed ModuleArmoryInfoScreen.东不丁丁丟丂丝不丁/1 @ 0x6dc77c3db8
hook: installed ModuleArmoryInfoScreen.丏丁丗东且三世丄丏/0 @ 0x6dc77c4554
23.1.3-modules: module screen entry points hooked (awake=1 open=1 prebuild=1)
hook: REQUIRED method not found: 丐三七世丝丗与丛上.七且十东十丆丑丈万/0
23.1.3-modules: install incomplete, module disabled
init: 23.1.3 port incomplete: ... progression=1 crafting=1 lobby-catalog=1 modules=0
```

There is no crash anywhere in the log: no `SIGSEGV`, no `Fatal signal`, no
backtrace. `init: 23.1.3 bootstrap initialization finished cleanly` is printed
right after. The modules simply never armed, which is why nothing was granted
and every module stayed at level I.

Two independent defects produced that outcome.

### Defect 1 — the level-source identifier was corrupted

A bad search/replace had rewritten two CJK code points in `kCurrentLevel`:

| | identifier | code points |
| --- | --- | --- |
| shipped (broken) | `七且十东十丆丑丈万` | `4E03 4E14 5341 4E1C 5341 4E06 4E11 4E08 4E07` |
| metadata (correct) | `七且丐东丒丆丑丈万` | `4E03 4E14 4E10 4E1C 4E12 4E06 4E11 4E08 4E07` |

U+4E10 (`丐`) and U+4E12 (`丒`) had both become U+5341 (`十`). **U+5341 does not
occur a single time in `dump2313.cs`**, so the spelling could never have
resolved. `hook::install` is fail-closed by design, so it correctly refused to
patch an address it could not verify — and the module disabled itself.

The verified name is now byte-identical to the metadata and is pinned by a
`static_assert` on its exact UTF-8 byte sequence. A mangled spelling now fails
the build instead of failing silently on the device.

### Defect 2 — all-or-nothing install

Six hooks were folded into a single `ok` flag, so a miss on any one of them —
including purely cosmetic refresh routes — disabled the level fix and the
catalogue grant as well. `install()` is now split:

- **critical:** the level source. Installed first; if it fails, the module
  reports and disables itself (correct, since the level cannot be faked).
- **optional (8 routes):** `OnInstanceCreated`, profile reload, storage getter,
  storage-view setter/refresh, screen awake/open/prebuild. Each is best-effort
  and logged individually; a miss only slows down UI refresh.

## Measured evidence

All figures below were measured directly on the supplied `libil2cpp.so` by
decoding every `BL` instruction in the image (RVA == file offset in this build).

| RVA | member | body | direct BL sites | role |
| --- | --- | --- | --- | --- |
| `0x024B0BB0` | `module::七且丐东丒丆丑丈万()` | 132 B | **24** | **current level → hooked** |
| `0x024B0C38` | `module::与丏一丗七丝一七丏()` | — | 5 | total owned parts — untouched |
| `0x024B13C8` | `module::丌丏业丁丅丑与丆丕()` | — | 8 | parts for next level — untouched |
| `0x024B140C` | `module::上丟七丝丒七丝不丈(level)` | — | 4 | cumulative threshold — untouched |
| `0x024B14EC` | `module::丗丂丙上丏丏专上专()` | — | 7 | progress in level — untouched |
| `0x02815F94` | controller storage getter | — | 9 | optional route |
| `0x028DCB90` | `ModuleStorageView` model setter | 36 B | 4 | optional route |
| `0x028DCBB4` | `ModuleStorageView` refresh | — | 0 | optional route |
| `0x023B6554` | `ModuleArmoryInfoScreen` prebuild | — | 1 | optional route |
| `0x023B5DB8` | `ModuleArmoryInfoScreen` open | — | 0 | optional route |
| `0x0281473C` | owned-modules getter | — | 0 | inlined — unusable |
| `0x02814784` | owned-sets getter | — | 0 | inlined — unusable |
| `0x03048A5C` | catalogue modules getter | — | 0 | inlined — called directly instead |
| `0x03048AB4` | catalogue sets getter | — | 0 | inlined — called directly instead |
| `0x028178E4` | `AddAllModulesDEV()` | 4 B | 0 | bare `RET` (`D65F03C0`) — dead |
| `0x028178E8` | `AddAllMaxModulesDEV()` | 4 B | 0 | bare `RET` (`D65F03C0`) — dead |

The level source is the one target with a real body **and** plenty of live call
sites, which is why the hook actually fires. This also confirms the two earlier
failed approaches: hooking the zero-call-site getters changed nothing, and
hooking `0x024B0C38` printed part totals as Roman numerals (`XL`, `XX`, `XXV`,
`LXXX`) instead of `X`.

## Complete built-in catalogue

Decoding the catalogue `.cctor` `PGCompany.丐丞丒专且丁丈丌业::.cctor`
(`0x02EF431C`, 284 308 bytes, 10 193 `BL` sites) finds exactly:

- **42** calls to module `.ctor` (`0x024B1150`)
- **42** calls to module-set `.ctor` (`0x024B3278`)

The build already ships the complete catalogue, so no managed objects are
fabricated and no backend response is needed. Every obfuscated name used by the
native module — all 16 constants plus 5 field names — was re-verified
byte-for-byte against `dump2313.cs`, and all fields match their expected
offsets (`+0x30`, `+0x38`, `+0x40`, `+0x48` on `ModulesController`, `+0x30` on
`ModuleStorageView`).

## Correct level semantics

| method | RVA | actual meaning |
| --- | --- | --- |
| `七且丐东丒丆丑丈万()` | `0x024B0BB0` | **current level**, rendered as a Roman numeral |
| `与丏一丗七丝一七丏()` | `0x024B0C38` | total owned parts |
| `上丟七丝丒七丝不丈(level)` | `0x024B140C` | cumulative parts threshold |
| `丌丏业丁丅丑与丆丕()` | `0x024B13C8` | parts required for the next level |
| `丗丂丙上丏丏专上专()` | `0x024B14EC` | progress inside the current level |

The current-level hook returns `max(original, 10)`, so a module that is already
above X is never downgraded. Part totals, thresholds and progress remain
untouched.

## Grant path

Every controller and UI entry point on this build is a weak route (see the table
above), so none of them may be load-bearing. The guarantee comes from
`MainMenuController.Update`, which `progression_2313.h` hooks as a *required*
target and which calls `weapon_modules_2313::pump_from_main_menu()` every
main-menu frame.

On each pump the order is:

```text
grant/verify the full catalogue into +0x30 / +0x38
-> invalidate controller storage caches (+0x40 and +0x48)
-> let the original storage getter rebuild the UI model
```

Safeguards:

- `List<T>.Contains`/`Add` preserves existing profile objects and prevents
  duplicate references; direct publication of the built-in list is the fallback
  when the destination list API is unusable.
- Grant completion is no longer gated on the hard-coded 42/42 expectation. A
  size mismatch is reported as a diagnostic and the reported counts are still
  granted, so an unexpected catalogue size can no longer leave the UI
  permanently un-refreshed.
- Catalogue mutation, storage-model preparation and the level hook are all
  re-entrancy guarded (`thread_local` flags).
- The storage getter has a non-recursive fallback: if its optional hook is
  absent, the original method pointer is called directly instead.
- `install()` is idempotent.
- Modules and module sets have independent fail-visible diagnostics; one
  failure cannot hide the other.

## Device verification

Full process restart, then open the modules screen:

```sh
adb logcat -c
adb logcat -s OPG3D | grep 23.1.3-modules
```

Expected lines:

```text
23.1.3-modules: optional refresh routes installed 8/8
23.1.3-modules: armed: level X guaranteed, catalogue grant pumped from main menu + module screen (expect 42 modules, 42 module sets)
23.1.3-modules: main-menu pump: reached live ModulesController 0x...
23.1.3-modules: verified built-in catalogue: 42 modules, 42 module sets
23.1.3-modules: main-menu pump: module source=42 owned=N +M -> 42
23.1.3-modules: module inventory complete (42/42)
23.1.3-modules: module-set inventory complete (42/42)
23.1.3-modules: main-menu pump: invalidated cached storage models before UI build
23.1.3-modules: displayed level 1 -> 10 (call 1)
```

The startup summary must now end with `modules=1`. If any stage cannot complete,
the log names the exact failing stage (null catalogue, unavailable list API,
unsafe count, publication failure, storage-cache invalidation failure, or a
named unavailable optional route).

## Build check

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion \
    -Werror -fsyntax-only weapon_modules_2313.h
```

Clean. A negative test that reintroduces the corrupted `十` spelling now fails
at compile time on the `kCurrentLevel` `static_assert`, which is the regression
this PR exists to prevent.

## Scope

Weapon crafting is intentionally untouched because it works on the device.
Lobby customization remains a separate, lower-priority follow-up in
`lobby_catalog_2313.h`.
