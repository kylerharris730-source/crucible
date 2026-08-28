/* --- what does routing cost per frame? --------------------------------------

   The flow field is the one thing in the creature code whose price is paid
   whether or not any creature uses it: navUpdate re-reads a 1664 x 1664 block
   of the world and runs two searches over it, on a clock, regardless of
   whether there is a single husk alive. So it is worth a standing measurement
   rather than a one-off one.

   The budget below is deliberately loose. It is not here to police tuning --
   NAV_W, NAV_H and NAV_PERIOD are all meant to be adjustable -- it is here to
   catch a change that makes the rebuild an order of magnitude dearer, which is
   the kind of mistake that is invisible until the game stutters.

   Measured on a generated world at the time of writing: 1.96 ms a rebuild,
   which at one rebuild every twelve frames is 0.16 ms a frame against a 16.7
   ms budget, or about one percent.

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "player.h"
#include "multiplayer.h"
#include "navigate.h"
#include "worldgen.h"
#include <stdio.h>
#include <time.h>

static World g_bench;

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();
    int failures = 0;

    World& w = g_bench;
    w.reset();
    /* A REAL world, not a test box. A hand-carved arena is mostly one material
       and blockOpen stops at the first solid cell, so a synthetic cavern would
       measure the early-out rather than the work. */
    generateWorld(w);

    /* The sky should read blue even near the horizon, and springs should exist
       in both forms: dry hidden seams and visible chamber pools. This scan
       piggybacks on the generated world the navigation test already needs. */
    const u32 horizon = g_skyLut[SKY_BAND - 1];
    const int horizonG = (int)((horizon >> 8) & 255), horizonB = (int)(horizon & 255);
    if (horizonB - horizonG < 20) {
        fprintf(stderr, "FAIL: lower sky drifted grey (%06lx)\n", (unsigned long)horizon);
        ++failures;
    }
    int wetSpringCells = 0, drySpringCells = 0;
    for (int y = PLAY_Y0 + 12; y <= PLAY_Y1 - 12; ++y) {
        for (int x = PLAY_X0 + 12; x <= PLAY_X1 - 12; ++x) {
            if (w.at(x, y).mat != MAT_SPRING) continue;
            bool wet = false;
            for (int yy = y - 10; yy <= y + 10 && !wet; ++yy)
                for (int xx = x - 10; xx <= x + 10; ++xx)
                    if (w.at(xx, yy).mat == MAT_WATER) { wet = true; break; }
            if (wet) ++wetSpringCells; else ++drySpringCells;
        }
    }
    printf("springs: %d chamber pools, %d wet source cells, %d hidden source cells\n",
           g_openSpringsPlaced, wetSpringCells, drySpringCells);
    if (g_openSpringsPlaced != 4 || wetSpringCells < 16 || drySpringCells < 100) {
        fprintf(stderr, "FAIL: visible/hidden spring mix missing (pools %d, wet %d, dry %d)\n",
                g_openSpringsPlaced, wetSpringCells, drySpringCells);
        ++failures;
    }

    float px, py;
    worldSpawnPoint(&px, &py);
    float sx[1], sy[1];
    sx[0] = px; sy[0] = py;

    navReset();
    navUpdate(w, sx, sy, 1);      /* warm the caches */

    const int N = 100;
    const clock_t t0 = clock();
    for (int i = 0; i < N; ++i) {
        /* navReset forces the clock, so this times a hundred REBUILDS rather
           than one rebuild and ninety-nine early returns. */
        navReset();
        navUpdate(w, sx, sy, 1);
    }
    const clock_t t1 = clock();

    const double ms = 1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC / N;
    printf("window %d x %d nodes (%d x %d cells)\n",
           NAV_W, NAV_H, NAV_W * NAV, NAV_H * NAV);
    printf("rebuild %.2f ms; reached short %d, tall %d, of %d nodes\n",
           ms, navReached(0), navReached(1), NAV_W * NAV_H);
    printf("at one rebuild in 12 frames that is %.3f ms/frame of 16.7\n", ms / 12.0);

    if (ms > 12.0) {
        fprintf(stderr, "FAIL: %.2f ms a rebuild is most of a frame -- routing "
                        "has become the expensive thing in the game\n", ms);
        ++failures;
    }
    /* A field that reaches almost nothing is cheap for the wrong reason, and
       the timing above would happily report success while every creature fell
       back to the straight line. Around the spawn point there is open cave in
       every direction, so a few thousand nodes is the floor. */
    if (navReached(1) < 2000) {
        fprintf(stderr, "FAIL: the tall search reached only %d nodes -- it is "
                        "fast because it is not finding anything\n", navReached(1));
        ++failures;
    }
    /* Every ordinary enemy remains alive to this radius. A routing window
       smaller than that produces the worst possible discontinuity: an enemy
       is close enough to keep simulating but too far away to know about walls,
       so it visibly reverts to pressing straight into them. Keep a one-node
       margin because navHeading needs neighbours around its current node. */
    const int edge = ENT_DESPAWN_DIST - NAV;
    if (!navCovers((int)px - edge, (int)py) ||
        !navCovers((int)px + edge, (int)py) ||
        !navCovers((int)px, (int)py - edge) ||
        !navCovers((int)px, (int)py + edge)) {
        fprintf(stderr, "FAIL: routing does not cover the active-enemy radius\n");
        ++failures;
    }

    if (failures) { fprintf(stderr, "\n%d cost check(s) failed\n", failures); return 1; }
    printf("\nPASS\n");
    return 0;
}
