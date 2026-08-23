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

All seven categories use the same `PGCompany` module class, so the two hooks in
this port cover both weapon modules and armor modules. The catalog contains 42
module definitions in total.

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

## Root cause

The previous implementation treated these `ModulesController` fields as owned
inventory:

```text
+0x30  List<module>
+0x38  List<moduleSet>
```

That interpretation is incorrect for 23.1.3. `ModulesController.OnInstanceCreated`
clears and materializes the normal definition lists from `ModulesContainer`.
They already contain 42 module entries before the mod attempts a grant.

A separate static catalog also contains 42 module objects, but they are
different managed instances. `List<T>.Contains` compares those objects by
reference, so appending the static catalog reports:

```text
modules 42 +42 -> 84
```

This is duplicate catalog data, not ownership. The old `>= 42` completion check
then produced a false success result even though the authoritative ownership
path had not changed.

## Authoritative ownership path

The relevant 23.1.3 methods are:

| Role | Managed identifier | RVA |
|---|---|---:|
| Module inventory count | `与丏一丗七丝一七丏()` | `0x024B0C38` |
| Module current level | `七且丐东丒丆丑丈万()` | `0x024B0BB0` |
| Progress level lookup | `Progress.丟一业丏一万万专丌::丁丒万不丙丛丂丏丗(module)` | `0x0171EEE4` |
| Stock progress increment | `Progress.丟一业丏一万万专丌::东东一丝丆与专丑且(module)` | `0x0171EFA8` |

The current-level method first calls the inventory-count method. If the result
is below one, it returns level zero without consulting the module Progress
store. Only an owned module reaches the Progress lookup.

That gate explains the reported symptom: the old level hook made level X work
for modules already shown, while armory paths that queried the raw inventory
count still rejected missing weapon and armor modules.

The stock progress increment is not used as a bootstrap grant here. It writes
persistent Progress state, derives the next value through the current-level
method, and emits stock events once per increment. Calling it in a bulk unlock
would mix a process-local mod with profile mutation and could over-increment
while a level hook is active.

## Implemented fix

`weapon_modules_2313.h` installs two required hooks:

1. **Inventory count:** call the stock method and change only `0` to `1`.
2. **Current level:** call the stock method and clamp values below `10` to `10`.

The controller and static catalog lists are never modified. This keeps the
normal 42-definition catalog, avoids reference duplicates, and makes the
stock ownership gate consistently report every weapon and armor module as
available.

### Safety properties

- Existing positive inventory quantities are preserved.
- Negative initialization/error sentinels are preserved by the count hook.
- Levels above 10 are preserved; lower values are promoted to 10.
- The original functions still run, preserving their normal read-side behavior.
- No Progress/profile value is written.
- `ModulesContainer`, module sets, and per-item equipped-module storage remain
  untouched.
- No managed method is invoked from the bootstrap thread. The logic runs only
  when the game calls the hooked methods on its own threads.
- Both hooks are required. A missing class, method, or trampoline makes module
  installation fail closed.
- Exact UTF-8 byte assertions protect all obfuscated identifiers from visual
  transcription errors.

## Runtime diagnostics

Expected installation lines:

```text
23.1.3-modules.inventory-count: installed
23.1.3-modules.current-level: installed
23.1.3-modules: armed: weapon and armor definitions remain in the stock catalog; zero inventory counts become one and levels are clamped to X
```

When previously missing modules are evaluated, the first calls also report:

```text
23.1.3-modules: inventory count 0 -> 1 (promotion=N)
23.1.3-modules: level OLD -> 10 (call=N)
```

Logs are sampled after an initial burst so normal armory redraws do not flood
`logcat`.

The obsolete success pattern must not appear:

```text
inventory grant ... 42 +42 -> 84
module inventory complete (84/42 ...)
```

## Validation checklist

1. Build branch `23.1.3` for `arm64-v8a`.
2. Confirm both required module hooks install.
3. Open the armory module screens.
4. Verify all weapon categories and the armor category are populated.
5. Verify each module reports level X.
6. Equip weapon and armor modules and reopen the screens to verify stable state.
7. Confirm the catalog remains at its stock definition count and no `42 -> 84`
   duplicate append is logged.
8. Confirm no crafting or per-item module-storage path changed in the diff.
