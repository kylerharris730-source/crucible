#include "item.h"
#include "materials.h"
#include "entity.h"
#include <stdio.h>

int main() {
    initMaterials();
    initItems();

    /* World substances stay terse. A material can opt in later if it becomes a
       special carried component, as Forge Core already does outside MAT_COUNT.

       SEEDS are the standing exception, and they earn it: a seed is a material
       you carry, plant, and have to be told the rules of -- where it will take
       root, what it grows into, what happens if you bury it. "Stone" needs no
       sentence because stone is self-evident; a seed that silently does
       nothing on the wrong ground is exactly the case a tooltip is for. */
    /* And the second exception, which is a LIST rather than a rule because
       there is no property that distinguishes them: a material earns a
       sentence when it is the input to a PROCESS a player cannot guess at.
       The fuel-to-coke chain is three steps, a temperature, a sealed vessel
       and a vented gas, and none of that is visible by looking at a lump of
       fuel. Naming them here rather than opening the rule keeps the next one
       a decision instead of a habit. */
    static const int DESCRIBED[] = { MAT_FUEL, MAT_COKE, MAT_COKE_GAS };
    for (int i = 1; i < MAT_COUNT; ++i) {
        if (g_matIsSeed[i]) continue;
        bool allowed = false;
        for (int k = 0; k < (int)(sizeof(DESCRIBED) / sizeof(DESCRIBED[0])); ++k)
            if (DESCRIBED[k] == i) allowed = true;
        if (allowed) continue;
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
