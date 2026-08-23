# Porting currency and level to 23.1.3 (ARM64)

This is the 23.1.3 counterpart of `PORT_16_1_1_PROGRESSION.md`. It documents
how the offline **currency** and **level** modules were re-derived for the
exact supplied 23.1.3 ARM64 build, and why the 16.1.0 module could not simply
be recompiled.

## Scope

| Feature | 16.1.0 | 23.1.3 before this change | 23.1.3 after |
| --- | --- | --- | --- |
| Online (Photon Cloud) | `photon_1610.h` | **already ported** (`photon_2313.h`, `photon_default_plugin_2313.h`, `photon_trace_2313.h`, `backend_local_2313.h`) | unchanged |
| Currency (coins + gems) | `progression_1610.h` | missing | `progression_2313.h` |
| Player level | `progression_1610.h` | missing | `progression_2313.h` |

The online half of the request was already complete on `23.1.3` and is
documented in `PORT_23_1_3_PHOTON.md`; it is not touched here. Only the two
missing modules are added.

The 16.1.0 tutorial/shop-tutorial progression carried by `progression_1610.h`
is deliberately **not** ported. Its keys (`shop_tutorial_state_passed_VER_12_1`)
and its `TrainingController` stage model do not correspond to the 23.1.3
`TrainingController`, which exposes a different API surface
(`get_NeedTraining`, `世上丞丝丛三专丁与`-typed stage accessors). Porting it would
require its own analysis pass and is out of scope for this change.

## Analysis inputs

All mappings below were derived from the artifacts supplied in the analysis
archive, not from guesswork:

| Artifact | SHA-256 |
| --- | --- |
| 23.1.3 `libil2cpp.so` (ELF64 AArch64, Build ID `57fcc18d2db06212416d480d53c0f881ee47c52a`) | `f0a130c4e8d9487059eab4b0f08462f6aa7d057510cada0fd6fb3043c77deb5c` |
| 23.1.3 `global-metadata.dat` | `28b8bddf53a8ebdaf70aec1e672d3bdea6e46ca2b2e478f1b7e66e69884c99dd` |
| 23.1.3 `dump.cs` (1,202,333 lines) | `803371a6246bdeb6f230ea54dbbbf77108ce088cdfbbcd0f6843a45185398830` |
| 16.1.0 `libil2cpp.so` (ELF32 ARM, Build ID `0fd9946b14fe013039ece2af653c5e9dc083b1ed`) | `2aab620cb58a597e86975a78ab20987e71685b507456707ed42fa63fad54032b` |
| 16.1.0 `global-metadata.dat` | `b709931396332f27c79a8ef0e696e66fcd4aefd5d8217dca361741cee404eca6` |
| 16.1.0 `dump.cs` | `6cbaff1fdbb21b1a93fc1c444689f9778a3e0a68b2204aaf9f5b3e59ed05a719` |

None of these files are committed. RVA equals file offset in the 23.1.3 ELF,
so call graphs were recovered by decoding AArch64 `BL`/`B`
(`(word >> 26) == 0x25` / `0x05`, target `= addr + (sext(imm26) << 2)`) across
the whole `.text` and attributing each address to the owning method from the
dump's RVA index (161,400 methods).

## Why `progression_1610.h` could not be reused

1. **ABI.** 16.1.0 is `armeabi-v7a`, 23.1.3 is `arm64-v8a`. As already
   recorded in `PORT_23_1_3_PHOTON.md`, the ARM32 generated static methods
   carried an unused leading static-context argument; the AArch64 method
   pointers take only the explicit managed arguments followed by
   `MethodInfo*`. Every static typedef had to drop that leading argument.
   `progression_2313.h` re-asserts `sizeof(void*) == 8`.
2. **Array layout.** The 16.1.0 module read managed arrays at length `+0x0C`,
   elements `+0x10`. On ARM64 those become `+0x18` / `+0x20`. The new module
   sidesteps the question entirely by never walking the experience table.
3. **The currency architecture no longer exists.** `BankController`,
   `Storager` and `CheaterConfigMemento` are absent from 23.1.3 by name. Coins
   and gems are now owned by a `Progress` wallet model and written through a
   `Progress` service facade. Nothing about the 16.1.0 write path survives.

## Currency mapping

Wallet model class `Progress.丁丞万上专上万丞丂`, service facade class
`Progress.东丝丂丄业丕且丙丑`.

| Role | Signature | RVA |
| --- | --- | --- |
| Wallet holder | `Progress.与丅丟丈丕上东丟丁::static 丁丞万上专上万丞丂 万丒丗丅丆丗丗下三()` | `0x026FCBEC` |
| Coin getter | `Progress.丁丞万上专上万丞丂::int 丄业丛三丒丌专丈世()` | `0x026FA298` |
| Gem getter | `Progress.丁丞万上专上万丞丂::int 丗丛七丝专丄业不丂()` | `0x026FA450` |
| Keyed getter (hooked for key capture) | `Progress.丁丞万上专上万丞丂::int 丐世东丑上丙丗丕丁(string)` | `0x026FA318` |
| Keyed setter | `Progress.丁丞万上专上万丞丂::void 不世下世丙且丅万丙(string,int)` | `0x026FA674` |
| Key normalizer | `Progress.丁丞万上专上万丞丂::static string 丌丝世一七且且丕万(string)` | `0x026F94F0` |
| Service holder | `Progress.东丝丂丄业丕且丙丑::static 东丝丂丄业丕且丙丑 丞丏业丐丒与业丗与()` | `0x01B3BA40` |
| **Add-currency transaction** | `Progress.东丝丂丄业丕且丙丑::void 丄丝丄丙且丝丟上丒(string key, int amount, 丂丝丑下丅丗丌专与 accrual, bool needIndication = true, bool = false)` | **`0x01B44CC0`** |
| Spend/withdraw | `Progress.东丝丂丄业丕且丙丑::bool 丞世丟丆丑专丝丝万(string,int,丟丑下丛丝丁丂丑业)` | `0x01B446A8` |

### Proof that `0x01B44CC0` is the real write path

Its internal call sequence is the complete stock transaction, which is exactly
why the module drives currency through it instead of poking the wallet setter:

```
+0x148 bl 0x026F94F0  丌丝世一七且且丕万(string)                  normalize key
+0x180 bl 0x026FCBEC  万丒丗丅丆丗丗下三()                        wallet
+0x190 bl 0x026FA318  丐世东丑上丙丗丕丁(key)                     read old value
+0x1B8 bl 0x026FA674  不世下世丙且丅万丙(key, newValue)           write new value
+0x224 bl 0x017ABD04  三丄丘丆丝上丙丗与::.ctor(string,int,bool,…)  transaction record
+0x338 bl 0x01B2EC1C  丙东丅丐丝丈丟丁业(…)                        persist / notify
+0x358 bl 0x040F1BDC  PGCompany.Analytics.丘万丞丗世丒丌丑世()
+0x3B0 bl 0x028B2080  PGCompany.Analytics.丑丐与丘丄丆丆丏丏(…)
+0x3EC bl 0x032B2EF8  CoinsMessage::丄丟且丙丒下万丙丕(string,int)   UI toast
+0x4F4 bl 0x04AAE960  System.Exception::.ctor(string)         unknown-key throw
```

It has 23 callers, including `CoinBonus.Update`, `RewardedLikeButton.OnClick`,
`Rilisoft.FortItemEffectFreeCoins`, `Rilisoft.FortItemEffectFreeGems`,
`Rilisoft.LobbyItemEffectFreeGems`, `Rilisoft.LeprechauntManager.DropReward`
and `MainMenuController::七七丟丂下丅丌业一`. The `Exception` at `+0x4F4` is the
reason keys are captured rather than guessed: an unrecognised key throws.

### Key capture instead of hardcoded literals

Both no-argument getters call the keyed getter at a fixed offset
(`0x026FA298+0x54` and `0x026FA450+0x54` both target `0x026FA318`). The module
therefore hooks `丐世东丑上丙丗丕丁(string)`, sets a capture mode, calls the stock
coin getter and then the stock gem getter, and records whichever key the game
itself passes. No currency string literal appears anywhere in the port.

The capture is rejected if either key is empty or if both keys are identical,
so a future build that reshapes the wallet fails closed instead of writing the
same key twice.

Grants pass `needIndication = false` so a synthetic top-up does not spawn the
`CoinsMessage` popup at `+0x3EC`.

## Level mapping

`ExperienceController` exposes five parameterless `int` statics. They were
disambiguated by caller count and caller identity, the same rule the 16.1.0
document used.

| Signature | RVA | Callers | Verdict |
| --- | --- | --- | --- |
| `static int 世丐丙丆业一丄丙丒()` | **`0x01C79A50`** | **411** — `WeaponManager` unlock gates, `RespawnWindow`, `GameConnect`, `GMHMainView` | **current level** |
| `static int 丕三丙上丏与下与丟()` | **`0x01C79AB0`** | 25 — incl. `ExpController::static float 三丆丑七丏一丅丕丝()` (the `ProgressExpInPer` equivalent) and both XP-add routines | **current experience** |
| `static int 三世丒丄丘下丘丝丅()` | `0x01C79B10` | 53 — `GMHSetupOnXpChange`, `SquadController`, `GameModeHub` | rank/tier, not level |
| `static int 三丛丟丙丈丝一三丞()` | `0x01C7A078` | 1 — `ExpController::丟丗丙与丗七不丂丐()` | exp to next level |
| `static int 不世业业上下丁七专()` | `0x01C7C188` | 4 — all `Progress.*::且丄东丘丛丙丅东不()` | serialization only |

The 411-caller getter being consumed by every weapon unlock gate is the
decisive signal: that is the player level. The 25-caller getter feeding
`ProgressExpInPer` is the raw experience, matching the 16.1.0 rule exactly.

**Add-experience** is
`ExperienceController::void 东丙丑万且专丞世丂(int amount, 丄丆三丅七丞丆专上 reason, Dictionary<string,object> payload)`
at **`0x01C7AC28`**. It reads `0x01C79AB0` twice (read, add, level-up check).
Two decoys were ruled out:

- `丄三丐下丕上丒专丝(int)` at `0x01C7AC24` is **four bytes long** and contains a
  single non-branch instruction — a dead stub.
- `一丂与丅丗丈丘丅丈(int)` at `0x01C79CE8` only forwards to
  `Rilisoft.丅丏丏丛丕丁丟上丞` accessors — a field-setter proxy.

The level cap is read from the class itself: `public const int maxLevel = 65`
(it was 45 in 16.1.0). The controller instance comes from the **unobfuscated**
static field `ExperienceController.sharedController`.

Because the stock routine performs its own level-up bookkeeping and fires the
level-up events, the port never has to reproduce the per-level experience
table: it grants a large fixed amount per tick and the game stops itself at
`maxLevel`.

## Save shield

`CheaterConfigMemento` does not exist in 23.1.3. The punishment path is now
concentrated in `CheatDetectedBanner`, and the two routines that matter were
identified from their call graphs:

| Routine | RVA | What it does |
| --- | --- | --- |
| `private static 丏万且丝上丙丐下丗()` | `0x04B40164` | calls `UnityEngine.PlayerPrefs::DeleteAll()` then `Save()`, rewrites the tamper flag through `丐丘丌丞丙丌丗东与::丈丅三丒丄三丘丆与(...)` and starts the wipe coroutine — **this is the save wipe** |
| `internal static 丈且丁丞丛丅丄七上()` | `0x04B400D0` | calls `丟丝专丄丑世丞世丒::丟丙与丙丞七三丙丗()` (PhotonNetwork.Disconnect) and unloads the asset bundle — **forced offline kick** |

`CheatDetectedBanner::Update()` at `0x04B40590` branches straight into the
wipe routine at `+0x64`, which is how a synthetic balance would reach it.

Both statics are replaced with no-ops. Critically, the shield is installed
**before** anything is granted and is treated as mandatory: if either hook
fails, `install()` returns `false` and **no currency or level is ever
granted**. This is the same fail-closed posture the rest of the 23.1.3
bootstrap uses.

## Runtime order and exclusions

`progression_2313::install_hooks()` runs last in `main.cpp`, after
`photon_trace_2313`, so it only ever runs on a runtime that already passed the
root-domain publication gate in `il2cpp_runtime_2313::wait_for_domain()` and
the `Assembly-CSharp.dll` readiness loop.

Grants are driven from `MainMenuController::Update()`, which is inherently
self-gating: it only ticks once the player is in the main menu, on the Unity
main thread, with the wallet and `ExperienceController.sharedController`
already constructed. There is no polling thread and no timing guess.

- warm-up: 60 frames before the first managed call
- level: every 5 frames until `maxLevel`
- currency: every 120 frames (the stock add path also writes the save)

Install order inside the module is deliberate: resolve everything → bind
`sharedController` → **shield** → key-capture hook → main-menu tick. Any
failure at any step aborts before a single value is written.

## Device validation checklist

```
adb logcat -s OPG3D
```

1. `init: libopg3d build 23.1.3 ARM64 lobby gate v4 + progression …`
2. `23.1.3-progression: installed (currency target 999999999, level cap 65)`
3. On reaching the main menu:
   `23.1.3-progression: armed; coin key='…' gem key='…' level=… exp=…`
   — confirm the two captured keys differ and look like real wallet keys.
4. Coins and gems climb to `999999999` without a `CoinsMessage` toast storm.
5. Level climbs to 65 and then stops; the level-up UI fires normally.
6. No `CheatDetectedBanner` wipe: `PlayerPrefs` survives an app restart and
   progress is still present.
7. Online still works — the Photon path is untouched; confirm
   `ConnectedToMaster` still appears from `photon_trace_2313`.

If the metadata ever stops matching, the expected log is
`23.1.3-progression: metadata does not match the expected 23.1.3 build;
nothing was hooked`, and the rest of the bootstrap continues unaffected apart
from the aggregate `progression=0` in the summary line.

Symbolize any crash with:

```
python3 tools/symbolize_log.py --dump <dump>.cs --log logcat.txt
```
