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

/* Carried down the rows by the sun pass -- see lightCompute. */
static u8 g_sun[LIGHT_W];

/* Anything that is not solid or liquid lets daylight straight through. Gases
   attenuate light (see g_matOpacity) but they do not cast a shadow, which is
   the difference between smoke dimming a room and smoke turning it to night. */
static inline bool lightOpen(u8 m) {
    return m == MAT_EMPTY || MATS[m].kind == KIND_GAS;
}

/* --- where daylight comes from ---------------------------------------------
   Sunlight is not a source in the buffer; it is a column property. Every cell
   with an unobstructed run of open cells above it is at full brightness, and
   the first solid thing in the column ends it.

   The awkward part is that the run continues off the top of the light
   rectangle, and the honest answer -- walk up to the surface -- is a scan
   thousands of cells long when you are deep underground, every column, every
   frame. So the question is answered in two cheap halves instead:

     the ZONE says whether this chunk is outdoors at all, which is exactly what
     generation already decided and stored (see ZoneId in world.h), and

     a short probe upward rules out standing under a roof that begins just off
     the top of the screen.

   SUN_PROBE is what that costs, and it bounds the error too: a chamber whose
   ceiling is more than SUN_PROBE cells above the top of the light rectangle
   AND which sits in a sky chunk would take daylight it should not. That needs
   a room over 100 cells tall built above ground; a room-sized room is covered,
   and being generous with sunlight outdoors is the harmless direction to be
   wrong in. Underground -- where a false sunbeam would actually matter -- the
   zone label refuses before the probe even runs. */
static const int SUN_PROBE = 112;

static bool openToSky(const World& w, int x, int yTop) {
    const int zy = yTop < 0 ? 0 : (yTop >= SIM_H ? SIM_H - 1 : yTop);
    if (w.zoneAt(x, zy) != ZONE_SKY) return false;
    for (int y = yTop; y > yTop - SUN_PROBE; --y) {
        if (y < 0) return true;              /* out the top of the world */
        if (y >= SIM_H) continue;
        if (!lightOpen(w.cells[y * SIM_W + x].mat)) return false;
    }
    return true;
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

    for (int lx = 0; lx < LIGHT_W; ++lx)
        g_sun[lx] = (lx >= lx0 && lx < lx1 && openToSky(w, wx0 + lx, wy0))
                  ? (u8)LIGHT_MAX : 0;

    /* One pass gathers emission and attenuation and carries the sun down the
       columns at the same time. The sun genuinely wants a column walk, but
       doing it as one is a strided read of the world 480 cells long per
       column; carrying it row by row in g_sun[] gets the identical answer out
       of memory the gather is already touching. */
    for (int ly = 0; ly < LIGHT_H; ++ly) {
        const int wy = wy0 + ly;
        u8* L = g_light + ly * LIGHT_W;
        u8* A = g_att   + ly * LIGHT_W;

        if (wy < 0 || wy >= SIM_H) {
            memset(L, 0, LIGHT_W);
            memset(A, 255, LIGHT_W);        /* the void swallows light */
            continue;
        }
        if (lx0 > 0)       { memset(L, 0, lx0); memset(A, 255, lx0); }
        if (lx1 < LIGHT_W) { memset(L + lx1, 0, LIGHT_W - lx1);
                             memset(A + lx1, 255, LIGHT_W - lx1); }

        const Cell* row = w.cells + wy * SIM_W + wx0;
        for (int lx = lx0; lx < lx1; ++lx) {
            const u8 m = row[lx].mat;
            u8 lit = g_matLight[m];
            A[lx] = g_matOpacity[m];
            if (g_sun[lx]) {
                if (lightOpen(m)) { if (lit < g_sun[lx]) lit = g_sun[lx]; }
                else                g_sun[lx] = 0;
            }
            L[lx] = lit;
        }
    }

    for (int p = 0; p < LIGHT_PASSES; ++p) {
        sweepForward();
        sweepBackward();
    }
}
