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
   and the underground runs from here to the bottom of the world. */
static const int SURFACE_Y = 1200;

void generateWorld(World& w);

/* Where the character should start: on the grass, in the plains, clear of
   anything. */
void worldSpawnPoint(float* outX, float* outY);
