# 23.1.3 — why the weapon sweep was slow, and what v4 changes

## Symptom

After v3 restored the full weapon grant, the arsenal did arrive and the menu
stayed responsive — but filling it took tens of minutes. Reported precisely as
"not laggy, just insanely slow".

## Cause: the pacing, not the workload

v3 budgeted grants in wall-clock time per frame and treated **any** grant that
exceeded the budget as a stall:

```c
constexpr uint64_t kGrantBudgetUs   = 10000u;  // 10 ms per frame
constexpr uint64_t kBackoffMaxFrames = 90u;    // ~1.5 s at 60 fps

if (cost_us > kGrantBudgetUs) {           // <-- always true in practice
    g_backoff_frames = ... * 2u;           // exponential, saturates fast
    g_next_grant_frame = g_frames + 1u + g_backoff_frames;
    return;
}
```

A full stock inventory transaction re-serialises the whole pending
profile-update command queue (`PrUpCmKey`), so on real hardware it costs tens
of milliseconds — always more than 10 ms. The condition above was therefore
true for essentially every grant, the backoff doubled to its 90-frame ceiling
within the first handful of items and never decayed back, and the sweep settled
at roughly **one weapon per 1.5 seconds**. For ~800 weapons that is about
twenty minutes.

The budget was defending a frame rate that was never actually at risk. It was
calibrated in v2, when the sweep granted a few dozen items in total.

## What v4 does

| Knob | v3 | v4 | Why |
| --- | --- | --- | --- |
| First-pass budget | 10 ms/frame | **120 ms/frame** (`kBurstBudgetUs`) | The bulk pass is allowed to cost frame rate. |
| First-pass cap | 4/frame | **64/frame** (`kBurstGrantsPerFrame`) | Stops being the binding constraint. |
| Steady-state budget | 10 ms/frame, 4 | 20 ms/frame, 8 | Later passes are re-checks, so they stay invisible. |
| Backoff trigger | cost > frame budget | **cost > 250 ms** (`kGrantStallUs`) | Only a pathological transaction should throttle anything. |
| Backoff ceiling | 90 frames (~1.5 s) | **8 frames** (~130 ms) | The backoff may slow the sweep, not stop it. |
| Warm-up | frame 300 | **frame 180** | Starts while the player is still reading the menu. `weapon_modules_2313` finishes around frame 145, so the two never share a frame. |
| Read-only checks/frame | 24 | **64** | Also how fast a later launch skips an inventory that is already full. |
| Sweep order | gadgets first | **weapons first** | Weapons are the bulk and the point. |

Expected shape: ~800 transactions at 15-40 ms each, 3-8 per frame, so the
arsenal completes in single-digit seconds. The menu is expected to drop to
roughly 8 fps while that happens. That is the trade that was requested.

## Verifying the rate instead of assuming it

v4 adds a running progress line every 100 grants:

```sh
adb logcat -s OPG3D | grep -E '23\.1\.3-hidden-items: progress'
```

```
progress: 400 granted in 5210 ms wall clock (4980 ms inside transactions,
          avg 12450 us each, 0 stalls)
```

Read it like this:

- **avg** is the true per-transaction cost. If it climbs steeply with the count,
  the queue re-serialisation is still quadratic and the batch entry point below
  is the real fix.
- **stalls** counts transactions over 250 ms. A non-zero value means the backoff
  did engage, which is intended.
- **wall clock** vs **inside transactions**: a large gap means something other
  than the grant is the limiter (frame pacing, warm-up, the owned re-check).

The per-pass summary reports the same totals plus `worst grant`.

## The actual end state: the batch entry point

The registry ships a batch grant that would collapse the whole sweep into one
transaction and one serialisation:

```
0x3061C20  List<item> 丘上丄三业丏丙不且(List<key> keys, Nullable<cause>, Action)
0x3062B08  item       丘上丄三业丏丙不且(key,       Nullable<cause>, Action)   <- used today
```

The single-key overload is a thin wrapper that allocates a one-element list and
runs the same transaction, which is why per-item cost grows with the number of
items already queued.

The blocker for using the batch overload is narrow and worth recording: the
current `il2cpp.h` surface exposes no `object_new` and no generic-instantiation
helper, so a managed `List<丑一丘与丁丄专专专>` cannot be constructed from native
code.

It can, however, be **borrowed**. Several shipped methods return
`List<丑一丘与丁丄专专专>`, for example:

| RVA | Member |
| --- | --- |
| `0x1B4B0E0` | `Progress.东丝丂丄业丕且丙丑::专与丁丞丂丁丐与丝(enum)` — instance already resolved by this module |
| `0x3C1F090` | `丏且专丛丆丛且专丘::丗丑世丌业世丞丐丒(int)` — static |
| `0x2F81384` | `上丝不下世七丄丅丟::丘丌丛三丂三丙东丁(enum)` — static |
| `0x4602 7BC` | `ItemSelectorView::丙丘丙丆丛一丟丁丞()` — instance |

Calling `List<T>.GetRange(0, 0)` on any of them returns a **fresh, empty,
correctly typed** `List<key>` that we own outright — which sidesteps the risk of
clearing a list the game still uses, and avoids type-punning a `List<item>`
into a `List<key>` (same shared IL2CPP code, but the backing array's element
type would be a lie).

Why it is not in v4: picking the borrow source needs validation on a device.
`dump2313.cs` is a declaration dump — method bodies are empty — and the sandbox
has no aarch64 disassembler (no capstone, host `objdump` is not aarch64-capable),
so there is no way to confirm from here whether a given method returns a fresh
list or a cached internal one, or what its enum argument means. v4 therefore
ships the pacing fix on code paths already known to work on the target
hardware, and the `avg` figure in the progress log is what decides whether the
batch work is still needed.

`kRegistryGrantBatchRva` is recorded in the header for that follow-up.

## Unchanged

Fail-closed binding (five metadata anchors, four RVA checks against
`base + RVA`), no patched memory, owned-check before and after every grant so a
re-run cannot duplicate items, self-disarm after 24 consecutive failures, and
the full-sweep fallback if the catalogue filter selects nothing.
