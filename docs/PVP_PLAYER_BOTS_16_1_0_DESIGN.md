# 16.1.0 PvP player-bot design

> Status: design and reverse-engineering contract. This commit does not enable
> bots yet. Runtime work must remain fail-closed until a compatible player-bot
> donor and map navigation data have been validated.

Target game build: supplied ARMv7 16.1.0 client. The integration branch remains
`16.1.1-test`; implementation is developed only through
`feat/16.1.0-pvp-player-bots` and its pull request.

## Goal

Add fair, network-synchronized player bots to the selected PvP modes without
turning the existing campaign/co-op mobs into fake players. Bots must use stock
combat values, participate in the normal mode rules and leave cleanly with the
room or owning player.

## Mode policy

| Mode | Team model | Bot policy | Start condition |
| --- | --- | --- | --- |
| Escort | Two teams | Pre-match roster fillers | After 10 seconds with no human roster growth, add one bot at a time to the most underfilled side until the mode's configured minimum is met |
| Squad Battle | 3 vs 3 | Pre-match roster fillers | Same 10-second cadence; stop at exactly three active slots per team |
| Battle Royale | Free for all | Pre-match roster fillers | Same 10-second cadence until the mode's configured minimum is met; every bot is an independent opponent |
| Team Fight | Two teams | One paired bot per human | Spawn five seconds after that human becomes playable; put the bot on the opposite or most underfilled team |
| Deathmatch / Free For All | Free for all | One paired bot per human | Spawn five seconds after that human becomes playable; the bot is an independent opponent |

Explicitly excluded:

- Survival;
- Campaign and co-op PvE;
- modes not listed above, until they receive their own reviewed adapter.

Battle Royale never creates teams or a permanent paired target. Its bots target
all valid visible opponents under the same rules as human participants.

## Two bot lifecycles

### Paired bots

Used by Team Fight and Deathmatch.

1. A human reaches the playable in-map state.
2. A five-second grace timer starts.
3. The authoritative client creates exactly one bot and records
   `human actor -> bot network view`.
4. A duplicate creation request for the same actor is ignored.
5. `OnPlayerLeftRoom`, a confirmed Photon timeout, room leave or match teardown
   destroys the paired bot through the stock network path.
6. If no humans remain, all bots are destroyed immediately.

### Roster fillers

Used by Escort, Squad Battle and Battle Royale.

1. The pre-match controller records the human roster revision and timestamp.
2. If the human roster has not grown for ten seconds, it creates one filler.
3. Team modes place the filler on the side with fewer occupied slots. Ties use
   deterministic alternation so the local team is filled as well as the enemy.
4. Battle Royale allocates one independent FFA slot.
5. The controller recomputes capacity after every creation and never exceeds
   the mode's configured requirement.
6. A human joining before match start replaces one compatible filler instead
   of increasing total occupancy.
7. Once the configured roster is satisfied, the normal game start path is
   allowed to continue. No timer or start RPC is fabricated early.
8. If every human leaves, all fillers are removed and the room is not allowed
   to continue as a bot-only match.

Late-join replacement after match start must happen only at a safe death or
respawn boundary; never destroy a live bot in the middle of damage processing.

## Authority and recovery

- Only the Photon master client may create, replace or destroy bots.
- Bot identifiers, mode slot and optional paired actor are serialized in room
  or object data so every client reconstructs the same roster.
- Master-client migration scans existing bot views before creating anything.
- The stable identity is the human actor number plus a match generation; a
  native pointer is never used as a network identity.
- All transitions are idempotent and bounded. Missing metadata disables the
  module instead of guessing an RVA, prefab or team value.

## Fair combat model

Bots must not receive hidden damage, health or accuracy advantages.

### Perception

- target only living opponents accepted by the current mode adapter;
- require field-of-view and unobstructed line of sight before firing;
- remember a lost target only briefly and never track through geometry;
- reaction delay sampled around 350-650 ms, with a longer delay after a sudden
  target turn or first sighting;
- mostly aim at centre mass; head aim is uncommon and affected by distance and
  movement;
- apply smooth turn speed and aim spread instead of snapping.

### Weapon decisions

The planner evaluates at a limited cadence and uses hysteresis so a bot cannot
switch every frame. It considers:

- target distance and weapon effective range;
- magazine and reserve ammunition;
- reload duration and current exposure;
- splash danger at close range;
- target count and line of fire;
- a switch cooldown and minimum benefit threshold.

Damage, fire interval, reload, projectile speed and ammo consumption come from
the stock weapon configuration. Empty weapons reload or switch; no ammo is
invented during a life. A conservative initial policy uses primary/backup at
medium range, melee only nearby and sniper only at long visible range.

### Difficulty

The first implementation ships one bounded normal profile. Later adaptation may
use player level and recent match performance, but only by changing reaction,
spread and tactical delay. It must never multiply stock damage or health.

## Navigation

The 16.1.0 binary contains `RilisoftBot` campaign/co-op mob AI, but it does not
contain the later multiplayer player-bot stack (`PlayerBotsManager`,
`PlayerBotInstance`, `PlayerBotEntity`, `PlayerBotWeapon`, `PlayerBotWear` and
`PlayerBotPathsGroup`). Campaign mobs are not valid substitutes: they do not
occupy normal player slots, use normal loadouts or integrate with every PvP
scoreboard.

A compatible donor must therefore provide the player-bot lifecycle and path
model. Every selected 16.1.0 map also needs validated navigation data. If donor
paths cannot be mapped safely, that map is disabled rather than falling back to
wall-following or teleportation.

Required donor inputs:

- `dump.cs`;
- `global-metadata.dat`;
- `libil2cpp.so`;
- OBB or asset bundles containing player-bot prefabs and path groups;
- preferably the earliest version containing multiplayer player bots, to
  reduce Unity serialization and gameplay drift.

## Implementation stages

1. Map donor player-bot classes, network events, prefab schema and path assets.
2. Add a disabled-by-default authoritative roster state machine with no combat.
3. Validate creation, replacement, disconnect cleanup and master migration in
   each requested mode.
4. Add navigation and perception with firing disabled.
5. Add stock-weapon firing, ammo/reload and conservative switching.
6. Tune the bounded normal profile using device traces, then enable each mode
   independently.

No stage may enable a later stage merely because a symbol name resolved.
Call-site and ABI validation are required for every native hook.

## Acceptance checks

- no bot appears before its five- or ten-second grace condition;
- Team Fight and Deathmatch create at most one paired bot per playable human;
- Squad Battle starts with exactly 3 vs 3 occupied slots;
- Escort fills both teams and reaches only the configured minimum;
- Battle Royale bots attack all valid participants and never acquire a team;
- a joining human replaces a pre-match filler without overfilling the room;
- disconnecting a paired human removes the paired bot;
- master migration creates no duplicate bots;
- no-human rooms tear down all bots;
- bots cannot fire through walls, snap aim, bypass ammo or exceed stock damage;
- weapon switches are distance/ammo driven and rate limited;
- teardown, rematch and scene changes leave no persistent bot objects;
- unsupported maps and unresolved APIs fail closed with one diagnostic log.
