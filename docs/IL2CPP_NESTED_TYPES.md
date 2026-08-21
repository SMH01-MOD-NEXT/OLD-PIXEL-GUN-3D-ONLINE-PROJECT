# Nested types: why the two iterator hooks failed, and how they resolve now

This note covers the one remaining gap in the armory v12.1 cheat-banner
suppression on 13.2.1: both compiler-generated iterator hooks failed to
install, so the CHEAT DETECTED overlay could still appear on screen even though
it could no longer erase anything.

## Symptom

From `adb logcat -s OPG3D` on the affected build:

```
W hook: optional method not found: AppsMenu/<MeetTheCoroutine>c__Iterator0.MoveNext/0
W hook: optional method not found: <MeetTheCoroutine>c__Iterator0.MoveNext/0
W hook: optional method not found: AppsMenu.<MeetTheCoroutine>c__Iterator0.MoveNext/0
E cheat-guard: the delayed CHEAT DETECTED scene load could not be hooked under
  any known nested class name; the overlay can still appear, but it can no
  longer erase anything
W hook: optional method not found: CheatDetectedBanner/<SendCheatTypeOnServer>c__Iterator0.MoveNext/0
W hook: optional method not found: <SendCheatTypeOnServer>c__Iterator0.MoveNext/0
W hook: optional method not found: CheatDetectedBanner.<SendCheatTypeOnServer>c__Iterator0.MoveNext/0
W cheat-guard: the abuse report iterator could not be hooked; a report could
  still be sent if the wipe path is ever reached
```

Every *required* hook installed, so `cheat_guard::install_hooks()` still
returned true and the build was never reported as unsafe:

```
I cheat-guard: armed (verdicts=forced clean, ...)
```

That combination is the worst one to debug: the module reports itself as armed
while the two hooks that stop the banner from being *shown* are missing.

## Why all three spellings failed

The names were never wrong. Three facts, taken together, explain it:

1. The declaring types resolve fine. `AppsMenu.MeetTheCoroutine/3`,
   `AppsMenu.GetAbuseKey_*`, and every `CheatDetectedBanner` method installed
   in the same run, so neither the class names nor the global namespace were
   at fault.
2. The nested types exist in the shipped metadata. `global-metadata.dat` of
   this exact build contains both `<MeetTheCoroutine>c__Iterator0` and
   `<SendCheatTypeOnServer>c__Iterator0` in its string table.
3. In metadata v22 a nested type is reachable only through its declaring
   type. The image-level type list that `il2cpp_class_from_name()` walks holds
   top-level types only; nested types hang off `Il2CppTypeDefinition` of the
   type that declares them.

So `il2cpp_class_from_name()` cannot return a nested class under *any*
spelling: not `Outer/Nested`, not `Outer.Nested`, and not the bare `Nested`.
Adding a fourth spelling would have failed the same way. Since
`il2cpp::find_class()` was implemented purely on top of that export, the two
iterator lookups were unreachable by construction.

## The fix

`il2cpp::find_class()` now falls back to a real nested-type walk. If the direct
image-level lookup fails and the requested name is composite, the name is split
at `/` or `.` — from the right, because `.` is also the namespace separator —
the left part is resolved recursively as the declaring type, and the nested
types of that type are enumerated and matched by their short metadata name.

The walk uses `il2cpp_class_get_nested_types()`, which is exported by the
13.2.1 `libil2cpp.so` (verified against the shipped binary, along with
`il2cpp_class_get_name()`). It sets up the nested-type list on demand and does
not run managed static constructors, so the existing contract of this file is
preserved.

Both new exports are bound *outside* the required set in `il2cpp::resolve()`.
On a layout that does not export them, every top-level hook still installs and
the nested blocks degrade to exactly the warning they produced before, instead
of failing the whole module.

No call site had to change: `cheat_guard.h` keeps its existing spelling list,
and the first entry (`Outer/<Method>c__IteratorN`) is now the one that
resolves. Any other module that ever needs a nested type gets the same
behaviour for free.

## How to verify on device

`adb logcat -s OPG3D` should now show the two installs instead of the six
warnings, with real addresses:

```
I hook: installed AppsMenu/<MeetTheCoroutine>c__Iterator0.MoveNext/0 @ 0x...
I hook: installed CheatDetectedBanner/<SendCheatTypeOnServer>c__Iterator0.MoveNext/0 @ 0x...
I cheat-guard: the CHEAT DETECTED scene trigger is blocked (<class>.MoveNext)
I cheat-guard: the abuse report POST is blocked (<class>.MoveNext)
```

Neither `the delayed CHEAT DETECTED scene load could not be hooked ...` nor
`the abuse report iterator could not be hooked ...` should appear again.

The interesting confirmation line stays the same as before: when the client
arms the banner, `cheat-guard: AppsMenu.MeetTheCoroutine('ClosingScene',
abuseTicks=..., nowTicks=...) was armed by the client; its first tick will be
refused` proves the delivery path was reached and refused.

## Note on `CHEAT_BANNER_SUPPRESSION.md`

Two passages in that document describe the old behaviour and are now stale:
the sentence stating that "three metadata spellings are tried for each"
iterator, and the failure-mode row for `the delayed CHEAT DETECTED scene load
could not be hooked under any known nested class name`. The spelling list is
still tried, but it is the nested walk that resolves the class, and that
failure row should now only be reachable on a genuinely different metadata
layout.
