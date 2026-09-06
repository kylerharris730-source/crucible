/* --- the Censer: a boss with parts -------------------------------------------

   Asked for: "then the boss, i want it to be bigger, and multi part, like a lot
   of terraria bosses, we have the ligature system, lets make it creepy too, a
   lot of noita bosses have this creepy spidery thing going on."

   Bigger is a number in a table and needs no harness. MULTI-PART is a structure,
   and it is the whole reason this file exists: the Brood Mother is a charge and
   the Widow is a chase, but both are one target and a health bar, and a fight
   with an ORDER to it is a different thing that can break in ways a single
   creature cannot.

   Eight properties. The middle four are the multi-part claim and the last two
   are the failure modes that would make it unfinishable or absurd:

     it is bigger than the boss above it            (the easy half of the ask)
     summoning it brings four limbs with it         (it arrives assembled)
     the limbs hold station on the body             (parts, not an escort)
     the body shrugs off damage while they live     (the fight has an order)
     and stops shrugging once they are gone         (the order pays off)
     killing the body kills the limbs               (no orphans in the arena)
     the limbs never come back                      (or it cannot be finished)
     beating it is recorded, and opens no seal      (there is no layer 4)

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "player.h"
#include "projectile.h"
#include "craft.h"
#include "multiplayer.h"
#include <stdio.h>
#include <math.h>

static World g_testWorld;
static const int CX = 1400, CY = 5000;
static const int FLOOR = CY + 60;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static void fill(World& w, int x0, int y0, int x1, int y1, u8 m) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1)
                w.setCell(x, y, m);
}

/* An arena big enough for a 56-cell creature and its limbs. */
static int arena(World& w) {
    w.reset();
    projClear();
    fill(w, CX - 400, CY - 200, CX + 400, FLOOR + 40, MAT_STONE);
    fill(w, CX - 380, CY - 180, CX + 380, FLOOR,      MAT_EMPTY);
    w.setLiveWindow(CX - 420, CY - 220, CX + 420, FLOOR + 60);
    Player& p = g_player;
    p.reset((float)(CX - 200), (float)(FLOOR - PLAYER_H / 2));
    p.alive = true; p.hp = PLAYER_HP_MAX;
    entReset();
    return entSpawn(w, ENT_CENSER, (float)(CX + 120), (float)(FLOOR - 30));
}

static void run(World& w, int frames) {
    Player& p = g_player;
    for (int f = 0; f < frames; ++f) {
        p.x = (float)(CX - 200);
        p.y = (float)(FLOOR - PLAYER_H);
        p.alive = true; p.hp = PLAYER_HP_MAX;
        entTick(w, p, g_inv);
        projUpdate(w);
    }
}

static int limbsAlive(int core) {
    int n = 0;
    for (int i = 0; i < MAX_ENTITIES; ++i)
        if (g_entities[i].type == ENT_CENSER_LIMB && g_entities[i].alive() &&
            g_entities[i].home == (i16)core) ++n;
    return n;
}

static void killLimbs(int core) {
    for (int i = 0; i < MAX_ENTITIES; ++i)
        if (g_entities[i].type == ENT_CENSER_LIMB && g_entities[i].home == (i16)core)
            g_entities[i].hp = 0;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();
    World& w = g_testWorld;

    /* --- 1. bigger ------------------------------------------------------- */
    {
        const EntityDef& c = ENT_DEFS[ENT_CENSER];
        const EntityDef& v = ENT_DEFS[ENT_WIDOW];
        printf("Censer %dx%d %d hp, Widow %dx%d %d hp\n",
               c.w, c.h, c.hp, v.w, v.h, v.hp);
        check(c.w > v.w && c.h > v.h && c.hp > v.hp,
              "the Censer is bigger than the boss above it");
        check(bossBitOf(ENT_CENSER) == BOSS_LAYER3,
              "and owns layer 3's bit");
        /* Its limb is furniture, not a boss: no bit, no egg, no layer. */
        const EntityDef& l = ENT_DEFS[ENT_CENSER_LIMB];
        check(!l.isBoss && l.eggItem == ITEM_NONE && l.layerMask == 0,
              "and its limb is a part, not a creature that spawns on its own");
    }

    /* --- 2 & 3. it arrives assembled, and the parts hold station ---------- */
    int core = -1;
    {
        core = arena(w);
        if (core < 0) { fprintf(stderr, "could not place the Censer\n"); return 2; }
        run(w, 30);
        printf("30 frames after summoning: %d limbs\n", limbsAlive(core));
        check(limbsAlive(core) == 4, "summoning it brings four limbs with it");

        /* Station-keeping, measured while the body MOVES: a limb that merely
           spawned in the right place would pass a check taken at rest. */
        run(w, 400);
        float worst = 0.0f;
        for (int i = 0; i < MAX_ENTITIES; ++i) {
            const Entity& l = g_entities[i];
            if (l.type != ENT_CENSER_LIMB || !l.alive() || l.home != (i16)core) continue;
            const float dx = l.centreX() - g_entities[core].centreX();
            const float dy = l.centreY() - g_entities[core].centreY();
            const float d = sqrtf(dx * dx + dy * dy);
            if (d > worst) worst = d;
        }
        printf("after 400 frames of the body walking: furthest limb %.0f cells\n",
               worst);
        check(limbsAlive(core) == 4 && worst < 90.0f,
              "and they stay with it rather than wandering off");
    }

    /* --- 3b. and they hang STEADILY ---------------------------------------
       Reported from play: "make the orbs around the censer move more smoothly
       and slowly, they bob around a lot now."

       They did, and by construction: the steering was a pure spring --
       acceleration proportional to displacement, with nothing taking energy out
       -- which is the textbook definition of a thing that oscillates forever. A
       limb pulled toward its station arrives with all the speed it built up
       getting there, sails past, and comes back.

       Measured as the SPREAD of each limb's distance to its post once the boss
       has settled: a damped limb sits at a roughly constant offset, and a
       springy one swings through a range. The body is held still for this, so
       what is measured is the limb's own behaviour and not the boss walking. */
    {
        core = arena(w);
        if (core < 0) return 2;
        run(w, 200);                       /* let them arrive */
        Entity& body = g_entities[core];

        float lo[4] = { 1e9f, 1e9f, 1e9f, 1e9f };
        float hi[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float worstStep = 0.0f;
        float prevX[4] = {0,0,0,0}, prevY[4] = {0,0,0,0};
        bool  seen[4] = { false, false, false, false };
        for (int t = 0; t < 400; ++t) {
            /* Pinned. A moving body is what the limbs are supposed to trail
               behind; holding it still isolates the spring from the chase. */
            body.vx = body.vy = 0.0f;
            const float bx = body.centreX(), by = body.centreY();
            run(w, 1);
            body.x = bx - (float)ENT_DEFS[ENT_CENSER].w * 0.5f;
            body.y = by - (float)ENT_DEFS[ENT_CENSER].h * 0.5f;
            for (int i = 0; i < MAX_ENTITIES; ++i) {
                const Entity& l = g_entities[i];
                if (l.type != ENT_CENSER_LIMB || !l.alive() ||
                    l.home != (i16)core) continue;
                const int k = l.phase >= 0 && l.phase < 4 ? l.phase : 0;
                const float dx = l.centreX() - body.centreX();
                const float dy = l.centreY() - body.centreY();
                const float d = sqrtf(dx * dx + dy * dy);
                if (d < lo[k]) lo[k] = d;
                if (d > hi[k]) hi[k] = d;
                if (seen[k]) {
                    const float step = fabsf(l.centreX() - prevX[k]) +
                                       fabsf(l.centreY() - prevY[k]);
                    if (step > worstStep) worstStep = step;
                }
                prevX[k] = l.centreX(); prevY[k] = l.centreY();
                seen[k] = true;
            }
        }
        float widest = 0.0f;
        for (int k = 0; k < 4; ++k)
            if (seen[k] && hi[k] - lo[k] > widest) widest = hi[k] - lo[k];
        printf("limbs over 400 settled frames: widest swing %.1f cells, "
               "fastest step %.2f cells/frame\n", widest, worstStep);
        check(widest < 6.0f, "a limb hangs at its station rather than swinging");
        check(worstStep < 0.9f, "and moves slowly enough to read as hanging");
    }

    /* --- 3c. it gets to you ------------------------------------------------
       Reported from play: "cant hit me but isnt adjusting, should be able to
       jump or pass through blocks if it needs to."

       Two separate obstacles, and a boss this size has to beat both. A WALL is
       in front of its face and is answered by ploughing; a LEDGE is under its
       feet and is answered by jumping. It had neither, and it was being routed
       by a flow field whose tallest size class is 24 cells against its own 56 --
       so the advice it was following was about a creature less than half its
       size.

       Measured as ground covered toward a player it cannot reach by walking. */
    {
        struct Case { const char* what; bool wall; bool ledge; };
        const Case cases[2] = { { "a wall in the way", true, false },
                                { "a ledge to climb", false, true } };
        for (int c = 0; c < 2; ++c) {
            core = arena(w);
            if (core < 0) return 2;
            Player& p = g_player;
            /* The player to the LEFT, the boss to the right, and the obstacle
               between them. */
            if (cases[c].wall)
                fill(w, CX - 40, CY - 180, CX - 20, FLOOR, MAT_STONE);
            /* STRATUM, not stone, and the choice is what makes this test the
               HOP rather than the plough a second time. Ploughing is strength
               gated -- it cuts stone and stops at a layer barrier -- so a stone
               step is simply cut through and says nothing about jumping. A
               sealed one cannot be cut at all, so the only way past it is over.

               Thirty cells: inside the new hop's reach (3.6 gives v*v/2g = 36)
               and well outside the old one's 16. An earlier version used a
               forty-cell stone plateau and filled the boss's own spawn point
               with it, which entombed the creature and measured nothing at
               all -- the gap did not change by a single cell in either
               build. */
            if (cases[c].ledge)
                fill(w, CX - 40, FLOOR - 30, CX - 20, FLOOR, MAT_STRATUM);
            run(w, 30);
            const float start = g_entities[core].centreX() - p.centreX();
            run(w, 600);
            const float end = g_entities[core].centreX() - p.centreX();
            printf("with %-18s gap %.0f -> %.0f cells\n",
                   cases[c].what, start, end);
            /* It has to REACH them, not merely make progress. At "start - 40"
               the wall case passed on the old build, which walked up to the
               wall and stopped 208 cells short -- ninety cells of approach is
               not the same as getting there. */
            check(end < 60.0f, cases[c].wall
                  ? "it goes through a wall rather than standing at it"
                  : "and climbs a ledge rather than standing under it");
        }
    }

    /* --- 4 & 5. the armour, and losing it -------------------------------- */
    /* The whole point of the structure. Same hit, twice, with the only
       difference being whether the limbs are alive. */
    {
        core = arena(w);
        if (core < 0) return 2;
        run(w, 30);
        Entity& body = g_entities[core];

        body.hp = 2000;
        const int before = body.hp;
        entDamageDisc((int)body.centreX(), (int)body.centreY(), 6, 100);
        const int armoured = before - body.hp;

        killLimbs(core);
        run(w, 2);
        body.hp = 2000;
        entDamageDisc((int)body.centreX(), (int)body.centreY(), 6, 100);
        const int exposed = 2000 - body.hp;

        printf("a 100-damage hit: %d through the limbs, %d without them\n",
               armoured, exposed);
        check(armoured < exposed / 2,
              "the body shrugs off damage while a limb lives");
        check(exposed == 100, "and takes it in full once they are gone");
        /* Not zero, ever. An untouchable boss reads as a broken one, because
           nothing on screen distinguishes "armoured" from "not registering". */
        check(armoured > 0, "but a hit still registers, so it never looks broken");
    }

    /* --- 6 & 7. no orphans, and no respawns ------------------------------ */
    {
        core = arena(w);
        if (core < 0) return 2;
        run(w, 30);
        check(limbsAlive(core) == 4, "four limbs to start");

        /* Kill one. It must not come back -- a boss that replaces its parts
           faster than they can be cleared cannot be finished at all. */
        for (int i = 0; i < MAX_ENTITIES; ++i)
            if (g_entities[i].type == ENT_CENSER_LIMB &&
                g_entities[i].home == (i16)core) { g_entities[i].hp = 0; break; }
        run(w, 300);
        printf("300 frames after killing one limb: %d left\n", limbsAlive(core));
        check(limbsAlive(core) == 3, "a killed limb stays dead");

        /* And killing the body takes the rest with it. A limb whose parent is
           gone has nothing to hold station on and would otherwise sit in the
           arena forever looking like something you failed to kill. */
        g_entities[core].hp = 0;
        run(w, 10);
        int orphans = 0;
        for (int i = 0; i < MAX_ENTITIES; ++i)
            if (g_entities[i].type == ENT_CENSER_LIMB && g_entities[i].alive())
                ++orphans;
        printf("after the body died: %d limbs still standing\n", orphans);
        check(orphans == 0, "and killing the body clears its limbs");
    }

    /* --- 8. beating it is recorded, and opens nothing --------------------- */
    /* There is no layer 4. The reward is the drop, and the seal logic has to
       say so rather than falling through to layer 2's depth -- which is what a
       two-way test did before this boss existed. */
    {
        core = arena(w);
        if (core < 0) return 2;
        g_bossesBeaten = 0;
        g_entities[core].hp = 0;
        run(w, 4);
        printf("bosses beaten after killing it: 0x%X\n", (unsigned)g_bossesBeaten);
        check((g_bossesBeaten & BOSS_LAYER3) != 0, "beating it is remembered");
        check((g_bossesBeaten & (BOSS_LAYER1 | BOSS_LAYER2)) == 0,
              "and does not credit the two bosses above it");
        check(ENT_DEFS[ENT_CENSER].dropItem == ITEM_PYRE_CORE,
              "and it pays out a Pyre Core");
    }

    /* --- and it is reachable --------------------------------------------- */
    {
        bool hasRecipe = false;
        for (int r = 0; r < N_RECIPES; ++r)
            if (RECIPES[r].out == ITEM_CENSER_CALL) hasRecipe = true;
        check(hasRecipe, "there is a recipe that calls it");
        check(ITEMS[ITEM_CENSER_CALL].summons == ENT_CENSER,
              "and the summon summons it");
    }

    if (failures) {
        fprintf(stderr, "\n%d Censer check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
