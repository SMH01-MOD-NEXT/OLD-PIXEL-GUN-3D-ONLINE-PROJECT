# 23.1.3 — in-match micro-freezes and the dead end-of-match screen

Two defects reported on the private 23.1.3 build:

1. Micro-freezes during play, most noticeable in combat.
2. After a match only the "your team won" caption appears — no buttons, no
   rewards.

Both were assumed to be "the game is trying to sync with the retired backend".
That assumption is correct for (1) and only partly relevant for (2). The two
problems have different causes and are handled by two different modules.

## Rejected approach: porting the 14.1.1 module

`opg3d/src/main/cpp/post_match_ui.h` implements a post-match fix for **14.1.1**
(`namespace post_match_ui`, log tag `post-match-14.1.1`). It is not included by
`main.cpp`, so it has never been part of the 23.1.3 build.

It is also not portable. None of its target identifiers exist anywhere in
`dump2313.cs`:

| 14.1.1 identifier | occurrences in `dump2313.cs` |
| --- | --- |
| `HideAvardPanel` | 0 |
| `ShowEndInterface` | 0 |
| `AnimationEventShowRewardsFinished` | 0 |
| `isCancelHideAvardPanel` | 0 |
| `isRewardsShowing` | 0 |
| `ShowRewardsCoroutine` | 0 |
| `OnRewardFirstWindowShow` | 0 |

The two builds are roughly seven years apart and the post-match UI was rewritten
in between. The 23.1.3 work was therefore started from scratch against
`dump2313.cs` and `libil2cpp.so`.

## Micro-freezes: blocking DNS on the Unity game thread

### Rejected hypothesis: synchronous .NET HTTP

The first theory was a synchronous HTTP request stalling frames. It is wrong,
and the whole-image call graph proves it: both blocking entry points have **zero**
call sites in this build.

| Blocking API | RVA | call sites |
| --- | --- | --- |
| `HttpWebRequest.GetResponse` | `0x15ADB84` | 0 |
| `WebRequest.GetResponse` | `0x1521FC8` | 0 |

### Actual cause

`tools/find_callers.py` decodes every A64 `BL`/`B` in `libil2cpp.so` and
recovers the exact managed call graph (there are no relocations for internal
managed calls, so this is the only reliable cross-reference). `tools/resolve_rva.py`
names the results using the same dump index as `tools/symbolize_log.py`.

The chain that stalls frames:

```
PhotonHandler.<obfuscated>.MoveNext        (coroutine -> Unity game thread)
  0x4464810, 0x4464A38
    -> <wss-resolver>.<iterator>.MoveNext            0x44743EC
         -> <wss-resolver>.<resolve>(string)          0x4473CCC
              -> System.Net.Dns.GetHostAddresses      0x159FB44   (blocking)
```

The declaring class (dump line 70006) holds `private const string = "wss://"`,
so it resolves the address of the retired WebSocket backend. The caller is a
**coroutine**, which in Unity runs on the game thread, so every DNS timeout
against the dead host is a dropped frame. It is worst in combat because that is
when `PhotonHandler` is busiest.

Other blocking network entry points exist but are not the cause of the in-match
freezes and were left alone:

| Blocking call | Reached from |
| --- | --- |
| `Dns.GetHostEntry` `0x159F878` | `ExitGames.Client.Photon.IPhotonSocket.GetIpAddress` |
| `Dns.GetHostAddresses` | `System.Net.Sockets.Socket.Connect`, `TcpClient.Connect` |
| `TcpClient.Connect` `0x3EDF1D8` | `BestHTTP.HTTPConnection.Connect` `0x267C8A0` |

`BestHTTP` runs its connections on its own worker threads, so it does not drop
game frames; hooking it would add risk without fixing the reported symptom.

### Fix: `net_stall_guard_2313.h`

The resolver is memoized rather than disabled:

- The first lookup for a host is delegated to the stock implementation and the
  outcome is recorded **verbatim**, including the `null` case.
- Every later lookup for that host replays that identical observed result
  without entering the resolver.
- Name resolution is *not* disabled: Photon Cloud is live in this port and must
  keep resolving normally. Only the repetition is removed.

So one unavoidable stall replaces an unbounded series of them. The memo table is
bounded at 32 hosts. Managed string allocation happens outside the native lock,
because `il2cpp_string_new` can trigger a GC.

Rejected alternative: returning the input host, or a canned address, without
ever calling the resolver. That invents a result the runtime never produced and
would break Photon Cloud name resolution, which currently works.

## Dead end-of-match screen: why this ships as a trace first

The 23.1.3 flow lives in `NetworkStartTableNGUIController` (dump line 50731,
`TypeDefIndex: 1175`, 169 methods). Every node is declared `public void X()` —
instance, no arguments, void return.

Expected sequence, with the RVAs kept for review only:

| Step | Method | RVA |
| --- | --- | --- |
| 1 | `OnMatchEndAnimationDone` | `0x484FDA4` |
| 2 | `ShowStartInterface` | `0x484E65C` |
| 3 | `OnTablesShow` | `0x484FDFC` |
| 4 | `OnTablesShown` | `0x485247C` |
| 5 | `OnRewardShow` | `0x484FF90` |
| 6 | `CanShowNextReward` | `0x4850138` |
| 7 | `OnRewardAnimationEnds` | `0x4856704` |
| 8 | `StartTrophyAnim` | `0x4850400` |
| 9 | `OnTrophyAnimationDone` | `0x484FB3C` |
| 10 | `OnHideTrophy` | `0x484FE04` |
| 11 | `OnTrophyOkButtonPress` | `0x4856790` |
| 12 | `OnCWViewShow` | `0x484FEA8` |
| 13 | `HandleContinue_GoToLobbyButton` | `0x4846310` |

Disassembly shows the chain is driven by `UnityEngine.Animator.SetBool`
(`0x25D7940`), `RewardWindowController` (`0x3C86964`) and
`MonoBehaviour.StartCoroutine` (`0x4436094`).

**The stalling link cannot be identified statically.** `OnTablesShown`,
`OnRewardShow` and `OnRewardAnimationEnds` have **zero** call sites in the image,
because they are Unity Animation Events invoked by name from the animation
clips. Nothing in the binary records which clip fires which event, so any patch
written now would be a guess — exactly the mistake that the 14.1.1 port already
represented.

Therefore `post_match_trace_2313.h` is passive: it wraps all 13 nodes, logs
entry, exit and the `finishedInterface` flag, and **always delegates**. No
reward, button or callback is fabricated or suppressed. The device log then
names the last step that ran, and the real fix (`post_match_2313.h`) is written
against that evidence.

## Validation

Local verification is impossible in this environment: there is no Android
SDK/NDK, so only CI can build the native library. Push the branch and take the
artifact from the workflow.

Unfiltered capture (the trace lines are `OPG3D`-tagged but the surrounding Unity
lines matter here):

```
adb logcat -c; adb logcat --pid=$(adb shell pidof com.pixel.gun3d) > pg3d.log
```

Symbolize any unexpected caller addresses:

```
python3 tools/symbolize_log.py --dump analys2313/dump2313.cs --log pg3d.log
```

### Expected lines

On startup:

```
23.1.3-net-stall: blocking backend name lookup is memoized; ...
23.1.3-post-match-trace: installed 13/13 hooks (match-end=OK tables-shown=OK)
```

First backend lookup, then no further stalls:

```
23.1.3-net-stall: blocking backend name lookup of '<host>' froze the calling thread for <N> ms (result=<null>); memoized, later lookups will not block
23.1.3-net-stall: replayed memoized lookup of '<host>' (1 blocking DNS calls avoided so far)
```

After a match, the last `->` line without a matching `<-` line, or the step
after which the sequence stops, is the broken link:

```
23.1.3-post-match: -> OnMatchEndAnimationDone hit=1 self=0x... finishedInterface=false
23.1.3-post-match: <- OnMatchEndAnimationDone done finishedInterface=false
23.1.3-post-match: -> OnTablesShow hit=1 ...
```

### Checklist

- [ ] CI build of `:opg3d:assembleRelease` succeeds.
- [ ] `23.1.3-post-match-trace: installed 13/13 hooks` appears.
- [ ] Micro-freezes during combat are gone; at most one
      `froze the calling thread` line appears per host per process.
- [ ] One match played to the end; the post-match trace is captured.
- [ ] The last reached step is identified, and `post_match_2313.h` is written
      against it.

## Revision 2: what the first end-of-match capture proved

The revision-1 trace installed 13/13 hooks and a full online match was played to
the end. The end screen showed the "victory" caption with no player table, no
buttons and no rewards; it was not frozen, and after 10-20 seconds it closed by
itself and let the player continue. The captured nodes:

| Time | Node | Fired |
| --- | --- | --- |
| +27.2 s | `ShowStartInterface` | yes (pre-match panel) |
| +270.2 s | `OnTablesShow` | yes |
| +270.2 s | `OnMatchEndAnimationDone` | yes |
| +270.3 s | `OnTablesShown` | yes |
| +270.3 s | `OnTrophyAnimationDone` | yes |
| +330.8 s | `OnRewardShow` | yes |
| +332.7 s | `OnRewardAnimationEnds` | yes |
| - | `CanShowNextReward` | never |
| - | `StartTrophyAnim` | never |
| - | `OnHideTrophy` | never |
| - | `OnTrophyOkButtonPress` | never |
| - | `OnCWViewShow` | never |
| - | `HandleContinue_GoToLobbyButton` | never |

`finishedInterface` stayed `false` for every single node, entry and exit.

### The split is exactly the call-graph split

`tools/find_callers.py` on all thirteen nodes explains the table without any
guesswork. Every node that fired has **zero** call sites in `libil2cpp.so`,
meaning it is invoked by name from a Unity animation event. Every node that
never fired is either a button handler or is called from inside a coroutine:

```
CanShowNextReward  0x4850138  3 call sites, all reward-queue MoveNext bodies
OnCWViewShow       0x484FEA8  1 call site, results coroutine MoveNext +0x1FF0
panel switch       0x484DD44  1 call site, results coroutine MoveNext +0x16A8
```

So the **animation timeline of the end screen runs to completion while the
data-driven half never runs at all**. `OnTrophyAnimationDone` firing with no
preceding `StartTrophyAnim` is the same fact seen from the other side: the clip
reported "animation finished" for a trophy that was never set up.

This also rules out the theory that the screen is waiting on a network reply and
that the 10-20 second self-close is a timeout: nothing in the data half was ever
entered, so there is nothing there to time out. The self-close is the animation
timeline reaching its own end.

### The data-driven half

One coroutine owns everything that is missing:
`NetworkStartTableNGUIController.一丗丈丞丑丐丗三丆` at RVA `0x484F130`, iterator type
`世丟丈丄丙丒专丘丂`, `MoveNext` at `0x4E61894`. It takes the whole match result as
about thirty arguments (added experience, coins, gems, rating change, winner,
clan currency, tournament and pixel-pass values, VIP rewards) and it is the code
that fills the labels (`UILabel.set_text`), switches the end-of-match panels,
drives `RewardWindowController` / `RewardWindowView` and calls
`TrophyMagicAnimation.SetValues`, `DuelController.PayBetReward`,
`RatingSystem`, `TournamentController` and `ClansController`.

Its three call sites:

```
0x0475CF08  NetworkStartTable.丝丕世丂世丑东丌下        +0x744   <- the real entry
0x0484F114  NetworkStartTableNGUIController.丏丟一丒世东丄下丈   +0x80
0x0484F874  NetworkStartTableNGUIController.丅丆丟丙七丐丒七与   +0x2E0
```

### What revision 2 adds

Revision 1 instrumented the animation side, which turned out to be the healthy
side. Revision 2 keeps those thirteen nodes and adds nine hooks on the driver
side, so the next log localises the break instead of only proving it exists.
All signatures are small and verified against the dump, and the 30-argument
coroutine factory is deliberately **not** hooked: a trampoline call with that
many stack arguments is not worth the risk when hooking its iterator gives the
same information.

| Hook | Signature | Question it answers |
| --- | --- | --- |
| `NetworkStartTable.丝丕世丂世丑东丌下` `0x475C7C4` | `(int, int[])` | does the result payload reach the UI at all |
| `controller.丏丟一丒世东丄下丈` `0x484F094` | `(string, ratingChange)` | wrapper entry taken instead |
| `controller.丅丆丟丙七丐丒七与` `0x484F594` | `(object, object)` | wrapper entry taken instead |
| `controller.三丕丟丅丐丕丆丘万` `0x484DD44` | `(bool, bool)` | did the coroutine reach +0x16A8 |
| results iterator `MoveNext` `0x4E61894` | `()` | which state it stops at, plus the payload |
| four reward-queue iterators `MoveNext` | `()` | which queue owes `CanShowNextReward` |

The results iterator hook also reports the payload once, because the generated
iterator fields are **not** obfuscated: `_addExpierence`, `_addCoin`,
`_addGems`, `_addClanCurrency`, `_winnerCommand`, `amIWinner`, `firstPlace`,
`showAward`. That distinguishes "the UI was asked to show nothing" from "the UI
was asked to show something and failed".

Coroutine hooks are stepped every frame, so they log only on state transitions
and on completion.

The install gate is now `match-end && tables-shown && results-entry &&
results-coroutine && panel-switch`. A silent partial install is what wasted the
previous bot-trace run, so a missing driver hook must fail loudly.

### Reading the next log

- No `results entry` and no `results wrapper` line at match end: the payload
  never arrives. The retired backend owes it, and the fix belongs in
  `backend_local_2313.h`, not in the UI.
- `results entry` fires but no `results coroutine state` line: the entry bails
  out before `StartCoroutine`. Disassemble `0x475C7C4` and find the guard.
- `results coroutine state -> N` appears and then stops at some `N` with no
  `panel switch` line: that state's yield never completes. The state number
  points at the exact `yield` in `MoveNext` to disassemble.
- `panel switch` fires but the screen is still empty: the panels are switched
  and the failure is further down, in `RewardWindowView` / label population.
- `reward-queue * state` lines without `CanShowNextReward`: the queue runs but
  its exit condition is never met.

### Not the cause

- Not the DNS stall: `net_stall_guard_2313.h` is armed and there are no
  repeated in-match resolver stalls in either capture.
- Not a frozen main thread: unrelated per-frame lines
  (`FriendsController.Update`, `ConnectionLostChecker.Update`) keep ticking
  across the whole end-of-match window, so the game loop is alive.
- Not `finishedInterface`: it is `false` in the broken capture and was `true` in
  an earlier pre-match-only capture, so it tracks which panel is up rather than
  gating the reward flow.
