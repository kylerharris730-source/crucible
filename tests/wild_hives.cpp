/* --- wax has a source now ------------------------------------------------------

   Asked for: "lets change everything that needs web to needing wax, and lets
   have some hives spawn naturally in little divots in the ground, in like 3
   places in the world, so theres a way to get some wax."

   The two halves are one problem. Six of the seven shot modifiers were made of
   spider silk, and silk has no survival source: the Widow lays it mid-fight and
   it decays with a mean life around four seconds, so the recipes asked for
   something a player could not keep. Wax is the answer that needs no new
   mechanism -- but a hive was something you had to build, and building one is
   not a thing you can do before you have any wax to want.

   Three standing in the world closes the loop.

   Four properties:

     nothing in the crafting table asks for web any more   (the swap, enforced)
     the modifiers ask for wax instead                     (and it went somewhere)
     a generated world contains hives                      (the supply exists)
     each one is standing in a dip in the ground           (the ask, measured)

   Compile with every src cpp file except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "worldgen.h"
#include "item.h"
#include "sprite.h"
#include "device.h"
#include "craft.h"
#include "player.h"
#include "multiplayer.h"
#include <stdio.h>

/* The first cell of actual ground in a column, searched around a starting row.
   Soil only, for the reason the call site gives. */
static int groundRow(int x, int nearY) {
    if (x <= 0 || x >= SIM_W) return -1;
    for (int y = imax(1, nearY - 60); y < SIM_H - 1 && y < nearY + 60; ++y) {
        const u8 m = g_world.at(x, y).mat;
        if (m == MAT_DIRT || m == MAT_GRASS || m == MAT_SAND || m == MAT_STONE)
            return y;
    }
    return -1;
}

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();

    /* --- 1. the modifiers are made of something a hive makes -------------
       MAT_BEESWAX, spelled out, and checked against what a hive EXTRUDES
       rather than against "some kind of wax". The first version of this swap
       asked for MAT_WAX, which is a different material with no source at all
       -- creative palette only -- so six recipes went from asking for
       something you cannot keep to asking for something that does not exist.
       This check is written the way it is because the looser version passed
       that build. */
    {
        int webRecipes = 0, waxModifiers = 0, modifiers = 0;
        for (int r = 0; r < N_RECIPES; ++r) {
            bool web = false, wax = false;
            for (int i = 0; i < CRAFT_MAX_IN; ++i) {
                if (RECIPES[r].in[i].count <= 0) continue;
                if (RECIPES[r].in[i].item == (ItemId)MAT_WEB) web = true;
                if (RECIPES[r].in[i].item == (ItemId)MAT_BEESWAX) wax = true;
            }
            if (web) { ++webRecipes; printf("  %s still needs web\n", RECIPES[r].label); }
            if (ITEMS[RECIPES[r].out].kind == ITEMK_MODULE &&
                ITEMS[RECIPES[r].out].modKind != MODK_NONE) {
                ++modifiers;
                if (wax) ++waxModifiers;
            }
        }
        printf("%d recipes need web, %d of %d shot modifiers are made of "
               "beeswax\n", webRecipes, waxModifiers, modifiers);
        check(webRecipes == 0, "nothing in the crafting table asks for silk");
        check(waxModifiers > 0, "and the shot modifiers ask for beeswax instead");
    }

    /* --- 1b. and the coal residue is worth carrying home ------------------
       Asked for: "you should be able to craft coal honey and coal wax into
       coal." A soured hive makes those two instead of wax and honey, and they
       had no use at all -- which quietly made souring a colony a mistake
       rather than a choice. */
    {
        bool fromWax = false, fromHoney = false;
        for (int r = 0; r < N_RECIPES; ++r) {
            if (RECIPES[r].out != (ItemId)MAT_COAL) continue;
            for (int i = 0; i < CRAFT_MAX_IN; ++i) {
                if (RECIPES[r].in[i].count <= 0) continue;
                if (RECIPES[r].in[i].item == (ItemId)MAT_COAL_WAX) fromWax = true;
                if (RECIPES[r].in[i].item == (ItemId)MAT_COAL_HONEY) fromHoney = true;
            }
        }
        check(fromWax, "coal wax renders back into coal");
        check(fromHoney, "and so does coal honey");
    }

    /* --- 2. a world has hives in it, in dips ------------------------------ */
    {
        /* A real generated world rather than a hand-built scene: the whole
           claim is about what worldgen produces, and a harness that carved its
           own divot would be testing its own carving. */
        generateWorld(g_world);

        int hives = 0, inDivots = 0, standing = 0;
        for (int i = 0; i < MAX_DEVICES; ++i) {
            const Device& d = g_devices[i];
            if (!d.used || d.type != DEV_HIVE) continue;
            ++hives;
            const int cx = d.x, cy = d.y;
            /* Standing ON ground: the row under the footprint is solid. */
            const int below = cy + 7;
            if (below < SIM_H && g_world.at(cx, below).mat != MAT_EMPTY) ++standing;
            /* And in a dip: the surface a shoulder's width to each side is
               higher than the ground it stands on. Measured off the world
               rather than off the generator's own numbers. */
            /* Ground means SOIL, and it is scanned the same way the generator
               scans it. "First non-empty cell" counts a stalk of wheat and a
               tree trunk as ground, which is how an earlier version of this
               reported three hives standing on peaks: the columns beside them
               had crops in the air and the column under the hive did not. */
            const int floorY = groundRow(cx, cy);
            const int leftY  = groundRow(cx - 30, cy);
            const int rightY = groundRow(cx + 30, cy);
            const bool dip = floorY > 0 && leftY > 0 && rightY > 0 &&
                             floorY > leftY && floorY > rightY;
            if (dip) ++inDivots;
            printf("  hive at (%d,%d): floor %d, shoulders %d and %d%s\n",
                   cx, cy, floorY, leftY, rightY, dip ? "  -- in a dip" : "");
        }
        printf("%d wild hives, %d standing on ground, %d in dips\n",
               hives, standing, inDivots);
        check(hives >= 2, "a generated world has wild hives in it");
        check(standing == hives, "every one of them is standing on the ground");
        check(inDivots == hives, "and every one is in a dip, which is the ask");
    }

    if (failures) {
        fprintf(stderr, "\n%d wild-hive check(s) failed\n", failures);
        return 1;
    }
    printf("\nwax is reachable without building anything first\n");
    return 0;
}
