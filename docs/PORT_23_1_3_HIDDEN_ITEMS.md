# 23.1.3 hidden weapon, wear and gadget unlock

Branch `23.1.3`, ARM64 only. Source: `opg3d/src/main/cpp/hidden_items_2313.h`,
driven once per main-menu frame from `progression_2313.h`.

Verified artifacts:

| file | sha256 |
| --- | --- |
| `libil2cpp.so` | `f0a130c4e8d9487059eab4b0f08462f6aa7d057510cada0fd6fb3043c77deb5c` |
| `global-metadata.dat` | `28b8bddf53a8ebdaf70aec1e672d3bdea6e46ca2b2e478f1b7e66e69884c99dd` |

ELF build id `57fcc18d2db06212416d480d53c0f881ee47c52a`.

## What the port does

Some items ship inside 23.1.3 but can never be obtained on a private server:
they are not sold in any shop tab, they have no reachable recipe, and the
retired backend events that used to hand them out are gone. Ultimatum and
Locator (weapons) are the well known examples, plus several wear pieces.

The port does not patch an "is hidden" or "can craft" read path and it does not
fabricate UI rows. It grants real ownership through the same stock item
inventory transaction the game itself uses, so the Armory, the loadout slots,
the equipped storage, the Progress profile and the save payload stay internally
consistent: everything the player sees is materialised by the game.

## v1 postmortem: why the main menu stalled for minutes

v1 (PR #38) worked, but it granted **every** definition the build ships, one
stock call at a time, through the single-key registry entry point at
`0x3062B08`. On device the first launch froze the menu for about five minutes.

A BL scan of the shipped code shows why. That entry point is only a wrapper:

```
丘上丄三业丏丙不且(key, Nullable<cause>, Action)                 0x3062B08   12 BL
└─ 丘上丄三业丏丙不且(List<key>, Nullable<cause>, Action)         0x3061C20   17 BL
   └─ 下万丗世丑万丌东东(List<key> give, List<key> take, cause)   0x3061DB0  130 BL
      ├─ Progress.东丝丂丄业丕且丙丑::丞丏业丐丒与业丗与()                   0x1B3BA40
      ├─ Progress.东丝丂丄业丕且丙丑::丂一丈东世业丆业丅(...)                 0x1B44114
      └─ Progress.东丝丂丄业丕且丙丑::丈且东丝丝东且丈专(Dictionary<string,object>) 0x1B44230
```

So a "grant one item" call allocates a one-element list and runs the **full
inventory transaction**, which appends a profile-update command and
re-serialises the whole pending command queue (`PrUpCmKey`). The cost of item
N grows with the number of items already queued, so granting roughly 1500
definitions is quadratic work: minutes of stalling, getting worse as it goes.

Two consequences worth writing down:

* **Frame pacing cannot fix this.** v1's `kGrantsPerTick` spread the calls over
  frames, but the expense sits inside a single transaction, so every call still
  blocked its frame for ~100 ms once the queue was long.
* **The cheap primitive is not enough either.** `与上丌丛丟上业世上(key, Nullable)` at
  `0x3062C74` registers an item in memory and fires the stock "obtained" event
  without touching Progress, but then nothing persists it, so the sweep would
  repeat on every launch. Not used.

## v2 design

### 1. Only grant what nothing offers

The request was hidden and non-craftable items, not the whole catalogue. v2
answers "can the player reach this item?" structurally, with the stock lookup

```
与丅丟七与丌东丙丌(item) -> 丒专与三七丁丌丟丆     0x305C6C0
```

A definition with no catalogue entry is offered by no shop tab, no craft list
and no event list. Those are exactly the hidden items. Everything the shop
already sells is skipped, so a fresh profile pays a few dozen transactions
instead of ~1500, and the whole sweep finishes in seconds.

An id allowlist (`ultimatum`, `locator`, `harpoon`, substring, case
insensitive) is always granted regardless of the catalogue answer, so items
that work for players today cannot regress.

**Safety net.** If a pass finds at least `kFallbackMinDefinitions` definitions
and none of them looks unobtainable, the catalogue assumption is wrong for that
profile; the driver logs a warning and retries once with the full sweep, which
is now paced. A wrong assumption degrades to "slower but complete" rather than
"silently does nothing".

### 2. Wall-clock budget instead of a frame counter

Every grant is timed with `CLOCK_MONOTONIC`. At most one grant runs per menu
frame, and when one costs more than `kGrantBudgetUs` the driver backs off
exponentially (`kBackoffStartFrames` doubling up to `kBackoffMaxFrames`, ~4 s)
before the next attempt; a cheap grant halves the backoff again. Read-only work
(list access, owned count, catalogue lookup) is not budgeted because it costs
nothing: `kChecksPerTick` of those run per frame.

Worst case on a slow device is therefore a trickle of one transaction every few
seconds, never two in a row, instead of a frozen menu.

### 3. No category stage, fewer passes

v1 swept the registry and then walked 22 catalogue categories, re-checking the
same definitions many times, for up to three passes. v2 sweeps the registry
only (`丈丂丆丙丂一七丞丌(OfferItemType)` is the definition list of record) with
`kMaxPasses = 2`. The three catalogue helpers stay bound, but only as image
proofs.

### 4. Targeted find-by-id probe

Before the sweep, one type per frame, the registry is asked directly for a
short list of ids through

```
与丒丅丝丕丕丒丟丆(OfferItemType, string) -> item    0x3060088
```

This reaches definitions that the per-type enumeration may not list at all. It
exists for the harpoon (below). A lookup is not a transaction, so the probe is
free; at most `kMaxProbeGrants` grants can come out of it.

## Verified offsets

| RVA | member | use |
| --- | --- | --- |
| `0x3046000` | `下丌丑丁下丟丛丘上()` | registry singleton, image proof |
| `0x305C074` | `三与七丆丅丆丕丒业(type, category)` | image proof only |
| `0x30479D0` | `丌丄丛丈与丝丑世丆(entry)` | image proof only |
| `0x305C50C` | `丁丒丕丌丂丌且丙且(category)` | image proof only |
| `0x3060030` | `丈丂丆丙丂一七丞丌(type)` | definition list per type |
| `0x304F634` | `丙丛业丐丐七丛不丂(key, Nullable)` | owned count, idempotence guard |
| `0x3062B08` | `丘上丄三业丏丙不且(key, Nullable, Action)` | the stock grant |
| `0x3060088` | `与丒丅丝丕丕丒丟丆(type, string)` | targeted id probe |
| `0x305C6C0` | `与丅丟七与丌东丙丌(item)` | "is this offered anywhere" |

The overloaded members are taken by RVA, and only after the four metadata
anchors resolve to exactly `base + RVA`. If any check fails, nothing is armed.

`Nullable<>` sizes: owned filter 24 B, obtain cause 104 B. Both are passed as
all-zero buffers, which IL2CPP reads as a null optional, so the callee
substitutes its own defaults.

## Tunables

| constant | value | meaning |
| --- | --- | --- |
| `kWarmupFrames` | 300 | never overlaps the module sweep |
| `kChecksPerTick` | 24 | read-only checks per menu frame |
| `kGrantBudgetUs` | 6000 | a grant above this triggers backoff |
| `kBackoffStartFrames` | 6 | first backoff |
| `kBackoffMaxFrames` | 240 | ~4 s ceiling between grants |
| `kMaxPasses` | 2 | plus a `kRecheckFrames` (60 s) gap |
| `kMaxConsecutiveFailures` | 24 | then the port disarms itself |
| `kIncludeSkins` | `false` | skins are cosmetics, opt-in |
| `kGrantEverything` | `false` | restores v1 scope, now paced |
| `kDumpGadgetIds` | `true` | one-shot gadget id dump, 96 lines max |

## Log lines to expect (`adb logcat -s OPG3D`)

```
23.1.3-hidden-items: armed: definitions no shop or craft list offers are granted ...
23.1.3-hidden-items: gadget definition 'Grenade' (offered, owned 1)
23.1.3-hidden-items: probe found gadget id 'Harpoon' (name '...', owned 0)
23.1.3-hidden-items: granted weapon 'Ultimatum' (4213 us, next grant in 1 frames)
23.1.3-hidden-items: gadget: 41 definitions, 38 already owned, 3 unobtainable, 3 granted
23.1.3-hidden-items: pass 1 complete (seen=... unobtainable=... granted=... worst grant ... us)
```

The per-type summary and `worst grant` value are the two numbers to check when
judging whether the menu can stall: as long as the worst grant stays in the
low milliseconds, no frame is visibly lost.

## The harpoon

Still unresolved, and deliberately not faked. 23.1.3 metadata contains **no
harpoon item id**. An anchored string scan of `global-metadata.dat` returns
only `Harpoon` and `HarpoonIfFullCharged`, plus field names
(`harpoonImpulse`, `harpoonMaxDistance`, `harpoonStartPoint`,
`isHarpoonProjectile`, `[...(Harpoon)] public bool harpoon`). `Harpoon` itself
is member 2 of the movement-gadget kind enum
`专丟且东丐三丟丕业 { None, Rocket, Harpoon, Dash, Portal, Bot, Trampoline }`
(TypeDefIndex 3669), i.e. a behaviour flag, not an inventory id.

That is consistent with v1 granting everything and the harpoon still not
appearing: gadget ids in this build follow the bare-name convention (`Grenade`,
`JetPack`, `Portal`, `Shield`, `Trampoline`, `Drone`, `Healing`) and are
supplied as data, so the real id has to be read off a device. Hence:

1. `kDumpGadgetIds` dumps every gadget definition the build ships, with its
   catalogue status and owned count.
2. `kProbeIds` tries `Harpoon`, `HarpoonGun`, `Harpoon_1`, `HarpoonGun_1` and
   `harpoon` against every swept type.

Send the `OPG3D` log from one menu entry and the id is a one-line change. If
the dump shows no harpoon-like gadget at all, the harpoon is a weapon-config
behaviour in 23.1.3 rather than an ownable gadget, and no unlock can create it.

## Safety properties

* Fail closed: metadata mismatch, unknown base address or a failed image proof
  arms nothing.
* No patched code, no written game memory, only stock public calls.
* Idempotent: an item is granted only when its owned count is 0, and the count
  is re-read afterwards to confirm the transaction registered.
* Self-disarming after `kMaxConsecutiveFailures` consecutive failures.
* Skins remain excluded unless `kIncludeSkins` is turned on.
