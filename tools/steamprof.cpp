/* ============================================================================
   steamprof.cpp -- the frame the player actually complains about.

   tools/profile.cpp measures steady states, and every one of them fits inside
   the 16.6 ms budget with room to spare. That is a true answer to the wrong
   question: nothing settled is slow. What is slow is the TRANSIENT -- lava
   dropped into water, where the pool boils, the steam expands under pressure,
   and the frame time doubles for a couple of seconds.

   Averages hide a transient by construction, so this reports the distribution
   instead: p50/p90/p99, the worst single frame, and the share of frames that
   miss 60 fps. Those are the numbers that correspond to what the eye sees.

   It also prints a census of the basin at the end. That is not decoration --
   it is the control. Nearly every way to make this scene faster works by
   simulating less of it, and without a census a variant that got quick by
   quietly refusing to boil the water looks like a win.

   Found, with this: the 33x33 bent-outlet search in updateGasPressure running
   6,597 times a frame at 4.28 ms, of which 1.70 ms was clearing an array
   before deciding it had nothing to search. See world.cpp.

   Build like the other harnesses, all of src except main.cpp:

     g++ -std=c++11 -O3 -Isrc tools/steamprof.cpp <src/(*).cpp except main> \
         -o artifacts/steamprof.exe -lgdi32 -luser32 -lwinmm -lmsimg32 -lws2_32

   Pass "unlit" to take the light solver out of the measurement.
   ========================================================================== */
#include "world.h"
#include "materials.h"
#include "render.h"
#include "light.h"
#include "worldgen.h"
#include "item.h"
#include "sprite.h"
#include "multiplayer.h"
#include "projectile.h"
#include "room.h"
#include "device.h"
#include "tree.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static World g_w;
static u32   g_px[VIEW_CELLS_W * VIEW_CELLS_H];

static const int SIM_MARGIN = 192;   /* matches main.cpp */

static double g_freq;
static double now(void) {
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (double)t.QuadPart / g_freq;
}

/* --- the scene ---------------------------------------------------------
   A stone box with water in it, hollowed out of whatever the worldgen put
   there, so the measurement does not depend on finding a convenient cave. */
static const int BASIN_HALF_W    = 150;
static const int BASIN_DEPTH     = 200;
static const int BASIN_WATER     = 90;
static const int SETTLE_FRAMES   = 400;
static const int MEASURE_FRAMES  = 900;

struct Sample { double sim, aux, light, render; int chunks; };
static Sample g_s[MEASURE_FRAMES];

/* One frame, phases timed separately, in the order serverTick runs them. */
static void runFrame(int camX, int camY, bool lit, Sample* s) {
    g_w.setLiveWindow(camX - SIM_MARGIN, camY - SIM_MARGIN,
                      camX + VIEW_CELLS_W + SIM_MARGIN,
                      camY + VIEW_CELLS_H + SIM_MARGIN);
    const double t0 = now();
    g_w.step();
    const double t1 = now();
    projUpdate(g_w);
    roomsTick(g_w);
    devTick(g_w);
    treesTick(g_w);
    dayAdvance();
    const double t2 = now();
    if (lit) { lightClearDynamic(); lightUpdate(g_w, camX, camY); }
    const double t3 = now();
    renderView(g_w, g_px, VIEW_NORMAL, camX, camY, lit);
    const double t4 = now();
    if (!s) return;
    s->sim    = (t1 - t0) * 1000.0;
    s->aux    = (t2 - t1) * 1000.0;
    s->light  = (t3 - t2) * 1000.0;
    s->render = (t4 - t3) * 1000.0;
    s->chunks = g_w.activeChunks;
}

static int cmpd(const void* a, const void* b) {
    const double x = *(const double*)a, y = *(const double*)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void report(const char* label, const Sample* s, int n) {
    double* tot = (double*)malloc(sizeof(double) * n);
    double sum = 0, sim = 0, aux = 0, light = 0, render = 0;
    int chunkMax = 0;
    for (int i = 0; i < n; ++i) {
        tot[i] = s[i].sim + s[i].aux + s[i].light + s[i].render;
        sum += tot[i]; sim += s[i].sim; aux += s[i].aux;
        light += s[i].light; render += s[i].render;
        if (s[i].chunks > chunkMax) chunkMax = s[i].chunks;
    }
    qsort(tot, n, sizeof(double), cmpd);
    int over = 0;
    for (int i = 0; i < n; ++i) if (tot[i] > 16.6) ++over;
    printf("  %-18s  mean %6.2f  p50 %6.2f  p90 %6.2f  p99 %6.2f  max %6.2f\n"
           "  %-18s  sim %6.2f  aux %5.2f  light %5.2f  render %5.2f"
           "   over 16.6ms %5.1f%%   chunks<=%d\n",
           label, sum / n, tot[n / 2], tot[(int)(n * 0.90)],
           tot[(int)(n * 0.99)], tot[n - 1],
           "", sim / n, aux / n, light / n, render / n,
           100.0 * over / n, chunkMax);
    free(tot);
}

static void buildBasin(int cx, int cy) {
    for (int y = cy - BASIN_DEPTH; y <= cy + 8; ++y)
        for (int x = cx - BASIN_HALF_W - 6; x <= cx + BASIN_HALF_W + 6; ++x) {
            const bool wall = (x < cx - BASIN_HALF_W) ||
                              (x > cx + BASIN_HALF_W) || (y > cy);
            g_w.setCell(x, y, wall ? (u8)MAT_STONE : (u8)MAT_EMPTY);
        }
    for (int y = cy - BASIN_WATER; y <= cy; ++y)
        for (int x = cx - BASIN_HALF_W; x <= cx + BASIN_HALF_W; ++x)
            g_w.setCell(x, y, MAT_WATER);
}

static void pourLava(int cx, int cy, int halfW, int h) {
    for (int y = cy - h; y <= cy; ++y)
        for (int x = cx - halfW; x <= cx + halfW; ++x)
            g_w.setCell(x, y, MAT_LAVA);
}

/* The control. A variant that is fast because it stopped boiling the water
   shows up here and nowhere else. */
static void census(int cx, int cy) {
    int steam = 0, excess = 0, water = 0, lava = 0, stone = 0;
    for (int y = cy - BASIN_DEPTH - 40; y <= cy + 8; ++y)
        for (int x = cx - BASIN_HALF_W - 8; x <= cx + BASIN_HALF_W + 8; ++x) {
            const Cell& c = g_w.cells[y * SIM_W + x];
            if (c.mat == MAT_STEAM) {
                ++steam;
                excess += (c.moisture & GAS_EXCESS_MASK);
            } else if (c.mat == MAT_WATER) ++water;
            else if (c.mat == MAT_LAVA)    ++lava;
            else if (c.mat == MAT_STONE)   ++stone;
        }
    printf("\n=== basin census after %d frames ===\n", MEASURE_FRAMES);
    printf("  steam %6d  (hidden volume %6d)   water %6d   lava %5d   stone %6d\n",
           steam, excess, water, lava, stone);
}

int main(int argc, char** argv) {
    LARGE_INTEGER fq; QueryPerformanceFrequency(&fq);
    g_freq = (double)fq.QuadPart;
    const bool lit = !(argc > 1 && strcmp(argv[1], "unlit") == 0);

    initMaterials();
    initItems();
    g_w.reset();
    printf("generating world...\n");
    generateWorld(g_w);

    float sx = 0, sy = 0;
    worldSpawnPoint(&sx, &sy);
    const int camX = (int)sx - VIEW_CELLS_W / 2;
    const int camY = (int)sy - VIEW_CELLS_H / 2;
    const int bx = (int)sx, by = (int)sy + 120;
    printf("  spawn %.0f,%.0f   basin %d,%d   lighting %s\n\n",
           sx, sy, bx, by, lit ? "on" : "off");

    buildBasin(bx, by);
    for (int i = 0; i < SETTLE_FRAMES; ++i) runFrame(camX, camY, lit, NULL);

    printf("=== the floor: basin dug and filled, nothing happening ===\n");
    for (int i = 0; i < 300; ++i) runFrame(camX, camY, lit, &g_s[i]);
    report("water settled", g_s, 300);

    /* The case itself. */
    pourLava(bx, by - 100, 60, 30);
    for (int i = 0; i < MEASURE_FRAMES; ++i) runFrame(camX, camY, lit, &g_s[i]);

    printf("\n=== lava into water ===\n");
    report("whole transient", g_s, MEASURE_FRAMES);
    report("first 150 frames", g_s, 150);

    census(bx, by);
    printf("\ndone\n");
    return 0;
}
