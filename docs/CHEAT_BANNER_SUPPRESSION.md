# CHEAT DETECTED: where it really comes from, and how it is suppressed

Armory v9 intercepted `CheatDetectedBanner` and stopped the wipe body. The
banner still appeared, because the punishment is decided in one place and
delivered from a completely different one. Armory v12 closes both.

Everything below comes from the 13.2.1 client: the IL2CPP dump, the metadata
string table, and two whole-binary scans of every direct `B`/`BL` in the ARM
`.text` section.

## Layer 1 - the verdict

```
internal enum AbuseMetod {
    None = 0, UpgradeFromVulnerableVersion = 1, Coins = 2, Gems = 4,
    Expendables = 8, Weapons = 16, AndroidPackageSignature = 32, health = 64
}

Switcher                                     (internal sealed MonoBehaviour)
    internal const string AbuseMethodKey = "AbuseMethod"
    private static Nullable<AbuseMetod> _abuseMethod
    internal static AbuseMetod get_AbuseMethod()          RVA 0xEE737C
        +0x108  bl  Storager.getInt(...)                  RVA 0xEB6CD0

AdsConfigManager.GetCheatingMethods(AdsConfigMemento)     RVA 0xE50FEC
        +0x78   bl  CheaterConfigMemento.get_CheckSignatureTampering 0x130536C
        +0xBC   bl  Switcher.get_AbuseMethod                        0xEE737C
        +0x12C  bl  Storager.getInt          ; coin balance
        +0x144  bl  CheaterConfigMemento.get_CoinThreshold          0x1305374
        +0x1B0  bl  Storager.getInt          ; gem balance
        +0x1C8  bl  CheaterConfigMemento.get_GemThreshold           0x130537C
    single caller: GetPlayerCategory+0x118

Rilisoft.CheatingMethods {
    None = 0, SignatureTampering = 1, CoinThreshold = 2, GemThreshold = 4
}
```

The thresholds arrive as ordinary configuration (`CheaterDetectParameters`,
fed through `FriendsController.NewCheaterDetectParametersAvailable`) and are
compared against the wallet. On a private server with a granted balance this
is a permanent, self-renewing "cheater" verdict, which is why the banner came
back on its own.

## Layer 2 - the delivery (obfuscated, in the first loading scene)

```
AppsMenu                                     (internal sealed MonoBehaviour)
    public string intendedSignatureHash
    private const string _suffix = "Scene"
    private static string GetAbuseKey_53232de5(uint pad)   RVA 0x1BB3530
    private static string GetAbuseKey_21493d18(uint pad)   RVA 0x1BB3640
    private static string GetTerminalSceneName_4de1(uint)  RVA 0x1BB3750
    private static IEnumerator MeetTheCoroutine(string sceneName,
                            long abuseTicks, long nowTicks) RVA 0x1BB3468

AppsMenu.<MeetTheCoroutine>c__Iterator0.MoveNext()         RVA 0x1BB81E0
        +0x74   bl  SceneManager.LoadScene(string)          RVA 0x1DB0D4C
        +0xFC   bl  TimeSpan.FromTicks(long)
        +0x148  bl  Defs.get_IsDeveloperBuild()
        +0x1C0  bl  Random..ctor(int seed)
        +0x220  bl  WaitForSeconds..ctor(float)
```

A mark carrying ticks is written into an obfuscated `Storager` slot, and a
*later* launch waits a randomised delay before loading the scene that carries
`CheatDetectedBanner`. Nothing in this path contains the word "cheat", the
scene name is built at runtime from a number plus the `"Scene"` suffix, and
sibling slot builders are scattered across unrelated classes:

| Slot builder | Owner | RVA |
| --- | --- | --- |
| `GetAbuseKey_53232de5` | `AppsMenu` | `0x1BB3530` |
| `GetAbuseKey_21493d18` | `AppsMenu` | `0x1BB3640` |
| `GetAbuseKey_d4d3cbab` | `Initializer` | `0xE36C90` |
| `GetAbuseKey_f1a4329e` | `MainMenuController` | `0xF78C70` |

Because the mark is persisted, a wipe that already happened can still fire the
banner once more after the fix is installed - unless the read is neutralised,
which is what the slot redirection below does.

## Layer 3 - the banner (unchanged from v9)

`CheatDetectedBanner.Update()` tail-calls `ClearAllProgress()`
(`PlayerPrefs.DeleteAll` + `Storager` marks + `CloudSyncController.ApplyChanges`
+ the `update_abuse_info` abuse report). `Awake()` tears down the rest of the
scene through `RemoveObjects()`.

## The hook table

| Target | Result | Required |
| --- | --- | --- |
| `Switcher.get_AbuseMethod/0` | returns `AbuseMetod.None` | yes |
| `AdsConfigManager.GetCheatingMethods/1` | returns `CheatingMethods.None` | yes |
| `CheatDetectedBanner.Update/0` | no-op | yes |
| `CheatDetectedBanner.ClearAllProgress/0` | no-op | yes |
| `CheatDetectedBanner.ShowAndClearProgress/0` | no-op | yes |
| `AppsMenu.<MeetTheCoroutine>c__Iterator0.MoveNext/0` | returns `false` | loud if missing |
| `AppsMenu.MeetTheCoroutine/3` | forwarded, logged only | no |
| the four `GetAbuseKey_*` builders | inert key names | no |
| `CheatDetectedBanner.Awake/0` | destroys the banner object | no |
| `CheatDetectedBanner.RemoveObjects/0` | no-op | no |
| `DevToDevFacade.set_UserIsCheater/1` | no-op | no |
| `PremiumAccountController.CheckTimeHack/0` | no-op | no |

The iterator is a compiler-generated nested type, so three metadata spellings
are tried in order: `AppsMenu/<MeetTheCoroutine>c__Iterator0`,
`<MeetTheCoroutine>c__Iterator0`, `AppsMenu.<MeetTheCoroutine>c__Iterator0`.

## Guarantees

* No save writes: no `Storager` or `PlayerPrefs` write, no `CloudSyncController`
  push, no ownership, currency or level change. The persisted `HackDetected`
  and `AbuseMethod` marks are read exactly once, for the log, and left as they
  are.
* The stock abuse slots are neither read nor written once redirected; the
  inert keys (`opg3d_inert_slot_a` .. `_d`) are unknown to the rest of the
  client.
* Detection inputs stay stock: balances, ownership getters, the package
  signature hash and the shop are untouched. Only the verdicts and the
  delivery are removed.
* `TempItemsController.CheckForTimeHack` is a 4-byte empty stub in this build,
  so it is deliberately not hooked.

## What to look for in `adb logcat -s OPG3D`

```
init: libopg3d build 13.2.1 cheat banner suppression (armory v12) built ...
cheat-guard: armed (verdicts=forced clean, banner scene=never loaded,
  abuse slots=inert, wipe path=blocked,
  PlayerPrefs/Storager/CloudSync writes=none, detection inputs=stock)
cheat-guard: the CHEAT DETECTED scene trigger is blocked (<class>.MoveNext)
cheat-guard: persisted marks: 'HackDetected'=<n>, 'AbuseMethod'=<n>
  (read-only; this module never writes save state)
```

Event lines, each capped at eight occurrences:

```
cheat-guard: Switcher.get_AbuseMethod() forced to AbuseMetod.None ...
cheat-guard: AdsConfigManager.GetCheatingMethods() forced to CheatingMethods.None ...
cheat-guard: AppsMenu.MeetTheCoroutine('<scene>', abuseTicks=<n>, nowTicks=<n>)
  was armed by the client; its first tick will be refused
cheat-guard: AppsMenu.<MeetTheCoroutine>c__Iterator0.MoveNext() stopped ...
cheat-guard: AppsMenu.GetAbuseKey_53232de5(pad=<n>) redirected to the inert key ...
cheat-guard: DevToDevFacade.set_UserIsCheater(1) swallowed ...
cheat-guard: PremiumAccountController.CheckTimeHack() refused ...
```

The `MeetTheCoroutine` line is the interesting one: it proves the client tried
to show the banner and names the scene it wanted to load.

## Failure modes

| Log line | Meaning |
| --- | --- |
| `the local punishment path could not be neutralised` | a mandatory hook failed; do not run this build on a real save |
| `the delayed CHEAT DETECTED scene load could not be hooked under any known nested class name` | the overlay can still appear, but it can no longer erase anything |
| `AppsMenu.GetAbuseKey_... could not be redirected` | a stale mark in that slot is still read; the refused tick is the remaining defence |
| `Storager.getInt is unavailable` | the diagnostic mark read was skipped; nothing else changes |
| `optional call <X> is unavailable` | that single side effect stays stock |
