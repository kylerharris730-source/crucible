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

## 2. Compressed gas mass

An occupied gas cell represents one volume unit and stores a bounded number of
extra mass units. Boiling creates multiple Steam units. Excess first expands
into open space; if sealed, it remains as pressure and equalizes through
connected gas. This must reuse kind-specific cell storage rather than add a
world-sized pressure plane.

Condensation must account for excess mass instead of deleting it: a compressed
Steam parcel may condense several Water cells over time.

## 3. Liquid displacement

Pressurized gas spends one excess unit to create another gas cell and shifts a
connected liquid path toward its nearest surface or outlet. Prefer a cheap
vertical column search, followed by a bounded lateral search. No world-wide
flood fill and no teleporting to a remote outlet.

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
