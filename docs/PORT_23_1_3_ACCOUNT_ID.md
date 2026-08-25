# 23.1.3 - the account id the game runs as

## Symptom

The game shows an account id starting with `35...`, while
`<app files>/opg3d-backend/state.kv` holds a different, nine-digit id. Since
every answer the emulated backend gives is keyed off that file, the backend and
the screen described two different accounts, and no relaunch could bring them
together.

## Cause

Two defects, both in this repository.

1. `backend_emu_routes.h::handle_auth` discarded the request (`(void)request;`)
   and always answered with `backend_emu_store::player_id()`. The game sends
   the id it already runs as with every authentication call
   (`auth_v2/?id_player=...`), and this build keeps that id in a preference
   that `CryptoPlayerPrefsManager` encrypts (salt + Rijndael + XOR), so this
   port can neither read it nor overwrite it. An answer that names a different
   id therefore cannot win: the game keeps its own id, and `state.kv` keeps
   ours.
2. `backend_emu_store.h::looks_like_id` accepted exactly nine digits. The ids
   this build carries are ten digits (`35...`), so even an adoption path built
   on the old check would have rejected the real id and re-minted on every
   launch.

The id was never the *auth answer's* to give on this build. That is the part
the previous revision, and the `identity_2313.h` PlayerPrefs approach before
it, both got backwards.

## Fix

The game owns the id; the store follows it.

* `handle_auth` reads the presented id from every spelling the retired service
  accepted - `id_player`, `player_id`, `user_id`, `account_id`, `id_user`,
  `uid`, `id` - in the query string or in an urlencoded / multipart body, and
  calls `backend_emu_store::adopt_player_id`.
* Placeholders fail the shape test (digits only, no leading zero, 5-18 wide),
  so the `?id_player=1` an unregistered client sends is reported as "no id
  presented" instead of being adopted as account 1. In that case the stored id
  is offered exactly as before, which is what a fresh install needs.
* A minted id is explicitly provisional now: it exists so that a fresh install
  has something to authenticate with, and the first authentication that names a
  real account replaces it. The replaced value is kept in `id_player_previous`.
* Adoption is loopback-only. On a LAN host, a joining device is answered about
  its own id and the host's account is left untouched, so whoever authenticates
  last can no longer take over the host state.
* `id_player_source` records `client` or `minted`. A nickname this port derived
  from a replaced id is dropped and derived again from the adopted one; a
  nickname the player chose is left alone.

## Log markers

```
23.1.3-backend-store: minted the provisional account id <id>; the first authentication that names a real account replaces it
23.1.3-backend-store: adopted the account id the game presented (<old> -> <new>); state.kv now names the account the game shows
23.1.3-backend-store: reusing local account id <id> (source 'client')
23.1.3-backend-auth: the game authenticated as id_player=<id>, which is already the stored account (confirmation #n)
23.1.3-backend-auth: the game presented no account id (target '...'); it was offered the stored id <id>, source 'minted' (request #n)
23.1.3-backend-auth: LAN client <ip> authenticated as id_player=<id>; this host keeps its own id <id> (request #n)
```

The first launch after this change is expected to print exactly one
`adopted the account id the game presented` line, naming the `35...` id the
player sees on screen. Every later launch prints
`reusing local account id <that id> (source 'client')`.

## Validation

1. `adb logcat -s OPG3D | grep -E 'backend-store|backend-auth'` - one adoption
   line, then confirmations.
2. `adb shell run-as com.pixel.gun3d cat files/opg3d-backend/state.kv` -
   `id_player` equals the id on the profile screen, `id_player_source` is
   `client`, and `id_player_previous` holds the provisional id.
3. `curl http://<device>:47317/opg3d/status` - `id_player`,
   `id_player_presented` and the on-screen id all agree.
4. Relaunch: the id must not change.
5. Two devices on one network: each keeps its own id, and the host's
   `state.kv` is not rewritten by the client's authentication.

## Still open

Whether this build ever *adopts* an id from the auth answer on a genuinely
wiped install is still unproven - it has only ever been observed keeping its
own. The fix does not depend on it: if the game takes the offered id, both
sides already agree, and if it mints its own instead, the next authentication
is adopted. A `wipe data` logcat would settle it and is worth capturing when
convenient.

## Falsified along the way

* Hooking `UnityEngine.PlayerPrefs` by key name (`main_player_id`,
  `identity_2313.h`) cannot fire on this build: the keys are encrypted at
  runtime, so a plaintext key never exists.
* Answering the auth endpoint with a locally minted id does not change the id
  the game displays, no matter which field aliases the answer carries.
