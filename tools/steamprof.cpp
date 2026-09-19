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

   And, with the "layer" scene: pressure searches that could not succeed. Steam
   boiled under a sheet of lava was sealed in by the basin above it, 94-98% of
   its searches found nothing, visiting ~300,000 cells a frame to find it, and
   the same buried cells took the whole budget every pass. A failed search now
   makes its chunk wait PRESSURE_SEARCH_WAIT passes: sim 10.1 -> 6.5 ms at 8
   threads on a 15-cell sheet, 1.6% of frames over budget -> none, and the
   "drain" census ends where it did. scripts/lagbench.sh runs these scenes.

   Build like the other harnesses, all of src except main.cpp:

     g++ -std=c++11 -O3 -Isrc tools/steamprof.cpp <src/(*).cpp except main> \
         -o artifacts/steamprof.exe -lgdi32 -luser32 -lwinmm -lmsimg32 -lws2_32

   Pass "unlit" to take the light solver out of the measurement, and a number
   to set the thread count (default 1). "drain" runs on after the census and
   repeats it every 300 frames. "layer" (with "thick=N", default 6) drops a sheet of lava
   across the whole basin instead of a blob into the middle of it -- the case
   reported as lagging: every cell of the surface boils at once. Set
   STEAMPROF_SAMPLES=file to sample the main thread's instruction pointer
   every millisecond (use 1 thread, and addr2line on a -g build).
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

/* --- a sampling profiler, the one in tools/powderbench.cpp -----------------
   gprof's mcount in every function IS the profile for code this hot; sampling
   costs the program nothing it would not otherwise spend. */
static HANDLE g_mainThread;
static volatile LONG g_sampling = 0;
static unsigned g_samples[1 << 20];
static volatile LONG g_nSamples = 0;
static DWORD WINAPI samplerMain(LPVOID) {
    while (g_sampling) {
        Sleep(1);
        if (SuspendThread(g_mainThread) == (DWORD)-1) continue;
        CONTEXT ctx; ctx.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(g_mainThread, &ctx) && g_nSamples < (1 << 20))
#ifdef _WIN64
            g_samples[g_nSamples++] = (unsigned)ctx.Rip;
#else
            g_samples[g_nSamples++] = (unsigned)ctx.Eip;
#endif
        ResumeThread(g_mainThread);
    }
    return 0;
}
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
    /* steamprof [unlit] [threads] -- threads defaults to 1, which runs every
       stripe on this thread. The stripe decomposition is used either way, so
       the numbers across thread counts are comparable and the worlds they
       produce are identical; see simSetWorkers in world.h. */
    bool lit = true, layer = false, drain = false;
    int layerThick = 6;
    int threads = 1;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "unlit") == 0) lit = false;
        else if (strcmp(argv[i], "layer") == 0) layer = true;
        else if (strcmp(argv[i], "drain") == 0) drain = true;
        else if (strncmp(argv[i], "thick=", 6) == 0) layerThick = atoi(argv[i] + 6);
        else threads = atoi(argv[i]);
    }
    simSetWorkers(threads);

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
    printf("  spawn %.0f,%.0f   basin %d,%d   lighting %s   threads %d\n\n",
           sx, sy, bx, by, lit ? "on" : "off", simWorkers() + 1);

    buildBasin(bx, by);
    for (int i = 0; i < SETTLE_FRAMES; ++i) runFrame(camX, camY, lit, NULL);

    printf("=== the floor: basin dug and filled, nothing happening ===\n");
    for (int i = 0; i < 300; ++i) runFrame(camX, camY, lit, &g_s[i]);
    report("water settled", g_s, 300);

    /* The case itself. A 121 x 31 blob into the middle, or a sheet the width
       of the basin and six cells thick -- about half the lava, spread over
       every cell of the surface. */
    if (layer) pourLava(bx, by - 100, BASIN_HALF_W, layerThick - 1);
    else       pourLava(bx, by - 100, 60, 30);
    const char* sampleOut = getenv("STEAMPROF_SAMPLES");
    if (sampleOut) {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                        &g_mainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
        timeBeginPeriod(1);
        g_sampling = 1;
        CreateThread(NULL, 0, samplerMain, NULL, 0, NULL);
    }
    for (int i = 0; i < MEASURE_FRAMES; ++i) runFrame(camX, camY, lit, &g_s[i]);
    if (sampleOut) {
        g_sampling = 0; Sleep(20);
        FILE* sf = fopen(sampleOut, "w");
        if (sf) {
            for (LONG i = 0; i < g_nSamples; ++i) fprintf(sf, "0x%x\n", g_samples[i]);
            fclose(sf);
        }
    }

    printf("\n=== lava into water (%s) ===\n", layer ? "a sheet across the basin" : "a blob");
    report("whole transient", g_s, MEASURE_FRAMES);
    report("first 150 frames", g_s, 150);

    /* The frames that are felt, by phase: a mean that looks fine can be a
       handful of frames that do not. */
    {
        int idx[MEASURE_FRAMES];
        for (int i = 0; i < MEASURE_FRAMES; ++i) idx[i] = i;
        for (int a = 0; a < 6; ++a)
            for (int b = a + 1; b < MEASURE_FRAMES; ++b) {
                const Sample& x = g_s[idx[a]]; const Sample& y = g_s[idx[b]];
                if (y.sim + y.aux + y.light + y.render > x.sim + x.aux + x.light + x.render) {
                    const int t = idx[a]; idx[a] = idx[b]; idx[b] = t;
                }
            }
        printf("  worst frames:\n");
        for (int a = 0; a < 6; ++a) {
            const Sample& q = g_s[idx[a]];
            printf("    frame %3d   sim %6.2f  light %5.2f  render %5.2f   chunks %d\n",
                   idx[a], q.sim, q.light, q.render, q.chunks);
        }
    }

    census(bx, by);
    /* "drain": keep going and census every 300 frames, so stored steam that is
       merely late can be told from stored steam that is stuck -- the snapshot
       at 900 frames cannot tell those apart. */
    if (drain)
        for (int k = 0; k < 6; ++k) {
            for (int i = 0; i < 300; ++i) runFrame(camX, camY, lit, NULL);
            census(bx, by);
        }
    printf("\ndone\n");
    return 0;
}
