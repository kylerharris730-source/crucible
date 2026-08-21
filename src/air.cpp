#include "air.h"
#include "materials.h"
#include <string.h>

i16 g_airVX[AIR_W * AIR_H];
i16 g_airVY[AIR_W * AIR_H];
i16 g_airP [AIR_W * AIR_H];

/* Advection reads the whole neighbourhood while writing, so it needs somewhere
   else to write. Static rather than stack: these are 4.7 MB each. */
static i16 g_vx2[AIR_W * AIR_H];
static i16 g_vy2[AIR_W * AIR_H];
/* One bit per air cell, rebuilt each frame: is this pocket a wall? */
static u8  g_airWall[AIR_W * AIR_H];

/* --- the dials, and what each one does --------------------------------------
   Every one of these is in fixed point against AIR_V_ONE = 256.

   BUOYANCY is the only SOURCE in the system: gas cells push their pocket
   upward. Everything else -- circulation, the return flow, the sideways spread
   at a ceiling -- is a consequence of that source plus incompressibility, which
   is the whole reason this is worth more than a per-cell rule.

   PRESS_GAIN turns divergence into pressure and GRAD_GAIN turns pressure back
   into velocity. Together they are what stops the air simply blowing outward
   forever: air leaving a pocket lowers its pressure, which pulls neighbours in.
   Set both too high and the field rings; the damping below is what keeps that
   from becoming a standing oscillation.

   DAMP is velocity retained per frame. Air with no drag never settles, and a
   room that is still churning a minute after the fire went out reads as broken
   rather than as physical. */
int g_airBuoyancy  = 26;
int g_airPressGain = 48;
int g_airGradGain  = 40;
int g_airDamp      = 242;      /* of 256 */
int g_airOn        = 1;

void airClear() {
    memset(g_airVX, 0, sizeof(g_airVX));
    memset(g_airVY, 0, sizeof(g_airVY));
    memset(g_airP,  0, sizeof(g_airP));
    memset(g_airWall, 0, sizeof(g_airWall));
}

static void airWindow(const World& w, int* ax0, int* ay0, int* ax1, int* ay1) {
    int x0 = w.liveX0, y0 = w.liveY0, x1 = w.liveX1, y1 = w.liveY1;
    if (x0 < PLAY_X0) x0 = PLAY_X0;
    if (y0 < PLAY_Y0) y0 = PLAY_Y0;
    if (x1 > PLAY_X1) x1 = PLAY_X1;
    if (y1 > PLAY_Y1) y1 = PLAY_Y1;
    /* One cell of margin so every interior cell has neighbours to read. */
    *ax0 = (x0 >> AIR_SHIFT) - 1;
    *ay0 = (y0 >> AIR_SHIFT) - 1;
    *ax1 = (x1 >> AIR_SHIFT) + 1;
    *ay1 = (y1 >> AIR_SHIFT) + 1;
    if (*ax0 < 1) *ax0 = 1;
    if (*ay0 < 1) *ay0 = 1;
    if (*ax1 > AIR_W - 2) *ax1 = AIR_W - 2;
    if (*ay1 > AIR_H - 2) *ay1 = AIR_H - 2;
}

/* Bilinear sample of a field at fractional air coordinates, in 1/256 units.
   Used by the advection step: a parcel's new velocity is the velocity at the
   place it came FROM, which is what makes momentum persist and curl instead of
   just diffusing. */
static int sampleField(const i16* f, int fx, int fy) {
    int ax = fx >> 8, ay = fy >> 8;
    const int rx = fx & 255, ry = fy & 255;
    if (ax < 0) ax = 0;
    if (ay < 0) ay = 0;
    if (ax > AIR_W - 2) ax = AIR_W - 2;
    if (ay > AIR_H - 2) ay = AIR_H - 2;
    const int i = ay * AIR_W + ax;
    const int a = f[i],         b = f[i + 1];
    const int c = f[i + AIR_W], d = f[i + AIR_W + 1];
    const int top = a + (((b - a) * rx) >> 8);
    const int bot = c + (((d - c) * rx) >> 8);
    return top + (((bot - top) * ry) >> 8);
}

void airStep(const World& w) {
    if (!g_airOn) return;
    int ax0, ay0, ax1, ay1;
    airWindow(w, &ax0, &ay0, &ax1, &ay1);
    if (ax1 < ax0 || ay1 < ay0) return;

    const int side = 1 << AIR_SHIFT;

    /* --- 1. sample the world, and inject buoyancy ------------------------
       The only pass that touches world cells: sixteen reads per air cell over
       the window. A pocket that is mostly solid becomes a wall; gas in a pocket
       pushes it upward. */
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
                    if (MATS[m].kind == KIND_GAS) ++gas;
                    else ++solid;   /* liquids and powders are not open air */
                }
            }
            const int ai = ay * AIR_W + ax;
            const bool wall = solid * 2 > side * side;
            g_airWall[ai] = wall ? 1u : 0u;
            if (wall) { g_airVX[ai] = 0; g_airVY[ai] = 0; g_airP[ai] = 0; continue; }
            /* Upward is negative y. Proportional to how much gas is in the
               pocket, so a dense plume drives a strong updraft and a stray
               wisp barely stirs the air. */
            g_airVY[ai] = (i16)(g_airVY[ai] - (gas * g_airBuoyancy) / side);
        }
    }

    /* --- 2. advect velocity by itself -----------------------------------
       Semi-Lagrangian: each cell takes the velocity from where its air came
       from. This is what gives the field momentum -- without it the solver is
       a diffusion, and a diffusion cannot curl. */
    for (int ay = ay0; ay <= ay1; ++ay) {
        for (int ax = ax0; ax <= ax1; ++ax) {
            const int ai = ay * AIR_W + ax;
            if (g_airWall[ai]) { g_vx2[ai] = 0; g_vy2[ai] = 0; continue; }
            /* Velocity is in world cells per frame; the grid is in units of
               `side` world cells, so a step back through the field is scaled
               down by that. */
            const int backX = (ax << 8) - (g_airVX[ai] << 8) / (AIR_V_ONE * side);
            const int backY = (ay << 8) - (g_airVY[ai] << 8) / (AIR_V_ONE * side);
            g_vx2[ai] = (i16)sampleField(g_airVX, backX, backY);
            g_vy2[ai] = (i16)sampleField(g_airVY, backX, backY);
        }
    }
    for (int ay = ay0; ay <= ay1; ++ay) {
        const int row = ay * AIR_W + ax0;
        const size_t n = (size_t)(ax1 - ax0 + 1) * sizeof(i16);
        memcpy(&g_airVX[row], &g_vx2[row], n);
        memcpy(&g_airVY[row], &g_vy2[row], n);
    }

    /* --- 3. divergence becomes pressure ---------------------------------
       Air piling into a pocket raises its pressure; air leaving lowers it.
       A wall neighbour contributes nothing, which is what makes a wall a wall:
       flow cannot cross it, so it cannot relieve pressure through it. */
    for (int ay = ay0; ay <= ay1; ++ay) {
        for (int ax = ax0; ax <= ax1; ++ax) {
            const int ai = ay * AIR_W + ax;
            if (g_airWall[ai]) { g_airP[ai] = 0; continue; }
            const int l = g_airWall[ai - 1]     ? 0 : g_airVX[ai - 1];
            const int r = g_airWall[ai + 1]     ? 0 : g_airVX[ai + 1];
            const int u = g_airWall[ai - AIR_W] ? 0 : g_airVY[ai - AIR_W];
            const int d = g_airWall[ai + AIR_W] ? 0 : g_airVY[ai + AIR_W];
            const int div = ((r - l) + (d - u)) / 2;
            int p = g_airP[ai] - (div * g_airPressGain) / 256;
            /* Bounded hard. An unbounded pressure integrator in fixed point
               finds a way to saturate, and a saturated field is a field that
               has stopped carrying information. */
            if (p >  8000) p =  8000;
            if (p < -8000) p = -8000;
            g_airP[ai] = (i16)((p * g_airDamp) / 256);
        }
    }

    /* --- 4. pressure gradient becomes velocity, then damping ------------- */
    for (int ay = ay0; ay <= ay1; ++ay) {
        for (int ax = ax0; ax <= ax1; ++ax) {
            const int ai = ay * AIR_W + ax;
            if (g_airWall[ai]) continue;
            const int pl = g_airWall[ai - 1]     ? g_airP[ai] : g_airP[ai - 1];
            const int pr = g_airWall[ai + 1]     ? g_airP[ai] : g_airP[ai + 1];
            const int pu = g_airWall[ai - AIR_W] ? g_airP[ai] : g_airP[ai - AIR_W];
            const int pd = g_airWall[ai + AIR_W] ? g_airP[ai] : g_airP[ai + AIR_W];
            int vx = g_airVX[ai] - ((pr - pl) * g_airGradGain) / 512;
            int vy = g_airVY[ai] - ((pd - pu) * g_airGradGain) / 512;
            vx = (vx * g_airDamp) / 256;
            vy = (vy * g_airDamp) / 256;
            /* Clamped to what a parcel could actually be moved by, so the
                field can never ask for a step the mover will not take. */
            const int cap = AIR_ADVECT_MAX * AIR_V_ONE;
            if (vx >  cap) vx =  cap;
            if (vx < -cap) vx = -cap;
            if (vy >  cap) vy =  cap;
            if (vy < -cap) vy = -cap;
            g_airVX[ai] = (i16)vx;
            g_airVY[ai] = (i16)vy;
        }
    }
}

void airVelocity(int x, int y, int* vx, int* vy) {
    *vx = 0; *vy = 0;
    if (!g_airOn) return;
    if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) return;
    const int ai = (y >> AIR_SHIFT) * AIR_W + (x >> AIR_SHIFT);
    if (g_airWall[ai]) return;
    *vx = g_airVX[ai];
    *vy = g_airVY[ai];
}

int airPressureAt(int x, int y) {
    if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) return 0;
    return g_airP[(y >> AIR_SHIFT) * AIR_W + (x >> AIR_SHIFT)];
}
