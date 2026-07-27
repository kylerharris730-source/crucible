# Simulation internals and material reference

Everything specific about how the simulation actually works: the engine, the
physical models, and the per-material numbers with the measurements behind them.

Inherited from [powder](https://github.com/kylerharris730-source/powder) and
still accurate. [DESIGN.md](DESIGN.md) covers what is being built on top of it;
[README.md](README.md) is the overview.

Most of what follows is written as *why a number is what it is*, because nearly
every constant here was set by measuring something rather than by taste, and a
plausible-looking tweak to one of them tends to quietly break something two
systems away. Where that happened it is recorded.

**Contents**

- [How it's put together](#how-its-put-together) — the engine and the three
  things that make it fast
- [Wetness](#wetness), [Liquids](#liquids), [Heat and phase
  changes](#heat-and-phase-changes) — the physical models
- Materials in detail: [Plasma](#plasma), [the cold set](#the-cold-set-cold-fire-liquid-nitrogen-mercury),
  [Rubber and the conductor ladder](#rubber-and-the-conductor-ladder),
  [Freezing and ice](#freezing-and-ice), [Clone and Void](#clone-and-void),
  [Heater and Cooler](#heater-and-cooler)
- [Material reference tables](#material-reference-tables) — every row of
  `MATS[]`, generated from the table itself
- [Adding a material](#adding-a-material)

---

## How it's put together

| File | Role |
|---|---|
| `src/common.h` | Types, xorshift32 RNG |
| `src/materials.*` | The material table and the colour LUT |
| `src/world.*` | Grid, chunk system, all movement, moisture and heat rules |
| `src/render.*` | Cell → pixel, heat glow, heat view |
| `src/main.cpp` | Window, input, timing, blit |

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

Every cell — air and walls included — carries a `temp` byte, kept in its own
array (`World::temp`) rather than inside `Cell`, so the movement rules never
pay for heat they do not read.

The byte holds **degrees Celsius plus `TEMP_OFFSET` (40)**, giving a range of
−40 °C to +215 °C. Ambient is 20 °C, water freezes at 0 and boils at 100. The
offset exists because ice does: the scale used to start at 0 °C, so there was
nothing below freezing to represent and the heat view had no cold half to
colour. Write temperatures as `degC(100)`, never as a bare `140` — the raw
numbers mean nothing on their own and all shift together if the offset ever
changes.

Paying for the cold end in headroom was unavoidable, because the byte was
already full at the top: lava sat at exactly 255. Only the three hottest values
had any slack, and all three were lowered — lava to 215 °C, fire to 205, stone's
melting point to 185. Everything else simply shifted by the offset, so its
distance from ambient — which is what actually drives the sim — is unchanged.
Steam still has exactly 70 units to cool through before it condenses, so it
rides just as far up as it ever did.

One consequence looks worse on paper than it is. Lava's molten band is spawn
minus freeze, and freezing cannot drop below water's boiling point without
breaking "lava is always hot enough to boil water", so the band narrowed from
155 units to 115. Measured, that barely matters: **2418 frames before against
2423 after** for a thick blob, 81 against 71 for a thin puddle. The narrower
band is offset by a shallower gradient to ambient (195 units where it was 235),
so lava sheds heat more slowly by almost exactly as much as it has less heat to
shed. Worth knowing before "fixing" it.

`0` is the "disabled" sentinel in every temperature column of `MATS[]`, and
`degC(-40)` is also 0 — so no material can have a threshold at exactly −40 °C.
Nothing wants one, and the cooler reaches it by being pinned rather than
through the table.

Conduction: each off-ambient cell exchanges heat with **all four** neighbours
each frame. The rate is set by the poorer of the two materials' `heatCond`, so
metal-on-metal is fast while a hot cell next to air barely bleeds — **air
conducts poorly on purpose**, or a single flame would heat the whole room in
seconds. Each exchange conserves energy and is capped at half the gap, so a
temperature can never cross the pairwise average; that keeps every value in
`[0,255]` regardless of the order neighbours are visited, with no second buffer.

**Air-to-air is the one exception**, and uses `AIR_MIX` rather than air's own
`heatCond`. Those are two different jobs that were wearing one number, and as a
single number it could only do one of them. Air's `heatCond` has to stay tiny so
a hot *solid* next to air barely bleeds; but at that value air-to-air was not
slow, it was **off**. `move = (adiff * cond) >> 9` needs a large gap before it
even reaches 1, and the round-up-to-1 rule deliberately skips near-insulators —
so a single air cell 20° above ambient never warmed its neighbour *at all*, and
a hot patch sat exactly where it was, dithering toward ambient in place for half
a minute. In the heat view it stayed a visible hard-edged rectangle.

Splitting them lets air mix properly while air-to-solid keeps its own rate. Two
things are worth knowing before touching either:

- **This makes heat leave the scene faster, not slower** — the opposite of the
  usual intuition about raising a conductivity. Conduction only *moves* energy;
  `AIR_COOL` is what removes it, and it is a per-cell chance, so spreading a hot
  patch over more cells multiplies the places it can drain from. A hot blob now
  clears in ~850 frames instead of ~1900.
- **The air-to-air path skips the round-up-to-1**, even though `AIR_MIX` is well
  over the threshold. With it, every value from 40 to 255 behaved *identically*
  — the flat 1°/frame floor swamped the proportional term and made the knob an
  on/off switch. Without it, mixing scales with the gradient, which is both what
  diffusion should look like and what makes the value tunable at all.

#### Conductivity saturates, so better conductors are ranked by *reach*

`heatCond` looks like a 0–255 dial, and it is not. The cap that keeps the
exchange stable — never move more than half the gap — also means the rate
saturates well before the byte does. Measured across every possible gap:

| `heatCond` | fraction of the hard cap achieved |
|---|---|
| 200 | 77.7% |
| 240 | 93.4% |
| 255 | **99.2%** |
| 256 | 100% |
| 400, 1000, 100000 | 100% — bit-identical to 256 |

Iron has sat at 255 since it was added, so it was already the most conductive
material the rule can express. There was no room above it, and "turn iron up"
was not a thing the table could say.

The axis with headroom left is **reach**, not rate. Ordinary conduction only
touches the 4-neighbourhood, so a heat front advances at most one cell per frame
*however conductive the material is* — a geometric limit, not a thermal one, and
the honest one to relax for a better conductor. `heatSpread` is how many cells
along an unbroken run of conductive material a cell *also* exchanges with each
frame:

| | `heatCond` | `heatSpread` | front after 60 frames |
|---|---|---|---|
| Stone | 85 | 0 | 10 cells |
| Iron (before) | 255 | — | 22 cells |
| **Iron** | 255 | 2 | **42 cells** |
| **Copper** | 255 | 5 | **89 cells** |
| **Graphene** | 255 | 28 | **227 cells** |

Four details that matter:

- **The extra exchange reuses the neighbour exchange verbatim** (`heatPair`), so
  it is capped, symmetric and mass-scaled identically. Energy is moved, never
  created — measured, total heat peaks at exactly its starting value and falls
  from there — and temperatures still cannot leave `[0,255]`.
- **It hops to the far end of the run only**, not to every cell along the way:
  one exchange for O(spread) steps of walking. The intermediate cells are filled
  in by their own neighbour loops on the same frame.
- **It cannot jump a break.** The walk stops at the first non-conductor, so a gap
  of air or a wooden handle insulates exactly as expected. The run is "anything
  with `heatSpread > 0`" rather than "the same material", so copper bolted to
  iron conducts across the joint.
- **Only materials that set it pay for it.** Ordinary scenery is untouched — with
  the column forced to zero everywhere, the sim is bit-identical to before the
  feature existed.

Graphene is the one value with a real cost, since the walk is O(spread) per cell
per frame:

| scene | ms/frame |
|---|---|
| bare heater in air | 0.17 |
| iron slab 300×40 | 0.47 |
| copper slab 300×40 | 0.57 |
| graphene slab 300×40 | 2.24 |
| graphene filling the playfield | 3.83 |

That last case at **4× speed is ~15 ms/frame**, which is right at the 16.6 ms
budget for 60 fps. It is the worst case the sim can be put in, and it takes
deliberately covering the screen in graphene to reach it.

The ratios (2.5× copper over iron, 14× graphene) are exaggerated against reality
— copper is about 1.7× iron, and graphene is off the scale entirely. At this grid
size the difference has to be visible across a bar you would actually draw.

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
not work. Transfer uses `min(a, b)` and air is small, so any value above it
leaves loss-to-air unchanged; going below it does slow the bleed, but then lava
can no longer heat anything up to its ignition or boiling point.

#### What air mixing cost, and why air's own conductivity moved

Air that spreads heat is also air that **carries heat off a hot solid**, and
that is not a tuning detail — it is the same mechanism seen from the other side.
The old model insulated hot objects by accident: the air touching them saturated
and the gradient collapsed, so they stopped losing heat. Once air disperses,
that blanket is gone and hot things bleed continuously. Lava's lifetime fell to
a quarter. Air's solid-facing `heatCond` came down 12 → 6 to compensate, and the
two numbers are now **coupled — change them together**. Measured against the
pre-change build:

| | before | after |
|---|---|---|
| hot air blob, spread | 15 cells | **26** |
| hot air blob, fully gone | 1911f | **856f** |
| lava, thick blob | 2103f | 1760f |
| lava, thin puddle | 1104f | 1227f |
| steam rise | 339 | 339 |
| ms/frame | 0.017 | 0.020 |

The value is 6 and not 5 on purpose: 5 restores the thick-blob lifetime exactly
but overshoots on thin lava (1104 → 2164), flattening the difference between a
deep pool and a shallow puddle to nothing. At 6 every geometry lands within 16%
*and* a thick blob still outlasts a thin one by 1.4×. The curve is steep — 7
already halves lava's life — so **re-run the puddle measurements before nudging
it**. (Lava's own `heatMassShift` 3 → 2 lands every lava figure closer still,
but that halves its heat capacity to chase a 16% metric, so it was left alone.)

Two knock-on effects, both traced to the same cause — a heat source no longer
gets to pool warmth in the air pocket beside it:

- **A heater under a wood pile stopped igniting it.** The heater never lit wood
  by touching it; it heated the adjacent air pocket, which pooled at ~250 and
  slow-cooked the pile past wood's ignition point. With air dispersing, the
  pocket settles at ~194, the pile asymptotes at 97 against an ignition point of
  120, and it burns exactly one cell and stalls **forever**. Fixed by raising
  `MACHINE_DRIVE` 24 → 64, which puts the pocket back over the line. It is a
  knee rather than a slope: 48 suffices and 48/96/160/255 are identical.
- **A heater buried in rock melts a pocket rather than the whole blob** (104
  cells → 20). This one is *not* recovered — melting is limited by the blob's
  equilibrium against air, not by how hard the machine pushes, so it is flat at
  20 cells for `MACHINE_DRIVE` 24 through 255. Arguably the more defensible
  result (heat now has somewhere to go), but it is a visible difference.

Both were caught by *measurement*, not by the test suite — the assertions were
`d1 < d0` and `peakLava > 0`, which a pile burning one single cell and a blob
melting almost nothing both sail straight through. Both have been tightened to
assert the actual behaviour.

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
conduction code is applied *only* to real conductors (`cond >= 40`, and never to
an air-air pair), so a near-insulator like steam conducts its true (often zero)
amount into passing
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
conduction ramp.

**Iron**, **copper** and **graphene** are static solids that exist to move heat.
All three share the maximum conductivity the exchange rule can express, and are
ranked against each other by `heatSpread` — how many cells of bar a heat front
crosses per frame — because `heatCond` had no room left above iron. A copper bar
carries a flame's warmth roughly twice as far as an iron one in the same time,
and a graphene sheet is close to isothermal the moment you heat any part of it.

All three are **flat-coloured** (`dryA == dryB == wetA == wetB`) rather than
speckled — speckle reads as loose grains, and a milled bar should read as one
uniform material. Graphene's dark indigo is a legibility fix, not a style
choice: as a flat neutral grey it sat 25 units from Wall's dark end by a
green-weighted RGB distance, against 729 for Wall vs Stone, and on screen it
simply read as wall. The palette already has three dark greys. Keeping it dark
and low-chroma but pushing the cast to indigo gets it to 610, clear of
everything, without making it a bright new primary.
Stone, by contrast, is a poor conductor, so heat stays local to where it is
applied. See [Conductivity saturates](#conductivity-saturates-so-better-conductors-are-ranked-by-reach)
for why the ranking had to move off `heatCond`.

### Plasma

**Plasma** is fire's hotter sibling, and the interesting part is that it could
not actually be made hotter. Fire spawns at 205 °C and the scale ends at 215 °C
(see the encoding note in `materials.h`), so there are ten degrees of headroom
in the entire byte. Peak temperature was never going to be the difference.

What actually limits fire is not how hot it is but how fast it spends itself.
Fire has `heatMassShift` 0: every degree it hands to a pot comes straight off
its own temperature, so it falls to its 100 °C cutoff and dies with the pot
still full. Plasma has mass 3. It delivers heat at the same full rate but feels
an eighth of the loss, so it stays in its working range far longer. **It is not
hotter than fire; it is inexhaustible** — which is what actually boils a pot dry.

| | fire | plasma |
|---|---|---|
| spawn temperature | 205 °C | 215 °C |
| thermal mass shift | 0 | 3 |
| quenched by water | yes | **no** |
| dies below | 100 °C | 120 °C |
| lifetime submerged | 1 frame | 21 frames |
| **frames to boil 90% off a 961-cell pot** | **234** | **37** |

Three other things differ:

- **Water does not snuff it.** Fire has `quenchedBy = MAT_WATER` and is gone the
  frame water touches it, so it can never do anything submerged. Plasma has no
  quench rule and flash-boils instead — measured, it lasts 21 frames under water
  against fire's 1, and makes five times the steam. It is *not* immune: a large
  enough cold body still drags it under its cutoff and kills it. This is the
  difference you feel most.
- **`heatSpread` 2** puts it on the same long-range conduction path as the
  metals. The run rule is "any material with `heatSpread` > 0", so a plasma
  cloud conducts along its own body, and plasma touching a graphene rod couples
  into the whole rod rather than just the cell it sits on.
- **It jets rather than billows** — jitter 30 against fire's 60, density 1
  against fire's 3, so it rises fast and climbs through ordinary flame.

#### Making it stop loitering, and why the obvious knob was the wrong one

Plasma first shipped dying below 90 °C and hung around far too long once it had
nothing left to heat. The intuitive fix — halve its thermal mass — **does not
work**, and the measurement is the point:

| | cloud lifetime | pot boil-off |
|---|---|---|
| original (mass 3, dies < 90 °C) | 380 frames | 44 |
| halve the mass (mass 2) | 357 — *barely moves* | 60 — **36% worse** |
| raise the cutoff to 120 °C | **211** | 45 — *unchanged* |

Thermal mass damps heat lost by **conduction**. What makes a cloud linger in
open air is the flat per-frame drift toward ambient (`GAS_COOL`), which mass
does not scale at all — so halving it pays full price in boiling power and buys
almost no reduction in loitering. Raising the cutoff instead truncates a long
cold tail that was doing no useful work, which is why it costs nothing.

Two consequences worth knowing. A lone plasma cell now expires *faster* than a
lone flame (57 frames against 78) — deliberate, since what plasma has over fire
is delivered heat, not time on screen. And because plasma is glow-exempt it
renders the same blue at every temperature, so there is no visible fade to lose:
shortening the cold tail only shortens its time on screen.

Fire's `boilTemp` was spare (fire had nowhere hotter to go), so it now points at
plasma: **fire driven to the top of the scale becomes plasma.** That is only
reachable by feeding it external heat — a heater, or lava tapped through a metal
run — since fire spawns ten degrees short of it and cools from there. It is a
deliberate reward for building the heat plumbing.

#### Plasma is the one material that does not take the heat glow

Worth knowing before adding another coloured-by-temperature material, because
the failure is not obvious until you look at a pixel. The glow ramp is orange at
working temperatures, and blending orange over blue does not produce a hotter
blue — it produces mud. Plasma rendered `#5652B4`, a murky purple, when its
material colour is `#3866FF`.

The fix is `g_matGlows[]`, a flag saying whether a material takes the overlay at
all. The justification is that the glow's entire job is to reveal temperature
you cannot otherwise see, and a material that *only exists* at extreme heat is
already saying that with its own colour. The Heat view still reports plasma's
real temperature like everything else.

Two implementation notes, both deliberate. It is a flag, not a 0–255 scale: a
scale means a multiply and shift on the alpha, which perturbs every *other*
material's blend by a rounding step to buy a partial glow nothing wants. And it
is a standalone array rather than a `MatInfo` field, because as a field it would
be the one column missing from all nineteen `MATS[]` rows, which trips
`-Wmissing-field-initializers` on every row under `-Wextra`; at `MAT_COUNT`
bytes the whole table also fits in one cache line.

### The cold set: cold fire, liquid nitrogen, mercury

Three materials that all live in the bottom of the temperature scale, plus the
two extra phases mercury needs. Before any of the individual notes, the fact
that shapes all of them:

**The cold half of the scale is tiny.** Hot runs ambient → 215 °C, nearly 200
units. Cold runs ambient → −40 °C, sixty. Fire gets to sit 120 degrees above
wood's ignition point; cold fire, liquid nitrogen and frozen mercury share sixty
units between them. Thresholds down here are packed close together out of
necessity. Raising `TEMP_OFFSET` would buy more, but every degree comes off the
top, where lava and stone's melting point are already tuned against each other
with ~30 units of slack — a real option if the cold side grows, just not a free
one. (Also: `degC(-40)` is 0, and 0 is the "disabled" sentinel in every
temperature column, so −39 °C is the coldest usable setpoint.)

**Cold fire** is fire read backwards. It rises, wanders and expires like a
flame, but chills instead of burns — conduction is symmetric, so a very cold
cell with a high `heatCond` pulls warmth out of its neighbours exactly as fire
pushes warmth in. Where fire dies by cooling (`coolTemp` → Empty), cold fire
dies by *warming*: the boil column pointed at Empty. It rises, which real cold
gas does not do — deliberate, from the brief "acts like fire but cold".

#### Why cold fire needs a decay timer when fire does not

The sharpest consequence of the cold half of the scale being small, and it is
not a matter of degree — it is a cliff. Conduction moves `(adiff * cond) >> 9`,
and against air `cond` is only 6:

| | gap to ambient | moved per neighbour per frame |
|---|---|---|
| fire | 185 | `(185*6)>>9` = **2** |
| cold fire | 58 | `(58*6)>>9` = **0** |

Cold fire exchanges **literally nothing** with open air; it truncates to zero.
Fire cools itself to death in ~94 frames and every degree it sheds goes into
something. Cold fire had no route to expiry but the slow ambient drift, so it
lived **503 frames** and rode **359 cells** up (fire: 94 and 68), gathering in a
layer under whatever it could not get past instead of being spent on it.

Since temperature cannot govern its lifetime, lifetime became its own knob:
`g_matDecay`, a chance out of 255 that a cell simply expires, non-zero only for
cold fire. The counterintuitive part is that **killing it faster made it colder
in practice** — spent cells were crowding out their own replacements, so a bed
that turns over keeps putting fresh −39 °C material against the target:

| graphene bucket of water, cold-fire bed underneath | before | after |
|---|---|---|
| coldest the water reaches | −18 °C | **−25 °C** |
| peak ice | 1198 | **1459** |
| cells escaping past the bucket | 863 | **42** |
| open-air lifetime / rise | 503 / 359 | **116 / 88** |

At 6/255 that lands cold fire almost exactly on fire — 116 frames and 88 cells
against fire's 108 and 84 — which is what "acts like fire but cold" should mean.
Cooling power is nearly flat across the useful range of this knob (1459 ice at
decay 6 against 1476 at decay 10), so it trades lifetime, not potency.

Measure it with more than one run. A puff's lifetime is the last of eleven cells
to expire — the maximum of eleven geometric draws — so single readings swung
45..150 at fixed settings and even placed decay 8 *below* decay 10. The figures
here are 25-run means, which come out monotonic.

`heatMassShift` went 1 → 2 at the same time, and the pairing is the point: mass
decides how much cold *one* cell unloads before it warms, decay decides how long
cells hang around. Splitting payload from lifetime is the whole reason to have
two knobs rather than one.

**What it cost.** A *sealed one-shot charge* of cold fire no longer freezes
water — 461 peak ice before the timer, 58 after — because a fixed quantity runs
out before it can unload. That is a genuine loss, accepted because the fed case
(a bed under a target, the way a fire is used) is both the realistic one and
hugely better. There is no decay value that keeps both: even 3/255 collapses the
sealed charge to 79 ice.

**Cold fire is still only 58 degrees below ambient**, against fire's 185 above,
and nothing here changes that. Only widening the scale would, and that costs hot
headroom which is already densely packed — mercury 150, copper 175, stone 185,
iron 200, fire 205, lava/plasma/heater 215.

**Liquid nitrogen** boils at −25 °C, so an exposed pool is always boiling — as
real LN2 always is — and only stays liquid where something keeps it cold. It
boils into cold fire rather than into nothing, which conserves the visual (a
splash throws off a cold plume) and gives cold fire a natural source the way
burning gives fire one.

Its `heatMassShift` of 4 is what makes it usable rather than merely present. At
mass 2 a poured pool of 820 cells was gone in **28 frames** — under half a
second, before it could chill anything, freezing no mercury at all. Each step
roughly doubles: 90 frames at mass 3, 158 at mass 4, 318 at mass 5. Mass 4 is
where a splash lasts long enough to read as a splash and still does work.

**Mercury** is the only liquid metal and the only material with a phase change
at *both* ends, which is what makes it worth distilling. Denser than everything
that moves, so it sinks under water and sand.

Both its thresholds are deliberate departures from reality, for reasons worth
recording:

- **Freezes at −30 °C**, not the real −38.8 °C. The real figure sits one degree
  off the coldest value the byte can hold, leaving no room for a cold source to
  reach it and no gap for hysteresis. At −30 there are ten degrees of headroom
  below, which is what makes freezing mercury *achievable* rather than merely
  representable. It melts back at −24 °C; that 6-degree gap is hysteresis.
- **Boils at 150 °C**, not the real 357 °C (off this scale entirely) and not the
  200 °C first tried. 200 was reachable in theory and not in practice: a
  **heater**, pinned at the top of the scale forever and the strongest sustained
  source in the game, only carried a walled mercury charge to about 140 °C,
  because a vessel loses heat to the room faster than conduction tops it up.
  Measured, 200 °C gave 32 vapour cells against 94 at 150 °C.

150 °C also puts it 50 degrees clear of water. The three boiling points now
ladder — **LN2 −25, water 100, mercury 150** — so a mixture held between two of
them separates. Measured at 125 °C, a checkerboard water/mercury charge gives
off steam and mercury vapour in an **18:1** ratio. Not infinite, and correctly
so: surface evaporation scales with distance above *ambient* regardless of
boiling point, so warm mercury always gives off a little vapour. Real
distillation is imperfect the same way.

Mercury vapour is density 20 against steam's 8, so the two **separate by
weight** on their own — steam rides above mercury vapour — which is the point of
distilling a mixture rather than just boiling it.

#### A note on testing anything here

Everything in this section is transient: cold fire is consumed as it chills, LN2
boils itself away, vapour condenses back. **Measure the peak, not the end
state.** Sampling at the end reported that cold fire did nothing at all — at
frame 400 the test pool sits at −3 °C with ice in it, and by frame 1500 it is
uniformly +20 °C again with none. Three separate tests in this area failed that
way before the measurements were fixed.

### Rubber, and the conductor ladder

**Rubber** is the best insulator in the game: `heatCond` 12, against wood's 70
and stone's 85. Since conduction runs at `min(condA, condB)`, a rubber layer
throttles heat crossing it whatever sits on either side. Air itself is 6, so
rubber is within a whisker of an air gap while still being something you can
build a wall out of. Measured through a 17-cell wall with the hot face held at
90 °C, of a possible +70 rise:

| wall | far-face rise |
|---|---|
| **rubber** | **+0** |
| stone | +40 |
| iron | +54 |
| graphene | +68 |

It melts at 100 °C — exactly water's boiling point — and the consequence is the
interesting part:

> **Insulating well is precisely what destroys it.** Heat cannot cross into
> whatever rubber is protecting, so the *outer* face climbs past 100 °C and the
> jacket fails. A stone jacket conducts heat away from itself and never gets hot
> enough to be in danger. Measured with a fire underneath, a pot in a stone
> jacket first steams at frame 115; in a rubber jacket, at frame **54** — the
> better insulator is worse in practice, because it breaches.

That is the gap a genuine high-temperature insulator would fill.

**Molten rubber** is the only viscous liquid in the game, and viscosity is its
own axis: for a **liquid**, the `jitter` byte (documented "gases only") is a
chance out of 255 that the cell refuses to flow sideways this frame. Molten
rubber sits at 230 — stuck 90% of frames. It still falls straight down at full
speed; viscosity resists flow, not gravity.

Measured, to run 100 cells along a floor:

| liquid | frames |
|---|---|
| **molten rubber** | **~355** |
| lava | 28 |
| water | 17 |

**Why viscosity is not just `dispersion`.** Dispersion ran out. Molten rubber
was already at 1, and 0 is not "thicker" — it is "no longer a liquid": measured,
a dispersion-0 pool poured down one side of a container had still not reached
the far side after 6000 frames, and a vessel draining through its floor kept 270
of 2370 cells welded to the walls permanently. A probability gate degrades
gracefully instead. A stuck cell simply tries again next frame, so at 90%
viscosity the liquid still **levels perfectly** (17 deep against 17 across a
container filled from one side) and still **drains completely** (0 of 2370 cells
left behind). It just takes its time. Dispersion went back up to 2 for the same
reason: if flow is gated to one frame in ten, the frames it does move should be
worth something, or it crawls a pixel at a time and reads as broken.

Molten rubber is **denser than water** (115 against 100), so it sinks through a
pool and sets on the bottom; it still floats on mercury (250). Solid rubber is
150, also above water, though that number is never actually read — rubber is
`KIND_STATIC`, and `tryMove` refuses to displace anything that is not a liquid
or a gas, so a static material's density is decorative.

Making that density *visible* took a second change, and it was not obvious until
measured. At `heatMassShift` 0 a blob poured onto water sank briefly, cooled past
its 65 °C set point on the way down and solidified halfway — and once solid it is
static, so it stopped there and read as floating no matter what the density
column said. Mass 2 keeps it molten long enough to reach the bottom first:
measured mean depth y=321 at mass 0 (above the water's 339) against y=364 at
mass 2 (below it). Rubber being an insulator, holding onto its heat is the
physically sensible reading anyway.

It sets again below 65 °C.

> **The dirty-rect trap, third time.** A cell that refuses to move has not
> moved, so it schedules nothing — and a pool whose cells all refused on the
> same frame would put its own chunk to sleep mid-ooze. So a stuck cell with an
> empty neighbour re-dirties itself; one packed solid on all sides does not, and
> is left to sleep. A pool held *artificially* molten forever therefore never
> sleeps, at a bounded 0.15 ms/frame over 8 of 192 chunks. Left alone it cools,
> sets solid, and the world sleeps — measured at frame 9222.

#### Iron and copper melt; graphene does not

This is what makes the three conductors a real ladder rather than a strict
upgrade path. Measured against every heat source in the game:

| source | reaches | iron (melts 200 °C) | copper (melts 175 °C) |
|---|---|---|---|
| fire | ~130 °C | survives | survives |
| lava | ~175 °C | **survives** | **melts** |
| heater / plasma | ~215 °C | melts | melts |
| — | — | graphene: **no melting point at all** |

So: **lava melts copper but not iron.** Copper is the faster conductor and the
first to give out, which makes choosing it a genuine trade instead of a free
upgrade; iron is the tougher one; and above ~200 °C graphene is the only thing
left that still conducts.

Copper's 175 °C was set by measurement, not taste. At 185 a lava bed — which
carries a slab to about 178 °C — melted a token 3 cells, true in principle and
invisible in play. 175 turns a vague ordering into a rule worth knowing.

**Melting destroys what you built the metal for.** Both molten forms have
`heatSpread` 0, so a copper bar that carries heat five cells a frame becomes a
puddle that only conducts to what it touches. Overheating your heat plumbing
does not merely deform it.

One consequence to know: **an iron vessel sitting directly on a heater now
partly melts and re-freezes.** The mercury still still works (the vessel
recovers), but graphene is now the correct material for anything in direct
contact with a heater or plasma.

#### The freeze-back point is not free to choose

Every melt/freeze pair here has a gap wider than `LATENT_HEAT` (30), and that is
forced. A phase change runs `latentDrain()`, pulling the new cell 30 units
toward ambient — so iron melting at 200 °C arrives as molten iron at 170 °C. If
it re-froze anywhere above that it would solidify on its very next update, and
the bar would flicker between states without ever flowing. Iron sets at 160,
copper at 140, rubber at 65.

### Freezing and ice

Water freezes below 0 °C into **Ice**, and ice melts back above +6 °C. Both are
ordinary table rows — `coolTemp`/`coolsTo` one way, `boilTemp`/`boilsTo` the
other — so freezing needed no new code in `world.cpp` at all, only somewhere on
the scale to put it.

The 6 °C gap is hysteresis, the same trick stone and lava use: it leaves a band
(40..45 in stored units) where *both* states are stable, so a cell sitting right
at freezing cannot flip back and forth every frame.

Ice is `KIND_STATIC`, so it never flows and never gets displaced — `tryMove`
refuses to swap with anything that is not a liquid or gas, which is what stops
water tunnelling through a frozen sheet regardless of the densities. Hand-placed
ice arrives at −16 °C; without that it would spawn at ambient and melt on the
very next frame, which reads as a broken brush.

One subtlety worth knowing, because it produced a real bug. Latent heat used to
be `imax(AMBIENT, t - LATENT_HEAT)`, which is right for every transition that
happens *above* ambient — boiling, evaporating — but **ice melts below it**, and
subtracting there drove the new water back under the freeze threshold it had
just crossed. The cell refroze the next frame and ice sat flickering instead of
melting. `latentDrain()` now moves the cell toward ambient from whichever side
it is on, which makes the two directions symmetric and leaves the hot path
behaving exactly as before.

The simplest way to make ice: a **Cooler** under a pool.

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

### Heater and Cooler

Two more machines: static blocks pinned to `HEATER_TEMP` (255) and `COOLER_TEMP`
(0) forever. Put a heater under a basin and it boils; embed one in a wooden wall
and it burns the wall down; drop coolers on a lava flow and it sets to stone.
They are the standing-source counterpart to the **Heat**/**Cool** brushes, which
only act while you drag — hence the four similar names sitting together in the
panel.

Each machine does two things per frame, and the second one is the one that
matters:

1. **Pins its own temperature** to the setpoint. Ordinary conduction then
   carries that outward like any other hot or cold cell.
2. **Drives its four orthogonal neighbours** one `MACHINE_DRIVE` step (64°)
   toward the setpoint, never past it.

Step 1 alone is not enough, and the reason is the interesting part.
Conduction runs at `min(condA, condB)` — **the poorer conductor sets the rate,
and that is the neighbour, not the machine.** A heater against stone therefore
delivers at *stone's* rate no matter how conductive the heater is, and the rock
settles into a gradient that levels off near 150 — short of its 220 melting
point. Turning the heater's own `heatCond` up does nothing whatsoever about
this. Measured: pinning only, the hottest stone next to a heater plateaus at
~150 and never melts.

Step 2 is what makes it a *source* rather than a hot *object*. Clamping at the
setpoint is what keeps it stable: the step can only close the gap, so a
neighbourhood converges on the setpoint and then stops changing. Writing it as
a plain `+= N` instead would have no fixed point — the region would climb until
it saturated and never settle, holding chunks awake forever.

That clamp is load-bearing for cost: a lone heater reaches an equilibrium and
then stops changing. It holds **8 chunks and ~2200 warm cells awake, steady from
frame 3000 out to 6000**.

Those figures used to be 4 chunks and 85 cells, and were *identical* across
`MACHINE_DRIVE` 24–96 — the drive rate set how fast equilibrium arrived, not how
far it reached. Air mixing changed both halves of that: warmth now genuinely
travels, so the halo is far wider, and the drive rate does now affect its extent
(warm cells roughly double from drive 24 to 64). It still converges, which is
the property that matters — but the old "insensitive to `MACHINE_DRIVE`" claim
no longer holds, so raising it is no longer free.

A heater does keep its own chunk awake permanently, which is the one place the
"don't dirty unless something really happened" rule is deliberately broken. A
permanent source is by definition never finished; the cost is bounded and it is
one chunk per machine.

Two behaviours worth expecting:

- **Melting stone is transient.** The rock at the contact melts, the lava flows
  out of the hole it made, and it refreezes once below 100 — so a snapshot at
  an arbitrary frame often shows no lava at all even though plenty melted.
- **Embedding beats touching.** Against a single face, heat leaks into the
  surrounding rock nearly as fast as it arrives and only a little melts right at
  the contact. Buried in the block, all four faces drive and it melts properly.

Like clone and void, neither is cloneable — and here that matters more than
elsewhere. A clone latched onto a heater would emit heaters into every empty
neighbour, each one itself a permanent source holding a chunk awake, so the
thing would grow without bound in both temperature and cost.

---

## Adding a material

1. Add an id to `MatId` in `materials.h`, above `MAT_COUNT`.
2. Add a row to `MATS[]` in `materials.cpp` (rows must stay in id order).
3. Only if it needs behaviour no existing `MatKind` covers, add a rule in
   `world.cpp`.

> **The id space is now FULL.** Ice took `MAT_COUNT` to **16, which is the hard
> maximum** — the `static_assert` in `world.cpp` explains why: clone packs a
> material id into the spare moisture byte, and the colour LUT indexes moisture
> as `moisture & 0xF0`, so an id of 16 or more would make a loaded clone render
> as a different wetness bucket. **The next material added will not compile**
> until that is dealt with. The fix is to stop overloading `moisture` — give
> clone its own storage, or widen `Cell` past 4 bytes and accept the cache cost.
> The assert will stop you, which is the point of it.
>
> The panel has more room: the row pitch dropped from 24+5 to 22+4 when Ice made
> an 18th button, leaving about **54px of clearance** above the stats block, so
> two more rows fit. Nothing warns about *that* one — the buttons simply draw
> over the stats text — so check `layoutPanel()` by eye if the palette grows.

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

### Adding a material to the picker

The palette is data-driven: one row in the `BRUSHES[]` table in `main.cpp` adds
a labelled button, and its swatch colour is pulled straight from the material's
own palette, so it always matches what lands in the world. The panel widens the
window by a fixed strip and the sim blits to the right of it at a clean integer
scale, so the window→cell mapping stays a plain divide.

Row height is **derived** from the space between the title and the stats block
rather than hard-coded, which fixes a trap that had already bitten twice: the
palette grew, the buttons quietly overflowed the stats text at the foot of the
panel, and nothing warned — they simply drew on top of it. Adding a button now
just makes every row slightly shorter instead.


---

## Material reference tables

Generated from `MATS[]` in `src/materials.cpp` rather than transcribed, so the
numbers cannot drift from the code. Temperatures are degrees Celsius. A dash
means the feature is switched off for that material.

`conducts` is `heatCond`, 0–255: how readily heat crosses into the material.
Conduction between two cells runs at the *lower* of the two, which is why air's
6 dominates almost every exchange with open space.

`thermal mass` is a right-shift, so 3 means the material feels only an eighth of
the temperature change it hands out. It is what lets lava heat everything it
touches at full rate while staying molten.

`reach` is `heatSpread`: extra cells along an unbroken conductive run that a
cell also exchanges with each frame. It exists because `heatCond` saturates —
255 is already 99.2% of the maximum the exchange rule can express, so the only
way to rank conductors above iron was to relax how far a front travels per
frame rather than how hard it pushes.

`viscosity` (the `jitter` byte, for liquids) is a chance out of 255 that a cell
refuses to flow sideways on a given frame. For gases the same byte is `jitter`:
the chance of drifting sideways instead of rising.

| material | kind | density | conducts | thermal mass | reach | spawns at |
|---|---|---|---|---|---|---|
| Wall | solid | 255 | 30 | 0 | 0 | — |
| Stone | solid | 255 | 85 | 0 | 0 | — |
| Sand | powder | 150 | 90 | 0 | 0 | — |
| Dirt | powder | 140 | 80 | 0 | 0 | — |
| Water | liquid | 100 | 180 | 0 | 0 | — |
| Ice | solid | 92 | 120 | 0 | 0 | -16 |
| Steam | gas | 8 | 5 | 0 | 0 | +115 |
| Fire | gas | 3 | 200 | 0 | 0 | +205 |
| Plasma | gas | 1 | 255 | 3 | 2 | +215 |
| ColdFire | gas | 3 | 200 | 2 | 0 | -39 |
| LiqN2 | liquid | 80 | 200 | 4 | 0 | -39 |
| Mercury | liquid | 250 | 240 | 1 | 0 | — |
| HgVapour | gas | 20 | 20 | 0 | 0 | +150 |
| FrozenHg | solid | 255 | 240 | 1 | 0 | -35 |
| Iron | solid | 220 | 255 | 0 | 2 | — |
| MoltIron | liquid | 215 | 255 | 1 | 0 | +200 |
| Copper | solid | 224 | 255 | 0 | 5 | — |
| MoltCopp | liquid | 218 | 255 | 1 | 0 | +175 |
| Graph | solid | 190 | 255 | 0 | 28 | — |
| Lava | liquid | 200 | 120 | 3 | 0 | +215 |
| Wood | solid | 150 | 70 | 0 | 0 | — |
| Rubber | solid | 150 | 12 | 0 | 0 | — |
| MoltRub | liquid | 115 | 12 | 2 | 0 | +100 |
| Clone | solid | 255 | 40 | 0 | 0 | — |
| Void | solid | 255 | 40 | 0 | 0 | — |
| Heater | solid | 255 | 200 | 0 | 0 | +215 |
| Cooler | solid | 255 | 200 | 0 | 0 | — |


### phase changes

| material | below | becomes | at or above | becomes | ignites at | into | destroyed by |
|---|---|---|---|---|---|---|---|
| Stone | — | — | +185 | Lava | — | — | — |
| Water | +0 | Ice | +100 | Steam | — | — | — |
| Ice | — | — | +6 | Water | — | — | — |
| Steam | +45 | Water | — | — | — | — | — |
| Fire | +100 | Empty | +215 | Plasma | — | — | Water |
| Plasma | +120 | Empty | — | — | — | — | — |
| ColdFire | — | — | +5 | Empty | — | — | — |
| LiqN2 | — | — | -25 | ColdFire | — | — | — |
| Mercury | -30 | FrozenHg | +150 | HgVapour | — | — | — |
| HgVapour | +130 | Mercury | — | — | — | — | — |
| FrozenHg | — | — | -24 | Mercury | — | — | — |
| Iron | — | — | +200 | MoltIron | — | — | — |
| MoltIron | +160 | Iron | — | — | — | — | — |
| Copper | — | — | +175 | MoltCopp | — | — | — |
| MoltCopp | +140 | Copper | — | — | — | — | — |
| Lava | +100 | Stone | — | — | — | — | — |
| Wood | — | — | — | — | +80 | Fire | — |
| Rubber | — | — | +100 | MoltRub | — | — | — |
| MoltRub | +65 | Rubber | — | — | — | — | — |


### movement

| material | kind | dispersion | jitter/viscosity | slide dry | slide wet | holds water |
|---|---|---|---|---|---|---|
| Sand | powder | 0 | 0 (jitter) | 235 | 60 | 128 |
| Dirt | powder | 0 | 0 (jitter) | 150 | 12 | 192 |
| Water | liquid | 5 | 0 (viscosity) | 0 | 0 | 0 |
| Steam | gas | 7 | 150 (jitter) | 0 | 0 | 0 |
| Fire | gas | 3 | 60 (jitter) | 0 | 0 | 0 |
| Plasma | gas | 6 | 30 (jitter) | 0 | 0 | 0 |
| ColdFire | gas | 3 | 60 (jitter) | 0 | 0 | 0 |
| LiqN2 | liquid | 6 | 0 (viscosity) | 0 | 0 | 0 |
| Mercury | liquid | 5 | 0 (viscosity) | 0 | 0 | 0 |
| HgVapour | gas | 5 | 120 (jitter) | 0 | 0 | 0 |
| MoltIron | liquid | 4 | 0 (viscosity) | 0 | 0 | 0 |
| MoltCopp | liquid | 4 | 0 (viscosity) | 0 | 0 | 0 |
| Lava | liquid | 3 | 0 (viscosity) | 0 | 0 | 0 |
| MoltRub | liquid | 2 | 230 (viscosity) | 0 | 0 | 0 |


### special flags

- Plasma does not take the heat glow
- ColdFire expires on a timer, 6/255 per frame


ambient +20 C, scale runs -40 C .. +215 C
27 materials
