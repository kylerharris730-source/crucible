#pragma once
#include "world.h"

/* --- air, as a velocity field ------------------------------------------------

   Empty is VACUUM in this simulation. A gas rises because a rule says gases
   rise; it spreads because a rule says it diffuses. Both work, and together
   they have one hard limit that no amount of tuning escapes: a gas cell gets
   ONE MOVE PER FRAME, so rise speed and volume filling compete for it. Spend
   more moves going up and the plume becomes a column; spend more spreading and
   it climbs like treacle.

   A pressure field does not fix that -- it was tried on the `air-pressure`
   branch and measured worse than doing nothing in all three formulations,
   because a force does not add moves, it only re-spends them.

   What breaks the trade is ADVECTION: the gas is carried, several cells at a
   time, by a medium that has its own momentum. One updraft move can lift a
   parcel five cells while the return flow carries its neighbour sideways, and
   neither came out of the other's budget.

   --- the model ---
   The Powder Toy's, which is the proven design for this in a falling-sand game:
   a coarse grid holding pressure and a velocity vector, where

     - gas cells inject upward momentum (buoyancy),
     - velocity is advected by itself, so momentum persists and curls,
     - divergence becomes pressure, pressure gradient becomes velocity, which
       is what makes the flow behave incompressibly enough to circulate rather
       than just blow outward,
     - solid pockets are walls: no velocity, and no flow across them.

   Gas cells then read the local velocity and move along it, up to
   AIR_ADVECT_MAX cells in one frame.

   --- what this buys that the old rules cannot ---
   Circulation. A plume rising in a closed room drags air up with it, that air
   has to come back down somewhere, and the return flow pushes the cloud out
   sideways at the top and back in at the bottom. That is a convection cell, and
   it is not expressible as a per-cell preference no matter how it is weighted.

   HEAT comes along for free rather than needing its own advection pass: a cell
   carries its temperature when it moves (see tryMove), so gas that travels five
   cells has moved its heat five cells. Nothing here touches the temperature
   field directly.

   --- cost ---
   One value per 4x4 block, computed only over the live window. The fields are
   full-world so that a camera move needs no remapping -- an air cell's index
   comes from its world position and never changes -- at 4.7 MB each in i16
   against a world that is already 190 MB of cells and temperature. */

static const int AIR_SHIFT = 2;                  /* 4x4 world cells per air cell */
static const int AIR_W = SIM_W >> AIR_SHIFT;
static const int AIR_H = SIM_H >> AIR_SHIFT;

/* Velocity is fixed point, AIR_V_ONE units to one WORLD CELL per frame. Fixed
   rather than float because there are five full-world arrays here and halving
   each of them is worth more than the arithmetic is. */
static const int AIR_V_ONE = 256;

/* How far a gas parcel may be carried in one frame. This is the number the
   whole feature exists to make larger than one -- see the note above. */
static const int AIR_ADVECT_MAX = 6;

extern i16 g_airVX[AIR_W * AIR_H];
extern i16 g_airVY[AIR_W * AIR_H];
extern i16 g_airP [AIR_W * AIR_H];

/* Zeroed on world reset and load. The field is derived from the cells and from
   its own previous state; it is never saved. A load simply starts still. */
void airClear();

/* One step of the air solver, over the world's live window. Called at the top
   of World::step so gas reads the field its own frame produced. */
void airStep(const World& w);

/* Local air velocity at a WORLD cell, in AIR_V_ONE units. Zero outside the
   world and inside walls. */
void airVelocity(int x, int y, int* vx, int* vy);

/* Pressure at a world cell, for diagnostics and for the debug overlay. */
int airPressureAt(int x, int y);

/* --- the dials -------------------------------------------------------------
   Exposed rather than static so a harness can sweep them without a rebuild per
   value, which is what made tuning this tractable at all. */
extern int g_airBuoyancy;    /* upward momentum injected per gas cell */
extern int g_airPressGain;   /* divergence -> pressure */
extern int g_airGradGain;    /* pressure gradient -> velocity */
extern int g_airDamp;        /* velocity retained per frame, of 256 */
extern int g_airOn;          /* 0 disables the whole solver */
