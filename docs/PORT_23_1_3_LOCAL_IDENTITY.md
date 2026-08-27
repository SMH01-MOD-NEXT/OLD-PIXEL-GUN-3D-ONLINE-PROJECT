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

`identity_2313.h` now hooks the actual static CryptoPlayerPrefs facade
`丒丁专与丏丈丙丈世`:

- `七丌丁丝丏丝丐丑丘(string)` — presence check;
- `丘丞丝丂三丌专丑丒(string,string)` — encrypted write;
- `下丁丏且丕与下丑丗(string,string)` — encrypted read.

The persistent backend store is the single source of truth. New local IDs are
ten digits in the native 23.1.3 `35xxxxxxxx` shape. On the first stock access
to `main_player_id`, the module writes that value through the unhooked game
facade and immediately reads it back. This lets the game perform its own key
hashing, encryption, cache update and PlayerPrefs persistence.

All later reads return the same authoritative ID. Empty or foreign writes are
replaced with it. Identity hooks are installed before both local backend
modules, so authentication cannot start with an empty game identity.

Seeding is lazy rather than running in the native initialization thread. This
ensures `CryptoPlayerPrefsManager.Awake` has already supplied the stock salt and
encryption-mode settings before the first encrypted write.

## Expected logs

```text
23.1.3-identity: encrypted identity bridge armed before auth
23.1.3-backend-store: minted the provisional account id 35xxxxxxxx
23.1.3-identity: authoritative local id 35xxxxxxxx was written and verified through the stock encrypted identity store
23.1.3-identity: served authoritative player id 35xxxxxxxx through CryptoPlayerPrefs
```
