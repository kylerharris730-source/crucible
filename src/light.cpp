#include "light.h"
#include <string.h>

u8   g_light[LIGHT_W * LIGHT_H];
bool g_lightOn = true;

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

/* Anything that is not solid or liquid lets daylight straight through. Gases
   attenuate light (see g_matOpacity) but they do not cast a shadow, which is
   the difference between smoke dimming a room and smoke turning it to night. */
static inline bool lightOpen(u8 m) {
    return m == MAT_EMPTY || MATS[m].kind == KIND_GAS;
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
static u8 g_ray[SUN_RAYS][RAY_SPAN];

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
                if (w.zone[cy * CHUNKS_X + cx] == ZONE_UNDER) { lastY = cy << CHUNK_SHIFT; break; }
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
   makes 2 a measurement instead of a guess. It costs 0.8 ms of the 2.6 ms
   total. */
static const int LIGHT_PASSES = 2;

static void sweepForward() {
    for (int ly = 1; ly < LIGHT_H; ++ly) {
        u8*       L  = g_light + ly * LIGHT_W;
        const u8* U  = L - LIGHT_W;
        const u8* A  = g_att + ly * LIGHT_W;
        for (int lx = 1; lx < LIGHT_W - 1; ++lx) {
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

static void sweepBackward() {
    for (int ly = LIGHT_H - 2; ly >= 0; --ly) {
        u8*       L = g_light + ly * LIGHT_W;
        const u8* D = L + LIGHT_W;
        const u8* A = g_att + ly * LIGHT_W;
        for (int lx = LIGHT_W - 2; lx >= 1; --lx) {
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

void lightCompute(const World& w, int camX, int camY) {
    const int wx0 = camX - LIGHT_MARGIN;
    const int wy0 = camY - LIGHT_MARGIN;

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
       nearest edge column's answer -- the rectangle's own edge is 85 cells
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
        for (int r = 0; r < SUN_RAYS; ++r) g_ray[r][lx + RAY_BIAS] = (u8)RAY_WEIGHT[r];
        anySky = true;
    }
    if (anySky)
        for (int r = 0; r < SUN_RAYS; ++r) {
            for (int c = 0; c < lx0 + RAY_BIAS; ++c)        g_ray[r][c] = g_ray[r][lx0 + RAY_BIAS];
            for (int c = lx1 + RAY_BIAS; c < RAY_SPAN; ++c) g_ray[r][c] = g_ray[r][lx1 - 1 + RAY_BIAS];
        }

    /* One pass gathers emission and attenuation and carries the rays down at
       the same time. The rays genuinely want a column walk, but doing it that
       way is a strided read of the world 554 cells long per column; carrying
       them row by row gets the identical answer out of memory the gather is
       already touching. */
    for (int ly = 0; ly < LIGHT_H; ++ly) {
        const int wy = wy0 + ly;
        u8* L = g_light + ly * LIGHT_W;
        u8* A = g_att   + ly * LIGHT_W;
        u8* S = g_sky   + ly * LIGHT_W;

        if (wy < 0 || wy >= SIM_H) {
            memset(L, 0, LIGHT_W);
            memset(S, 0, LIGHT_W);
            memset(A, 255, LIGHT_W);        /* the void swallows light */
            continue;
        }
        if (lx0 > 0)       { memset(L, 0, lx0); memset(S, 0, lx0); memset(A, 255, lx0); }
        if (lx1 < LIGHT_W) { memset(L + lx1, 0, LIGHT_W - lx1); memset(S + lx1, 0, LIGHT_W - lx1);
                             memset(A + lx1, 255, LIGHT_W - lx1); }

        const Cell* row = w.cells + wy * SIM_W + wx0;

        /* No column of this rectangle can see the sky at all -- deep
           underground, which is most of the game. Skipping the rays here is
           what keeps depth CHEAPER than the surface rather than dearer: with
           nothing seeded, every solid cell was still writing five zeros over
           five already-zero slots, 1.9 M scattered writes a frame to achieve
           nothing. Measured 4.80 ms against the surface's 3.69 before this. */
        if (!anySky) {
            for (int lx = lx0; lx < lx1; ++lx) {
                const u8 m = row[lx].mat;
                L[lx] = g_matLight[m];
                A[lx] = g_matOpacity[m];
                S[lx] = 0;
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

            if (!lightOpen(m)) {
                /* Solid: every ray passing through this cell ends here, and
                   stays ended, because a ray is a straight line from the sky.

                   Tested before writing, not written unconditionally. A source
                   slot can only be killed once, so all but SUN_RAYS*LIGHT_W of
                   these writes are storing a zero over a zero -- and a write
                   dirties a cache line where a read does not, which for cells
                   the rays reach diagonally is most of the cost. */
                for (int r = 0; r < SUN_RAYS; ++r) {
                    u8& v = g_ray[r][lx - off[r] + RAY_BIAS];
                    if (v) v = 0;
                }
            } else {
                int sum = 0;
                for (int r = 0; r < SUN_RAYS; ++r) sum += g_ray[r][lx - off[r] + RAY_BIAS];
                if (sum) {
                    const int depth = wy - g_boundY[lx];
                    if (depth > 0) sum -= (depth * SUN_FADE_Q8) >> 8;
                    if (sum > 0) sky = (u8)(sum > LIGHT_MAX ? LIGHT_MAX : sum);
                    if (sky > lit) lit = sky;
                }
            }
            S[lx] = sky;
            L[lx] = lit;
        }
    }

    for (int p = 0; p < LIGHT_PASSES; ++p) {
        sweepForward();
        sweepBackward();
    }

    /* Last, and that ordering is the whole mechanism -- see sunSoak(). */
    if (anySky) sunSoak(w, wx0, wy0, lx0, lx1);
}
