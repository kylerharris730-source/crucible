#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "player.h"
#include "projectile.h"
#include "multiplayer.h"
#include <stdio.h>
#include <string.h>

/* A crop is not cover.

   Wheat and flax are planted in the open, in quantity, at chest height, so a
   field sat exactly where you shoot across. Every stalk was stopping bolts
   aimed at whatever was behind it, which is not difficulty -- it is a wall you
   cannot see as one.

   The rule is g_matPassable, which already existed and already had the torch on
   it: what you can walk through, a shot flies through. So this checks both
   halves. Crops and canopy let a shot pass; the things that are genuinely
   cover still stop it, because a rule that let everything through would pass
   this test just as well as the right one.

   Compile with all source files except main.cpp. */

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

static const int SX = 700, SY = 700;
static const int SCREEN = SX + 40;    /* where the material under test sits */
static const int BACKSTOP = SX + 90;  /* solid rock, well beyond it */

/* Lay a vertical band of `mat` across the shot's path and fire through it.
   Returns how far along x the shot got before it stopped. */
static int flightThrough(u8 mat) {
    World& w = g_world;
    for (int y = SY - 30; y <= SY + 30; ++y)
        for (int x = SX - 10; x <= BACKSTOP + 30; ++x)
            w.setCell(x, y, MAT_EMPTY);
    for (int y = SY - 30; y <= SY + 30; ++y)
        for (int x = BACKSTOP; x <= BACKSTOP + 20; ++x)
            w.setCell(x, y, MAT_STONE);
    if (mat != MAT_EMPTY)
        for (int y = SY - 30; y <= SY + 30; ++y)
            for (int x = SCREEN; x <= SCREEN + 2; ++x)
                w.setCell(x, y, mat);
    w.setLiveWindow(SX - 100, SY - 100, BACKSTOP + 100, SY + 100);

    projClear();
    /* A starter bolt: power 0, so it breaks nothing and stops at the first
       thing that counts as cover. That is precisely the question here. */
    projSpawn((float)SX, (float)SY, 4.0f, 0.0f, 0, 1, 200, 0xFFFFFF,
              0, MAT_EMPTY, 0, false, 0.0f, PROJ_EFFECT_NONE, 0, 0.0f, 0);
    int lastX = SX;
    for (int f = 0; f < 200 && projCount() > 0; ++f) {
        projUpdate(w);
        if (projCount() > 0) lastX = (int)g_proj[0].x;
    }
    return lastX;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    g_world.reset();
    playerSessionsReset();

    /* With nothing in the way the shot reaches the rock. Everything below is
       measured against this, so it has to be established first. */
    const int clear = flightThrough(MAT_EMPTY);
    printf("clear run reached x=%d (screen at %d, rock at %d)\n", clear, SCREEN, BACKSTOP);
    check(clear >= SCREEN, "with nothing in the way the shot passes the screen line");
    check(clear <= BACKSTOP + 2, "and stops at the rock");

    /* --- crops and canopy are not cover ----------------------------------- */
    struct { u8 mat; const char* name; } soft[] = {
        { MAT_WHEAT,     "wheat" },
        { MAT_FLAX,      "flax" },
        { MAT_STALK,     "a stalk" },
        { MAT_COTTON,    "cotton" },
        { MAT_OAK_LEAF,  "oak leaves" },
    };
    for (int i = 0; i < 5; ++i) {
        const int got = flightThrough(soft[i].mat);
        char msg[120];
        sprintf(msg, "a shot flies through %s", soft[i].name);
        check(got > SCREEN + 10, msg);
        /* And is not eaten by it either -- the band should still be standing. */
        sprintf(msg, "and does not mow %s down on the way", soft[i].name);
        check(g_world.at(SCREEN, SY).mat == soft[i].mat, msg);
    }

    /* --- but real cover still stops it ------------------------------------ */
    /* Without this the test would pass just as happily if every material had
       been made transparent, which is the easy way to break this. */
    struct { u8 mat; const char* name; } hard[] = {
        { MAT_STONE, "stone" },
        { MAT_DIRT,  "dirt" },
        { MAT_SAND,  "sand" },
        { MAT_WOOD,  "wood" },
    };
    for (int i = 0; i < 4; ++i) {
        const int got = flightThrough(hard[i].mat);
        char msg[120];
        sprintf(msg, "%s still stops a shot", hard[i].name);
        check(got <= SCREEN + 4, msg);
    }

    if (failures) {
        fprintf(stderr, "%d cover check(s) failed\n", failures);
        return 1;
    }
    puts("crops and leaves let shots through; rock, dirt, sand and wood do not");
    return 0;
}
