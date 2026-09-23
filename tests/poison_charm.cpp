/* --- the Slime Gland poisons what you hit -------------------------------------

   Asked for: "slime magnet should be a poision damage apply". The charm was a
   pickup magnet; it now leaves poison on whatever the wearer hits, which is the
   creature's own acid handed to the player.

   Four properties, and the control is the second one -- a creature that simply
   died of the first hit would satisfy the first on its own:

     a poisoned creature keeps losing health after the hit
     an identical creature hit without the charm does not
     the damage is a point every ENT_POISON_TICK frames, and it ends
     hitting again REFRESHES rather than stacks

   Compile with every src/*.cpp except main.cpp. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "accessory.h"
#include "projectile.h"
#include "player.h"
#include "multiplayer.h"
#include <stdio.h>

static World g_testWorld;
static Player g_p;
static Inventory g_poisonInv;
static int failures = 0;

static void check(bool ok, const char* what) {
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static const int X = 900, Y = 700;

/* One husk -- 46 hp, enough to outlive several doses -- on a stone floor. */
static int spawnHusk() {
    World& w = g_testWorld;
    w.reset();
    entReset();
    devClear();
    playerSessionsReset();
    for (int x = X - 60; x <= X + 60; ++x)
        for (int y = Y; y <= Y + 4; ++y) w.setCell(x, y, MAT_STONE);
    w.setLiveWindow(X - 120, Y - 120, X + 120, Y + 120);
    g_p.reset((float)(X - 50), (float)(Y - PLAYER_H));
    g_p.alive = false;              /* it should stand still and be poisoned */
    g_poisonInv.clear();
    return entSpawn(w, ENT_HUSK, (float)X, (float)(Y - 25));
}

static void run(int frames) {
    for (int f = 0; f < frames; ++f) {
        entTick(g_testWorld, g_p, g_poisonInv);
        g_testWorld.step();
    }
}

int main() {
    initMaterials();
    initItems();
    initSprites();

    /* The charm is what decides the dose, and nothing else grants one. */
    Inventory worn; worn.clear();
    worn.equip[EQ_TRINKET_A].item  = ITEM_SLIME_MAGNET;
    worn.equip[EQ_TRINKET_A].count = 1;
    Inventory bare; bare.clear();
    const int dose = accessoryPoisonFrames(worn);
    printf("  the gland applies %d frames, a point every %d\n", dose, ENT_POISON_TICK);
    check(dose > 0, "the Slime Gland carries a poison dose");
    check(accessoryPoisonFrames(bare) == 0, "and nothing else does");

    /* --- poisoned, and the control ---------------------------------------- */
    int slot = spawnHusk();
    if (slot < 0) { fprintf(stderr, "could not spawn a husk\n"); return 2; }
    printf("  husk box: x %d..%d, y %d..%d, hp %d\n", g_entities[slot].left(),
           g_entities[slot].right(), g_entities[slot].top(),
           g_entities[slot].bottom(), g_entities[slot].hp);
    entDamageAt(X - 1, Y - 25, 1, true, dose);
    const int afterHit = g_entities[slot].hp;
    run(dose + ENT_POISON_TICK);
    const int poisoned = g_entities[slot].hp;

    slot = spawnHusk();
    entDamageAt(X - 1, Y - 25, 1, true, 0);
    const int cleanHit = g_entities[slot].hp;
    run(dose + ENT_POISON_TICK);
    const int clean = g_entities[slot].hp;

    printf("  poisoned: %d hp after the hit, %d later; unpoisoned: %d then %d\n",
           afterHit, poisoned, cleanHit, clean);
    check(poisoned < afterHit, "a poisoned creature keeps losing health");
    check(clean == cleanHit, "control: an unpoisoned one does not");
    check(afterHit - poisoned == dose / ENT_POISON_TICK,
          "one point per tick, for exactly the dose");

    /* --- and it ends ------------------------------------------------------- */
    const int settled = g_entities[slot].hp;
    slot = spawnHusk();
    entDamageAt(X - 1, Y - 25, 1, true, dose);
    run(dose * 3);
    const int longAfter = g_entities[slot].hp;
    run(dose * 3);
    check(g_entities[slot].hp == longAfter, "the poison runs out rather than lasting");
    (void)settled;

    /* --- refreshed, not stacked -------------------------------------------- */
    slot = spawnHusk();
    entDamageAt(X - 1, Y - 25, 1, true, dose);
    const int start = g_entities[slot].hp;
    for (int k = 0; k < 5; ++k) {         /* five more doses inside one dose */
        run(ENT_POISON_TICK / 2);
        entDamageAt(X - 1, Y - 25, 0, true, dose);
    }
    run(dose + ENT_POISON_TICK);
    const int stacked = start - g_entities[slot].hp;
    const int longest = (dose * 3) / ENT_POISON_TICK + 2;
    printf("  five overlapping doses cost %d health (one dose is %d)\n",
           stacked, dose / ENT_POISON_TICK);
    check(stacked <= longest,
          "overlapping hits refresh the clock rather than stacking doses");

    /* --- and a real shot carries it ----------------------------------------
       Everything above calls entDamageAt directly, which proves the mechanic
       and not the WIRING: the charm is read off the shooter when a bolt lands
       (projPoisonFrames), and that is the part a refactor breaks. So fire one,
       from a player wearing the gland. */
    slot = spawnHusk();
    projClear();
    g_playerSessions[0].inventory.clear();
    g_playerSessions[0].inventory.equip[EQ_TRINKET_A].item  = ITEM_SLIME_MAGNET;
    g_playerSessions[0].inventory.equip[EQ_TRINKET_A].count = 1;
    projSpawn((float)(X - 30), (float)(Y - 25), 3.0f, 0.0f,
              0, 1, 240, 0xFFFFFF, 0, MAT_EMPTY, 1, false, 0.0f,
              PROJ_EFFECT_NONE, 0, 0.0f, 0);
    int hitAt = 0;
    for (int f = 0; f < 60 && !hitAt; ++f) {
        projUpdate(g_testWorld);
        entTick(g_testWorld, g_p, g_poisonInv);
        g_testWorld.step();
        if (g_entities[slot].hp < ENT_DEFS[ENT_HUSK].hp) hitAt = f + 1;
    }
    const int shotHit = g_entities[slot].hp;
    run(dose + ENT_POISON_TICK);
    const int shotLater = g_entities[slot].hp;
    printf("  a bolt landed on frame %d: %d hp, then %d\n", hitAt, shotHit, shotLater);
    check(hitAt > 0, "control: the bolt reached the husk at all");
    check(shotHit - shotLater == dose / ENT_POISON_TICK,
          "a shot from a wearer poisons what it hits");

    if (failures) { fprintf(stderr, "\n%d poison check(s) failed\n", failures); return 1; }
    printf("PASS\n");
    return 0;
}
