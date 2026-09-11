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
/* The Loader, in two stages -- see accessoryBurstBolts. 36 frames is a beat:
   about the gap you leave stepping sideways between shots, which is the whole
   point of putting the first stage there. 90 is a deliberate pause. */
static const int   LOADER_ONE_FRAMES    = 36;
static const int   LOADER_TWO_FRAMES    = 90;

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

int accessoryBurstBolts(int playerSlot, const Inventory& inv) {
    if (playerSlot < 0 || playerSlot >= MAX_PLAYERS) return 0;
    if (!inv.hasEquipped(ITEM_CULVERIN_LOADER)) return 0;
    const int held = g_playerSessions[playerSlot].idleFrames;
    if (held >= LOADER_TWO_FRAMES) return 2;
    if (held >= LOADER_ONE_FRAMES) return 1;
    return 0;
}

bool accessoryBurstReady(int playerSlot, const Inventory& inv) {
    return accessoryBurstBolts(playerSlot, inv) > 0;
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
   Embers behind a running player, and every constraint on it is about not
   handing the player a weapon they cannot control.

   BEHIND, not underneath: the cell is placed at the heel, on the side the
   player came from, so running forward never puts fire where you are about to
   be. Sparse, one cell every seventh frame, so it is a dotted line rather than
   a wall -- a solid one would be a door the player has closed behind
   themselves in a corridor they may need to come back down.

   And only into EMPTY air with something solid under it. Fire in mid-air falls
   and spreads unpredictably; something burning on a floor stays where it was
   put and burns out, which is the version a player can plan around.

   --- what it drops, and why it is not fire any more ---
   Reported from play: it "rarely and inconsistently produces a small amount of
   fire". Both halves were true and they had different causes.

   The SMALL amount was MAT_FIRE, which is what a flame IS rather than what is
   burning: dropped on bare stone with nothing to consume, a cell of it is gone
   within a frame or two. Measured over ten seconds of running, the trail put
   down 37 cells and never had more than ONE alight at any moment -- a charm
   whose entire visible output is a single flickering pixel somewhere behind
   you.

   MAT_EMBER is burning coal: static, 185 C, mean life about four seconds, and
   it lights the ground it sits on. So the same 37 placements become a line you
   can see, walk enemies into, and set a wooden floor alight with -- which is
   the hazard the charm was always described as. It is a real hazard to the
   things you own, too, and the tooltip says so.

   The INCONSISTENCY was the clock. The spacing was taken off `sprintFrames`,
   which belongs to the Ashhound Collar and resets every time you change
   direction -- so a player pivoting during a fight reset it constantly, hit
   the "every seventh frame" test on the reset itself, and dropped embers at
   roughly twice the rate of a player running in a straight line. Its own
   counter, incremented once per eligible frame, makes the rate the rate. */
/* How tall a drop is. The SPACING is not a number any more -- see below. */
static const int   ASH_TRAIL_HEIGHT = 2;
/* --- how far the trail looks for a floor -----------------------------------
   Reported from play: "it doesn't work on slopes up". It worked on flat ground
   and nowhere else, and the geometry is the reason.

   The character is eleven cells wide and rests on the HIGHEST ground under its
   box, which on a slope is the leading edge. So climbing a one-in-one, the
   heel is a full body-width of slope below the feet -- measured at twelve
   cells -- and a search that only looked a step-height down found nothing but
   air. Down is therefore two body-widths of slope plus the step the character
   can climb, which covers everything up to about a two-in-one face. Steeper
   than that you are climbing a wall rather than running along ground, and the
   trail stops -- which is the right answer rather than a limitation.

   The consequence worth knowing: run past a narrow shaft and the embers for
   those columns land at the BOTTOM of it rather than hanging in the air over
   the hole. That is the honest reading of "the ground behind you", and it is
   what falling embers would have done anyway.

   UP stays short. The ground behind can only be a little higher than your feet
   -- one step you walked down -- and a long upward search on a cliff edge
   would find the ledge above your head and lay the trail on the roof. */
static const int   ASH_FLOOR_DOWN = 2 * PLAYER_W + PLAYER_STEP_UP;
static const int   ASH_FLOOR_UP   = PLAYER_STEP_UP;

static void ashColumn(World& world, int x, int y);
static int  ashFloorY(const World& world, int x, int footY);

void accessoryAshTrail(int playerSlot, const Player& player,
                       const Inventory& inv, World& world) {
    if (playerSlot < 0 || playerSlot >= MAX_PLAYERS) return;
    if (!inv.hasEquipped(ITEM_CINDERLING_ASH)) return;
    const float vx = player.vx;
    if (vx > -0.25f && vx < 0.25f) return;
    /* --- off the ground, briefly -------------------------------------------
       Player::runningOnGround rather than `onGround`, which was the rule and
       which is wrong for running: a slope is a staircase, and skipping down
       one leaves the character airborne 40% of frames. An onGround test turned
       a run downhill into a dotted line for a reason the player cannot see.

       It is the same grace the walk cycle and the crouch already use, asked
       through the same function, so there is one answer to "is this character
       running" rather than three thresholds that can drift apart. */
    if (!player.runningOnGround()) return;

    /* --- laid by the CELL, not by the clock --------------------------------
       Every version of this before now dropped one cell every N frames, and
       that is the wrong axis for a trail. A player moves between one and three
       cells a frame depending on boots, so a per-frame rate puts the drops
       four cells apart at a sprint and one cell apart at a walk -- the faster
       you run, the thinner the thing you are laying down. Measured at a
       drop every three frames it came out as scattered dots four cells apart,
       which is the complaint exactly: "not a stream".

       So the trail covers the GROUND CROSSED. Each frame it fills the cells
       between where the heel was and where it is now, which makes the trail
       continuous at any speed and needs no memory of the last position: the
       distance covered is |vx|, and that is a number this function already
       has. */
    const int foot = player.bottom();
    if (foot <= PLAY_Y0 || foot >= PLAY_Y1 - 1) return;
    const int heel = vx > 0.0f ? player.left() - 1 : player.right() + 1;
    const float speed = vx > 0.0f ? vx : -vx;
    int span = (int)(speed + 0.999f);
    if (span < 1) span = 1;

    for (int i = 0; i < span; ++i) {
        const int behind = vx > 0.0f ? heel - i : heel + i;
        if (behind <= PLAY_X0 || behind >= PLAY_X1) continue;
        const int y = ashFloorY(world, behind, foot);
        if (y < 0) continue;
        ashColumn(world, behind, y);
    }
}

/* --- finding the floor -----------------------------------------------------
   Where the trail lands in one column: the first cell of open air with
   something solid under it, searched OUTWARD from the height the player's feet
   are at.

   Reported from play: "it doesn't work on slopes up". It did not work on
   slopes at all, and the numbers are worth keeping because they say why. Laid
   on the flat it put down 1.50 embers per cell of ground; up a gentle 1-in-8
   it managed 0.02, and up anything steeper than that, or at any real climb,
   exactly zero. Downhill was quietly broken too, at 0.28.

   The old version asked for the cell at foot height to be empty AND the one
   directly beneath it to be solid, which is only true on flat ground.
   Climbing, the character's feet are already above the ground behind them, so
   the cell under the heel is open air and the test failed. Descending, the
   heel is over ground that rises behind it, so the cell at foot height is
   inside rock and the test failed the other way. A charm for running was
   checking for a floor that only exists while you are standing still on the
   level.

   Searched nearest-first rather than top-down, so a ledge above your head or a
   pit below does not steal the drop from the step you actually took, and
   bounded by PLAYER_STEP_UP because that is how far the character can climb
   without jumping -- the ground it can reach is exactly the ground it can have
   just walked over. */
static int ashFloorY(const World& world, int x, int footY) {
    for (int d = 0; d <= ASH_FLOOR_DOWN; ++d) {
        for (int s = 0; s < 2; ++s) {
            const int y = s ? footY + d : footY - d;
            if (d == 0 && s) continue;             /* footY once, not twice */
            if (s == 0 && d > ASH_FLOOR_UP) continue;
            if (y <= PLAY_Y0 || y >= PLAY_Y1 - 1) continue;
            if (world.at(x, y).mat != MAT_EMPTY) continue;
            /* Something to rest ON. Gas is not a floor -- an ember laid on a
               cloud of steam falls through it and lands somewhere nobody
               chose, which is the thing the whole function is avoiding.

               And not the TRAIL either. Resting on its own output is how a
               dotted line becomes a tower: the search finds the top of the
               ember it laid last frame, calls that a floor, and stacks
               another one on it. Measured at four-cell columns before this
               line existed, on flat ground, where the trail is supposed to be
               two. */
            const u8 under = world.at(x, y + 1).mat;
            if (under == MAT_EMPTY || MATS[under].kind == KIND_GAS) continue;
            if (under == MAT_CINDERLING_EMBER || under == MAT_WOOD_EMBER || under == MAT_EMBER ||
                under == MAT_FIRE) continue;
            return y;
        }
    }
    return -1;
}

/* One column of the trail: the floor cell, and a second above it on every
   other cell of ground.

   The top row is deliberately dashed rather than solid. Two unbroken rows of
   burning wood is a wall, and the rule this whole function is built around is
   that the trail must stay something you can get back through -- see the note
   above. Dashed on the CELL's parity rather than on a frame counter so the
   pattern is a property of the ground and does not crawl as you watch it. */
static void ashColumn(World& world, int x, int y) {
    /* --- what it drops -----------------------------------------------------
       Its own Cinderling Ember now: FuelFire's thermal output with twice the
       wood ember's flame-emission chance. Half wood's average lifetime so a
       running player leaves a temporary hazard, not a permanent furnace.

       Two cells tall, bottom up, and the second one is allowed to fail: at a
       wall or under a low ceiling the trail simply gets shorter rather than
       refusing to appear. */
    const int tall = (x & 1) ? 1 : ASH_TRAIL_HEIGHT;
    for (int k = 0; k < tall; ++k) {
        const int cy = y - k;
        if (cy <= PLAY_Y0) break;
        if (world.at(x, cy).mat != MAT_EMPTY) break;
        world.setCell(x, cy, MAT_CINDERLING_EMBER);
        world.dirtyPoint(x, cy);
    }
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
