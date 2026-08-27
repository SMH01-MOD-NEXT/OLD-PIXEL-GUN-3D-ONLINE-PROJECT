# 23.1.3 authoritative local player identity

## Problem

The previous implementation hooked `UnityEngine.PlayerPrefs` and waited for a
plaintext `main_player_id` key. That path cannot work in 23.1.3: the game sends
preference names through `CryptoPlayerPrefsManager` first, where they are
hashed/encrypted before PlayerPrefs sees them. After device identity data was
changed, the profile therefore had an empty Player ID even though the local
backend had independently minted an ID in `state.kv`.

This produced two unrelated identities: the backend ID and the ID (or empty
value) held by the game. Backend-dependent state could then initialize only
partially.

## Implementation

`identity_2313.h` hooks both storage layers that receive the plaintext key:

- the game-facing `Rilisoft.丐不专丛丄一丕丌丆` facade (`HasKey`, one-argument
  `GetString`, and `SetString`);
- the lower static CryptoPlayerPrefs facade `丒丁专与丏丈丙丈世` (`HasKey`,
  defaulted `GetString`, and `SetString`).

The August 27 device log showed AppsMenu calling the Rilisoft facade directly;
the earlier CryptoPlayerPrefs-only bridge therefore installed cleanly but never
saw a `main_player_id` access. Both routes are now authoritative.

The persistent backend store is the single source of truth. New local IDs are
ten digits in the native 23.1.3 `35xxxxxxxx` shape. Legacy IDs minted by older
port revisions are migrated once, with the previous value retained in
`id_player_previous`. On the first stock access to `main_player_id`, the module
writes through the unhooked Rilisoft facade and immediately reads it back. This
lets the game perform its normal storage, encryption and persistence work.

All later reads return the same authoritative ID. Empty or foreign writes are
replaced with it. Identity hooks are installed before both local backend
modules, so authentication cannot start with an empty game identity.

Seeding is lazy rather than running in the native initialization thread. This
ensures `CryptoPlayerPrefsManager.Awake` has already supplied the stock salt and
encryption-mode settings before the first encrypted write.

## Expected logs

```text
23.1.3-identity: Rilisoft + CryptoPlayerPrefs identity bridges armed before auth
23.1.3-backend-store: migrating legacy minted id ... to the native 35xxxxxxxx shape
23.1.3-identity: authoritative local id 35xxxxxxxx was written and verified through the Rilisoft identity store
23.1.3-identity: served authoritative player id 35xxxxxxxx through Rilisoft storage
```
