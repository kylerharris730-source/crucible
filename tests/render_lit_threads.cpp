/* --- a lit frame is the same picture at any thread count ---------------------

   renderView draws row bands on the sim's thread pool. With lighting on, each
   band asked lightRow() for its rows' light, and lightRow hands back ONE static
   buffer -- so two bands on two threads overwrote each other's light and the
   picture came out with rows shaded by some other row. The first check of the
   threaded renderer compared pictures drawn UNLIT, which is why it passed.

   This draws the same lit view from a generated world on one thread and on
   eight and requires every pixel to match, then does it again with the light
   solver on the pool too.

   Compile with every source file except main.cpp. No socket, no window. */

#include "world.h"
#include "materials.h"
#include "render.h"
#include "light.h"
#include "worldgen.h"
#include "item.h"
#include <stdio.h>
#include <string.h>

static u32 g_one[VIEW_CELLS_W * VIEW_CELLS_H];
static u32 g_many[VIEW_CELLS_W * VIEW_CELLS_H];

int main() {
    initMaterials();
    initItems();
    g_world.reset();
    generateWorld(g_world);
    float sx = 0, sy = 0;
    worldSpawnPoint(&sx, &sy);
    /* Somewhere with both daylight and shadow in it: the surface at spawn,
       and a cave-ish slice further down. */
    const int views[2][2] = {
        { (int)sx - VIEW_CELLS_W / 2, (int)sy - VIEW_CELLS_H / 2 },
        { (int)sx - VIEW_CELLS_W / 2, (int)sy + 600 },
    };
    int failures = 0;
    for (int v = 0; v < 2; ++v) {
        const int camX = views[v][0], camY = views[v][1];

        simSetWorkers(1);
        lightInvalidate();
        lightUpdate(g_world, camX, camY);
        renderView(g_world, g_one, VIEW_NORMAL, camX, camY, true);

        simSetWorkers(8);
        lightInvalidate();
        lightUpdate(g_world, camX, camY);
        renderView(g_world, g_many, VIEW_NORMAL, camX, camY, true);

        int differ = 0, firstRow = -1;
        for (int i = 0; i < VIEW_CELLS_W * VIEW_CELLS_H; ++i)
            if (g_one[i] != g_many[i]) { if (firstRow < 0) firstRow = i / VIEW_CELLS_W; ++differ; }
        if (differ) {
            fprintf(stderr, "view %d: %d pixels differ between 1 and 8 threads (first at row %d)\n",
                    v, differ, firstRow);
            ++failures;
        }
    }
    simSetWorkers(1);
    if (failures) return 1;
    printf("PASS: lit views are pixel-identical on 1 and 8 threads\n");
    return 0;
}
