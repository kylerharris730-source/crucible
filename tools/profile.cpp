/* ============================================================================
   profile.cpp -- where a frame actually goes.

   The question this answers is "what should a lightweight build change", and
   the point of measuring rather than guessing is that the obvious answers are
   usually wrong. Lighting was once ninety percent of a frame here and is now
   incremental; the sim is chunked, so settled ground is free however much of
   it there is. Neither fact is visible from reading the code.

   It reproduces the real frame as closely as a headless harness can: the same
   live window the game sets around the player (view plus SIM_MARGIN), the same
   phase order out of serverTick and clientRender, and the same renderer and
   light solver. What it leaves out is the panel, which is GDI and not portable
   here -- that is measured separately in the browser.

   Build like the other harnesses, all of src except main.cpp:

     g++ -std=c++11 -O2 -Isrc tools/profile.cpp <src/*.cpp except main> \
         -o build/profile.exe -lgdi32 -luser32 -lwinmm -lmsimg32 -lws2_32
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
#include <math.h>

static World g_w;
static u32   g_px[VIEW_CELLS_W * VIEW_CELLS_H];

static const int SIM_MARGIN = 192;   /* matches main.cpp */

static double g_freq;
static double now() {
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (double)t.QuadPart / g_freq;
}

struct Acc {
    double total; double worst; int n;
    Acc() : total(0), worst(0), n(0) {}
    void add(double ms) { total += ms; if (ms > worst) worst = ms; ++n; }
    double avg() const { return n ? total / n : 0.0; }
};

struct Frame {
    Acc sim, aux, light, render;
    double avgTotal() const { return sim.avg() + aux.avg() + light.avg() + render.avg(); }
};

/* One frame, phases timed separately, in the order the game runs them. */
static void runFrame(int camX, int camY, bool lit, Frame& f) {
    g_w.setLiveWindow(camX - SIM_MARGIN, camY - SIM_MARGIN,
                      camX + VIEW_CELLS_W + SIM_MARGIN,
                      camY + VIEW_CELLS_H + SIM_MARGIN);

    double t0 = now();
    g_w.step();
    double t1 = now();

    projUpdate(g_w);
    roomsTick(g_w);
    devTick(g_w);
    treesTick(g_w);
    dayAdvance();
    double t2 = now();

    if (lit) { lightClearDynamic(); lightUpdate(g_w, camX, camY); }
    double t3 = now();

    renderView(g_w, g_px, VIEW_NORMAL, camX, camY, lit);
    double t4 = now();

    f.sim.add((t1 - t0) * 1000.0);
    f.aux.add((t2 - t1) * 1000.0);
    f.light.add((t3 - t2) * 1000.0);
    f.render.add((t4 - t3) * 1000.0);
}

static void report(const char* label, const Frame& f) {
    printf("  %-26s sim %6.3f  aux %6.3f  light %6.3f  render %6.3f  = %6.3f ms"
           "   (worst frame %6.3f)\n",
           label, f.sim.avg(), f.aux.avg(), f.light.avg(), f.render.avg(),
           f.avgTotal(),
           f.sim.worst + f.aux.worst + f.light.worst + f.render.worst);
}

/* Carve a hole and fill it, which is what a player actually does and what
   wakes chunks up. A settled world measures the floor, not the game. */
static void disturb(World& w, int camX, int camY, u8 mat, int radius) {
    const int cx = camX + VIEW_CELLS_W / 2, cy = camY + VIEW_CELLS_H / 2;
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
            if (x * x + y * y <= radius * radius)
                w.setCell(cx + x, cy + y, mat);
}

int main(void) {
    LARGE_INTEGER fq; QueryPerformanceFrequency(&fq);
    g_freq = (double)fq.QuadPart;

    printf("=== static footprint ===\n");
    const double cells = (double)sizeof(Cell) * SIM_W * SIM_H;
    const double plane = (double)SIM_W * SIM_H;
    printf("  world %d x %d = %.1f M cells\n", SIM_W, SIM_H, plane / 1e6);
    printf("  cells   %6.1f MB   (%d bytes/cell)\n", cells / 1048576.0, (int)sizeof(Cell));
    printf("  temp    %6.1f MB\n", plane / 1048576.0);
    printf("  bg      %6.1f MB\n", plane / 1048576.0);
    printf("  sizeof(World) %.1f MB\n", (double)sizeof(World) / 1048576.0);

    initMaterials();
    initItems();
    g_w.reset();
    printf("\ngenerating world...\n");
    double g0 = now();
    generateWorld(g_w);
    printf("  worldgen %.0f ms\n", (now() - g0) * 1000.0);

    float sx = 0, sy = 0;
    worldSpawnPoint(&sx, &sy);
    const int camX = (int)sx - VIEW_CELLS_W / 2;
    const int camY = (int)sy - VIEW_CELLS_H / 2;
    printf("  spawn %.0f,%.0f  cam %d,%d\n", sx, sy, camX, camY);

    const int WARM = 30, N = 240;

    printf("\n=== per-phase cost, %d frames each ===\n", N);

    {   /* Settled world: the floor. */
        Frame f;
        for (int i = 0; i < WARM; ++i) runFrame(camX, camY, true, f);
        Frame m;
        for (int i = 0; i < N; ++i) runFrame(camX, camY, true, m);
        report("settled, lit", m);
    }
    {   /* Same, lighting off -- what the Light toggle already buys. */
        Frame f;
        for (int i = 0; i < WARM; ++i) runFrame(camX, camY, false, f);
        Frame m;
        for (int i = 0; i < N; ++i) runFrame(camX, camY, false, m);
        report("settled, unlit", m);
    }
    {   /* A cavity of falling sand: chunks awake, material moving. */
        disturb(g_w, camX, camY, MAT_SAND, 40);
        Frame m;
        for (int i = 0; i < N; ++i) runFrame(camX, camY, true, m);
        report("falling sand, lit", m);
    }
    {   /* Water, which spreads far wider than sand and keeps chunks awake. */
        disturb(g_w, camX, camY, MAT_WATER, 40);
        Frame m;
        for (int i = 0; i < N; ++i) runFrame(camX, camY, true, m);
        report("flowing water, lit", m);
    }
    {   /* Lava: movement plus heat plus light emission, all at once. */
        disturb(g_w, camX, camY, MAT_LAVA, 30);
        Frame m;
        for (int i = 0; i < N; ++i) runFrame(camX, camY, true, m);
        report("lava, lit", m);
    }
    {
        Frame m;
        for (int i = 0; i < N; ++i) runFrame(camX, camY, false, m);
        report("lava, unlit", m);
    }

    printf("\n=== render alone, by view mode ===\n");
    {
        double t0 = now();
        for (int i = 0; i < N; ++i) renderView(g_w, g_px, VIEW_NORMAL, camX, camY, false);
        printf("  unlit  %.3f ms\n", (now() - t0) * 1000.0 / N);
        t0 = now();
        for (int i = 0; i < N; ++i) renderView(g_w, g_px, VIEW_NORMAL, camX, camY, true);
        printf("  lit    %.3f ms\n", (now() - t0) * 1000.0 / N);
    }
    printf("\ndone\n");
    return 0;
}
