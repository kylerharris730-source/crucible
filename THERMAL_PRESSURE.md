# Thermal buoyancy and pressure

This branch develops two related mechanics in isolated milestones. Each stage
must remain useful and testable without depending on the next one.

## 1. Temperature-dependent density — implemented

- Fluid density is compared in Q8 fixed point.
- Every liquid and gas has a small thermal-expansion coefficient; Wax has a
  deliberately large coefficient that crosses Water's density near 60 C.
- Convection exchanges whole cells and their temperature, not temperature by
  itself. Water parcels can rise through three cooler Water cells per frame at
  a two-degree gradient; other fluids retain the conservative adjacent swap.
- A quarter-density hysteresis prevents one-degree interface jitter.
- Wax is available in the sandbox palette. Its survival source and stronger
  blob cohesion remain design work, not assumptions hidden in this milestone.

Regression coverage holds a Wax parcel at 90 C until it rises through a sealed
Water column, cools it to 20 C, and verifies that it sinks again with exactly
one Wax cell remaining.

## 2. Compressed gas volume — implemented

An occupied gas cell represents one volume unit and stores a bounded number of
unexpanded volume units. Boiling Water creates one owner Steam parcel carrying
two extra units. At a usable boundary, all two units expand through connected
open air or a straight liquid column in one turn; if sealed, excess remains as
pressure and equalizes through connected gas. The kind-specific
`Cell::moisture` payload stores this without adding a 38 MB pressure plane.

Expansion daughters carry a volume-only provenance bit. On cooling, daughters
collapse to Empty and the single owner condenses to Water, preserving the round
trip `1 Water -> 3 Steam volumes -> 1 Water`. Gas-sieve occupancy packs and
restores that provenance bit as well. This temporarily constrains material ids
to seven bits; reaching 128 materials should replace packed sieve occupants
with a sidecar rather than steal another bit.

The original six-to-one Water expansion made ordinary boiling overwhelm large
lakes once pressure transport became fast. Water now expands three-to-one;
Mercury gas retains the larger six-volume charge, so the pressure machinery
still supports and tests five-unit bursts.

Regression coverage verifies open expansion, sealed retention, connected
equalization, condensation conservation, reopening a sleeping chamber, and
sieve provenance.

## 3. Liquid displacement — implemented

Pressurized gas spends one excess unit to create another gas cell and shifts a
connected liquid path toward its nearest surface or outlet. Prefer a cheap
vertical column search, followed by a bounded lateral search. No world-wide
flood fill and no teleporting to a remote outlet.

The vertical path is capped at 512 cells and the lateral search at a 16-cell
radius. A straight vertical outlet can displace up to five volumes in one turn;
a bent path moves one. Cells carry their temperature through the shift, liquid
volume is conserved, player-occupied cells cannot be selected as outlets, and
every moved path is dirtied and stamped so it cannot relay again during the
same in-place simulation scan.

Regression coverage verifies both a 23-cell-deep lift and a bent liquid path,
including exact liquid volume and pressure-unit conservation.

## 4. Permeable reactive powders — implemented

The visible grid has no sub-cell pore space, so Steam cannot physically pass a
fully occupied Coal pile without an explicit permeability abstraction. Coal
will temporarily co-occupy one gas parcel, similar to a Sieve, allowing Steam
to percolate and trigger the existing Coal-to-Fuel contact reaction. Low
pressure must not throw the pile around.

Permeability is deliberately derived from the existing powder/gas reaction
pair rather than granted to every powder. Coal accepts Steam because its table
already names Steam as the consumed input for Fuel; unrelated gases cannot get
trapped inside it. Compressed gas emits one provenance-preserving volume into
the Coal and retains the remaining pressure, exactly as it does for a Sieve.
The transient occupant is material-name-remapped across save/load.

Regression coverage feeds four stored Steam volumes through a supported
four-cell Coal column and verifies that the co-occupied state occurs when the
pile has no movable outlet, producing exactly four Fuel with no Coal or Steam
left over.

## 5. Bounded powder pressure pushing — implemented

After liquid displacement and permeability are predictable, pressure may move
loose powders according to a material resistance table. Static terrain,
devices, and placed walls remain immovable initially. Ruptures, destructive
pressure, player force, and pistons are later gameplay decisions.

Pressure tests four straight rays and shifts the lowest-resistance powder plug
into a real empty outlet, up to eight cells long. Each material has an explicit
starting threshold; every additional cell adds one to the required pressure.
The successful shove still spends only one expansion volume. Sand starts at
one and Coal at two, so fresh Steam can move two Sand grains or one loose Coal
grain; packed Coal remains permeable and reactive. Dirt starts at three, Clay
at four, common ores at six, Titanium at seven, and Tungsten at eight. Static
materials never enter the path search.

Shared pressure recognizes a powder face only after verifying that the entire
bounded plug can reach real empty space. This lets pressure from inside a gas
pocket reach movable Sand or Coal without treating packed terrain as relief.

The hover readout now reports stored pressure on gas cells. Regression coverage
checks short-plug movement, low-pressure refusal, length resistance, powder and
gas-volume conservation, and the immovable/ore thresholds.

## 6. Shared gas-pocket pressure — implemented

Stored expansion volume no longer has to diffuse one pixel at a time through a
connected Steam blob before its surface can use it. An interior compressed cell
routes part of its pressure directly to a lower-pressure boundary cell of the
same gas pocket. Four straight rays of up to 512 cells handle broad ordinary
pockets cheaply; a 32-cell-radius, 2,048-node bounded flood handles bends and
irregular cavities. Neither path walks the world or adds a per-cell pressure
plane. At most 128 cells may start a pocket-routing search per world step;
additional pressure uses the local equalizer and retries on later frames.

Routing only moves hidden pressure units. The receiving boundary still performs
the existing visible expansion, liquid displacement, sieve entry, Coal reaction,
or powder push, so represented gas volume and material remain conserved. A cell
already touching a possible outlet keeps its own pressure rather than bouncing
it around the pocket when that outlet is sealed.

Liquid touching the side or bottom of a gas pocket is not considered pressure
relief by itself. A shared-pressure target must have a straight liquid column
that really reaches open space within 512 cells (or a genuine empty/filter/
reaction outlet); this keeps deep-lake pressure moving toward the surface rather
than accumulating on a useless underwater wall. Long vertical shifts dirty one
span instead of thousands of individual cells.

Regression coverage releases five volumes across all 41 exposed cells of a
41x41 Steam pocket with 81 compressed interior cells in one turn—205 visible
Steam volumes at once—and verifies exact gas-plus-pressure and Water
conservation. A separate test routes pressure through 75 cells of Steam and a
160-cell-deep lake, verifying that it ignores a sealed side-water dead end,
reaches the real surface, and releases all five stored volumes in one step.
A separate L-shaped pocket verifies the bounded flood reaches a Water face that
no straight ray can see.
A temporary 121x121 stress scene with 81 compressed cells measured roughly
3.5-4.0 ms per simulation step on the development machine. An intentionally
pathological 1,681-cell compressed core initially measured about 40 ms; the
128-search world budget reduced it to roughly 5.1-5.3 ms while letting it drain
over additional frames.

## Required diagnostics

Before destructive pressure exists, retain the pressure hover readout. Existing
regressions cover sealed expansion, pressure equalization, deep-water lift,
Coal permeability, large and curved pocket sharing, material conservation, and
active-window cost. Save/reload and sleeping chunks need explicit stress
harnesses before rupture or cell destruction is added.
