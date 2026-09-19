# Cinderlift

Cinderlift is a game prototype about building machines out of real pixel physics
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

Then launch `build\cinderlift.exe`.

For the player-facing distribution, build `build\cinderlift-launcher.exe` with
`build_launcher.bat`. Players download the launcher once; it installs the latest
tagged GitHub Release into `%LOCALAPPDATA%\Cinderlift`, verifies updates before
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

## powderlike

A second executable over the same simulation: the falling-sand toy with the
game taken off it. No character, no camera, no minimap, no inventory -- one
fixed screen, the material palette, and the physics.

```bash
build_powderlike.bat
```

Then launch `build\powderlike.exe`. `mingw32-make powderlike` does the same
thing. It is **not** a release artefact: nothing in the launcher or the CI
workflow builds it, and it has no version resource and no saving.

It links every `src/*.cpp` except `main.cpp`, plus its own `src/powder/main.cpp`
-- the same shape the test suite uses. So it inherits simulation changes for
free: a change to how sand piles or what titanium melts at lands in both
programs the next time each is built. The material palette is shared through
`src/brushes.h` for the same reason.

**World size** is the feature the game does not have. There is no camera, so a
smaller cell does not zoom out -- it makes the world bigger. Normal is two
pixels a cell and a 512x384 world, Half is one pixel and 1024x768, Quarter is
half a pixel and 2048x1536 -- 3.15 million cells and 3,072 chunks, all of them
live every frame. Changing it clears the world, because the walls that hold the
material in move.

Drawing runs on the simulation's thread pool, so even Quarter draws in about
4 ms; the panel reports sim and draw milliseconds separately so you can see
which half a slow frame is paying. `tools/powderbench.cpp` reproduces a water
pour at Half headless, with a sampling profiler and a world hash for checking
that an optimisation changed nothing.

Material also moves a fixed number of *cells* per frame, so at Quarter
everything crosses the screen four times slower in seconds -- that is what the
speed control is for.

Left button paints, right button erases, the wheel sizes the brush (or scrolls
the palette when it is over it), `Space` pauses, `.` steps one frame, `[` and
`]` size the brush, `V` cycles the view, `C` clears, `1`/`2`/`3` pick the world
size, and `Esc` quits.

## Documentation

- [DESIGN.md](DESIGN.md) - game direction, open decisions, and long-term risks.
- [INTERNALS.md](INTERNALS.md) - simulation models and material behavior.
- [CIRCUITS.md](CIRCUITS.md) - circuit wires, signals, combinators, and
  circuit-controlled filters.
- [LOGISTICS.md](LOGISTICS.md) - chests, pipes, spouts, drains, and item flow.
- [SPRITES.md](SPRITES.md) - sprite conventions and phase language.

## Relationship to powder

Cinderlift is a fork of
[powder](https://github.com/kylerharris730-source/powder), the finished
falling-sand sandbox that supplied the simulation foundation. The projects now
diverge: Cinderlift adds a scrolling world, player systems, machines, automation,
and game progression.

`powderlike` above is that sandbox rebuilt as a Cinderlift derivative rather
than kept as a separate project, so it tracks the simulation instead of
drifting from it.
