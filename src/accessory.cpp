#include "accessory.h"
#include "world.h"
#include "materials.h"
#include "entity.h"
#include "light.h"
#include "multiplayer.h"

void accessoryReset() {
    for (int slot = 0; slot < MAX_PLAYERS; ++slot) {
        g_playerSessions[slot].garlicCooldown = 0;
        g_playerSessions[slot].regenTimer     = 0;
        g_playerSessions[slot].momentumFrames = 0;
        g_playerSessions[slot].idleFrames     = 0;
        g_playerSessions[slot].sprintFrames   = 0;
        g_playerSessions[slot].sprintDir      = 0;
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

/* --- the three clocks --------------------------------------------------------
   How long each effect takes to reach full, and what full is worth. Every one
   of these is a RAMP rather than a switch, which is the whole reason they are
   worth having as separate charms: a flat bonus for moving would just be a
   bonus, and what makes these read is watching the number climb while you
   commit to doing the thing. */
static const int   MOMENTUM_FULL_FRAMES = 150;  /* two and a half seconds */
static const int   MOMENTUM_MAX_PCT     = 40;
static const int   SPRINT_FULL_FRAMES   = 180;  /* three seconds one way */
static const int   SPRINT_MAX_PCT       = 35;
static const int   LOADER_IDLE_FRAMES   = 120;  /* two seconds of held fire */

/* The trail the Cinderling Ash leaves. Sparse -- one cell every few frames --
   because a solid line of fire behind a running player is a wall the player
   themselves cannot get back through, and this charm is meant to be a hazard
   you are carrying rather than a door you are closing. */
static const int   ASH_TRAIL_EVERY = 7;

int accessoryMomentumPct(int playerSlot, const Inventory& inv) {
    if (playerSlot < 0 || playerSlot >= MAX_PLAYERS) return 0;
    if (!inv.hasEquipped(ITEM_THRESHING_SPURS)) return 0;
    const int frames = g_playerSessions[playerSlot].momentumFrames;
    if (frames >= MOMENTUM_FULL_FRAMES) return MOMENTUM_MAX_PCT;
    return MOMENTUM_MAX_PCT * frames / MOMENTUM_FULL_FRAMES;
}

int accessorySprintPct(int playerSlot, const Inventory& inv) {
    if (playerSlot < 0 || playerSlot >= MAX_PLAYERS) return 0;
    if (!inv.hasEquipped(ITEM_ASHHOUND_COLLAR)) return 0;
    const int frames = g_playerSessions[playerSlot].sprintFrames;
    if (frames >= SPRINT_FULL_FRAMES) return SPRINT_MAX_PCT;
    return SPRINT_MAX_PCT * frames / SPRINT_FULL_FRAMES;
}

bool accessoryBurstReady(int playerSlot, const Inventory& inv) {
    if (playerSlot < 0 || playerSlot >= MAX_PLAYERS) return false;
    if (!inv.hasEquipped(ITEM_CULVERIN_LOADER)) return false;
    return g_playerSessions[playerSlot].idleFrames >= LOADER_IDLE_FRAMES;
}

void accessoryNoteShot(int playerSlot) {
    if (playerSlot < 0 || playerSlot >= MAX_PLAYERS) return;
    g_playerSessions[playerSlot].idleFrames = 0;
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

/* --- the trail -------------------------------------------------------------
   Fire behind a running player, and every constraint on it is about not
   handing the player a weapon they cannot control.

   BEHIND, not underneath: the cell is placed at the heel, on the side the
   player came from, so running forward never puts fire where you are about to
   be. Sparse, one cell every seventh frame, so it is a dotted line rather than
   a wall -- a solid one would be a door the player has closed behind
   themselves in a corridor they may need to come back down.

   And only into EMPTY air with something solid under it. Fire in mid-air falls
   and spreads unpredictably; fire on a floor stays where it was put and burns
   out, which is the version a player can plan around. */
void accessoryAshTrail(int playerSlot, const Player& player,
                       const Inventory& inv, World& world) {
    if (playerSlot < 0 || playerSlot >= MAX_PLAYERS) return;
    if (!inv.hasEquipped(ITEM_CINDERLING_ASH)) return;
    if (!player.onGround) return;
    const float vx = player.vx;
    if (vx > -0.25f && vx < 0.25f) return;
    if ((g_playerSessions[playerSlot].sprintFrames % ASH_TRAIL_EVERY) != 0) return;

    const int behind = vx > 0.0f ? player.left() - 1 : player.right() + 1;
    const int y = player.bottom();
    if (behind <= PLAY_X0 || behind >= PLAY_X1 || y <= PLAY_Y0 || y >= PLAY_Y1 - 1)
        return;
    if (world.at(behind, y).mat != MAT_EMPTY) return;
    if (world.at(behind, y + 1).mat == MAT_EMPTY) return;
    world.setCell(behind, y, MAT_FIRE);
    world.dirtyPoint(behind, y);
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

    /* --- the three clocks -------------------------------------------------
       Counted for everybody rather than only while the charm is worn, and that
       is deliberate: a counter that only runs while equipped would make taking
       a charm off and putting it back a way to keep progress that the tooltip
       never promised, and the cost of counting is four integers. */
    PlayerSession& session = g_playerSessions[playerSlot];
    const bool moving = body.vx > 0.08f || body.vx < -0.08f;
    if (moving) ++session.momentumFrames; else session.momentumFrames = 0;
    ++session.idleFrames;

    /* ONE WAY. Turning round resets it, which is what makes this a sprint and
       not a reward for jiggling on the spot -- and it is the difference between
       the Collar and the Swift Charm, which simply makes you faster. */
    const int dir = !moving ? 0 : (body.vx > 0.0f ? 1 : -1);
    if (dir == 0 || dir != session.sprintDir) {
        session.sprintFrames = 0;
        session.sprintDir = dir;
    } else {
        ++session.sprintFrames;
    }

    int& cooldown = g_playerSessions[playerSlot].garlicCooldown;
    if (!inv.hasEquipped(ITEM_GARLIC_ACCESSORY)) {
        cooldown = 0;
        return;
    }
    if (cooldown > 0) { --cooldown; return; }
    /* Sparing tame creatures, like every other passive: the garlic field fires
       on its own clock whether or not you meant it to, and a charm that quietly
       killed your own bees while you stood in your apiary is the same complaint
       the drones got. */
    entDamageDisc((int)player.centreX(), (int)player.centreY(),
                  ACCESSORY_GARLIC_RADIUS, ACCESSORY_GARLIC_DAMAGE, true);
    cooldown = ACCESSORY_GARLIC_COOLDOWN;
}
