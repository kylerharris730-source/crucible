#pragma once
#include "world.h"

/* --- terrain generation ----------------------------------------------------

   Builds the whole surface in one pass: a heightmap, the material columns
   under it, the natural background behind those, and the per-chunk zone
   labels. It lives in its own file because it is the one part of the program
   that decides what the world IS, and everything else only reacts to that.

   The regions run left to right and blend into one another rather than butting
   up, so there is no line in the world where one biome stops and the next
   starts.

   The heightmap is also published, because several things need to know where
   the ground is without searching for it -- spawning the character, and every
   test that wants to stand something on the surface. */
extern int g_surfaceY[SIM_W];

/* Where the STONE starts in each column -- the underside of the soil. Published
   for the same reason the heightmap is: several things need to know where the
   rock begins without searching for it, and the one that forced the issue is
   caves.

   A cave wants a ceiling that holds itself up, and dirt is a POWDER. Carving into
   soil produces a hole with nothing above it, and measured, that is not a cosmetic
   problem: 51 cave cells with a soil ceiling collapsed into roughly 1900 cells of
   open surface within 1200 frames, because each grain that falls exposes the one
   above it and the failure walks all the way up through the soil column. The
   world caved in the first time it was simulated. Anything carving or placing
   underground should be asking about this array, not about the surface. */
extern int g_stoneY[SIM_W];

/* Nominal sea-level surface, in world cells. Everything is measured from here:
   the plains wander a little either side of it, the mountain rises far above,
   and the underground runs from here to the bottom of the world.

   Moved with SIM_H rather than left alone, and it has to be: this is the single
   number that splits the world into sky and rock, so doubling the height while
   leaving it at 1200 would have spent the entire new allowance on underground
   and none of it on air. Half of SIM_H keeps the proportion the world was tuned
   at while doubling both halves in absolute terms. */
static const int SURFACE_Y = SIM_H / 2;

/* --- the three cave layers -------------------------------------------------

   Depths BELOW THE LOCAL STONE LINE at which one layer ends and the next
   begins, with a band of MAT_STRATUM sealing each join (see STRATUM_THICK).
   Quoted against the stone line rather than as absolute y for the same reason
   the ore bands are: the surface wanders about a hundred cells and the mountain
   far more, so an absolute depth would put the boundary halfway up a hillside
   on one seed and below the ore on another.

   These are MEASURED against the real world rather than derived from SIM_H, and
   the difference is large enough to matter: the mean stone line sits at y=2962,
   not at SURFACE_Y, because soil depth and the lake basin both push it down. So
   the underground is about 3180 cells, not the 3900 that halving the world
   height would suggest. The first cut of these constants was picked from the
   optimistic figure and gave layer 3 only 730 cells -- barely a fifth of the
   underground for the layer that has to hold the endgame.

   At 1050 and 2050 the three layers are about 1050, 1000 and 1130 cells: even,
   and thirty-five body heights each. Anything that changes the surface, the
   soil depth or the world height has to come back and re-measure, which the
   layers harness does in one line. */
static const int LAYER1_DEPTH  = 1050;   /* stone line .. here is layer 1 */
static const int LAYER2_DEPTH  = 2050;   /* .. and here is layer 2 */
/* Thickness of the sealed band between layers. Thick enough that it cannot be
   mistaken for an ordinary vein and cannot be skirted by a cave worm that
   happens to graze it, thin enough that breaking through once you CAN is a
   moment rather than a chore. */
static const int STRATUM_THICK = 24;

void generateWorld(World& w);

/* How many trees the last generation put down, for the HUD and for tests. */
extern int g_treesPlanted;

/* Where the character should start: on the grass, in the plains, clear of
   anything. */
void worldSpawnPoint(float* outX, float* outY);
