#include "entity.h"
#include "sprite.h"
#include "light.h"
#include <string.h>
#include <math.h>

Entity g_entities[MAX_ENTITIES];

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
    /* name        w   h  hp  dmg  cd   speed accel  fly  layers night  drop            min max sprite      egg */
    { "none",      0,  0,  0,   0,   0, 0.00f, 0.00f, false, 0,  false, ITEM_NONE,        0, 0, SPR_NONE,  0x000000 },

    /* --- rock mite ---------------------------------------------------------
       The one that makes the first twenty minutes treacherous. Slow enough to
       outrun, tough enough that punching it is a bad idea, and it CHEWS ROCK --
       so a wall is a delay rather than a solution and sealing yourself in is
       not a strategy. That last property is the whole reason it exists: it is
       the cheapest possible way to make the world's solidity negotiable.

       Drops coal, which is not a joke about carbon so much as the most useful
       thing a layer 1 player can be handed. Coal is the fuel gate on the entire
       early ladder, and a creature that pays for the trip into a dark tunnel
       with the thing you went in for is a creature worth fighting. */
    { "Rock Mite",12,  9, 18,   6,  36, 0.34f, 0.05f, false, 1,  true,  (ItemId)MAT_COAL, 1, 2, SPR_MITE,  0x8E7758 },

    /* --- cinder moth -------------------------------------------------------
       Navigates to the hottest cell it can sense, which means it navigates to
       whatever you are smelting with. Teaching that archetype in layer 1, while
       the furnace is a small coal fire and losing it costs minutes rather than
       an hour, is the entire point of putting it this early.

       Fragile and fast: it should die to one good shot and be genuinely
       annoying to hit with a bad one. Drops glass -- wings fused by the heat it
       chases -- which matters because glass gates the Chemistry Bench and the
       Assembly Table, and until now the world's only source of it was one beach
       on one lake. */
    { "Cinder Moth",9,  7, 10,   4,  30, 0.52f, 0.09f, true,  1,  true,  (ItemId)MAT_GLASS,1, 2, SPR_MOTH,  0xE0561C },

    /* --- drip slime --------------------------------------------------------
       The corroder, and the slowest thing in the game: it is not a chase, it is
       something you have to deal with or route around. What makes it dangerous
       is the trail -- see slimeTick -- because acid outlives the creature and
       the puddle it leaves in a corridor is still there on the way back.

       Introduces acid a whole layer above where acid pockets generate, so the
       material is familiar before the terrain is full of it. Drops it too,
       which is the only way to get any in layer 1. */
    { "Drip Slime",11,  8, 24,   5,  40, 0.20f, 0.04f, false, 1,  false, (ItemId)MAT_ACID, 1, 3, SPR_SLIME, 0x6FA23C },
};

void entReset() { memset(g_entities, 0, sizeof(g_entities)); }

int entAliveCount() {
    int n = 0;
    for (int i = 0; i < MAX_ENTITIES; ++i) if (g_entities[i].alive()) ++n;
    return n;
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
   Drops go into the WORLD as cells rather than straight into the pack, which is
   a deliberate difference from how mining works. digInto banks its yield
   because you asked for it and are standing there; a creature dies wherever it
   happened to be, often over a drop or in a pool, and teleporting its remains
   into your inventory from across a cavern would make loot something that
   happens TO you rather than something you collect.

   Placed only into empty cells, walking outward from the body, so a drop never
   overwrites the world and never vanishes into a wall. */
static void entDie(World& w, Entity& e) {
    const EntityDef& d = ENT_DEFS[e.type];
    if (d.dropItem != ITEM_NONE && d.dropMax > 0) {
        int n = d.dropMin + (int)(rngNext() % (u32)(d.dropMax - d.dropMin + 1));
        const int cx = (int)e.centreX(), cy = (int)e.centreY();
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

void entTick(World& w, Player& p) {
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        Entity& e = g_entities[i];
        if (e.type == ENT_NONE) continue;
        if (e.hp <= 0) { entDie(w, e); continue; }

        const EntityDef& d = ENT_DEFS[e.type];
        if (e.hurtFlash > 0) --e.hurtFlash;
        if (e.touchTimer > 0) --e.touchTimer;

        switch (e.type) {
        case ENT_MITE:  miteTick(w, e, p);  break;
        case ENT_MOTH:  mothTick(w, e, p);  break;
        case ENT_SLIME: slimeTick(w, e, p); break;
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
   Twenty probes against a cap of ten live creatures fills a dark cavern over a
   few seconds, which is the pace this wants -- somewhere gradually becoming
   occupied, not an ambush materialising. */
static const int SPAWN_TRIES = 20;
/* Brightness at or below which a site counts as dark. Torchlight is far above
   this, so a lit corridor is genuinely clear. */
static const int SPAWN_DARK  = 40;
/* Cells of clearance kept around the player, so nothing appears in your lap
   even if the camera happens to be looking elsewhere. Comfortably more than
   half the view's height. */
static const int SPAWN_MIN_DIST = 150;

void entSpawnTick(World& w, const Player& p, int camX, int camY) {
    if (entAliveCount() >= ENT_MAX_ALIVE) return;

    for (int attempt = 0; attempt < SPAWN_TRIES; ++attempt) {
        /* Drawn from the padded light rectangle, then rejected if it lands on
           screen -- rather than sampling the margin's four arms directly, which
           needs a case per arm and biases toward the corners where two arms
           overlap. */
        const int lx = (int)(rngNext() % (u32)LIGHT_W) - LIGHT_MARGIN;
        const int ly = (int)(rngNext() % (u32)LIGHT_H) - LIGHT_MARGIN;
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
        if (!d.flies && !solidBox(w, bx, by + d.h, d.w, 1, SOLID_FLOOR)) continue;

        if (entSpawn(w, type, (float)x, (float)y) >= 0) return;   /* one a frame */
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

        const int ox = (int)e.x + (d.w - SPR_W) / 2 - camX;
        const int oy = (int)e.y + (d.h - SPR_H) / 2 - camY;
        for (int sy = 0; sy < SPR_H; ++sy) {
            const int vy = oy + sy;
            if (vy < 0 || vy >= VIEW_CELLS_H) continue;
            for (int sx = 0; sx < SPR_W; ++sx) {
                const int vx = ox + sx;
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
}
