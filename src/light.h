#pragma once
#include "render.h"

/* --- lighting --------------------------------------------------------------

   How bright each visible cell is. Computed fresh every frame, for the view
   and a margin around it, and thrown away -- there is no per-cell light stored
   in the world at all.

   That is the central decision here, and it is the same one the renderer and
   the simulation already made for their own reasons. A stored light field
   would be another 12 MB array beside cells/temp/bg, and worse, it would have
   to be KEPT CORRECT: every dug cell, every falling grain of sand and every
   flame that flickers out changes what is lit, so the world would need an
   incremental relight with a dirty queue of its own, and the bug where a
   shadow forgets to lift is exactly the kind that survives to release. Light
   is a pure function of the cells around it, so recomputing it is both simpler
   and self-healing: whatever the world looks like this frame is what you see.

   The cost of that is paid once per frame over a fixed area, so like
   renderView it does not grow with the world. See lightCompute for the
   measured figure.

   --- the margin ---
   Light is computed over a rectangle LARGER than the view, because a source
   just off the left edge of the screen must still spill onto what you can see.
   Without the margin, walking toward a lamp would make the wall in front of
   you brighten only once the lamp itself came into frame.

   85 is not a round number picked for comfort: it is exactly the furthest a
   source can reach, LIGHT_MAX divided by air's attenuation of 3 (see initLight
   in materials.cpp). At that width there is no seam to hide -- a source one
   cell outside the rectangle contributes nothing to the first cell inside it,
   so cutting the rectangle off is not an approximation at all. It is also the
   only thing here that costs area, so it is the number to look at if lighting
   ever needs to be cheaper: air's attenuation and this must move together, or
   lamps will start popping into existence at the edge of the screen. */
static const int LIGHT_MARGIN = 85;
static const int LIGHT_W = VIEW_CELLS_W + 2 * LIGHT_MARGIN;
static const int LIGHT_H = VIEW_CELLS_H + 2 * LIGHT_MARGIN;

/* Row-major over the padded rectangle whose top-left cell is
   (camX - LIGHT_MARGIN, camY - LIGHT_MARGIN). Use lightRow() to get at the
   part of it the renderer wants; the padding is scaffolding, not output. */
extern u8 g_light[LIGHT_W * LIGHT_H];

/* Whether lighting is applied at all. Off restores the flat, fully-lit look,
   which is what the sandbox half of this program wants -- you cannot inspect a
   contraption you cannot see, and a heat rig buried in rock is unlit by
   definition. */
extern bool g_lightOn;

void lightCompute(const World& w, int camX, int camY);

/* The first light value of view row vy, so the render loop can walk it with
   the same linear stride it walks everything else. */
static inline const u8* lightRow(int vy) {
    return g_light + (vy + LIGHT_MARGIN) * LIGHT_W + LIGHT_MARGIN;
}

/* Multiply a packed 0xRRGGBB colour by a 0..255 brightness. Two multiplies for
   three channels: the red and blue lanes are far enough apart in a 32-bit word
   that they can be scaled together without bleeding into each other. */
static inline u32 shadeColor(u32 c, u32 l) {
    const u32 rb = ((((c & 0xFF00FFu) * l) >> 8) & 0xFF00FFu);
    const u32 g  = ((((c & 0x00FF00u) * l) >> 8) & 0x00FF00u);
    return rb | g;
}
