# 23.1.3 (ARM64) local player identity

## Problem

On a fresh install the build still registers the account against the retired
official backend and adopts the id that service hands out. For a private
deployment that is unacceptable: the profile depends on a service nobody
controls, and the moment it answers differently (or stops answering) the
local profile is worthless.

The id had to be minted **on the device, on the first launch**, so that the
registration round-trip never happens in the first place.

## Where the id lives

The build persists the account id in `PlayerPrefs` under the key
`main_player_id` (the literal sits next to `id_player`, the HTTP parameter the
old backend received it with). Registration is only attempted while that key
has no usable value, so seeding the key **before** the auth flow reads it
removes the request instead of patching it out afterwards.

## Why PlayerPrefs and not the storage wrapper

Every managed name in 23.1.3 is obfuscated, the storage wrapper included, and
the per-callsite string literals the wrapper uses could not be named from the
dump (`stringliteral.json` and every `script.json` section describe the literal
table, not the metadata-usage cache slots the generated code actually reads).

The wrapper nevertheless ends in `UnityEngine.PlayerPrefs`, whose names are
stable, so `identity_2313.h` anchors there:

| Hooked method | Behaviour for the id key |
| --- | --- |
| `PlayerPrefs.GetString(string)` | returns the local id, minting it on first access |
| `PlayerPrefs.GetString(string, string)` | same, for the defaulted overload |
| `PlayerPrefs.HasKey(string)` | reports the key as present, so nothing treats the account as unregistered |
| `PlayerPrefs.SetString(string, string)` | refuses any write carrying a different id |

All four are resolved through IL2CPP metadata by namespace/class/name/argument
count, so no RVA is involved and obfuscation cannot break them.

## Behaviour

* The id is uniformly random in `100000000..999999999` — exactly nine digits,
  never a leading zero. Entropy comes from `/dev/urandom`, whitened with a
  splitmix64 finalizer; if `/dev/urandom` were unavailable the seed falls back
  to `CLOCK_REALTIME ^ getpid()`.
* It is written under `main_player_id` **and** under this port's own marker key
  `opg3d_local_player_id`, then flushed with `PlayerPrefs.Save()`.
* Later launches adopt the marker, so the id is stable forever. It is only
  regenerated if the player wipes the game data.
* `SetString` refusal is what actually closes the door: even if a backend reply
  arrived, it cannot take the identity over.
* Minting is lazy and happens inside the first hooked call, i.e. always on a
  game thread. The port never touches `PlayerPrefs` from its own init thread.
* Our own reads and writes go through the hook trampolines, so they never
  re-enter the hooks.

## Logs

Prefix `23.1.3-identity:`.

```
23.1.3-identity: armed: 'main_player_id' is served from a device-minted 9-digit id and foreign writes to it are refused
23.1.3-identity: PlayerPrefs bridge is live (first key read: '...')
23.1.3-identity: generated local player id 481203975 (9 digits, minted on device, no backend round-trip)
23.1.3-identity: served local player id 481203975 (read #1)
23.1.3-identity: refused a foreign player id write ('...'); keeping the local id 481203975
```

On the second launch the first line becomes `reusing local player id ...`.

## Self-diagnosis

If this build ever asked for the id under a different key, the port would
silently do nothing, so up to 12 reads of keys that merely *look* id-related
(`player_id`, `playerid`, `user_id`, `userid`, case-insensitive) are logged with
the key name. If such a line shows up and no id was ever served, the key in
`kPlayerIdKey` needs to be adjusted.

Key matching accepts an exact match on `main_player_id` and a suffix match, so
a wrapper that prefixes its keys is still covered.

## Files

* `opg3d/src/main/cpp/identity_2313.h` — the whole subsystem.
* `opg3d/src/main/cpp/main.cpp` — `identity_2313::install_hooks()` in the init
  thread, reported as `identity=…` in the diagnostic line.

## Verified metadata (23.1.3 ARM64, `libil2cpp.so` `f0a130c4…`)

```
UnityEngine.PlayerPrefs (dump.cs:1108840, TypeDefIndex 20297)
  0x4402A2C  public static void   SetString(string key, string value)
  0x4402AC0  public static string GetString(string key, string defaultValue)
  0x4402B04  public static string GetString(string key)
  0x4402B70  public static bool   HasKey(string key)
  0x4402C10  public static void   Save()
```
