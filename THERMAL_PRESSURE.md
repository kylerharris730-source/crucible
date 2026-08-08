# Thermal buoyancy and pressure

This branch develops two related mechanics in isolated milestones. Each stage
must remain useful and testable without depending on the next one.

## 1. Temperature-dependent density — implemented

- Fluid density is compared in Q8 fixed point.
- Every liquid and gas has a small thermal-expansion coefficient; Wax has a
  deliberately large coefficient that crosses Water's density near 60 C.
- Convection exchanges whole cells and their temperature, not temperature by
  itself. Alternating row pairs prevent a parcel moving many cells in one scan.
- A quarter-density hysteresis prevents one-degree interface jitter.
- Wax is available in the sandbox palette. Its survival source and stronger
  blob cohesion remain design work, not assumptions hidden in this milestone.

Regression coverage holds a Wax parcel at 90 C until it rises through a sealed
Water column, cools it to 20 C, and verifies that it sinks again with exactly
one Wax cell remaining.

## 2. Compressed gas volume — implemented

An occupied gas cell represents one volume unit and stores a bounded number of
unexpanded volume units. Boiling Water creates one owner Steam parcel carrying
five extra units. Excess expands locally one adjacent cell per frame; if sealed,
it remains as pressure and equalizes through connected gas. The kind-specific
`Cell::moisture` payload stores this without adding a 38 MB pressure plane.

Expansion daughters carry a volume-only provenance bit. On cooling, daughters
collapse to Empty and the single owner condenses to Water, preserving the round
trip `1 Water -> 6 Steam volumes -> 1 Water`. Gas-sieve occupancy packs and
restores that provenance bit as well. This temporarily constrains material ids
to seven bits; reaching 128 materials should replace packed sieve occupants
with a sidecar rather than steal another bit.

Regression coverage verifies open expansion, sealed retention, connected
equalization, condensation conservation, reopening a sleeping chamber, and
sieve provenance.

## 3. Liquid displacement — implemented

Pressurized gas spends one excess unit to create another gas cell and shifts a
connected liquid path toward its nearest surface or outlet. Prefer a cheap
vertical column search, followed by a bounded lateral search. No world-wide
flood fill and no teleporting to a remote outlet.

The vertical path is capped at 64 cells and the lateral search at a 16-cell
radius. One compressed parcel displaces one volume per frame. Cells carry their
temperature through the shift, liquid volume is conserved, player-occupied
cells cannot be selected as outlets, and every moved path is dirtied and
stamped so it cannot relay again during the same in-place simulation scan.

Regression coverage verifies both a 23-cell-deep lift and a bent liquid path,
including exact liquid volume and pressure-unit conservation.

## 4. Permeable powders

The visible grid has no sub-cell pore space, so Steam cannot physically pass a
fully occupied Coal pile without an explicit permeability abstraction. Coal
will temporarily co-occupy one gas parcel, similar to a Sieve, allowing Steam
to percolate and trigger the existing Coal-to-Fuel contact reaction. Low
pressure must not throw the pile around.

## 5. General pressure pushing

After liquid displacement and permeability are predictable, pressure may move
loose powders according to a material resistance table. Static terrain,
devices, and placed walls remain immovable initially. Ruptures, destructive
pressure, player force, and pistons are later gameplay decisions.

## Required diagnostics

Before destructive pressure exists, add a pressure view or hover readout. Test
sealed expansion, pressure equalization, deep-water lift, Coal permeability,
material conservation, save/reload, sleeping chunks, and active-window cost.
