#include "entity.h"
#include "sprite.h"
#include "light.h"
#include "projectile.h"
#include "worldgen.h"
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
    /* name       w   h  hp dmg  cd   speed accel  fly layers night | shotEvery dmg spd standOff boss | drop min max sprite egg */
    { "none",      0,  0,  0,   0,   0, 0.00f, 0.00f, false, 0,  false,   0, 0, 0.0f, 0.0f, false, ITEM_NONE,        0, 0, SPR_NONE,  0x000000 },

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
    { "Rock Mite",12,  9, 18,   6,  36, 0.34f, 0.05f, false, 1,  true,    0, 0, 0.0f, 0.0f, false, (ItemId)MAT_CHITIN, 1, 2, SPR_MITE,  0x8E7758 },

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
    { "Cinder Moth",9,  7, 10,   4,  30, 0.52f, 0.09f, true,  1,  true,    0, 0, 0.0f, 0.0f, false, (ItemId)MAT_COAL,   1, 2, SPR_MOTH,  0xE0561C },

    /* --- drip slime --------------------------------------------------------
       The corroder, and the slowest thing in the game: it is not a chase, it is
       something you have to deal with or route around. What makes it dangerous
       is the trail -- see slimeTick -- because acid outlives the creature and
       the puddle it leaves in a corridor is still there on the way back.

       Introduces acid a whole layer above where acid pockets generate, so the
       material is familiar before the terrain is full of it. Drops it too,
       which is the only way to get any in layer 1. */
    { "Drip Slime",11,  8, 24,   5,  40, 0.20f, 0.04f, false, 1,  false,   0, 0, 0.0f, 0.0f, false, (ItemId)MAT_ACID,   1, 3, SPR_SLIME, 0x6FA23C },

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
    { "Husk",     11, 22, 46,  11,  34, 0.42f, 0.06f, false, 1,  true,    0, 0, 0.0f, 0.0f, false, (ItemId)MAT_CHITIN, 1, 3, SPR_HUSK,  0x6E7A52 },

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
    { "Bat",       9,  7, 12,   7,  26, 1.35f, 0.055f, true, 1,  true,    0, 0, 0.0f, 0.0f, false, (ItemId)MAT_CHITIN, 1, 1, SPR_BAT,   0x6A4C68 },

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
    { "Spitter",  10, 12, 22,   5,  30, 0.26f, 0.05f, false, 1,  false,  95, 9, 4.7f, 90.0f, false, (ItemId)MAT_CHITIN, 1, 2, SPR_SPITTER, 0x8A5A3A },

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
    { "Brood Mother", 34, 24, 900, 16, 26, 0.55f, 0.05f, false, 0, false,  0, 0, 0.0f, 0.0f, true,  ITEM_FORGE_CORE, 1, 1, SPR_BROOD, 0xB04838 },
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

static void pickupTick(World& w, const Player& player, Inventory& inv) {
    static const float PICKUP_RADIUS = 20.0f;
    for (int i = 0; i < MAX_PICKUPS; ++i) {
        Pickup& p = g_pickups[i];
        if (!p.used) continue;

        /* Nothing is collected without someone alive to collect it. That covers
           the obvious case of a corpse hoovering up its own drops, and the less
           obvious one of the character being switched off entirely -- see the
           stand-in in the main loop, which stands where the camera is and would
           otherwise pull every loose item on screen into the pack. */
        const float dx = player.centreX() - p.x, dy = player.centreY() - p.y;
        if (player.alive && dx * dx + dy * dy <= PICKUP_RADIUS * PICKUP_RADIUS) {
            const int left = inv.add(p.item, p.count);
            p.count = (i16)left;
            if (p.count == 0) { p.used = false; continue; }
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
    if (d.dropItem != ITEM_NONE && d.dropMax > 0) {
        int n = d.dropMin + (int)(rngNext() % (u32)(d.dropMax - d.dropMin + 1));
        const int cx = (int)e.centreX(), cy = (int)e.centreY();
        /* Chitin is a collectible, not falling-sand debris. It therefore
           cannot get caught on a torch, and comes to the player like a classic
           game drop once they are nearby. Other drops retain their existing
           physical-cell behavior (especially the Forge Core). */
        if (d.dropItem == (ItemId)MAT_CHITIN && pickupSpawn(d.dropItem, n, (float)cx, (float)cy)) {
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

static void miteTick(World& w, Entity& e, const Player& p) {
    const EntityDef& d = ENT_DEFS[e.type];
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

static void mothTick(World& w, Entity& e, const Player& p) {
    const EntityDef& d = ENT_DEFS[e.type];

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

    float wantX, wantY;
    if (best >= 0) { wantX = (float)DX8[best]; wantY = (float)DY8[best]; }
    else {
        wantX = p.centreX() - e.centreX();
        wantY = p.centreY() - e.centreY();
        const float len = sqrtf(wantX * wantX + wantY * wantY);
        if (len > 0.01f) { wantX /= len; wantY /= len; }
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
    const float toward = p.centreX() - e.centreX();
    if (toward > 1.0f)      e.facing = 1;
    else if (toward < -1.0f) e.facing = -1;
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

/* Walk toward the player, or away to hold a standoff. Shared by everything that
   moves along the ground, because "which way is the player" is the same
   question however the creature answers it. */
static void groundChase(Entity& e, const Player& p, float speed, float accel,
                        float standOff) {
    const float dx = p.centreX() - e.centreX();
    const float adx = dx < 0 ? -dx : dx;
    float want = dx > 0 ? 1.0f : -1.0f;
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
    groundChase(e, p, d.speed, d.accel, 0.0f);
    /* Hops when it is up against something and the player is above. The whole
       creature is "it does not stop", and a zombie permanently stuck on a
       one-cell lip is a zombie that stops. */
    if (e.onGround && e.vx == 0.0f && p.centreY() < e.centreY()) e.vy = -2.2f;
}

/* --- the bat: why it misses -------------------------------------------------
   It picks a heading, commits to it for a stretch of frames, and cannot turn
   fast enough to fix a bad one. That is the whole trick, and it is worth being
   explicit that the overshoot is NOT emergent from being fast: a fast creature
   that re-aims every frame tracks you perfectly and is simply unavoidable.

   aimHold is the commitment. While it runs, the bat flies at a point it decided
   on earlier -- so if you move after it commits, it arrives where you WERE,
   sails past, and has to come round again. */
static void batTick(Entity& e, const Player& p) {
    const EntityDef& d = ENT_DEFS[e.type];
    if (--e.aimHold <= 0) {
        /* Re-aim at where the player is RIGHT NOW, then stop looking. The
           jitter stops two bats in one room flying as a matched pair. */
        e.aimX = p.centreX() + (float)((int)(rngNext() % 41u) - 20);
        e.aimY = p.centreY() + (float)((int)(rngNext() % 41u) - 20);
        e.aimHold = 34 + (int)(rngNext() % 26u);
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
   keeps arriving -- rather than the branching of a decision tree. */
static void broodTick(World& w, Entity& e, const Player& p) {
    const EntityDef& d = ENT_DEFS[e.type];
    const bool wounded = e.hp * 2 <= d.hp;
    e.phase = wounded ? 1 : 0;

    /* actTimer counts down to the next charge; while it is NEGATIVE the charge
       is in progress and the creature is committed. Committing is what makes a
       charge dodgeable -- a boss that could abort mid-lunge would only be a
       faster walker. */
    if (--e.actTimer <= -CHARGE_FRAMES) e.actTimer = wounded ? 130 : 210;

    const bool charging = e.actTimer < 0;
    const float speed = charging ? d.speed * 3.4f : d.speed;
    groundChase(e, p, speed, charging ? d.accel * 3.0f : d.accel, 0.0f);
    if (e.onGround && e.vx == 0.0f && p.centreY() < e.centreY()) e.vy = -2.6f;

    /* Chews rock like its young, but across its whole face -- it is 34 cells
       wide and should read as going THROUGH a wall rather than nibbling it. */
    const int ahead = e.facing > 0 ? e.right() + 1 : e.left() - 1;
    if (ahead > PLAY_X0 && ahead < PLAY_X1) {
        for (int y = e.top(); y <= e.bottom(); ++y) {
            if (y < PLAY_Y0 || y > PLAY_Y1) continue;
            const u8 m = w.at(ahead, y).mat;
            if (m == MAT_EMPTY || g_matStrength[m] > STR_ROCK) continue;
            w.setCell(ahead, y, MAT_EMPTY);
        }
    }

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

void entTick(World& w, Player& p, Inventory& inv) {
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        Entity& e = g_entities[i];
        if (e.type == ENT_NONE) continue;
        if (e.hp <= 0) { entDie(w, e); continue; }

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
        case ENT_BAT:     batTick(e, p);        break;
        case ENT_SPITTER: spitterTick(w, e, p); break;
        case ENT_BROOD:   broodTick(w, e, p);   break;
        default: break;
        }

        if (!d.flies) {
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
            const int dmg = imax(1, d.touchDamage - g_inv.armour());
            p.damage((float)dmg);
            p.hurtFlash = 10;
            e.touchTimer = d.touchCooldown;
        }

        if (e.hp <= 0) entDie(w, e);
    }
    pickupTick(w, p, inv);
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
static const int SPAWN_COOL = 45;
/* Brightness at or below which a site counts as dark. Torchlight is far above
   this, so a lit corridor is genuinely clear. */
static const int SPAWN_DARK  = 40;
/* Cells of clearance kept around the player, so nothing appears in your lap
   even if the camera happens to be looking elsewhere. Comfortably more than
   half the view's height. */
static const int SPAWN_MIN_DIST = 150;

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

void entSpawnTick(World& w, const Player& p, int camX, int camY) {
    if (g_spawnCool > 0) { --g_spawnCool; return; }
    if (entSpawnedCount() >= ENT_MAX_ALIVE) return;

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
        if (g_lightOn && lightRow(ly)[lx] > SPAWN_DARK) continue;

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
        const int bx = x - d.w / 2, by = y - d.h / 2;
        if (solidBox(w, bx, by, d.w, d.h)) continue;
        if (liquidBox(w, bx, by, d.w, d.h)) continue;
        if (!d.flies && !solidBox(w, bx, by + d.h, d.w, 1, SOLID_FLOOR)) continue;

        if (entSpawn(w, type, (float)x, (float)y) >= 0) {
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
void entDraw(u32* px, int camX, int camY, bool lit) {
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        const Entity& e = g_entities[i];
        if (!e.alive()) continue;
        const EntityDef& d = ENT_DEFS[e.type];
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
            const int vy = oy + qy;
            if (vy < 0 || vy >= VIEW_CELLS_H) continue;
            for (int qx = 0; qx < stepX; ++qx) {
                const int sx = scaled ? qx * SPR_W / stepX : qx;
                const int vx = ox + qx;
                if (vx < 0 || vx >= VIEW_CELLS_W) continue;
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
