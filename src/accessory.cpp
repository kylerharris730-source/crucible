#include "accessory.h"
#include "entity.h"
#include "light.h"
#include "multiplayer.h"

void accessoryReset() {
    for (int slot = 0; slot < MAX_PLAYERS; ++slot) {
        g_playerSessions[slot].garlicCooldown = 0;
        g_playerSessions[slot].regenTimer     = 0;
    }
}

int accessoryShotDelay(const Inventory& inv, int baseDelay) {
    /* Two things shorten a delay and they do NOT compound. Whichever is kinder
       wins, and then that is the answer.

       This mattered the moment the trinket row went from two slots to four. The
       Overload takes a quarter off and the Chronometer takes a fifth; multiplied
       together that is 40% off from two trinkets, and with four slots there is
       nothing stopping a build wearing every cooldown item in the game. Fire
       rate is the one stat where compounding runs away fastest, because halving
       the delay doubles EVERY other number the weapon has at once. */
    int best = baseDelay;
    if (inv.hasEquipped(ITEM_OVERLOAD_ACCESSORY))
        best = imin(best, (baseDelay * 3 + 3) / 4);
    const int pct = inv.cooldownPct();
    if (pct > 0)
        best = imin(best, baseDelay - baseDelay * pct / 100);
    return imax(1, best);
}

int accessoryShotDamage(const Inventory& inv, int baseDamage) {
    const int pct = inv.damagePct();
    if (pct <= 0) return baseDamage;
    /* At least one more point than it did, so a percentage can never round away
       to nothing on the weak end of the ladder. The Bolt Caster does 4, and a
       25% charm that visibly did zero to the weapon you are handed at spawn
       would read as broken rather than as rounding. */
    return imax(baseDamage + 1, baseDamage + baseDamage * pct / 100);
}

float accessoryShotSpeed(const Inventory& inv, float baseSpeed) {
    const int pct = inv.shotSpeedPct();
    if (pct <= 0) return baseSpeed;
    return baseSpeed * (1.0f + (float)pct / 100.0f);
}

float accessoryPickupRadius(const Inventory& inv) {
    return PICKUP_BASE_RADIUS + (float)inv.pickupRadius();
}

void accessoryRegisterLights() {
    for (int slot = 0; slot < MAX_PLAYERS; ++slot) {
        const PlayerSession& session = g_playerSessions[slot];
        if (!session.connected || !session.body.alive) continue;
        const int glow = session.inventory.lightGlow();
        if (glow <= 0) continue;
        lightAddDynamic((int)session.body.centreX(), (int)session.body.centreY(),
                        (u8)imin(255, glow));
    }
}

bool accessoryTwinShot(const Inventory& inv) {
    return inv.hasEquipped(ITEM_TWIN_ACCESSORY);
}

void accessoryTick(const Player& player, const Inventory& inv) {
    accessoryTickFor(0, player, inv);
}

void accessoryTickFor(int playerSlot, const Player& player, const Inventory& inv) {
    if (playerSlot < 0 || playerSlot >= MAX_PLAYERS) return;

    /* --- regeneration -----------------------------------------------------
       Its own counter beside the garlic clock rather than sharing one. They are
       both "frames until this accessory acts" and it is tempting to run them off
       a single number, but the two are independent effects on independent items
       and a shared clock would mean putting one charm on silently reset the
       other's progress -- a coupling nothing on either tooltip admits to.

       The heal goes through Player::heal so it is clamped by exactly the rule
       food is clamped by, rather than by a second copy of the cap here. Reset to
       zero while the charm is off, so taking it away and putting it back is not
       a way to bank a point of health.

       Written to the SESSION's body rather than the `player` argument, which is
       const and is the same object in every existing caller. */
    Player& body = g_playerSessions[playerSlot].body;
    const int per = inv.regenPer();
    if (per > 0 && body.alive && body.hp < PLAYER_HP_MAX) {
        if (++g_playerSessions[playerSlot].regenTimer >= per) {
            g_playerSessions[playerSlot].regenTimer = 0;
            body.heal(1);
        }
    } else {
        g_playerSessions[playerSlot].regenTimer = 0;
    }

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
