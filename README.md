# Crucible

Crucible is a game prototype about building machines out of real pixel physics
on a planet you are trying to leave. Heat, phase changes, fluids, conductors,
and materials are the building vocabulary rather than background decoration.

## Status

**Playable systems prototype.** The current work is about making the existing
systems coherent and useful as a progression game: exploration, refining,
automation, electricity, and eventually an escape machine.

## Current prototype systems

- A scrolling chunked world with adaptive active margins, a brief grace period
  for recently visible chunks, and sealed rooms that can stay loaded.
- A player with collision, temperature damage, hotbar, inventory, equipment,
  crafting, a searchable creative inventory, saves, and sandbox play.
- Materials with solid, powder, liquid, gas, frozen, and molten behavior;
  thermal conduction and phase changes are simulated directly.
- Item logistics: chests, item pipes, pipe crossovers, spouts, drains, placers,
  miners, filters, and block-watching devices.
- Physical electrical pulses in conductors. Pulses branch, create heat, melt
  weak conductors, and clean up destructive overloads.
- Separate circuit wires for named integer signals, including material signals,
  virtual channels, and Constant, Arithmetic, and Decider Combinators.

## Build and run

Run:

```bash
build.bat
```

Then launch `build\crucible.exe`.

For the player-facing distribution, build `build\crucible-launcher.exe` with
`build_launcher.bat`. Players download the launcher once; it installs the latest
tagged GitHub Release into `%LOCALAPPDATA%\Crucible`, verifies updates before
replacing files, and can launch the installed version offline when GitHub is
unavailable. See [LAUNCHER.md](LAUNCHER.md) for publishing and self-update details.

The left catalog has materials and devices. `Tab` opens the creative inventory,
`X` enters Circuit Wire mode, `F` enters the one-cell copper wiring tool, and
holding `Q` shows the brush footprint while also making the wheel adjust brush
size. `Ctrl+Z` reverses the most recent build or dig stroke (materials,
background walls, copper wiring, and placed devices), including its survival
inventory cost or mining drops.

Multitools store energy and recharge continuously. Each installed module is a
complete shot with its own energy cost and cadence; occupied module slots fire
from left to right and wrap. A shot waits when the battery cannot pay its cost
rather than skipping ahead. Bounce is cheap rapid suppression, Shot is the
general beam, Blast trades charge and cadence for a crater, Homing is a slow
expensive seeker, and Teleport moves its owner to the projectile's last safe
point on impact. Quick-tap `R` cursor teleport remains available.

The window is resizable. The game keeps its native aspect ratio and letterboxes
as needed so UI hit targets and the visible world remain stable. Press `F11` to
toggle borderless fullscreen. The pause menu has a persistent 80--120% UI scale
setting for interface text and item artwork; 100% uses the compact 34-pixel icon
baseline while leaving click targets comfortably sized.

## Documentation

- [DESIGN.md](DESIGN.md) - game direction, open decisions, and long-term risks.
- [INTERNALS.md](INTERNALS.md) - simulation models and material behavior.
- [CIRCUITS.md](CIRCUITS.md) - circuit wires, signals, combinators, and
  circuit-controlled filters.
- [LOGISTICS.md](LOGISTICS.md) - chests, pipes, spouts, drains, and item flow.
- [SPRITES.md](SPRITES.md) - sprite conventions and phase language.

## Relationship to powder

Crucible is a fork of
[powder](https://github.com/kylerharris730-source/powder), the finished
falling-sand sandbox that supplied the simulation foundation. The projects now
diverge: Crucible adds a scrolling world, player systems, machines, automation,
and game progression.
