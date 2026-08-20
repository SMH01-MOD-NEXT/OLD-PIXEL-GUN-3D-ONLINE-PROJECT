# Old Pixel Gun 3D Online Project

Open-source (GPLv3) restoration of online multiplayer for **Pixel Gun 3D 13.2.1** on Android (`armeabi-v7a`). The original backend for this legacy client no longer exists, so the project supplies a compatibility layer and routes multiplayer exclusively through a fan-run **Photon Cloud** application.

The project does not connect to Cubic Games services and is not affiliated with Cubic Games or Photon. It is a compatibility project for a discontinued 2017 client, not a cheat or a bypass of active server-side checks.

## Features

`libopg3d.so` is loaded into the game process and installs metadata-resolved IL2CPP hooks for the parts of the old client that depended on retired services:

- **Photon AppID redirect.** The mod replaces the AppID at the source of the game's selection path, before `ServerSettings.UseCloud(...)` consumes it. The AppID comes from a build secret and is never committed or printed in clear text.
- **Fixed EU routing.** Every connection uses Photon Cloud region `eu`. This removes the first-launch `32756 / Region none is not available` race and keeps every player in one regional room pool. EU must also remain the only allowed region in the Photon dashboard.
- **Dead-backend disconnect guard.** The obsolete `FriendsController.Update` disconnect path is quarantined only while Photon is connecting or connected. Normal PUN callbacks, room transitions, RPC, synchronization, and intentional disconnects remain stock.
- **Persistent release progression.** The game repeatedly runs its own `ExperienceController.AddExperience` path until the real final level, **38**, is reached. Repeated level-up popups are suppressed only during automatic steps. Coins and Gems are maintained at **999,999,999** through the stock bank and Storager paths, so the values persist.
- **Automatic tutorial skip.** The initial training stage is completed before scene routing can send a fresh profile into training. Both the first-match stage and the 12.1+ shop-tutorial flag are written through the game's own persistence APIs, so the skip survives a restart.
- **Free detail weapons.** Weapons that originally required craft details report **0 required details** and **0 craft seconds**, allowing the stock craft flow to grant them immediately. The override is restricted to the six weapon categories; avatars and unrelated craft categories keep their original requirements.
- **Offline-safe weapon upgrades.** When the retired server-time endpoint returns an invalid value, the client receives local Unix UTC seconds instead. The fallback is monotonic, so a device-clock rollback cannot strand an active item. The normal craft/upgrade timers, inventory provisioning, save routines, and UI refreshes remain responsible for state.
- **Connection and compatibility diagnostics.** Runtime decisions are logged under one `OPG3D` logcat tag with sequence numbers, timestamps, thread IDs, and caller addresses where useful.

## How the armory compatibility works

The supplied PG3D 13.2.1 dump, metadata, and ARMv7 `libil2cpp.so` were used as local analysis inputs. They are not part of the repository and must not be committed.

The verified managed targets are:

1. `BalanceController.NumOfDetailsForCraft(string)` identifies items whose stock configuration requires details.
2. `ItemDb.GetItemCategory(string)` returns categories `0` through `5` for Primary, Backup, Melee, Special, Sniper, and Premium weapons.
3. For detail-based items in those categories only, the compatibility hook reports a requirement of `0`.
4. `BalanceController.GetFullTimeCraftInSeconds(string)` reports `0` for the same items, removing the craft wait.
5. The original craft button, inventory provisioning, persistence, and UI refresh paths remain in control of granting the weapon.

This replaces the previous Gem-price conversion, which did not work reliably in the legacy client. No synthetic premium-currency transaction or server response is required.

## Technical design

1. **Safe early initialization.** A native constructor starts a detached thread, waits for `libil2cpp.so`, waits for the assembly list to stabilize, and attaches to the IL2CPP runtime before touching metadata.
2. **Symbol resolution without `dlsym`.** Android linker namespaces and `RTLD_LOCAL` hide the game's exports from `dlsym(RTLD_DEFAULT)`. `elf_sym` reads the already-mapped ELF dynamic symbol table directly.
3. **Metadata-driven, fail-closed hooks.** Targets are found with the IL2CPP metadata API and hooked at their actual `MethodInfo::methodPointer` using ShadowHook in UNIQUE mode. No managed method RVA is compiled into the project. If a required class, method, or trampoline is unavailable, the module reports failure instead of patching an assumed address.
4. **Stock state transitions.** Tutorial completion, level advancement, bank writes, zero-detail crafting, item grants, upgrades, and saves go through original game methods. Hooks provide missing decisions or inputs rather than replacing the persistence model.
5. **Category-scoped detail override.** The original detail requirement is checked first, then the stock item category restricts the zero requirement and zero duration to weapons. Non-weapon craft content remains unchanged.
6. **ARM32 ABI correctness.** This IL2CPP build gives static generated methods a hidden `null` context in `r0`; managed arguments begin in `r1`, followed by `MethodInfo*`. Every hook and stock-call signature models that layout explicitly. A `long` return such as server time uses the ARM32 `r0:r1` pair.
7. **Unwinder compatibility.** The native library is compiled with `-fno-exceptions -funwind-tables`. This lets managed exceptions unwind through hook frames without mixing the game's GNU-compatible ARM EHABI context with the statically linked LLVM unwinder.
8. **Single native artifact.** ShadowHook v2.0.1 is built from source, patched for this ARMv7 environment, and linked statically into `libopg3d.so`.

## Building

The Photon AppID is a credential and must not be stored in the repository.

### GitHub Actions

Create the repository Actions secret `PHOTON_APP_ID`. The workflow exposes it as `ORG_GRADLE_PROJECT_PHOTON_APP_ID`, builds the selected maintained branch, and publishes one `libopg3d.so` artifact:

- `13.2.1` — PG3D 13.2.1, including the release progression and legacy gameplay compatibility described above.
- `12.5.0` — the older PG3D 12.5.0 target.

### Local build

Requirements: JDK 17, Android SDK, NDK `27.3.13750724`, and CMake `3.31.5`.

```bash
gradle :opg3d:assembleRelease \
  -PPHOTON_APP_ID="xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
```

A build without the AppID is allowed for diagnostics, but the AppID hook runs in passthrough mode and logs a warning.

Optional experimental Gradle properties are `PHOTON_MODE=cloud` (default) or `selfhosted`, `PHOTON_SERVER_ADDRESS`, and `PHOTON_SERVER_PORT` (default `5055`).

## Diagnostics

```bash
adb logcat -s OPG3D
```

Relevant healthy-startup lines include:

```text
init: phase 0 ready — Photon Cloud routing, progression grant, tutorial skip, free detail weapons and upgrade timers active
legacy: tutorial skipped automatically; stage 3 and shop tutorial completion saved
legacy: retired server-time endpoint unavailable; using local UTC seconds for crafting and upgrades
legacy: tutorial auto-skip and local upgrade/crafting time armed
free-details: zero-detail, instant weapon crafting armed
free-details: detail weapons now require 0 details and 0 craft seconds
boost: persisted grant armed (trigger=MainMenuController.Update, level target=38, currency target=999999999, level-up UI=skipped)
cloud-force[...]: ... host=1(PhotonCloud expected=1) region=0(eu expected=0) ... ready=1
photon-status: 1024 (Connect) ...
ui: ConnectionControl.OnConnectedToMaster state=16 ...
```

AppIDs are logged only as a length and FNV-1a fingerprint.

Caller RVAs can be mapped to managed methods with the matching private `dump.cs` file. Dumps, metadata, and `libil2cpp.so` are analysis inputs and must not be committed:

```bash
python3 tools/symbolize_log.py --dump dump1321.cs --log logcat.txt
```

## Project structure

```text
opg3d/src/main/cpp/
├── main.cpp                  # IL2CPP wait/attach and module installation
├── elf_sym.cpp/.h            # in-memory ELF export resolver
├── il2cpp.cpp/.h             # metadata and managed-value helpers
├── hook.cpp/.h               # fail-closed ShadowHook wrapper
├── photon_hooks.cpp/.h       # AppID override and connection tracing
├── cloud_guard.h             # fixed-EU routing and obsolete disconnect guard
├── player_boost.h            # stock level steps and verified bank top-up
├── legacy_gameplay.h         # tutorials and upgrade clock fallback
├── free_detail_weapons.h     # zero-detail, instant weapon crafting
├── config.h                  # build-time defaults; no credentials
└── CMakeLists.txt            # pinned static ShadowHook and libopg3d.so
```

## Verification status

- Target: PG3D 13.2.1, Android ARMv7, IL2CPP metadata v22.
- The detail-count, craft-duration, and weapon-category targets were verified against the supplied 13.2.1 analysis files.
- Photon handshake, master/game-server transitions, room creation, and a 1v1 match have been verified between two devices on different networks.
- The final zero-detail grant behavior still requires an on-device check with the target client before release.
- All clients must use EU, with EU configured as the only allowed Photon region.
- The mechanism used to load `libopg3d.so` into the game process is outside this repository.

## License

Project code is licensed under GPLv3; see [LICENSE](LICENSE). ShadowHook is MIT-licensed; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
