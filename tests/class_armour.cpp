/* The armour ladder has one baseline and three Ichor specializations. This
   keeps the balance promises at the Inventory boundary: mixed sets do not
   combine, two- and three-piece thresholds differ, and every specialized
   recipe is genuinely gated by Ichor rather than merely placed later in the
   crafting list. */

#include "materials.h"
#include "item.h"
#include "craft.h"
#include <stdio.h>

static void wear(Inventory& inv, int slot, ItemId item) {
    inv.equip[slot].item = item;
    inv.equip[slot].count = 1;
}

static bool recipeHas(ItemId out, ItemId ingredient, int station) {
    for (int r = 0; r < N_RECIPES; ++r) {
        if (RECIPES[r].out != out) continue;
        if (station >= 0 && RECIPES[r].station != station) return false;
        for (int i = 0; i < CRAFT_MAX_IN; ++i)
            if (RECIPES[r].in[i].item == ingredient && RECIPES[r].in[i].count > 0)
                return true;
        return false;
    }
    return false;
}

int main() {
    initMaterials();
    initItems();
    Inventory inv;

    inv.clear();
    wear(inv, EQ_HEAD, ITEM_IRON_HELMET);
    wear(inv, EQ_BODY, ITEM_IRON_CUIRASS);
    wear(inv, EQ_FEET, ITEM_IRON_GREAVES);
    if (inv.armour() != 4 || inv.tempResist().heat != 10 ||
        inv.tempResist().cold != 10 ||
        inv.armourSetPieces(ARMOUR_SET_RANGED) != 0 ||
        !recipeHas(ITEM_IRON_HELMET, (ItemId)MAT_IRON, STATION_ANVIL)) {
        fprintf(stderr, "basic Iron Armour is not the weak no-set baseline\n");
        return 1;
    }

    inv.clear();
    wear(inv, EQ_HEAD, ITEM_RANGER_VISOR);
    wear(inv, EQ_BODY, ITEM_RANGER_COAT);
    if (inv.rangedDamagePct() != 10 || inv.rangedRangePct() != 15) {
        fprintf(stderr, "Ranger two-piece bonus is wrong\n");
        return 2;
    }
    wear(inv, EQ_FEET, ITEM_RANGER_GREAVES);
    if (inv.armour() != 6 || inv.rangedDamagePct() != 20 ||
        inv.rangedRangePct() != 30) {
        fprintf(stderr, "Ranger three-piece bonus is wrong\n");
        return 3;
    }

    inv.clear();
    wear(inv, EQ_HEAD, ITEM_VANGUARD_HELM);
    wear(inv, EQ_BODY, ITEM_VANGUARD_PLATE);
    if (inv.meleeDamagePct() != 20 || inv.meleeReachPct() != 0 ||
        inv.meleeSpeedPct() != 0) {
        fprintf(stderr, "Vanguard two-piece bonus is wrong\n");
        return 4;
    }
    wear(inv, EQ_FEET, ITEM_VANGUARD_GREAVES);
    if (inv.armour() != 14 || inv.tempResist().heat != 60 ||
        inv.tempResist().cold != 60 || inv.meleeDamagePct() != 20 ||
        inv.meleeReachPct() != 20 || inv.meleeSpeedPct() != 15) {
        fprintf(stderr, "Vanguard three-piece stats are wrong\n");
        return 5;
    }

    const ItemId ichorSets[] = {
        ITEM_DRONE_VISOR, ITEM_DRONE_HARNESS, ITEM_DRONE_GREAVES,
        ITEM_RANGER_VISOR, ITEM_RANGER_COAT, ITEM_RANGER_GREAVES,
        ITEM_VANGUARD_HELM, ITEM_VANGUARD_PLATE, ITEM_VANGUARD_GREAVES
    };
    for (int i = 0; i < (int)(sizeof(ichorSets) / sizeof(ichorSets[0])); ++i)
        if (!recipeHas(ichorSets[i], ITEM_ICHOR, STATION_ASSEMBLY)) {
            fprintf(stderr, "%s is not gated by Ichor at the Assembly Table\n",
                    ITEMS[ichorSets[i]].name);
            return 6;
        }

    puts("iron baseline and Ichor class armour bonuses passed");
    return 0;
}
