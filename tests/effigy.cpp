/* --- the Effigy: the last boss ------------------------------------------------

   Asked for: "then lets do a final boss, i want it to be bigger. multi part,
   bigger than any of the other bosses."

   Bigger is a number in a table and needs one line here. What needs a harness
   is the CLAIM the fight makes, which is that its three parts each close off
   one of the three answers a player has to a slow enemy:

     stand close   -- the arms reach for you
     stand away    -- the crown drops fire on you
     keep moving   -- the ground erupts where you were standing

   Each of those is measured against the thing it is supposed to prevent, not
   against itself: an arm is tested by whether standing next to the body is
   survivable, not by whether it moves.

   Nine properties:

     it is bigger than every other boss           (the ask, stated)
     summoning it brings three parts              (it arrives assembled)
     two arms and one crown, not three of a kind  (the structure)
     the body shrugs off damage while any lives   (the fight has an order)
     and stops shrugging once they are gone       (the order pays off)
     an arm leaves its post to reach you          (standing close is answered)
     the crown fires from outside the arms' reach (standing away is answered)
     the ground opens under a player who stops    (standing still is answered)
     killing the body clears its parts            (no orphans in the arena)
     beating it is recorded, and opens no seal    (there is no layer 4)

   Compile with every src/ cpp file except main.cpp. No socket, no window. Do not
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
static const int FLOOR = CY + 120;

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

/* An arena with room for an 88-cell creature and a player running away from
   it. `back` is floor behind the player and has to exceed anything a fleeing
   one can cover -- the Censer's harness was caught twice reporting a boss as
   giving up when the player had run off the end of the world. */
static int arena(World& w, int back = 500) {
    w.reset();
    projClear();
    fill(w, CX - back, CY - 200, CX + 500, FLOOR + 40, MAT_STONE);
    fill(w, CX - back + 20, CY - 180, CX + 480, FLOOR, MAT_EMPTY);
    w.setLiveWindow(CX - back - 20, CY - 220, CX + 520, FLOOR + 60);
    Player& p = g_player;
    p.reset((float)(CX - 240), (float)(FLOOR - PLAYER_H / 2));
    p.alive = true; p.hp = PLAYER_HP_MAX;
    entReset();
    return entSpawn(w, ENT_EFFIGY, (float)(CX + 160), (float)(FLOOR - 50));
}

/* Runs the world with the player pinned where the caller put them. Pinned
   rather than driven, so what is measured is the creature. */
static void run(World& w, int frames, float px, float py) {
    Player& p = g_player;
    for (int f = 0; f < frames; ++f) {
        p.x = px; p.y = py;
        p.alive = true; p.hp = PLAYER_HP_MAX;
        entTick(w, p, g_inv);
        projUpdate(w);
    }
}

static int partsAlive(int core, int type) {
    int n = 0;
    for (int i = 0; i < MAX_ENTITIES; ++i)
        if (g_entities[i].type == type && g_entities[i].alive() &&
            g_entities[i].home == (i16)core) ++n;
    return n;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();
    World& w = g_testWorld;

    /* --- 1. bigger than everything ---------------------------------------- */
    {
        const EntityDef& e = ENT_DEFS[ENT_EFFIGY];
        int widest = 0, tallest = 0, biggest = 0, toughest = 0;
        for (int t = ENT_NONE + 1; t < ENT_COUNT; ++t) {
            if (t == ENT_EFFIGY || !ENT_DEFS[t].isBoss) continue;
            if (ENT_DEFS[t].w > widest)  widest  = ENT_DEFS[t].w;
            if (ENT_DEFS[t].h > tallest) tallest = ENT_DEFS[t].h;
            if (ENT_DEFS[t].w * ENT_DEFS[t].h > biggest)
                biggest = ENT_DEFS[t].w * ENT_DEFS[t].h;
            if (ENT_DEFS[t].hp > toughest) toughest = ENT_DEFS[t].hp;
        }
        printf("The Effigy %dx%d (%d cells) %d hp; biggest other boss "
               "%d cells, %d hp\n", e.w, e.h, e.w * e.h, e.hp, biggest, toughest);
        check(e.w * e.h > biggest && e.h > tallest && e.w >= widest,
              "it is bigger than every other boss");
        check(e.hp > toughest, "and tougher");
        check(bossBitOf(ENT_EFFIGY) == BOSS_FINAL, "and owns the final bit");
    }

    /* --- 2. it arrives assembled, and the parts are DIFFERENT ------------- */
    int core = -1;
    {
        core = arena(w);
        if (core < 0) { fprintf(stderr, "could not place the Effigy\n"); return 2; }
        run(w, 40, (float)(CX - 240), (float)(FLOOR - PLAYER_H));
        const int arms = partsAlive(core, ENT_EFFIGY_ARM);
        const int crowns = partsAlive(core, ENT_EFFIGY_CROWN);
        printf("40 frames after summoning: %d arms, %d crown\n", arms, crowns);
        check(arms == 2 && crowns == 1, "summoning it brings two arms and a crown");
        /* Structure, not decoration: the parts have to differ in what they do,
           or this is the Censer's four orbs with a smaller count. */
        check(ENT_DEFS[ENT_EFFIGY_ARM].shotEvery == 0 &&
              ENT_DEFS[ENT_EFFIGY_CROWN].shotEvery > 0,
              "and the arm is melee where the crown is artillery");
        check(!ENT_DEFS[ENT_EFFIGY_ARM].isBoss &&
              ENT_DEFS[ENT_EFFIGY_ARM].eggItem == ITEM_NONE,
              "a part is furniture: no bit of its own, and no egg");
    }

    /* --- 3. the fight has an order ---------------------------------------- */
    {
        core = arena(w);
        if (core < 0) return 2;
        run(w, 40, (float)(CX - 240), (float)(FLOOR - PLAYER_H));
        Entity& body = g_entities[core];
        /* Through the world's own damage path rather than by calling the
           armour rule directly -- the point is that a shot landing on the body
           is reduced, not that a static function returns what it returns. */
        body.hp = 4000;
        const int before = body.hp;
        entDamageDisc((int)body.centreX(), (int)body.centreY(), 6, 120);
        const int armoured = before - body.hp;

        for (int i = 0; i < MAX_ENTITIES; ++i)
            if ((g_entities[i].type == ENT_EFFIGY_ARM ||
                 g_entities[i].type == ENT_EFFIGY_CROWN) &&
                g_entities[i].home == (i16)core) g_entities[i].hp = 0;
        run(w, 4, (float)(CX - 240), (float)(FLOOR - PLAYER_H));
        body.hp = 4000;
        entDamageDisc((int)body.centreX(), (int)body.centreY(), 6, 120);
        const int exposed = 4000 - body.hp;

        printf("a 120-damage hit: %d through the parts, %d without them\n",
               armoured, exposed);
        check(armoured < exposed / 4,
              "the body shrugs off damage while a part lives");
        check(exposed == 120, "and takes it in full once they are gone");
        check(armoured > 0, "but a hit still registers, so it never looks broken");
    }

    /* --- 4. standing close is answered ------------------------------------
       An arm has to LEAVE its post. Measured as how close a part gets to a
       player standing beside the body, against where its station is: a limb
       that merely orbits would sit at its shoulder offset and never come
       nearer than that whatever the player did. */
    {
        core = arena(w);
        if (core < 0) return 2;
        const float px = g_entities[core].centreX() - 70.0f;
        const float py = (float)(FLOOR - PLAYER_H);
        run(w, 40, px, py);
        float closest = 1e9f;
        for (int f = 0; f < 200; ++f) {
            run(w, 1, px, py);
            for (int i = 0; i < MAX_ENTITIES; ++i) {
                const Entity& a = g_entities[i];
                if (a.type != ENT_EFFIGY_ARM || !a.alive()) continue;
                const float dx = a.centreX() - g_player.centreX();
                const float dy = a.centreY() - g_player.centreY();
                const float d = sqrtf(dx * dx + dy * dy);
                if (d < closest) closest = d;
            }
        }
        printf("an arm reached to within %.0f cells of a player alongside\n",
               closest);
        check(closest < 20.0f, "an arm leaves its post to reach you");
    }

    /* --- 5. standing away is answered ------------------------------------- */
    {
        core = arena(w);
        if (core < 0) return 2;
        /* Well outside an arm's reach, and pinned there. */
        const float px = g_entities[core].centreX() - 190.0f;
        const float py = (float)(FLOOR - PLAYER_H);
        run(w, 40, px, py);
        projClear();
        int shots = 0;
        for (int f = 0; f < 700; ++f) {
            const int before = projCount();
            run(w, 1, px, py);
            if (projCount() > before) shots += projCount() - before;
        }
        printf("a player standing at 190 cells took %d shots in 700 frames\n",
               shots);
        check(shots > 0, "the crown fires at somebody outside the arms' reach");
    }

    /* --- 6. standing still is answered ------------------------------------
       The body's own attack. Measured as fire appearing UNDER the player, and
       the arena floor is stone rather than brimstone so nothing else in the
       scene could have produced it. */
    {
        core = arena(w);
        if (core < 0) return 2;
        const float px = g_entities[core].centreX() - 250.0f;
        const float py = (float)(FLOOR - PLAYER_H);
        int burning = 0;
        for (int f = 0; f < 700 && burning == 0; ++f) {
            run(w, 1, px, py);
            for (int y = FLOOR - 10; y <= FLOOR; ++y)
                for (int x = (int)px - 10; x <= (int)px + 10; ++x)
                    if (w.at(x, y).mat == MAT_BRIMFIRE) ++burning;
        }
        printf("a player who stood still had %d cells of fire open under them\n",
               burning);
        check(burning > 0, "the ground erupts under a player who stops moving");
    }

    /* --- 7. no orphans, and it is remembered ------------------------------ */
    {
        core = arena(w);
        if (core < 0) return 2;
        run(w, 40, (float)(CX - 240), (float)(FLOOR - PLAYER_H));
        g_bossesBeaten = 0;
        g_entities[core].hp = 0;
        run(w, 60, (float)(CX - 240), (float)(FLOOR - PLAYER_H));
        const int orphans = partsAlive(core, ENT_EFFIGY_ARM)
                          + partsAlive(core, ENT_EFFIGY_CROWN);
        printf("after the body died: %d parts still standing, bosses 0x%X\n",
               orphans, (unsigned)g_bossesBeaten);
        check(orphans == 0, "killing the body clears its parts");
        check((g_bossesBeaten & BOSS_FINAL) != 0, "beating it is remembered");
        check((g_bossesBeaten & (BOSS_LAYER1 | BOSS_LAYER2 | BOSS_LAYER3)) == 0,
              "and it credits none of the three bosses above it");
        check(ENT_DEFS[ENT_EFFIGY].dropItem == ITEM_ASCENT_CORE,
              "and it pays out an Ascent Core");
    }

    /* --- 8. and it can be asked for ---------------------------------------- */
    {
        bool gated = false;
        for (int r = 0; r < N_RECIPES; ++r) {
            if (RECIPES[r].out != ITEM_EFFIGY_CALL) continue;
            for (int i = 0; i < CRAFT_MAX_IN; ++i)
                if (RECIPES[r].in[i].item == ITEM_PYRE_CORE &&
                    RECIPES[r].in[i].count > 0) gated = true;
        }
        check(gated, "its call is gated behind the Censer's own drop");
        check(ITEMS[ITEM_EFFIGY_CALL].summons == ENT_EFFIGY,
              "and the summon summons it");
    }

    if (failures) {
        fprintf(stderr, "\n%d Effigy check(s) failed\n", failures);
        return 1;
    }
    printf("\nthe last boss closes all three doors\n");
    return 0;
}
