#include "air.h"
#include "materials.h"
#include <string.h>

i16 g_airP[AIR_W * AIR_H];
int g_airPush = 3;

/* Scratch for the relaxation passes. Static rather than on the stack: 4.7 MB is
   not a thing to put on a thread's stack, and it is written before it is read
   on every pass so it needs no clearing. */
static i16 g_airScratch[AIR_W * AIR_H];

/* --- the numbers ------------------------------------------------------------
   AIR_GAS_UNIT is what one gas cell contributes. Sixteen world cells make an
   air cell, so a fully packed pocket reads 16 * AIR_GAS_UNIT and an empty one
   reads zero -- the field's whole range is set by this and nothing else.

   AIR_BLOCKED marks a pocket that is mostly solid. It is not a pressure, it is
   a sentinel: a wall neither holds pressure nor passes it, and treating it as
   "very high pressure" would make gas flee walls, while treating it as zero
   would make gas pour into them. Excluded from the relaxation entirely.

   AIR_RELAX_PASSES is how far pressure travels in one frame. Two is enough to
   reach across a small chamber over a handful of frames and cheap enough not to
   be worth arguing about. */
static const int AIR_GAS_UNIT     = 16;
static const int AIR_RELAX_PASSES = 2;
static const i16 AIR_BLOCKED      = -1;

/* --- air has weight ---------------------------------------------------------
   Pressure rises with depth, by this much per air row. Without it the field is
   a pure crowding map: its gradient points away from a dense pocket equally in
   every direction, including DOWN, so the push spent the parcel's move
   spreading it sideways instead of lifting it. Measured that way, the rise got
   worse -- 1.53 s to climb 40 cells against 1.00 s without any field at all --
   which is the field competing with buoyancy for the one move a gas gets.

   With weight, the gradient tilts: the lowest pressure in an open pocket is at
   its top, so downhill and buoyancy AGREE and the push reinforces the climb
   instead of fighting it. That is also the actual physics -- buoyancy IS a
   pressure gradient, and modelling the gradient without gravity in it was
   modelling half of the thing.

   24 against a fully-packed pocket's 256: gravity sets a floor that tilts every
   open pocket upward, and local crowding still dominates near a plume, which is
   what keeps a vent venting sideways. Over the world's 2304 air rows the
   absolute baseline reaches 55k -- so it is deliberately NOT stored absolutely;
   see the note at the sampling loop. */
static const int AIR_DEPTH_WEIGHT = 24;

void airClear() {
    memset(g_airP, 0, sizeof(g_airP));
}

/* The live window in AIR coordinates, clipped to the field and to the playable
   area, with one air cell of margin so the relaxation has somewhere to read
   from at the edges. */
static void airWindow(const World& w, int* ax0, int* ay0, int* ax1, int* ay1) {
    int x0 = w.liveX0, y0 = w.liveY0, x1 = w.liveX1, y1 = w.liveY1;
    if (x0 < PLAY_X0) x0 = PLAY_X0;
    if (y0 < PLAY_Y0) y0 = PLAY_Y0;
    if (x1 > PLAY_X1) x1 = PLAY_X1;
    if (y1 > PLAY_Y1) y1 = PLAY_Y1;
    *ax0 = (x0 >> AIR_SHIFT) - 1;
    *ay0 = (y0 >> AIR_SHIFT) - 1;
    *ax1 = (x1 >> AIR_SHIFT) + 1;
    *ay1 = (y1 >> AIR_SHIFT) + 1;
    if (*ax0 < 0) *ax0 = 0;
    if (*ay0 < 0) *ay0 = 0;
    if (*ax1 >= AIR_W) *ax1 = AIR_W - 1;
    if (*ay1 >= AIR_H) *ay1 = AIR_H - 1;
}

void airStep(const World& w) {
    int ax0, ay0, ax1, ay1;
    airWindow(w, &ax0, &ay0, &ax1, &ay1);
    if (ax1 < ax0 || ay1 < ay0) return;

    /* --- sample ---------------------------------------------------------
       One pass over the window counting what each 4x4 block contains. This is
       the only part that touches world cells, and it is sixteen reads per air
       cell -- the same total as reading the window once, which is what makes
       the whole feature affordable. */
    const int side = 1 << AIR_SHIFT;
    for (int ay = ay0; ay <= ay1; ++ay) {
        for (int ax = ax0; ax <= ax1; ++ax) {
            const int wx0 = ax << AIR_SHIFT, wy0 = ay << AIR_SHIFT;
            int gas = 0, solid = 0;
            for (int oy = 0; oy < side; ++oy) {
                const int wy = wy0 + oy;
                if (wy < PLAY_Y0 || wy > PLAY_Y1) { solid += side; continue; }
                for (int ox = 0; ox < side; ++ox) {
                    const int wx = wx0 + ox;
                    if (wx < PLAY_X0 || wx > PLAY_X1) { ++solid; continue; }
                    const u8 m = w.at(wx, wy).mat;
                    if (m == MAT_EMPTY) continue;
                    const u8 kind = MATS[m].kind;
                    if (kind == KIND_GAS) ++gas;
                    /* Liquids count as solid for this. A pocket of water is not
                       somewhere gas can expand into, and letting it read as low
                       pressure would have every plume trying to flow into the
                       nearest pool. Gas crossing water is the bubble rule's
                       business, and it works on its own. */
                    else if (kind != KIND_POWDER) ++solid;
                    else ++solid;
                }
            }
            const int ai = ay * AIR_W + ax;
            /* More than half solid and the pocket is a wall.

               The depth term is measured from the TOP OF THE WINDOW rather than
               from the top of the world, because an absolute baseline reaches
               55k over 2304 air rows and does not fit an i16. Only differences
               between neighbours are ever read (see airDownhill), so the origin
               is free to move -- and the window is the only frame of reference
               every cell being compared this frame shares. */
            const int depth = (ay - ay0) * AIR_DEPTH_WEIGHT;
            g_airP[ai] = (solid * 2 > side * side)
                       ? AIR_BLOCKED
                       : (i16)(depth + gas * AIR_GAS_UNIT);
        }
    }

    /* --- relax ----------------------------------------------------------
       Each open pocket moves toward the average of itself and its open
       orthogonal neighbours. Blocked pockets are skipped as sources and as
       neighbours, so pressure flows around a wall rather than through it --
       which is the one property that makes this worth having over a blur. */
    for (int pass = 0; pass < AIR_RELAX_PASSES; ++pass) {
        for (int ay = ay0; ay <= ay1; ++ay) {
            for (int ax = ax0; ax <= ax1; ++ax) {
                const int ai = ay * AIR_W + ax;
                const i16 here = g_airP[ai];
                if (here == AIR_BLOCKED) { g_airScratch[ai] = AIR_BLOCKED; continue; }
                int sum = (int)here * 2, n = 2;
                if (ax > 0)         { const i16 v = g_airP[ai - 1];     if (v != AIR_BLOCKED) { sum += v; ++n; } }
                if (ax < AIR_W - 1) { const i16 v = g_airP[ai + 1];     if (v != AIR_BLOCKED) { sum += v; ++n; } }
                if (ay > 0)         { const i16 v = g_airP[ai - AIR_W]; if (v != AIR_BLOCKED) { sum += v; ++n; } }
                if (ay < AIR_H - 1) { const i16 v = g_airP[ai + AIR_W]; if (v != AIR_BLOCKED) { sum += v; ++n; } }
                g_airScratch[ai] = (i16)(sum / n);
            }
        }
        for (int ay = ay0; ay <= ay1; ++ay)
            memcpy(&g_airP[ay * AIR_W + ax0], &g_airScratch[ay * AIR_W + ax0],
                   (size_t)(ax1 - ax0 + 1) * sizeof(i16));
    }
}

int airAt(int x, int y) {
    if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) return 0;
    const int v = g_airP[(y >> AIR_SHIFT) * AIR_W + (x >> AIR_SHIFT)];
    return v == AIR_BLOCKED ? 0 : v;
}

void airDownhill(int x, int y, int* dx, int* dy) {
    *dx = 0; *dy = 0;
    const int ax = x >> AIR_SHIFT, ay = y >> AIR_SHIFT;
    if (ax <= 0 || ay <= 0 || ax >= AIR_W - 1 || ay >= AIR_H - 1) return;
    const int ai = ay * AIR_W + ax;
    const i16 here = g_airP[ai];
    if (here == AIR_BLOCKED) return;

    int best = (int)here;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            if (!ox && !oy) continue;
            const i16 v = g_airP[ai + oy * AIR_W + ox];
            if (v == AIR_BLOCKED) continue;
            /* Strictly lower, so a flat pocket produces no direction at all
               rather than an arbitrary one -- a gas in still air should be
               moved by its own rules, not nudged by rounding. */
            if ((int)v < best) { best = v; *dx = ox; *dy = oy; }
        }
    }
}
