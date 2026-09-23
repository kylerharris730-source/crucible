#include "entity.h"
#include "item.h"
#include <stdio.h>
int main() {
    initMaterials(); initItems();
    /* 25, not the 50 everything else uses and not the 10 it started at:
       reported as "its filling my inventory" at 1 in 10. Bats die in two hits
       and are everywhere, so the feather still comes fastest of any charm. */
    if (ENT_DEFS[ENT_BAT].rareDrop != ITEM_EMBERWING_FEATHER ||
        ENT_DEFS[ENT_BAT].rareOneIn != 25) return 1;
    if (ENT_DEFS[ENT_EMBERWING].rareDrop != ITEM_SWIFT_CHARM) return 2;
    Inventory inv; inv.clear();
    inv.equip[EQ_TRINKET_A].item = ENT_DEFS[ENT_BAT].rareDrop;
    inv.equip[EQ_TRINKET_A].count = 1;
    if (inv.airJumps() != 1) return 3;
    inv.equip[EQ_TRINKET_B] = inv.equip[EQ_TRINKET_A];
    if (inv.airJumps() != 1) return 4;
    puts("PASS: bats drop double jump at 1 in 25; Swift Charm remains obtainable; duplicate effects do not stack");
    return 0;
}
