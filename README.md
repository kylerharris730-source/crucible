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

The left catalog has materials and devices. `Tab` opens the creative inventory,
`X` enters Circuit Wire mode, `F` enters the one-cell copper wiring tool, and
holding `Q` shows the brush footprint while also making the wheel adjust brush
size.

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
