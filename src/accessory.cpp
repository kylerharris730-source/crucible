#include "accessory.h"
#include "entity.h"
#include "multiplayer.h"

void accessoryReset() {
    for (int slot = 0; slot < MAX_PLAYERS; ++slot) g_playerSessions[slot].garlicCooldown = 0;
}

int accessoryShotDelay(const Inventory& inv, int baseDelay) {
    if (!inv.hasEquipped(ITEM_OVERLOAD_ACCESSORY)) return baseDelay;
    return imax(1, (baseDelay * 3 + 3) / 4);
}

bool accessoryTwinShot(const Inventory& inv) {
    return inv.hasEquipped(ITEM_TWIN_ACCESSORY);
}

void accessoryTick(const Player& player, const Inventory& inv) {
    accessoryTickFor(0, player, inv);
}

void accessoryTickFor(int playerSlot, const Player& player, const Inventory& inv) {
    if (playerSlot < 0 || playerSlot >= MAX_PLAYERS) return;
    int& cooldown = g_playerSessions[playerSlot].garlicCooldown;
    if (!inv.hasEquipped(ITEM_GARLIC_ACCESSORY)) {
        cooldown = 0;
        return;
    }
    if (cooldown > 0) { --cooldown; return; }
    entDamageDisc((int)player.centreX(), (int)player.centreY(),
                  ACCESSORY_GARLIC_RADIUS, ACCESSORY_GARLIC_DAMAGE);
    cooldown = ACCESSORY_GARLIC_COOLDOWN;
}
