#pragma once
#include "render.h"

/* --- lighting --------------------------------------------------------------

   How bright each visible cell is. Computed for the view and a margin around
   it, and held in a buffer the size of that rectangle -- there is still no
   per-cell light stored in the WORLD, which is the part that matters.

   That is the central decision here, and it is the same one the renderer and
   the simulation already made for their own reasons. A world-sized light field
   would be another 12 MB array beside cells/temp/bg, and worse, it would have
   to be kept correct everywhere at once, including in the thousands of chunks
   nobody is looking at. Light is a pure function of the cells around it, so
   deriving it from the world near the camera is both simpler and self-healing.

   --- what is reused, and what that cost ---
   This used to be recomputed in full every frame, which is the cheapest thing
   to be sure of and was also, measured, ninety percent of the frame -- more
   than the simulation, the renderer and everything else put together. It is now
   reused where the world has not moved and patched where it has; see
   lightUpdate for the three cases and what decides between them.

   The reason the original said "recompute it every frame" was a real one and it
   has not gone away: the failure mode of an incremental relight is a shadow
   that forgets to lift, and that is a bug which survives to release, because it
   needs one specific thing to have happened in one specific place to show up at
   all. Two things hold it off. The dirty region comes from the SIMULATION's own
   chunk rectangles rather than from a second set of marks kept up to date by
   hand, so it cannot miss a change that the simulation itself did not also miss
   (see lightAccumulateDirty). And the claim that a patch equals a full solve is
   checked rather than argued: run the same world twice, once patched and once
   recomputed, and compare the visible field cell for cell.

   That check has already earned its keep. It caught a patch region that was
   right for lamps and wrong for the sun -- daylight travels down the buffer in
   straight rays, so a change high up moves the answer in a wedge hanging below
   it, not in a ball around it, and the cells outside the region kept their old
   shading permanently. See the note in lightSolve.

   --- the margin ---
   Light is computed over a rectangle LARGER than the view, because a source
   just off the left edge of the screen must still spill onto what you can see.
   Without the margin, walking toward a lamp would make the wall in front of
   you brighten only once the lamp itself came into frame.

   127 is not a round number picked for comfort: it is exactly the furthest a
   source can reach, LIGHT_MAX divided by air's attenuation of 2 (see initLight
   in materials.cpp). It was 85 against an attenuation of 3, and the two moved
   together when torches were made to reach further -- which is the invariant
   this paragraph exists to state. At that width there is no seam to hide -- a source one
   cell outside the rectangle contributes nothing to the first cell inside it,
   so cutting the rectangle off is not an approximation at all. It is also the
   only thing here that costs area, so it is the number to look at if lighting
   ever needs to be cheaper: air's attenuation and this must move together, or
   lamps will start popping into existence at the edge of the screen. */
static const int LIGHT_MARGIN = 127;
static const int LIGHT_W = VIEW_CELLS_W + 2 * LIGHT_MARGIN;
static const int LIGHT_H = VIEW_CELLS_H + 2 * LIGHT_MARGIN;

/* Row-major over the padded rectangle whose top-left cell is
   (g_lightAnchorX, g_lightAnchorY). Use lightRow() to get at the part of it
   the renderer wants; the padding is scaffolding, not output. */
extern u8 g_light[LIGHT_W * LIGHT_H];

/* --- the field is anchored to the WORLD, not to the camera -----------------
   Where view cell (0,0) currently sits inside the buffer. It is LIGHT_MARGIN
   on the frame the field is computed, and drifts from there as the camera
   moves under a field that is being reused.

   That indirection is the whole reason a field can outlive the frame it was
   computed on. Anchored to the camera, reusing last frame's field after the
   player took a step would shade every cell with the value belonging to the
   cell beside it -- the light would visibly slide against the world. Anchored
   to the world, a reused field is in exactly the right PLACE and merely
   slightly out of date, which is a difference you cannot see at 60 Hz.

   The drift is bounded by LIGHT_DRIFT (see light.cpp): past that the margin on
   the trailing edge has worn thin enough for a source just off-screen to be
   missing, and the field is re-cut. */
extern int g_lightOfsX, g_lightOfsY;

/* Whether lighting is applied at all. Off restores the flat, fully-lit look,
   which is what the sandbox half of this program wants -- you cannot inspect a
   contraption you cannot see, and a heat rig buried in rock is unlit by
   definition. */
extern bool g_lightOn;

/* --- the day ---------------------------------------------------------------

   One counter, advanced a frame at a time and wrapped at DAY_LENGTH. It is the
   only piece of world state in this file, and it lives here because the only
   thing it does is scale the sky: night is not a separate lighting model, it is
   the same skylight with less of it.

   Twelve minutes, which is between Minecraft's twenty and Terraria's fifteen,
   and chosen from what a cycle has to be long enough to CONTAIN. A day should
   fit a trip out to the caves and back with something to show for it; much
   under ten minutes and night arrives while you are still walking to where you
   meant to dig, which trains players to ignore the surface entirely.

   Night is a little shorter than day -- see dayLight() -- because the
   interesting part is the transition and the pressure it puts on being outside,
   not the waiting.

   Saved, because a world that resets to noon every time you load it does not
   have a day/night cycle, it has a lighting effect. One u32. */
extern u32 g_worldTime;

static const u32 DAY_LENGTH = 60 * 60 * 12;   /* frames; twelve minutes */

/* Advance the clock one step. Separate from lightCompute because time passes
   whether or not the lighting is switched on, and because a headless harness
   wanting to test night has to be able to get there without a camera. */
void dayAdvance();

/* How bright the sun is right now, 0..255. Full daylight through the middle of
   the day, a genuine dusk and dawn at the ends, and a floor at night rather
   than zero -- see the note in light.cpp for why moonlight is not optional. */
int dayLight();

/* Is it dark enough outside for things to spawn on the surface? A threshold on
   dayLight() rather than a second clock, so the two can never disagree about
   what time it is. */
bool isNight();

/* Recompute the whole field, from nothing, for this camera. Unconditional: it
   is what lightUpdate falls back on, and what a headless harness wants when it
   is asking "what does this scene look like" rather than driving a game loop. */
void lightCompute(const World& w, int camX, int camY);

/* One frame's worth of lighting: reuse, patch, or recut as the scene demands.
   This is what the game loop calls. See the note above it in light.cpp for
   what it will and will not do.

   Must be called every frame even when it is expected to do nothing, because
   part of its job is to keep track of what the simulation has disturbed since
   the field was last computed. Skipping a call loses that frame's changes and
   the field goes quietly stale. */
void lightUpdate(const World& w, int camX, int camY);

/* What lightUpdate did on the frame just gone, and over how much of the field.
   Reported for the same reason activeChunks is: lighting is the most expensive
   thing in the frame, what it costs now depends on how much of it was avoided,
   and a stats line that says "24 ms" without saying "because it recut" tells
   you the price and not the reason. */
enum { LIGHT_REUSED, LIGHT_PATCHED, LIGHT_RECUT };
extern int g_lightWork;      /* one of the above */
extern int g_lightWorkPct;   /* share of the field solved, 0..100 */

/* Throw the field away. Anything that changes the world wholesale behind the
   simulation's back -- loading a save, regenerating -- has to say so, because
   the dirty tracking works in chunk rects and a world that was REPLACED has no
   meaningful set of changed chunks. */
void lightInvalidate();

/* The first light value of view row vy, so the render loop can walk it with
   the same linear stride it walks everything else. */
static inline const u8* lightRow(int vy) {
    return g_light + (vy + g_lightOfsY) * LIGHT_W + g_lightOfsX;
}

/* Brightness at one view cell, for things drawn ON TOP of the world after
   renderView has run -- the character, the tool in their hand. Without this
   they are the only objects in the game that ignore the light, which reads as
   them being self-illuminated: a figure standing in an unlit cave was the
   brightest thing on screen. Bounds are the caller's problem, since every one
   of them is already clipping to the view to write a pixel at all. */
static inline u32 viewShade(int vx, int vy) {
    return g_lightShade[lightRow(vy)[vx]];
}

/* Multiply a packed 0xRRGGBB colour by a 0..255 brightness. Two multiplies for
   three channels: the red and blue lanes are far enough apart in a 32-bit word
   that they can be scaled together without bleeding into each other. */
static inline u32 shadeColor(u32 c, u32 l) {
    const u32 rb = ((((c & 0xFF00FFu) * l) >> 8) & 0xFF00FFu);
    const u32 g  = ((((c & 0x00FF00u) * l) >> 8) & 0x00FF00u);
    return rb | g;
}
