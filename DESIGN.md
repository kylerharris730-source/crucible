# Cinderlift — design notes

A game about **building machines out of real physics**, on a planet you are
trying to leave.

This is a working document, not a spec. It exists so the reasoning behind
decisions survives, in the same spirit as [INTERNALS.md](INTERNALS.md) — which
documents the simulation this project inherits from
[powder](https://github.com/kylerharris730-source/powder).

## The pitch

Crashed on an alien planet. Build back up, and escape.

You survive by **collecting materials and refining them**, and you refine them by
building working mock-ups of real mechanisms — a still, an oven, a condenser, a
furnace. Not crafting-menu abstractions: actual vessels with actual heat flowing
through them, made of pixels, which work because the simulation works.

Below you is a cave. It gets more dangerous the deeper you go, and each layer
holds materials the layers above do not. That is both the pressure and the
reward.

## Why this and not "Noita but easier"

Noita's depth is alchemy and combat — material reactions as a spell system.
This simulation's depth is **thermal engineering**, which Noita has nothing like:
a continuous temperature field, conductivity, thermal mass, insulators,
distillation. Nobody builds a still in Noita.

So the game to build here is the one about *machines*, not the one about
fighting. That is also a far less crowded niche than pixel roguelikes.

**Closest prior art is Oxygen Not Included**, not Noita — real thermodynamics as
the core system, machines, survival. Study it mainly for what it gets wrong: its
thermal model is close to opaque without overlays, and late-game colonies grind
to a halt. Both of those are live risks here (see below).

## The loop

```
depth  →  materials  →  machines  →  capability  →  more depth
```

Every arrow answers "why bother" for the one before it. This is the whole game;
everything else is content hung off it.

## Open design decisions

### 1. Are machines simulated, or recognised?  ← the big one

**Simulated** (what the sandbox does today): a still works because heat genuinely
flows through graphene into mercury. This is the magic and nothing else has it.
It is also fragile — one rock through the condenser and it silently stops, and
players will build things they cannot reproduce.

**Recognised**: the game detects a valid arrangement (sealed vessel + heat source
+ condenser) and blesses it with guaranteed throughput, while still *showing* the
simulation. Keeps the "I built this out of real parts" feeling, loses the
fragility. This is what most games in this space settle on.

Not decided. **The first prototype should be built to answer this question**, not
to look good. If placing a still is satisfying, stay simulated as long as
possible. If it is fiddly, move to recognise-and-stabilise early — it is much
worse to discover that after building content on the other assumption.

### 2. Weapons fire materials

Rather than a parallel weapon-crafting tree, a launcher fires **whatever you load
it with**: an LN2 canister, molten copper, plasma. Modules calibrate delivery —
spread, velocity, payload size, fuse delay, arc.

This collapses collection, refining and combat into *one* system. A frost grenade
is not a separate item, it is "LN2 + dispersal module". It also caps module
complexity at something comprehensible: you are calibrating a delivery mechanism,
not composing arbitrary spell logic, which is exactly the part of Noita that is
impenetrable to new players.

### 3. Progression gates are already in the material table

The materials have constraints that *interact*, and those constraints are the
tech tree:

| material | strength | limit |
|---|---|---|
| Copper | conducts furthest | melts first (175 °C — lava melts it) |
| Iron | survives lava | melts at 200 °C (a heater kills it) |
| Graphene | no melting point at all | — |
| Rubber | best insulator in the game | fails at 100 °C |

**Rubber's ceiling is a designed gap, not an oversight.** There is deliberately
no high-temperature insulator yet. That is layer 3: you physically cannot build a
furnace hot enough for the next tier until you go deeper and find one.

Map the ladder onto depth and the progression writes itself.

### 4. The escape should be a machine, not a boss

If the finale is "build the fuel refinery that gets you off the planet", the last
thing you do is the same verb as everything else. A boss fight at the end of a
building game always feels bolted on.

## Technical risks

### Cost scales with activity, and this is a game about causing activity

The inherited engine's entire performance model rests on most of the world being
**asleep** — settled chunks cost literally zero, which is why 91,000 cells at
rest benchmark at 0.00 ms.

A machine-building game is a game about making more things happen at once. A base
with five running stills, a furnace and a lava tap is a base where none of those
chunks ever sleep — and free-surface evaporation already keeps every exposed
liquid awake by design. **The late game is the performance problem**, and it
arrives exactly when players are most invested. This is the same wall Oxygen Not
Included hits.

Mitigations, roughly in order of appeal:

- Threading (see below).
- Recognise-and-stabilise machines can also *stop simulating* their interior.
- Sleep heuristics for steady-state systems.

### Threading is harder here than in Noita, for a specific reason

Noita parallelises by four-colouring the chunk grid so no two concurrently
updating chunks are adjacent. That is safe because a cell only ever moves one
cell per update.

**This simulation breaks that assumption.** Liquid lateral scans reach
`dispersion + pressure` (up to ~15 cells) and graphene's `heatSpread` is **28** —
nearly a full chunk in either direction, so a cell can reach clean past its
neighbour into the chunk beyond. One chunk of separation is not enough.

Options: a wider guard band, larger chunks, or special-casing the long-range
paths. Worth solving early rather than after content depends on the timings.

### Other known work

- **World bigger than the window.** `SIM_W`/`SIM_H` are compile-time constants
  and the world *is* the screen. Camera, world coordinates and streaming touch
  nearly everything. **This is the point of no return** — everything before it
  leaves the inherited sandbox intact.
- **A player that collides with pixels.** Sub-pixel position, collision queries
  against the grid, and an answer for "the ground under you just became lava".
- **Saving.** A base you return to has to persist. 4-byte cells help; a large
  world is still a real serialisation problem.
- **Rigid bodies**, if objects rather than terrain are ever needed. Noita does
  marching squares → simplify → triangulate → Box2D. Possibly skippable for a
  long time in a machine game.
- **Building tools.** A straight-line tool is the minimum. The moment someone
  builds a still they like, they will want to save and re-place it — blueprints
  are core to Factorio for exactly this reason. Not v1, but it affects how
  structures are stored.

## First milestone

Deliberately not the crash, the story, or most of the cave:

> One cave, three shallow layers. Two machines — an oven and a still. One
> launcher with two modules. A single gate: you need distilled *something* to
> survive layer 3.

That is enough to answer the only question that matters early: **is building a
machine to unlock the next layer actually fun?** If yes, the rest is execution.
If it is fiddly and you find yourself fighting pixel placement, that is decision
#1 answering itself — in a month rather than a year.

## Inherited from powder

The simulation, essentially complete: 27 materials, a real temperature field with
conductivity and thermal mass, phase changes, wetness, chunked dirty rectangles.
See [INTERNALS.md](INTERNALS.md) for every number and the measurements behind it.

The reference sandbox stays at
[kylerharris730-source/powder](https://github.com/kylerharris730-source/powder)
and is not maintained from here. The two diverge; cherry-pick anything genuinely
shared rather than trying to keep a common core.
