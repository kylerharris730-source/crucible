#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "tree.h"
#include "entity.h"
#include "multiplayer.h"
#include <stdio.h>

/* A seed roots on dirt or grass, WET OR DRY.

   The moisture requirement was removed because it was invisible: a dry seed
   looked exactly like one about to sprout, so the lesson players took from it
   was that seeds are unreliable rather than that soil needs watering. This
   pins the new rule down, and just as importantly pins down the parts that did
   NOT change -- a rule removal is the easy place to take a neighbouring rule
   with it by accident.

   Compile with all source files except main.cpp. */

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

/* Ground at `y`, seed resting on top of it at y-1, open above that. */
static void layout(World& w, int x, int y, u8 ground, u8 seed, u8 above) {
    for (int dy = -3; dy <= 3; ++dy)
        for (int dx = -3; dx <= 3; ++dx)
            w.setCell(x + dx, y + dy, MAT_EMPTY);
    w.setCell(x, y, ground);
    w.setCell(x, y - 1, seed);
    w.setCell(x, y - 2, above);
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    g_world.reset();
    treesClear();

    /* Any seed will do; the rule under test is about the ground, not the
       species. Taken from the table rather than hard-coded so adding a species
       cannot silently leave this testing a material that is no longer a seed. */
    u8 seed = MAT_EMPTY;
    for (int m = 1; m < MAT_COUNT; ++m)
        if (treeSpeciesOfSeed((u8)m) >= 0) { seed = (u8)m; break; }
    if (seed == MAT_EMPTY) {
        fprintf(stderr, "no seed material found in the species table\n");
        return 2;
    }

    const int x = 600, y = 600;
    const size_t below = (size_t)y * SIM_W + (size_t)x;

    /* --- the change: bone-dry dirt must now root ------------------------- */
    layout(g_world, x, y, MAT_DIRT, seed, MAT_EMPTY);
    g_world.temp[below] = (u8)AMBIENT_TEMP;
    g_world.cells[below].moisture = 0;
    check(treeCanRoot(g_world, x, y - 1), "a seed roots on bone-dry dirt");

    layout(g_world, x, y, MAT_GRASS, seed, MAT_EMPTY);
    g_world.cells[below].moisture = 0;
    check(treeCanRoot(g_world, x, y - 1), "a seed roots on bone-dry grass");

    /* Wet ground obviously still works -- the rule got weaker, not different,
       and a change that broke the case that always worked would be a bad
       trade for the one that did not. */
    layout(g_world, x, y, MAT_DIRT, seed, MAT_EMPTY);
    g_world.cells[below].moisture = (u8)MATS[MAT_DIRT].capacity;
    check(treeCanRoot(g_world, x, y - 1), "a seed still roots on wet dirt");

    /* --- what did NOT change -------------------------------------------- */
    layout(g_world, x, y, MAT_STONE, seed, MAT_EMPTY);
    check(!treeCanRoot(g_world, x, y - 1), "a seed does not root on stone");

    layout(g_world, x, y, MAT_SAND, seed, MAT_EMPTY);
    check(!treeCanRoot(g_world, x, y - 1), "a seed does not root on sand");

    /* Buried: something solid directly above the seed. Without this a seed
       swallowed by a landslide sprouts a tree through the rock on top of it. */
    layout(g_world, x, y, MAT_DIRT, seed, MAT_STONE);
    g_world.cells[below].moisture = 0;
    check(!treeCanRoot(g_world, x, y - 1), "a buried seed does not root");

    /* Not a seed at all. */
    layout(g_world, x, y, MAT_DIRT, MAT_SAND, MAT_EMPTY);
    check(!treeCanRoot(g_world, x, y - 1), "a non-seed does not root");

    if (failures) {
        fprintf(stderr, "%d seed rooting check(s) failed\n", failures);
        return 1;
    }
    puts("seeds root on dry dirt and grass, and nowhere they should not");
    return 0;
}
