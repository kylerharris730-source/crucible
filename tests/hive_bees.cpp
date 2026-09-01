/* --- does the hive actually run? -------------------------------------------

   The bee loop is the first thing in this game that produces material without
   the player doing anything, so the question is not "do the pieces compile"
   but "does a hive left alone beside a flower turn into wax and honey".

   Five properties, and each of them is something that was wrong at some point
   while this was being written:

     1. A hive makes bees, up to its setpoint and no further.
     2. A bee finds a flower, goes to it, comes back, and the hive turns that
        into wax ABOVE it and honey BESIDE it.
     3. Coal settling on a bee changes its species -- and brushing past coal
        does not, or a hive anywhere near a coal seam converts itself.
     4. Coal wax boils back down into coal. That is the whole argument for the
        coal bee, and it is the one step that makes the loop a coal supply
        rather than a novelty.
     5. Bees survive their own hive's working temperature and die above it.
        A bee that cooked at 60 C could not live next to melting wax, which is
        the one place a bee is guaranteed to be.

   Compile with every source file except main.cpp. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "entity.h"
#include "device.h"
#include "player.h"
#include "multiplayer.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(bool ok, const char* what) {
    printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static const int HX = 1200, HY = 5000;   /* where the hive goes */

static Player  g_p;
static Inventory g_testInv;

static void stepWorld(int frames) {
    for (int i = 0; i < frames; ++i) {
        devTick(g_world);
        entTick(g_world, g_p, g_testInv);
        g_world.step();
    }
}

/* A floor, a hive on it, and a flower a short flight away. */
static Device* buildApiary(int flowerOffset) {
    entReset();
    devClear();
    g_world.reset();
    g_world.setLiveWindow(HX - 400, HY - 400, HX + 400, HY + 400);
    g_p.reset((float)HX, (float)(HY - 40));
    g_testInv.clear();

    for (int x = HX - 300; x <= HX + 300; ++x)
        g_world.setCell(x, HY + DEV_H / 2 + 1, MAT_STONE);

    if (!devPlace(g_world, DEV_HIVE, HX, HY)) return 0;
    Device* d = devAt(HX, HY);
    if (!d) return 0;

    if (flowerOffset)
        g_world.setCell(HX + flowerOffset, HY, MAT_FLOWER);
    return d;
}

static int beeCount() {
    int n = 0;
    for (int i = 0; i < MAX_ENTITIES; ++i)
        if (g_entities[i].alive() &&
            (g_entities[i].type == ENT_BEE || g_entities[i].type == ENT_COAL_BEE)) ++n;
    return n;
}

static int countMat(u8 mat) {
    int n = 0;
    for (int y = HY - 60; y <= HY + 60; ++y)
        for (int x = HX - 60; x <= HX + 60; ++x)
            if (g_world.at(x, y).mat == mat) ++n;
    return n;
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();

    /* --- 1. a hive fills up, and then stops -------------------------------- */
    {
        Device* d = buildApiary(0);
        if (!d) { fprintf(stderr, "could not place a hive\n"); return 2; }
        d->value = 3;
        stepWorld(60);
        const int early = beeCount();
        stepWorld(300 * 6);
        const int settled = beeCount();
        check(early < 3, "a new hive is not instantly full");
        check(settled == 3, "a hive fills to its setpoint and stops");
    }

    /* --- 2a. no flowers, no product --------------------------------------- */
    /* The interesting negative. A hive with bees but nothing to forage must
       produce NOTHING however long it runs -- otherwise the flowers are
       decoration and the machine is really just a timer. */
    {
        Device* d = buildApiary(0);
        if (!d) return 2;
        d->value = 5;
        stepWorld(300 * 6 + 1800);
        check(countMat(MAT_BEESWAX) == 0 && countMat(MAT_HONEY) == 0,
              "a hive with no flowers produces nothing");
    }

    /* --- 2b. the round trip produces wax above and honey beside ------------ */
    {
        Device* d = buildApiary(40);
        if (!d) return 2;
        d->value = 5;
        stepWorld(300 * 6);           /* let the colony arrive */
        stepWorld(1800);              /* thirty seconds of foraging */
        const int wax   = countMat(MAT_BEESWAX);
        const int honey = countMat(MAT_HONEY);
        check(wax > 0,   "a working hive extrudes beeswax");
        check(honey > 0, "a working hive extrudes honey");

        /* Where they came out, not just that they exist. */
        int waxAbove = 0, honeyBeside = 0;
        for (int x = d->x; x < d->x + DEV_W; ++x)
            for (int y = d->y - 6; y < d->y; ++y)
                if (g_world.at(x, y).mat == MAT_BEESWAX) ++waxAbove;
        for (int y = d->y; y < d->y + DEV_H; ++y) {
            for (int x = d->x - 6; x < d->x; ++x)
                if (g_world.at(x, y).mat == MAT_HONEY) ++honeyBeside;
            for (int x = d->x + DEV_W; x < d->x + DEV_W + 6; ++x)
                if (g_world.at(x, y).mat == MAT_HONEY) ++honeyBeside;
        }
        check(waxAbove > 0,    "wax comes out of the top");
        check(honeyBeside > 0, "honey comes out of the sides");
    }

    /* --- 2c. a coal bee brings back coal wax and coal honey ---------------- */
    /* The whole point of the upgrade. A hive fed by coal bees must produce the
       coal variants, because those are what boil back down into coal -- see
       check 4. If this delivered plain wax the coal bee would be cosmetic. */
    {
        Device* d = buildApiary(40);
        if (!d) return 2;
        d->value = 1;
        for (int i = 0; i < 4; ++i) hiveDeliver(*d, true);
        stepWorld(400);
        check(countMat(MAT_COAL_WAX) > 0,   "a coal bee's load becomes coal wax");
        check(countMat(MAT_COAL_HONEY) > 0, "a coal bee's load becomes coal honey");
        check(countMat(MAT_BEESWAX) == 0,   "and not the ordinary kind");
    }

    /* --- 3. coal converts a bee, and a glance does not -------------------- */
    {
        buildApiary(0);
        const int slot = entSpawn(g_world, ENT_BEE, (float)HX, (float)(HY - 20));
        if (slot < 0) { fprintf(stderr, "could not spawn a test bee\n"); return 3; }
        g_entities[slot].home = -1;

        /* A brief brush: a handful of frames in contact. */
        g_world.setCell(HX, HY - 20, MAT_COAL);
        for (int i = 0; i < 20; ++i) entTick(g_world, g_p, g_testInv);
        check(g_entities[slot].type == ENT_BEE, "brushing past coal does not convert a bee");

        /* Held in it. Re-laid every frame because a bee pushes material around. */
        for (int i = 0; i < 400 && g_entities[slot].type == ENT_BEE; ++i) {
            Entity& e = g_entities[slot];
            for (int y = e.top(); y <= e.bottom(); ++y)
                for (int x = e.left(); x <= e.right(); ++x)
                    if (g_world.at(x, y).mat == MAT_EMPTY) g_world.setCell(x, y, MAT_COAL);
            entTick(g_world, g_p, g_testInv);
        }
        check(g_entities[slot].type == ENT_COAL_BEE, "sustained coal makes a coal bee");
    }

    /* --- 4. coal wax boils back down to coal ------------------------------ */
    {
        g_world.reset();
        g_world.setLiveWindow(HX - 40, HY - 40, HX + 40, HY + 40);
        g_world.setCell(HX, HY, MAT_COAL_WAX);
        g_world.temp[HY * SIM_W + HX] = degC(95);   /* above its 78 C */
        for (int i = 0; i < 40 && g_world.at(HX, HY).mat == MAT_COAL_WAX; ++i)
            g_world.step();
        check(g_world.at(HX, HY).mat == MAT_COAL,
              "coal wax boils down into coal");

        /* Ordinary wax must NOT do that -- it melts and comes back. */
        g_world.setCell(HX + 4, HY, MAT_BEESWAX);
        g_world.temp[HY * SIM_W + HX + 4] = degC(60);
        for (int i = 0; i < 40 && g_world.at(HX + 4, HY).mat == MAT_BEESWAX; ++i)
            g_world.step();
        check(g_world.at(HX + 4, HY).mat == MAT_BEESWAX_MELT,
              "plain beeswax melts rather than boiling away");
    }

    /* --- 5. a bee tolerates its own hive, and not a furnace ---------------- */
    {
        buildApiary(0);
        const int warm = entSpawn(g_world, ENT_BEE, (float)HX, (float)(HY - 20));
        if (warm < 0) return 4;
        g_entities[warm].home = -1;
        for (int i = 0; i < 120; ++i) {
            Entity& e = g_entities[warm];
            for (int y = e.top() - 1; y <= e.bottom() + 1; ++y)
                for (int x = e.left() - 1; x <= e.right() + 1; ++x)
                    g_world.temp[y * SIM_W + x] = degC(70);   /* wax-melting warm */
            entTick(g_world, g_p, g_testInv);
        }
        check(g_entities[warm].alive(), "a bee survives wax-melting heat");

        const int hot = entSpawn(g_world, ENT_BEE, (float)(HX + 30), (float)(HY - 20));
        if (hot < 0) return 5;
        g_entities[hot].home = -1;
        for (int i = 0; i < 200 && g_entities[hot].alive(); ++i) {
            Entity& e = g_entities[hot];
            for (int y = e.top() - 1; y <= e.bottom() + 1; ++y)
                for (int x = e.left() - 1; x <= e.right() + 1; ++x)
                    g_world.temp[y * SIM_W + x] = degC(150);
            entTick(g_world, g_p, g_testInv);
        }
        check(!g_entities[hot].alive(), "too much heat kills a bee");
    }

    if (failures == 0) { puts("PASS"); return 0; }
    fprintf(stderr, "%d hive check(s) failed\n", failures);
    return 1;
}
