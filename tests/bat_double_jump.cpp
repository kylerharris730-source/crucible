#include "entity.h"
#include "item.h"
#include <stdio.h>
int main() {
    initMaterials(); initItems();
    if (ENT_DEFS[ENT_BAT].rareDrop != ITEM_EMBERWING_FEATHER ||
        ENT_DEFS[ENT_BAT].rareOneIn != 10) return 1;
    if (ENT_DEFS[ENT_EMBERWING].rareDrop != ITEM_SWIFT_CHARM) return 2;
    Inventory inv; inv.clear();
    inv.equip[EQ_TRINKET_A].item = ENT_DEFS[ENT_BAT].rareDrop;
    inv.equip[EQ_TRINKET_A].count = 1;
    if (inv.airJumps() != 1) return 3;
    inv.equip[EQ_TRINKET_B] = inv.equip[EQ_TRINKET_A];
    if (inv.airJumps() != 1) return 4;
    puts("PASS: bats drop double jump at 1 in 10; Swift Charm remains obtainable; duplicate effects do not stack");
    return 0;
}
