# Multiplayer architecture

## First playable target

- Two to four players in cooperative survival.
- One player's game is the authoritative host and owns the save.
- Another computer joins by entering the host's IPv4 address and port.
- The first transport milestone is LAN/direct IP. Discovery, NAT traversal,
  matchmaking, dedicated servers, host migration, and cross-version play are
  explicitly later work.
- Every peer must run the exact same build/protocol version.

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

Clients send input/intent, never results. A client asks to use the selected
item at an aim point; it does not announce that a block disappeared or an item
was added. The host validates reach, inventory, cooldown, and world state, then
performs the same operation single-player uses.

Menus and cameras are client-local. Opening inventory must not pause the host.
Simulation speed, saving, loading, and debug stepping are host-only controls.

## Current foundation

`PlayerSession` supplies four stable player slots. Slot zero backs the legacy
`g_player` and `g_inv` references, so converting ownership can proceed system
by system without maintaining two copies of the local player.

Each session has a generation counter. Future packet references are
`(PlayerId, generation)`, preventing a delayed packet from a disconnected
client from acting on a new player who later reused the same slot.

World material occupancy supports four independent player shapes. The common
single-player path checks only the active slot, preserving the movement-loop
cost until more players actually exist.

World activation is a union of chunk islands. The local camera establishes the
primary core and its adaptive activity fingers; each remote player contributes
another core around their body. The world never constructs a bounding box
between distant players, so simulation cost scales with the number of active
areas rather than the distance separating them.

## Next implementation slices

1. Introduce a fixed-tick `PlayerCommand` representation and route local
   movement, aiming, using, interaction, crafting, and inventory operations
   through the same host command queue remote players will use.
2. Split authoritative simulation ticking from local UI/input/rendering while
   retaining an in-process single-player host.
3. Make enemy targeting, pickups, doors, drones, accessories, and projectile
   ownership operate on `PlayerId` rather than the slot-zero aliases.
4. Add a versioned packet encoder and direct-IP host/join handshake. Do not
   transmit raw C++ structs: padding and later field additions are not a wire
   format.
5. Send an initial compressed snapshot, then measure and transmit compressed
   changed-chunk and overlay-state updates. Add sequence numbers, acknowledgments,
   periodic hashes, and targeted chunk resynchronization.
6. Decide cooperative rules for sleeping, death/respawn, boss rewards, pausing,
   disconnect grace, and joining an in-progress world.

## Replication rule

Do not add network calls inside material behaviors. The host records or compares
changed active chunks and replicates those changes generically. New materials,
heat rules, and fluid reactions should therefore require no multiplayer code.
New player-directed actions need a command and validation; new overlay entities
need owner/target and replicated state. Cosmetic rendering needs neither.

