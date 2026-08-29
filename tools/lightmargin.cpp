/* ============================================================================
   lightmargin.cpp -- how big does the light margin actually need to be?

   The field is solved over the view plus a margin, and the margin is 128
   SAMPLES, which is 512 world cells on every side. That makes the solved field
   384x352 where the visible part is 128x96: ninety-one percent of every solve
   is thrown away.

   That was not waste when it was written. The margin existed so the field
   could be REUSED across frames -- the camera drifted inside the buffer and
   only the newly exposed strip needed solving. That machinery was removed when
   lighting moved to quarter resolution (see db4b9e6); lightUpdate is now a
   direct call to the full solve, so the buffer is rebuilt from scratch every
   frame and the margin no longer buys any reuse. What it still buys is
   CORRECTNESS AT THE EDGES: light entering from off-screen, and sun rays that
   travel diagonally and must be tracked from above the view.

   So the question is empirical -- how far in does the outside actually reach?
   This measures it the way light.cpp's own comment says such a claim should be
   checked: solve the same world at different margins and compare the visible
   field cell for cell against the full-margin answer.

   Built once per margin, since LIGHT_MARGIN is a compile-time constant:

     g++ -std=c++11 -O2 -Isrc -DLIGHT_MARGIN=32 tools/lightmargin.cpp ...
   ========================================================================== */
#include "world.h"
#include "materials.h"
#include "render.h"
#include "light.h"
#include "worldgen.h"
#include "item.h"
#include "sprite.h"
#include "multiplayer.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static World g_w;

struct Scene {
    const char* name;
    float       timeOfDay;   /* fraction of DAY_LENGTH */
    int         camDrop;     /* cells below spawn */
    int         lampDist;    /* how far off-screen the test lamps sit, in cells */
};

int main(int argc, char** argv) {
    const char* outDir = argc > 1 ? argv[1] : 0;

    LARGE_INTEGER fq; QueryPerformanceFrequency(&fq);
    const double freq = (double)fq.QuadPart;

    initMaterials();
    initItems();
    g_w.reset();
    generateWorld(g_w);

    float sx = 0, sy = 0;
    worldSpawnPoint(&sx, &sy);

    /* Deliberately spread: the sun only matters above ground and only when it
       is up, lamps only matter when it is dark, and a lamp far off-screen is
       the case the margin exists for. A single scene proves nothing. */
    static const Scene scenes[] = {
        { "night-surface",  0.70f,    0,  40 },
        { "day-surface",    0.20f,    0,  40 },
        { "dusk-surface",   0.47f,    0,  40 },
        { "day-deep",       0.20f, 1200,  40 },
        { "night-deep",     0.70f, 1200,  40 },
        { "night-farlamp",  0.70f,    0, 200 },
        { "day-farlamp",    0.20f,    0, 200 },
    };
    const int NS = (int)(sizeof(scenes) / sizeof(scenes[0]));
    const int N = 60;
    double worst = 0.0;

    for (int i = 0; i < NS; ++i) {
        const Scene& sc = scenes[i];
        int camX = (int)sx - VIEW_CELLS_W / 2;
        int camY = (int)sy - VIEW_CELLS_H / 2 + sc.camDrop;
        if (camY > SIM_H - VIEW_CELLS_H) camY = SIM_H - VIEW_CELLS_H;

        for (int k = 0; k < 3; ++k) {
            g_w.setCell(camX - sc.lampDist, camY + 100 + k, MAT_LAMP);
            g_w.setCell(camX + VIEW_CELLS_W + sc.lampDist, camY + 140 + k, MAT_LAMP);
            g_w.setCell(camX + 200, camY + 300 + k, MAT_LAVA);
        }

        g_worldTime = (u32)(DAY_LENGTH * sc.timeOfDay);
        for (int k = 0; k < 10; ++k) { lightClearDynamic(); lightCompute(g_w, camX, camY); }

        LARGE_INTEGER t0; QueryPerformanceCounter(&t0);
        for (int k = 0; k < N; ++k) { lightClearDynamic(); lightCompute(g_w, camX, camY); }
        LARGE_INTEGER t1; QueryPerformanceCounter(&t1);
        const double ms = 1000.0 * (double)(t1.QuadPart - t0.QuadPart) / freq / N;
        if (ms > worst) worst = ms;

        if (outDir) {
            char path[256];
            sprintf(path, "%s/%s-m%d.bin", outDir, sc.name, LIGHT_MARGIN);
            FILE* f = fopen(path, "wb");
            if (f) {
                for (int vy = 0; vy < VIEW_CELLS_H; ++vy)
                    for (int vx = 0; vx < VIEW_CELLS_W; ++vx) {
                        const unsigned char l = (unsigned char)lightAt(vx, vy);
                        fwrite(&l, 1, 1, f);
                    }
                fclose(f);
            }
        }
        printf("  margin %3d  %-14s %6.3f ms\n", LIGHT_MARGIN, sc.name, ms);
    }
    printf("  margin %3d  field %d x %d = %d samples, worst scene %.3f ms\n",
           LIGHT_MARGIN, LIGHT_W, LIGHT_H, LIGHT_W * LIGHT_H, worst);
    return 0;
}
