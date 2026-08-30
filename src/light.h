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
   RECOMPUTED IN FULL EVERY FRAME. lightUpdate is a direct call to lightCompute
   and there is nothing incremental left in it.

   There was, once, and the history is worth keeping because it explains the
   shape of what remains. Lighting was measured at ninety percent of a frame --
   more than the simulation, the renderer and everything else together -- and
   was rewritten to reuse the field where the world had not moved and patch it
   where it had, with a dirty region taken from the SIMULATION's own chunk
   rectangles so it could not miss a change the simulation had not also missed.
   Moving to quarter resolution then made a full solve cheap enough that the
   patching was not worth what it cost to be sure of, and it was removed.

   Removing it was the right call, because the failure mode of an incremental
   relight is a shadow that forgets to lift: a bug that survives to release,
   since it needs one specific thing to have happened in one specific place to
   appear at all. That is not hypothetical here. The check that a patch equals
   a full solve once caught a patch region that was right for lamps and wrong
   for the sun -- daylight travels down the buffer in straight rays, so a change
   high up moves the answer in a wedge hanging below it, not a ball around it,
   and the cells outside the region kept their old shading permanently.

   The method outlived the machinery, and anything touching this file should
   still use it: solve the same world both ways and compare the VISIBLE field
   sample for sample. tools/lightmargin.cpp does exactly that across day, night
   and dusk, at the surface and deep underground, and it is how the margin
   below was set.

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
/* --- the field is COARSER than the world -----------------------------------
   One light sample per LIGHT_CELL x LIGHT_CELL block of world, smoothed back up
   when it is applied. This is the single biggest thing about the cost of
   lighting, so it is worth being clear about why it is not a compromise.

   Light here is a diffuse field: it is produced by two raster sweeps that
   spread it outward at a couple of units per cell, and by soaks that fade over
   fifteen or twenty. Nothing in it can change abruptly between one cell and the
   next -- by construction, the sharpest edge it can hold is the attenuation of
   a single step. Sampling something that smooth once per cell is measuring a
   gentle slope with a micrometer: the answer is 16x the work and carries almost
   no information the coarse version does not.

   At 4, the field is 192x160 = 30,720 samples where it was 766x638 = 488,708.
   The costs that dominated -- four sweeps and two soaks over the whole field --
   fall with the area.

   What is genuinely lost is detail below the block size: a one-cell crack no
   longer throws a crisp shadow, and a lamp's pool of light has a softer edge.
   Neither is something the old field rendered convincingly anyway, because the
   display mapping smooths that range out regardless.

   The margin stays exactly one source's reach, as it always has -- it is just
   counted in samples now. Air attenuates 2 per CELL, so 8 per sample step, and
   LIGHT_MAX / 8 is 32 samples, which is the same 128 world cells the old
   127-cell margin covered. The invariant in the paragraph above is unchanged;
   only its units moved. */
static const int LIGHT_SHIFT = 2;
static const int LIGHT_CELL  = 1 << LIGHT_SHIFT;   /* world cells per sample */

/* 128 -> 64, which is the reach the paragraph above says this is supposed to
   be, restated in the units it is now counted in.

   The invariant is "exactly one source's reach". Air is opacity 1 per cell
   (KIND_EMPTY, initLight) and a sample spans LIGHT_CELL = 4 cells, so a sample
   step costs 4, and LIGHT_MAX / 4 is 63 samples. The margin was 128 -- twice
   the reach it claims to be. The doubling looks like it was carried forward
   from the previous value rather than recomputed: 60 was already close to
   twice the old 32-sample reach, and when halving the attenuation doubled the
   reach to 63, the margin doubled to 128 alongside it instead of being set to
   the new reach.

   Area is the whole cost of lighting, and this is the number that governs it.
   Measured over seven scenes -- day, night and dusk, at the surface and 1200
   cells underground, with lamps 40 and 200 cells off screen -- the VISIBLE
   field is bit-identical at 64 and the solve goes from 5.8 ms to 2.5 ms.
   Lighting was 77% of a settled frame and is now about 55%.

   Do not take it below 64 for the ordinary build. 48 and 32 also measured
   identical, but only because no test scene puts a lamp in open air at exactly
   the distance where it would matter; the invariant is what protects the case
   nobody thought to build, and below 63 samples there are lamp positions that
   genuinely cannot reach in. A build that accepts that trade -- see the
   lightweight notes -- can override it. */
#ifndef LIGHT_MARGIN
#define LIGHT_MARGIN LIGHT_MARGIN_DEFAULT
#endif
static const int LIGHT_MARGIN_DEFAULT = 64;        /* in SAMPLES, not cells */
static const int LIGHT_W = VIEW_CELLS_W / LIGHT_CELL + 2 * LIGHT_MARGIN;
static const int LIGHT_H = VIEW_CELLS_H / LIGHT_CELL + 2 * LIGHT_MARGIN;

/* The same coverage in WORLD CELLS, for code that reasons about the AREA the
   light field spans rather than about samples.

   Two of these exist because the field stopped being one sample per cell, and
   everything that used LIGHT_W or LIGHT_MARGIN as a distance silently changed
   meaning when it did. The spawner was one: it draws candidate sites from the
   lit rectangle so that creatures appear off screen but near enough to matter,
   and reading a sample count as a cell count shrank that ring from 127 cells to
   32, hard against the edge of the view -- creatures arriving in front of the
   player instead of somewhere behind the dark.

   Anything asking "how far does the lit area reach" wants these. Anything
   indexing g_light wants the ones above. */
static const int LIGHT_MARGIN_CELLS = LIGHT_MARGIN * LIGHT_CELL;
static const int LIGHT_CELLS_W      = VIEW_CELLS_W + 2 * LIGHT_MARGIN_CELLS;
static const int LIGHT_CELLS_H      = VIEW_CELLS_H + 2 * LIGHT_MARGIN_CELLS;

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

/* Where the view's top-left cell sits in the world, and where the field's
   sample (0,0) does. The anchor is always a multiple of LIGHT_CELL: the sample
   grid is pinned to the WORLD, not to the camera, so that walking does not slide
   the sample points under the terrain. Unpinned, every block boundary would
   crawl across the scene and the shading would visibly crawl with it. */
extern int g_lightViewX, g_lightViewY;
extern int g_lightAnchorX, g_lightAnchorY;

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
/* --- light from things that are not cells ----------------------------------

   Emission is read out of the GRID: g_matLight per material, gathered per
   block. Anything that lights the world without being a cell is therefore
   invisible to it -- and the overlays are exactly the things you would most
   want to glow. A drone following you through a cave is the case this exists
   for.

   The alternatives were worse in instructive ways. Having the drone STAMP a
   lamp cell under itself each frame would light correctly and then edit the
   world sixty times a second: chunks permanently dirty, the falling-sand rules
   fighting a cell that teleports, and a drone crossing rock quietly carving a
   tunnel through it. Making the drone a placed device is not a follower at all.

   So: a small registry, cleared and refilled each frame by whoever owns the
   thing, folded into the emission field after the gather and before the sweeps.
   The sweeps do not care where a source came from, which is the whole reason
   this is cheap -- it is one write per source, not a second lighting model.

   Cleared every frame rather than tracked, because a follower's position is not
   a thing worth keeping in step: it is easier to say where it is now than to
   remember where it was and correct for the difference. */
void lightClearDynamic();
/* The registry grows for this frame's sources. Moving followers and persistent
   fixtures share the same light solve without imposing a source-count cap. */
void lightAddDynamic(int wx, int wy, u8 level);

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

/* Brightness at one view cell, interpolated between the four samples around it.

   The interpolation is not decoration -- without it the shading is delivered in
   4x4 blocks and the eye finds every one of them. Blocks are far more visible
   than the detail the coarse field gave up, because a straight edge that has no
   counterpart in the world reads as a fault in the picture, while a soft shadow
   just reads as a soft shadow. */
u8 lightAt(int vx, int vy);

/* Brightness at a WORLD cell, including the MARGIN around the view.

   lightAt and lightRow both speak in view coordinates and cover only what
   is on screen -- lightRow in particular hands back a row of exactly
   VIEW_CELLS_W bytes. That is right for drawing and wrong for anything
   asking about the padded rectangle, which is most of the field: the
   spawner picks candidate sites from the margin BY DESIGN, so every point
   it wants to know about is off screen, and indexing a view row with an
   off-screen coordinate reads past the end of it.

   This samples the buffer where the buffer actually is. Nearest sample
   rather than interpolated: callers asking whether somewhere is dark do not
   need a smooth answer, and the field is smooth to begin with.

   Returns 0 outside the computed rectangle -- there is no measurement
   there, and reporting darkness is the answer that does not invent light
   that was never solved for. */
u8 lightAtWorld(int wx, int wy);


/* The whole of view row vy, smoothed up to one value per cell, so the render
   loop can walk it with the same linear stride it walks everything else. The
   row is built once and cached; calling it for the row being drawn costs one
   pass over 512 bytes rather than a bilinear sample per pixel. */
const u8* lightRow(int vy);

/* Brightness at one view cell, for things drawn ON TOP of the world after
   renderView has run -- the character, the tool in their hand. Without this
   they are the only objects in the game that ignore the light, which reads as
   them being self-illuminated: a figure standing in an unlit cave was the
   brightest thing on screen. Bounds are the caller's problem, since every one
   of them is already clipping to the view to write a pixel at all. */
static inline u32 viewShade(int vx, int vy) {
    return g_lightShade[lightAt(vx, vy)];
}

/* Multiply a packed 0xRRGGBB colour by a 0..255 brightness. Two multiplies for
   three channels: the red and blue lanes are far enough apart in a 32-bit word
   that they can be scaled together without bleeding into each other. */
static inline u32 shadeColor(u32 c, u32 l) {
    const u32 rb = ((((c & 0xFF00FFu) * l) >> 8) & 0xFF00FFu);
    const u32 g  = ((((c & 0x00FF00u) * l) >> 8) & 0x00FF00u);
    return rb | g;
}
