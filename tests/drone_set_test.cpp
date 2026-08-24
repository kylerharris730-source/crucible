#include "materials.h"
#include "item.h"
#include "drone.h"
#include "world.h"
#include <stdio.h>

static void wear(Inventory& inv, int slot, ItemId item) {
    inv.equip[slot].item = item;
    inv.equip[slot].count = 1;
}

int main() {
    initMaterials();
    initItems();

    Inventory inv;
    inv.clear();
    if (inv.combatDroneSlots() != 1 || !inv.droneBayUnlocked(EQ_DRONE_A) ||
        inv.droneBayUnlocked(EQ_DRONE_B) || inv.droneBayUnlocked(EQ_DRONE_C)) {
        fprintf(stderr, "bare player did not start with exactly one combat-drone bay\n");
        return 1;
    }

    wear(inv, EQ_HEAD, ITEM_DRONE_VISOR);
    wear(inv, EQ_BODY, ITEM_DRONE_HARNESS);
    if (inv.armourSetPieces(ARMOUR_SET_DRONE) != 2 ||
        inv.combatDroneSlots() != 2 || !inv.droneBayUnlocked(EQ_DRONE_B) ||
        inv.droneBayUnlocked(EQ_DRONE_C) || inv.droneDamagePct() != 0) {
        fprintf(stderr, "Drone Armour two-piece bonus is wrong\n");
        return 2;
    }

    wear(inv, EQ_FEET, ITEM_DRONE_GREAVES);
    if (inv.combatDroneSlots() != 2 || inv.droneDamagePct() != 50) {
        fprintf(stderr, "Drone Armour three-piece bonus is wrong\n");
        return 3;
    }

    wear(inv, EQ_TRINKET_A, ITEM_DRONE_BEACON);
    wear(inv, EQ_TRINKET_B, ITEM_DRONE_BEACON);
    if (inv.combatDroneSlots() != 3 || !inv.droneBayUnlocked(EQ_DRONE_C)) {
        fprintf(stderr, "Drone Beacon did not add exactly one non-stacking bay\n");
        return 4;
    }

    inv.clear();
    wear(inv, EQ_TRINKET_A, ITEM_CARAPACE_CHARM);
    wear(inv, EQ_TRINKET_B, ITEM_CARAPACE_CHARM);
    if (inv.armour() != ITEMS[ITEM_CARAPACE_CHARM].armour) {
        fprintf(stderr, "duplicate accessory armour stacked\n");
        return 5;
    }

    if (!equipFits(ITEM_ATTACK_DRONE, EQ_DRONE_C)) {
        fprintf(stderr, "general drone chassis does not structurally fit Drone C\n");
        return 6;
    }

    /* A locked bay may retain a chassis for save compatibility, but it must be
       inert until capacity returns. */
    inv.clear();
    wear(inv, EQ_DRONE_B, ITEM_ATTACK_DRONE);
    wear(inv, EQ_DRONE_C, ITEM_ATTACK_DRONE);
    Player player; player.reset(300.0f, 300.0f);
    g_world.reset(); droneReset(); droneTick(g_world, player, inv);
    if (droneCount() != 0) {
        fprintf(stderr, "locked stored drone became active\n");
        return 7;
    }
    wear(inv, EQ_HEAD, ITEM_DRONE_VISOR);
    wear(inv, EQ_BODY, ITEM_DRONE_HARNESS);
    droneTick(g_world, player, inv);
    if (droneCount() != 1) {
        fprintf(stderr, "two-piece bay did not activate its stored drone\n");
        return 8;
    }
    wear(inv, EQ_TRINKET_A, ITEM_DRONE_BEACON);
    droneTick(g_world, player, inv);
    if (droneCount() != 2) {
        fprintf(stderr, "Beacon bay did not activate its stored drone\n");
        return 9;
    }

    puts("drone set and accessory rules passed");
    return 0;
}
