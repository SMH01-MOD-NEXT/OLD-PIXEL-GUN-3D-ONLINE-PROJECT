# PG3D 16.1.1 local currency, level and tutorial port

This change ports only three progression features from the working 14.1.1 implementation to the supplied obfuscated 16.1.0 ARMv7 client used by branch `16.1.1-test`:

- coins and gems are topped up to `999,999,999`;
- the player advances through the stock experience controller to level `45`;
- shooting-range, shop and first-match tutorial progression is persisted as complete.

Crafts, lobby catalogue ownership, weapons, armour, item prices, crafting time and server-time fallbacks are intentionally outside this change.

## Analysis inputs

- `libil2cpp.so` SHA-256: `2aab620cb58a597e86975a78ab20987e71685b507456707ed42fa63fad54032b`
- `global-metadata.dat` SHA-256: `b709931396332f27c79a8ef0e696e66fcd4aefd5d8217dca361741cee404eca6`
- 16.1.0 `dump.cs` SHA-256: `6cbaff1fdbb21b1a93fc1c444689f9778a3e0a68b2204aaf9f5b3e59ed05a719`
- comparison 14.1.1 `dump1411.cs` SHA-256: `484a6041d330b587aaed702232c58423be59d495557ba84fdef64bf6a8ca24d1`

The binaries and dumps are analysis inputs and are not committed.

## Currency mapping

16.1.0 moved currency state behind the backend-first `Progress.Currency` model. Writing guessed `Storager` keys would bypass that live model and can be overwritten by the next profile serialization, so the port follows the stock call path instead.

Verified mappings:

- `BankController.AddCoins(int,bool,AccrualType)` → `丒与丒业不丆丆丐业/3`, RVA `0x02BE22E8`
- `BankController.AddGems(int,bool,AccrualType)` → `与丙万不丂丌丗业万/3`, RVA `0x02BE2520`
- generic currency dispatcher → `丟专丕与丗上世东专/4`, RVA `0x02BE1FC0`
- `Progress.Currency` class → `Progress.丁丌专丕且一丏与丌`
- live wallet getter → `不丂万丌丘丘世丛丛(string)/1`, RVA `0x00DBBF4C`
- live wallet setter → `丟丞三丘丂业丄不丌(string,int)/2`, RVA `0x00DBC1A4`

The `AddCoins` / `AddGems` order is not inferred from names. `ExperienceController`'s stock level-reward routine at RVA `0x02A1CE34` reads the gem reward table and calls `0x02BE2520`, then reads the coin reward table and calls `0x02BE22E8`.

At runtime the module:

1. calls the appropriate stock `BankController` method with a zero delta;
2. observes the synchronous `Progress.Currency` getter call to capture the exact canonical key and live wallet object;
3. reads the current value through that original getter;
4. computes only the missing delta to `999,999,999`;
5. calls `BankController` again with that delta and verifies the resulting value through the live model.

No `Coins` / `Gems` key is guessed, no currency field is patched, and the stock event, analytics and profile-save path remains responsible for persistence.

## Level mapping

Verified mappings:

- current player level → `ExperienceController.丝丞与东丏丂下丏丄/0`, RVA `0x02A15A98`
- current experience → `ExperienceController.丁三东丁业上丕丙丌/0`, RVA `0x02A18514`
- adjusted level helper → `ExperienceController.丌丏不上七丁丌丈丞/0`, RVA `0x02A16D34`
- `AddExperience(int)` → `ExperienceController.丅东丟丌七丙丝七丁/1`, RVA `0x02A1D47C`
- UI progress subscriber → `ExpController.专东丒丁下不世世丂/1`, RVA `0x02A191D0`
- `ExperienceController.sharedController` → static field offset `0x40`
- max-experience table → `与丗丟丑丈丈上且世`, static field offset `0x20`
- health-by-level safety table → `三且业丗上丄丙丕丑`, static field offset `0x28`

The distinction between level and experience getters is proven by their callers: `ExpController.ProgressExpInPer` and `ExpToString` call RVA `0x02A18514`, while balance requirements, level rewards and `AddExperience` use RVA `0x02A15A98` as the player level.

The class constant is `maxLevel = 45`. A32 decoding of `ExperienceController..cctor` shows a 46-entry experience table (`mov r1, #0x2e`), matching levels `0..45`.

As in 14.1.1, one `AddExperience` call can consume at most one player level. The module therefore advances one level every five main-menu frames. It uses the stock experience table to calculate a sufficient grant, invokes the original controller, and verifies that the level advanced. The stock code still awards level-up coins/gems, updates dependent systems and saves the result.

Only the `ExpController` UI subscriber is suppressed inside the thread-local synthetic transaction. This prevents 44 stacked modal/coroutine animations while leaving all stock progression work active. Ordinary experience changes use the original UI listener.

## Tutorial mapping

Verified mappings:

- `TrainingController.get_TrainingCompleted()` → `丄丕东丆丌丞丒丁丄/0`, RVA `0x02D14A98`
- completed-stage getter → `丛业且丕上丛与丏丗/0`, RVA `0x02D14D34`
- completed-stage setter → `丁丘丈丛世丅丟丘丙/1`, RVA `0x02D14FD0`
- obfuscated `Storager` class → `丌丑丌丒丝万丏丘丄`
- `Storager.setInt(string,int,bool,bool)` → `七丕专丂丒丅丛丛丏/4`, RVA `0x0095EECC`

On the first stock completion query, the port records stage `3` (`FirstMatchCompleted`) through the original stage setter and writes the existing shop-tutorial key `shop_tutorial_state_passed_VER_12_1 = 1` through the original `Storager.setInt`. The getter then reports completion. A thread-local recursion guard prevents the stage setter from re-entering the persistence transaction.

The 14.1.1 broken-`HintBig` workaround and server-time fallback are not ported: the 16.1.0 assets and current scope do not require them.

## Required local-save shield

The client still contains the legacy high-balance verdict:

- verdict method `Rilisoft.丛万丗丘一下丏下业.与下丐与世丈东丅丏`, RVA `0x00ACCEFC`
- `CheaterConfigMemento.get_CheckSignatureTampering`, RVA `0x00CD9CF4`
- `CheaterConfigMemento.get_CoinThreshold`, RVA `0x00CD9D04`
- `CheaterConfigMemento.get_GemThreshold`, RVA `0x00CD9D0C`
- `Switcher` abuse getter `一东与丕且丆丕丆丒`, RVA `0x028E14E8`

The verdict calls those three memento getters at offsets `+0x80`, `+0x144` and `+0x1CC`. A `999,999,999` wallet can exceed the downloaded thresholds. The progression module therefore fails closed unless it can first:

- answer the local abuse verdict with `None`;
- disable the duplicate signature verdict for the re-signed local APK;
- raise both balance thresholds to `int.MaxValue`.

The client also retains the destructive banner route:

- show-and-clear entry → `CheatDetectedBanner.与丛丏且丟丈丂三丒`, RVA `0x02A78158`
- clear-all entry → `CheatDetectedBanner.丘且业丘丑下丅丟丅`, RVA `0x02A781F4`
- `CheatDetectedBanner.Awake`, RVA `0x02A78514`
- `CheatDetectedBanner.Update`, RVA `0x02A78854`

The clear-all body calls `PlayerPrefs.DeleteAll` and saves immediately; `Update` tail-calls that body. All four entry points are refused before any progression grant is enabled. This shield does not write, delete or rename any save key. If any required shield hook is unavailable, currency and level grants do not run.

## Runtime order and exclusions

`progression_1610.h` is installed after the local backend and Photon hooks. Its main-menu trigger does nothing until `backend_local_1610::runtime_ready()` confirms the stock `FullySynchronized` / post-load `Empty` transition. This avoids racing profile initialization.

Explicitly unchanged:

- lobby crafts and catalogue;
- weapons, armour, skins and ownership getters;
- craft timers and server time;
- prices and purchase checks;
- Photon matchmaking, RPCs and battle state;
- backend-session emulation from the preceding port.

## Device validation

Build and install the ARMv7 artifact, then capture:

```bash
adb logcat -s OPG3D
```

Expected lines include:

```text
init: libopg3d build 16.1.1 Local progression v1 ...
16.1.x-progression: local save shield armed ...
16.1.x-progression: armed (coins/gems=999999999, level=45, training=skipped ...)
16.1.x-progression: tutorial skipped automatically; stage 3 and shop tutorial completion saved
16.1.x-progression: final level 45 reached and saved
16.1.x-progression: coins ... -> 999999999 through BankController
16.1.x-progression: gems ... -> 999999999 through BankController
```

Validate all of the following:

1. no shooting-range, shop or first-match tutorial starts;
2. level reaches `45` without stacked level-up panels or a frozen menu;
3. coins and gems reach `999,999,999` without becoming negative;
4. restart preserves level, wallet and tutorial completion;
5. spending currency is topped back up in the main menu;
6. no `CHEAT DETECTED` scene, save wipe or forced exit occurs;
7. HUD, movement, firing, weapon switching, loadout and Photon online play from the backend-session port still work;
8. crafts and catalogue ownership remain unchanged.
