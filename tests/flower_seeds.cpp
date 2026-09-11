#include "world.h"
#include "item.h"
#include "tree.h"
#include "save.h"
#include "multiplayer.h"
#include <stdio.h>

static Inventory inv;
static int failures;
static void check(bool ok, const char* message) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", message);
    if (!ok) ++failures;
}
static void growOn(u8 soil) {
    g_world.reset(); treesClear(); inv.clear();
    const int x = 1200, y = 1500;
    for (int dx = -35; dx <= 35; ++dx)
        for (int dy = 1; dy < 5; ++dy) g_world.setCell(x + dx, y + dy, soil);
    for (int dx = -40; dx <= 40; ++dx) g_world.setCell(x + dx, y + 5, MAT_STONE);
    inv.add(ITEM_FLOWER_SEED, 1); inv.selected = 0;
    check(placeFrom(g_world, inv, x, y - 16, 0) == 1, "seed can be placed above the ground");
    check(inv.countOf(ITEM_FLOWER_SEED) == 0, "placing consumes exactly one seed");
    bool rooted = false;
    for (int frame = 0; frame < 1000; ++frame) {
        g_world.step(); treesTick(g_world);
        rooted = rooted || treeCount() > 0;
    }
    check(rooted, "falling seed settles and roots");
    int blooms = 0, bx = 0, by = 0;
    for (int dx = -30; dx <= 30; ++dx)
        for (int dy = -30; dy <= 0; ++dy)
            if (g_world.at(x + dx, y + dy).mat == MAT_FLOWER) {
                ++blooms; bx = x + dx; by = y + dy;
            }
    check(blooms > 1, "plant grows a visible flower crown");
    if (blooms) {
        check(digInto(g_world, inv, bx, by, 0) == 1, "flower can be harvested");
        check(inv.countOf(ITEM_FLOWER_SEED) == 1, "harvesting gives a replantable seed");
    }
}
int main() {
    initMaterials(); initItems(); playerSessionsReset();
    g_world.reset();
    const u8 flowerParts[] = { MAT_FLOWER, MAT_STALK };
    for (int p = 0; p < 2; ++p) {
        g_world.setCell(1200, 1200, flowerParts[p]);
        check(!playerSolid(g_world, 1200, 1200, SOLID_ANY), "flower parts do not block walking or jumping");
        check(!playerSolid(g_world, 1200, 1200, SOLID_FLOOR), "flower parts cannot become a floor");
        check(g_matIsPlant[flowerParts[p]] != 0, "flower parts retain their plant classification");
    }
    check(g_matDrift[MAT_FLOWER_SEED] == g_matDrift[MAT_WHEAT_SEED], "flowers use the crop seed drift rate");
    for (int m = 1; m < MAT_COUNT; ++m)
        if (g_matIsSeed[m]) check(g_matDrift[m] > 0, "every seed species has sideways drift");
    /* Two airborne seeds with opposing tint directions must fan out, not
       merely reach the ground. This is the behavior the rooting test missed. */
    g_world.reset(); treesClear();
    g_world.setCell(1200, 1200, MAT_FLOWER_SEED);
    g_world.setCell(1202, 1200, MAT_FLOWER_SEED);
    g_world.cells[1200 * SIM_W + 1200].tint = 14;
    g_world.cells[1200 * SIM_W + 1202].tint = 15;
    for (int frame = 0; frame < 40; ++frame) g_world.step();
    int left = 9999, right = 0, seeds = 0;
    for (int y = 1200; y <= 1300; ++y)
        for (int x = 1100; x <= 1300; ++x)
            if (g_world.at(x, y).mat == MAT_FLOWER_SEED) {
                left = imin(left, x); right = imax(right, x); ++seeds;
            }
    check(seeds == 2, "drifting preserves both seeds");
    check(left < 1192 && right > 1210, "flower seeds visibly fan out in both directions during a fall");
    check(ITEMS[ITEM_FLOWER_SEED].kind == ITEMK_MATERIAL && g_matIsSeed[MAT_FLOWER_SEED], "flower uses ordinary seed placement and physics");
    growOn(MAT_DIRT); growOn(MAT_GRASS);
    g_world.reset(); treesClear();
    g_world.setCell(1200, 1500, MAT_FLOWER_SEED);
    g_world.setCell(1200, 1501, MAT_STONE);
    check(!treeCanRoot(g_world, 1200, 1500), "stone is not suitable soil");
    g_world.setCell(1200, 1501, MAT_DIRT);
    g_world.setCell(1200, 1499, MAT_STONE);
    check(!treeCanRoot(g_world, 1200, 1500), "buried seed does not sprout");
    g_world.setCell(1200, 1499, MAT_EMPTY);
    check(treeCanRoot(g_world, 1200, 1500), "exposed dry soil works");
    g_inv.clear();
    g_inv.slot[0].item = ITEM_FLOWER_SEED_LEGACY; g_inv.slot[0].count = 7;
    g_inv.add(ITEM_MULTITOOL, 1);
    /* Relative to build/tbin, which is where run_tests.sh runs a harness from
       -- not the repo root. "build/tests/..." resolved to a directory that
       does not exist there, so saveWrite failed and took three checks with it
       while passing when the binary was run by hand. Every other test in the
       suite writes its fixture as "build/<name>", which lands in the one
       directory that is always there. */
    const char* path = "build/flower-seed-migration.tmp";
    const bool written = saveWrite(path, g_world);
    check(written, "save fixture writes");
    if (written) {
        check(saveRead(path, g_world), "save fixture loads");
        check(g_inv.countOf(ITEM_FLOWER_SEED) == 7, "legacy flower seed stacks migrate");
        check(g_inv.countOf(ITEM_MULTITOOL) == 1, "other items remain intact");
        remove(path);
    }
    return failures ? 1 : 0;
}
