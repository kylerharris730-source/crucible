#include "light.h"
#include <string.h>
#include <vector>

u8   g_light[LIGHT_W * LIGHT_H];
bool g_lightOn = true;
int  g_lightOfsX = LIGHT_MARGIN;
int  g_lightOfsY = LIGHT_MARGIN;
int  g_lightViewX = 0, g_lightViewY = 0;
int  g_lightAnchorX = 0, g_lightAnchorY = 0;

/* --- a rectangle of the light buffer ---------------------------------------
   Buffer coordinates, inclusive on all four sides, empty when x1 < x0. Every
   pass below takes one, because every pass can now be asked to redo a part of
   the field rather than all of it. */
struct LRect { int x0, y0, x1, y1; };
static inline bool lrEmpty(const LRect& r) { return r.x1 < r.x0 || r.y1 < r.y0; }
static const LRect LR_ALL = { 0, 0, LIGHT_W - 1, LIGHT_H - 1 };

/* Per-cell attenuation, gathered once so the sweeps never touch Cell again.
   Worth its own array rather than reading g_matOpacity[cells[i].mat] inside the
   sweeps: the sweeps read every cell four times over, and this turns each of
   those into one sequential byte load instead of a dependent lookup through a
   4-byte struct in a 4096-wide grid. */
static u8 g_att[LIGHT_W * LIGHT_H];

/* Skylight only, per cell, as the rays delivered it -- no lamps, nothing the
   propagation sweeps added. Kept because sunSoak needs to be about the sun
   specifically (see the note there), and by the time it runs g_light has
   everything else mixed into it. */
static u8 g_sky[LIGHT_W * LIGHT_H];

/* Solidity, gathered alongside attenuation, for the two soak passes -- both of
   which need to know where material starts and stops and neither of which
   should be re-reading Cell to find out. */
static u8 g_solid[LIGHT_W * LIGHT_H];

/* The solid soak's own field. See solidSoak(). */
static u8 g_soak[LIGHT_W * LIGHT_H];

/* Anything that is not solid or liquid lets daylight straight through. Gases
   attenuate light (see g_matOpacity) but they do not cast a shadow, which is
   the difference between smoke dimming a room and smoke turning it to night. */
static inline bool lightOpen(u8 m) {
    return m == MAT_EMPTY || MATS[m].kind == KIND_GAS || g_matSheer[m] != 0;
}

/* --- putting a coarse field back onto the screen ----------------------------
   Bilinear between the four samples surrounding a view cell. The sample grid is
   pinned to world positions that are multiples of LIGHT_CELL, so a cell's
   position between samples is just its low bits, and walking sideways slides
   the weights smoothly instead of jumping the sample points.

   The half-cell shift is what centres it: sample s stands for the block of
   cells [s*LIGHT_CELL, s*LIGHT_CELL + LIGHT_CELL), whose middle is half a block
   in. Without the shift the whole field is displaced half a block up and left,
   which does not look like an offset -- it looks like the shadows do not line
   up with the things casting them. */
static inline int lightBiasedX(int vx) {
    return (g_lightViewX + vx - g_lightAnchorX) * 2 - (LIGHT_CELL - 1);
}
static inline int lightBiasedY(int vy) {
    return (g_lightViewY + vy - g_lightAnchorY) * 2 - (LIGHT_CELL - 1);
}

u8 lightAt(int vx, int vy) {
    const int fx = lightBiasedX(vx), fy = lightBiasedY(vy);
    int sx = fx >> (LIGHT_SHIFT + 1), sy = fy >> (LIGHT_SHIFT + 1);
    const int tx = fx - (sx << (LIGHT_SHIFT + 1));
    const int ty = fy - (sy << (LIGHT_SHIFT + 1));
    sx = imax(0, imin(sx, LIGHT_W - 2));
    sy = imax(0, imin(sy, LIGHT_H - 2));
    const u8* p = g_light + sy * LIGHT_W + sx;
    const int top = (int)p[0] * ((LIGHT_CELL * 2) - tx) + (int)p[1] * tx;
    const int bot = (int)p[LIGHT_W] * ((LIGHT_CELL * 2) - tx) + (int)p[LIGHT_W + 1] * tx;
    return (u8)((top * ((LIGHT_CELL * 2) - ty) + bot * ty)
                >> (2 * (LIGHT_SHIFT + 1)));
}

/* One view row, smoothed up once and handed to the renderer as a plain array so
   its inner loop stays a linear byte walk. Two horizontal lerps per row and one
   vertical blend per cell, rather than a full bilinear per pixel. */
const u8* lightRow(int vy) {
    static u8 row[VIEW_CELLS_W];
    static int cached = -1, cachedX = -1, cachedY = -1;
    if (cached == vy && cachedX == g_lightViewX && cachedY == g_lightViewY) return row;
    cached = vy; cachedX = g_lightViewX; cachedY = g_lightViewY;

    const int fy = lightBiasedY(vy);
    int sy = fy >> (LIGHT_SHIFT + 1);
    const int ty = fy - (sy << (LIGHT_SHIFT + 1));
    sy = imax(0, imin(sy, LIGHT_H - 2));
    const u8* up = g_light + sy * LIGHT_W;
    const u8* dn = up + LIGHT_W;
    const int full = LIGHT_CELL * 2;

    for (int vx = 0; vx < VIEW_CELLS_W; ++vx) {
        const int fx = lightBiasedX(vx);
        int sx = fx >> (LIGHT_SHIFT + 1);
        const int tx = fx - (sx << (LIGHT_SHIFT + 1));
        sx = imax(0, imin(sx, LIGHT_W - 2));
        const int top = (int)up[sx] * (full - tx) + (int)up[sx + 1] * tx;
        const int bot = (int)dn[sx] * (full - tx) + (int)dn[sx + 1] * tx;
        row[vx] = (u8)((top * (full - ty) + bot * ty) >> (2 * (LIGHT_SHIFT + 1)));
    }
    return row;
}

/* --- reading the world at sample resolution ---------------------------------
   One sample stands for a LIGHT_CELL x LIGHT_CELL block, and the three things
   the solve needs from it are not summarised the same way, because they answer
   different questions.

   Emission takes the MAXIMUM: a single torch cell in the block means the block
   emits. Averaging would dim a lamp by the sixteen cells of air around it and a
   torch would barely register.

   Attenuation takes the MEAN, scaled by how many cells a step now crosses. That
   is what makes partial occlusion work: a block half filled with rock stops
   roughly half as much light as a solid one, which is how a coarse field
   represents a thin wall at all.

   Solidity asks whether the block is mostly material, because the soak passes
   want "is this rock" rather than "how much light gets through it".

   The occluding half is read on a 2x2 lattice rather than all sixteen cells.
   Sixteen reads per sample is the same traffic over the world as the per-cell
   field did, which would have thrown away most of the saving; four catches
   anything two cells across, and below that the coarse field has nothing to
   say about how much light gets through.

   EMISSION is different, and the difference is not a matter of degree. Sampling
   is a statement that missing something costs a little accuracy -- true of an
   occluder, where the block still attenuates roughly right. A source you fail
   to look at is not slightly wrong, it is ABSENT: there is no light to be
   inaccurate about. Measured on the lattice alone, twelve of the sixteen places
   a lamp can sit in a block lit nothing at all, and which twelve depended on
   nothing but its coordinates.

   That is worse than it sounds, because the single-cell sources MOVE. A torch
   is a 14x14 device and always covers a sampled offset, so it looked fine; fire
   and embers are one cell and drift, so they would wink in and out as they
   crossed the lattice.

   So emission scans the whole block and the rest keeps its lattice. */
static inline void sampleBlock(const World& w, int wx, int wy,
                               u8* emit, u8* att, u8* solid, u8* sheer) {
    int e = 0, a = 0, s = 0, h = 0;
    for (int oy = 0; oy < LIGHT_CELL; ++oy) {
        const int y = wy + oy;
        for (int ox = 0; ox < LIGHT_CELL; ++ox) {
            const int x = wx + ox;
            const u8 m = (x < 0 || x >= SIM_W || y < 0 || y >= SIM_H)
                       ? (u8)MAT_WALL : w.cells[y * SIM_W + x].mat;
            const int l = g_matLight[m];
            if (l > e) e = l;
            /* Occlusion keeps the 2x2 lattice; only emission needed the whole
               block. Reading all sixteen for this too was tried and reverted --
               it cost 1.07 -> 1.25 ms and fixed nothing measurable. The case it
               was aimed at, a one-cell door not blocking light, is bound by the
               RESOLUTION rather than by the sampling: one opaque cell is a
               sixteenth of its block whether you look at it or not. */
            if ((ox & 1) && (oy & 1)) {
                a += g_matOpacity[m];
                h += g_matSheer[m];
                s += lightOpen(m) ? 0 : 1;
            }
        }
    }
    const int n = (LIGHT_CELL / 2) * (LIGHT_CELL / 2);
    *emit  = (u8)e;
    /* Mean per cell, times the cells one sample step crosses. */
    int perStep = (a * LIGHT_CELL) / n;
    /* --- open media attenuate HALF as much -------------------------------
       This is the one lever on how far light carries, and it is applied here
       rather than in g_matOpacity because air is already at 1 and there is no
       smaller integer. Halving at the point the per-step figure is computed is
       the same change expressed where there is still resolution to express it
       in.

       Only where the block is NOT solid, and that restriction is the whole
       design. Halving everything would double how deep light seeps into rock
       as well -- measured, a stone wall goes from stopping light in about 7
       cells to about 13, which reads as walls glowing rather than as a brighter
       cave. Open air, gas and water carry twice as far; anything you built to
       keep the light out keeps working exactly as it did.

       LIGHT_MARGIN moves with this and MUST -- see the note on it in light.h.
       The margin is exactly one source's reach, which is what makes cutting the
       light rectangle off exact rather than approximate. */
    if (!(s * 2 >= n)) perStep = (perStep + 1) / 2;
    /* A block that emits does not get to smother itself. Per cell, materials.cpp
       already forces every light source transparent for exactly this reason; at
       block resolution the averaging quietly undid it, because a lamp set into
       rock shares its block with the rock and inherited its opacity. The lamp
       then attenuated its own light before any of it left the block, which
       showed up as a source lighting a couple of cells and stopping. */
    if (e && perStep > 3 * LIGHT_CELL) perStep = 3 * LIGHT_CELL;
    *att   = (u8)(perStep > 255 ? 255 : perStep);
    *solid = (u8)(s * 2 >= n);
    /* Sheer is a fraction removed per cell, so a step across LIGHT_CELL of them
       removes correspondingly more. Scaled rather than compounded: the exact
       form is 1-(1-x)^LIGHT_CELL, and over the range foliage actually uses the
       two differ by less than a shade. */
    const int hs = (h * LIGHT_CELL) / n;
    *sheer = (u8)(hs > 255 ? 255 : hs);
}

/* --- skylight, from more than one direction --------------------------------

   The sky is not a lamp directly overhead, and treating it as one produced both
   of the artefacts this replaced. A single vertical ray per column means a cell
   is either in full daylight or in none, so:

     a floating slab cast a HARD-EDGED column of dark straight down. Measured in
     the ground under a 17-wide slab: 81 58 32 16 16 16 percent going down,
     against 95 85 75 65 54 43 beside it. Air was fine -- the propagation
     sweeps fill a shadow in open space from the sides, 99% under a one-cell
     slab -- but ground gets its light from the soak pass, which was gated on a
     yes/no "is this column open to the sky", so in a shadow it got nothing at
     all. An inky rectangle under anything floating.

     and there was no such thing as partial sky, so nothing could fade.

   Now several rays arrive at each cell from different angles and a cell's
   skylight is the SUM of the ones that got through. That is what the sky
   physically is -- a hemisphere, not a point -- and the useful consequence is
   that "how much sky can this cell see" becomes a number instead of a bit:

     under a one-cell slab, four of five rays still arrive
     under a wide slab the penumbra grades in from the edges
     down a narrow shaft only the vertical ray fits, so a shaft is dim
     down a wide pit the slanted rays reach too, so a pit is bright

   The last two are worth noticing: a well being darker than an open pit falls
   out of the geometry rather than being a rule anybody wrote.

   Weights sum to LIGHT_MAX so open sky is still exactly full daylight, and are
   biased toward the vertical the way a real sky's contribution is: straight up
   is a third of it. */
/* --- the day ---------------------------------------------------------------
   See light.h for why this lives here at all. The shape of the curve is the
   only part with any content in it:

     0.00 .. 0.42   day      full sun
     0.42 .. 0.50   dusk     falling to the night floor
     0.50 .. 0.92   night    the floor
     0.92 .. 1.00   dawn     rising back

   Dusk and dawn are 8% of the cycle each, which at twelve minutes is about
   fifty-five seconds -- long enough to notice it happening and to decide to
   head home, short enough that it is an event rather than a mood.

   NIGHT_FLOOR is 34 of 255, not 0. A moonless night that renders the surface as
   pure black sounds atmospheric and plays terribly: the world outside becomes
   indistinguishable from the void beyond the world's edge, you cannot see
   terrain to walk on, and the only workable response is to stand still for four
   minutes. At 34 the ground is legible in silhouette and a torch is still
   obviously worth carrying, which is the balance the whole feature needs. */
u32 g_worldTime = 0;
static const int NIGHT_FLOOR = 34;

void dayAdvance() { g_worldTime = (g_worldTime + 1) % DAY_LENGTH; }

int dayLight() {
    const float t = (float)g_worldTime / (float)DAY_LENGTH;
    if (t < 0.42f) return LIGHT_MAX;
    if (t < 0.50f) {
        const float k = (t - 0.42f) / 0.08f;              /* dusk: 1 -> 0 */
        return NIGHT_FLOOR + (int)((float)(LIGHT_MAX - NIGHT_FLOOR) * (1.0f - k));
    }
    if (t < 0.92f) return NIGHT_FLOOR;
    const float k = (t - 0.92f) / 0.08f;                  /* dawn: 0 -> 1 */
    return NIGHT_FLOOR + (int)((float)(LIGHT_MAX - NIGHT_FLOOR) * k);
}

/* Halfway down from full daylight. Deliberately not "at the night floor":
   things should start appearing during dusk, while you can still see them
   coming, rather than all at once the instant it is fully dark. */
bool isNight() { return dayLight() < (LIGHT_MAX + NIGHT_FLOOR) / 2; }

static const int SUN_RAYS = 5;
/* Lateral movement per row, in halves: 0, -1/2, +1/2, -1, +1. */
static const int RAY_HALF[SUN_RAYS] = { 0, -1, 1, -2, 2 };
static const int RAY_WEIGHT[SUN_RAYS] = { 85, 51, 51, 34, 34 };   /* = 255 */

/* Each ray is stored indexed by the column it ENTERED the rectangle at, not by
   where it currently is, so a row advance is a change of index rather than a
   memmove of the whole array -- 1.9 M byte moves a frame otherwise. A ray that
   entered at source column c is at rect column c + off(row), so reading rect
   column lx means reading source column lx - off.

   Hence the padding: after LIGHT_H rows a slope-1 ray has moved LIGHT_H columns,
   so source indices run from -LIGHT_H to LIGHT_W + LIGHT_H. */
static const int RAY_BIAS = LIGHT_H + 2;
static const int RAY_SPAN = LIGHT_W + 2 * RAY_BIAS;
/* u16 holding the weight shifted up by RAY_FRAC bits, not a bare u8.
   Fixed point, and it is forced rather than tidy. The weights are 85, 51 and
   34; attenuating a ray by a fraction of itself in a u8 truncates to a
   SUBTRACTION OF ONE per cell however small the fraction is, so the weakest
   rays died after 34 cells and a canopy stayed black no matter what the sheer
   value said. Swept from 1 to 16 it made no difference at all, which is the
   tell: the number being tuned was not the number doing the work.

   Eight fractional bits give a beam room to lose half a percent per cell for a
   hundred cells and still mean something. Costs 18 KB, once. */
/* How bright a cell is allowed to be purely because there is open sky BEHIND
   it, with no ray able to reach it. See the note where it is applied.

   64 of 255 -- a quarter. Enough to move about, read the terrain and see a
   creature coming; nowhere near enough to work by, so a roofed building still
   wants torches. Above about a third it stops reading as shelter at all. */
static const int SKY_AMBIENT = 64;

static const int RAY_FRAC = 8;
static u16 g_ray[SUN_RAYS][RAY_SPAN];

/* --- how far down daylight gets ---------------------------------------------
   Skylight fades with depth below the sky/underground zone boundary, and
   reaching zero there is what replaced a hard cutoff.

   What was there before was a gate: openToSky() refused any column whose chunk
   was labelled underground. Walking down a shaft, that meant full daylight --
   255, no falloff at all -- until the top of the light rectangle crossed the
   boundary, and then nothing. Measured at 100% at 275 cells down and 15% at
   300. Both halves of that are wrong: a shaft 275 cells deep should not be as
   bright as open ground, and nothing should ever change that abruptly.

   The fade is measured from the ZONE BOUNDARY, which is a fixed feature of the
   world, and not from the top of the light rectangle, which moves with the
   camera. That is the whole reason it can be done at all: a fade measured from
   anything camera-relative would make a cell's brightness depend on where you
   were standing when you looked at it.

   Because the fade is a subtraction rather than a scaling, dim light dies
   sooner than bright light -- so a shaft only the vertical ray fits down goes
   dark sooner than a pit the slanted rays reach the bottom of. Measured at 70
   cells down, a nine-wide shaft is at 56 and a hundred-wide pit at 180, and a
   nine-wide shaft runs out at 142. That ordering is right and nobody had to
   write it down; it is the geometry.

   It also makes SUN_PROBE's bound invisible rather than merely large: a column
   deeper than SUN_REACH below the boundary has no skylight whatever the probe
   would have said, so it is rejected before probing. */
static const int SUN_REACH  = 400;
static const int SUN_FADE_Q8 = (LIGHT_MAX * 256) / SUN_REACH;

/* How far up to look for a roof. Only columns that are open at the top of the
   rectangle AND within SUN_REACH of the boundary ever pay for this, and a shaft
   is open along its whole length, so exhausting the probe inside one correctly
   reports "open" -- which is why there is no cutoff at any depth. */
static const int SUN_PROBE = 112;

static bool openAbove(const World& w, int x, int yTop) {
    for (int y = yTop; y > yTop - SUN_PROBE; --y) {
        if (y < 0) return true;              /* out the top of the world */
        if (y >= SIM_H) continue;
        if (!lightOpen(w.cells[y * SIM_W + x].mat)) return false;
    }
    return true;
}

/* World y at which this column stops being sky. Per chunk column, because that
   is the granularity zones have. */
static i32 g_boundY[LIGHT_W];

static void findBoundaries(const World& w, int wx0, int lx0, int lx1) {
    int lastCx = -1;
    i32 lastY = 0;
    for (int lx = lx0; lx < lx1; ++lx) {
        const int cx = (wx0 + (lx << LIGHT_SHIFT)) >> CHUNK_SHIFT;
        if (cx != lastCx) {
            lastCx = cx;
            lastY = SIM_H;
            for (int cy = 0; cy < CHUNKS_Y; ++cy)
                /* "Not sky", not "is the shallow tier". Underground is several
                   zones now (one per cave layer), and testing for one of them
                   by name would put the sky boundary at the top of layer 1 --
                   fine until a column's first non-sky chunk is a deeper layer,
                   at which point daylight would be declared to reach all the
                   way down to it. */
                if (w.zone[cy * CHUNKS_X + cx] != ZONE_SKY) { lastY = cy << CHUNK_SHIFT; break; }
        }
        g_boundY[lx] = lastY;
    }
}

/* --- propagation -----------------------------------------------------------
   Two raster sweeps, forward and backward, each taking the best of the
   neighbours already finalised in that direction. This is a chamfer distance
   transform with light in place of distance, and it is the reason lighting
   costs a fixed few milliseconds instead of scaling with how many sources are
   on screen: a thousand lamps and one lamp are the same two passes.

   Diagonals are included at 1.5x the cost of an orthogonal step. Without them
   a source lights a DIAMOND -- the falloff is Manhattan distance, and a lamp in
   open air visibly comes to four points. 1.5 is the integer-friendly stand-in
   for sqrt(2); at 1.41 the shape is rounder still and needs a multiply per
   neighbour to say so, which is not worth it for a difference you have to
   measure to see.

   Two forward/backward pairs rather than one, and the reason is worth being
   precise about, because it is not "more is better".

   A sweep only carries light the way it is running. The forward pass moves it
   rightward and downward, the backward pass leftward and upward, so ONE pair
   handles any path that runs forward-ish and then backward-ish -- a straight
   corridor, a room, a single corner. What it cannot do is ALTERNATE, because
   the forward pass belonging to the second turn has already been and gone.

   Measured on a five-leg switchback corridor with a lamp at the mouth, the
   brightness reaching each leg:

       1 pair     255  183   32    0    0      leg 3 is a rounding error
       2 pairs    255  183  119   55    0      legs 3 and 4 are properly lit
       3 pairs    255  183  119   55    0      identical

   The second pair is the difference between a corridor that doubles back being
   lit and being black; the third changes literally nothing, because by then
   the light has run out on its own rather than on the algorithm. That is what
   makes 2 a measurement instead of a guess. It costs 0.8 ms of the total.

   These are the MAIN sweeps, over g_light. The solid soak has a pair of its own
   and takes one rather than two; see solidSoak for why a lump of rock has no
   switchback to light around. */
static const int LIGHT_PASSES = 2;

/* --- sweeping a REGION rather than the field --------------------------------
   `r` is the set of cells this sweep may WRITE. Everything it reads outside r
   is a boundary condition, and for a partial update those cells hold last
   frame's finished light -- which is exactly right, because a partial update is
   only ever run over a region far enough around the change that the light on
   its rim provably did not move. See lightUpdate.

   The clamps are the same ones the full-field version had hard-coded: the
   forward sweep cannot do row 0 or either edge column because it reads up and
   left, and the backward sweep cannot do the last row for the mirror reason. */
static void sweepForward(const LRect& r) {
    const int x0 = imax(r.x0, 1), x1 = imin(r.x1, LIGHT_W - 2);
    const int y0 = imax(r.y0, 1), y1 = imin(r.y1, LIGHT_H - 1);
    for (int ly = y0; ly <= y1; ++ly) {
        u8*       L  = g_light + ly * LIGHT_W;
        const u8* U  = L - LIGHT_W;
        const u8* A  = g_att + ly * LIGHT_W;
        for (int lx = x0; lx <= x1; ++lx) {
            const int a = A[lx];
            const int d = a + (a >> 1);
            int v = L[lx], t;
            t = (int)L[lx - 1] - a; if (t > v) v = t;
            t = (int)U[lx]     - a; if (t > v) v = t;
            t = (int)U[lx - 1] - d; if (t > v) v = t;
            t = (int)U[lx + 1] - d; if (t > v) v = t;
            L[lx] = (u8)v;
        }
    }
}

static void sweepBackward(const LRect& r) {
    const int x0 = imax(r.x0, 1), x1 = imin(r.x1, LIGHT_W - 2);
    const int y0 = imax(r.y0, 0), y1 = imin(r.y1, LIGHT_H - 2);
    for (int ly = y1; ly >= y0; --ly) {
        u8*       L = g_light + ly * LIGHT_W;
        const u8* D = L + LIGHT_W;
        const u8* A = g_att + ly * LIGHT_W;
        for (int lx = x1; lx >= x0; --lx) {
            const int a = A[lx];
            const int d = a + (a >> 1);
            int v = L[lx], t;
            t = (int)L[lx + 1] - a; if (t > v) v = t;
            t = (int)D[lx]     - a; if (t > v) v = t;
            t = (int)D[lx + 1] - d; if (t > v) v = t;
            t = (int)D[lx - 1] - d; if (t > v) v = t;
            L[lx] = (u8)v;
        }
    }
}

/* --- how far ANY light reads into solid material ----------------------------

   A thick pillar of stone was pure black in the middle, and at the shipped
   numbers it did not have to be very thick: measured across the middle of one,
   16 cells wide was already dead black four cells in, and even 8 cells wide had
   a centre of 43. Rock read as a silhouette rather than as a mass.

   The obvious fix is to make solids less opaque, and it does work -- at 13 a
   16-wide pillar has a centre of 91. It also puts 142 of 255 through an
   EIGHT-CELL WALL from a single lamp, which is the thing g_matOpacity's solid
   figure exists to prevent and which lit.cpp tests for by name. Those are not
   two effects that happen to conflict; they are one measurement. "Light reaches
   6 cells into rock" and "6 cells of rock is opaque" are the same sentence, so
   no value of that number gives both.

   sunSoak had already met this and answered it -- see the long note below. A
   pass that runs AFTER the sweeps and writes only to SOLID cells lights the
   material it is in and transmits from there to nothing, because nothing reads
   those values again. That is the shape reused here, with two differences:
   this one comes from every direction rather than downward, and it is seeded
   from whatever light actually reached each open cell rather than from the sun
   specifically.

   The consequence worth stating plainly, because it is what makes this safe:
   propagation in g_light is COMPLETELY UNCHANGED. Air on the far side of a wall
   gets exactly what it got before, so no room lights through a wall, no lamp
   reaches next door, and every existing guarantee holds at its existing number.
   What changes is only how deep the rock you can see is lit.

   The one artefact it does have: the far FACE of a wall is lit from the near
   side, so standing in a dark room you see the shared wall dimly rather than
   black. That is the honest consequence of light reading 40 cells into rock,
   the wall is genuinely lit rock, and it is a surface rather than a room -- the
   air beside it stays dark, which is the part that would actually have looked
   broken.

   Open cells are seeded and then held: the sweep writes solids only, so air
   acts as a fixed source and can never GAIN from the soak. Without that, light
   would pool in air at the soak's low attenuation and creep along corridors it
   has no business in. */
static const int SOLID_SOAK     = 40 / LIGHT_CELL;   /* SAMPLES light reads into */
static const int SOLID_SOAK_ATT = LIGHT_MAX / SOLID_SOAK;

/* One forward/backward pair, where the main sweeps use two. The second pair
   buys light that turns a corner and comes back -- a switchback corridor -- and
   there is no such path inside a lump of rock: the soak travels from a surface
   inward and has nowhere to double back from. Measured, a second pair changes
   no cell of any of the shapes tested.

   Seeding and merging are folded INTO the two sweeps rather than being loops of
   their own, which is worth the slight awkwardness: written as four separate
   walks over the field this pass measured 1.12 ms, and four walks over 489 k
   cells is mostly the walking. (That figure, and the others quoted in this
   file, are for a whole-field solve on the machine they were taken on; what a
   frame actually costs now depends on how much of the field it touches -- see
   lightUpdate.) A sweep visits every cell exactly once in an
   order that only ever reads already-finalised neighbours, so there is nowhere
   a separate pass can see anything these two cannot. */
/* `sr` is the region the soak is SOLVED over, `wr` the region it is allowed to
   write back into g_light. For a full field the two are the same thing. For a
   partial update they are not, and the gap between them is what keeps the pass
   honest: the soak's rim has to be seeded with a guess (air keeps its light,
   material starts at nothing), and a solid cell on the rim seeded at nothing is
   simply wrong. That error decays at SOLID_SOAK_ATT and is gone within
   SOLID_SOAK cells, so lightUpdate hands over an `sr` grown by SOLID_SOAK past
   `wr` and the wrong values never reach anything that gets written back. */
static void solidSoak(const LRect& sr, const LRect& wr) {
    /* Forward, and the seed. Air takes the light the main sweeps gave it and
       keeps it -- so a lamp lights the rock around it and a sunlit hillside
       lights its own stone, with no second notion of where light comes from.
       Material starts at nothing and collects from its neighbours.

       Note `v = 0` rather than `v = K[lx]` for material: g_soak holds LAST
       frame's values, and starting from them would make the soak accumulate
       over time instead of being recomputed -- the exact bug the whole
       recompute-every-frame design exists to avoid. */
    /* The rim first, and it MUST be first: the sweep starts one row in and reads
       the row above it, so seeding afterwards would feed the whole region one
       row of LAST frame's values. */
#define SOAK_SEED(lx, ly) do { const int _i = (ly) * LIGHT_W + (lx); \
        g_soak[_i] = g_solid[_i] ? 0 : g_light[_i]; } while (0)
    for (int lx = sr.x0; lx <= sr.x1; ++lx) {
        SOAK_SEED(lx, sr.y0);
        if (sr.y1 < LIGHT_H - 1) SOAK_SEED(lx, sr.y1);
    }
    for (int ly = sr.y0; ly <= sr.y1; ++ly) {
        SOAK_SEED(sr.x0, ly);
        SOAK_SEED(sr.x1, ly);
    }
#undef SOAK_SEED

    /* The region the two passes actually solve: inside the rim on every side a
       rim exists on. A side flush against the edge of the BUFFER has no rim --
       there is nothing outside it to hold fixed -- so the passes own those rows
       and columns themselves, which is what the full-field case has always
       done. The two passes then differ only in the one row each cannot reach:
       forward reads the row above, so it cannot start at row 0; backward reads
       the row below, so it cannot finish on the last one. */
    const int ix0 = imax(sr.x0 + (sr.x0 > 0), 1);
    const int ix1 = imin(sr.x1 - (sr.x1 < LIGHT_W - 1), LIGHT_W - 2);
    const int ry0 = sr.y0 + (sr.y0 > 0);
    const int ry1 = sr.y1 - (sr.y1 < LIGHT_H - 1);

    for (int ly = imax(ry0, 1); ly <= ry1; ++ly) {
        u8*       K = g_soak  + ly * LIGHT_W;
        const u8* U = K - LIGHT_W;
        const u8* L = g_light + ly * LIGHT_W;
        const u8* S = g_solid + ly * LIGHT_W;
        for (int lx = ix0; lx <= ix1; ++lx) {
            if (!S[lx]) { K[lx] = L[lx]; continue; }
            const int a = SOLID_SOAK_ATT, d = a + (a >> 1);
            int v = 0, t;
            t = (int)K[lx - 1] - a; if (t > v) v = t;
            t = (int)U[lx]     - a; if (t > v) v = t;
            t = (int)U[lx - 1] - d; if (t > v) v = t;
            t = (int)U[lx + 1] - d; if (t > v) v = t;
            K[lx] = (u8)v;
        }
    }
    /* Backward, and the merge. Writing back into g_light for MATERIAL ONLY is
       the whole safety property of this pass: an open cell is never written, so
       nothing the soak carried through a wall can light the space on the other
       side of it. */
    for (int ly = imin(ry1, LIGHT_H - 2); ly >= ry0; --ly) {
        u8*       K = g_soak  + ly * LIGHT_W;
        const u8* D = K + LIGHT_W;
        u8*       L = g_light + ly * LIGHT_W;
        const u8* S = g_solid + ly * LIGHT_W;
        const bool wrow = (ly >= wr.y0 && ly <= wr.y1);
        const int  wa = imax(wr.x0, ix0), wb = imin(wr.x1, ix1);
        for (int lx = ix1; lx >= ix0; --lx) {
            if (!S[lx]) continue;                  /* air holds its seed */
            const int a = SOLID_SOAK_ATT, d = a + (a >> 1);
            int v = K[lx], t;
            t = (int)K[lx + 1] - a; if (t > v) v = t;
            t = (int)D[lx]     - a; if (t > v) v = t;
            t = (int)D[lx + 1] - d; if (t > v) v = t;
            t = (int)D[lx - 1] - d; if (t > v) v = t;
            K[lx] = (u8)v;
            if (wrow && lx >= wa && lx <= wb && v > L[lx]) L[lx] = (u8)v;
        }
    }
}

/* --- how far daylight soaks into the ground --------------------------------

   Ground under open sky is lit SUN_SOAK cells down, and this is a pass of its
   own rather than a change to how opaque solids are. The reason is the one
   thing the last round of tuning ran into: g_matOpacity's solid figure governs
   two unrelated questions at once, and they want opposite answers.

       how deep does daylight soak into a hillside   -- want deep, it is what
                                                        gives ground any depth
       how much lamp light crosses a wall            -- want almost none, or
                                                        adjacent rooms bleed

   At 38 the second is right and the first is 6 cells, which on the grassy
   overworld means you see almost no soil before it goes flat -- the ground is a
   green line on a dark mass. Lowering it to reach 15 cells makes a six-cell
   wall pass 62% of a lamp next door, which is worse than the problem.

   Separating them works because THE SUN IS NOT A SOURCE IN THE FIELD. It is
   already a column property (see the ray model above), so it can have its own
   attenuation, and this pass runs AFTER the sweeps -- so the light it adds
   lights the rock it is in and does not propagate anywhere from there. That
   last part is what keeps it honest: soaking daylight 32 cells into a roof
   cannot brighten the room under it, because nothing reads these values again.

   It also stops at the first open cell below ground, which is what makes it
   ground soak rather than transmission: daylight does not come through a floor
   into the cave below it.

   The one visible consequence of not propagating: a sealed air pocket within
   SUN_SOAK of the surface has dim rock around it and a dark interior. It is a
   rare shape, the values involved are low, and the alternative is letting the
   soak into the sweeps, which is exactly the leak this pass exists to avoid. */
static const int SUN_SOAK     = 32 / LIGHT_CELL;     /* SAMPLES daylight reaches */
static const int SUN_SOAK_ATT = LIGHT_MAX / SUN_SOAK;

/* No state machine any more, and losing it fixed a bug that had nothing to do
   with the one it was written for.

   It used to be "above ground" -> "in the ground" -> "finished", where finished
   was terminal, so that daylight could not come through a floor into the cave
   under it. But the FIRST solid thing a column meets going down is not always
   the ground: with a slab floating overhead it is the slab. The pass soaked
   three cells of slab, came out into the air below it, latched "finished", and
   the actual ground in that column never got soaked at all. That is what the
   inky rectangle under a floating object really was -- not the missing
   penumbra, which was only half of it.

   Written as "while in open air, remember the skylight here; while in solid,
   decay and write", the terminal case disappears and so does the bug: coming
   out into air simply re-reads the skylight there, which below a slab is the
   slab's own shadow and below a roofed room is nothing. Daylight still cannot
   reach through a floor, because the air under the floor has no skylight to
   re-seed from -- the property survives without a rule enforcing it. */
static void sunSoak(int wy0, int lx0, int lx1) {
    static u8 soak[LIGHT_W];
    for (int lx = lx0; lx < lx1; ++lx) soak[lx] = 0;

    for (int ly = 0; ly < LIGHT_H; ++ly) {
        const int wy = wy0 + (ly << LIGHT_SHIFT);
        if (wy < 0) continue;
        if (wy >= SIM_H) break;

        u8*       L   = g_light + ly * LIGHT_W;
        const u8* SKY = g_sky   + ly * LIGHT_W;
        const u8* O   = g_solid + ly * LIGHT_W;

        for (int lx = lx0; lx < lx1; ++lx) {
            if (!O[lx]) {
                /* Seeded from SKYLIGHT ONLY, not from the light in the buffer.
                   Total light would let a lamp soak fifteen cells of rock,
                   which contradicts the five that walls are tuned to pass and
                   would read as a lamp leaking through them. g_sky is the ray
                   sum the gather pass already worked out, kept precisely so
                   this pass can be about the sun and nothing else. */
                soak[lx] = SKY[lx];
            } else if (soak[lx]) {
                soak[lx] = (u8)(soak[lx] > SUN_SOAK_ATT ? soak[lx] - SUN_SOAK_ATT : 0);
                if (L[lx] < soak[lx]) L[lx] = soak[lx];
            }
        }
    }
}

/* --- one lighting solve ------------------------------------------------------
   `wr` is the region of the buffer this is allowed to change. Pass LR_ALL and
   it is the original whole-field recompute. Pass something smaller and it is a
   patch: everything outside wr keeps the light it already had, and is used as
   the boundary the patched region propagates from.

   The gather is the one pass that does NOT simply shrink to wr, and the reason
   is the sun. Each of the five rays is carried down the buffer row by row, so
   what a ray is worth at row N depends on every row above it -- a strip in the
   middle of the field cannot be gathered without first knowing what the rays
   arriving at its top row have already been through. So when there is any
   skylight at all the gather still walks the whole field to carry the rays, and
   only its WRITES to g_light are held inside wr. Underground, where no column
   sees the sky, there are no rays to carry and the gather shrinks with
   everything else -- which is why depth is the cheaper case to patch.

   (wx0, wy0) is the world cell the buffer's (0,0) corresponds to. */
/* See the note in light.h. Position in WORLD cells; the solve converts. */
struct DynLight { int x, y; u8 level; };
static std::vector<DynLight> g_dyn;

void lightClearDynamic() { g_dyn.clear(); }

void lightAddDynamic(int wx, int wy, u8 level) {
    if (level == 0) return;
    const DynLight source = { wx, wy, level };
    g_dyn.push_back(source);
}

static void lightSolve(const World& w, int wx0, int wy0, LRect wr) {

    /* The horizontal span actually inside the world, in SAMPLES. */
    int lx0 = 0, lx1 = LIGHT_W;
    if (wx0 < 0)                              lx0 = (-wx0 + LIGHT_CELL - 1) >> LIGHT_SHIFT;
    if (wx0 + (lx1 << LIGHT_SHIFT) > SIM_W)   lx1 = (SIM_W - wx0) >> LIGHT_SHIFT;
    if (lx0 > LIGHT_W) lx0 = LIGHT_W;
    if (lx1 < lx0)     lx1 = lx0;

    findBoundaries(w, wx0, lx0, lx1);

    /* Seed every ray at the top row. A ray that entered through the SIDE of the
       rectangle has no top-row cell of its own, so those source slots take the
       nearest edge column's answer -- the rectangle's own edge is LIGHT_MARGIN cells
       outside the view, and replicating one column's sky state is a far smaller
       error than starting those rays dark, which would draw dim vertical bands
       down both sides of the screen. */
    for (int r = 0; r < SUN_RAYS; ++r) memset(g_ray[r], 0, sizeof(g_ray[r]));
    bool anySky = false;
    for (int lx = lx0; lx < lx1; ++lx) {
        /* Rejected before probing when the fade alone would zero it -- this is
           what replaced the zone gate, and unlike the gate it cannot introduce
           a step, because it only ever refuses columns that would have come out
           at zero anyway. */
        const int depth = wy0 - g_boundY[lx];
        if (depth > 0 && ((depth * SUN_FADE_Q8) >> 8) >= LIGHT_MAX) continue;
        if (!openAbove(w, wx0 + (lx << LIGHT_SHIFT), wy0)) continue;
        /* Night is applied HERE, at the source, rather than to the finished
           light value. Scaling the seed means everything downstream -- the
           per-ray attenuation, the sheer canopy multiply, the depth fade, the
           ground soak -- is already working in the dimmed units, so a torch at
           night is correctly brighter than its surroundings instead of being
           dimmed along with them. Scaling the output would have darkened lamps
           and lava at midnight too, which is not what night is. */
        const int sun = dayLight();
        for (int r = 0; r < SUN_RAYS; ++r)
            g_ray[r][lx + RAY_BIAS] = (u16)((RAY_WEIGHT[r] * sun / LIGHT_MAX) << RAY_FRAC);
        anySky = true;
    }
    if (anySky)
        for (int r = 0; r < SUN_RAYS; ++r) {
            for (int c = 0; c < lx0 + RAY_BIAS; ++c)        g_ray[r][c] = g_ray[r][lx0 + RAY_BIAS];
            for (int c = lx1 + RAY_BIAS; c < RAY_SPAN; ++c) g_ray[r][c] = g_ray[r][lx1 - 1 + RAY_BIAS];
        }

    /* One pass gathers emission and attenuation and carries the rays down at
       the same time. The rays genuinely want a column walk, but doing it that
       way is a strided read of the world LIGHT_H cells long per column; carrying
       them row by row gets the identical answer out of memory the gather is
       already touching. */
    /* --- what the sun does to the size of a patch ---------------------------
       A change does not only alter light within one source's reach of itself.
       If any column sees the sky, it also alters which RAYS get past it, and a
       ray is a line running to the bottom of the buffer -- so the region whose
       answer moves is not a ball around the change, it is a wedge hanging below
       it, spreading a column per row because that is the steepest ray's slope.

       Getting this wrong does not look like a rounding error. The gather
       recomputes the ray sums over the whole field either way; holding the
       WRITES inside a region that does not cover the wedge leaves the cells
       below it holding light from before the change, and nothing revisits them
       -- a hard-edged 45-degree wedge of stale shading, pinned to the world,
       that survives until something forces a recut. It showed up as exactly
       that: a wedge whose edge tracked the camera at the pan speed.

       So the region grows to the wedge here rather than at the call site: this
       is where anySky is known, and a caller that has to remember to widen its
       own request is a caller that will one day forget. When the wedge swallows
       the field the solve is simply a full one, which is the honest price of
       changing something high up in daylight. */
    if (anySky && !lrEmpty(wr) && (wr.y1 < LIGHT_H - 1 || wr.x0 > 0 || wr.x1 < LIGHT_W - 1)) {
        const int drop = LIGHT_H - 1 - wr.y0;   /* rows a ray can still fall */
        wr.y1 = LIGHT_H - 1;
        wr.x0 = imax(0, wr.x0 - drop);
        wr.x1 = imin(LIGHT_W - 1, wr.x1 + drop);
    }
    g_lightWorkPct = (int)(((long long)(wr.x1 - wr.x0 + 1) * (wr.y1 - wr.y0 + 1) * 100)
                           / ((long long)LIGHT_W * LIGHT_H));

    /* Rows the gather has to walk at all. With rays to carry that is every row,
       whatever wr says; without them, only the rows wr covers. */
    const int gy0 = anySky ? 0 : imax(0, wr.y0);
    const int gy1 = anySky ? LIGHT_H - 1 : imin(LIGHT_H - 1, wr.y1);

    for (int ly = gy0; ly <= gy1; ++ly) {
        const int wy = wy0 + (ly << LIGHT_SHIFT);
        u8* L = g_light + ly * LIGHT_W;
        u8* A = g_att   + ly * LIGHT_W;
        u8* S = g_sky   + ly * LIGHT_W;
        u8* O = g_solid + ly * LIGHT_W;

        /* The span of this row whose g_light the solve owns. Outside it the
           buffer holds a finished value from an earlier solve that is still
           correct, and overwriting it with bare emission would erase exactly
           the boundary a patch needs to propagate from. The other three arrays
           are pure functions of the cell and get rewritten either way -- doing
           so costs nothing and keeps them trivially consistent. */
        const bool wrow = (ly >= wr.y0 && ly <= wr.y1);
        const int  wa   = wrow ? imax(wr.x0, 0) : 0;
        const int  wb   = wrow ? imin(wr.x1, LIGHT_W - 1) : -1;

        /* Off the world counts as SOLID, not air. It is the one place the two
           choices differ visibly: as air it would seed the soak field with a
           border of darkness that then held, since air holds its seed, and draw
           a dark line down the edge of a world-edge view. */
        if (wy < 0 || wy >= SIM_H) {
            if (wb >= wa) memset(L + wa, 0, wb - wa + 1);
            memset(S, 0, LIGHT_W);
            memset(A, 255, LIGHT_W);        /* the void swallows light */
            memset(O, 1, LIGHT_W);
            continue;
        }
        if (lx0 > 0)       { memset(S, 0, lx0); memset(A, 255, lx0); memset(O, 1, lx0);
                             const int e = imin(lx0 - 1, wb);
                             if (e >= wa) memset(L + wa, 0, e - wa + 1); }
        if (lx1 < LIGHT_W) { memset(S + lx1, 0, LIGHT_W - lx1); memset(A + lx1, 255, LIGHT_W - lx1);
                             memset(O + lx1, 1, LIGHT_W - lx1);
                             const int b = imax(lx1, wa);
                             if (wb >= b) memset(L + b, 0, wb - b + 1); }

        /* No column of this rectangle can see the sky at all -- deep
           underground, which is most of the game. Skipping the rays here is
           what keeps depth CHEAPER than the surface rather than dearer: with
           nothing seeded, every solid cell was still writing five zeros over
           five already-zero slots, 1.9 M scattered writes a frame to achieve
           nothing. Measured 4.80 ms against the surface's 3.69 before this. */
        if (!anySky) {
            /* No rays, so nothing carries between cells and the loop can be cut
               down to the region being solved -- the whole reason a patch
               underground is so much cheaper than one at the surface. */
            for (int lx = imax(lx0, wa); lx <= imin(lx1 - 1, wb); ++lx) {
                u8 emit, att, solid, sheer;
                sampleBlock(w, wx0 + (lx << LIGHT_SHIFT), wy, &emit, &att, &solid, &sheer);
                L[lx] = emit;
                A[lx] = att;
                S[lx] = 0;
                O[lx] = solid;
            }
            continue;
        }

        /* Where each ray has got to by this row. Halves, so a slope-1/2 ray
           moves on alternate rows without needing an accumulator. */
        int off[SUN_RAYS];
        for (int r = 0; r < SUN_RAYS; ++r) off[r] = (RAY_HALF[r] * ly) / 2;

        for (int lx = lx0; lx < lx1; ++lx) {
            u8 emit, att, solid, blockSheer;
            sampleBlock(w, wx0 + (lx << LIGHT_SHIFT), wy, &emit, &att, &solid, &blockSheer);
            u8 lit = emit;
            A[lx] = att;
            u8 sky = 0;
            O[lx] = solid;

            if (solid) {
                /* Solid: every ray passing through this cell ends here, and
                   stays ended, because a ray is a straight line from the sky.

                   Tested before writing, not written unconditionally. A source
                   slot can only be killed once, so all but SUN_RAYS*LIGHT_W of
                   these writes are storing a zero over a zero -- and a write
                   dirties a cache line where a read does not, which for cells
                   the rays reach diagonally is most of the cost. */
                for (int r = 0; r < SUN_RAYS; ++r) {
                    u16& v = g_ray[r][lx - off[r] + RAY_BIAS];
                    if (v) v = 0;
                }
            } else {
                /* Sheer material -- foliage -- dims every ray passing through
                   it instead of stopping it. Applied BEFORE the sum, so a cell
                   is lit by the beam as it arrives and the next cell down gets
                   what is left: the outside of a canopy is bright and the
                   depths are dim, which is the whole point of the table. See
                   g_matSheer in materials.h.

                   Multiplicative rather than a subtraction, because a fixed
                   subtraction would kill the two weakest rays (weight 34) five
                   times sooner than the vertical one and a canopy would go
                   flat-shadowed rather than soft. */
                const u8 sheer = blockSheer;
                if (sheer) {
                    const int keep = 256 - (int)sheer;
                    for (int r = 0; r < SUN_RAYS; ++r) {
                        u16& v = g_ray[r][lx - off[r] + RAY_BIAS];
                        if (v) v = (u16)(((u32)v * (u32)keep) >> 8);
                    }
                }
                int sum = 0;
                for (int r = 0; r < SUN_RAYS; ++r)
                    sum += g_ray[r][lx - off[r] + RAY_BIAS] >> RAY_FRAC;
                if (sum) {
                    const int depth = wy - g_boundY[lx];
                    if (depth > 0) sum -= (depth * SUN_FADE_Q8) >> 8;
                    if (sum > 0) sky = (u8)(sum > LIGHT_MAX ? LIGHT_MAX : sum);
                    if (sky > lit) lit = sky;
                }

                /* --- the sky you cannot see is still the sky ---------------
                   Rays are straight lines, so ANY roof takes a cell from full
                   daylight to nothing. Standing under a plank two cells above
                   open ground was as dark as a cave three thousand cells down,
                   which is wrong in a way you feel immediately: putting a lid
                   on a hut should dim it, not blind you.

                   The signal is the BACKGROUND. Worldgen writes a backdrop
                   behind every cell it fills, so a cell whose background is
                   still empty is one where the world never had any ground --
                   open air, whatever has since been built in front of it. That
                   is exactly "outdoors, under cover", and it is distinguishable
                   from a cave (backed by rock) and from a room whose walls you
                   put up yourself (backed by placed background) without any new
                   state at all.

                   Scaled by the same daylight the rays carry, so a covered
                   porch still goes dark at night, and capped well below full
                   sun -- it is bounced light, not a second sun. */
                const int wx = wx0 + (lx << LIGHT_SHIFT);
                if (sky < SKY_AMBIENT && wx >= 0 && wx < SIM_W
                    && wy >= 0 && wy < SIM_H
                    && !w.bgPlaced(wx, wy) && w.bgAt(wx, wy) == MAT_EMPTY) {
                    const int amb = (SKY_AMBIENT * dayLight()) / LIGHT_MAX;
                    if (amb > (int)sky) sky = (u8)amb;
                    if (sky > lit) lit = sky;
                }
            }
            S[lx] = sky;
            if (lx >= wa && lx <= wb) L[lx] = lit;
        }
    }

    /* Sources that are not in the grid -- see lightAddDynamic. Folded in AFTER
       the gather, so a drone lights the block it is standing in, and BEFORE the
       sweeps, so its light spreads by exactly the same machinery a torch's
       does. Nothing downstream can tell the difference, which is the point. */
    for (size_t i = 0; i < g_dyn.size(); ++i) {
        const int sx = (g_dyn[i].x - wx0) >> LIGHT_SHIFT;
        const int sy = (g_dyn[i].y - wy0) >> LIGHT_SHIFT;
        if (sx < wr.x0 || sx > wr.x1 || sy < wr.y0 || sy > wr.y1) continue;
        u8& v = g_light[sy * LIGHT_W + sx];
        if (g_dyn[i].level > v) v = g_dyn[i].level;
    }

    for (int p = 0; p < LIGHT_PASSES; ++p) {
        sweepForward(wr);
        sweepBackward(wr);
    }

    /* Both soaks run AFTER the sweeps, and that ordering is the whole mechanism
       -- see the notes on solidSoak() and sunSoak(). Between themselves the
       order does not matter: each writes only to material, each takes the
       brighter of what it found and what is already there, and neither reads
       what the other wrote. */
    LRect sr = { imax(0, wr.x0 - SOLID_SOAK), imax(0, wr.y0 - SOLID_SOAK),
                 imin(LIGHT_W - 1, wr.x1 + SOLID_SOAK),
                 imin(LIGHT_H - 1, wr.y1 + SOLID_SOAK) };
    solidSoak(sr, wr);
    /* sunSoak walks each column from the top of the buffer down, because that
       is what "how far has daylight got into the ground by here" means, so it
       cannot be cut down to a band in the middle. Run whole, it stays correct
       during a patch for free: it only ever raises a solid cell toward the
       skylight above it, and outside wr both that skylight and the cell's
       current value are the ones it produced last time, so the max is a no-op
       there. Idempotence is what makes running it wide harmless. */
    if (anySky) sunSoak(wy0, lx0, lx1);
}

/* ==========================================================================
   One frame of lighting
   ========================================================================== */

/* The field is recomputed every frame, from nothing.

   It did not used to be. There was a layer here that reused the field when the
   world had not moved, patched the part that had, slid the buffer under a
   moving camera, and cut a new one when none of that would do. It worked and it
   was measured -- but it existed to avoid a 21 ms solve, and solving at sample
   resolution costs about a tenth of that. Once the expensive thing is cheap,
   the machinery for avoiding it is just machinery.

   Deleting it bought back more than speed. Every patch rested on an argument
   about how far a change could possibly have reached, and an argument like that
   is a standing obligation: it has to be re-checked whenever attenuation, the
   sun, or a material's opacity changes, and when it is wrong the symptom is
   stale shading welded to the world rather than anything that announces itself.
   Recomputing has no such obligation. Whatever the world looks like this frame
   is what you see, which is the property the original design was built around
   and the reason it is worth having back. */
int  g_lightWork = LIGHT_RECUT;
int  g_lightWorkPct = 100;

/* Sample (0,0) is pinned to a world position that is a multiple of LIGHT_CELL,
   so the sample grid never slides under the terrain as the camera moves. An
   arithmetic shift rather than a mask, so it still rounds downward left of the
   world origin. */
static inline int lightAlign(int v) { return (v >> LIGHT_SHIFT) << LIGHT_SHIFT; }

void lightInvalidate() { }

void lightCompute(const World& w, int camX, int camY) {
    g_lightViewX = camX;
    g_lightViewY = camY;
    g_lightAnchorX = lightAlign(camX - LIGHT_MARGIN * LIGHT_CELL);
    g_lightAnchorY = lightAlign(camY - LIGHT_MARGIN * LIGHT_CELL);
    g_lightOfsX = (camX - g_lightAnchorX) >> LIGHT_SHIFT;
    g_lightOfsY = (camY - g_lightAnchorY) >> LIGHT_SHIFT;
    lightSolve(w, g_lightAnchorX, g_lightAnchorY, LR_ALL);
}

void lightUpdate(const World& w, int camX, int camY) {
    lightCompute(w, camX, camY);
}
