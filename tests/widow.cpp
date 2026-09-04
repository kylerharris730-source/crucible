/* --- the Widow: eight legs and a mouthful of silk ----------------------------

   Asked for: "lets make the boss for layer 2, it should be a scary looking
   spider thats really big that has 8 legs it walks around with and hurts the
   player with, it can also spit webs that hurt sometimes".

   Layer 2 had five ordinary creatures and no capstone. The Brood Mother is a
   charge you dodge in an open room, and backing away from her works -- which is
   correct for her and would be fatal to repeat, because it means the answer to
   "boss" in this game is currently "walk backwards". This one takes the ground
   you walk backwards onto and covers it in silk.

   Seven properties. The first three are the request, and the last four are the
   things that stop the request from making an unfightable creature:

     it is big, and wider than it is tall        (a spider, not a tower)
     it hurts on contact, across that width      (the legs ARE the threat)
     it spits webs, and the webs hurt            (the request)
     a web is passable, never a wall             (it cannot seal you in)
     silk rots, so the arena clears              (a long fight is not punished)
     fire clears silk faster                     (there is counterplay)
     killing it opens the layer 2 seal           (it is worth fighting)

   The fourth is the one that matters most. A boss that can place solid cells at
   range can build a box around a player, and no amount of tuning fixes that.

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
#include "worldgen.h"
#include "multiplayer.h"
#include <stdio.h>
#include <math.h>

static World g_testWorld;
static const int CX = 1400, CY = 5000;
static const int FLOOR = CY + 50;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  %-54s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static void fill(World& w, int x0, int y0, int x1, int y1, u8 m) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1)
                w.setCell(x, y, m);
}

/* An arena: flat floor, the player at the left, the Widow `gap` to the right. */
static int arena(World& w, int gap) {
    w.reset();
    projClear();
    fill(w, CX - 200, CY - 90, CX + gap + 260, FLOOR + 40, MAT_STONE);
    fill(w, CX - 180, CY - 60, CX + gap + 240, FLOOR,      MAT_EMPTY);
    w.setLiveWindow(CX - 220, CY - 110, CX + gap + 280, FLOOR + 60);

    Player& p = g_player;
    p.reset((float)CX, (float)(FLOOR - PLAYER_H));
    p.alive = true; p.hp = PLAYER_HP_MAX;

    entReset();
    return entSpawn(w, ENT_WIDOW, (float)(CX + gap), (float)(FLOOR - 20));
}

/* Holds the player in place and runs the fight, so what is measured is the
   creature rather than a chase. */
static void run(World& w, int frames, bool holdPlayer = true) {
    Player& p = g_player;
    for (int f = 0; f < frames; ++f) {
        if (holdPlayer) {
            p.x = (float)CX;
            p.y = (float)(FLOOR - PLAYER_H);
            p.alive = true; p.hp = PLAYER_HP_MAX;
        }
        entTick(w, p, g_inv);
        projUpdate(w);
        w.step();
    }
}

static int countMat(const World& w, u8 m) {
    int n = 0;
    for (int y = CY - 90; y <= FLOOR + 10; ++y)
        for (int x = CX - 200; x <= CX + 500; ++x)
            if (w.at(x, y).mat == m) ++n;
    return n;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();
    World& w = g_testWorld;

    /* --- 1. it is big, and it is a spider's shape ------------------------- */
    {
        const EntityDef& d = ENT_DEFS[ENT_WIDOW];
        const EntityDef& brood = ENT_DEFS[ENT_BROOD];
        printf("Widow %dx%d, %d hp, %d contact  (Brood Mother %dx%d, %d hp, %d)\n",
               d.w, d.h, d.hp, d.touchDamage,
               brood.w, brood.h, brood.hp, brood.touchDamage);
        check(d.w > brood.w && d.h > brood.h, "bigger than the layer 1 boss");
        check(d.w > d.h, "and wider than it is tall, which a spider is");
        check(d.isBoss && bossBitOf(ENT_WIDOW) != 0,
              "it is a boss and it owns a bit to be remembered by");
        /* The guard on bossBitOf: a creature whose row says isBoss and which
           has no bit would beat you and not be recorded. */
        bool everyBossHasABit = true;
        for (int t = ENT_NONE + 1; t < ENT_COUNT; ++t)
            if (ENT_DEFS[t].isBoss && bossBitOf(t) == 0) everyBossHasABit = false;
        check(everyBossHasABit, "and every boss in the table owns one");
    }

    /* --- 2. the legs hurt, across the whole width ------------------------- */
    /* The Thresher's lesson at twice the size: the box is the reach, so a
       player level with the creature and well off its centre still gets hit.
       Tested at the edge of the box rather than at the middle, because the
       middle would pass on any creature with any hitbox at all. */
    {
        const int e = arena(w, 0);
        if (e < 0) { fprintf(stderr, "could not place the Widow\n"); return 2; }
        const EntityDef& d = ENT_DEFS[ENT_WIDOW];
        /* Stand the player just inside the far edge of its box. */
        Entity& widow = g_entities[e];
        widow.x = (float)CX - (float)d.w * 0.5f + 3.0f;
        widow.y = (float)(FLOOR - d.h);
        Player& p = g_player;
        p.reset((float)CX, (float)(FLOOR - PLAYER_H));
        p.alive = true; p.hp = PLAYER_HP_MAX;
        int hp = p.hp;
        for (int f = 0; f < 120; ++f) {
            widow.x = (float)CX - (float)d.w * 0.5f + 3.0f;
            widow.y = (float)(FLOOR - d.h);
            p.y = (float)(FLOOR - PLAYER_H);
            entTick(w, p, g_inv);
        }
        printf("standing at the edge of its span: %d -> %d hp\n", hp, p.hp);
        check(p.hp < hp, "the legs hurt you out at the edge of its reach");
    }

    /* --- 3. it spits webs, and they hurt ---------------------------------- */
    {
        const int e = arena(w, 80);
        if (e < 0) return 2;
        /* PEAK, not the count at the end. Silk decays, so a single sample
           catches only whatever happened to be mid-life at that instant -- the
           same reason the aqua regia harness reads a high-water mark. What is
           being asked is how much ground the creature covers, and that is the
           level this settles at, not the number standing at one arbitrary
           frame. */
        int silk = 0;
        for (int t = 0; t < 900; t += 15) {
            run(w, 15);
            const int n = countMat(w, MAT_WEB);
            if (n > silk) silk = n;
        }
        printf("after 900 frames at 80 cells: silk peaked at %d cells\n", silk);
        check(silk > 12, "it spits enough web to be worth walking round");
        check(g_matContactDamage[MAT_WEB] > 0.0f, "and standing in one hurts");
        /* It must not be free damage at any range. Silk that arrives through a
           wall is not an attack, it is weather. */
        check(ENT_DEFS[ENT_WIDOW].shotEvery > 0, "on a clock, not every frame");
    }

    /* --- 4. a web is never a wall ----------------------------------------- */
    /* THE one that matters. A boss placing solid cells at range can build a box
       round the player, and no tuning fixes that -- so silk is in
       g_matPassable, the same table the open door uses. */
    {
        w.reset();
        fill(w, CX - 60, CY - 60, CX + 60, FLOOR + 20, MAT_STONE);
        fill(w, CX - 40, CY,      CX + 40, FLOOR,      MAT_EMPTY);
        /* A wall of silk across the corridor, floor to ceiling, thicker than
           anything a volley could ever stack. */
        fill(w, CX + 4, CY, CX + 12, FLOOR - 1, MAT_WEB);
        w.setLiveWindow(CX - 80, CY - 20, CX + 80, FLOOR + 40);
        entReset();

        Player& p = g_player;
        p.reset((float)(CX - 20), (float)(FLOOR - PLAYER_H));
        p.alive = true; p.hp = PLAYER_HP_MAX;
        PlayerInput in;
        in.left = false; in.right = true; in.jump = false; in.down = false;
        const float x0 = p.centreX();
        for (int f = 0; f < 200; ++f) {
            p.hp = PLAYER_HP_MAX;      /* the damage is checked above, not here */
            p.update(w, in);
        }
        const float got = p.centreX() - x0;
        printf("walking into a nine-cell curtain of silk: moved %.0f cells\n",
               got);
        check(got > 20.0f, "you walk through silk rather than into it");
    }

    /* --- 5. silk rots -------------------------------------------------- */
    {
        w.reset();
        fill(w, CX - 60, CY - 60, CX + 60, FLOOR + 20, MAT_STONE);
        fill(w, CX - 40, CY,      CX + 40, FLOOR,      MAT_EMPTY);
        fill(w, CX - 20, FLOOR - 20, CX + 20, FLOOR - 1, MAT_WEB);
        w.setLiveWindow(CX - 80, CY - 20, CX + 80, FLOOR + 40);
        const int before = countMat(w, MAT_WEB);
        for (int f = 0; f < 2000; ++f) w.step();
        const int after = countMat(w, MAT_WEB);
        printf("silk left alone: %d -> %d cells in 2000 frames\n", before, after);
        check(after * 2 < before, "silk rots away rather than redecorating the arena");
    }

    /* --- 6. and fire clears it faster ------------------------------------- */
    {
        w.reset();
        fill(w, CX - 60, CY - 60, CX + 60, FLOOR + 20, MAT_STONE);
        fill(w, CX - 40, CY,      CX + 40, FLOOR,      MAT_EMPTY);
        fill(w, CX - 20, FLOOR - 20, CX + 20, FLOOR - 1, MAT_WEB);
        w.setLiveWindow(CX - 80, CY - 20, CX + 80, FLOOR + 40);
        const int before = countMat(w, MAT_WEB);
        /* One torch's worth of flame at one end. */
        fill(w, CX - 22, FLOOR - 10, CX - 21, FLOOR - 8, MAT_FIRE);
        for (int f = 0; f < 300; ++f) w.step();
        const int after = countMat(w, MAT_WEB);
        printf("silk with a flame in it: %d -> %d cells in 300 frames\n",
               before, after);
        check(after * 2 < before, "a flame is a real answer to a curtain of silk");
    }

    /* --- 7. killing it opens the way down --------------------------------- */
    /* The reward, and the reason to fight it at all. Same mechanism the Brood
       Mother uses on the layer above -- and it must be HER bit that stays hers,
       or beating one boss would credit you with the other. */
    {
        w.reset();
        for (int x = PLAY_X0; x <= PLAY_X1; ++x) g_stoneY[x] = CY - 200;
        const int seal = CY - 200 + LAYER2_DEPTH;
        if (seal < PLAY_Y1 - 20) {
            fill(w, CX - 40, seal - 4, CX + 40, seal + 4, MAT_STRATUM);
            w.setLiveWindow(CX - 60, seal - 40, CX + 60, seal + 40);
            int before = 0;
            for (int y = seal - 6; y <= seal + 6; ++y)
                for (int x = CX - 40; x <= CX + 40; ++x)
                    if (w.at(x, y).mat == MAT_STRATUM) ++before;
            g_bossesBeaten = 0;
            entReset();
            const int e = entSpawn(w, ENT_WIDOW, (float)CX, (float)(CY - 100));
            if (e >= 0) {
                g_entities[e].hp = 0;
                Player& p = g_player;
                p.reset((float)CX, (float)(CY - 120));
                entTick(w, p, g_inv);
            }
            int after = 0;
            for (int y = seal - 6; y <= seal + 6; ++y)
                for (int x = CX - 40; x <= CX + 40; ++x)
                    if (w.at(x, y).mat == MAT_STRATUM) ++after;
            printf("layer 2 seal after killing it: %d -> %d cells of stratum\n",
                   before, after);
            check((g_bossesBeaten & BOSS_LAYER2) != 0, "it is recorded as beaten");
            check((g_bossesBeaten & BOSS_LAYER1) == 0,
                  "and the Brood Mother is NOT credited with its death");
            check(after < before, "the layer 2 seal is opened");
        }
    }

    /* --- and it is reachable at all --------------------------------------- */
    /* A boss with no summon is a boss nobody meets. */
    {
        bool hasRecipe = false;
        for (int r = 0; r < N_RECIPES; ++r)
            if (RECIPES[r].out == ITEM_WIDOW_CALL) hasRecipe = true;
        check(hasRecipe, "there is a recipe that calls it");
        check(ITEMS[ITEM_WIDOW_CALL].summons == ENT_WIDOW,
              "and the summon summons it");
        check(ENT_DEFS[ENT_WIDOW].dropItem == ITEM_SILK_GLAND,
              "and beating it hands you something");
    }

    if (failures) {
        fprintf(stderr, "\n%d Widow check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
