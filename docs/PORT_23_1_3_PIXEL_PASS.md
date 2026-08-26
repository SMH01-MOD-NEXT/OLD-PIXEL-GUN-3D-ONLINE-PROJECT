# 23.1.3 - PixelPass season, supplied from inside the library

This note covers why 23.1.3 showed **no battle pass button at all** in the
main menu, and how the port now supplies a real season without a self-hosted
backend and without reimplementing a single screen.

Everything below was proven from the supplied artifacts only: the 23.1.3
ARM64 `libil2cpp.so`, its `global-metadata.dat` and the Il2CppDumper output.

## 1. Why there was no pass, not even a locked one

`PGCompany.PixelPassLobbyView` (TypeDefIndex 12069) is the lobby entry point.
It owns one container per state:

| Field | Offset | State |
| --- | --- | --- |
| `_holder` | 0x48 | the whole button |
| `_lockContainer` | 0x70 | locked |
| `_unLockContainer` | 0x78 | available |
| `_comingSoonContainer` | 0x80 | coming soon |
| `_needLevelContainer` | 0x88 | level gated |
| `_tutorialContainer` | 0xA0 | tutorial |

The state selectors are `0x28F4E5C`, `0x28F5600` and `0x28F56F0`, and the pass
service they read (`PGCompany.PixelPass` service type, entry `0x18EF98C`) is
held at `+0x110`.

With no season, the view does not pick a state - it switches `_holder` off
entirely. That is exactly what the reported screenshot showed: **Sets** and
**Lottery** rendered their own coming-soon containers, while the pass button
was absent rather than greyed out.

So the previous work in this area could not have helped: unlocking a level
gate or granting cosmetics natively never creates a season, and without a
season there is nothing to render.

## 2. The season is configuration, not backend logic

`PGCompany.PixelPass` season config (TypeDefIndex 13225) is tagged with the
config-id attribute `(123, 1, True)` and `[JsonObject(1)]`:

| JSON | Meaning |
| --- | --- |
| `c` | Common (season id, name, start, end, counters) |
| `p` | Pages |
| `l` | Levels, i.e. the tiers |
| `t`, `prt`, `tb`, `at` | tasks, premium task indexes, base tasks, ad tasks |
| `r` | game rewards |
| `of` | offers |

ConfigId 123 is the payload the stock client calls `pixel-pass-v6`. The
retired backend never computed it - it only shipped it. That is the whole
reason a local season is possible.

Tier DTO (TypeDefIndex 13234): `l` level, `t` type, `p` page, `e` exp,
`r` rewards, `f` is-free, `c` is-cool. Tier type enum (13227) is
`None=0, First=1, Regular=2, Last=3`.

## 3. The delivery path: the game's own config cache

23.1.3 ships an on-device config cache, `PGCompany` TypeDefIndex 11078 - the
class that owns the `BinaryConfigStorage.Key` marker. It exposes, keyed by
`ConfigId`:

```
save(ConfigId, byte[], out string)      // 0x249CD64
load(ConfigId, out byte[], out string)  // 0x249D670
```

`pixel_pass_2313.h` binds those two by metadata name and argument count, plus
the cache singleton, and writes a season into them. The game then parses it
with its own Newtonsoft pipeline and renders it with its own pass screens.

Note the sibling enum in the same area, `Unknown / NewtonSoftJson /
MessagePack`: the pass config is a `[JsonObject]` type with no `[Key]`
attributes, so JSON is the correct encoding for this id.

### Why the payload is base64 on the way in

The cache takes a managed `byte[]`. This port's IL2CPP wrapper has no array
allocator, and `System.Text.Encoding.GetBytes` has two single-argument
overloads - `GetBytes(char[])` at dump line 660401 and `GetBytes(string)` at
660410 - which a lookup by name and argument count cannot tell apart. Handing
a string to the `char[]` overload would be a crash.

`System.Convert.FromBase64String(string)` (dump line 562789) has exactly one
overload, so the JSON is base64-encoded in native code and decoded by the
runtime. No new IL2CPP exports and no new hook engine features were needed.

## 4. Two details that make hand-authored JSON safe

**Salted ints.** `Rilisoft` salted int (TypeDefIndex 9203), used for season id
and tier exp, is tagged `[JsonConverter(...)]`. In JSON it is a plain number
and the converter re-salts it on read, so no salt is ever fabricated and
nothing looks tampered with to the client.

**Reward tokens are strings.** The reward converter's `ReadJson`
(`0x33494A4`) hands the token to the data collector at `0x2B005A4`, and the
reward parse helper `0x24B4260` splits on `':'` (`movz w1, #0x3A`) and
int-parses the first field. That matches the reward constructor
`(OfferItemType, string id, int amount)` at `0x24B39D8`, so a reward is:

```
<type>:<id>:<amount>        e.g. 1170:<skin id>:1
```

`WeaponSkin = 1170` and `Graffiti = 1470` come from the `OfferItemType` enum
(dump line 364118 onwards).

## 5. What the season contains

* 50 tiers across 5 pages, 100 exp per tier.
* Tier types: first tier `First`, last tier `Last`, everything else `Regular`.
* **Every tier is free.** This port has no store to buy a premium track from,
  so gating tiers would only hide content.
* Tiers 1-40 award **weapon skins**, tiers 41-50 award **graffiti**.
* Season window is fixed at 2020-01-01 to 2099-01-01, so it is active whatever
  the device clock says and no runtime date arithmetic is involved.

Weapon skin ids are never hardcoded: they are read at runtime from the
build's own local `WeaponSkins` catalogue (`Rilisoft` skin catalogue static,
`0x35B4C88`), which is backed by a resource and therefore populated with no
network at all. Ids carrying a quote, a backslash, a colon or a control
character are dropped rather than escaped.

Only fields whose JSON shape is proven are emitted. Prices, premium flags and
elite-task previews are deliberately **omitted**: an omitted property keeps
its default, whereas a guessed one can fail the whole season parse and put the
lobby back to having no pass.

## 6. Safety and lifecycle

* Fail-closed: if any metadata target is missing, or the skin catalogue is
  empty, nothing is written.
* Idempotent: if a non-empty payload for id 123 is already cached, it is left
  untouched, so a real season is never clobbered.
* Runs from the `MainMenuController.Update` slot this port already owns - the
  seeder needs a game thread and a settled managed heap. It warms up 240
  frames in, retries at most 5 times, and then stops for the launch.
* Writes nothing outside the game's own config cache.

## 7. How to verify on device

```
adb logcat -s OPG3D | grep pixelpass
```

Expected, in order:

```
23.1.3-pixelpass: armed (config id 123, 50 tiers, 10 per page)
23.1.3-pixelpass: season written to the stock config cache (id 123, ... )
```

Other outcomes and what they mean:

| Line | Meaning |
| --- | --- |
| `metadata does not match` | not the expected 23.1.3 build; nothing written |
| `local weapon skin catalogue is still empty` | resource not loaded yet; it retries |
| `config storage refused the season` | the cache reported an error, which is logged verbatim |
| `already cached` | a payload for id 123 already existed |

The config system reads this cache while starting up, so if the button is
still missing on the launch that wrote the season, relaunch once and check the
menu again.

## 8. Known follow-ups

* **Lottery and Sets** still render their own coming-soon containers. They are
  separate payloads: loot boxes is ConfigId 119 and the Sets coming-soon flag
  lives on `ShopNGUIController` at `+0x360`. Their DTOs need the same
  treatment as the pass before they can be seeded honestly.
* **Graffiti keys** follow the graffiti system's own `graffiti_-1` sentinel,
  so real entries are `graffiti_<n>`. That shape is inferred, not dumped. If
  the closing tiers show empty slots, the graffiti catalogue (ConfigId 133)
  needs seeding too, and `kIncludeGraffiti` in `pixel_pass_2313.h` turns them
  off in one line meanwhile.
* **Reward token field order** is `type:id:amount`, matching the reward
  constructor. If a build ever parses it the other way round, only
  `append_reward` has to change.
