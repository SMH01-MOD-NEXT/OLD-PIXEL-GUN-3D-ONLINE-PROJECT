# 23.1.3 auth scene: why the boot parked at 90%

Capture: `logcat_2026-08-22_19-55-26.txt` (pid 30468, `com.pixel.gun3d`,
arm64-v8a, ru-RU).
Binary: `libil2cpp.so` SHA-256
`f0a130c4e8d9487059eab4b0f08462f6aa7d057510cada0fd6fb3043c77deb5c`.

All RVAs below are analysis references from a static A64 pass over that exact
binary. Nothing in the runtime code reads them; every entry point is resolved
by metadata name through IL2CPP.

## 1. The Switcher half of the boot is healthy

```
23.1.3-stall: skipping InitializeSwitcher state=35 #1 (ranchoOk=0 minSec=3.000 bar=0.450)
23.1.3-stall: InitializeSwitcher finished on its own at state=45
23.1.3-stall: LoadMainMenu tail completed (state 1 -> 2, result=1) ... the loading watchdog is disarmed
```

No `NullReferenceException`, no v1 regression. Everything the player still sees
as a frozen 90% loading screen happens afterwards, inside the auth scene.

## 2. What the capture shows in the auth scene

```
+011917ms 23.1.3-auth: AuthSceneController.Awake ENTER / RETURN
+012044ms suppressing retired Auth Start transport; scheduling mapped stock completion (before=0/Initial)
+012044ms stock completion coroutine accepted (0x6c760e5f80); iterator <completion> (0x6df7eb9000)
+012061ms completion iterator advanced -2147483648 -> 1
+013313ms completion iterator advanced 1 -> -1 after 72 frame(s)
+024550ms completion still pending after 300 frames (state=0/Initial ready=1)
          still parked; chain[0] <completion> state=-1 after 2700 frame(s); this class awaits no Task
          ignored retired version gate only for the local completion transaction
```

The scheduled coroutine ran to its end (`<>1__state == -1`), awaited no `Task`,
and the published `AuthSceneState` never left `0/Initial` while the
session-ready flag was already `1`.

## 3. Static analysis of the state machine

```
AuthSceneController.Start()                      0x3DC1BCC
  FpsManager.<setup>()                           0x2512E74
  <loadingBar>.<setProgress>(float)              0x35AD0A4
  ActivityIndicator.<setProgress>(float)         0x1E18DC0
  <stopAllCoroutines>()                          0x3DC1DB8
  <resetUi>()                                    0x3DC1E18
  ldr w1, [x8, #16]        ; static AuthSceneState backing field (+0x10)
  b   0x3DC1F38            ; TAIL CALL into the dispatcher

<dispatch>(AuthSceneState)                       0x3DC1F38
  if (<settings>.<localSessionGate>())  -> <startCompletion>()   0x3DC20E8
                                           = StartCoroutine(<completion>())
  InfoWindowController.HideServerMessageBox()
  switch (state) {
    2 Authorized   -> <cachedCommands>()        0x3DC2410
    1 Authorizing  -> StartCoroutine(<ping>())  0x3DC6DB0
    0 Initial      -> login UI / retired transport (0x3DC21B4 / 0x3DC2270)
    default        -> ret            // 3 FullySynchronized is NOT handled here
  }

<completion> iterator MoveNext                   0x3DC9320
  walks a Delegate[] and DynamicInvokes it   ([PROD-32809] OnResetData)
  strb w9, [x8, #20]     ; static session-ready bool (+0x14) = true
  <settings>.<setOffline>(false)
  Storager singleton: commit, subscribe, save
  loading bar + ActivityIndicator progress
  never writes the static AuthSceneState (+0x10)
```

Two facts follow, and both are decisive:

1. **The state transition belongs to `Start -> <dispatch>`, not to the
   completion coroutine.** The previous revision suppressed `Start` entirely
   and hand-started the coroutine, so nothing ever moved the state.
   `publish_if_ready()` requires `ready && (FullySynchronized || Empty)` and
   therefore could never become true. The mod parked until its fail-closed
   timeout while the loading screen stayed at 90%.
2. **Passing `FullySynchronized` to the dispatcher would be a no-op.** It only
   handles `0`, `1`, `2`; anything else falls through to a bare `ret`. The
   session has to be published through the static setter, not through the
   dispatcher.

Supporting observations:

- A caller scan over the whole executable segment (161,400 parsed methods)
  finds **zero** call sites for the state getter (0x3DC0920), the state setter
  (0x3DC0968) and the session-ready setter (0x3DC0A04): these
  `[CompilerGenerated]` accessors are inlined by IL2CPP and stock code writes
  the static field directly. So the missing setter-trace lines in
  `version_2313.h` are expected and are not, by themselves, proof that the
  state never moved -- the polled getter is what proves it.
- The only caller of the version blocker (0x3DC5F54) is the completion
  iterator's `MoveNext`, so the blocker mapping is correct and the bypass is
  scoped to the right transaction. Forcing it to `false` is the right
  semantics ("not blocked"), and it is not what kept the state at `Initial`.

## 4. The fix

| Step | Behaviour |
| --- | --- |
| Run the stock `Start` | FpsManager, loading bar, coroutine cleanup, UI reset and the dispatcher all execute as in the stock build. |
| Force the local-session gate (0x2B71728) | Only while one local auth transaction is active. This is the stock "a local session already exists" branch, so the retired ping/login/transport branches are never selected. |
| Hook the iterator factory (0x3DC3EE0) | The iterator created by the stock dispatcher is captured for introspection instead of being scheduled by this module. |
| Publish explicitly | Once the coroutine ends (or after the frame budget), set session-ready `true` (0x3DC0A04) and `AuthSceneState = FullySynchronized` (0x3DC0968) through the controller's own static setters. |
| Bounded fallbacks | 60 frames: hand-schedule the coroutine if the dispatcher never created it. 900 frames: invoke the stock post-auth continuation (0x3DC2CC4, version banner + SceneLoader) once. 3600 frames: existing fail-closed timeout. |

## 5. What the next capture should contain

```
23.1.3-local-backend: running the stock Auth Start on the local-session route (before=0/Initial ready=0)
23.1.3-local-backend: reporting an existing local session to the auth dispatcher for this transaction only
23.1.3-local-backend: the stock dispatcher created the completion iterator <name> (0x...)
23.1.3-local-backend: completion iterator <name> advanced ... -> -1 after N frame(s)
23.1.3-local-backend: published the local session explicitly (... state 0/Initial -> 3/FullySynchronized ready=1)
23.1.3-auth: direct state setter -> 3 (FullySynchronized)          <- version_2313.h trace
23.1.3-local-backend: the local session is usable (state=3/FullySynchronized readyFlag=1)
```

If `published the local session explicitly` appears but the scene does not
change, the follow-up line
`invoking the stock post-auth continuation once` tells the next reviewer that
the hand-off itself, not the auth state, is the remaining defect.

## 6. Known follow-ups (not in this change)

- `loading_stall_guard_2313.h` disarms the watchdog at the
  `Switcher -> AuthScene` hand-off, so an auth-scene stall is no longer covered
  by a watchdog. Re-arming it around the auth scene would turn a future hang
  into a bounded, logged failure.
- `photon_2313.h` logs the empty `PHOTON_APP_ID` at error severity on the local
  route, where it is expected; `ParseFullSlotConfig: versionDict not contains
  version 23.1.3` is likewise expected offline noise.

## TechnicalWorks suppression (August 27, 2026)

The retired transport can still send the auth machine to
`AuthSceneState.TechnicalWorks` (`15`). Waiting until the next `Update` to
repair the stored enum is too late because
`AuthSceneController.丙丟不丗丑下丌丁专(AuthSceneState)` (`0x3DC2458`) creates the
maintenance presentation while dispatching that state.

The local backend now hooks this dispatcher. When the central
`network::suppress_technical_works` switch is enabled, state 15 is replaced by
`FullySynchronized` before the stock dispatcher sees it. The session-ready
flag and stored state are published through the controller's own setters. All
other states stay stock. Failure to install this guard is treated as an
incomplete local-backend hook set.
