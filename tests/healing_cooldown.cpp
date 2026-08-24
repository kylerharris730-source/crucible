#include "materials.h"
#include "item.h"
#include "multiplayer.h"
#include <stdio.h>

int main() {
    initMaterials(); initItems(); playerSessionsReset();
    PlayerSession& s = g_playerSessions[0];
    s.body.hp = 50;
    s.inventory.add(ITEM_BREAD, 2);

    if (!playerConsumeHealing(s, ITEM_BREAD) || s.body.hp != 80 ||
        s.inventory.countOf(ITEM_BREAD) != 1 ||
        s.healCooldown != HEAL_COOLDOWN_FRAMES) {
        fprintf(stderr, "first healing item did not heal and start cooldown\n");
        return 1;
    }
    if (playerConsumeHealing(s, ITEM_BREAD) || s.inventory.countOf(ITEM_BREAD) != 1) {
        fprintf(stderr, "healing item bypassed shared cooldown\n");
        return 2;
    }

    for (int i = 0; i < HEAL_COOLDOWN_FRAMES - 1; ++i) playerHealingCooldownTick(s);
    if (s.healCooldown != 1 || playerConsumeHealing(s, ITEM_BREAD)) {
        fprintf(stderr, "healing unlocked one frame early\n");
        return 3;
    }
    playerHealingCooldownTick(s);
    s.body.hp = 50;
    if (!playerConsumeHealing(s, ITEM_BREAD) || s.body.hp != 80 ||
        s.inventory.countOf(ITEM_BREAD) != 0) {
        fprintf(stderr, "healing did not unlock when cooldown ended\n");
        return 4;
    }

    /* Regeneration and other non-consumable healing use Player::heal directly;
       they must neither start nor reset potion sickness. */
    s.healCooldown = 100;
    s.body.hp = 70;
    s.body.heal(1);
    if (s.body.hp != 71 || s.healCooldown != 100) {
        fprintf(stderr, "passive healing interacted with consumable cooldown\n");
        return 5;
    }

    puts("shared healing cooldown passed");
    return 0;
}
