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

    /* --- the post-Censer three ------------------------------------------
       Asked for: "lets make an armor that you can make post censer. one for
       each archetype."

       Four properties, and the last two are the ones that would quietly not be
       true if the sets shared their ids with the Ichor three. */
    struct PyreSet {
        const char* name;
        ItemId head, body, feet;
        int armour, heat;
    };
    const PyreSet pyre[3] = {
        { "Thurible",  ITEM_THURIBLE_CROWN, ITEM_THURIBLE_HARNESS,
                       ITEM_THURIBLE_GREAVES, 11, 70 },
        { "Ashen",     ITEM_ASHEN_HOOD,     ITEM_ASHEN_COAT,
                       ITEM_ASHEN_GREAVES,   10, 65 },
        { "Brimsteel", ITEM_BRIMSTEEL_HELM, ITEM_BRIMSTEEL_PLATE,
                       ITEM_BRIMSTEEL_GREAVES, 21, 90 }
    };
    for (int i = 0; i < 3; ++i) {
        inv.clear();
        wear(inv, EQ_HEAD, pyre[i].head);
        wear(inv, EQ_BODY, pyre[i].body);
        wear(inv, EQ_FEET, pyre[i].feet);
        if (inv.armour() != pyre[i].armour ||
            inv.tempResist().heat != pyre[i].heat) {
            fprintf(stderr, "%s armour/heat is wrong: %d / %d\n", pyre[i].name,
                    inv.armour(), inv.tempResist().heat);
            return 7;
        }
        /* Every piece is a Censer killed. Gated at the Assembly Table by the
           Pyre Core, the same way the Ichor three are gated by Ichor. */
        const ItemId piece[3] = { pyre[i].head, pyre[i].body, pyre[i].feet };
        for (int k = 0; k < 3; ++k)
            if (!recipeHas(piece[k], ITEM_PYRE_CORE, STATION_ASSEMBLY)) {
                fprintf(stderr, "%s is not gated by the Pyre Core\n",
                        ITEMS[piece[k]].name);
                return 8;
            }
    }

    /* Each set beats its own Ichor predecessor, and every one of these numbers
       has to come from the NEW set rather than the old: they are separate
       ArmourSets, so a bonus that still read the old id would report zero. */
    inv.clear();
    wear(inv, EQ_HEAD, ITEM_THURIBLE_CROWN);
    wear(inv, EQ_BODY, ITEM_THURIBLE_HARNESS);
    if (inv.combatDroneSlots() < 2) {
        fprintf(stderr, "two Thurible pieces do not open a second drone bay\n");
        return 9;
    }
    wear(inv, EQ_FEET, ITEM_THURIBLE_GREAVES);
    if (inv.droneDamagePct() != 110) {
        fprintf(stderr, "Thurible three-piece drone damage is wrong\n");
        return 10;
    }

    inv.clear();
    wear(inv, EQ_HEAD, ITEM_ASHEN_HOOD);
    wear(inv, EQ_BODY, ITEM_ASHEN_COAT);
    wear(inv, EQ_FEET, ITEM_ASHEN_GREAVES);
    if (inv.rangedDamagePct() != 45 || inv.rangedRangePct() != 60) {
        fprintf(stderr, "Ashen three-piece bonus is wrong\n");
        return 11;
    }

    inv.clear();
    wear(inv, EQ_HEAD, ITEM_BRIMSTEEL_HELM);
    wear(inv, EQ_BODY, ITEM_BRIMSTEEL_PLATE);
    wear(inv, EQ_FEET, ITEM_BRIMSTEEL_GREAVES);
    if (inv.meleeDamagePct() != 45 || inv.meleeReachPct() != 40 ||
        inv.meleeSpeedPct() != 30) {
        fprintf(stderr, "Brimsteel three-piece bonus is wrong\n");
        return 12;
    }

    /* And the tiers do not blend. Two Vanguard pieces and one Brimsteel is
       three pieces of melee armour and pays the two-piece bonus of neither
       three-piece set -- which is what stops the new tier from being something
       you reach two thirds of the way into. */
    inv.clear();
    wear(inv, EQ_HEAD, ITEM_VANGUARD_HELM);
    wear(inv, EQ_BODY, ITEM_VANGUARD_PLATE);
    wear(inv, EQ_FEET, ITEM_BRIMSTEEL_GREAVES);
    if (inv.meleeReachPct() != 0 || inv.meleeDamagePct() != 20) {
        fprintf(stderr, "mixing armour tiers pays a three-piece bonus\n");
        return 13;
    }

    puts("iron baseline, Ichor class armour and the post-Censer three passed");
    return 0;
}
