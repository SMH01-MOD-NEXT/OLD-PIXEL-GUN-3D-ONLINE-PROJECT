# PG3D 23.1.3 ARM64 backend bootstrap

## Scope

This is the first deliberately small 23.1.3 port. It only aims to pass the
startup/auth scenes through the game's stock successful path, reach the first
run tutorial, and then reach the main menu. The 16.1.0 Photon, progression,
crafting, lobby and battle hooks are not loaded on this branch.

Target fingerprint:

- `libil2cpp.so`: ELF64 AArch64
- Build ID: `57fcc18d2db06212416d480d53c0f881ee47c52a`
- SHA-256: `f0a130c4e8d9487059eab4b0f08462f6aa7d057510cada0fd6fb3043c77deb5c`
- metadata SHA-256: `28b8bddf53a8ebdaf70aec1e672d3bdea6e46ca2b2e478f1b7e66e69884c99dd`
- dump SHA-256: `803371a6246bdeb6f230ea54dbbbf77108ce088cdfbbcd0f6843a45185398830`

## Obfuscation rule

The Chinese-looking method names are not assumed to survive an update. They
are version-local metadata keys recorded only after a structural mapping. The
mapping used:

1. stable class and lifecycle anchors (`AppsMenu`, `AuthSceneController`,
   `Awake`, `Start`, `Update`, `OnDestroy`);
2. ordered method signatures and return types across 16.1.0, 21.2.3 and 23.1.3;
3. iterator state-machine ownership;
4. native AArch64 `BL`/branch targets and direct caller graph.

This established the 23.1.3 roles below:

| Role | 23.1.3 metadata key | Native evidence |
| --- | --- | --- |
| Auth state getter | `丒与且丙业丁丏丁三()` | RVA `0x3DC0920`, same ordered property slot |
| Session-ready getter | `丄不丘丝业且丄丁世()` | RVA `0x3DC09BC`, same ordered bool slot |
| Completion iterator | `万丕丂丑丄世丈丁丌()` | RVA `0x3DC3EE0`, mapped from old completion slot; owns `七且丌丑丁世丛三且.MoveNext` |
| Completion blocker | `丑七丏三丌丕丞不丙()` | RVA `0x3DC5F54`; calls `VersionBlocker` and is called by that completion state machine |

The completion changed from a synchronous `void` routine in 21.2.3 to an
`IEnumerator` in 23.1.3. The hook therefore does not call it as the old ABI.
It creates the mapped iterator and schedules it with Unity's uniquely named
`StartCoroutine_Auto(IEnumerator)` entry. The ordinary `StartCoroutine` name is
not used because it has two one-argument overloads.

## Startup signature compatibility

The old A32 patch was discarded. The supplied AArch64 AppsMenu Start state
machine was decoded independently:

```text
0x04372974  bl  String.Compare
0x04372978  cbz w0, 0x04372B04  // accepted signature path
```

Only the verified `CBZ` is changed to an A64 unconditional branch with the same
target. Both the preceding call opcode and decision opcode must exactly match
this binary or the patch refuses to write.

## Runtime behavior

- `AuthSceneController.Awake` remains stock.
- `AuthSceneController.Start` does not launch the retired backend transport.
- The stock completion iterator remains responsible for auth flags,
  synchronization callbacks and tutorial/menu routing.
- Its VersionBlocker predicate is bypassed only while that local completion
  transaction is active.
- `Update` observes `FullySynchronized`/`Empty` plus the stock ready flag.
- A 3600-frame guard ends the bypass fail-closed if readiness never appears.
- Lifecycle traces mark AppsMenu, Auth, Training and MainMenu entry.
- No HTTP response, server payload, progress value, field offset, or ARM32
  instruction is fabricated.

## Device checklist

Capture filtered logcat while starting with the exact 23.1.3 APK/OBB:

```bash
adb logcat -c
adb logcat | grep -E "23.1.3|libopg3d|TUTORIAL|MAIN MENU"
```

Expected milestones:

1. `APK re-sign compatibility active` (or `already patched`).
2. `AuthSceneController.Awake RETURN`.
3. `stock completion coroutine accepted`.
4. `stock completion published a usable session`.
5. First install: `TUTORIAL REACHED`, tutorial can be completed.
6. Later launch: `MAIN MENU REACHED`.
7. No retired-backend retry loop and no `completion timed out fail-closed`.

If any exact opcode or metadata lookup mismatches, stop testing that build: the
module intentionally refuses to guess. Attach the complete filtered logcat to
the PR before expanding the port to progression, time fallback, crafting or
Photon.
