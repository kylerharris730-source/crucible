#pragma once
#include "world.h"

enum ViewMode {
    VIEW_NORMAL = 0,   /* materials, with hot cells glowing */
    VIEW_MATERIAL,     /* materials only -- no heat shown at all */
    VIEW_HEAT,         /* temperature as false colour, for inspecting heat */
    VIEW_COUNT
};

/* --- the view --------------------------------------------------------------
   How much of the world is on screen at once, in cells. The world is far
   larger (see SIM_W/SIM_H); this is the window onto it.

   Rendering is windowed rather than whole-world for a measured reason: drawing
   every cell each frame cost 0.27 ms at 512x384 and 8.8 ms at 2048x3072, and
   all of the extra was thrown away before it reached the screen. Windowed, the
   cost is the same at any world size. */
static const int VIEW_CELLS_W = 512;
static const int VIEW_CELLS_H = 384;

/* Fills `out` (VIEW_CELLS_W * VIEW_CELLS_H pixels, 0x00RRGGBB) with the region
   of the world whose top-left cell is (camX, camY), and returns the number of
   non-wall, non-empty cells IN VIEW.

   The camera is clamped by the caller; anything outside the world renders as
   the void colour rather than reading out of bounds. */
int renderView(const World& w, u32* out, int view, int camX, int camY);
