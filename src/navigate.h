#pragma once
#include "common.h"

struct World;

/* --- routing: how a walker finds its way round a wall -------------------------

   Creatures used to answer one question -- is the player to my left or my
   right -- and walk that way. In an open cavern that is enough, and in a cave
   with any structure at all it is a cage: moveAxis zeroes vx against a wall
   and flips `facing`, the chase re-derives facing from the player next frame,
   and the creature presses, turns, turns back, forever. Measured in
   tests/enemy_path.cpp: a husk two rooms away spent 5,867 frames of 6,000
   motionless and never got closer than 370 cells, on a route a lured husk
   walks in 1,357.

   The fix is a FLOW FIELD, not a path. One breadth-first sweep outward from
   the player fills a coarse grid with "how many steps from here to the
   player", and every creature then reads one number and its neighbours. That
   choice is the whole design:

     - it is O(one search) for the entire roster rather than O(one search per
       creature), so a room full of husks costs what one husk costs;
     - it needs no per-creature path to store, invalidate, or re-plan when the
       terrain changes -- and in a falling-sand game the terrain changes
       constantly;
     - a creature that walks into a wall mid-route simply reads the field again
       and is already re-planned.

   It is COARSE on purpose: one node per 4x4 block of world cells. Creatures
   here are 8 to 24 cells tall, so a 4-cell node is finer than the things
   moving through it, and a full-resolution grid would cost sixteen times as
   much to fill for detail nothing can use.

   It is also WINDOWED, covering a fixed span centred on the player rather than
   the whole world -- SIM_W * SIM_H is 37 million cells and no search of that
   runs at 60 fps. Outside the window a creature falls back to the old straight
   line, which is exactly the behaviour it has today and is fine, because
   outside the window it is far off screen. */

static const int NAV_SHIFT = 2;
static const int NAV       = 1 << NAV_SHIFT;   /* world cells per nav node */

/* The window, sized off the SCREEN rather than off the despawn radius.

   The screen is VIEW_CELLS_W x VIEW_CELLS_H = 512 x 384 cells, and this is
   that plus a margin of 320 cells on every side. The margin is what stops a
   creature from being routed only once it walks into shot: it is already
   following the field well before it is visible, so it comes round the corner
   rather than appearing to notice you.

   It deliberately does NOT reach ENT_DESPAWN_DIST, which is 700. Covering that
   would mean a 1400-cell square, three and a half times the area, for
   creatures two screens away that nobody can see. Measured on a generated
   world: 1.14 ms to rebuild a 768-cell square, and cost scales with area.
   Beyond the window a creature falls back to the straight-line chase it has
   always had, which off screen is indistinguishable from anything better. */
static const int NAV_W     = 288;              /* 1152 world cells across */
static const int NAV_H     = 224;              /*  896 world cells down */

/* Two size classes rather than one field, because one field cannot serve both.
   Built for a husk (22 cells) it would refuse every tunnel a slime (8 cells)
   fits through; built for a slime it would route a husk into rock. The search
   is the cheap half -- the expensive half is reading the terrain, and that is
   shared -- so a second class costs almost nothing.

   Heights are in NODES and round UP, so a class is never optimistic about what
   fits. */
enum NavClass { NAV_SHORT = 0, NAV_TALL, NAV_CLASSES };

/* Re-read the terrain around the players and re-run the search, but only every
   NAV_PERIOD frames -- call it every frame and let it decide. */
void navUpdate(const World& w, const float* seedX, const float* seedY, int seeds);

/* Forget everything. For world loads and for the tests. */
void navReset();

/* Which way should a walker standing at (footX, footY) go to reach a player?

   Returns false when the field cannot answer -- outside the window, inside
   rock, or in a pocket with no route -- and the caller must then fall back to
   its own straight-line chase. That fallback is not a formality: it is what
   guarantees this change can only ever add reachability, never remove it.

   `dirX` comes back as -1, 0 or +1, and a returned TRUE with dirX of 0 is a
   real answer rather than a non-answer: it means the route from here is
   straight down, there is nothing to steer, and the caller should stop
   steering rather than fall back to the straight line. Confusing those two is
   what turns a creature at the mouth of a shaft into a creature pacing beside
   one.

   `climb` is set when the next node is above this one, which is the
   creature's cue to hop. */
bool navHeading(int footX, int footY, int agentH, int* dirX, bool* climb);

/* Diagnostics, for the harnesses: how many nodes the last search reached, and
   whether the field currently covers a point at all. */
int  navReached(int cls);
int  navDistAt(int worldX, int worldY, int cls);
int  navFloorAt(int worldX, int worldY);
bool navCovers(int worldX, int worldY);
