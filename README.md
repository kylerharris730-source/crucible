# Powder

A pixel-based falling-sand physics sim. C++11, runs on Windows, macOS and Linux.

## Build & run

**Windows** — no dependencies, just a compiler:

```bat
build.bat
```

Then `build\powder.exe`. (`mingw32-make` and `mingw32-make run` also work.)

**macOS / Linux** — needs SDL2:

```bash
brew install sdl2            # macOS
sudo apt install libsdl2-dev # Debian/Ubuntu

./build.sh
```

Then `./build/powder`. (`make` and `make run` also work.)

### The two shells

The simulation is plain, portable C++11 with no platform code in it at all.
Only the outermost layer — window, input, timing, blit — knows what OS it is
on, and there are two interchangeable versions of it:

| Backend | File | Notes |
|---|---|---|
| `win32` | `src/main.cpp` | Win32 + GDI. **Default on Windows.** No external dependencies. |
| `sdl` | `src/main_sdl.cpp` | SDL2. **Default everywhere else**, and usable on Windows too. |

`make BACKEND=sdl` builds the portable shell on Windows as well (point it at an
unpacked SDL2 dev package with `SDL_CFLAGS=-IC:/SDL2/include
SDL_LIBS=-LC:/SDL2/lib`). The Win32 shell only exists to keep the
zero-dependency Windows build; the two are deliberately kept behaviourally
identical, sharing their layout constants and palette through `src/panel.h`, so
this is one program with two backends rather than two diverging programs.

Two things the SDL shell does that the Win32 one does not, both because laptops
and Retina displays make them matter: it **shrinks the window to fit** a display
too small for the full 1174×768 (the Win32 one just hangs off the bottom edge),
and it renders through a fixed logical coordinate system, so the window is
freely resizable and stays crisp on a Retina Mac. Because all the layout and
hit-testing works in those logical coordinates, none of that code has to know
the window was resized.

Text is drawn from a 5×7 bitmap font baked into `src/font8.h` rather than via
SDL2_ttf. That avoids a second dependency and a font file to locate at runtime
(font paths differ per OS, which is exactly what this port exists to stop
caring about), and it suits a pixel sim: every glyph lands on exact pixel
boundaries at any integer scale.

## Controls

Everything is picked from the **tool panel** down the left side: click a
material or tool swatch to select it, use the size stepper (or the mouse wheel),
and toggle Overwrite / View / Pause / Clear. The selected brush shows a gold
highlight; toggles show their live state in the label. Keyboard shortcuts still
work as accelerators but are no longer needed — new materials just get a button.

| Input | Action |
|---|---|
| Left mouse (over the sim) | Draw with the current material / apply the current tool |
| Right mouse (over the sim) | Erase |
| Panel swatch, or `1`–`9` / `B` | Pick a material (Clone and Void are panel-only) |
| Panel Heat/Cool, or `H` `J` | Heat / Cool tool (warms or chills under the brush) |
| Panel Erase, or `0` / `E` | Eraser |
| Panel size `-`/`+`, wheel, `[` `]` | Brush size |
| Panel Overwrite, or `O` | Toggle whether the brush replaces existing cells |
| Panel View, or `V` | Cycle view: Glow → Material → Heat |
| Panel Pause, or `Space` | Pause |
| `.` | Single step while paused |
| Panel Clear, or `C` | Clear |
| `Esc` | Quit |

With **Overwrite off**, the brush only fills empty space, so you can pour a
material into a scene without carving through what is already there. Erasing
always works regardless.

There are three **views**, cycled with `V` or the button:

- **Glow** — materials with a heat glow over anything hot. The glow alpha is
  capped so even white-hot material stays recognisable rather than washing out.
- **Material** — materials only, no heat shown at all. Use it when the glow is
  in the way of reading the sim.
- **Heat** — temperature as false colour, for inspecting where heat is.

The **Heat**/**Cool** tools plus the heat view are the way to explore the
temperature system: heat a pool until it boils to steam, watch the steam
billow up and condense, melt stone into lava, or set a wooden wall alight. The
stats at the foot of the panel show fps, sim time in ms, live cell count, and
how many chunks are actually being simulated — that last number is the useful
one when judging whether a change costs anything.

### Adding a material to the picker

The palette is data-driven: one row in the `BRUSHES[]` table in `panel.h` adds
a labelled button, and its swatch colour is pulled straight from the material's
own palette, so it always matches what lands in the world. That table is shared
by both shells, so one edit adds the button on every platform. The panel widens the
window by a fixed strip and the sim blits to the right of it at a clean integer
scale, so the window→cell mapping stays a plain divide.

## How it's put together

| File | Role |
|---|---|
| `src/common.h` | Types, xorshift32 RNG |
| `src/materials.*` | The material table and the colour LUT |
| `src/world.*` | Grid, chunk system, all movement, moisture and heat rules |
| `src/render.*` | Cell → pixel, heat glow, heat view |
| `src/panel.h` | Layout constants, tool ids, the `BRUSHES[]` palette — shared by both shells |
| `src/main.cpp` | Win32/GDI shell: window, input, timing, blit |
| `src/main_sdl.cpp` | SDL2 shell: the same, portably |
| `src/font8.h` | 5×7 bitmap font, for the SDL shell's panel text |

Everything above `panel.h` in that table is free of platform code entirely —
which is why the macOS port only had to add a shell, not touch the physics.

### The three things that make it fast

**Chunked dirty rectangles.** The grid is split into 32×32 chunks, each
tracking the smallest box that had activity. Only those boxes get simulated.
A settled pile costs zero — 91,000 cells at rest benchmark at 0.00 ms.

**4-byte cells.** `{mat, moisture, tint, flags}` packs 16 cells per cache line,
and the update loop reads `mat` and `moisture` together.

**Colour lookup table.** Rendering a cell is
`LUT[(mat<<8) | (moisture & 0xF0) | (tint>>4)]` — three shifts and a load, no
branches or multiplies in the per-pixel loop.

Two smaller tricks: a 7-bit **frame stamp** in `flags` marks "already handled
this frame" without clearing a flag array each frame, and the permanent **wall
border** means no movement rule needs a bounds check.

The stamp has to be wider than one bit, and the reason is worth knowing because
it is easy to reintroduce. A single parity bit assumes every cell is visited
every frame — which is exactly what dirty rectangles stop doing. A cell that
goes unvisited for one frame returns with a parity that aliases to "already
handled", `updateCell` early-returns forever, and nothing dirties it again. The
visible symptom was sand stranded in mid-air under an overhang. Seven bits push
aliasing out to 128 frames, and `updateCell` dirties anything it skips, so even
that case heals in one frame.

### Wetness

Sand and dirt carry a `moisture` byte rather than having separate "wet"
material ids. That gives a smooth gradient instead of a binary flip, makes
diffusion plain arithmetic, and doesn't double the material count as more
materials arrive. Moisture drives both colour and cohesion — wet sand piles
steeper, wet dirt packs into mud.

Three mechanics, all O(1) per wet cell per frame and all riding inside the
normal update pass, so there is no separate diffusion sweep:

- **Absorption** — a grain touching water consumes that cell and gains
  `MOISTURE_UNIT` (64). Capacities are multiples of that, so water is never
  split and lost. Sand holds 128, dirt 192.
- **Percolation** — each wet cell trades moisture with *one* randomly chosen
  neighbour, biased 5/8 downward, 1/8 up for capillary rise. Averaged over
  frames this looks like diffusion for a fraction of the cost.
- **Wicking** — a transfer only happens into a cell drier than this one by
  more than a threshold, `wick << direction`, where the shift makes downward,
  sideways and upward reach fall in a 4:2:1 ratio.

That last rule is the one that bounds the wet band, and it is worth
understanding before changing it. Because every direction is gradient driven,
at rest two neighbours differ by about `wick`; moisture therefore falls off
*linearly* with distance from the water and reaches zero after roughly
`capacity / wick` cells. Beyond that the material stays dry however long water
sits on it. Measured bands: sand 12 cells, dirt 19, both stable out to 14k
frames.

Turn `wick` **down** for a deeper band, up for a shallower one. (The transfer
uses a `>>1`, which sets a floor on the resting gradient of about `1<<shift`
regardless of `wick`; that floor is what caps how far hydration can reach, so
if you ever want a *much* deeper band than tuning `wick` gives, that shift is
the thing to change.)

The trap to avoid: if any direction drains without consulting the receiving
cell — as an earlier "drain everything above a retention level" rule did — each
cell shoves its excess into the next, the front never runs out of anywhere to
go, and water tunnels to the floor forever. That version was also *slower*,
because a moisture field that never reaches equilibrium never lets its chunks
sleep.

Material near saturation with open space beneath it sheds a droplet, returning
exactly one unit — so water still drains through the underside of a *thin*
layer and pools again below, while a thick layer never gets wet enough that far
down to trigger it. Absorb and drip are exact inverses; total water is
conserved, and a pool stops draining once the band saturates rather than
slowly vanishing into the ground.

### Liquids

Liquids fall at a flat **one cell per frame**. All of the naturalness comes from
what happens once they land.

**Pressure.** A liquid cell that cannot fall may look straight *through* its own
liquid to find somewhere to go, and gets extra sideways reach for each cell of
the same liquid stacked overhead (up to `PRESSURE_MAX`). Both halves matter:

- Without see-through, only the outermost cell of each row can ever move, so a
  pile erodes one edge layer at a time and holds a tall blocky core while thin
  films race away along the floor.
- Without the depth term every row drains at the same rate, which holds
  dead-vertical walls — a poured pile settled into a flat-topped mesa. Buried
  liquid throwing further is what makes a body slump into a dome.

The scan sees through the cell's **own material only** — anything else stops it
dead, so containers still hold.

> **Fall acceleration was tried and removed.** Velocity in 1/8-cell units in the
> spare `moisture` byte, plus splash on impact. It worked as designed — a stream
> stretched and shed droplets — but that read as wrong, so it went. Worth knowing
> if it looks like an obvious missing feature. Two things it left behind: liquids
> no longer touch `moisture` at all, and the pressure rules used to be gated on
> `impact == 0` to stop a falling body spraying sideways in mid-air. That gate is
> gone, and it is not needed: a free-falling body's cells all descend together,
> so none of them are ever blocked, and the lateral code never runs. Measured, a
> dropped 53-wide blob stays exactly 53 wide the whole way down.

One thing to know: **a pool never fully sleeps.** That is free-surface
evaporation, not a leak in the liquid code — every exposed liquid cell keeps
itself dirty so it can keep ticking (disable evaporation and the same basin
sleeps at frame 2). The cost stays confined to that thin surface strip:
a settled full-width pool measures about 0.04 ms/frame.

### Heat and phase changes

Every cell — air and walls included — carries a `temp` byte of degrees, kept in
its own array (`World::temp`) rather than inside `Cell`, so the movement rules
never pay for heat they do not read. Ambient is 20, water boils at 100, fire
burns near 230, so the whole useful range fits in a `u8` without scaling.

Conduction: each off-ambient cell exchanges heat with **all four** neighbours
each frame. The rate is set by the poorer of the two materials' `heatCond`, so
metal-on-metal is fast while a hot cell next to air barely bleeds — **air
conducts poorly on purpose**, or a single flame would heat the whole room in
seconds. Each exchange conserves energy and is capped at half the gap, so a
temperature can never cross the pairwise average; that keeps every value in
`[0,255]` regardless of the order neighbours are visited, with no second buffer.

**Thermal mass** (`heatMassShift`, a right-shift: 0 = normal, 3 = holds 8×)
splits apart two things `heatCond` alone conflates — how fast a material
*delivers* heat, and how fast it *loses its own*. The full amount of heat moves,
but each side's temperature changes in proportion to its own mass. Lava uses
this: it conducts at full rate (so it still melts, lights and boils things
normally) while cooling eight times slower, which is what keeps a drawn puddle
molten for ~9 s instead of ~1 s. It stands in for latent heat, which for rock is
enormous, and is deliberately not energy-conserving.

Two things about it are load-bearing. **Both sides must be scaled by their own
mass, not just the cell being updated** — every cell runs the conduction loop,
so scaling only the updater lets a neighbouring stone pull heat out of lava at
full rate on its own turn and cancel the effect entirely. And the remainder is
carried **stochastically** rather than truncated: integer division rounds small
transfers to zero, which would turn a heavy material into a perpetual heat
source that never cools and never lets its chunk sleep.

Turning conductivity down instead is the obvious-looking alternative and it does
not work. Transfer uses `min(a, b)` and air is 12, so any value above that
leaves loss-to-air unchanged; going below it does slow the bleed, but then lava
can no longer heat anything up to its ignition or boiling point.

Where heat *leaves* matters as much as how it spreads, and this is the part that
was reworked: only **air** dissipates heat quickly (it stands in for an open
room). Solids and liquids barely drift on their own — they shed heat almost
entirely by conducting it into a cooler neighbour, air included. That is what
lets heat conduct the length of an iron bar or up the sides of an iron bowl to
boil the water inside; an earlier model bled a little from *every* cell every
frame, so the far end of a bar never warmed no matter how conductive it was. The
slow solid/liquid drift (`SOLID_COOL`) exists only to guarantee a hot region
eventually reaches ambient and lets its chunk sleep, rather than glowing faintly
forever. Gases sit in between (`GAS_COOL`): a puff of steam cools faster than a
solid but still rides a good way up before condensing.

The important performance property: a cell at ambient does *nothing* — one
compare and out. Heat only costs anything where something is actually hot, and a
hot region keeps itself awake exactly until it has cooled. The flip side is that
a large, deliberately-heated region stays active until it cools, which is the
one case that keeps many chunks awake at once (see the benchmark).

Phase changes are entirely table-driven — no code per material:

- `boilTemp` / `boilsTo` — at or above this temperature, become that. Boiling
  costs `LATENT_HEAT`, so one hot cell simmers a pool instead of flashing it all
  to steam in a single frame.
- `coolTemp` / `coolsTo` — below this, become that. Steam condensing back to
  water and fire burning out are both just this.
- `quenchedBy` — touching this material destroys the cell and dumps its heat
  into the quencher. Fire hitting water dies and raises steam.
- `spawnTemp` — temperature when placed by hand (fire and steam start hot).

Free-surface **evaporation** is separate from boiling and lives in
`updateEvaporation`: a liquid cell with an empty neighbour has a small chance to
turn to vapour, rising with the square of how far above ambient it is. That
gives the occasional wisp at room temperature and a hard steam at heat, with no
special-casing between the two — and only surface cells pay for it, so a still
pool keeps just a thin live strip rather than waking its whole body of water.
Because of this, water alone is no longer exactly conserved; **water + steam**
is (they interconvert through a rise-and-condense cycle).

**Fire** and **steam** are both `KIND_GAS`: the same movement code as a liquid
with the vertical sense flipped, so they rise and spread along the ceiling.
Gases also invert the density test in `tryMove`, which is what lets steam bubble
up *through* water. Each gas has a `jitter` (0–255) giving it a per-frame chance
to wander sideways instead of rising, which is what makes steam and smoke billow
into a spreading plume rather than shoot up in a rigid column — steam jitters a
lot, fire less.

**Steam** has a very low `heatCond` and condenses only once fairly cool, so it
rides a long way up before turning back to water. Two things keep it from
cooling too fast: the "round the rate up to at least 1 degree" floor in the
conduction code is applied *only* to real conductors (`cond >= 40`), so a
near-insulator like steam conducts its true (often zero) amount into passing
air; and its cooling toward ambient uses the middling `GAS_COOL` rate. Tune
`GAS_COOL` up for steam that condenses sooner, down for steam that climbs
higher.

**Wood** ignites — via `igniteTemp` — either by being heated past its ignition
point (lava, the heat tool, a big enough blaze nearby) *or* simply by touching
fire, at `FIRE_SPREAD` chance per adjacent flame per frame. The contact path is
what makes a burn front travel reliably through a plank; heat diffusion alone
is too marginal to sustain it, and
contact also lights wood that a flame is only resting on before it rises away.
Ignition lights the new flame *hot* (combustion releases heat, unlike boiling
which absorbs it), so the front keeps itself going.

**Stone** melts to **lava** at 220°, and lava freezes back to stone below 100°.
The gap between those is hysteresis — without it a cell sitting right at the
melting point would flicker between states every frame. Lava spawns at 255 (the
top of the `u8` scale) and carries a thermal mass of 3, which together are what
keep it molten for ~9 s rather than ~1 s; freezing at 100 also guarantees any
lava is still hot enough to boil water. It sets wood alight on contact, the same
way an open flame does, so ignition is instant instead of waiting on a
conduction ramp. **Iron** is a static
solid with near-perfect conductivity, so a bar of it carries a flame's heat from
one end to the other; stone, by contrast, is a poor conductor, so heat stays
local to where it is applied.

### Clone and Void

Two machines rather than substances. Both are static blocks that act on their
neighbourhood, and both are careful to dirty themselves *only* when they
actually did something — so an idle dispenser or drain lets its chunk fall
asleep, and wakes automatically when anything moves or is painted next to it.

**Clone** copies the first material it touches and then produces it forever.
Paint a clone, touch it with water and you have a dispenser; touch it with fire
and you have a permanent flame source. The source can be removed afterwards and
the clone keeps going — that is the whole point of latching once.

- The latched material id lives in the clone's `moisture` byte, which is dead
  weight for a material with no `capacity`. That keeps `Cell` at 4 bytes. A
  `static_assert` pins `MAT_COUNT <= 16`, because the colour LUT is indexed by
  `(moisture & 0xF0)` — clone's flat colour makes it harmless today, but the
  assert stops that becoming a silent rendering bug if ids ever pass 16.
- Emitted cells get the material's `spawnTemp`, which is load-bearing for fire:
  fire dies below its `coolTemp`, so a clone emitting it at ambient would
  produce flames that vanish the same frame.
- **Latching scans all 8 neighbours; emission uses only the 4 orthogonals.** The
  wider catch matters because the scan runs bottom-to-top: a cell *below* the
  clone has already moved by the time the clone takes its turn, so a lone drop
  falling past would otherwise slip through unseen. Emission stays orthogonal so
  material cannot squeeze through a diagonal gap between two solid blocks.
- Emission is deliberately not rate-limited — a dispenser should keep up with
  whatever drains it, and it is self-limiting anyway: a clone loaded with sand
  buries itself and stops, while water or fire flows or rises away.
- Walls, other clones and voids are not cloneable, so you cannot wall off the
  box or build self-replicating machines.

**Void** destroys anything orthogonally adjacent — a drain for terminating a
waterfall or holding a basin at a steady level. It leaves temperature alone, so
voiding lava removes the matter but not its warmth; the leftover heat
dissipates through the air normally rather than the drain acting as a perfect
heat sink.

The one hard rule: **void never deletes `MAT_WALL`.** The movement rules do no
bounds checking whatsoever — they rely on the border ring of wall to stop a cell
walking off the grid — so a void able to chew through it would corrupt the sim.

## Adding a material

1. Add an id to `MatId` in `materials.h`, above `MAT_COUNT`.
2. Add a row to `MATS[]` in `materials.cpp` (rows must stay in id order).
3. Only if it needs behaviour no existing `MatKind` covers, add a rule in
   `world.cpp`.

Most materials need steps 1 and 2 only — even ones that boil, freeze, burn out
or quench, since those are all table fields. Knobs worth knowing: `slideDry` /
`slideWet` set a powder's angle of repose (high = flows like dry sand, low =
stacks in steep columns); `dispersion` is how fast a liquid or gas spreads;
`density` decides what sinks or bubbles through what; `capacity` / `wick` set
how much water the material takes up and how thick the wet band is; `heatCond`,
`heatMassShift` and the `boil*` / `cool*` / `ignite*` / `quenchedBy` /
`spawnTemp` fields cover heat. Reach for `heatMassShift` when something should
stay hot for a long time *without* becoming a worse heater — turning `heatCond`
down does the second thing as well as the first.

Anything that changes a cell must call `dirtyPoint()`, or the chunk will fall
asleep and the change will not propagate. Conversely, anything that changes
a cell *without* it being real motion should **not** dirty, or the world will
never go quiet.

If a material can move **more than one cell in a single step** (as water does,
via `dispersion`), dirty the whole swept path with `dirtyArea()`. `tryMove`
only dirties the two endpoints, so anything resting along the way silently
loses its support and never wakes up.
