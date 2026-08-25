# 23.1.3 — full weapon grant restored (hidden items v3)

## Why this change exists

`hidden_items_2313.h` v1 granted **every** definition the build ships. That is
the behaviour we want for the arsenal, but it froze the main menu for minutes on
first launch.

v2 fixed the freeze by narrowing the sweep to definitions that no shop tab,
craft list or event list offers (`与丅丟七与丌东丙丌(item)` returning null). That
removed the stall, but it also silently dropped every weapon the shop *does*
sell from the grant — so the arsenal was no longer complete.

v3 keeps v2's stall protection and puts the whole weapon list back.

## What is granted now

| Type | v2 | v3 |
| --- | --- | --- |
| Weapon | only if no catalogue offers it | **every definition** |
| Armor, mask, hat, boots, cape | only if no catalogue offers it | unchanged |
| Gadget | only if no catalogue offers it | unchanged |
| Skin | excluded (`kIncludeSkins = false`) | unchanged |

The switch is `kGrantEveryWeapon` (default `true`). `kGrantEverything`
(default `false`) still exists and widens the same behaviour to every type; the
driver also flips it on by itself for one retry if a pass selects nothing at
all, so a wrong assumption about the catalogue degrades into "slower but
complete" rather than "does nothing".

## Why the freeze does not come back

The cost is in the stock transaction, not in our loop: the single-key grant
`丘上丄三业丏丙不且` re-serialises the whole pending profile-command queue
(`PrUpCmKey`) on every call, so the cost of item *N* grows with *N*. Granting
~800 weapons is quadratic work.

v2 answered that with "at most one transaction per frame, whatever it cost".
That is safe for a few dozen grants, but across the whole weapon list it is
too slow: every grant costs a mandatory frame gap, and once grants exceed the
budget the exponential backoff saturates, which would have stretched a full
sweep over an hour of trickling.

v3 budgets **wall-clock time per frame** instead:

- `kGrantBudgetUs = 10000` (10 ms) — the transaction budget for one menu frame.
- `kGrantsPerFrameCap = 4` — hard cap on transactions in a single frame.
- Cheap grants share a frame until the budget is spent; the sweep then suspends
  with its cursor in place and resumes on the next frame.
- A grant that alone costs more than the budget still arms the exponential
  backoff (`kBackoffStartFrames = 6`, `kBackoffMaxFrames = 90`, down from 240),
  so the late and expensive part of the sweep degrades into a slow trickle
  rather than a stutter.

Net effect: the early bulk of the arsenal lands several times faster than v2
would have allowed, and the tail stays smooth. The menu is responsive
throughout — the sweep only starts at menu frame 300 (`kWarmupFrames`) and
never runs on the same frame as `weapon_modules_2313`.

Expect the full weapon sweep to complete over the first few minutes in the
menu, not instantly. Progress is visible in logcat.

## Verifying on device

```sh
adb logcat -s OPG3D | grep -E '23\.1\.3-hidden-items'
```

What to look for:

- `armed: every weapon the build ships, plus wear and gadgets no shop or craft
  list offers ... (10000 us budget and max 4 transactions per menu frame)`
- `granted weapon '<name>' (<cost> us, <n> this frame, backoff <k> frames)`
- per-type summary: `weapon: N definitions, M already owned, K wanted, G granted`
- `pass 1 complete (...)` then `weapon, wear and gadget inventory complete`

If `worst grant` in the pass summary is far above 10000 us, the device is
slower than assumed and the backoff is doing its job; raise `kGrantBudgetUs`
only if the menu still feels smooth.

## Safety model (unchanged)

- Fail closed: five metadata anchors must resolve, and four RVA-taken entry
  points must land exactly on `base + RVA`, or nothing is armed.
- No game memory is patched; only stock public calls are made.
- Every grant is guarded by an owned-count check before and verified after, so
  re-running the sweep cannot duplicate an item.
- `kMaxConsecutiveFailures = 24` disarms the module instead of spinning.
