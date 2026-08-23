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
