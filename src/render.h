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

/* Fills `out` (0x00RRGGBB pixels) with the region
   of the world whose top-left cell is (camX, camY), and returns the number of
   non-wall, non-empty cells IN VIEW.

   The camera is clamped by the caller; anything outside the world renders as
   the void colour rather than reading out of bounds.

   `lit` shades every pixel by the light field, which the caller must have
   computed for this same camera position first -- see lightCompute(). It is a
   flag rather than a pointer parameter because the light buffer is a single
   global with a fixed geometry tied to the view: there is never a second one to
   choose between, and passing it in would only create the possibility of
   passing one that does not line up. Defaults off, so every headless harness
   that renders the world keeps seeing material colours rather than a dark
   rectangle. */
/* `cellsW`/`cellsH` override how much world is drawn. `outputStride` is the
   distance between output rows in pixels; zero means tightly packed cellsW.
   A larger stride lets the zoomed game draw only the visible top-left region
   while retaining the full-width buffer expected by its sprite overlays.
   Pixels outside that region are left untouched. Non-positive dimensions or
   a stride smaller than cellsW draw nothing and return zero.

   Smaller views use the matching top-left region of the fixed light field.
   Oversized views (used by powderlike) render unlit rather than reading past
   its VIEW_CELLS_W/H geometry. */
int renderView(const World& w, u32* out, int view, int camX, int camY, bool lit = false,
               int cellsW = VIEW_CELLS_W, int cellsH = VIEW_CELLS_H,
               int outputStride = 0);
