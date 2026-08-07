#include "light.h"
#include <string.h>

u8   g_light[LIGHT_W * LIGHT_H];
bool g_lightOn = true;
int  g_lightOfsX = LIGHT_MARGIN;
int  g_lightOfsY = LIGHT_MARGIN;

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
        const int cx = (wx0 + lx) >> CHUNK_SHIFT;
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
   black. That is the honest consequence of light reading 20 cells into rock,
   the wall is genuinely lit rock, and it is a surface rather than a room -- the
   air beside it stays dark, which is the part that would actually have looked
   broken.

   Open cells are seeded and then held: the sweep writes solids only, so air
   acts as a fixed source and can never GAIN from the soak. Without that, light
   would pool in air at the soak's low attenuation and creep along corridors it
   has no business in. */
static const int SOLID_SOAK     = 20;    /* cells of material light reads into */
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
   last part is what keeps it honest: soaking daylight 15 cells into a roof
   cannot brighten the room under it, because nothing reads these values again.

   It also stops at the first open cell below ground, which is what makes it
   ground soak rather than transmission: daylight does not come through a floor
   into the cave below it.

   The one visible consequence of not propagating: a sealed air pocket within
   SUN_SOAK of the surface has dim rock around it and a dark interior. It is a
   rare shape, the values involved are low, and the alternative is letting the
   soak into the sweeps, which is exactly the leak this pass exists to avoid. */
static const int SUN_SOAK     = 15;    /* cells of ground daylight reaches */
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
static void sunSoak(const World& w, int wx0, int wy0, int lx0, int lx1) {
    static u8 soak[LIGHT_W];
    for (int lx = lx0; lx < lx1; ++lx) soak[lx] = 0;

    for (int ly = 0; ly < LIGHT_H; ++ly) {
        const int wy = wy0 + ly;
        if (wy < 0) continue;
        if (wy >= SIM_H) break;

        const Cell* row = w.cells + wy * SIM_W + wx0;
        u8*       L   = g_light + ly * LIGHT_W;
        const u8* SKY = g_sky   + ly * LIGHT_W;

        for (int lx = lx0; lx < lx1; ++lx) {
            if (lightOpen(row[lx].mat)) {
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
static void lightSolve(const World& w, int wx0, int wy0, LRect wr) {

    /* The horizontal span actually inside the world, so the gather loop below
       carries no bounds test -- the same trick renderView uses. */
    int lx0 = 0, lx1 = LIGHT_W;
    if (wx0 < 0)               lx0 = -wx0;
    if (wx0 + lx1 > SIM_W)     lx1 = SIM_W - wx0;
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
        if (!openAbove(w, wx0 + lx, wy0)) continue;
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
        const int wy = wy0 + ly;
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

        const Cell* row = w.cells + wy * SIM_W + wx0;

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
                const u8 m = row[lx].mat;
                L[lx] = g_matLight[m];
                A[lx] = g_matOpacity[m];
                S[lx] = 0;
                O[lx] = (u8)!lightOpen(m);
            }
            continue;
        }

        /* Where each ray has got to by this row. Halves, so a slope-1/2 ray
           moves on alternate rows without needing an accumulator. */
        int off[SUN_RAYS];
        for (int r = 0; r < SUN_RAYS; ++r) off[r] = (RAY_HALF[r] * ly) / 2;

        for (int lx = lx0; lx < lx1; ++lx) {
            const u8 m = row[lx].mat;
            u8 lit = g_matLight[m];
            A[lx] = g_matOpacity[m];
            u8 sky = 0;
            O[lx] = (u8)!lightOpen(m);

            if (!lightOpen(m)) {
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
                const u8 sheer = g_matSheer[m];
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
                const int wx = wx0 + lx;
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
    if (anySky) sunSoak(w, wx0, wy0, lx0, lx1);
}

/* ==========================================================================
   Reusing a field
   ========================================================================== */

/* --- what the field remembers between frames ------------------------------
   The buffer's own position in the world, so a field can outlive the frame it
   was computed on (see g_lightOfsX in light.h), plus enough to know when it has
   stopped being true. */
int  g_lightWork = LIGHT_RECUT;
int  g_lightWorkPct = 100;

static bool g_haveField = false;
static int  g_anchorX = 0, g_anchorY = 0;   /* world cell of buffer (0,0) */
static int  g_dayAt   = -1;                 /* dayLight() when it was solved */

/* World cells known to have changed since the field was solved, accumulated
   across every frame the field is reused. Inclusive; empty when x1 < x0. */
static int  g_dirtyX0 = 0, g_dirtyY0 = 0, g_dirtyX1 = -1, g_dirtyY1 = -1;
static int  g_stale = 0;                    /* frames since the last solve */

/* How many frames a field may be reused while the world under it is moving.

   Not a quality knob so much as an admission about what lighting is for. A
   shadow arriving a frame late is not detectable at 60 Hz -- the thing casting
   it has moved one cell -- and the pass costs more than the whole rest of the
   frame put together, so paying it every frame buys nothing anybody can see.
   Two was picked by halving the cost and looking for the seam; there is none.
   Four begins to show on fast-moving fire, where the lit region visibly steps.

   A field with nothing dirty under it is not on this clock at all: it is reused
   until something actually changes, which is most of the time in a built room
   with the machines switched off. */
static const int LIGHT_PERIOD = 2;

/* How far the camera may wander from the anchor before the field is recut.

   The margin is exactly one source's reach (see LIGHT_MARGIN), so a field whose
   camera has drifted d cells has only LIGHT_MARGIN - d of margin left on the
   trailing edge, and a source out in that shortfall is missing from the edge
   column. At 8 the worst a missing source can be worth is 8 * air attenuation =
   16 of 255, in the outermost column of the view only, and it is gone the next
   time the field is cut. Below about 4 the recuts cost more than they save;
   above about 16 you can see a lamp fade in at the screen edge. */
static const int LIGHT_DRIFT = 8;

/* Patching stops being a bargain once the patch approaches the size of the
   field: the region is solved with a rim around it and the soaks reach further
   still, so a patch over most of the buffer does more work than simply cutting
   a new one. Measured, the crossover is around three quarters. */
static const int LIGHT_PATCH_MAX_PCT = 70;

void lightInvalidate() { g_haveField = false; }

void lightCompute(const World& w, int camX, int camY) {
    g_anchorX = camX - LIGHT_MARGIN;
    g_anchorY = camY - LIGHT_MARGIN;
    g_lightOfsX = LIGHT_MARGIN;
    g_lightOfsY = LIGHT_MARGIN;
    lightSolve(w, g_anchorX, g_anchorY, LR_ALL);
    g_lightWork = LIGHT_RECUT;
    g_lightWorkPct = 100;
    g_haveField = true;
    g_dayAt = dayLight();
    g_dirtyX1 = -1; g_dirtyY1 = -1;
    g_dirtyX0 = 0;  g_dirtyY0 = 0;
    g_stale = 0;
}

/* Fold everything the simulation has disturbed inside the field into the
   running dirty box.

   The signal is the simulation's OWN chunk rectangles, not a second set of
   marks maintained beside them, and that choice is the safety of the whole
   scheme. Every material change in this engine is followed by a dirtyPoint --
   it has to be, or the cell would not be looked at again and the simulation
   itself would break -- so reading those rects cannot miss a change. A private
   list of "cells that changed appearance" would be a second invariant to keep,
   maintained at a dozen call sites, and the failure mode of forgetting one is a
   shadow that never lifts: exactly the bug this file's header warns about, and
   one that would survive to release because it needs a specific material doing
   a specific thing to show up at all.

   The price is that it is conservative. A chunk stays dirty while heat moves
   through it even though heat changes no cell's appearance, so a hot rock keeps
   its neighbourhood on the recompute clock. That costs frames; the alternative
   costs correctness. */
/* Fold a world rectangle into the running dirty box, clipped to the field. */
static void lightMarkDirty(int x0, int y0, int x1, int y1) {
    const int fx1 = g_anchorX + LIGHT_W - 1, fy1 = g_anchorY + LIGHT_H - 1;
    if (x0 < g_anchorX) x0 = g_anchorX;
    if (y0 < g_anchorY) y0 = g_anchorY;
    if (x1 > fx1) x1 = fx1;
    if (y1 > fy1) y1 = fy1;
    if (x1 < x0 || y1 < y0) return;
    if (g_dirtyX1 < g_dirtyX0) { g_dirtyX0 = x0; g_dirtyY0 = y0; g_dirtyX1 = x1; g_dirtyY1 = y1; }
    else {
        if (x0 < g_dirtyX0) g_dirtyX0 = x0;
        if (y0 < g_dirtyY0) g_dirtyY0 = y0;
        if (x1 > g_dirtyX1) g_dirtyX1 = x1;
        if (y1 > g_dirtyY1) g_dirtyY1 = y1;
    }
}

static void lightAccumulateDirty(const World& w) {
    const int fx1 = g_anchorX + LIGHT_W - 1, fy1 = g_anchorY + LIGHT_H - 1;
    const int cx0 = imax(0, g_anchorX >> CHUNK_SHIFT), cx1 = imin(CHUNKS_X - 1, fx1 >> CHUNK_SHIFT);
    const int cy0 = imax(0, g_anchorY >> CHUNK_SHIFT), cy1 = imin(CHUNKS_Y - 1, fy1 >> CHUNK_SHIFT);

    for (int cy = cy0; cy <= cy1; ++cy)
        for (int cx = cx0; cx <= cx1; ++cx) {
            const Chunk& c = w.next[cy * CHUNKS_X + cx];
            if (c.minX > c.maxX) continue;
            lightMarkDirty(c.minX, c.minY, c.maxX, c.maxY);
        }
}

/* --- moving the field without rebuilding it --------------------------------
   The camera wanders out of the drift tolerance several times a second while
   walking, and re-cutting the field each time is a full solve -- which showed
   up exactly as you would expect: a mean frame of 7.6 ms with 29.6 ms spikes
   through it, which at 60 Hz is a dropped frame every few steps rather than a
   fast game.

   But almost all of that field is still true. Sliding the buffer to the new
   anchor keeps the overlap -- which for a step or two of camera movement is
   better than 99% of it -- and leaves only a thin strip along the leading edge
   that has never been solved. That strip goes into the dirty box like any other
   change, and the ordinary patch below deals with it.

   Four arrays move: the light itself and the three the gather fills in beside
   it. g_soak does not, because every solve reseeds it inside the region it is
   about to use and never reads a value from outside. */
static void lightScroll(int nx, int ny) {
    const int dx = nx - g_anchorX, dy = ny - g_anchorY;
    if (dx == 0 && dy == 0) return;
    const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax >= LIGHT_W || ay >= LIGHT_H) { g_haveField = false; return; }

    const int keepW = LIGHT_W - ax, keepH = LIGHT_H - ay;
    const int dstX = imax(0, -dx), srcX = imax(0, dx);
    const int dstY = imax(0, -dy), srcY = imax(0, dy);

    u8* const planes[4] = { g_light, g_att, g_sky, g_solid };
    for (int p = 0; p < 4; ++p) {
        u8* A = planes[p];
        /* Rows are copied away from the overlap, so a move that shifts within
           the same buffer never reads a row it has already overwritten. */
        if (dstY <= srcY)
            for (int r = 0; r < keepH; ++r)
                memmove(A + (dstY + r) * LIGHT_W + dstX, A + (srcY + r) * LIGHT_W + srcX, keepW);
        else
            for (int r = keepH - 1; r >= 0; --r)
                memmove(A + (dstY + r) * LIGHT_W + dstX, A + (srcY + r) * LIGHT_W + srcX, keepW);
    }

    g_anchorX = nx; g_anchorY = ny;

    /* The strips that slid in hold whatever the arrays happened to contain, so
       they are dirty in the strongest sense: not stale, never solved at all. */
    const int fx1 = nx + LIGHT_W - 1, fy1 = ny + LIGHT_H - 1;
    if (dx > 0)      lightMarkDirty(fx1 - dx + 1, ny, fx1, fy1);
    else if (dx < 0) lightMarkDirty(nx, ny, nx - dx - 1, fy1);
    if (dy > 0)      lightMarkDirty(nx, fy1 - dy + 1, fx1, fy1);
    else if (dy < 0) lightMarkDirty(nx, ny, fx1, ny - dy - 1);
}

/* --- one frame of lighting --------------------------------------------------
   Three outcomes, cheapest first.

   REUSE. Nothing under the field has changed, or it has but the field is not
   yet due. The buffer is left alone and only the view's offset into it moves.

   PATCH. Something changed, in a region small enough to be worth isolating.
   Light reaches at most LIGHT_MARGIN cells (that is what the margin IS), so a
   change confined to a box can only have altered the field within that box
   grown by LIGHT_MARGIN -- everything beyond is provably the same value it
   already holds. Solving that grown box with its rim pinned to what is already
   there gives the same answer a full solve would, for a fraction of the area.

   RECUT. The camera has moved far enough that the buffer needs repositioning,
   the sun has moved so every column changed at once, or the dirty box has grown
   big enough that patching it is no longer a saving. */
void lightUpdate(const World& w, int camX, int camY) {
    if (!g_haveField) { lightCompute(w, camX, camY); return; }

    /* The sun moves every column at once, so there is nothing local about it and
       nothing to patch. It only ticks a step every couple of seconds. */
    if (dayLight() != g_dayAt) { lightCompute(w, camX, camY); return; }

    {   /* Drifted far enough that the margin on the trailing edge has worn
           thin: slide the field back under the camera, keeping the overlap. */
        const int driftX = camX - g_anchorX - LIGHT_MARGIN;
        const int driftY = camY - g_anchorY - LIGHT_MARGIN;
        if (driftX < -LIGHT_DRIFT || driftX > LIGHT_DRIFT ||
            driftY < -LIGHT_DRIFT || driftY > LIGHT_DRIFT) {
            lightScroll(camX - LIGHT_MARGIN, camY - LIGHT_MARGIN);
            if (!g_haveField) { lightCompute(w, camX, camY); return; }
            /* The strip that just slid in has never been solved, so it is not
               merely stale and must not wait for the reuse clock. Left to wait,
               a second scroll can arrive first -- which is not just a longer lag
               but a wrong answer that sticks, because by then the rim the patch
               would have propagated from has itself moved. */
            g_stale = LIGHT_PERIOD;
        }
    }

    /* The buffer stays where it is; the view slides within it. Assume reuse --
       the two paths below that do work say so themselves. */
    const int ofsX = camX - g_anchorX, ofsY = camY - g_anchorY;
    g_lightOfsX = ofsX;
    g_lightOfsY = ofsY;
    g_lightWork = LIGHT_REUSED;
    g_lightWorkPct = 0;

    lightAccumulateDirty(w);
    ++g_stale;
    if (g_dirtyX1 < g_dirtyX0) return;          /* nothing has moved: reuse */
    if (g_stale < LIGHT_PERIOD) return;         /* due, but not yet */

    /* The dirty box in buffer coordinates, grown by one source's reach. */
    LRect wr = { g_dirtyX0 - g_anchorX - LIGHT_MARGIN, g_dirtyY0 - g_anchorY - LIGHT_MARGIN,
                 g_dirtyX1 - g_anchorX + LIGHT_MARGIN, g_dirtyY1 - g_anchorY + LIGHT_MARGIN };
    if (wr.x0 < 0) wr.x0 = 0;
    if (wr.y0 < 0) wr.y0 = 0;
    if (wr.x1 > LIGHT_W - 1) wr.x1 = LIGHT_W - 1;
    if (wr.y1 > LIGHT_H - 1) wr.y1 = LIGHT_H - 1;
    if (lrEmpty(wr)) { g_stale = 0; g_dirtyX1 = -1; return; }

    const long long area = (long long)(wr.x1 - wr.x0 + 1) * (wr.y1 - wr.y0 + 1);
    /* Recut rather than patch. lightCompute re-anchors on the camera and sets
       the offset itself, so nothing here may write g_lightOfs* afterwards. */
    if (area * 100 >= (long long)LIGHT_W * LIGHT_H * LIGHT_PATCH_MAX_PCT) {
        lightCompute(w, camX, camY);
        return;
    }

    lightSolve(w, g_anchorX, g_anchorY, wr);
    g_lightWork = LIGHT_PATCHED;
    g_lightWorkPct = (int)(area * 100 / ((long long)LIGHT_W * LIGHT_H));
    g_stale = 0;
    g_dirtyX1 = -1; g_dirtyY1 = -1;
    g_dirtyX0 = 0;  g_dirtyY0 = 0;
}
