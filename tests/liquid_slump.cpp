/* --- a block of water must slump, not stand ---------------------------------

   Found with tools/powderbench.cpp. A poured body of water stood as a mesa
   with vertical walls, 300 cells tall, hundreds of frames after the pour, its
   steps sitting on the chunk grid. Two causes, both in updateLiquid:

     - the packed-fluid shortcut skipped the sideways hop for any parcel with
       its own liquid on all eight sides. That hop looks THROUGH its own liquid
       to an opening, and it is how a parcel inside a body gets out to the
       edge, so only the literal surface could flow;
     - PRESSURE_MAX was 10, so every row deeper than ten cells had the same
       reach and drained at the same rate -- which is what a vertical wall is.

   The scene is the measurement that showed it: a 200 x 300 block of water on
   a floor in a wide box. 600 frames later the block stood 246 tall with both
   faults, 199 with the shortcut fixed, and 119 with the cap at 32. The bound
   below sits between the fixed and the unfixed numbers with room either side,
   so it fails on either fault coming back and not on ordinary tuning.

   Compile with every source file except main.cpp. No socket, no window. */

#include "world.h"
#include "materials.h"
#include <stdio.h>

static World g_testWorld;

int main() {
    initMaterials();
    World& w = g_testWorld;
    w.reset();

    /* A 1024 x 768 box, walled like powderlike's play area. */
    const int OX = 1024, OY = 3072, W = 1024, H = 768;
    const int x1 = OX + W - 1, y1 = OY + H - 1;
    for (int y = OY - 6; y <= y1 + 6; ++y)
        for (int x = OX - 6; x <= x1 + 6; ++x) {
            const bool inside = x >= OX + 2 && x <= x1 - 2 && y >= OY + 2 && y <= y1 - 2;
            if (!inside) w.cells[y * SIM_W + x].mat = MAT_WALL;
        }
    w.setLiveWindow(OX, OY, x1, y1);

    const int floorY = y1 - 2;
    const int bx0 = OX + 400, bw = 200, bh = 300;
    for (int y = floorY - bh; y <= floorY; ++y)
        for (int x = bx0; x < bx0 + bw; ++x) w.setCell(x, y, MAT_WATER);
    long before = 0;
    for (int i = 0; i < SIM_W * SIM_H; i += 1) if (w.cells[i].mat == MAT_WATER) ++before;

    for (int f = 0; f < 600; ++f) w.step();

    int tallest = 0;
    long after = 0;
    for (int x = OX; x <= x1; ++x) {
        for (int y = OY; y <= floorY; ++y)
            if (w.cells[y * SIM_W + x].mat == MAT_WATER) {
                if (floorY - y + 1 > tallest) tallest = floorY - y + 1;
                break;
            }
    }
    for (int i = 0; i < SIM_W * SIM_H; i += 1) if (w.cells[i].mat == MAT_WATER) ++after;

    /* Evaporation takes a few; a slump that got its height by losing water
       would not be a slump. */
    if (after < before * 99 / 100) {
        fprintf(stderr, "water lost: %ld -> %ld cells\n", before, after);
        return 1;
    }
    if (tallest > 170) {
        fprintf(stderr, "water block still %d tall after 600 frames (was 301); "
                "expected it to have slumped below 170\n", tallest);
        return 1;
    }
    printf("PASS: 301-tall block of water slumped to %d in 600 frames\n", tallest);
    return 0;
}
