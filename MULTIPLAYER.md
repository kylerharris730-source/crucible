# Multiplayer architecture

## Playable target

- Two players in cooperative survival today: one host and one joined peer.
- The session/world model already has four stable slots; accepting more than
  one remote socket is a later transport extension, not a world-state rewrite.
- One player's game is the authoritative host and owns the save.
- Another computer joins by entering the host's IPv4 address. Port 27841 is
  fixed for this first version.
- LAN/direct IP is the baseline. Discovery, NAT traversal, matchmaking,
  dedicated servers, host migration, and cross-version play are later work.
- Every peer must run the exact same build and protocol.

On an ordinary home LAN, the host's private address is sufficient. The host
machine may still need to allow Crucible through Windows Firewall. Guest Wi-Fi,
client isolation, VPN routing, or separate VLANs can prevent two machines on
the same physical router from reaching each other; discovery cannot repair
that, which is why manual IP joining is the dependable baseline.

## Authority

The host owns all durable and gameplay-relevant state:

- world cells, temperatures, backgrounds, and active chunks;
- players, inventories, equipment, crafting, and damage;
- enemies, pickups, drones, projectiles, devices, rooms, and circuits;
- time, bosses, saving, and loading.

Clients send input or intent, never results. A client asks to use the selected
item at an aim point; it does not announce that a block disappeared or an item
was added. The host validates identity, generation, reach, inventory, equipment
fit, recipes, cooldowns, and world state before applying the same operations
single-player uses.

Menus and cameras are client-local. Opening inventory must not pause the host.
Simulation speed, pausing, saving, loading, stepping, and world reset are
host-only controls.

## Current implementation

`PlayerSession` supplies four stable player slots and directly owns each
player's body, inventory, drones, cursor/trash state, accessory cooldowns, and
host-side command runtime. Slot zero backs the legacy `g_player` and `g_inv`
references, so ordinary single-player code has no second copy of local state.
Each session has a generation counter. Packet references are
`(PlayerId, generation)`, preventing a delayed packet from a disconnected
client from acting on whoever later reuses that slot.

World material occupancy supports four independent player shapes. World
activation is a union of chunk islands: each remote player contributes a live
core around their body rather than stretching one huge rectangle between
distant players. Simulation cost therefore scales with active areas, not the
distance separating them.

The transport is nonblocking TCP. A protocol and exact-build handshake runs
before world data is accepted. A clean build identifies its Git revision; a
dirty build receives a per-build GUID, so separately compiled dirty trees
cannot accidentally claim compatibility while copying one executable to both
machines still works. The host sends a normal compressed save as the initial
snapshot, followed by explicit command/action packets, replicated overlay
state, and PackBits-compressed changed chunks around the joined player. The
high-frequency player/entity/device overlay block is PackBits-compressed as a
whole as well; mostly-unused fixed pools collapse to short zero runs. Chunk
hashing is amortized across frames. The client reports rotating hashes of the
chunks it actually holds; the authority sends the ordinary full chunk packet
whenever one differs, while a slower periodic forced refresh remains a second
repair path. A bounded send backlog prevents a slow peer from growing memory
indefinitely.

Commands carry held movement/use state plus preserved rising edges. Discrete
inventory, equipment, crafting, chest, machine configuration, copper-wire, and
circuit-wire gestures use an ordered action queue. The client predicts only its
own body movement; world cells and gameplay results are never client-authored.
Offline and hosting input uses the same commands and a local loopback action
queue rather than a trusted direct-mutation path. The main frame is correspondingly
split into `clientInputTick()`, authoritative `serverTick()`, client camera/UI,
and `clientRender()`; a joined process never enters `serverTick()`.

Enemies target the nearest connected living player. Contact damage, hostile
shots, pickups, accessories, and each player's three-drone bank use that
player’s body and inventory. Player and drone shots do not friendly-fire. Time
passes at 4x only when every connected living player rests in a valid bed.

Multiplayer rules are explicit:

- A host menu never pauses an online world; only the host may use the actual
  pause/speed controls. Client inventory, crafting, map, and device panels are
  presentation state and do not pause anybody.
- Sleeping accelerates only the day/night clock, and only while every connected
  living player is resting in a valid bed.
- Death is per player. A dead player can respawn without resetting the world or
  the other player.
- Bosses are world-owned. They choose among connected living targets, their
  transient fight state arrives in the post-snapshot entity state, and victory
  plus geological progression belongs to the host save.
- Joining mid-session spawns beside the host in a checked open location, gives
  the joining inventory its starter kit, loads the current compressed world,
  then supplies current players, enemies, projectiles, pickups, drones,
  machines, circuits, rooms, trees, torches, time, and boss progression before
  the client becomes ready.

State packets use explicit framing and scalar fields for commands and actions.
High-frequency overlay arrays currently use exact-build, size/schema-guarded
C++ blocks for efficiency. That is safe for the enforced same executable on
this Windows LAN milestone, but must become field-wise serialization before
cross-compiler or cross-platform play is promised.

## Starting and joining

Open Escape on the host and click **Host LAN**. The button shows its private IP
and the fixed port. On the other computer, open Escape, enter that IPv4 address,
and click **Join**. Windows Firewall may ask the host to allow Crucible on the
private network.

The command-line equivalents, mainly for repeatable testing, are
`crucible.exe --host` and `crucible.exe --join 192.168.x.x`.

The checked-in two-process smoke test creates a real loopback TCP peer and
asserts handshake, full snapshot load, player mapping, readiness, held command
framing, ordered discrete-action framing, and automatic repair after deliberately
corrupting a client chunk. A second test compiles two different build IDs and
proves the client is rejected before world synchronization. The production
executable also has a headless local-loopback test for placement, crafting, and
inventory transfer, and has been run as two simultaneous full processes with a
sustained established session.

## Remaining extensions

- Accept sockets for player slots two and three.
- Optional LAN discovery, NAT traversal/relay, dedicated server, and host
  migration.
- Replace ABI blocks with portable field-wise overlay encoding before
  non-identical compilers or platforms are supported.
- Add latency/loss simulation and longer soak tests for prediction and backlog
  tuning.

## Replication rule

Do not add network calls inside material behaviours. The host compares changed
interest chunks and replicates them generically. New materials, heat rules, and
fluid reactions therefore require no multiplayer code. New player-directed
actions need a validated command/action; new overlay entities need ownership,
targeting, and replicated state. Cosmetic rendering needs neither.
