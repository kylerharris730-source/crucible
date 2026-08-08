#include "accessory.h"
#include "entity.h"

static int g_garlicCool = 0;

void accessoryReset() { g_garlicCool = 0; }

int accessoryShotDelay(const Inventory& inv, int baseDelay) {
    if (!inv.hasEquipped(ITEM_OVERLOAD_ACCESSORY)) return baseDelay;
    return imax(1, (baseDelay * 3 + 3) / 4);
}

bool accessoryTwinShot(const Inventory& inv) {
    return inv.hasEquipped(ITEM_TWIN_ACCESSORY);
}

void accessoryTick(const Player& player, const Inventory& inv) {
    if (!inv.hasEquipped(ITEM_GARLIC_ACCESSORY)) {
        g_garlicCool = 0;
        return;
    }
    if (g_garlicCool > 0) { --g_garlicCool; return; }
    entDamageDisc((int)player.centreX(), (int)player.centreY(),
                  ACCESSORY_GARLIC_RADIUS, ACCESSORY_GARLIC_DAMAGE);
    g_garlicCool = ACCESSORY_GARLIC_COOLDOWN;
}
