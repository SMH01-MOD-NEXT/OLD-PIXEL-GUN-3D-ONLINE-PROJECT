# Old Pixel Gun 3D Online Project

Open-source (GPLv3) restoration of online multiplayer for **Pixel Gun 3D 13.2.1** (Android, `armeabi-v7a`) running on a self-hosted **Photon Cloud** application. The official backend for this version was shut down long ago; the mod routes the game exclusively to our own Photon infrastructure and never touches the original servers.

This is not a cheat: it does not modify gameplay against other players and does not connect to any official online service. It is a fan-made compatibility layer for a discontinued game version.

## What the mod does

A single native library, `libopg3d.so`, is loaded into the game process and hooks a small set of managed (C#) methods through the IL2CPP metadata API:

- **AppID redirect.** At startup the game runs `Switcher.SetUpPhoton -> SelectPhotonAppId -> ServerSettings.UseCloud(...)`. `SelectPhotonAppId` picks one of several encoded AppIDs from `HiddenSettings`, consults a local kill-switch value and the APK signature, so a late write into `PhotonServerSettings.AppID` would simply be overwritten. The mod hooks the **source of the value** instead: when `PHOTON_APP_ID` is compiled in, the original selection path is skipped and the game receives the AppID of our own Photon Cloud application. Everything downstream (PUN logic, matchmaking, rooms, RPC) keeps working unchanged.
- **Fixed EU routing.** Before every connection attempt, the stock `ServerSettings.UseCloud(appId, CloudRegionCode.eu)` overload is called and the result is verified by read-back (`HostType == PhotonCloud`, `PreferredRegion == eu`, stored AppID matches). This eliminates the cold-start race of `UseCloudBestRegion` (which used to reject first launches with `32756 / Region none is not available`) and guarantees that all players land in the same regional room pool. EU is also the only allowed region on the Photon dashboard side.
- **Dead-backend guard.** While a Photon session is active, `FriendsController.Update` — which on this build calls `PhotonNetwork.Disconnect` mid-handshake — is quarantined. When PUN is idle, the original method still runs. Manual disconnects, server-side disconnects, callbacks, rooms, RPC and sync are never replaced or faked.
- **Release progression grant (13.2.1 branch).** The stock `ExperienceController.AddExperience` intentionally advances no more than one level per call, regardless of the XP amount. The mod therefore invokes it once every few menu frames until the real final level (`38`, derived from `MaxExpLevelsDefault.Length - 1` and cross-checked against `HealthByLevel`) is reached. Only `ExpController`'s level-up UI subscriber is suppressed during these calls, so the modal panel and its coroutines do not accumulate; the game's own level calculation, rewards and Storager writes still run and persist normally. Coins and Gems are topped up to `999 999 999` through `BankController.AddCoins`/`AddGems`. Their canonical Storager keys are discovered from the bank methods' own zero-delta read path rather than guessed, and the real stored balances are periodically verified and topped up again after spending.
- **Full connection tracing.** Every step of the Photon path is logged with sequence numbers, timestamps, thread ids and caller addresses, so any remaining failure is directly attributable.

## How it works

1. **Early init.** A `__attribute__((constructor))` spawns a detached thread that waits for `libil2cpp.so` to appear in the process.
2. **Symbol resolution without dlsym.** The game loads `libil2cpp.so` via `System.loadLibrary` (`RTLD_LOCAL` + Android linker namespaces), so its exports are invisible to `dlsym`. A small in-memory ELF parser (`elf_sym`) walks `dl_iterate_phdr` and `.dynsym` directly.
3. **Safe timing.** The thread waits until the IL2CPP assembly list stops changing, attaches itself to the runtime, and only then touches metadata — the runtime reallocates its internal assembly vector during registration, and reading it mid-registration used to crash the init thread.
4. **Metadata-driven hooks.** Every target method is found through `il2cpp_class_get_method_from_name` and hooked at its real `MethodInfo::methodPointer` using ShadowHook (UNIQUE mode, so the returned trampoline is directly callable). There are no hardcoded addresses: if a method is missing, installation fails closed and nothing is patched.
5. **One-file artifact.** ShadowHook v2.0.1 is built from source and linked statically into `libopg3d.so`, with two local build-time patches (`cmake/patch-shadowhook.cmake`): dropping `SA_EXPOSE_TAGBITS` (armeabi-v7a kernels reject it) and disabling the linker module (which needs a helper `.so` we intentionally don't ship).
6. **Unwinder compatibility.** The library is built with `-fno-exceptions -funwind-tables`: PG3D's `libil2cpp.so` and the statically linked LLVM libc++abi carry two incompatible ARM EHABI unwinders, and a managed exception crossing a hook frame used to crash inside `__unw_set_reg`. The long comment in `CMakeLists.txt` explains the details, and CI asserts our objects never reference `__gxx_personality_v0`.
7. **Typed field-write ABI.** The old IL2CPP embedding API passes a pointer to the value for value types, but the object pointer itself for managed references. The typed `FieldSetValueApi` adapter in `il2cpp.h` strips one level of indirection for pointer types — a naive `&value` write used to store a native stack address into `ServerSettings.AppID` and crash the GC a second later.
8. **ARM32 static-method ABI.** In this IL2CPP build, static generated methods receive a hidden `null` slot in `r0`; their first managed argument begins in `r1`, and `MethodInfo*` follows the managed arguments. The progression code models that layout explicitly. Without the hidden slot, `BankController.AddCoins/AddGems` receive zero as their `count`, which looks like a failed backend sync even though no currency write was attempted.

## Building

The Photon AppID is a credential and is never stored in the repository.

**GitHub Actions:** create the repository secret `PHOTON_APP_ID` (Settings → Secrets and variables → Actions). The workflow passes it as `ORG_GRADLE_PROJECT_PHOTON_APP_ID`, builds the `main` (12.5.0) and `13.2.1` branches, and publishes the `libopg3d-armeabi-v7a` artifact — exactly one `libopg3d.so`.

**Local** (JDK 17, Android SDK, NDK `27.3.13750724`, CMake `3.31.5`):

```bash
gradle :opg3d:assembleRelease \
  -PPHOTON_APP_ID="xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
```

Building without the secret is allowed: all diagnostic hooks are installed, but the AppID hook runs in passthrough mode and logs a warning.

Optional experimental properties: `PHOTON_MODE=cloud` (default) or `selfhosted`, `PHOTON_SERVER_ADDRESS`, `PHOTON_SERVER_PORT` (default `5055`).

## Diagnostics

All runtime logs go to the `OPG3D` logcat tag:

```bash
adb logcat -s OPG3D
```

A healthy startup looks like:

```text
init: phase 0 ready — AppID override, Photon Cloud routing, dead-backend guard, progression grant and connection tracing active
appid: SelectPhotonAppId #1 -> configured AppID {chars=36 utf8=36 fnv1a=...}
boost: persisted grant armed (trigger=MainMenuController.Update, level target=38, currency target=999999999, level-up UI=skipped)
boost: automatic level-up UI suppression active
boost: player level 7 -> 8 (target 38, +... exp per stock one-level step, table read)
boost: final player level reached and saved (38)
boost: discovered canonical coins key '...' through BankController
boost: coins 15 -> 999999999 via BankController (target 999999999)
boost: discovered canonical gems key '...' through BankController
boost: gems 10 -> 999999999 via BankController (target 999999999)
cloud-force[...]: ... ready=1
photon-status: 1024 (Connect) ...
ui: ConnectionControl.OnConnectedToMaster state=16 ...
```

The AppID is only ever logged as a length + FNV-1a fingerprint, never in plain text. Caller RVAs in the log (`pc=libil2cpp.so+0x...`) can be mapped back to managed methods with `tools/symbolize_log.py` and the matching `dump.cs`:

```bash
python3 tools/symbolize_log.py --dump dump1321.cs --log logcat.txt
```

## Project structure

```text
opg3d/src/main/cpp/
├── main.cpp                  # init thread: IL2CPP wait, attach, hook installation
├── elf_sym.cpp/.h            # in-memory ELF dynsym resolver
├── il2cpp.cpp/.h             # IL2CPP metadata API wrappers (typed field-write ABI)
├── hook.cpp/.h               # fail-closed ShadowHook wrapper
├── photon_hooks.cpp/.h       # AppID override + network path tracing
├── cloud_guard.h             # fixed Photon Cloud (EU) routing + FriendsController quarantine
├── player_boost.h            # repeated stock level steps + verified bank top-up
├── config.h                  # compile-time defaults, no credentials
└── CMakeLists.txt            # pinned static ShadowHook + libopg3d.so
```

## Status

- Target build: PG3D 13.2.1, Android ARMv7, IL2CPP metadata v22 (the older 12.5.0 target lives on the `main` branch).
- Verified on devices: full Photon handshake, master/game-server transitions, room creation and a 1v1 match between two devices on different networks.
- All clients are pinned to the EU region; keep EU as the only allowed region in the Photon dashboard.
- The way `libopg3d.so` gets loaded into the game process is out of scope for this repository.

## License and disclaimer

Project code is GPLv3, see [LICENSE](LICENSE). ShadowHook is MIT, see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Not affiliated with Cubic Games or Photon. Intended for a compatible fan-run online revival of a discontinued game version, hosted on the build owner's infrastructure.
