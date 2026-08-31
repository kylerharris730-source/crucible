#include "world.h"
#include "materials.h"
#include "device.h"
#include "item.h"
#include "sprite.h"
#include <stdio.h>

static int failures;
static void check(bool ok, const char* what) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    g_world.reset();
    sparkClear();

    const int x = 300, y = 300;
    check(shedPlace(x, y), "a loose spark can be placed freely in air");
    check(shedCount() == 1, "placement creates the falling wire-end mote");
    check(sparkCount() == 0, "placement does not directly inject wire current");
    check(shedTakeNear(x + 2, y, 3), "a nearby loose spark can be captured");
    check(shedCount() == 0, "capture removes the falling mote");
    check(!shedTakeNear(x, y, 3), "an already captured spark cannot duplicate");

    /* It is the existing wire-end particle, including its useful collision:
       falling onto exposed copper must create current rather than merely stop. */
    g_world.setCell(x, y + 6, MAT_COPPER);
    g_world.setCell(x + 1, y + 6, MAT_COPPER);
    g_world.setLiveWindow(x - 16, y - 16, x + 16, y + 20);
    check(shedPlace(x, y), "another loose spark can be dropped above wire");
    bool energised = false;
    for (int frame = 0; frame < 60 && !energised; ++frame) {
        devTick(g_world);
        energised = sparkCount() > 0;
    }
    check(energised, "a placed mote starts a pulse when it lands on conductor");

    check(ITEMS[ITEM_SPARK].kind == ITEMK_SPARK, "Spark has its own use verb");
    check(ITEMS[ITEM_SPARK].maxStack > 1, "captured sparks stack");
    check(ITEMS[ITEM_SPARK].sprite == SPR_SPARK, "Spark uses bespoke art");
    int lit = 0;
    for (int i = 0; i < SPR_W * SPR_H; ++i) lit += g_sprite[SPR_SPARK][i] != 0;
    check(lit >= 12, "Spark art has a readable silhouette");

    if (failures) return 1;
    puts("placeable and capturable spark item passed");
    return 0;
}
