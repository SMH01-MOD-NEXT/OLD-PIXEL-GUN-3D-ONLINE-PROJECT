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
- **Clan blueprints without a clan.** Clan blueprints are refused by a code path that has nothing to do with clans: the craft handler only accepts items present in the ordinary craft-recipe list, and clan recipes are parsed into a separate table. Rather than faking clan state, a refused press is completed by calling the game's own craft start with the same arguments the handler would have used. No `Clan` object is created and no clan data is written or sent anywhere.
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

Zero-detail pricing is not enough for clan blueprints. They correctly display `0 of 0` details and were still refused, and for several revisions the reason was assumed to be clan state. It is not.

### What the device logs disproved

1. **Forcing the availability enum only changed a label.** `ShopCraftManager.GetCraftSectionAvailability()` was reported as `UnavailableNoClan(1) -> Available(3)`, and the only visible effect was the hint window changing its own call-to-action from **Find a Clan** to **Raise the level**. That enum is read by the window, not by the press.
2. **The clan predicates are never called during a press.** The `armory v6` build hooked `ClansController.IsClanItem(string)` and `ShopCraftManager.CraftItemAndNotCrafted(string)` and traced both. Both hooks installed successfully and neither produced a single line during a craft press, while the press itself was still refused.

### What the disassembly proved

The `libil2cpp.so` of this build is ARM (A32) code, not Thumb-2, so the call graph can be resolved by scanning `BL` instructions directly:

```text
ArmoryInfoScreenController.HandleCraftButtonClicked  RVA 0xC92A5C
  +0xDC  -> ShopNGUIController.HandleCraftButton_NoInfo  RVA 0x9F1740   (only caller)
WeaponManager.StartCraftWeaponOrAvatar(string, long)  RVA 0x962FB8
  called once, from HandleCraftButton_NoInfo +0x8FC                     (only caller)
```

The handler refuses the item at `+0x1A0`, long before any clan state is read, by asking a list of ordinary craft recipes whether it contains the pressed id:

```text
0x9F18E0  bl   List<string>.Contains(id)
0x9F18E4  cmp  r0, #1
0x9F18E8  bne  0x9F1A7C          ; returns without ever reaching +0x8FC
```

Clan recipes are parsed separately, by `BalanceController.ParseClanRecipesConfig`, so the ordinary recipe table consulted here has no entry for a clan blueprint. This is why every clan-side override was ineffective: the branch that refuses the press is not a clan check, and no clan answer can influence it. It also explains the log exactly — the availability enum, the item classification and the recipe check are all read after that point, or by the hint window.

### The workaround

When the stock handler refuses a press, the craft is started directly through the game's own methods, reproducing the argument sources of the handler's own success path at `+0x810 .. +0x8FC`:

```csharp
ItemDb.GetByTag(id).PrefabName                       // craft duration is keyed by prefab
BalanceController.GetFullTimeCraftInSeconds(prefab)  // stock duration, not zero
FriendsController.ServerTime + seconds               // endTime
WeaponManager.StartCraftWeaponOrAvatar(id, endTime)  // craft is keyed by item tag
```

The craft timer, persistence, the finish handler, and the reward path all stay inside the game's own code, and the caller refreshes the armory itself by calling `ArmoryInfoScreenController.SetItem(...)` right after the handler returns, so no UI is driven from the module.

Three guards keep the forced start honest:

1. It runs only for the outermost craft press, and only when the stock handler actually refused it — observed as the hint window being raised inside the press scope, which is the refusal signature of this branch. A press the handler completes on its own is never touched.
2. It runs only when the craft slot is empty. The handler's own "already crafting" check sits at `+0x494`, far behind the recipe-list exit, so a clan blueprint pressed while another weapon is crafting would never reach it. `WeaponManager.CurrentCraftingWeaponOrAvatar` is therefore read first, and a busy slot declines the press instead of overwriting a craft that is already running.
3. The slot is verified after the call, so a failed start is reported as an error instead of being logged as success.

The clan-side answers that remain are the ones that keep the armory screen itself coherent, because they read state that no longer exists:

- `ShopCraftManager.GetCraftSectionAvailability()` returns `Available` (mandatory hook).
- `ShopNGUIController.IsCraftSectionAvailable()` returns `true` (optional).
- `BalanceController.MedalsForClanCraft(string)` returns `0`: clan medals were farmed server-side and can no longer be earned.
- `ClansController.AnyPartExistsInStock(...)` and `ClansController.GetPartCountInStock(...)` report a small synthetic amount only when clan storage answers "empty".

The disproved `ClansController.IsClanItem(string)` override and the `CraftItemAndNotCrafted(string)` trace have been removed rather than left in place, so the module no longer touches the item classification at all.

**No clan is fabricated.** A synthetic clan would have to satisfy every other consumer of that state — clan screens, chests, seasons, siege matchmaking, forts, analytics payloads — and any of those reading a half-initialised `Clan` object is exactly how a dead-backend client crashes. This module therefore never constructs a `Clan`, never assigns `ClansController.myClan`, never rewrites the `Clan.MyClanCache` entry on disk, and never calls a retired clan endpoint (`SendUpdateStock`, `AskCraftFortItem`, `AddClanCurrency`, `SendClanMessageDetailsBought`).

### The craft hint window

The "Crafting Process" window (`CraftSectionClanInfoController`, fields `_availability` and `buttonLabels`) is constructed from the availability enum and selects its header and call-to-action from it. Both clan states describe requirements this client can never satisfy, and the button of either one routes into the retired clan search.

`ShopNGUIController.ShowCraftSectionInfo()` is therefore suppressed in two cases: when the stock reason is a clan reason (`UnavailableNoClan` or `UnavailableClansNotOpened`), and whenever the window would be raised during a craft press. In the second case the window is also the signal that the press was refused, which is what triggers the direct craft start. When the section is genuinely about missing details outside a press, the window is forwarded unchanged and stays useful.

One consequence is documented on purpose: if a craft is already running and a clan blueprint is pressed, the window stays suppressed and the press is declined with a log line, because this branch never reaches the stock "already crafting" handling that would have offered the speed-up window.

## Technical design

1. **Safe early initialization.** A native constructor starts a detached thread, waits for `libil2cpp.so`, waits for the assembly list to stabilize, and attaches to the IL2CPP runtime before touching metadata.
2. **Symbol resolution without `dlsym`.** Android linker namespaces and `RTLD_LOCAL` hide the game's exports from `dlsym(RTLD_DEFAULT)`. `elf_sym` reads the already-mapped ELF dynamic symbol table directly.
3. **Metadata-driven, fail-closed hooks.** Targets are found with the IL2CPP metadata API and hooked at their actual `MethodInfo::methodPointer` using ShadowHook in UNIQUE mode. No managed method RVA is compiled into the project; RVAs appear only in comments and documentation, as evidence for a decision. If a required class, method, or trampoline is unavailable, the module reports failure instead of patching an assumed address.
4. **Read-only predicates over synthetic state.** Where the client blocks an action because a retired service can no longer answer, the missing answer is supplied at the exact predicate that asks for it. State that other systems would read — clan membership, server responses, currency ledgers — is not fabricated.
5. **Narrowest possible scope.** When a predicate has consumers beyond the blocked action, its answer is relaxed only for the duration of that action instead of globally, so unrelated screens keep observing the real state.
6. **Evidence before overrides.** A hypothesis about why the client refuses something is confirmed by a device log or by disassembly of the actual handler before it is shipped, and an override that the logs disprove is removed instead of being left in place "just in case".
7. **Honest UI.** When a predicate is answered, the screens explaining that requirement are corrected as well, so the game never instructs the player to satisfy a condition that no longer exists or cannot be reached.
8. **Stock state transitions.** Tutorial completion, level advancement, bank writes, zero-detail crafting, item grants, upgrades, and saves go through original game methods. Where a refused action has to be completed, the module calls the same managed method the handler would have called, with the arguments taken from the same sources, rather than writing game state itself.
9. **Verifiable builds.** `OPG3D_BUILD_TAG` in `config.h` plus the compiler timestamp are logged before any hook is installed, so a report can always be tied to a specific library.
10. **ARM32 ABI correctness.** This IL2CPP build gives static generated methods a hidden `null` context in `r0`; managed arguments begin in `r1`, followed by `MethodInfo*`. Instance methods take the object in `r0` instead. A `long` return such as server time uses the `r0:r1` pair, a 64-bit argument occupies an aligned register pair, and a struct return such as `KeyValuePair<string, long>` is written through a hidden result pointer in `r0`, which pushes the context to `r1`. Every hook and stock-call signature models the layout of its own call site explicitly.
11. **Unwinder compatibility.** The native library is compiled with `-fno-exceptions -funwind-tables`. This lets managed exceptions unwind through hook frames without mixing the game's GNU-compatible ARM EHABI context with the statically linked LLVM unwinder.
12. **Single native artifact.** ShadowHook v2.0.1 is built from source, patched for this ARMv7 environment, and linked statically into `libopg3d.so`.

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
init: libopg3d build 13.2.1 clan-craft workaround (armory v7) built ...
init: phase 0 ready — Photon Cloud routing, progression grant, tutorial skip, free detail weapons, clan-free blueprint crafting and upgrade timers active
legacy: tutorial skipped automatically; stage 3 and shop tutorial completion saved
free-details: armed (required=0 for every recipe, craft gate=forced, owned count=synthetic); no category filtering
free-details: required details for '<item id>': 25 -> 0
clan-craft: armed (section=Available, shop shortcut=forced, medals=free, clan storage=synthetic, clan hint window=suppressed, press scope=on, direct craft start=on, busy-slot guard=on); no clan object is created
boost: persisted grant armed (trigger=MainMenuController.Update, level target=38, currency target=999999999, level-up UI=skipped)
cloud-force[...]: ... host=1(PhotonCloud expected=1) region=0(eu expected=0) ... ready=1
photon-status: 1024 (Connect) ...
ui: ConnectionControl.OnConnectedToMaster state=16 ...
```

Pressing **Craft** on a clan blueprint should produce this sequence:

```text
clan-craft: craft pressed for '<item id>' (stock section state ...)
clan-craft: stock handler refused the press and raised its hint window (last section state ...); window suppressed, starting the craft directly
clan-craft: stock handler refused '<item id>' because the ordinary craft recipe list has no entry for a clan blueprint; craft started through WeaponManager (prefab '<prefab>', <n> s, ends at <unix seconds>, slot now '<item id>')
```

The last line is the one that matters: it means the craft is running. If it is missing, the log states why instead — the item has no `ItemDb` record, `WeaponManager` did not accept the craft, the stock craft methods could not be resolved at startup, or another weapon is still being crafted:

```text
clan-craft: '<item id>' was not started because '<other item>' is still being crafted; ...
```

The `direct craft start` and `busy-slot guard` fields of the armed line, and the press lines above, exist only on `armory v7` or newer. The `clan hint window` field exists only on `armory v5` or newer, and the `free-details:` line with `owned count=synthetic` only on `armory v4` or newer. If they are missing, or the build stamp line is absent, the device is running an older `libopg3d.so` and no conclusion about the hooks should be drawn from that log.

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
- The craft-section availability hook is confirmed to take effect on device, but it was also confirmed to be insufficient: it only changes the hint window's own call-to-action.
- The `armory v6` build proved on device that `ClansController.IsClanItem` and `ShopCraftManager.CraftItemAndNotCrafted` are not called during a craft press, which is why both overrides were removed.
- The refusal itself was located by disassembling `ShopNGUIController.HandleCraftButton_NoInfo` (`+0x1A0`, ordinary craft-recipe list lookup) and confirming that `WeaponManager.StartCraftWeaponOrAvatar` has exactly one caller, `+0x8FC` of the same handler.
- The direct craft start of `armory v7` is derived from that handler's own success path and still needs a device report from a build printing the `armory v7` stamp.
- The clan-storage accessors and the medal price were verified against the supplied 13.2.1 analysis files only.
- Photon handshake, master/game-server transitions, room creation, and a 1v1 match have been verified between two devices on different networks.
- All clients must use EU, with EU configured as the only allowed Photon region.
- The mechanism used to load `libopg3d.so` into the game process is outside this repository.

## License

Project code is licensed under GPLv3; see [LICENSE](LICENSE). ShadowHook is MIT-licensed; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
