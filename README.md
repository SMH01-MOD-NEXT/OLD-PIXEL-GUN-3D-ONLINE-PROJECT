# Old Pixel Gun 3D Online Project

Open-source (GPLv3) restoration of online multiplayer for **Pixel Gun 3D 13.2.1** on Android (`armeabi-v7a`). The original backend for this legacy client no longer exists, so the project supplies a compatibility layer and routes multiplayer exclusively through a fan-run **Photon Cloud** application.

The project does not connect to Cubic Games services and is not affiliated with Cubic Games or Photon. It is a compatibility project for a discontinued 2017 client, not a cheat or a bypass of active server-side checks.

## Features

`libopg3d.so` is loaded into the game process and installs metadata-resolved IL2CPP hooks for the parts of the old client that depended on retired services:

- **Photon AppID redirect.** The mod replaces the AppID at the source of the game's selection path, before `ServerSettings.UseCloud(...)` consumes it. The AppID comes from a build secret and is never committed or printed in clear text.
- **Fixed EU routing.** Every connection uses Photon Cloud region `eu`. This removes the first-launch `32756 / Region none is not available` race and keeps every player in one regional room pool. EU must also remain the only allowed region in the Photon dashboard.
- **Dead-backend disconnect guard.** The obsolete `FriendsController.Update` disconnect path is quarantined only while Photon is connecting or connected. Normal PUN callbacks, room transitions, RPC, synchronization, and intentional disconnects remain stock.
- **Persistent release progression.** The game repeatedly runs its own `ExperienceController.AddExperience` path until the final level, **38**, is reached. The experience table is indexed from level 0, so its length of 39 is not a level; every computed target is clamped to 38. Repeated level-up popups are suppressed only during automatic steps. Coins and Gems are maintained at **999,999,999** through the stock bank and Storager paths, so the values persist.
- **The whole lobby craft catalogue on the account.** Lobby items — bases, gates, fences, terrain, roads, decor, backgrounds, effects, devices, the pet kennel and the paid bundles — are a progression of their own, normally bought with coins, gems or real money, or unlocked by in-match lockers that a private server can no longer make meaningful. Every item that exists in the build is granted for real, through the client's own `AddItemNow` and its own save, so each one becomes a genuine ownership record under the `lobby_items` key instead of an answer faked by a getter. It survives a restart, and prices, craft timers, lockers, item buffs and the cloud merge stay stock.
- **Automatic tutorial skip.** The initial training stage is completed before scene routing can send a fresh profile into training. Both the first-match stage and the 12.1+ shop-tutorial flag are written through the game's own persistence APIs, so the skip survives a restart.
- **Free detail weapons.** Every craft recipe reports **0 required details** and the craft gate is answered positively, so the stock craft flow grants the weapon immediately. These weapons already have no craft wait in this client. There is no category filtering.
- **Clan blueprints without a clan.** Clan blueprints are refused by a code path that has nothing to do with clans: the craft handler only accepts items present in the ordinary craft-recipe list, and clan recipes are parsed into a separate table. Rather than faking clan state, a refused press is completed by calling the game's own craft start with the same arguments the handler would have used. No `Clan` object is created and no clan data is written or sent anywhere.
- **Retired arsenal weapons back in the shop.** The weapons removed from the arsenal by the 2015-06-15 content update are still complete in the client and are only kept off the shelves by one name list. That list is bypassed for the duration of a single shelf decision, per record, so retired weapons are shown and bought through the normal shop flow at the price the client itself computes. Ownership, `CanBuy`, prices, and the tier, level, filter-map and campaign gates are untouched, and the list keeps its real contents for the multiplayer code that substitutes retired weapons on other players' models.
- **Local cheat-detection punishment blocked.** Detaching the client from the dead backend does not disarm `CheatDetectedBanner`. Its frame tick still runs `PlayerPrefs.DeleteAll()`, rewrites the `HackDetected` Storager marks, pushes the emptied state through `CloudSyncController.ApplyChanges` and posts an abuse report, and its show path additionally disconnects Photon and loads a full-screen banner scene. All three entry points are neutralised at the banner itself, and a banner shown by any other route removes itself instead of tearing down the rest of the scene. Detection inputs, ownership, currency, Storager and every save path stay stock; the module never writes save state.
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

## Retired arsenal weapons in the shop

The weapons pulled from the arsenal by the 2015-06-15 content update never left the client. `ItemDb` still holds their records, their prefabs, icons and upgrade chains ship with the APK, and the game still knows how to equip and render them. On a private server there is no reason to keep them off the shelves.

### Why `CanBuy` is not used

`ItemRecord.get_CanBuy()` (RVA `0x535CF4`) has no backing field in this build — it is computed — and it is read by ownership and pricing paths rather than by the shelf builder: `ItemDb.GetCanBuyWeapon()` (`0x5320A0`), `ItemDb.GetCanBuyWeaponTags()` (`0x532264`), `ItemDb.GetCanBuyWearTags(...)` (`0x5359D8`), `RespawnWindowItemToBuy.IsCanBuy()`. Forcing it — even only for retired items — would change what the client treats as a purchasable item and can present an owned weapon as unowned. It is therefore never touched.

### What actually hides them

The filter is one name list, not a per-item flag:

```text
WeaponManager._Removed150615_PrefabNAmes            static HashSet<string>, +0x130
  filled once by InitializeRemoved150615Weapons()    RVA 0x98AB94
    +0x90 -> BalanceController.RemovedWeaponNames()  RVA 0xC9E2D0
  exposed by  get_Removed150615_PrefabNames()        RVA 0x98ACC0
    +0xB4 -> InitializeRemoved150615Weapons()        ; lazy rebuild
```

and the shelf is refused inside the per-record shop builder:

```text
WeaponManager._AddWeaponToShopListsIfNeeded(ItemRecord)  RVA 0x98ADE4
  +0x474  bl  WeaponManager.get_Removed150615_PrefabNames()
          bl  ItemRecord.get_PrefabName()
              HashSet<string>.Contains(prefab)
          b   +0x71C        ; returns; the record is never appended
```

That method is called only from `_InitShopCategoryLists` (`+0x9C0`, `+0x1090`, `+0x123C`), which the shop rebuild drives. Nothing on the path reads or writes ownership, currency or saves: a filtered record is simply absent from a category list.

Because the getter rebuilds the list lazily, nulling the static field does not disable the filter — the next call restores it from the balance config.

### Why the list is not emptied

The same list has fifteen readers, and three of them must keep seeing its real contents, because they substitute a retired weapon on another player's model instead of hiding anything:

```text
CharacterInterface.SetWeapon(...)                           +0x664
CharacterView.SetWeaponAndSkin(..., replaceRemovedWeapons)  +0x948
Player_move_c.<SetWeaponRPC>c__Iterator2.MoveNext()         +0x138
```

So the module never clears, replaces or nulls the list. For the single record the shelf builder is currently deciding about, that record's prefab name is unlisted right before the stock method runs and put back immediately after it returns, and the restore is verified with the game's own `Contains`. `Contains`, `Remove` and `Add` are resolved from the class of the live `HashSet<string>` instance, so no container layout is assumed. Anything unexpected forwards the call unchanged, which keeps the stock result — the weapon stays hidden — instead of guessing.

### What stays stock

- **Prices.** `ItemDb.GetPriceByShopId` (`0x531448`) consults `BalanceController.pricesFromServer` first and falls back to the local `ItemDb` records, so a re-listed weapon is bought through the normal shop flow at the price the client computes for it. No price is fabricated.
- **Every other shelf condition** of the same method: campaign-only records, filter maps, the try-gun check, the player tier, and the event/craft tag set with its level requirement. An unhidden weapon appears where the client itself would have put it.
- **Ownership and saves.** Purchases, grants and persistence go through the untouched stock shop flow.
- **Armor and hats.** Retired wear is a different mechanism (a compensation path around `MoneyGivenRemovedArmorHat`, not this name list), so it is out of scope for this module.

## Blocking the local CHEAT DETECTED wipe

The dead backend cannot punish anyone any more, but the APK still ships a purely local punishment. It is a single MonoBehaviour, and it erases the save on the device.

### The punishment path

```text
CheatDetectedBanner : MonoBehaviour
  const string HackDetectedKey = "HackDetected"

  Update()                       RVA 0x12CB624
    +0xA0  b   ClearAllProgress()                     ; tail call

  ClearAllProgress()             RVA 0x12CAF4C
    +0x98  bl  Storager.getString(string)             RVA 0xEBD8D8
    +0xEC  bl  PlayerPrefs.DeleteAll()                RVA 0x1DA5640
    +0xF8  bl  PlayerPrefs.Save()                     RVA 0x1DA56D0
    +0x15C bl  Storager.setInt(string,int,...)        RVA 0xEB74A0
    +0x1B4 bl  Storager.setString(string,string)      RVA 0xEBD760
    +0x248 bl  CloudSyncController.ApplyChanges(bool) RVA 0x132A5FC
    +0x274 bl  SendCheatTypeOnServer()                RVA 0x12CB238

  ShowAndClearProgress()         RVA 0x12CAE84
    +0x80  bl  PhotonNetwork.Disconnect()             RVA 0x6E6344
    +0xA4  b   SceneManager.LoadScene(string)         RVA 0x1DB0D4C

  Awake()                        RVA 0x12CB2C4
    +0x50  bl  RemoveObjects()                        RVA 0x12CB448
    +0x90  bl  ConnectScene.MainLoadingTexture()      RVA 0x112E58C

  RemoveObjects()                RVA 0x12CB448
           bl  Object.Destroy(Object)                 RVA 0x1D9F274
           ; walks Transform.root of everything it finds and destroys it

  OnExitButtonClick()            RVA 0x12CB6EC  ->  Application.Quit()
  SendCheatTypeOnServer()        RVA 0x12CB238  ->  WWWForm abuse report
```

So the full-screen banner is not the damage: the damage is `PlayerPrefs.DeleteAll()` plus the rewritten `HackDetected` marks, and the emptied state is then pushed through the cloud-sync path so a later sync cannot bring the save back.

### What the whole-binary scans proved

Two scans of every direct `B`/`BL` in `.text` pin the graph down:

- `PlayerPrefs.DeleteAll` has exactly **six** callers in the entire client, and only one is cheat-driven: `ClearAllProgress +0xEC`. The rest are ordinary wrappers (`KeychainCleaner.Clear`, `P31Prefs.removeAll`, `Save.DeleteAll`, `CryptoPlayerPrefs.DeleteAll`, `CustomHungerBase`).
- `ClearAllProgress` has exactly **one** caller: `Update() +0xA0`.
- `SendCheatTypeOnServer` has exactly **one** caller: `ClearAllProgress +0x274`.
- `ShowAndClearProgress` has **no** direct caller in this build; it is reached indirectly, so it is neutralised as insurance rather than as the fix.

That is why the block is installed at the banner, three methods deep: nothing else in the client loses a path it legitimately uses.

### Why the detector itself is left alone

The cheat criteria are server-driven data, not code that can be safely inverted:

```text
Rilisoft.CheatingMethods { None=0, SignatureTampering=1, CoinThreshold=2, GemThreshold=4 }
Rilisoft.CheaterConfigMemento { CheckSignatureTampering, CoinThreshold, GemThreshold }
AdsConfigManager.GetCheatingMethods(AdsConfigMemento)   RVA 0xE50FEC
  single caller: GetPlayerCategory +0x118
FriendsController.NewCheaterDetectParametersAvailable   Action<int,int,int,int>
  fed by the cached "CheaterDetectParameters" config
```

The coin and gem thresholds are exactly what a private server's granted balance trips, and the methods that read that balance are ordinary getters shared with the shop, the bank and the HUD. Forcing them would corrupt real game state. The module therefore lets detection think whatever it likes and removes only its ability to act.

### What the module does

Mandatory, all on `CheatDetectedBanner`:

1. `Update()` is never forwarded, so the tail call into the wipe cannot happen.
2. `ClearAllProgress()` is refused, as defence in depth for any indirect route.
3. `ShowAndClearProgress()` is refused, so the Photon disconnect and the banner scene load are skipped as well.

Optional, cosmetic and scene safety, so a failure here still leaves the save protected:

4. `Awake()` is intercepted. Instead of running the stock body — which wires the overlay and calls `RemoveObjects()` — the banner destroys its own `GameObject` through `Component.get_gameObject` and `UnityEngine.Object.Destroy`, and play continues.
5. `RemoveObjects()` is refused, so a banner shown by any other route cannot destroy unrelated scene objects.

### What stays stock

- **No save writes.** The module does not clear the persisted `HackDetected` mark and never calls `Storager`, `PlayerPrefs` or `CloudSyncController`. The mark is read once, read-only, so a device report can show whether an earlier wipe already left it set.
- **Detection inputs.** Currency, level, ownership, signature and config readers are untouched.
- **The abuse report** is simply never started, because its only caller is the refused wipe.

## The whole lobby craft catalogue on the account

Lobby crafting is a third progression, unrelated to weapons and armour. It builds the player's own lobby out of items grouped by `LobbyItemGroupType` (`Buildings`, `Gate`, `Fance`, `Terrain`, `Road`, `Decor`, `BigDecor`, `Backgrounds`, `Effects`, `PetKennel`, `Devices`, `Bundles`) and placed in the slots of `LobbyItemInfo.LobbyItemSlot` (`Base`, `wall`, `gate`, `kennel`, `terrain`, `road`, `decor_small`, `decor_big`, `background_1`, `background_2`, `device_1`, `device_2`, `skybox`). Some items are sold in paid bundles (`bundle_my`, `bundle_winter`), and others are gated behind lockers that only a live population can produce:

```text
LobbyItemInfo.LobbyItemLockerType { None, KillMobs, KillPlayersInDuels,
                                    KillPlayersWithGadget, KillPlayersWithPet,
                                    GainLikes }
```

On a private server, `GainLikes` and duel lockers are unreachable by design, and a bundle can no longer be bought at all.

### Ownership is a record, not a flag

The build ships the entire catalogue: `LobbyItemsInfo.info` describes every item that exists, and `LobbyItemsController._allItems` holds one `LobbyItem` per description. Ownership is a separate per-item object, `LobbyItemPlayerInfo` (`InfoId`, `CraftStarted`, `IsCrafted`, `IsEquiped`, `EquipTime`, `Lockers`), and the client has exactly one method that creates it:

```text
LobbyItemsController.AddItemNow(LobbyItem)             RVA 0x13E8534
  +0x7C   bl  LobbyItem.get_IsExists()                 ; refuses duplicates
          bl  Object..ctor + LobbyItem.set_PlayerInfo(new LobbyItemPlayerInfo)
          bl  LobbyItemInfo.get_Id()                   ; InfoId of the record
          bl  FriendsController.get_ServerTime()       ; craft marked finished
          bl  LobbyItem.get_CraftTime()
  +0x434  bl  LobbyItem.get_IsBundle() / get_Bundle() / Bundle.get_Items()
  +0x9D0  bl  LobbyItemsController.Equip(item, silent) ; bundle path only
  +0x9D8  bl  LobbyItemsController.SavePlayerCurrentData()

LobbyItemsController.SavePlayerCurrentData()           RVA 0x13E81E0
  +0x2B8  bl  LobbyItemPlayerInfoSerializedObject..ctor()
  +0x2D4  b   SaveLobbyItemsPlayerData(serialized)     RVA 0x13E0BB4
            bl  JsonUtility.ToJson(obj)                RVA 0x1AB7278
            b   Storager.setString("lobby_items", json) RVA 0xEBD760
```

A scan of every branch inside `AddItemNow` finds **no** `BankController` call, **no** `ItemPrice` read and no purchase path at all: it is the client's own unconditional grant, and it saves the result itself. Bundles are expanded in the same call, so a bundle row grants its contents through the same code the game would have run after a real purchase.

### The loop is the game's own

The client already uses that method exactly the way this module does — for the starter items every account receives:

```text
LobbyItemsController.GetFreeItemsIfNotExists()         RVA 0x13EC858
  +0x168  bl  LobbyItem.get_IsExists()
  +0x17C  bl  LobbyItemsController.AddItemNow(item)
```

So the grant is not an override of an ownership getter. `LobbyItem.get_IsExists()` is only **read**, to decide what is still missing, and the item is then handed to the client's own grant. The account really owns every item afterwards: a real `LobbyItemPlayerInfo` record exists for it, serialised by `JsonUtility` into the `lobby_items` Storager entry, so it survives a restart and is visible to every screen, effect handler and cloud applyer that reads that save.

### How it runs

The module lives in `player_boost.h` (namespace `player_boost::lobby`) because it is part of the same progression grant, and it is driven from `LobbyItemsController.Update`:

1. Nothing happens until `LobbyItemsController.IsReady` is true, so `InitItems()` and `ReadPlayerData()` finish their own setup first.
2. `AllItems` is walked in small batches — at most three grants per frame — because every grant rewrites the whole lobby save through `JsonUtility.ToJson`. `List<LobbyItem>` is read with `get_Count`/`get_Item` resolved from the class of the live list, so no container layout is assumed.
3. Each granted item is named in the log, up to a cap, and a batch that granted anything calls the game's own `SavePlayerCurrentData()` once more, so a partially walked catalogue is still persisted.
4. A pass that granted something is followed by one more verification pass. When a pass grants nothing, the catalogue is complete and the walk stops; it is re-verified occasionally afterwards, because a cloud merge (`LobbyItemsCloudApplyer.MergeLobbyItems`) can reintroduce missing records.
5. If the client refuses a grant, the item is skipped and logged, and a long run of refusals disables the module instead of hammering the save.

### What stays stock

- **Prices, craft timers and speed-ups.** `LobbyItemInfo.PriceBuy`, `PriceSpeedUp`, `PriceInstant` and `CraftTime` are never touched; the grant simply does not go through a purchase.
- **Lockers.** `LobbyItemLocker` progress, `LockPointsLeft` and the like/kill counters are left alone. The item is owned, but no locker counter is falsified.
- **Equipping and appearance.** Plain items are granted without being equipped, exactly as the stock method does — only its bundle path equips. Item buffs and effects are applied by the client's own `AddEffects`/`GetEffect` code when it decides to.
- **The save format and the cloud merge.** Only the game's own serializer writes `lobby_items`, and `LobbyItemsCloudApplyer` keeps merging as it always did.

## Technical design

1. **Safe early initialization.** A native constructor starts a detached thread, waits for `libil2cpp.so`, waits for the assembly list to stabilize, and attaches to the IL2CPP runtime before touching metadata.
2. **Symbol resolution without `dlsym`.** Android linker namespaces and `RTLD_LOCAL` hide the game's exports from `dlsym(RTLD_DEFAULT)`. `elf_sym` reads the already-mapped ELF dynamic symbol table directly.
3. **Metadata-driven, fail-closed hooks.** Targets are found with the IL2CPP metadata API and hooked at their actual `MethodInfo::methodPointer` using ShadowHook in UNIQUE mode. No managed method RVA is compiled into the project; RVAs appear only in comments and documentation, as evidence for a decision. If a required class, method, or trampoline is unavailable, the module reports failure instead of patching an assumed address.
4. **Read-only predicates over synthetic state.** Where the client blocks an action because a retired service can no longer answer, the missing answer is supplied at the exact predicate that asks for it. State that other systems would read — clan membership, server responses, currency ledgers, ownership — is not fabricated.
5. **Ownership is granted, never simulated.** When the goal is that the account *has* something, the client's own grant and its own save are called, so the result is a real record other systems can read. An ownership getter is only ever read to find out what is missing; it is never answered on the game's behalf.
6. **Narrowest possible scope.** When a predicate or a collection has consumers beyond the blocked action, its answer is relaxed only for the duration of that action instead of globally, so unrelated screens and the multiplayer code keep observing the real state.
7. **Punishment is disarmed, detection is not.** Where the client reacts to a state a private server creates on purpose, the reaction is intercepted at its own entry points and the criteria that produced it are left untouched, because those criteria read ordinary game state that other systems share.
8. **Evidence before overrides.** A hypothesis about why the client refuses something is confirmed by a device log or by disassembly of the actual handler before it is shipped, and an override that the logs disprove is removed instead of being left in place "just in case".
9. **Honest UI.** When a predicate is answered, the screens explaining that requirement are corrected as well, so the game never instructs the player to satisfy a condition that no longer exists or cannot be reached.
10. **Stock state transitions.** Tutorial completion, level advancement, bank writes, zero-detail crafting, item grants, purchases, upgrades, and saves go through original game methods. Where a refused action has to be completed, the module calls the same managed method the handler would have called, with the arguments taken from the same sources, rather than writing game state itself.
11. **Bounded work per frame.** Grants that rewrite a save file are spread over several frames in small batches, so a full catalogue can be handed out without a visible hitch and without a save write storm.
12. **Verifiable builds.** `OPG3D_BUILD_TAG` in `config.h` plus the compiler timestamp are logged before any hook is installed, so a report can always be tied to a specific library.
13. **ARM32 ABI correctness.** This IL2CPP build gives static generated methods a hidden `null` context in `r0`; managed arguments begin in `r1`, followed by `MethodInfo*`. Instance methods take the object in `r0` instead. A `long` return such as server time uses the `r0:r1` pair, a 64-bit argument occupies an aligned register pair, and a struct return such as `KeyValuePair<string, long>` is written through a hidden result pointer in `r0`, which pushes the context to `r1`. Every hook and stock-call signature models the layout of its own call site explicitly.
14. **Unwinder compatibility.** The native library is compiled with `-fno-exceptions -funwind-tables`. This lets managed exceptions unwind through hook frames without mixing the game's GNU-compatible ARM EHABI context with the statically linked LLVM unwinder.
15. **Single runtime native library.** ShadowHook v2.0.1 is built from source, patched for this ARMv7 environment, and linked statically into `libopg3d.so`.

## Building

The Photon AppID is a credential and must not be stored in the repository.

### GitHub Actions

Create the repository Actions secret `PHOTON_APP_ID`. The workflow exposes it as `ORG_GRADLE_PROJECT_PHOTON_APP_ID`, builds the selected maintained branch, and publishes the stripped runtime library plus its unstripped symbol copy in one ARMv7 artifact:

- `13.2.1` — PG3D 13.2.1, including the release progression and legacy gameplay compatibility described above.
- `12.5.0` — the older PG3D 12.5.0 target.

When starting the workflow manually, select the branch explicitly: the default branch is `12.5.0` and does not contain the 13.2.1 compatibility modules. Always confirm the build stamp of the artifact you install.

### Local build

Requirements: JDK 17, Android SDK, NDK `29.0.14206865`, and CMake `4.4.3`.

```bash
gradle :opg3d:assembleRelease \
  -PPHOTON_APP_ID="xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
```

This branch remains strictly `armeabi-v7a`, matching the original 16.1.0
release. A successful build exports the installable stripped library and its
symbol-rich unstripped copy, using the same layout as 23.1.3:

- `lib/armeabi-v7a/libopg3d.so`;
- `lib/armeabi-v7a/libopg3d.unstripped.so`.

`gradle :opg3d:exportReleaseLibrary` can also build and export both standalone
files directly.

A build without the AppID is allowed for diagnostics, but the AppID hook runs in passthrough mode and logs a warning.

Optional experimental Gradle properties are `PHOTON_MODE=cloud` (default) or `selfhosted`, `PHOTON_SERVER_ADDRESS`, and `PHOTON_SERVER_PORT` (default `5055`).

## Diagnostics

```bash
adb logcat -s OPG3D
```

Relevant healthy-startup lines include:

```text
init: libopg3d build 13.2.1 lobby craft grant (armory v10) built ...
init: phase 0 ready — Photon Cloud routing, progression grant, tutorial skip, free detail weapons, clan-free blueprint crafting, retired arsenal in the shop, the whole lobby craft catalogue on the account, upgrade timers and the local cheat-detection progress-wipe block active
legacy: tutorial skipped automatically; stage 3 and shop tutorial completion saved
free-details: armed (required=0 for every recipe, craft gate=forced, owned count=synthetic); no category filtering
free-details: required details for '<item id>': 25 -> 0
clan-craft: armed (section=Available, shop shortcut=forced, medals=free, clan storage=synthetic, clan hint window=suppressed, press scope=on, direct craft start=on, busy-slot guard=on); no clan object is created
removed-arsenal: armed (scope=shop shelf builder only, list=never cleared, CanBuy=untouched, prices=stock, tier/level/filter gates=stock)
cheat-guard: armed (scope=CheatDetectedBanner only, PlayerPrefs/Storager/CloudSync writes=none, detection inputs=stock, persisted mark=read-only)
boost: persisted grant armed (trigger=MainMenuController.Update, level target=38, currency target=999999999, level-up UI=skipped)
boost: lobby craft grant armed (source=LobbyItemsController.AllItems, grant=AddItemNow + SavePlayerCurrentData, batch=3 per frame, ownership getters=untouched, prices/lockers/effects=stock)
cloud-force[...]: ... host=1(PhotonCloud expected=1) region=0(eu expected=0) ... ready=1
photon-status: 1024 (Connect) ...
ui: ConnectionControl.OnConnectedToMaster state=16 ...
```

Entering the lobby for the first time on a new save should hand out the catalogue and then go quiet:

```text
boost: lobby item '<item id>' granted through AddItemNow and saved (<n> so far)
boost: lobby pass 1 granted <n> item(s) out of <total> in the catalogue; verifying once more
boost: lobby catalogue complete — <total> item(s) exist in this build, <n> granted and saved by this module (Storager key 'lobby_items')
```

The `complete` line is the one that matters: `<total>` is how many lobby items this build contains, and `<n>` is how many the account did not have. If items are refused instead, the log names them:

```text
boost: the client refused to grant lobby item '<item id>'; it stays unowned
```

If the client decides to punish the session, the block is visible instead of a wiped save:

```text
cheat-guard: CheatDetectedBanner.Awake() intercepted; the stock body would tear down the rest of the scene and arm the wipe tick
cheat-guard: persisted 'HackDetected' mark reads <n> (read-only; this module never writes save state)
cheat-guard: banner object destroyed on Awake; the session continues untouched
cheat-guard: CheatDetectedBanner.Update() suppressed; its tail call into ClearAllProgress (PlayerPrefs.DeleteAll + Storager marks + cloud push + abuse report) never runs
cheat-guard: CheatDetectedBanner.ClearAllProgress() refused; local progress, Storager marks and CloudSyncController are left exactly as they were
```

Any of those lines means the detection fired and nothing was erased. `ShowAndClearProgress() refused` in the same block additionally means the client tried to disconnect Photon and load the banner scene.

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

Opening the shop after a rebuild names every retired weapon that reached the shelves, up to a logging cap:

```text
removed-arsenal: retired-name list is live; shop shelf filter is now bypassed per record
removed-arsenal: retired weapon '<item id>' (prefab '<prefab>') offered to the shop at its stock price; shelf decisions so far: <n>
```

The `boost: lobby` lines exist only on `armory v10` or newer. The `cheat-guard:` lines exist only on `armory v9` or newer. The `removed-arsenal:` lines exist only on `armory v8` or newer. The `direct craft start` and `busy-slot guard` fields of the clan-craft armed line, and the press lines above, exist only on `armory v7` or newer. The `clan hint window` field exists only on `armory v5` or newer, and the `free-details:` line with `owned count=synthetic` only on `armory v4` or newer. If they are missing, or the build stamp line is absent, the device is running an older `libopg3d.so` and no conclusion about the hooks should be drawn from that log.

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
├── player_boost.h            # stock level steps (cap 38), verified bank top-up,
│                             # and the whole lobby craft catalogue granted
├── legacy_gameplay.h         # tutorials and upgrade clock fallback
├── free_detail_weapons.h     # zero-detail weapon crafting
├── clan_craft.h              # clan blueprints without clan membership
├── removed_arsenal.h         # retired weapons on the shop shelves
├── cheat_guard.h             # local CHEAT DETECTED progress wipe blocked
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
- The retired-weapon shop filter of `armory v8` is **confirmed on device**: retired weapons appear in the shop, show a price and can be bought.
- The local wipe block of `armory v9` is provisionally confirmed on device: the "CHEAT DETECTED" banner that prompted it has not reappeared in play. A logcat capture showing the `cheat-guard:` lines and the value of the persisted `HackDetected` mark is still useful but no longer blocking.
- The lobby catalogue grant of `armory v10` was derived from disassembly: `AddItemNow` (RVA `0x13E8534`) contains no purchase call and ends in `SavePlayerCurrentData` (`0x13E81E0`), which serialises the whole item list into the `lobby_items` Storager entry (`0x13E0BB4` -> `JsonUtility.ToJson` -> `Storager.setString`), and the client's own `GetFreeItemsIfNotExists` (`0x13EC858`) already uses the same `get_IsExists` -> `AddItemNow` loop. It still needs a device report from a build printing the `armory v10` stamp, in particular the `lobby catalogue complete` line with its item counts.
- The clan-storage accessors and the medal price were verified against the supplied 13.2.1 analysis files only.
- Photon handshake, master/game-server transitions, room creation, and a 1v1 match have been verified between two devices on different networks.
- All clients must use EU, with EU configured as the only allowed Photon region.
- The mechanism used to load `libopg3d.so` into the game process is outside this repository.

## License

Project code is licensed under GPLv3; see [LICENSE](LICENSE). ShadowHook is MIT-licensed; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
