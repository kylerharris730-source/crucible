#include "entity.h"
#include "multiplayer.h"
#include "sprite.h"
#include "light.h"
#include "projectile.h"
#include "worldgen.h"
#include "accessory.h"
#include "navigate.h"
#include <string.h>
#include <math.h>

Entity g_entities[MAX_ENTITIES];
Pickup g_pickups[MAX_PICKUPS];

/* The first boss opens the first geological seal everywhere, not merely at the
   arena. A cleared local hole would turn progression into remembering where a
   particular fight happened; defeating her should make layer two part of the
   world from that point on. The first band wobbles by under 60 cells, so this
   generous local-stone window clears it without touching the layer-two seal. */
static void unlockLayerTwo(World& w) {
    for (int x = PLAY_X0; x <= PLAY_X1; ++x) {
        const int mid = g_stoneY[x] + LAYER1_DEPTH;
        for (int y = imax(PLAY_Y0, mid - 100); y <= imin(PLAY_Y1, mid + 100); ++y) {
            if (w.at(x, y).mat == MAT_STRATUM) w.setCell(x, y, MAT_EMPTY);
            if ((w.bgAt(x, y) & BG_MAT_MASK) == MAT_STRATUM) w.clearBg(x, y);
        }
    }
}

/* Gravity and terminal velocity, matching player.cpp's own figures rather than
   being tuned separately. Creatures and the character fall through the same
   world, and two different gravities in one game is the kind of difference
   nobody can name but everybody can feel -- a creature that dropped off a ledge
   at a visibly different rate would read as being on ice. They are copied
   rather than shared because player.cpp keeps them private and the alternative
   is publishing the character's whole movement model to get at two numbers; if
   a third mover ever appears, that is the moment to hoist them into world.h. */
static const float ENT_GRAVITY  = 0.18f;
static const float ENT_MAX_FALL = 6.0f;

const EntityDef ENT_DEFS[ENT_COUNT] = {
    /* name       w   h  hp dmg  cd   speed accel  fly layers night | shotEvery dmg spd standOff boss | drop min max charm 1-in sprite egg | eggItem indestructible */
    { "none",      0,  0,  0,   0,   0, 0.00f, 0.00f, false, 0,  false,   0, 0, 0.0f, 0.0f, false, ITEM_NONE,        0, 0, ITEM_NONE,            0, SPR_NONE,  0x000000, ITEM_NONE,          false },

    /* --- rock mite ---------------------------------------------------------
       The one that makes the first twenty minutes treacherous. Slow enough to
       outrun, tough enough that punching it is a bad idea, and it CHEWS ROCK --
       so a wall is a delay rather than a solution and sealing yourself in is
       not a strategy. That last property is the whole reason it exists: it is
       the cheapest possible way to make the world's solidity negotiable.

       Drops chitin, which is what calls the layer's boss -- so the commonest
       creature in layer 1 is also the one you farm to pick a fight with its
       matriarch. That is the Terraria shape: the summon is assembled out of
       what the place is already made of, so deciding to fight the boss is a
       decision you make gradually while doing something else. */
    { "Rock Mite",12,  9, 18,   6,  36, 0.34f, 0.05f, false, 1,  true,    0, 0, 0.0f, 0.0f, false, (ItemId)MAT_CHITIN, 1, 2, ITEM_CARAPACE_CHARM, 50, SPR_MITE,  0x8E7758, ITEM_EGG_MITE,      false },

    /* --- cinder moth -------------------------------------------------------
       Navigates to the hottest cell it can sense, which means it navigates to
       whatever you are smelting with. Teaching that archetype in layer 1, while
       the furnace is a small coal fire and losing it costs minutes rather than
       an hour, is the entire point of putting it this early.

       Fragile and fast: it should die to one good shot and be genuinely
       annoying to hit with a bad one.

       Drops COAL, not the glass it used to. Glass was the clever answer -- wings
       fused by the heat it chases -- and it was justified on supply grounds,
       since glass gates the Chemistry Bench and the Assembly Table and the only
       other source is a two-cell beach on one lake. It was still the wrong
       drop, for a reason no supply argument reaches: glass is a BUILDING
       BLOCK, and the commonest flying creature in the layer handing you stacks
       of one turns a fight into inventory management. What you want off a fast
       nuisance is something you spend, not something you store.

       Coal is what a heat-chaser should be carrying anyway, and it is consumed
       rather than stockpiled. See the note in PROGRESSION about what this costs:
       glass is back to being gated on that one beach, and the honest fix is for
       sand to generate somewhere underground too. */
    { "Cinder Moth",9,  7, 10,   4,  30, 0.52f, 0.09f, true,  1,  true,    0, 0, 0.0f, 0.0f, false, (ItemId)MAT_COAL,   1, 2, ITEM_MOTH_LANTERN,   50, SPR_MOTH,  0xE0561C, ITEM_EGG_MOTH,      false },

    /* --- drip slime --------------------------------------------------------
       The corroder, and the slowest thing in the game: it is not a chase, it is
       something you have to deal with or route around. What makes it dangerous
       is the trail -- see slimeTick -- because acid outlives the creature and
       the puddle it leaves in a corridor is still there on the way back.

       Introduces acid a whole layer above where acid pockets generate, so the
       material is familiar before the terrain is full of it. Drops it too,
       which is the only way to get any in layer 1. */
    { "Drip Slime",11,  8, 24,   5,  40, 0.20f, 0.04f, false, 1,  false,   0, 0, 0.0f, 0.0f, false, (ItemId)MAT_ACID,   1, 3, ITEM_SLIME_MAGNET,   50, SPR_SLIME, 0x6FA23C, ITEM_EGG_SLIME,     false },

    /* --- husk ---------------------------------------------------------------
       The zombie, and deliberately the dullest thing in the game: it walks at
       you, it does not stop, and it hits harder than anything else in the
       layer. No gimmick at all.

       That is the point. Terraria's zombie has perhaps ten lines of AI and is
       still worth fighting, because what makes it dangerous is not cleverness
       but COMMITMENT -- it is slow enough to walk away from and tough enough
       that you cannot casually kill it, so it turns a corridor into somewhere
       you have to decide about. Everything interesting about the encounter
       comes from the terrain it is standing in. */
    { "Husk",     11, 22, 46,  11,  34, 0.42f, 0.06f, false, 1,  true,    0, 0, 0.0f, 0.0f, false, (ItemId)MAT_CHITIN, 1, 3, ITEM_HUSK_HEART,     50, SPR_HUSK,  0x6E7A52, ITEM_EGG_HUSK,      false },

    /* --- bat ----------------------------------------------------------------
       Fast, and BAD AT STEERING. The overshoot is the entire creature.

       It is not achieved with speed -- a fast thing that tracks you perfectly
       is just an unavoidable thing. It comes from committing to a heading for a
       stretch of frames and only re-aiming occasionally (see aimHold), plus an
       acceleration far too low to correct a bad line. So it comes at you,
       misses, sails past, wheels around and comes again, and the way to fight
       it is to let it commit and then not be there.

       Fragile to match: two hits from the starting shot. A bat you had to chase
       AND could not kill would be a tax rather than an encounter. */
    { "Bat",       9,  7, 12,   7,  26, 1.35f, 0.055f, true, 1,  true,    0, 0, 0.0f, 0.0f, false, (ItemId)MAT_CHITIN, 1, 1, ITEM_SWIFT_CHARM,    50, SPR_BAT,   0x6A4C68, ITEM_EGG_BAT,       false },

    /* --- spitter ------------------------------------------------------------
       The one that makes standing still wrong. It holds its distance and shoots,
       so a corridor you were happily backing down becomes a place you have to
       either close or leave.

       standOff is what makes it a shooter rather than a bad melee creature: it
       actively backs away when you approach, so the answer is to commit to
       closing rather than to trade at range with something that outranges you.

       Slow shots, on purpose -- see shotSpeed. A projectile you can watch and
       step around is a threat you play against; one that arrives instantly is a
       threat you only read about afterwards in the health bar.

       shotSpeed went 1.7 -> 4.7 when shots started obeying gravity, and it had
       to: a lob's maximum reach is v*v/g regardless of how it is aimed, so at
       1.7 this creature could throw a glob SIXTEEN cells and it stands off at
       ninety. It was not inaccurate, it was physically incapable, and the
       symptom was a spitter that faced you, animated, and never fired.

       4.7 rather than the 4.2 that just barely reaches, for margin -- the
       player is rarely exactly level, and dy eats into the reachable range.
       Measured at the stand-off: 123 cells of maximum range, 21 frames in the
       air, and an arc peaking 9.8 cells above the muzzle. The last number is
       the one that had to be checked against a CEILING: a glob breaks nothing
       (power STR_NOTHING), so an arc taller than the room splatters on the
       roof. Ten cells clears any chamber down here. The high-angle solution to
       the same shot peaks at 51 cells and is exactly the mortar this rule
       rejects.

       The shot is faster than it was and therefore less time to react to --
       0.35s at the stand-off against the old 0.88s -- but it is now a visible
       ARC rather than a flat line, which is easier to read the landing point
       of, not harder. Whether that trade is right is a play-testing question
       and it is on the list. */
    { "Spitter",  10, 12, 22,   5,  30, 0.26f, 0.05f, false, 1,  false,  95, 9, 4.7f, 90.0f, false, (ItemId)MAT_CHITIN, 1, 2, ITEM_SPITTER_BRACER, 50, SPR_SPITTER, 0x8A5A3A, ITEM_EGG_SPITTER, false },

    /* --- the brood mother, layer 1's boss ------------------------------------
       A rock mite grown enormous, which is the right shape for a first boss:
       you have been killing its brood for the whole layer, so it needs no
       introduction and its threat is legible before it does anything.

       Two phases and nothing more, in the Terraria manner. It walks and charges
       and chews through rock, so no wall you build is an answer; below half
       health it charges more often and starts calling its young. See broodTick.

       hp 900 against a starting shot that does 6 is a real fight without being
       a war of attrition, and it is meant to be fought AFTER the Blast Module,
       which does 22. Drops the Forge Core -- see the note there. */
    { "Brood Mother", 34, 24, 900, 16, 26, 0.55f, 0.05f, false, 0, false,  0, 0, 0.0f, 0.0f, true,  ITEM_FORGE_CORE, 1, 1, ITEM_NONE,            0, SPR_BROOD, 0xB04838, ITEM_EGG_BROOD,     false },

    /* --- the crash dummy ---------------------------------------------------
       A test rig, not a creature, and every column says so: no touch damage, no
       layer it spawns in, no drop, no charm. You place it with an egg.

       PLAYER_W by PLAYER_H exactly, because the whole point is to watch a body
       the size of YOURS get hurt by something. A dummy that was a different
       shape would answer a different question -- whether that ceiling of lava
       reaches a 14-cell creature, rather than whether it reaches you.

       hp is a real number rather than something enormous, and indestructible
       does the work instead: see the restore in entTickMode. A huge hp bar
       would still tick down, still show damage numbers climbing toward an end,
       and would eventually get there if you left something burning it. */
    { "Crash Dummy", PLAYER_W, PLAYER_H, 100, 0, 60, 0.62f, 0.09f, false, 0, false, 0, 0, 0.0f, 0.0f, false, ITEM_NONE, 0, 0, ITEM_NONE, 0, SPR_DUMMY, 0xE8C233, ITEM_EGG_DUMMY, true },

    /* --- the Shambler, layer 2 --------------------------------------------
       The first ordinary enemy below the first seal and the first creature
       rendered by the armature rather than a hand-authored 14x14 decal. Big,
       slow and direct: its novelty is readable BODY LANGUAGE, not a hidden AI
       rule. Long arms, a permanent hunch and a full eight-frame walk make its
       weight legible before the contact damage arrives.

       layerMask 2 is bit 1, which is caveLayerOf(ZONE_LAYER2). No surface
       spawn: seeing one is the unmistakable announcement that the player has
       entered the second tier. Ichor is a pickup component rather than a world
       cell; see entDie for why that distinction is save-safe. */
    { "Shambler", SHAMBLER_SPR_W, SHAMBLER_SPR_H, 96, 16, 38,
      0.32f, 0.045f, false, 2, false,
      0, 0, 0.0f, 0.0f, false,
      ITEM_ICHOR, 2, 4, ITEM_NONE, 0, SPR_NONE, 0x6F8062,
      ITEM_EGG_SHAMBLER, false },
};

/* Not saved with the creatures -- see entity.h. Written by save.cpp as one u32
   because "which bosses have you beaten" is the one fact about them that has to
   outlive a session. */
u32 g_bossesBeaten = 0;

/* Frames left before the spawner may act again; see SPAWN_COOL, which is kept
   beside the rest of the spawn tuning rather than up here. Declared at this
   point only because entReset has to clear it. */
static int g_spawnCool = 0;

void entReset() {
    navReset();
    memset(g_entities, 0, sizeof(g_entities));
    memset(g_pickups, 0, sizeof(g_pickups));
    /* Or a new world inherits the last one's cooldown -- which would be a
       silent, unreproducible delay of up to SPAWN_COOL frames on the first
       spawn after every Clear, and a test that ran a second scenario would
       measure the tail of the first. */
    g_spawnCool = 0;
}

int entAliveCount() {
    int n = 0;
    for (int i = 0; i < MAX_ENTITIES; ++i) if (g_entities[i].alive()) ++n;
    return n;
}

int pickupCount() {
    int n = 0;
    for (int i = 0; i < MAX_PICKUPS; ++i) if (g_pickups[i].used) ++n;
    return n;
}

/* One stack, rather than one overlay per chitin plate: an enemy that drops
   three chitin should look like a satisfying little prize, not three pixels
   trying to occupy the same space and a third of the fixed pool. */
static bool pickupSpawn(ItemId item, int count, float x, float y) {
    for (int i = 0; i < MAX_PICKUPS; ++i) {
        Pickup& p = g_pickups[i];
        if (p.used) continue;
        p.used = true; p.item = item; p.count = (i16)count;
        p.x = x; p.y = y;
        p.vx = ((float)(int)(rngNext() % 101u) - 50.0f) / 100.0f;
        p.vy = -1.2f - (float)(rngNext() % 40u) / 100.0f;
        return true;
    }
    return false;
}

/* --- being drawn in --------------------------------------------------------
   A collection radius alone is not a magnet. Past about double the bare figure,
   "items are collected from further away" stops being a bonus you can feel and
   becomes an inventory that fills without you noticing -- the drop simply
   vanishes from somewhere you were not looking. What makes the charm READ is
   seeing the drops come to you, so the outer part of the radius pulls and only
   the inner part collects.

   The pull is an acceleration rather than a set velocity, so a drop that is
   already falling arcs in instead of snapping sideways, and so several drops
   converging look like several objects rather than one animation. It is applied
   before the ordinary physics below, which then does the moving -- this adds a
   force to a simulation that already exists rather than being a second mover
   with its own idea of where things are. */
static void pickupMagnet(Pickup& p, const Player& body, const Inventory& inv) {
    const float reach = accessoryPickupRadius(inv);
    if (reach <= PICKUP_BASE_RADIUS) return;
    const float dx = body.centreX() - p.x, dy = body.centreY() - p.y;
    const float d2 = dx * dx + dy * dy;
    if (d2 > reach * reach || d2 < 0.01f) return;
    const float d = sqrtf(d2);
    /* Stronger the closer it gets, so the last few cells snap rather than
       drifting. Capped so a drop never outruns the physics step it is about to
       be handed to. */
    const float pull = fminf(0.85f, 0.10f + (reach - d) / reach * 0.55f);
    p.vx += dx / d * pull;
    p.vy += dy / d * pull;
    /* Enough drag that the pull cannot build into an orbit. Without it a drop
       arriving fast sails through the collection radius and comes back, which
       looks like the magnet is broken rather than like momentum. */
    p.vx *= 0.86f; p.vy *= 0.86f;
}

static void pickupTickMode(World& w, const Player& fallbackPlayer, Inventory& fallbackInv,
                           bool multiplayer) {
    for (int i = 0; i < MAX_PICKUPS; ++i) {
        Pickup& p = g_pickups[i];
        if (!p.used) continue;

        /* Nothing is collected without someone alive to collect it. That covers
           the obvious case of a corpse hoovering up its own drops, and the less
           obvious one of the character being switched off entirely -- see the
           stand-in in the main loop, which stands where the camera is and would
           otherwise pull every loose item on screen into the pack. */
        if (multiplayer) {
            for (int slot = 0; slot < MAX_PLAYERS && p.used; ++slot) {
                PlayerSession& session = g_playerSessions[slot];
                if (!session.connected || !session.body.alive) continue;
                pickupMagnet(p, session.body, session.inventory);
                const float dx = session.body.centreX() - p.x;
                const float dy = session.body.centreY() - p.y;
                if (dx * dx + dy * dy > PICKUP_BASE_RADIUS * PICKUP_BASE_RADIUS) continue;
                p.count = (i16)session.inventory.add(p.item, p.count);
                if (!p.count) p.used = false;
            }
            if (!p.used) continue;
        } else {
            if (fallbackPlayer.alive) pickupMagnet(p, fallbackPlayer, fallbackInv);
            const float dx = fallbackPlayer.centreX() - p.x;
            const float dy = fallbackPlayer.centreY() - p.y;
            if (fallbackPlayer.alive && dx * dx + dy * dy <= PICKUP_BASE_RADIUS * PICKUP_BASE_RADIUS) {
                p.count = (i16)fallbackInv.add(p.item, p.count);
                if (!p.count) { p.used = false; continue; }
            }
        }

        /* Pickup physics is intentionally a POINT: these are loose items, not
           creatures. They settle on ordinary floor, but g_matPassable keeps a
           torch from being an invisible shelf that traps chitin forever. */
        p.vy += ENT_GRAVITY;
        if (p.vy > ENT_MAX_FALL) p.vy = ENT_MAX_FALL;
        const int nx = (int)(p.x + p.vx), ny = (int)(p.y + p.vy);
        if (!playerSolid(w, nx, (int)p.y, SOLID_ANY)) p.x += p.vx;
        else p.vx = 0.0f;
        if (!playerSolid(w, (int)p.x, ny, SOLID_FLOOR)) p.y += p.vy;
        else p.vy = 0.0f;
    }
}

/* Enemies are land/air creatures, not swimmers. A single liquid cell through a
   body is enough to make the site submerged, including dense glowfluid. */
static bool liquidBox(const World& w, int bx, int by, int bw, int bh) {
    for (int y = by; y < by + bh; ++y)
        for (int x = bx; x < bx + bw; ++x)
            if (MATS[w.at(x, y).mat].kind == KIND_LIQUID) return true;
    return false;
}

int entSpawn(const World& w, int type, float cx, float cy) {
    if (type <= ENT_NONE || type >= ENT_COUNT) return -1;
    const EntityDef& d = ENT_DEFS[type];
    const int bx = (int)(cx - d.w * 0.5f), by = (int)(cy - d.h * 0.5f);
    /* Refuse to place one inside the world. A creature spawned in rock either
       has to be shoved out by an unstick rule -- which is a whole mechanism the
       player already needed and creatures do not -- or it stands in the wall
       hitting you through it. Failing the spawn is the honest answer, and every
       caller is a loop that can simply try somewhere else. */
    if (bx < PLAY_X0 || by < PLAY_Y0 || bx + d.w > PLAY_X1 || by + d.h > PLAY_Y1) return -1;
    if (solidBox(w, bx, by, d.w, d.h)) return -1;
    if (liquidBox(w, bx, by, d.w, d.h)) return -1;

    for (int i = 0; i < MAX_ENTITIES; ++i) {
        Entity& e = g_entities[i];
        if (e.type != ENT_NONE) continue;
        memset(&e, 0, sizeof(e));
        e.type = (u8)type;
        e.x = (float)bx; e.y = (float)by;
        e.hp = d.hp;
        e.facing = rngChance(128) ? 1 : -1;
        e.prevX = e.x; e.prevY = e.y;
        e.gaitX = e.x; e.gaitY = e.y;
        /* Staggered, so a group that spawns together does not step in unison.
           entityPixelMotion used to get this from the entity index; the phase
           carries it now, because the phase is what the legs read. */
        e.walkPhase = (float)(i * 3 % 16);
        /* A boss arrives WALKING. Left at zero, the charge clock is already
           negative on her first tick, so she opened the fight standing
           motionless through a dash with no heading -- three quarters of a
           second of nothing, in the one moment the encounter most needs to
           announce itself. */
        if (d.isBoss) e.actTimer = 90 + BOSS_WINDUP;
        return i;
    }
    return -1;   /* pool full */
}

/* --- death -----------------------------------------------------------------
   Most drops go into the WORLD as cells rather than straight into the pack, which is
   a deliberate difference from how mining works. digInto banks its yield
   because you asked for it and are standing there; a creature dies wherever it
   happened to be, often over a drop or in a pool, and teleporting its remains
   into your inventory from across a cavern would make loot something that
   happens TO you rather than something you collect.

   Placed only into empty cells, walking outward from the body, so a drop never
   overwrites the world and never vanishes into a wall. */
static void entDie(World& w, Entity& e) {
    const EntityDef& d = ENT_DEFS[e.type];
    /* The one fact about a creature that outlives the session. Set BEFORE the
       drop, so a pack too full to hold the Forge Core still counts as having
       beaten her -- the loot is on the floor either way, and "you won but the
       game did not notice" is the worst possible outcome of a boss fight. */
    if (d.isBoss) {
        const bool firstWin = (g_bossesBeaten & BOSS_LAYER1) == 0;
        g_bossesBeaten |= BOSS_LAYER1;
        if (firstWin) unlockLayerTwo(w);
    }
    /* --- the charm, before the ordinary drop ---------------------------
       First because the ordinary path can RETURN -- chitin leaves through
       pickupSpawn and never reaches the bottom of this function -- so a rare
       drop written after it would simply never happen for the six creatures
       that actually have one. Which is exactly the kind of bug that looks like
       a tuning problem: the roll is right there in the code, the rate reads as
       one in fifty, and nothing ever drops.

       Spawned as a PICKUP rather than written into the grid. A charm placed as
       a cell is a charm that can be buried by the sand the fight loosened, or
       land in the acid a slime left, and losing a one-in-fifty drop to physics
       is not a story anybody enjoys. */
    if (d.rareDrop != ITEM_NONE && d.rareOneIn > 0 &&
        (int)(rngNext() % (u32)d.rareOneIn) == 0)
        pickupSpawn(d.rareDrop, 1, e.centreX(), e.centreY());

    if (d.dropItem != ITEM_NONE && d.dropMax > 0) {
        int n = d.dropMin + (int)(rngNext() % (u32)(d.dropMax - d.dropMin + 1));
        const int cx = (int)e.centreX(), cy = (int)e.centreY();
        /* Chitin and every non-material component are collectibles, not
           falling-sand debris. They therefore cannot get caught on a torch or
           be truncated to a u8 material id. World substances retain their
           physical-cell behavior; components such as Ichor and the Forge Core
           remain typed inventory objects on the floor. */
        if (d.dropItem == (ItemId)MAT_CHITIN) {
            if (pickupSpawn(d.dropItem, n, (float)cx, (float)cy)) {
                e.type = ENT_NONE;
                return;
            }
            /* A full overlay pool may fall back to a real Chitin cell: it has
               a MatId and therefore remains valid falling-sand matter. */
        } else if (d.dropItem >= MAT_COUNT) {
            /* Components have no legal cell representation. A saturated
               pickup pool may cost the drop, but must never reinterpret its
               low byte as a material and index the simulation tables with it. */
            pickupSpawn(d.dropItem, n, (float)cx, (float)cy);
            e.type = ENT_NONE;
            return;
        }
        for (int r = 0; r <= 6 && n > 0; ++r) {
            for (int dy = -r; dy <= r && n > 0; ++dy) {
                for (int dx = -r; dx <= r && n > 0; ++dx) {
                    if (imax(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy) != r) continue;
                    const int px = cx + dx, py = cy + dy;
                    if (px < PLAY_X0 || px > PLAY_X1 || py < PLAY_Y0 || py > PLAY_Y1) continue;
                    if (w.at(px, py).mat != MAT_EMPTY) continue;
                    w.setCell(px, py, (u8)d.dropItem);
                    --n;
                }
            }
        }
    }
    e.type = ENT_NONE;
}

bool entDamageAt(int x, int y, int damage) {
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        Entity& e = g_entities[i];
        if (!e.alive()) continue;
        if (x < e.left() || x > e.right() || y < e.top() || y > e.bottom()) continue;
        e.hp -= damage;
        e.hurtFlash = 6;
        return true;
    }
    return false;
}

int entDamageDisc(int cx, int cy, int radius, int damage) {
    int hit = 0;
    const int r2 = radius * radius;
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        Entity& e = g_entities[i];
        if (!e.alive()) continue;
        const float dx = e.centreX() - (float)cx, dy = e.centreY() - (float)cy;
        if (dx * dx + dy * dy > (float)r2) continue;
        e.hp -= damage;
        e.hurtFlash = 6;
        ++hit;
    }
    return hit;
}

/* --- movement --------------------------------------------------------------
   Axis-separated, the same way the player moves: try x, then try y, and give up
   only the axis that is blocked. Moving both at once and rejecting the whole
   step means a creature walking into a one-cell lip stops dead rather than
   sliding along it, and against terrain that is one cell per grain that is most
   of the terrain. */
static void moveAxis(const World& w, Entity& e, float dx, float dy) {
    const EntityDef& d = ENT_DEFS[e.type];
    if (dx != 0.0f) {
        const float nx = e.x + dx;
        if (!solidBox(w, (int)nx, (int)e.y, d.w, d.h)) e.x = nx;
        else {
            /* A step up, so a walker is not stopped by every pebble. Half the
               body height, which is roughly what the player's own STEP_UP is as
               a fraction and reads the same way: it clears clutter and refuses
               walls. */
            bool climbed = false;
            for (int up = 1; up <= d.h / 2 && !climbed; ++up)
                if (!solidBox(w, (int)nx, (int)e.y - up, d.w, d.h)) {
                    e.x = nx; e.y -= (float)up; climbed = true;
                }
            if (!climbed) { e.vx = 0.0f; e.facing = -e.facing; }
        }
    }
    if (dy != 0.0f) {
        const float ny = e.y + dy;
        if (!solidBox(w, (int)e.x, (int)ny, d.w, d.h, dy > 0 ? SOLID_FLOOR : SOLID_ANY)) e.y = ny;
        else { if (dy > 0.0f) e.onGround = true; e.vy = 0.0f; }
    }
}

/* --- the archetypes --------------------------------------------------------
   Each of these is a GRADIENT FOLLOWER, not a planner. The mite and the slime
   follow the player's x; the moth follows temperature. None of them knows the
   shape of the world, which is what makes them cheap and also what makes them
   behave like animals rather than like guided missiles. */

/* Defined below, beside groundChase, because that is where the reasoning
   lives; declared here because the mite and the slime come first in the file. */
static float routedDir(Entity& e, const Player& p, bool* climb);

static void miteTick(World& w, Entity& e, const Player& p) {
    const EntityDef& d = ENT_DEFS[e.type];
    /* NOT routed, alone among the walkers, and that is the creature rather
       than an oversight: a mite's answer to a wall is to EAT it. Send it round
       the long way and the chew below never triggers, because chewing only
       happens when it is pressed against something. The one thing that makes
       this creature different from a slow husk would quietly stop happening. */
    const float toward = p.centreX() - e.centreX();
    if (toward > 1.0f)      e.facing = 1;
    else if (toward < -1.0f) e.facing = -1;
    e.vx += (float)e.facing * d.accel;
    if (e.vx >  d.speed) e.vx =  d.speed;
    if (e.vx < -d.speed) e.vx = -d.speed;

    /* Chewing. Only when actually pressed against something, and only rock and
       softer -- so it eats stone, dirt and a wooden door, and is stopped cold
       by a metal wall or by a layer barrier. That ladder is the point: a wall
       is a delay whose length you choose by what you build it out of. */
    const int ahead = e.facing > 0 ? e.right() + 1 : e.left() - 1;
    if (ahead > PLAY_X0 && ahead < PLAY_X1 && ++e.actTimer >= 14) {
        e.actTimer = 0;
        for (int y = e.top(); y <= e.bottom(); ++y) {
            const u8 m = w.at(ahead, y).mat;
            if (m == MAT_EMPTY || g_matStrength[m] > STR_ROCK) continue;
            w.setCell(ahead, y, MAT_EMPTY);
            break;   /* one cell per bite; a mite is not a mining tool */
        }
    }
}

bool stalkTick(Entity& e, const Player& p, const StalkSpec& spec) {
    /* One counter, three phases, read by where it sits:

         actTimer >  poise            drifting
         actTimer in 1..poise         poised, telegraphing
         actTimer <= 0                dashing, for `dash` frames

       Counting DOWN through zero rather than holding a separate phase id is
       the same shape broodTick uses, and it is what lets a creature adopt this
       without a new field. */
    if (--e.actTimer <= -spec.dash) {
        e.actTimer = spec.drift + spec.poise;
        e.telegraph = 0;
        return false;
    }

    if (e.actTimer < 0) {
        /* Committed. The heading was chosen on the last frame of the poise and
           is not revisited -- which is the entire reason this is dodgeable
           rather than a homing missile with a pause in it. */
        e.vx = e.aimX * spec.speed;
        e.vy = e.aimY * spec.speed;
        if (e.vx > 0.05f) e.facing = 1; else if (e.vx < -0.05f) e.facing = -1;
        e.telegraph = 0;
        return true;
    }

    if (e.actTimer <= spec.poise) {
        /* Poised. Sheds speed rather than stopping dead, so the pause reads as
           a creature gathering itself rather than as the game hitching. */
        e.vx *= 0.55f;
        e.vy *= 0.55f;
        e.telegraph = e.actTimer;
        /* Aimed on the LAST frame of the poise. Choosing at the start would
           tell you where it is going before you have had a chance to be
           somewhere else, and then the telegraph would be decoration rather
           than information you can act on. */
        if (e.actTimer == 1) {
            float dx = p.centreX() - e.centreX();
            float dy = p.centreY() - e.centreY();
            const float len = sqrtf(dx * dx + dy * dy);
            if (len > 0.01f) { dx /= len; dy /= len; }
            else { dx = (float)e.facing; dy = 0.0f; }
            e.aimX = dx; e.aimY = dy;
        }
        return true;
    }

    e.telegraph = 0;
    return false;      /* drifting: the caller's frame */
}

/* --- the moth: STALK -------------------------------------------------------
   It was a pure heat-gradient follower, which made it the clearest example of
   the roster's one-note problem: it and the husk and the slime all walked at
   you continuously and differed only in speed and in what they left behind.

   Now it drifts up the gradient, stops, and lunges. That reads completely
   differently from the bat -- which is the other flier, and which is fast and
   CONTINUOUS -- so the two are told apart by watching them rather than by
   noticing one has more health. It also gives a moth a moment where it is not
   moving, which is when you shoot it. */
static const StalkSpec MOTH_STALK = { 64, 20, 16, 1.75f };

/* Can the flyer's WHOLE collision box travel to a point in a straight line?
   A centreline ray can pass through a narrow tube while both wings are in
   rock. Sampling a full body box every nav cell agrees with moveAxis and is
   fine enough that a 9x7 creature cannot skip across a corner. */
static bool flyerLineClear(const World& w, const Entity& e, float tx, float ty) {
    const EntityDef& d = ENT_DEFS[e.type];
    const float x0 = e.centreX(), y0 = e.centreY();
    const float dx = tx - x0, dy = ty - y0;
    const int steps = (int)(fmaxf(fabsf(dx), fabsf(dy)) / (float)NAV) + 1;
    for (int s = 1; s <= steps; ++s) {
        const float q = (float)s / (float)steps;
        const int x = (int)(x0 + dx * q - d.w * 0.5f);
        const int y = (int)(y0 + dy * q - d.h * 0.5f);
        if (solidBox(w, x, y, d.w, d.h)) return false;
    }
    return true;
}

static void mothTick(World& w, Entity& e, const Player& p) {
    const EntityDef& d = ENT_DEFS[e.type];

    /* The stalk owns the poise and the dash; everything below is the drift,
       which for a moth means steering up the heat gradient. */
    if (stalkTick(e, p, MOTH_STALK)) return;

    /* Sample the temperature field on a ring and steer up the gradient. Eight
       directions at three distances is 24 reads, which against a cap of ten
       creatures is nothing, and it is a genuine gradient rather than a search:
       the moth does not know where the furnace IS, only which way is warmer.

       Follows the PLAYER when nothing is warm, so a moth in a cold cave is
       still a threat rather than a decoration hovering in place. */
    /* Reach: 30, 60 and 90 cells along each of eight directions. The first cut
       sampled at 12/24/36 and that was too short to do the job the archetype
       exists for -- "your furnace is a beacon" has to be true from across a
       cavern, and a 36-cell radius is a tenth of the screen's width, so a moth
       had to blunder within a body length of the fire before it noticed it.
       At 90 the sense radius is a sixth of the view, which is far enough that a
       moth entering the room turns toward the forge rather than toward you. */
    static const int DX8[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
    static const int DY8[8] = { 0, 1, 1,  1,  0, -1, -1, -1 };
    const int cx = (int)e.centreX(), cy = (int)e.centreY();
    int best = -1; u8 bestT = w.temp[cy * SIM_W + cx];
    for (int k = 0; k < 8; ++k) {
        for (int step = 1; step <= 3; ++step) {
            const int sx = cx + DX8[k] * step * 30, sy = cy + DY8[k] * step * 30;
            if (sx < PLAY_X0 || sx > PLAY_X1 || sy < PLAY_Y0 || sy > PLAY_Y1) continue;
            const u8 t = w.temp[sy * SIM_W + sx];
            if (t > bestT) { bestT = t; best = k; }
        }
    }

    float wantX, wantY, targetX, targetY;
    if (best >= 0) {
        wantX = (float)DX8[best]; wantY = (float)DY8[best];
        targetX = e.centreX() + wantX * 90.0f;
        targetY = e.centreY() + wantY * 90.0f;
    }
    else {
        targetX = p.centreX(); targetY = p.centreY();
        wantX = targetX - e.centreX();
        wantY = targetY - e.centreY();
        const float len = sqrtf(wantX * wantX + wantY * wantY);
        if (len > 0.01f) { wantX /= len; wantY /= len; }
    }
    /* The original heat/player gradient remains the whole personality in open
       space. Only an obstructed full-body line consults the clearance field,
       so a too-small tube becomes a route around it (or a stop at its mouth). */
    if (!flyerLineClear(w, e, targetX, targetY)) {
        int rx = 0, ry = 0;
        if (navFlyHeading((int)e.centreX(), e.bottom(), &rx, &ry)) {
            wantX = (float)rx; wantY = (float)ry;
            if (rx && ry) { wantX *= 0.70710678f; wantY *= 0.70710678f; }
            if (!rx && !ry) { e.vx *= 0.82f; e.vy *= 0.82f; }
        }
    }
    /* A wingbeat bob, so it does not travel as if on rails. Cosmetic, and it
       also makes a moth genuinely harder to hit than a walker, which is the
       difference between the two that its low hp is balanced against. */
    e.animPhase += 0.18f;
    wantY += sinf(e.animPhase) * 0.55f;

    e.vx += wantX * d.accel;
    e.vy += wantY * d.accel;
    const float sp = sqrtf(e.vx * e.vx + e.vy * e.vy);
    if (sp > d.speed) { e.vx = e.vx / sp * d.speed; e.vy = e.vy / sp * d.speed; }
    if (e.vx > 0.05f) e.facing = 1; else if (e.vx < -0.05f) e.facing = -1;
}

static void slimeTick(World& w, Entity& e, const Player& p) {
    const EntityDef& d = ENT_DEFS[e.type];
    bool climb = false;
    const float toward = routedDir(e, p, &climb);
    if (toward > 0.5f)       e.facing = 1;
    else if (toward < -0.5f) e.facing = -1;
    e.vx += (float)e.facing * d.accel;
    if (e.vx >  d.speed) e.vx =  d.speed;
    if (e.vx < -d.speed) e.vx = -d.speed;

    /* The trail. One cell at a time and only into air directly beneath the
       body, so it drips rather than spraying -- and rarely, because acid is
       spent by whatever it dissolves but a corridor with a puddle every cell
       would still be impassable rather than merely dangerous.

       This is the archetype's whole threat: the creature is trivial to kill and
       what it leaves behind is not, so killing one in your own doorway is a
       mistake you make exactly once. */
    if (++e.actTimer >= 90) {
        e.actTimer = 0;
        /* The drip lands on the slime's OWN bottom row, not the cell below it.
           Below it is the floor it is standing on -- solid by definition, since
           that is what it is standing on -- so the "only into air" guard was
           never satisfied and the creature's entire threat quietly did nothing.
           Measured before this fix: 1,200 frames of walking laid zero acid.

           Its own rows are genuinely air: creatures are an overlay and do not
           occupy the grid (see the note at the top of entity.h), unlike the
           player, who publishes a collision box to the world. So this drips
           into the space the body is passing through, which then flows and
           pools on its own like any other liquid. */
        const int dx = e.left() + (int)(rngNext() % (u32)d.w);
        const int dy = e.bottom();
        if (dx > PLAY_X0 && dx < PLAY_X1 && dy > PLAY_Y0 && dy < PLAY_Y1
            && w.at(dx, dy).mat == MAT_EMPTY)
            w.setCell(dx, dy, MAT_ACID);
    }
}

/* --- which way is the player, ROUTED ---------------------------------------

   The straight line is the fallback here, not the rule. navHeading reads the
   flow field (see navigate.h) and answers with the way out of this room rather
   than the way the player happens to lie, which is the difference between a
   creature that comes round the corner and one that stands in a corner pushing
   at rock.

   Two things make this safe to put under every ground creature at once.

   It only ever ADDS reachability: when the field cannot answer -- off the
   window, buried, or in a pocket with no route at all -- this returns the
   straight line, which is exactly the behaviour the creature has today.

   And it is a HEADING, not a path. Nothing is stored on the creature, nothing
   has to be invalidated when a wall is dug through or a pile collapses, and a
   creature knocked across the room is not following a plan that is now wrong.

   `climb` comes back true when the route goes up, which is a much better hop
   cue than "am I stuck": it fires at the bottom of a step rather than after
   the creature has already failed to walk through one. */
static float routedDir(Entity& e, const Player& p, bool* climb) {
    *climb = false;
    const EntityDef& d = ENT_DEFS[e.type];
    int dir = 0;
    if (navHeading((int)e.centreX(), e.bottom(), d.h, &dir, climb))
        return (float)dir;

    const float dx = p.centreX() - e.centreX();
    return dx > 0 ? 1.0f : -1.0f;
}

/* --- the crash dummy: running away from things that would hurt YOU -----------

   It stands still until something frightens it, and the only things that
   frighten it are the things that damage the character. That is not a
   resemblance, it is the same two tables: bodyTemp's HEAT_HURT_AT and
   COLD_HURT_AT lines, and g_matContactDamage. Player::update asks exactly those
   two questions of exactly those sources -- see the two blocks in it -- so a
   hazard the dummy flinches from is a hazard that hurts you, and one it walks
   through does not. A second opinion here would make it a liar, and a liar is
   worse than no dummy at all.

   The BARE lines rather than a Player's, since it wears nothing. It is a
   measure of the room, not of your gear. */
/* How far off it notices trouble. Forty-eight rather than thirty, which is
   more than a body-length: at thirty it could only see a fire once the fire was
   nearer than the dummy is TALL, so it started running when it was already
   uncomfortable rather than when it saw the thing. */
static const int   DUMMY_SENSE   = 48;
/* Sampling step for the far scan. Two is cheap and blurry, and blurry is fine
   for "something over there is bad" -- but see the body scan in dummyTick,
   which is deliberately not blurry, because a stride of two steps straight over
   a hazard one cell wide. */
static const int   DUMMY_STRIDE  = 2;
/* How long it keeps running after the last frightening thing left its senses.
   This is the "it runs further than it has to" number and it is the whole
   character of the thing: stopping the instant it was safe would look like a
   machine satisfying a condition, and bolting for another second and a half
   looks like something that got a fright. */
static const int   DUMMY_PANIC   = 90;
static const float DUMMY_RUN     = 1.7f;  /* multiplies its walk when fleeing */

/* Is this cell one of the things that would damage the character? */
static bool dummyHazard(const World& w, int x, int y) {
    if (x < 0 || x >= SIM_W || y < 0 || y >= SIM_H) return false;
    const int i = y * SIM_W + x;
    if (g_matContactDamage[w.cells[i].mat] > 0.0f) return true;
    const u8 t = w.temp[i];
    return t >= HEAT_HURT_AT || t <= COLD_HURT_AT;
}

static void dummyTick(World& w, Entity& e, const Player& p) {
    const EntityDef& d = ENT_DEFS[e.type];

    /* Cells the body is actually IN, at FULL resolution and weighted heavily.
       The far scan below steps by two to stay cheap, and a stride of two walks
       straight over a hazard one cell wide -- a single acid drip sitting on an
       even column would be invisible to it. Blurring the horizon is fine;
       blurring "am I standing in it" is not, because that is the one answer
       this thing exists to give. */
    float away = 0.0f;
    int found = 0;
    bool touching = false;
    const int cx = (int)e.centreX(), cy = (int)e.centreY();
    for (int y = e.top(); y <= e.bottom(); ++y)
        for (int x = e.left(); x <= e.right(); ++x)
            if (dummyHazard(w, x, y)) {
                ++found; touching = true;
                away += (float)(cx - x) * 4.0f;
            }

    /* Then the horizon. A REPULSION rather than a nearest threat, weighted by
       1/distance so a wall of fire on one side outvotes a single warm cell on
       the other, and so being surrounded pushes it toward the gap instead of
       toward whichever hazard the scan happened to reach first. */
    for (int y = cy - DUMMY_SENSE; y <= cy + DUMMY_SENSE; y += DUMMY_STRIDE)
        for (int x = cx - DUMMY_SENSE; x <= cx + DUMMY_SENSE; x += DUMMY_STRIDE) {
            if (!dummyHazard(w, x, y)) continue;
            ++found;
            const float dx = (float)(cx - x), dy = (float)(cy - y);
            const float d2 = dx * dx + dy * dy;
            if (d2 < 1.0f) continue;      /* underfoot: the body scan has it */
            away += dx / d2 * 64.0f;
        }

    /* Pain from anything else -- a shot, a swing, a falling rock -- reads as
       "get away from whoever did that". The source is not recorded anywhere, so
       it flees the character, which is right almost every time because the
       character is what is poking it. */
    if (e.hurtFlash > 0 && !found) {
        away += p.centreX() > e.centreX() ? -1.0f : 1.0f;
        ++found;
    }

    if (found) {
        /* Committed on the frame the fright happens and not revisited while the
           panic lasts. Re-deciding every frame in a symmetrical hazard makes it
           jitter on the spot, which reads as a broken thing rather than a
           frightened one. */
        if (away > 0.01f || away < -0.01f) e.aimX = away > 0.0f ? 1.0f : -1.0f;
        /* Standing dead centre in something, so no side is worse. It has to
           pick one and commit rather than balance: a body in the middle of a
           pool that keeps re-deciding stays in the pool. */
        else if (touching || e.aimX == 0.0f) e.aimX = e.facing >= 0 ? 1.0f : -1.0f;
        e.actTimer = DUMMY_PANIC;
    }

    if (e.actTimer > 0) {
        --e.actTimer;
        const float top = d.speed * DUMMY_RUN;
        e.vx += e.aimX * d.accel;
        if (e.vx >  top) e.vx =  top;
        if (e.vx < -top) e.vx = -top;
        if (e.vx > 0.05f) e.facing = 1; else if (e.vx < -0.05f) e.facing = -1;
        /* A scramble over a lip while running, which is the difference between
           fleeing and cowering against the first step it meets. */
        if (e.onGround && e.vx == 0.0f) e.vy = -2.2f;
        return;
    }

    /* Calm: it stands where you put it. Braked rather than stopped dead so the
       end of a run is a skid, and so a shove still slides it a little way --
       which is most of what poking it around is. */
    e.vx *= 0.80f;
    if (e.vx < 0.02f && e.vx > -0.02f) e.vx = 0.0f;
}

/* Walk toward the player, or away to hold a standoff. Shared by everything that
   moves along the ground, because "which way is the player" is the same
   question however the creature answers it. */
static void groundChase(Entity& e, const Player& p, float speed, float accel,
                        float standOff, bool* climb = 0) {
    bool wantClimb = false;
    const float dx = p.centreX() - e.centreX();
    const float adx = dx < 0 ? -dx : dx;
    float want = routedDir(e, p, &wantClimb);
    if (climb) *climb = wantClimb;
    if (standOff > 0.0f) {
        /* Too close: back off. Roughly right: hold station. This is what makes
           a shooter a shooter rather than a slow melee creature. */
        if (adx < standOff * 0.75f)      want = -want;
        else if (adx < standOff * 1.25f) want = 0.0f;
    }
    if (want > 0.1f)       e.facing = 1;
    else if (want < -0.1f) e.facing = -1;
    e.vx += want * accel;
    if (e.vx >  speed) e.vx =  speed;
    if (e.vx < -speed) e.vx = -speed;
}

static void huskTick(Entity& e, const Player& p) {
    const EntityDef& d = ENT_DEFS[e.type];
    bool climb = false;
    groundChase(e, p, d.speed, d.accel, 0.0f, &climb);
    /* Hops when the route goes up, and still hops when it is simply stuck with
       the player overhead. The second clause is the old rule and it stays,
       because it is the one that covers everything the field cannot see: a
       creature outside the window, or one wedged on a lip too small to be a
       node. The whole creature is "it does not stop", and a zombie permanently
       stuck on a one-cell lip is a zombie that stops. */
    if (e.onGround && (climb || (e.vx == 0.0f && p.centreY() < e.centreY())))
        e.vy = -2.2f;
}

/* --- the Shambler: drag, lurch, recover -----------------------------------
   Sharing huskTick made the large creature behave like an enlarged Husk: a
   perfectly even walk and a hop every time the navigation field pointed up.
   For a 36-cell body, a route can point up for a long stretch before the body
   reaches the actual obstruction, so it spent more time jumping than walking.

   Its cadence has a long dragging half and a short acceleration into the next
   step. Because walkPhase is accumulated from real displacement, the authored
   walk slows during the drag and catches up during the lurch instead of feet
   cycling under a uniformly sliding body. */
static const int SHAMBLER_JUMP_COOLDOWN = 150;

static void shamblerTick(const World& w, Entity& e, const Player& p) {
    const EntityDef& d = ENT_DEFS[e.type];

    e.animPhase += 0.11f;
    const float wave = sinf(e.animPhase);
    const float lurch = wave > 0.0f ? wave : 0.0f;
    const float pace = 0.55f + 0.65f * lurch;

    bool climb = false;
    groundChase(e, p, d.speed * pace,
                d.accel * (0.80f + 0.50f * lurch), 0.0f, &climb);

    if (e.actTimer > 0) --e.actTimer;

    /* Jump at the wall, not merely because some later part of the route rises.
       The cooldown is longer than a complete jump, so landing cannot
       immediately launch the next hop while the same route remains uphill. */
    const int probeX = e.facing > 0 ? e.right() + 1 : e.left() - 1;
    bool blockedAhead = false;
    if (probeX > PLAY_X0 && probeX < PLAY_X1)
        for (int y = e.top(); y <= e.bottom(); ++y)
            if (playerSolid(w, probeX, y, SOLID_ANY)) {
                blockedAhead = true;
                break;
            }

    if (e.onGround && e.actTimer == 0 && blockedAhead &&
        (climb || p.centreY() < e.centreY())) {
        e.vy = -2.2f;
        e.actTimer = SHAMBLER_JUMP_COOLDOWN;
    }
}

/* --- the bat: why it misses -------------------------------------------------
   It picks a heading, commits to it for a stretch of frames, and cannot turn
   fast enough to fix a bad one. That is the whole trick, and it is worth being
   explicit that the overshoot is NOT emergent from being fast: a fast creature
   that re-aims every frame tracks you perfectly and is simply unavoidable.

   aimHold is the commitment. While it runs, the bat flies at a point it decided
   on earlier -- so if you move after it commits, it arrives where you WERE,
   sails past, and has to come round again. */
static void batTick(const World& w, Entity& e, const Player& p) {
    const EntityDef& d = ENT_DEFS[e.type];
    if (--e.aimHold <= 0) {
        if (!flyerLineClear(w, e, p.centreX(), p.centreY())) {
            int rx = 0, ry = 0;
            if (navFlyHeading((int)e.centreX(), e.bottom(), &rx, &ry)) {
                /* Short commitments while negotiating terrain. Once the
                   player is visible, the original long dodgeable line resumes. */
                e.aimX = e.centreX() + (float)rx * 64.0f;
                e.aimY = e.centreY() + (float)ry * 64.0f;
                e.aimHold = 8;
            } else {
                e.aimX = p.centreX(); e.aimY = p.centreY(); e.aimHold = 12;
            }
        } else {
            /* Re-aim at where the player is RIGHT NOW, then stop looking. The
               jitter stops two bats in one room flying as a matched pair. */
            e.aimX = p.centreX() + (float)((int)(rngNext() % 41u) - 20);
            e.aimY = p.centreY() + (float)((int)(rngNext() % 41u) - 20);
            e.aimHold = 34 + (int)(rngNext() % 26u);
        }
    }
    float ax = e.aimX - e.centreX(), ay = e.aimY - e.centreY();
    const float len = sqrtf(ax * ax + ay * ay);
    if (len > 0.01f) { ax /= len; ay /= len; }
    e.vx += ax * d.accel;
    e.vy += ay * d.accel;
    /* A flutter, so it does not read as a dart on rails. */
    e.animPhase += 0.32f;
    e.vy += sinf(e.animPhase) * 0.06f;
    const float sp = sqrtf(e.vx * e.vx + e.vy * e.vy);
    if (sp > d.speed) { e.vx = e.vx / sp * d.speed; e.vy = e.vy / sp * d.speed; }
    if (e.vx > 0.05f) e.facing = 1; else if (e.vx < -0.05f) e.facing = -1;
}

static void spitterTick(World& w, Entity& e, const Player& p) {
    const EntityDef& d = ENT_DEFS[e.type];
    groundChase(e, p, d.speed, d.accel, d.standOff);

    if (e.shotTimer > 0) { --e.shotTimer; return; }
    float dx = p.centreX() - e.centreX(), dy = p.centreY() - e.centreY();
    const float dist = sqrtf(dx * dx + dy * dy);
    if (dist > d.standOff * 2.2f || dist < 8.0f) return;

    /* Line of sight, SAMPLED rather than walked -- the same reasoning as the
       conduction probes: a handful of checks answers "is there a wall in the
       way" well enough, and this runs for a couple of creatures rather than for
       every cell in the world. */
    for (int k = 1; k <= 6; ++k) {
        const int sx = (int)(e.centreX() + dx * (float)k / 7.0f);
        const int sy = (int)(e.centreY() + dy * (float)k / 7.0f);
        if (sx < 0 || sx >= SIM_W || sy < 0 || sy >= SIM_H) return;
        if (playerSolid(w, sx, sy)) return;   /* blocked: hold fire */
    }

    /* --- aim ABOVE, because the glob falls ---------------------------------
       Once shots obey gravity this creature stops being a threat unless it is
       taught to lead its own arc, and it is the worst case in the game for it:
       a slow shot (1.7 cells a frame) at a long stand-off, so flight time runs
       to about fifty frames and an uncorrected glob lands roughly a hundred
       cells below the player's feet. Not "less accurate" -- it would never hit
       anything again, while still turning to face you and still playing its
       attack, which is the kind of break nobody spots from outside.

       This is the EXACT launch, not an approximation. The obvious iterative
       dodge -- "it takes this long, so it drops that far, so aim there" -- was
       tried first and measured, and it does not converge here: it treats the
       flight time as the straight-line distance over the speed, but the shot's
       horizontal speed is only v*cos(theta), so the steeper it aims the longer
       it actually takes, and at this speed and range the correction chases its
       own tail. It landed zero of six shots.

       The closed form. With y measured DOWNWARD, launch (vx,vy), offset
       (dx,dy), speed v and gravity g:

           dx = vx*t                     and    vx^2 + vy^2 = v^2
           dy = vy*t + g*t^2/2

       Substituting vx = dx/t and vy = (dy - g*t^2/2)/t and writing T = t^2
       collapses to one quadratic:

           (g^2/4)*T^2 - (g*dy + v^2)*T + (dx^2 + dy^2) = 0

       A negative discriminant means the target is simply out of ballistic
       range for this muzzle speed -- a real case, not an error -- and the
       answer there is to hold fire rather than to lob something that cannot
       arrive. Of the two roots, the SMALLER T is the flat, direct shot and the
       larger is the high mortar arc over it; a creature holding its distance
       and spitting at you wants the direct one. */
    const float v = d.shotSpeed, g = PROJ_GRAVITY;
    const float b = g * dy + v * v;
    const float disc = b * b - g * g * (dx * dx + dy * dy);
    if (disc < 0.0f) return;                       /* out of range: hold fire */
    const float T = 2.0f * (b - sqrtf(disc)) / (g * g);
    if (T <= 0.0001f) return;
    const float t = sqrtf(T);
    const float vx = dx / t;
    const float vy = (dy - 0.5f * g * T) / t;

    e.facing = dx > 0.0f ? 1 : -1;
    /* Power STR_NOTHING: an enemy shot passes through the world rather than
       digging it. A creature that could excavate at range would rewrite the
       terrain of every fight, which is the burrower archetype and deliberately
       not this one. */
    /* The muzzle offset follows the LAUNCH direction, not the line to the
       player: the glob has to leave along the arc it is actually flying, or a
       steeply-lobbed shot appears to start beside the creature and jump. */
    const float mlen = sqrtf(vx * vx + vy * vy);
    const float mx = (mlen > 0.001f) ? vx / mlen : 1.0f;
    const float my = (mlen > 0.001f) ? vy / mlen : 0.0f;
    projSpawn(e.centreX() + mx * 8.0f, e.centreY() + my * 8.0f, vx, vy,
              STR_NOTHING, 1, 240, 0xC8E060, 0, MAT_EMPTY, d.shotDamage, true,
              PROJ_GRAVITY);
    e.shotTimer = d.shotEvery;
}

/* --- the boss ---------------------------------------------------------------
   Two phases, and the second differs from the first in exactly two ways: it
   charges more often and it calls its brood. That is the entire fight.

   Terraria first bosses are this simple and they work, because what makes them
   memorable is the SHAPE of the encounter -- a thing far larger than you that
   keeps arriving -- rather than the branching of a decision tree.

   --- what was wrong with her ------------------------------------------------
   Two complaints, and one change answers both.

   She got STUCK. At 34x24 she is by far the largest box in the game, and the
   step-up in moveAxis clears half her height -- twelve cells -- which sounds
   generous and is not, because the thing she has to get over is usually the
   arena the player just built. Wedged, she walked on the spot forever.

   And a ROPE beat her outright. Her charge was a ground chase at a multiplier:
   it moved faster along the floor and it never left the floor, so a player
   hanging two body-lengths up was in a place the boss had no move to reach. The
   fight became "climb, wait, shoot", which is not a fight.

   The dash below is the answer to both, and it is deliberately ONE mechanism
   rather than two fixes. It commits to a free 2D heading, so up is a direction
   she can go; it suspends gravity, so the heading survives leaving the floor;
   and it ploughs rock across its leading face, so a platform is a delay chosen
   by what it is made of rather than a wall. Camping above her is now the thing
   that gets you hit.

   The wind-up is not decoration. A move that crosses the arena has to be
   readable or it is just damage, so she stops, glows, and only picks the
   heading at the end of it -- which also means the heading is aimed where you
   ARE at the moment of commitment, and dodging is a matter of moving after the
   telegraph rather than before it. */

/* Everything the dash bites through, across the leading face of the body. Rock
   and softer, the same ladder the mite's chewing respects: stone, dirt and a
   wooden platform give way, and a metal wall or a layer barrier stops her. That
   ladder is what makes building against her a decision rather than futile. */
static void broodPlough(World& w, const Entity& e, float dirX, float dirY) {
    /* One cell beyond the box on the leading side, along each axis she is
       actually travelling. Both, when the heading is diagonal -- a creature
       moving up and to the left is arriving at the corner, and clearing only
       one face would leave her grinding along the other. */
    if (dirX != 0.0f) {
        const int ax = dirX > 0.0f ? e.right() + 1 : e.left() - 1;
        if (ax > PLAY_X0 && ax < PLAY_X1)
            for (int y = e.top(); y <= e.bottom(); ++y) {
                if (y < PLAY_Y0 || y > PLAY_Y1) continue;
                const u8 m = w.at(ax, y).mat;
                if (m == MAT_EMPTY || g_matStrength[m] > STR_ROCK) continue;
                w.setCell(ax, y, MAT_EMPTY);
            }
    }
    if (dirY != 0.0f) {
        const int ay = dirY > 0.0f ? e.bottom() + 1 : e.top() - 1;
        if (ay > PLAY_Y0 && ay < PLAY_Y1)
            for (int x = e.left(); x <= e.right(); ++x) {
                if (x < PLAY_X0 || x > PLAY_X1) continue;
                const u8 m = w.at(x, ay).mat;
                if (m == MAT_EMPTY || g_matStrength[m] > STR_ROCK) continue;
                w.setCell(x, ay, MAT_EMPTY);
            }
    }
}

static void broodTick(World& w, Entity& e, const Player& p) {
    const EntityDef& d = ENT_DEFS[e.type];
    const bool wounded = e.hp * 2 <= d.hp;
    e.phase = wounded ? 1 : 0;

    /* --- has she actually been going anywhere? -------------------------
       Sampled against her own previous position, and read only while she is
       trying to walk: a boss holding still through a wind-up has not failed to
       move, she has decided not to. */
    const float moved = fabsf(e.x - e.prevX) + fabsf(e.y - e.prevY);
    e.prevX = e.x; e.prevY = e.y;

    /* actTimer counts down to the next charge; while it is NEGATIVE the charge
       is in progress and the creature is committed. Committing is what makes a
       charge dodgeable -- a boss that could abort mid-lunge would only be a
       faster walker. */
    --e.actTimer;
    if (e.actTimer <= -CHARGE_FRAMES) {
        /* The dash is over. Weight comes back, and the next one is scheduled
           far enough out that the wind-up is not the whole fight. */
        e.actTimer = (wounded ? 130 : 210) + BOSS_WINDUP;
        e.weightless = false;
        e.telegraph = 0;
        e.stuck = 0;
    }

    const bool charging = e.actTimer < 0;
    const bool winding  = !charging && e.actTimer <= BOSS_WINDUP;

    if (charging) {
        /* Committed. The heading was chosen on the frame the wind-up ended and
           is not revisited, which is the entire reason it can be dodged. */
        e.weightless = true;
        e.vx = e.aimX * BOSS_DASH_SPEED;
        e.vy = e.aimY * BOSS_DASH_SPEED;
        if (e.vx > 0.05f) e.facing = 1; else if (e.vx < -0.05f) e.facing = -1;
        broodPlough(w, e, e.aimX, e.aimY);
        e.telegraph = 0;
        return;
    }

    if (winding) {
        /* Braced. She sheds her speed rather than stopping dead, so the pause
           reads as a creature gathering itself and not as the game freezing. */
        e.vx *= 0.72f;
        e.telegraph = e.actTimer + 1;
        /* Aimed on the LAST frame of the wind-up. Choosing at the start would
           mean the telegraph tells you where she is going before you have had a
           chance to be somewhere else, which is a worse fight: the skill is in
           moving during the wind-up, and that only matters if it changes the
           answer. */
        if (e.actTimer == 0) {
            float dx = p.centreX() - e.centreX();
            float dy = p.centreY() - e.centreY();
            const float len = sqrtf(dx * dx + dy * dy);
            if (len > 0.01f) { dx /= len; dy /= len; }
            else { dx = (float)e.facing; dy = 0.0f; }
            e.aimX = dx; e.aimY = dy;
        }
        return;
    }

    e.telegraph = 0;
    e.weightless = false;

    /* --- wedged ---------------------------------------------------------
       Not a movement hack: she simply starts her next wind-up now. The dash
       already goes through rock and already ignores the floor, so the recovery
       is a move the player can see coming, and the fight has one fewer special
       case in it than a teleport or a nudge would have cost. */
    if (moved < BOSS_STUCK_CELLS) {
        if (++e.stuck >= BOSS_STUCK_FRAMES) {
            e.stuck = 0;
            e.actTimer = BOSS_WINDUP;
        }
    } else {
        e.stuck = 0;
    }

    groundChase(e, p, d.speed, d.accel, 0.0f);
    if (e.onGround && e.vx == 0.0f && p.centreY() < e.centreY()) e.vy = -2.6f;

    /* Chews rock like its young, but across its whole face -- it is 34 cells
       wide and should read as going THROUGH a wall rather than nibbling it. */
    broodPlough(w, e, (float)e.facing, 0.0f);

    /* The brood, in the second half only. Spawned AT the mother rather than
       around the player, so they arrive as a wave you can see coming. */
    if (wounded && ++e.shotTimer >= 150) {
        e.shotTimer = 0;
        for (int k = 0; k < 3; ++k)
            entSpawn(w, ENT_MITE,
                     e.centreX() + (float)((int)(rngNext() % 60u) - 30),
                     e.centreY() - 20.0f);
    }
}

static void entTickMode(World& w, Player& fallbackPlayer, Inventory& fallbackInv,
                        bool multiplayer) {
    /* One search for the whole roster, before anybody moves. Seeded from every
       live player, so in multiplayer a creature routes to whichever of them the
       terrain actually lets it reach rather than to the nearest one as the crow
       flies -- which is frequently the one behind a wall.

       navUpdate keeps its own clock and returns immediately on most frames; it
       is called unconditionally so there is exactly one place that decides how
       stale a route may be. */
    {
        float sx[MAX_PLAYERS + 1], sy[MAX_PLAYERS + 1];
        int n = 0;
        if (fallbackPlayer.alive) {
            sx[n] = fallbackPlayer.centreX();
            sy[n] = (float)fallbackPlayer.bottom();
            ++n;
        }
        if (multiplayer)
            for (int slot = 0; slot < MAX_PLAYERS && n < MAX_PLAYERS + 1; ++slot) {
                const PlayerSession& session = g_playerSessions[slot];
                if (!session.connected || !session.body.alive) continue;
                sx[n] = session.body.centreX();
                sy[n] = (float)session.body.bottom();
                ++n;
            }
        navUpdate(w, sx, sy, n);
    }

    for (int i = 0; i < MAX_ENTITIES; ++i) {
        Entity& e = g_entities[i];
        if (e.type == ENT_NONE) continue;
        /* A dummy cannot die, and the restore happens HERE -- before the death
           check, before contact, before the hazard pass -- so there is no
           ordering in which a big enough hit gets to kill it first. It still
           takes the damage and still flashes, because the flinch is the point;
           it just never runs out. */
        if (ENT_DEFS[e.type].indestructible) e.hp = ENT_DEFS[e.type].hp;
        if (e.hp <= 0) { entDie(w, e); continue; }

        /* The gait advances by however far the creature actually got last
           frame -- see Entity::walkPhase. Read before this frame's movement
           so every creature animates on the same completed step, rather than
           on whichever ones happen to be ticked before the draw. */
        e.walkPhase += fabsf(e.x - e.gaitX) + fabsf(e.y - e.gaitY);
        e.gaitX = e.x; e.gaitY = e.y;

        Player* targetPlayer = &fallbackPlayer;
        Inventory* targetInventory = &fallbackInv;
        if (multiplayer) {
            float best2 = 1e30f;
            for (int slot = 0; slot < MAX_PLAYERS; ++slot) {
                PlayerSession& session = g_playerSessions[slot];
                if (!session.connected || !session.body.alive) continue;
                const float dx = session.body.centreX() - e.centreX();
                const float dy = session.body.centreY() - e.centreY();
                const float d2 = dx * dx + dy * dy;
                if (d2 < best2) {
                    best2 = d2; targetPlayer = &session.body;
                    targetInventory = &session.inventory;
                }
            }
        }
        Player& p = *targetPlayer;
        Inventory& inv = *targetInventory;

        /* --- far enough away to stop existing ----------------------------
           Checked before anything else, so a creature the player will never
           see costs one distance test rather than a full tick.

           Removed, NOT killed: entDie drops loot and counts toward the boss
           flags, and a creature you walked away from has not been defeated.
           Silently dropping chitin in an empty cavern two screens away would
           be a slow leak of the one material the boss summon needs.

           Bosses are exempt, on the same reasoning that keeps them out of the
           spawn cap: she is summoned rather than found, the fight is a thing
           you chose to start, and having her evaporate because you backed down
           a corridor would be worse than losing to her. */
        if (!ENT_DEFS[e.type].isBoss) {
            const float dx = e.centreX() - p.centreX();
            const float dy = e.centreY() - p.centreY();
            if (dx * dx + dy * dy >
                (float)(ENT_DESPAWN_DIST * ENT_DESPAWN_DIST)) {
                e.type = ENT_NONE;
                e.hp   = 0;
                continue;
            }
        }

        const EntityDef& d = ENT_DEFS[e.type];
        if (e.hurtFlash > 0) --e.hurtFlash;
        if (e.touchTimer > 0) --e.touchTimer;

        switch (e.type) {
        case ENT_MITE:    miteTick(w, e, p);    break;
        case ENT_MOTH:    mothTick(w, e, p);    break;
        case ENT_SLIME:   slimeTick(w, e, p);   break;
        case ENT_HUSK:    huskTick(e, p);       break;
        case ENT_BAT:     batTick(w, e, p);     break;
        case ENT_SPITTER: spitterTick(w, e, p); break;
        case ENT_BROOD:   broodTick(w, e, p);   break;
        case ENT_DUMMY:   dummyTick(w, e, p);   break;
        case ENT_SHAMBLER: shamblerTick(w, e, p); break;
        default: break;
        }

        /* `weightless` is a per-frame decision a creature makes about itself,
           not a property of its species like `flies` -- the boss is a walker
           for all but the forty-six frames she is airborne, and giving her
           d.flies would mean she never touched the ground at all. */
        if (!d.flies && !e.weightless) {
            e.vy += ENT_GRAVITY;
            if (e.vy > ENT_MAX_FALL) e.vy = ENT_MAX_FALL;
            e.onGround = false;
        }
        moveAxis(w, e, e.vx, 0.0f);
        moveAxis(w, e, 0.0f, e.vy);

        /* --- the world hurts creatures too ------------------------------
           Not a courtesy. If lava and acid only hurt the player, then every
           hazard in the game is a pure downside and the obvious tactic is to
           lead things into a pool and watch nothing happen. Sampling the
           creature's own cells is the same measurement the player's heat
           damage makes, and it means a firetrap is a real answer. */
        const u8 hot = degC(60);
        for (int y = e.top(); y <= e.bottom(); ++y)
            for (int x = e.left(); x <= e.right(); ++x) {
                if (x < 0 || x >= SIM_W || y < 0 || y >= SIM_H) continue;
                if (w.temp[y * SIM_W + x] >= hot) { e.hp -= 1; y = e.bottom(); break; }
            }

        /* Contact damage, on a cooldown so that standing next to one is a
           steady drain rather than sixty hits a second. Armour is subtracted
           here rather than at the call site because this is the only place a
           creature ever hurts the player, and a resistance applied somewhere
           else would be a resistance somebody could forget to apply. */
        if (e.touchTimer == 0 && p.alive
            && e.right()  >= p.left() && e.left() <= p.right()
            && e.bottom() >= p.top()  && e.top()  <= p.bottom()) {
            const int dmg = imax(1, d.touchDamage - inv.armour());
            p.damage((float)dmg);
            p.hurtFlash = 10;
            e.touchTimer = d.touchCooldown;
        }

        if (e.hp <= 0) entDie(w, e);
    }
    pickupTickMode(w, fallbackPlayer, fallbackInv, multiplayer);
}

void entTick(World& w, Player& p, Inventory& inv) {
    entTickMode(w, p, inv, false);
}

void entTickPlayers(World& w) {
    entTickMode(w, g_player, g_inv, true);
}

/* PlayerSession::swingHit is sized in bits and cannot say MAX_ENTITIES, because
   multiplayer.h is included by things entity.h has no business dragging in. So
   the two are checked against each other HERE, in the one file that can see
   both -- the same trick the egg table's startup abort used, and for the same
   reason: a hardcoded size that quietly disagrees with the pool it indexes
   writes past the end of a struct, and does it only once the pool grows.

   If this fires, widen swingHit in multiplayer.h to match. */
static_assert(sizeof(((PlayerSession*)0)->swingHit) * 8 >= MAX_ENTITIES,
              "PlayerSession::swingHit has fewer bits than there are entities");

int entHitSegment(float x0, float y0, float x1, float y1,
                  float fromX, float fromY,
                  int damage, float knockback, u8* hitMask) {
    int struck = 0;
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        Entity& e = g_entities[i];
        if (!e.alive()) continue;
        if (hitMask && (hitMask[i >> 3] & (1u << (i & 7)))) continue;

        /* Nearest point on the segment to the creature's centre, then a box
           test around it. Sampling points ALONG the blade instead would miss a
           creature the stroke passes clean through between two samples -- and
           at a blade tip travelling most of an arc in fourteen frames, that is
           the common case rather than the corner one. */
        const float cx = e.centreX(), cy = e.centreY();
        const float dx = x1 - x0, dy = y1 - y0;
        const float len2 = dx * dx + dy * dy;
        float t = 0.0f;
        if (len2 > 0.0001f) {
            t = ((cx - x0) * dx + (cy - y0) * dy) / len2;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
        }
        const float nx = x0 + dx * t, ny = y0 + dy * t;

        /* Against the BOX rather than a radius. Creatures here are wider than
           they are tall or the other way round -- the Brood Mother is 34 by 24
           -- so a circular approximation would either miss her flanks or hit
           empty air above her. */
        const float hw = e.width() * 0.5f, hh = e.height() * 0.5f;
        if (nx < cx - hw || nx > cx + hw || ny < cy - hh || ny > cy + hh) continue;

        e.hp -= damage;
        e.hurtFlash = 6;
        if (knockback > 0.0f) {
            float kx = cx - fromX, ky = cy - fromY;
            const float kd = sqrtf(kx * kx + ky * ky);
            if (kd > 0.001f) { kx /= kd; ky /= kd; }
            else { kx = 1.0f; ky = 0.0f; }
            e.vx += kx * knockback;
            /* Biased upward. A shove that is purely horizontal slides a walker
               along the floor and it walks straight back; lifting it off the
               ground buys the swinger the frames to step away, which is the
               entire defensive value of knockback. */
            e.vy += ky * knockback - knockback * 0.45f;
        }
        if (hitMask) hitMask[i >> 3] |= (u8)(1u << (i & 7));
        ++struck;
    }
    return struck;
}

int entDamageKnockbackDisc(int cx, int cy, int radius, int damage, float knockback) {
    int hit = 0;
    const float r2 = (float)(radius * radius);
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        Entity& e = g_entities[i];
        if (!e.alive()) continue;
        float dx = e.centreX() - (float)cx, dy = e.centreY() - (float)cy;
        const float d2 = dx * dx + dy * dy;
        if (d2 > r2) continue;
        e.hp -= damage; e.hurtFlash = 6;
        const float len = sqrtf(d2);
        if (len > 0.01f) { e.vx += dx * knockback / len; e.vy += dy * knockback / len; }
        ++hit;
    }
    return hit;
}

/* --- the spawner -----------------------------------------------------------

   Where a creature may appear, and the rules are all negative -- a site has to
   survive every one of them. In order of how much each costs to evaluate, so
   the cheap refusals happen first:

     1. NOT ON SCREEN. Things appearing in front of you is the single most
        immersion-breaking thing a spawner can do. Sites are drawn from the
        margin around the view, which is exactly the band the light field
        already covers (see LIGHT_MARGIN) -- so this rule and rule 4 want the
        same rectangle, which is why the margin is what it is.
     2. IN THE DARK. The classic rule, and the one that makes a torch a tool
        rather than decoration: light is not just how you see, it is how you
        make somewhere safe. Underground that is nearly everywhere; on the
        surface it is only true at night.
     3. NOT ON PLAYER-PLACED BACKGROUND. Your own walls are yours. This is what
        makes building a base mean something mechanically instead of
        aesthetically, and the bit that records it has existed since the
        background layer was added -- see BG_PLACED -- waiting for exactly this.
     4. STANDING ROOM, on ground, in air. A creature needs somewhere to be.

   The type is chosen from the chunk's ZONE, which is what makes "what lives
   here" a property of the place. */

/* Sites tried per frame. Small: a spawn is a rare event and this runs every
   frame forever, so the budget is per-frame cost rather than per-spawn success.

   This used to carry a claim that twenty probes against a cap of ten "fills a
   dark cavern over a few seconds, which is the pace this wants -- somewhere
   gradually becoming occupied, not an ambush materialising". That claim was
   written beside the constant rather than measured from it, and it was wrong in
   the worst possible direction: measured in a dark layer-1 cavern, the first
   creature arrives on frame 1 and the cap is full on frame 10. A sixth of a
   second. It was precisely the ambush it promised not to be.

   Probes are not the pacing lever, though -- they are how hard the spawner
   looks for a legal site, and lowering them would make spawning depend on how
   cluttered the cave is rather than on time. The pacing lever is SPAWN_COOL
   below, which is a clock. */
static const int SPAWN_TRIES = 20;

/* Frames between spawns, whatever the probes find. This is the whole of the
   pacing: a cavern now reaches the cap in about five seconds instead of a sixth
   of one, and creatures arrive one at a time, which is what "somewhere
   gradually becoming occupied" has to mean if it means anything.

   A single global clock rather than per-creature or per-site cooldowns, because
   what wants limiting is the RATE THE PLAYER EXPERIENCES, and the player
   experiences one world. Reset on a successful spawn only -- frames where every
   probe was rejected cost nothing and should not bank credit toward a burst the
   moment you step into somewhere dark. */
static const int SPAWN_COOL = 80;
/* Brightness at or below which a site counts as dark. Torchlight is far above
   this, so a lit corridor is genuinely clear. */
static const int SPAWN_DARK  = 40;
/* Cells of clearance kept around the player, so nothing appears in your lap
   even if the camera happens to be looking elsewhere. Comfortably more than
   half the view's height. */
static const int SPAWN_MIN_DIST = 150;
/* How far a walker may fall from its sample point to find a floor. Deep enough
   to reach the floor of an ordinary cave from a point in its air, shallow
   enough that it cannot drop through a ceiling into the chamber below and
   appear somewhere the sample never described. */
static const int SPAWN_DROP = 48;
/* Local crowding. The global cap says how many creatures may exist, and says
   nothing at all about WHERE, so the whole allowance could be -- and was --
   spent filling one chamber while the rest of the cave stayed empty. A
   candidate point that already has SPAWN_LOCAL_MAX neighbours within
   SPAWN_LOCAL_R is refused, which pushes the next one somewhere else rather
   than lowering the total. */
static const int SPAWN_LOCAL_R   = 220;
static const int SPAWN_LOCAL_MAX = 3;

static int crowdingNear(float x, float y) {
    int n = 0;
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        const Entity& e = g_entities[i];
        if (!e.alive() || ENT_DEFS[e.type].isBoss) continue;
        const float dx = e.centreX() - x, dy = e.centreY() - y;
        if (dx * dx + dy * dy <= (float)(SPAWN_LOCAL_R * SPAWN_LOCAL_R)) ++n;
    }
    return n;
}

/* Live creatures that the SPAWNER is responsible for. Bosses are excluded,
   because they are summoned rather than spawned and a boss in the room must not
   stop the cave around it from being occupied -- nor count toward a cap that
   would then let her suppress her own brood. */
static int entSpawnedCount() {
    int n = 0;
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        const Entity& e = g_entities[i];
        if (e.alive() && !ENT_DEFS[e.type].isBoss) ++n;
    }
    return n;
}

void entSpawnTick(World& w, const Player& p, int camX, int camY, bool lightFieldValid) {
    if (g_spawnCool > 0) { --g_spawnCool; return; }
    int connected = 0;
    for (int slot = 0; slot < MAX_PLAYERS; ++slot)
        if (g_playerSessions[slot].connected && g_playerSessions[slot].body.alive) ++connected;
    if (connected < 1) connected = 1;
    if (entSpawnedCount() >= ENT_MAX_ALIVE * connected) return;

    for (int attempt = 0; attempt < SPAWN_TRIES; ++attempt) {
        /* Drawn from the padded light rectangle, then rejected if it lands on
           screen -- rather than sampling the margin's four arms directly, which
           needs a case per arm and biases toward the corners where two arms
           overlap. */
        /* In CELLS. LIGHT_W and LIGHT_MARGIN are sample counts now, and using
           them here quietly shrank the ring creatures appear in from 127 cells
           to 32 -- close enough to the view edge that they walked straight into
           it. See LIGHT_CELLS_W in light.h. */
        const int lx = (int)(rngNext() % (u32)LIGHT_CELLS_W) - LIGHT_MARGIN_CELLS;
        const int ly = (int)(rngNext() % (u32)LIGHT_CELLS_H) - LIGHT_MARGIN_CELLS;
        if (lx >= 0 && lx < VIEW_CELLS_W && ly >= 0 && ly < VIEW_CELLS_H) continue;

        const int x = camX + lx, y = camY + ly;
        if (x < PLAY_X0 + 2 || x > PLAY_X1 - 2 || y < PLAY_Y0 + 2 || y > PLAY_Y1 - 2) continue;

        const float pdx = (float)x - p.centreX(), pdy = (float)y - p.centreY();
        if (pdx * pdx + pdy * pdy < (float)(SPAWN_MIN_DIST * SPAWN_MIN_DIST)) continue;

        /* Somewhere already busy is not where the next one should appear. */
        if (crowdingNear((float)x, (float)y) >= SPAWN_LOCAL_MAX) continue;

        /* --- dark? --------------------------------------------------------
           The light buffer is only meaningful when lighting is actually being
           computed; with it switched off the array holds whatever was last
           written, possibly for a different camera position, and reading it
           would be reading stale numbers as if they were a measurement. So when
           it is off, fall back to the zone alone -- underground is dark, the
           surface is dark at night -- which is the same answer the light field
           gives everywhere except within a few dozen cells of a torch. */
        const u8 zone = w.zoneAt(x, y);
        const bool surface = (zone == ZONE_SKY);
        if (surface && !isNight()) continue;
        if (lightFieldValid && g_lightOn && lightRow(ly)[lx] > SPAWN_DARK) continue;

        /* --- yours? -------------------------------------------------------
           Player-placed background makes a place safe. Checked over the whole
           box the creature would occupy rather than at the one probe cell, so
           standing at the edge of your own wall is not a loophole. */
        bool claimed = false;
        for (int yy = y - 8; yy <= y + 8 && !claimed; ++yy)
            for (int xx = x - 8; xx <= x + 8; ++xx) {
                if (xx < 0 || xx >= SIM_W || yy < 0 || yy >= SIM_H) continue;
                if (w.bgPlaced(xx, yy)) { claimed = true; break; }
            }
        if (claimed) continue;

        /* --- which creature? ---------------------------------------------- */
        const int layer = surface ? 0 : caveLayerOf(zone);
        int pick[ENT_COUNT], np = 0;
        for (int t = ENT_NONE + 1; t < ENT_COUNT; ++t) {
            const EntityDef& d = ENT_DEFS[t];
            if (d.isBoss) continue;   /* summoned, never found */
            if (surface) { if (!d.surfaceAtNight) continue; }
            else if (!(d.layerMask & (1 << layer))) continue;
            pick[np++] = t;
        }
        if (!np) continue;
        const int type = pick[rngNext() % (u32)np];
        const EntityDef& d = ENT_DEFS[type];

        /* --- room to stand? -----------------------------------------------
           Fliers only need air. Walkers need air with a floor under it, or they
           spawn in a chimney and spend their life falling. */
        int bx = x - d.w / 2, by = y - d.h / 2;
        /* A walker used to need floor DIRECTLY under the randomly chosen point,
           and a random point in a cave is almost never exactly on the floor.
           Measured, the effect was not a bias, it was a near-monopoly: fliers
           took 97.6% of all spawns, and every walker sat under 1% -- the husk
           at 0.5%, which is why it read as "never spawns". Uniform picker,
           wildly non-uniform outcome, entirely from this one test.

           So look for the floor instead of demanding it: fall from the sample
           point up to SPAWN_DROP cells, and stand on the first floor found.
           That is the same intent -- do not spawn a walker in a chimney -- but
           it asks a question the world can usually answer. */
        if (!d.flies) {
            int drop = 0;
            while (drop <= SPAWN_DROP &&
                   !solidBox(w, bx, by + d.h, d.w, 1, SOLID_FLOOR)) { ++by; ++drop; }
            if (drop > SPAWN_DROP) continue;
        }
        if (solidBox(w, bx, by, d.w, d.h)) continue;
        if (liquidBox(w, bx, by, d.w, d.h)) continue;
        if (!d.flies && !solidBox(w, bx, by + d.h, d.w, 1, SOLID_FLOOR)) continue;

        if (entSpawn(w, type, (float)(bx + d.w / 2), (float)(by + d.h / 2)) >= 0) {
            g_spawnCool = SPAWN_COOL;
            return;                                   /* one a frame, at most */
        }
    }
}

/* --- drawing ---------------------------------------------------------------
   The sprite is 14x14 and the collision boxes are smaller, so the art is drawn
   CENTRED on the box and is allowed to overhang it. That is deliberate: wings,
   antennae and a slime's wobble should not be things you can be hit by, and a
   collision box that matched the art exactly would make every creature feel
   larger than it looks. */
/* A few cells of procedural articulation keep enemies from reading as decals
   translated across the world. This deliberately moves DRAWN pixels only:
   hitboxes, aim points, attacks and pathfinding retain their exact geometry.

   Each species moves the part its silhouette already teaches you to watch --
   wings on fliers, feet on walkers, the slime's loose underside, the spitter's
   venom sac. One or two cells is enough at this scale; more turns readable
   pixel art into flicker. `entityIndex` offsets otherwise identical creatures
   so a group does not flap in mechanical unison. */
/* Which half of the step cycle a walker is in, from ground covered.

   The STRIDE SCALES WITH THE CREATURE, which is the part worth stating: a husk
   twice the height of a mite has legs twice as long, and should not take twice
   as many steps to cross the same ground. Timer-driven gaits cannot express
   that at all -- every creature paces at whatever rate its divisor says,
   regardless of size or speed -- and it is most of why they read as identical
   machinery wearing different sprites.

   It also removes special cases rather than adding them. A charging creature
   covers more ground per frame, so its legs speed up because they are measuring
   the ground and not the clock; nothing has to notice that it is charging. */
static inline int gaitStep(const Entity& e) {
    const float stride = (float)imax(2, ENT_DEFS[e.type].h / 3);
    return (int)(e.walkPhase / stride) & 1;
}

static void entityPixelMotion(const Entity& e, int entityIndex, int sx, int sy,
                              int* dx, int* dy) {
    *dx = *dy = 0;
    /* Still the clock, and deliberately: wings beat while a flier hovers and
       lungs work while a creature stands, so those are genuinely time-based.
       Only the LEGS moved to the gait phase. */
    const u32 tick = g_world.frame + (u32)(entityIndex * 17);
    const bool moving = fabsf(e.vx) > 0.04f || fabsf(e.vy) > 0.04f;

    switch (e.type) {
    case ENT_MITE: {
        const int gait = gaitStep(e);
        if (moving && gait) --*dy;                         /* shell rises on a step */
        if (sy >= 10) *dx += (((sx / 2) + gait) & 1) ? 1 : -1;
        break;
    }
    case ENT_MOTH: {
        static const int wing[4] = { -1, 0, 1, 0 };
        const int beat = wing[(tick / 3u) & 3u];
        if (sy >= 3 && sy <= 9 && (sx <= 4 || sx >= 9)) {
            *dy += beat;
        } else if (sx >= 5 && sx <= 8) {
            *dy += (int)((tick / 14u) & 1u);               /* hot core breathes */
        }
        break;
    }
    case ENT_SLIME: {
        /* A slime MOVES by squashing, so the squash is ground covered. */
        const int squash = gaitStep(e);
        if (sy >= 3 && sy <= 6) *dy += squash;
        if (sy >= 9 && sy <= 12) {
            const int side = sx < SPR_W / 2 ? -1 : 1;
            *dx += squash ? -side : side;                  /* belly spreads/collects */
        }
        if (sy == 13) *dy += squash ? 1 : -1;              /* loose drips lag behind */
        break;
    }
    case ENT_HUSK: {
        const int gait = gaitStep(e);
        if (moving) {
            if (sy >= 12) *dx += (sx < SPR_W / 2) == (gait != 0) ? 1 : -1;
            if (sy >= 7 && sy <= 10 && (sx <= 3 || sx >= 10))
                *dx += (sx < SPR_W / 2) == (gait != 0) ? -1 : 1;
            if (gait) --*dy;
        } else if (sy >= 5 && sy <= 10) {
            *dy += (int)((tick / 24u) & 1u);               /* slow idle weight shift */
        }
        break;
    }
    case ENT_DUMMY: {
        /* A rig, so its motion is JOINTED rather than organic: legs swing, arms
           swing the opposite way, and nothing breathes. Standing, it is dead
           still -- every other creature here idles, because a living thing that
           stops completely looks broken, and this one is meant to look like
           equipment somebody stood up.

           The sy bands are rows of the 14-row CANVAS, not cells of the 30-cell
           box: rows 5-8 are the arms and shoulders, 11-13 the legs. */
        const int gait = gaitStep(e);
        if (moving) {
            if (sy >= 12) *dx += (sx < SPR_W / 2) == (gait != 0) ? 1 : -1;
            /* Arms counter-swing. Only the outer columns, which are the arms --
               the torso between them must not shear or the whole body wobbles
               like jelly, which is the one thing a rig should never do. */
            if (sy >= 5 && sy <= 8 && (sx <= 2 || sx >= 11))
                *dy += (sx < SPR_W / 2) == (gait != 0) ? 1 : -1;
            if (gait) --*dy;
        }
        break;
    }
    case ENT_BAT: {
        static const int wing[4] = { -1, 0, 1, 0 };
        const int beat = wing[(tick / 2u) & 3u];
        if (sy >= 1 && sy <= 8 && (sx <= 5 || sx >= 8)) *dy += beat;
        else if (sx >= 6 && sx <= 7) *dy += (int)((tick / 10u) & 1u);
        break;
    }
    case ENT_SPITTER: {
        const int gait = gaitStep(e);
        if (sy >= 10) *dx += (((sx / 2) + gait) & 1) ? 1 : -1;
        if (sy >= 2 && sy <= 5) {
            /* The sac tightens as the shot comes due, otherwise breathes at a
               slow idle rhythm. It is a warning encoded in the moving part. */
            const bool primed = e.shotTimer > 0 && e.shotTimer < 18;
            *dy -= (primed || ((tick / 16u) & 1u)) ? 1 : 0;
        } else if (moving && gait) --*dy;
        break;
    }
    case ENT_BROOD: {
        /* No charge special case: a dash covers more ground, so the legs
           speed up on their own. See gaitStep. */
        const int gait = gaitStep(e);
        if (sy >= 11) {
            /* Source-space grouping keeps every enlarged block of one authored
               leg together instead of tearing it at a scale boundary. */
            *dx += (((sx / 2) + gait) & 1) ? 1 : -1;
            if (gait) --*dy;
        } else if (e.telegraph > 0) {
            if (sy <= 5) ++*dy;                            /* brace into the wind-up */
        } else {
            if (sy <= 6) *dy -= (int)((tick / 18u) & 1u); /* armoured breathing */
            if (e.weightless) *dx += e.facing;             /* lean through the dash */
        }
        break;
    }
    default: break;
    }
}

void entDraw(u32* px, int camX, int camY, bool lit) {
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        const Entity& e = g_entities[i];
        if (!e.alive()) continue;
        const EntityDef& d = ENT_DEFS[e.type];

        /* The Shambler is already baked at its 22x36 collision size. Keeping
           that path explicit is what proves the rig can serve an enemy without
           squeezing it through the 14x14 hand-art sheet or adding live posing
           to this hot loop. The walk consumes the same distance clock as every
           other walker, but uses all eight authored rig frames rather than the
           two-pose pixel offsets those sprites can support. */
        if (e.type == ENT_SHAMBLER) {
            const u32* art = 0;
            const bool moving = fabsf(e.vx) > 0.04f || fabsf(e.vy) > 0.04f;
            if (!e.onGround) art = e.vy < 0.0f ? g_shamblerJump : g_shamblerFall;
            else if (moving) {
                const float stride = (float)imax(2, d.h / 3);
                const int frame = ((int)(e.walkPhase * 4.0f / stride)) &
                                  (SHAMBLER_WALK_FRAMES - 1);
                art = g_shamblerWalk[frame];
            } else {
                const int frame = (int)((g_world.frame / 36u + (u32)i) & 1u);
                art = g_shamblerIdle[frame];
            }

            const int ox = (int)e.x - camX, oy = (int)e.y - camY;
            for (int sy = 0; sy < SHAMBLER_SPR_H; ++sy) {
                for (int sx = 0; sx < SHAMBLER_SPR_W; ++sx) {
                    const int vx = ox + sx, vy = oy + sy;
                    if (vx < 0 || vx >= VIEW_CELLS_W || vy < 0 || vy >= VIEW_CELLS_H)
                        continue;
                    u32 out = art[sy * SHAMBLER_SPR_W +
                                  (e.facing < 0 ? SHAMBLER_SPR_W - 1 - sx : sx)];
                    if (!out) continue;
                    if (lit) out = shadeColor(out, viewShade(vx, vy));
                    if (e.hurtFlash > 0) out = 0xFFFFFF;
                    px[vy * VIEW_CELLS_W + vx] = out;
                }
            }
            continue;
        }

        if (d.sprite == SPR_NONE) continue;
        const u32* art = g_sprite[d.sprite];

        /* Anything much larger than the canvas is SCALED to its box rather than
           centred in it. A 34-cell boss drawn from a 14-pixel sprite centred
           would be a small creature floating in a large hitbox, which is the
           one thing a boss must not look like. Everything ordinary keeps the
           centred path, where the art overhanging the box is what gives wings
           and antennae something to be. */
        const bool scaled = d.w > SPR_W + 6 || d.h > SPR_H + 6;
        const int ox = scaled ? (int)e.x - camX : (int)e.x + (d.w - SPR_W) / 2 - camX;
        const int oy = scaled ? (int)e.y - camY : (int)e.y + (d.h - SPR_H) / 2 - camY;
        const int stepX = scaled ? d.w : SPR_W, stepY = scaled ? d.h : SPR_H;
        for (int qy = 0; qy < stepY; ++qy) {
            const int sy = scaled ? qy * SPR_H / stepY : qy;
            for (int qx = 0; qx < stepX; ++qx) {
                const int sx = scaled ? qx * SPR_W / stepX : qx;
                int motionX, motionY;
                entityPixelMotion(e, i, sx, sy, &motionX, &motionY);
                const int vx = ox + qx + motionX;
                const int vy = oy + qy + motionY;
                if (vx < 0 || vx >= VIEW_CELLS_W) continue;
                if (vy < 0 || vy >= VIEW_CELLS_H) continue;
                /* Mirrored by facing, so a creature walking left looks left.
                   Read from the far column rather than writing to it, so the
                   bounds test above still governs where the pixel lands. */
                const u32 c = art[sy * SPR_W + (e.facing < 0 ? SPR_W - 1 - sx : sx)];
                if (!c) continue;
                u32 out = c;
                if (lit) out = shadeColor(out, viewShade(vx, vy));
                /* Hit flash goes on AFTER the shading, or a creature struck in
                   a dark cave would flash dark grey and the one piece of
                   feedback that says "you hit it" would be invisible exactly
                   where combat happens. */
                /* The wind-up glow, under the hit flash for the same reason
                   the hit flash sits over the shading: being struck is the more
                   urgent fact and must never be hidden by a telegraph. It
                   PULSES rather than holding a colour, because a steady tint on
                   a creature that is already red-brown reads as lighting, and a
                   thing that is about to cross the room at you has to read as
                   an event. */
                if (e.telegraph > 0) {
                    const int beat = (e.telegraph / 3) & 1;
                    const u32 warn = beat ? 0xFF9A50u : 0xFFE0A0u;
                    out = lerpColor(out, warn, 150);
                }
                if (e.hurtFlash > 0) out = 0xFFFFFF;
                px[vy * VIEW_CELLS_W + vx] = out;
            }
        }
    }

    /* Draw pickups after creatures: a freshly dropped chitin plate should be
       visible at a corpse's feet instead of hiding beneath its last frame. */
    for (int i = 0; i < MAX_PICKUPS; ++i) {
        const Pickup& p = g_pickups[i];
        if (!p.used) continue;
        const int x = (int)p.x - camX, y = (int)p.y - camY;
        if (x < 1 || x >= VIEW_CELLS_W - 1 || y < 1 || y >= VIEW_CELLS_H - 1) continue;
        u32 c = ITEMS[p.item].colour;
        if (lit) c = shadeColor(c, viewShade(x, y));
        px[y * VIEW_CELLS_W + x] = c;
        px[y * VIEW_CELLS_W + x - 1] = c;
        px[y * VIEW_CELLS_W + x + 1] = c;
        px[(y - 1) * VIEW_CELLS_W + x] = c;
        px[(y + 1) * VIEW_CELLS_W + x] = c;
    }
}
