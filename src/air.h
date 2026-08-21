#pragma once
#include "world.h"

/* --- air, as a pressure field ------------------------------------------------

   Empty is VACUUM in this simulation. A gas rises because a rule says gases
   rise, not because anything heavier is falling past it, and it spreads because
   a rule says it diffuses. That works, and it has two visible costs: a plume
   climbs at whatever rate the movement rules allow rather than at a rate the
   scene implies, and nothing anywhere is pushed by anything else.

   The alternative that would be RIGHT is also the one that is impossible: make
   air a material and simulate 37.7 million cells of it. So this is the cheap
   half of the idea -- not air as cells, but air as a PRESSURE FIELD over them.

   --- what it actually models ---
   One number per 4x4 block of world, meaning "how crowded is this pocket". Gas
   raises it, open space lowers it, solid blocks it entirely. The field is then
   relaxed so a crowded pocket bleeds into its neighbours, and gas movement
   reads the gradient and prefers to go downhill.

   That is a real fluid effect rather than a fudge, and it is the one the
   existing rules cannot express: a gas under a cap builds pressure and vents
   sideways, a plume packed at its source pushes itself outward faster than
   diffusion alone would, and a chamber connected to a corridor equalises
   through it. None of that is reachable from per-cell buoyancy.

   --- what it deliberately does NOT model ---
   Air is not conserved, it has no velocity, and it does not carry heat. Each of
   those is a much larger feature, and each would need this one to exist first.
   Naming that here so the next person does not read "air simulation" and expect
   wind.

   --- the resolution is the whole cost argument ---
   4x4 blocks, so the field is a sixteenth the size of the world, and it is only
   ever computed over the live window -- which is around 96k world cells, or 6k
   air cells. A handful of relaxation passes over 6k values is nothing against
   a frame that already lights and simulates that same region.

   The full-world array is 4.7 MB and exists so that a camera move needs no
   remapping: an air cell's index is derived from its world position and never
   changes. Against a world that is already 190 MB of cells and temperature,
   that is not the expensive part of this. */

static const int AIR_SHIFT = 2;                  /* 4x4 world cells per air cell */
static const int AIR_W = SIM_W >> AIR_SHIFT;
static const int AIR_H = SIM_H >> AIR_SHIFT;

/* Pressure, in arbitrary units. i16 rather than u8 because the interesting part
   is the GRADIENT between neighbours, and an eight-bit field quantises small
   differences away exactly where a gentle plume needs them. */
extern i16 g_airP[AIR_W * AIR_H];

/* Zeroed on world reset and load. Pressure is derived from the cells every
   frame, so it is never saved -- it is a cache, not state. */
void airClear();

/* Rebuild the field over the world's live window. Called once per World::step,
   before the cell rules run, so a gas reads the pressure its own frame
   produced rather than the previous one's. */
void airStep(const World& w);

/* Pressure at a WORLD cell. Bounds-checked; outside the world reads as the
   ambient baseline so an edge never looks like a vacuum worth flowing into. */
int airAt(int x, int y);

/* The downhill direction from a world cell, as one of the eight neighbours or
   (0,0) when the pocket is flat. This is what gas movement consumes -- see
   updateGas.

   Returns the STEEPEST descent rather than a smooth vector, because the thing
   asking can only move to a neighbouring cell anyway; handing it a float
   direction would only mean rounding it back to the same eight choices. */
void airDownhill(int x, int y, int* dx, int* dy);

/* How strongly a crowded pocket pushes. Exposed so a harness can sweep it. */
extern int g_airPush;
