#include "item.h"
#include "materials.h"
#include "entity.h"
#include <stdio.h>

int main() {
    initMaterials();
    initItems();

    /* World substances stay terse. A material can opt in later if it becomes a
       special carried component, as Forge Core already does outside MAT_COUNT. */
    for (int i = 1; i < MAT_COUNT; ++i) {
        if (ITEMS[i].description && ITEMS[i].description[0]) {
            fprintf(stderr, "ordinary material unexpectedly has description: %s\n", ITEMS[i].name);
            return 1;
        }
    }

    for (int i = MAT_COUNT; i < ITEM_COUNT; ++i) {
        if (!ITEMS[i].maxStack) continue;
        if (!ITEMS[i].description || !ITEMS[i].description[0]) {
            fprintf(stderr, "usable item lacks description: %s (%d)\n", ITEMS[i].name, i);
            return 2;
        }
    }

    puts("item description coverage passed");
    return 0;
}
