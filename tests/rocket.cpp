/* --- the rocket stands, holds, and hands back --------------------------------

   Stage one of ENDGAME.md: the hull, the assembly device, and what goes into
   it. Nothing here launches anything -- ignition, readiness and the win screen
   are stages two and three -- so what is defended is the OBJECT: that it is
   the size it is drawn at, that it lands on ground rather than in the air, and
   that a machine holding a boss drop can never eat it.

   The size is the interesting half. Every device in this game was fourteen
   cells square, and DEV_W was a constant carrying three different meanings:
   this machine's own rectangle, the logistics lattice, and the width of a
   miner's bite. Only the first of those became per-type, and a footprint that
   is quietly still fourteen would leave a rocket you can walk through above
   its base, whose top two thirds cannot be clicked, and which does not clear
   its own cells when mined. So this measures the rectangle from several
   directions rather than trusting one call.

   Eight properties:

     the assembly is craftable, at the Assembly Table
     a placed rocket occupies 28 x 80 cells and answers to all of them
     it lands ON the ground you point at
     it refuses to stand in the air
     the corridor check sees a roof and ignores weather
     a core and fuel go in, and sand does not read as fuel
     digging it out gives back the rocket, the core and the fuel
     and a full pack leaves it standing

   Compile with every src cpp file except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "device.h"
#include "craft.h"
#include "player.h"
#include "multiplayer.h"
#include <stdio.h>
#include <string.h>

static World g_testWorld;
static const int CX = 1400, GROUND = 5000;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

/* A floor at GROUND, open sky above, nothing else. */
static void arena(World& w) {
    w.reset();
    devClear();
    for (int y = GROUND - 200; y <= GROUND + 20; ++y)
        for (int x = CX - 200; x <= CX + 200; ++x)
            w.setCell(x, y, y >= GROUND ? MAT_STONE : MAT_EMPTY);
    w.setLiveWindow(CX - 220, GROUND - 220, CX + 220, GROUND + 40);
}

static int held(const Inventory& inv, ItemId item) {
    int n = 0;
    for (int i = 0; i < INV_SLOTS; ++i)
        if (inv.slot[i].item == item) n += (int)inv.slot[i].count;
    return n;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();
    World& w = g_testWorld;
    static Inventory inv;

    /* --- 1. you can build one --------------------------------------------- */
    {
        int recipes = 0, atAssembly = 0;
        for (int r = 0; r < N_RECIPES; ++r) {
            if (RECIPES[r].out != ITEM_LAUNCH_ASSEMBLY) continue;
            ++recipes;
            if (RECIPES[r].station == STATION_ASSEMBLY) ++atAssembly;
        }
        printf("%d recipe(s) make a Launch Assembly, %d at the Assembly Table\n",
               recipes, atAssembly);
        check(recipes == 1, "exactly one recipe makes the launch assembly");
        check(atAssembly == 1, "and it is made at the Assembly Table");
        check(ITEMS[ITEM_LAUNCH_ASSEMBLY].kind == ITEMK_DEVICE &&
              ITEMS[ITEM_LAUNCH_ASSEMBLY].deviceType == DEV_ROCKET,
              "and what it makes places the rocket");
    }

    /* --- 2. it is 28 by 80, and every cell of it belongs to it ------------- */
    {
        arena(w);
        const bool placed = devPlace(w, DEV_ROCKET, CX, GROUND);
        check(placed, "a rocket goes down on flat ground");
        Device* d = placed ? devAt(CX, GROUND - 1) : 0;
        check(d != 0, "and the cell just above the ground is part of it");
        if (d) {
            /* The footprint is the PICTURE, not the rectangle -- see the note
               in device.h. So what is checked is that every opaque pixel of
               the hull is a solid cell and every transparent one is not: a
               rectangle fill would stand a slab of machinery in the sky beside
               the cone, and a fill that missed would leave holes in the hull
               you could walk through. */
            int solid = 0, drawnPixels = 0, wrong = 0;
            for (int y = 0; y < ROCKET_H; ++y)
                for (int x = 0; x < ROCKET_W; ++x) {
                    const bool drawn = g_rocketHull[y * ROCKET_SPR_W + x] != 0;
                    const bool cell = w.at(d->x + x, d->y + y).mat == MAT_DEVICE;
                    if (drawn) ++drawnPixels;
                    if (cell) ++solid;
                    if (drawn != cell) ++wrong;
                }
            printf("footprint: %d solid cells against %d drawn pixels, %d "
                   "disagreements\n", solid, drawnPixels, wrong);
            check(wrong == 0, "the footprint is exactly the shape of the hull");
            check(solid > 0 && solid < ROCKET_W * ROCKET_H,
                  "which is neither empty nor the whole bounding box");
            /* The corner a fourteen-cell assumption cannot reach. Both
               coordinates matter: 20 is past DEV_W, 70 is past DEV_H. */
            check(devAt(d->x + 20, d->y + 70) == d,
                  "a cell well outside a 14-cell box still answers to it");
            check(devAt(d->x + 6, d->y + 4) == d, "and so does the nose");
            check(devAt(d->x - 1, d->y + 40) == 0,
                  "and a cell just outside the hull does not");
        }
    }

    /* --- 3. it stands on what you pointed at ------------------------------ */
    {
        Device* d = devAt(CX, GROUND - 1);
        if (d) {
            printf("aimed at row %d; hull occupies rows %d..%d\n",
                   GROUND, d->y, d->y + ROCKET_H - 1);
            check(d->y + ROCKET_H == GROUND,
                  "the base sits directly on the aimed cell, not centred on it");
            check(d->x < CX && CX < d->x + ROCKET_W,
                  "and it is centred left to right on the cursor");
        }
    }

    /* --- 4. and not in the air -------------------------------------------- */
    {
        arena(w);
        const bool floating = devPlace(w, DEV_ROCKET, CX, GROUND - 60);
        check(!floating, "a rocket refuses to stand in mid-air");
        /* A ledge is not a pad either if most of the base overhangs it: the
           support rule is half the width, so a four-cell shelf fails. */
        arena(w);
        for (int y = GROUND - 60; y < GROUND - 56; ++y)
            for (int x = CX - 2; x <= CX + 1; ++x) w.setCell(x, y, MAT_STONE);
        const bool onLedge = devPlace(w, DEV_ROCKET, CX, GROUND - 60);
        check(!onLedge, "and refuses a shelf narrower than its own base");
    }

    /* --- 5. the corridor check sees a roof -------------------------------- */
    {
        arena(w);
        devPlace(w, DEV_ROCKET, CX, GROUND);
        Device* d = devAt(CX, GROUND - 1);
        check(d && rocketCorridorClear(w, *d), "open sky reads as a clear corridor");
        if (d) {
            /* One slab, directly over the hull, well inside ROCKET_CORRIDOR. */
            for (int x = d->x + 4; x < d->x + 10; ++x)
                w.setCell(x, d->y - 30, MAT_STONE);
            check(!rocketCorridorClear(w, *d), "and a slab overhead does not");
            for (int x = d->x + 4; x < d->x + 10; ++x)
                w.setCell(x, d->y - 30, MAT_EMPTY);
            /* Steam is not an obstruction -- see the note on
               rocketCorridorClear. A corridor a passing cloud closed would
               make the checklist flicker. */
            for (int x = d->x + 4; x < d->x + 10; ++x)
                w.setCell(x, d->y - 30, MAT_STEAM);
            check(rocketCorridorClear(w, *d), "and neither does a drift of steam");
        }
    }

    /* --- 6 and 7. what goes in comes back out ----------------------------- */
    {
        arena(w);
        devPlace(w, DEV_ROCKET, CX, GROUND);
        Device* d = devAt(CX, GROUND - 1);
        if (!d) { fprintf(stderr, "no rocket to load\n"); return 2; }

        check(!rocketCore(*d) && rocketFuel(*d) == 0, "a fresh assembly is empty");
        rocketSetCore(*d, true);
        d->mat = MAT_FUEL; d->count = 120;
        check(rocketCore(*d) && rocketFuel(*d) == 120,
              "a core and a part-load of fuel read back");
        /* Fuel means FUEL. The buffer is an ordinary stack, so the accessor is
           what stops a rocket fed a hundred cells of sand claiming to be
           fuelled -- see rocketFuel. */
        d->mat = MAT_SAND;
        check(rocketFuel(*d) == 0, "and a buffer full of sand is not fuel");
        d->mat = MAT_FUEL;

        memset(&inv, 0, sizeof(inv));
        digInto(w, inv, d->x + 14, d->y + 40, 1, 8, false, 255, 0);
        const bool gone = devAt(CX, GROUND - 1) == 0;
        printf("after digging: rocket gone %s, assemblies %d, cores %d, fuel %d\n",
               gone ? "yes" : "NO", held(inv, ITEM_LAUNCH_ASSEMBLY),
               held(inv, ITEM_ASCENT_CORE), held(inv, (ItemId)MAT_FUEL));
        check(gone, "digging a rocket takes the whole machine");
        check(held(inv, ITEM_LAUNCH_ASSEMBLY) == 1, "and hands back the assembly");
        /* The one that matters. An Ascent Core costs a boss fight and there is
           one per Effigy, so a rocket that swallowed it when you moved the
           thing six cells left would be the most expensive bug in the game. */
        check(held(inv, ITEM_ASCENT_CORE) == 1, "with the core still in it");
        check(held(inv, (ItemId)MAT_FUEL) == 120, "and the fuel that was loaded");

        int leftover = 0;
        for (int y = GROUND - ROCKET_H; y < GROUND; ++y)
            for (int x = CX - ROCKET_W; x < CX + ROCKET_W; ++x)
                if (w.at(x, y).mat == MAT_DEVICE) ++leftover;
        check(leftover == 0, "and clears all of its cells, not the first 196");
    }

    /* --- 8. it does not fall over on its first frame ----------------------
       The shaped footprint's one real hazard. devIntact samples five cells to
       decide whether a machine has been dug into, and for a rectangle those
       are the four corners and the middle -- four of which are empty sky on a
       rocket. A machine that reports itself broken is DELETED, so getting this
       wrong makes the rocket vanish the instant it is placed: not a subtle
       failure, but one a placement check alone would never see. */
    {
        arena(w);
        devPlace(w, DEV_ROCKET, CX, GROUND);
        for (int f = 0; f < 8; ++f) devTick(w);
        check(devAt(CX, GROUND - 1) != 0,
              "and it is still standing eight frames later");
    }

    /* --- 9. a full pack leaves it standing -------------------------------- */
    {
        arena(w);
        devPlace(w, DEV_ROCKET, CX, GROUND);
        Device* d = devAt(CX, GROUND - 1);
        if (d) rocketSetCore(*d, true);
        memset(&inv, 0, sizeof(inv));
        for (int i = 0; i < INV_SLOTS; ++i) {
            inv.slot[i].item = (ItemId)MAT_STONE;
            inv.slot[i].count = (u32)ITEMS[MAT_STONE].maxStack;
        }
        if (d) digInto(w, inv, d->x + 14, d->y + 40, 1, 8, false, 255, 0);
        check(devAt(CX, GROUND - 1) != 0,
              "a full pack leaves the rocket standing rather than losing the core");
    }

    if (failures) {
        fprintf(stderr, "\n%d rocket check(s) failed\n", failures);
        return 1;
    }
    printf("\nthe assembly stands where you put it and gives back what it holds\n");
    return 0;
}
