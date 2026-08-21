# Old Pixel Gun 3D Online Project

Open-source (GPLv3) restoration of online multiplayer for **Pixel Gun 3D 13.2.1** on Android (`armeabi-v7a`). The original backend for this legacy client no longer exists, so the project supplies a compatibility layer and routes multiplayer exclusively through a fan-run **Photon Cloud** application.

The project does not connect to Cubic Games services and is not affiliated with Cubic Games or Photon. It is a compatibility project for a discontinued 2017 client, not a cheat or a bypass of active server-side checks.

## Features

`libopg3d.so` is loaded into the game process and installs metadata-resolved IL2CPP hooks for the parts of the old client that depended on retired services:

- **Photon AppID redirect.** The mod replaces the AppID at the source of the game's selection path, before `ServerSettings.UseCloud(...)` consumes it. The AppID comes from a build secret and is never committed or printed in clear text.
- **Fixed EU routing.** Every connection uses Photon Cloud region `eu`. This removes the first-launch `32756 / Region none is not available` race and keeps every player in one regional room pool. EU must also remain the only allowed region in the Photon dashboard.
- **Dead-backend disconnect guard.** The obsolete `FriendsController.Update` disconnect path is quarantined only while Photon is connecting or connected. Normal PUN callbacks, room transitions, RPC, synchronization, and intentional disconnects remain stock.
- **Persistent release progression.** The game repeatedly runs its own `ExperienceController.AddExperience` path until the final level, **38**, is reached. The experience table is indexed from level 0, so its length of 39 is not a level; every computed target is clamped to 38. Repeated level-up popups are suppressed only during automatic steps. Coins and Gems are maintained at **999,999,999** through the stock bank and Storager paths, so the values persist.
- **Automatic tutorial skip.** The initial training stage is completed before scene routing can send a fresh profile into training. Both the first-match stage and the 12.1+ shop-tutorial flag are written through the game's own persistence APIs, so the skip survives a restart.
- **Free detail weapons.** Every craft recipe reports **0 required details** and the craft gate is answered positively, so the stock craft flow grants the weapon immediately. These weapons already have no craft wait in this client. There is no category filtering.
- **Clan blueprints without a clan.** Clan membership was server state and can never become true again on this client. The craft section's own availability answer is corrected, and while a craft press is on the stack the item classification answers "regular craft item", so the press follows the ordinary zero-detail recipe instead of the clan branch. Outside that press every clan answer stays stock. The clan hint window no longer advertises a requirement that has stopped blocking anything. No `Clan` object is created and no clan data is written or sent anywhere.
- **Offline-safe weapon upgrades.** When the retired server-time endpoint returns an invalid value, the client receives local Unix UTC seconds instead. The fallback is monotonic, so a device-clock rollback cannot strand an active item. The normal craft/upgrade timers, inventory provisioning, save routines, and UI refreshes remain responsible for state.
- **Build stamp and diagnostics.** The first init line prints the source tag and compile timestamp of the running library, so a stale `libopg3d.so` is recognisable immediately. Runtime decisions are logged under one `OPG3D` logcat tag with sequence numbers, timestamps, thread IDs, and caller addresses where useful.

## How the armory compatibility works

The supplied PG3D 13.2.1 dump, metadata, and ARMv7 `libil2cpp.so` were used as local analysis inputs. They are not part of the repository and must not be committed.

Rewriting a single balance value is not sufficient in this client: the craft flow asks several independent managed helpers about details. All three inputs are therefore neutralised:

1. `BalanceController.NumOfDetailsForCraft(string)` — the configured requirement of a recipe, forced to `0`.
2. `CraftSetsManager.IsEnoughDetailsForCraftItem(string, string)` — the decision the craft button makes, forced to `true`.
3. `Rilisoft.WeaponCraftDetailsInfo.GetDetailsCount(string)` — the owned-detail count read by the armory UI, reported as a large value.

Only the first hook is mandatory. The other two install when their class is present, so a metadata mismatch degrades gracefully instead of disabling the library, and each path logs the first decisions it makes with the real item id. If crafting is ever refused again, logcat shows which of the three paths the client consulted instead of leaving it to guesswork.

`WeaponCraftDetailsInfo` is the only one of the three that lives in the `Rilisoft` namespace. Earlier revisions requested the global namespace for it, the optional lookup failed silently, and the armory kept comparing against the real (zero) owned count.

Earlier revisions also restricted the override to guessed armory categories and overrode the craft duration. Both were wrong for this client: category values did not match the craftable items, and detail weapons have no craft wait at all. Category filtering and the craft-time hook have been removed.

The original craft button, inventory provisioning, persistence, and UI refresh paths still grant the weapon. No synthetic premium-currency transaction or server response is involved.

## Clan blueprints without a clan

Zero-detail pricing is not enough for clan blueprints. They correctly display `0 of 0` details and were still refused, because the clan branch of the craft flow was written for state that only ever existed on the server.

The craft section describes itself through an enum with a dedicated "no clan" value:

```csharp
enum CraftSectionAvailability { UnavailableClansNotOpened = 0, UnavailableNoClan = 1,
                               UnavailableNoDetails = 2, Available = 3 }

ShopCraftManager.GetCraftSectionAvailability() -> CraftSectionAvailability
ShopNGUIController.IsCraftSectionAvailable()   -> bool
ClansController.IsClanItem(string id)          -> bool
```

### What the device logs proved

1. **Forcing the enum works.** The hook reports `UnavailableNoClan(1) -> Available(3)`, and the hint window changed its own call-to-action from **Find a Clan** to **Raise the level** — a label that is only derived from that enum value.
2. **The press is refused before the enum is read.** The press trace prints `stock section state <unknown>`, meaning the availability getter had not run even once at that moment, and the first call right afterwards returns into `0x1ABD670..0x1ABD8F8`, the body of `CraftSectionClanInfoController.Awake`. The enum is therefore only read by the window the handler had already decided to open.
3. **Clan storage is not consulted during the press either.** No clan-storage decision is logged between the press and the window.

The only clan input the handler can consult that early is the item classification itself, `ClansController.IsClanItem(string)`.

### The workaround

- `ClansController.IsClanItem(string)` answers `false` **only while a craft press is on the stack**, so a clan blueprint is routed into the ordinary detail recipe, which already costs zero details and finishes without a timer. Outside the press the real answer is returned, which keeps item categories, clan storage screens, and the chest logic exactly as the game built them. The scope is a thread-local depth counter around `ShopNGUIController.HandleCraftButton_NoInfo(...)`.
- `ShopCraftManager.GetCraftSectionAvailability()` returns `Available` (mandatory hook).
- `ShopNGUIController.IsCraftSectionAvailable()` returns `true` (optional).
- `BalanceController.MedalsForClanCraft(string)` returns `0`: clan medals were farmed server-side and can no longer be earned.
- `ClansController.AnyPartExistsInStock(...)` and `ClansController.GetPartCountInStock(...)` report a small synthetic amount only when clan storage answers "empty".
- `ShopCraftManager.CraftItemAndNotCrafted(string)` is forwarded unchanged and only traced during a press, so a report shows whether the handler still considered the item a craftable recipe.

**No clan is fabricated.** A synthetic clan would have to satisfy every other consumer of that state — clan screens, chests, seasons, siege matchmaking, forts, analytics payloads — and any of those reading a half-initialised `Clan` object is exactly how a dead-backend client crashes. This module therefore never constructs a `Clan`, never assigns `ClansController.myClan`, never rewrites the `Clan.MyClanCache` entry on disk, and never calls a retired clan endpoint (`SendUpdateStock`, `AskCraftFortItem`, `AddClanCurrency`, `SendClanMessageDetailsBought`). The forced answers are observable only in the code path that asks whether this item may be crafted right now.

### The craft hint window

The "Crafting Process" window (`CraftSectionClanInfoController`, fields `_availability` and `buttonLabels`) is constructed from the same enum value and selects its header and call-to-action from it. Both clan states describe requirements this client can never satisfy, and the button of either one routes into the retired clan search.

`ShopNGUIController.ShowCraftSectionInfo()` is therefore suppressed in two cases: when the stock reason is a clan reason (`UnavailableNoClan` or `UnavailableClansNotOpened`), and whenever the window would be raised during a craft press, because at that point the press has already been granted. A window suppressed during a press is also logged as a warning, since it means the stock handler still refused the item and the craft did not start. When the section is genuinely about missing details outside a press, the window is forwarded unchanged and stays useful.

Only the availability gate is mandatory; everything else installs when the metadata matches and logs a warning otherwise.

## Technical design

1. **Safe early initialization.** A native constructor starts a detached thread, waits for `libil2cpp.so`, waits for the assembly list to stabilize, and attaches to the IL2CPP runtime before touching metadata.
2. **Symbol resolution without `dlsym`.** Android linker namespaces and `RTLD_LOCAL` hide the game's exports from `dlsym(RTLD_DEFAULT)`. `elf_sym` reads the already-mapped ELF dynamic symbol table directly.
3. **Metadata-driven, fail-closed hooks.** Targets are found with the IL2CPP metadata API and hooked at their actual `MethodInfo::methodPointer` using ShadowHook in UNIQUE mode. No managed method RVA is compiled into the project. If a required class, method, or trampoline is unavailable, the module reports failure instead of patching an assumed address.
4. **Read-only predicates over synthetic state.** Where the client blocks an action because a retired service can no longer answer, the missing answer is supplied at the exact predicate that asks for it. State that other systems would read — clan membership, server responses, currency ledgers — is not fabricated.
5. **Narrowest possible scope.** When a predicate has consumers beyond the blocked action, its answer is relaxed only for the duration of that action instead of globally, so unrelated screens keep observing the real state.
6. **Honest UI.** When a predicate is answered, the screens explaining that requirement are corrected as well, so the game never instructs the player to satisfy a condition that no longer exists or cannot be reached.
7. **Stock state transitions.** Tutorial completion, level advancement, bank writes, zero-detail crafting, item grants, upgrades, and saves go through original game methods. Hooks provide missing decisions or inputs rather than replacing the persistence model.
8. **Verifiable builds.** `OPG3D_BUILD_TAG` in `config.h` plus the compiler timestamp are logged before any hook is installed, so a report can always be tied to a specific library.
9. **ARM32 ABI correctness.** This IL2CPP build gives static generated methods a hidden `null` context in `r0`; managed arguments begin in `r1`, followed by `MethodInfo*`. Instance methods take the object in `r0` instead. Every hook and stock-call signature models that layout explicitly. A `long` return such as server time uses the ARM32 `r0:r1` pair.
10. **Unwinder compatibility.** The native library is compiled with `-fno-exceptions -funwind-tables`. This lets managed exceptions unwind through hook frames without mixing the game's GNU-compatible ARM EHABI context with the statically linked LLVM unwinder.
11. **Single native artifact.** ShadowHook v2.0.1 is built from source, patched for this ARMv7 environment, and linked statically into `libopg3d.so`.

## Building

The Photon AppID is a credential and must not be stored in the repository.

### GitHub Actions

Create the repository Actions secret `PHOTON_APP_ID`. The workflow exposes it as `ORG_GRADLE_PROJECT_PHOTON_APP_ID`, builds the selected maintained branch, and publishes one `libopg3d.so` artifact:

- `13.2.1` — PG3D 13.2.1, including the release progression and legacy gameplay compatibility described above.
- `12.5.0` — the older PG3D 12.5.0 target.

When starting the workflow manually, select the branch explicitly: the default branch is `12.5.0` and does not contain the 13.2.1 compatibility modules. Always confirm the build stamp of the artifact you install.

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
init: libopg3d build 13.2.1 clan-craft workaround (armory v6) built ...
init: phase 0 ready — Photon Cloud routing, progression grant, tutorial skip, free detail weapons, clan-free blueprint crafting and upgrade timers active
legacy: tutorial skipped automatically; stage 3 and shop tutorial completion saved
free-details: armed (required=0 for every recipe, craft gate=forced, owned count=synthetic); no category filtering
free-details: required details for '<item id>': 25 -> 0
clan-craft: armed (section=Available, shop shortcut=forced, medals=free, clan storage=synthetic, clan item during press=regular, clan hint window=suppressed, press scope=on, recipe trace=on); no clan object is created
boost: persisted grant armed (trigger=MainMenuController.Update, level target=38, currency target=999999999, level-up UI=skipped)
cloud-force[...]: ... host=1(PhotonCloud expected=1) region=0(eu expected=0) ... ready=1
photon-status: 1024 (Connect) ...
ui: ConnectionControl.OnConnectedToMaster state=16 ...
```

Pressing **Craft** on a clan blueprint should produce this sequence:

```text
clan-craft: craft pressed for '<item id>' (stock section state <unknown>)
clan-craft: '<item id>' is a clan blueprint; answered 'regular craft item' for this press so the detail recipe is used instead of the clan branch
clan-craft: craft recipe check for '<item id>' -> craftable
free-details: craft gate for '<item id>' ... -> allowed
```

If this line appears instead, the handler still refused the item and a further predicate is involved:

```text
clan-craft: craft press still opened the hint window (last section state ...); window suppressed, the craft itself did not start
```

The `clan item during press` and `press scope` fields of the armed line, and the two press lines above, exist only on `armory v6` or newer; the `clan hint window` field only on `armory v5` or newer; the `free-details:` line with `owned count=synthetic` only on `armory v4` or newer. If they are missing, or the build stamp line is absent, the device is running an older `libopg3d.so` and no conclusion about the hooks should be drawn from that log.

AppIDs are logged only as a length and FNV-1a fingerprint.

Caller RVAs can be mapped to managed methods with the matching private `dump.cs` file. Dumps, metadata, and `libil2cpp.so` are analysis inputs and must not be committed:

```bash
python3 tools/symbolize_log.py --dump dump1321.cs --log logcat.txt
```

## Project structure

```text
opg3d/src/main/cpp/
├── main.cpp                  # IL2CPP wait/attach, build stamp and module installation
├── elf_sym.cpp/.h            # in-memory ELF export resolver
├── il2cpp.cpp/.h             # metadata and managed-value helpers
├── hook.cpp/.h               # fail-closed ShadowHook wrapper
├── photon_hooks.cpp/.h       # AppID override and connection tracing
├── cloud_guard.h             # fixed-EU routing and obsolete disconnect guard
├── player_boost.h            # stock level steps (cap 38) and verified bank top-up
├── legacy_gameplay.h         # tutorials and upgrade clock fallback
├── free_detail_weapons.h     # zero-detail weapon crafting
├── clan_craft.h              # clan blueprints without clan membership
├── config.h                  # build-time defaults and build tag; no credentials
└── CMakeLists.txt            # pinned static ShadowHook and libopg3d.so
```

## Verification status

- Target: PG3D 13.2.1, Android ARMv7, IL2CPP metadata v22.
- Zero-detail crafting is confirmed on device for event blueprints with the `armory v4` build.
- The craft-section availability hook is confirmed to take effect on device: the craft hint window changed its own call-to-action, which is only derived from that enum value.
- The `armory v5` build confirmed that the craft press is refused before the availability enum is consulted, which is what the scoped item classification addresses. That scoped workaround itself still needs a device report from a build printing the `armory v6` stamp.
- The clan-storage accessors and the medal price were verified against the supplied 13.2.1 analysis files only.
- Photon handshake, master/game-server transitions, room creation, and a 1v1 match have been verified between two devices on different networks.
- All clients must use EU, with EU configured as the only allowed Photon region.
- The mechanism used to load `libopg3d.so` into the game process is outside this repository.

## License

Project code is licensed under GPLv3; see [LICENSE](LICENSE). ShadowHook is MIT-licensed; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
