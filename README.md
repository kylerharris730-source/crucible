# Crucible

A game about building machines out of real physics, on a planet you are trying
to leave.

You crash. You survive by collecting materials and refining them, and you refine
them by building working mock-ups of real mechanisms — a still, an oven, a
condenser, a furnace. Not crafting-menu abstractions: actual vessels with actual
heat flowing through them, made of pixels, which work because the simulation
works. Below you is a cave that gets more dangerous the deeper you go, and holds
materials the layers above do not.

**Status: just started.** Right now this repository is the falling-sand
simulation it is built on, and nothing else. There is no game yet.

---

## What exists today

The inherited simulation, which is essentially complete:

- 27 materials — solids, powders, liquids, gases, and machines
- A real temperature field: every cell has a temperature, heat conducts between
  neighbours, and behaviour falls out of that rather than being scripted
- Conductivity, thermal mass, and long-range conduction along good conductors
- Phase changes both ways — melting, freezing, boiling, condensing, burning
- Wetness that spreads as a bounded band rather than a single wet pixel
- Chunked dirty rectangles, so a settled world costs literally nothing

It builds and runs as a sandbox:

```bash
build.bat
```

Then `build\powder.exe`. Draw with the mouse, pick materials from the panel,
press `V` to cycle to the Heat view.

![The inherited simulation: three vessels, one boiled by fire, one chilled by
cold fire, one frozen](powder.png)

## What it needs to become a game

In rough order of how structural the change is:

1. **A world bigger than the window**, with a camera. Right now the world *is*
   the screen, at compile time. This is the point of no return.
2. **A player** that collides with the pixel grid.
3. **Machines** — and the open question of whether they are purely simulated or
   recognised-and-stabilised.
4. **Weapons that fire materials**, with modules that calibrate delivery.
5. **A cave** with depth-gated materials.
6. **Threading**, which is harder here than usual for a specific reason.

All of it, with the reasoning, is in **[DESIGN.md](DESIGN.md)**.

## Documentation

- **[DESIGN.md](DESIGN.md)** — what the game is, the open decisions, the
  technical risks, and the first milestone.
- **[INTERNALS.md](INTERNALS.md)** — the simulation in full detail: the engine,
  the physical models, and every material's numbers with the measurements that
  set them.

## Relationship to `powder`

This is a fork of [powder](https://github.com/kylerharris730-source/powder), a
finished falling-sand sandbox, and it keeps its full history — the reasoning
behind every tuned constant lives in those commit messages.

The two diverge from here. `powder` stays as the clean reference implementation
and is not maintained from this repository; the changes this project needs
(runtime world size, a camera, streaming, threading) are exactly the ones that
would destabilise it. Cherry-pick anything genuinely shared rather than trying to
keep a common core.
