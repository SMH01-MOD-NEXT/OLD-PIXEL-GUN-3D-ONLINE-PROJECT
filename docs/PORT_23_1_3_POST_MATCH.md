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

## Revision 3: the freeze is located and repaired

Revision 2 answered its question. The driver half **does** run; it parks.

| device log (2026-08-24, +ms) | event |
| --- | --- |
| 262574 | `results wrapper B`, payload `exp=20 coins=4 gems=0 clan=0 winnerCommand=1 amIWinner=true firstPlace=true showAward=true` |
| 262574 | `results coroutine state -> 0` (first step, run inline by `StartCoroutine`) |
| 262582 | `panel switch a=true b=false` |
| 262583 | `results coroutine state 0 -> 1` — **the last transition, ever** |
| 265011 | `OnTablesShow` |
| 265036 | `OnMatchEndAnimationDone` |
| 265157 | `OnTablesShown` |
| 265163 | `OnTrophyAnimationDone`, then `reward-delay state -> 0` and `0 -> 1` — **also the last transition of that routine** |
| 325553 | `OnRewardShow` (60 s later, animation event) |
| 327480 | `OnRewardAnimationEnds` |

### What state 1 waits for

`世丟丈丄丙丒专丘丂.MoveNext` (0x4E61894) at the panel-switch call site:

```text
4e62f3c  bl   0x484dd44            三丕丟丅丐丕丆丘万(this, true, false, null)
4e62f44  strb w8,  [x19, #0x5b0]   controller.与东丞丙丏丝丗与业 = true
4e62f4c  ldrb w8,  [x19, #0x5b0]   while (that flag)
4e62f54  str  xzr, [x20, #0x18]!     <>2__current = null
4e62f68  stur w0,  [x20, #-0x8]      <>1__state  = 1      -> yield return null
4e62f6c  b    0x4e64300              return true
```

So state 1 is `while (flag@0x5B0) yield return null;`. A full scan of the image
for `STRB`/`LDRB` with immediate `0x5B0` (the same BL/B decoding technique as
`tools/find_callers.py`) gives every party interested in that flag:

| site | method |
| --- | --- |
| `STRB 0x0484FDFC` | `OnTablesShow (+0x0)` — writes `wzr`, i.e. clears it |
| `STRB 0x04E61DBC` | results `MoveNext (+0x528)` — an earlier wait on the same flag |
| `STRB 0x04E62F44` | results `MoveNext (+0x16B0)` — the wait above |
| `LDRB 0x04E61DD0` / `0x04E62F4C` | the two `while` checks |
| `LDRB 0x04E67B98` | `丁丛不丘不世丟不丗.MoveNext (+0x328)` |

`OnTablesShow` fires at +265011, i.e. the wait condition **was** satisfied 2.4 s
after the park, and the coroutine still never advanced. A `yield return null`
that is not resumed after its condition clears means nobody steps the routine
any more.

### Root cause

Two independent iterators of the same MonoBehaviour — the results coroutine and
the reward-delay coroutine — each ran exactly one step and then went silent.
That is the Unity signature of a host GameObject that is not active:
`StartCoroutine` runs the first `MoveNext` inline and only afterwards refuses to
schedule the routine. Everything that keeps firing is an animation event, which
Unity delivers to the component regardless of the object's state (the scene also
has `NetworkStartTableCupAnimationEventsHandler`, which forwards events to this
controller through a serialized reference), so the animation half looks healthy
while the data half is frozen.

The lone caption is explained by the same call: `三丕丟丅丐丕丆丘万(true, false)`
(0x484DD44) is not a passive panel toggle. It activates `finishedInterface`
(field 0x68), plays `winSound` (0x78, chosen because the payload has
`firstPlace=true`), bulk-toggles three panel arrays (0x370/0x378/0x380) and
writes the localized caption into `finishedInterfaceLabels` (0x88). Everything
else the player expects — player table, reward window, trophy, OK button — lives
in the coroutine states after that yield, and never runs.

The retired backend is **not** involved: the payload is complete before the
first step, and no network call sits between the park and the freeze.

### The fix: `post_match_2313.h`

The module is a repair, not a trace. Seven hooks, all resolved by metadata name:

- the five nested iterators of the controller (results plus the four reward-queue
  routines) — every step is observed and each live iterator is kept alive with
  `il2cpp_gchandle_new`, so Unity dropping the routine cannot leave a dangling
  pointer;
- `NetworkStartTable.Update` — the heartbeat that keeps ticking even while the
  controller's object is switched off;
- `NetworkStartTableNGUIController.Update` — the heartbeat whose `self` is a
  provably live controller.

On every heartbeat each live routine is checked. A routine that has not been
stepped for `kOrphanMs` (500 ms) while parked at a yield is treated as
abandoned, and then, in order:

1. **Repair.** The inactive GameObject chain that hosts the controller is
   switched back on (own object first, then up to eight ancestors, each logged by
   name), and the very same iterator is handed back to Unity through
   `MonoBehaviour.StartCoroutineManaged2` — the unique internal entry that
   `StartCoroutine(IEnumerator)` itself calls, chosen because the public overload
   set is ambiguous by name and argument count. The stock coroutine then finishes
   the screen with stock timing. Up to `kMaxRepairs` (3) attempts, spaced by
   `kRestartGraceMs` (1.5 s).
2. **Guaranteed exit.** If the results routine still has not completed
   `kGiveUpMs` (12 s) after the freeze was first seen, the module presses the
   screen's own `HandleContinue_GoToLobbyButton` (0x4846310) once, so the player
   always reaches the next screen instead of a dead end.

Safety rules that the module never breaks: no reward is granted, no label is
written, no payload field is modified; every managed object is tested with
`UnityEngine.Object.op_Implicit` before it is touched, so a destroyed controller
is dropped instead of dereferenced; and if the screen closes by itself the slot
is released with `controller is gone; the screen was closed elsewhere`.

### Reading the next log

```text
23.1.3-post-match: repair armed (iterators=5/5 table-tick=OK controller-tick=OK ...)
23.1.3-post-match: results coroutine started (pinned=1)
23.1.3-post-match: results coroutine was abandoned by Unity at state 1 (host active=0, attempt 1)
23.1.3-post-match: re-activating host object '...'
23.1.3-post-match: results handed back to Unity (re-activated=1 accepted=1)
23.1.3-post-match: results coroutine is being stepped again from state 1
23.1.3-post-match: results coroutine finished normally
```

- `host active=0` confirms the root cause above, and the `re-activating ...`
  lines name the object that the panel switch had switched off.
- `host active=1` instead would mean the routine was cancelled rather than
  orphaned; the hand-back covers that case too.
- `accepted=0` means Unity refused the hand-back; the guaranteed exit then
  closes the screen after 12 s with `pressing its own Continue button`.
- If a future build wants the screen skipped unconditionally, drop `kGiveUpMs`
  to zero: the repair is then bypassed and the Continue button is pressed as
  soon as the freeze is seen.

`post_match_trace_2313.h` is kept in the tree for future mapping work but is no
longer installed from `main.cpp`: it hooks the same iterators, and shadowhook
allows one hook per target.

## Revision 4: clickable but invisible result HUD

The 2026-08-24 device report refined the remaining failure: the OK control can
still be clicked at its normal coordinates, but the HUD, rewards, and button
art are not drawn. Static A64 analysis also shows that the stock panel switch
already activates `finishedInterface`. The defect is therefore the NGUI
presentation state, not a missing GameObject or collider.

The repair keeps the stock transition and adds a 30-second visibility watchdog,
armed by the results iterator and refreshed after `OnTablesShown`,
`OnRewardShow`, and `OnRewardAnimationEnds`. It walks only currently active
result roots through the exact 23.1.3
`GameObject.GetComponentsInChildren(Type, bool)` RVA and:

- re-enables disabled active `UIPanel`/`UIWidget` components;
- changes only effectively invisible alpha values (`<= 0.01`) back to `1`;
- leaves inactive win/defeat/draw, ad, and other mutually exclusive branches
  under stock control;
- stops when the stock Continue/OK handler runs, or after 30 seconds.

Bounded `presentation watchdog` markers report active roots and the number of
panels/widgets actually restored. No reward, match payload, or label value is
fabricated.
