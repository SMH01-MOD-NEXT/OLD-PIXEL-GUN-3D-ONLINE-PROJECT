# 23.1.3 - backend emulation inside the library

The services this build was written against are gone. Everything they used to
answer - the account handshake, remote configuration, telemetry, leaderboards,
mailbox, clans and the matchmaking hints - is now answered by an HTTP server
that lives inside `libopg3d.so` and starts with the process.

Nothing has to be hosted, installed or configured. Installing the APK is the
whole setup, which is the point: a public build cannot ask its players to run a
server.

## Layout

| File | Role |
| --- | --- |
| `backend_emu_store.h` | Persistent state. `<app files>/opg3d-backend/state.kv`, tab-separated, written through a temporary file and `rename()` so a crash cannot leave half a file. Mints the account id and the session token. |
| `backend_emu_http.h` | The HTTP/1.1 server and the LAN discovery responder. One thread per connection, capped; hard limits on header and body size. |
| `backend_emu_routes.h` | One handler per retired service. |
| `backend_emu_2313.h` | Redirects the game's transports at `UnityWebRequest` and `WWW`. |

## How a request is redirected

The URL is rewritten before the original method ever sees it:

```
https://server-v2.pixelgun3dserver.com/auth_v2/?id_player=1
  -> http://127.0.0.1:47317/@server-v2.pixelgun3dserver.com/auth_v2/?id_player=1
```

Rewriting the address rather than synthesising responses keeps the game on its
own real HTTP path. Status codes, POST bodies, headers, retries and coroutine
timing behave exactly as they did against the live service; only the socket
that answers has changed. It also removes TLS from the picture, so no
certificate has to be trusted, pinned or faked.

The original host survives as the first path segment (`/@host/...`), so a single
socket can still tell the services apart.

Intercepted host suffixes: `pixelgun3dserver.com`, `pixelgun3d.com`,
`pixelgunserver.com`, `lightmap.com`. Photon's hosts are deliberately left
alone - that traffic is not HTTP and the Photon port owns it.

### Overload safety

`UnityWebRequest` overloads its constructors and helpers on `(string)` and
`(Uri)`, and IL2CPP metadata lookup by name plus argument count cannot tell
those apart. So no argument is assumed to be a string: each candidate is
type-checked against `System.String` and anything else is passed through
untouched, with one log line naming the surface.

## The account id

The id is served by the emulated `auth_v2` endpoint, not by a preference hook.
This build encrypts every preference through `CryptoPlayerPrefsManager` (salt,
Rijndael, XOR), so at runtime the keys look like `AEAD:<base64>` and a
plaintext key such as `main_player_id` never exists. A hook that matches on the
key name therefore cannot fire - which is exactly why the earlier attempt did
not take. The authentication answer is the one place this build accepts an id
from, so that is where it now comes from.

The id is nine digits, minted once from `/dev/urandom`, and kept in
`state.kv`, so it survives relaunches. The account answer also carries every
spelling this build's parsers are known to read (`id_player`, `player_id`,
`id`, `user_id`, plus `session` / `token` and `nick` / `name`); a JSON consumer
ignores members it does not know, so covering the aliases removes a class of
guesswork at no cost.

## LAN sessions

The server binds `0.0.0.0`, so the emulated backend is reachable from the rest
of the network, and roles are decided at startup without any address entry:

1. a UDP probe goes out on port **47318** (three attempts, 300 ms apart);
2. no answer means this instance is the **host**: it binds HTTP on **47317**
   and starts answering probes;
3. an answer means this instance is a **LAN client**: it points the game at the
   host that replied and binds nothing.

So the first device to launch carries the session and every device that joins
afterwards shares its accounts, clans and progress. That is enough to run a LAN
party on a network with no internet at all.

If 47317 is taken, the next eight ports are tried, then an ephemeral one; the
port that won is what discovery advertises.

## Diagnostics

`http://<host>:47317/opg3d/status` reports the endpoint, the role, the port,
the number of requests served, the account id and the state directory. It is
reachable from the device and from any machine on the same network.

In logcat, filter on tag `OPG3D`:

| Marker | Meaning |
| --- | --- |
| `23.1.3-backend-emu: armed as ...` | the server is up; names the role, endpoint and account id |
| `23.1.3-backend-emu: ... -> local backend (#n)` | a URL was redirected |
| `23.1.3-backend-http: no route for ...` | an endpoint with no handler of its own |

## Unknown endpoints

Anything without a handler is answered by a permissive success payload rather
than an error, so the game keeps running. Each miss costs one warning line
naming the host and path.

Those lines are the input for the next pass: collect them from a full session
(launch, lobby, shop, a match, the post-match screen, the clan screens), and
each one names an endpoint that still needs a real schema. Content that is
still trimmed after this change is almost always a config or catalogue endpoint
that replied `{}` when it should have replied with a list.

## Limits worth knowing

- Empty is not the same as correct. Leaderboards, mailbox and clan lists answer
  well-formed but empty, so the screens open instead of hanging; populating
  them is later work.
- Configuration endpoints answer `{}`, which makes the client fall back to the
  content shipped in the APK. That is the desired outcome for a trimmed build,
  but a genuine override cannot be expressed yet.
- Matchmaking hints only stop blocking; room brokering stays with Photon.
- The store is a flat key-value file, not a database. It is fine for one device
  hosting a handful of players, and it is not meant for more.
