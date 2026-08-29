# Multiplayer architecture

## Playable target

- Four players in cooperative survival: one host and up to three joined peers.
- One player's game is the authoritative host and owns the save.
- Another computer joins by entering the host's IPv4 address. Port 27841 is
  fixed for this first version.
- LAN/direct IP is the baseline. Discovery, NAT traversal, matchmaking,
  dedicated servers, host migration, and cross-version play are later work.
- Every peer must run the exact same build and protocol.

On an ordinary home LAN, the host's private address is sufficient. The host
machine may still need to allow Cinderlift through Windows Firewall. Guest Wi-Fi,
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

The transport holds one `Peer` per connection rather than a single socket.
Buffers, handshake progress, the assigned slot, the acknowledgement watermarks
and the chunk replica the host believes that client holds are all per peer:
two players in different caves must not share one chunk-hash table, and one
shared acknowledgement watermark would tell every client that input it never
sent had been applied, discarding its own pending movement. Held commands are
coalesced per player for the same reason. A connection may only drive the
player it was given, so a packet naming somebody else's id is discarded rather
than applied. The world half of a state packet is serialized and compressed
once per host frame and framed per peer behind its own watermarks. A fourth
joiner is told the game is full.

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
repair path. State and chunk production stop behind a small bounded send
backlog, and each poll drains a bounded batch rather than one 64 KiB fragment;
a busy scene therefore sheds intermediate snapshots instead of displaying
seconds-old state. TCP's small-packet coalescing is disabled for input.

Commands carry held movement/use state plus preserved rising edges. Discrete
inventory, equipment, crafting, chest, machine configuration, copper-wire, and
circuit-wire gestures use an ordered action queue. Every host state acknowledges
the newest command actually processed. The client restores that authoritative
body and replays only newer pending movement, so ordinary snapshots do not
rewind the player while real collision/death disagreements are still corrected.
Between packets the client also advances its replicated world, heat/fluids,
machines, actors, projectiles, drones, trees, and clock for smooth presentation.
Those results are speculative: the client never sends them to the host, and
authoritative state/chunks correct divergence. Chunk-hash repair reports the
last authoritative chunk received rather than the locally predicted contents,
so prediction does not create a self-inflicted correction loop.
Offline and hosting input uses the same commands and a local loopback action
queue rather than a trusted direct-mutation path. The main frame is correspondingly
split into `clientInputTick()`, authoritative/predictive branches of
`serverTick()`, client camera/UI, and `clientRender()`; a joined process never
enters the authoritative branch or applies gameplay actions locally.

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

Every packet is field-wise. `codec.h`/`codec.cpp` hold one visitor per struct,
and each visitor serves both directions: a `Blob` is either storing or
loading, and the loading mode fills the same references the storing mode
reads. Two hand-written halves of a codec drift, and a drifting codec corrupts
state rather than failing. Scalars are little-endian and floats are IEEE-754
bit patterns, so the format now depends on the CPU rather than on the
compiler's struct layout, padding and enum widths. Pool sizes are written and
checked, so a peer built with a different capacity is rejected explicitly
instead of reading off the end of a buffer.

**The join snapshot is still not portable.** It is an ordinary save file, and
`save.cpp` writes `Player`, `Inventory`, `Device`, `Entity` and the rest as
raw `fwrite(&x, sizeof(x))` blocks. Cross-compiler play therefore remains
blocked by the save format, not by the packets. Converting saves to the same
visitors is the remaining work, and it is worth doing for a second reason
that bites today: `PLYR` is loaded only `if (len == sizeof(Player))`, so
adding one field to the player silently discards every existing character.

## Starting and joining

Open Escape on the host and click **Host LAN**. The button shows its private IP
and the fixed port. On the other computer, open Escape, enter that IPv4 address,
and click **Join**. Windows Firewall may ask the host to allow Cinderlift on the
private network.

The command-line equivalents, mainly for repeatable testing, are
`cinderlift.exe --host` and `cinderlift.exe --join 192.168.x.x`.

For iteration on one computer, run `multiplayer_test.bat`, optionally with a
client count: `multiplayer_test.bat 3` launches a full four-window session.
It runs the same built executable over loopback, labels the windows LOCAL HOST
and LOCAL CLIENT n, and creates a small deterministic arena with inventory
stacks. This exercises the
real socket, snapshot, prediction, correction, inventory, and rendering paths
without copying a build to another machine. Note that keyboard input is gated
on the foreground window: `GetAsyncKeyState` reports the keyboard rather than
one window, so without that gate every local window would move as one (and an
unfocused Cinderlift would walk your character while you typed elsewhere). The automated two-process smoke
test remains the fast headless check; the launcher is the playable check.

The checked-in two-process smoke test creates a real loopback TCP peer and
asserts handshake, full snapshot load, player mapping, readiness, held command
framing, authoritative command acknowledgement, ordered discrete-action framing,
and automatic repair after deliberately corrupting a client chunk. A second test
compiles two different build IDs and
proves the client is rejected before world synchronization. A third runs four
real processes: it asserts three sockets are accepted into three distinct
slots, that each connection drives only the player it was given (one client
deliberately sends a command impersonating another player, which must never
reach the host), that no two slots share an aim, and that a fourth joiner is
refused rather than admitted or left hanging. The production
executable also has a headless local-loopback test for placement, crafting, and
inventory transfer, and has been run as two simultaneous full processes with a
sustained established session.

## Remaining extensions

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
