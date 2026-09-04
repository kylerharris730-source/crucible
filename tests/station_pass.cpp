/* --- a crafting station is furniture, not a wall -----------------------------

   Asked for: "the crafting tables nontangible easier to interact with then."

   A station used to be a single cell. It is now a fourteen-by-fourteen device
   (see DEV_WORKBENCH), and the whole block is written in MAT_STATION_*, which
   is KIND_STATIC -- so the workshop you built is also a fence you have to walk
   around. Five benches in a row is a wall with no door in it.

   The fix is the one MAT_DOOR_OPEN already uses and documents: g_matPassable.
   Solid to the SIMULATION, thin air to the character. Which is the important
   half of this harness -- "make it non-tangible" must not quietly mean "make it
   stop being a block", because a bench with sand pouring through it is worse
   than a bench you have to walk round.

   Four properties:

     you can walk through a station               (the request)
     all five tiers, not just the bench           (one rule, not a list)
     it still holds back powder                   (KIND is untouched)
     and crafting still finds it                  (nothing else moved)

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "player.h"
#include "device.h"
#include "craft.h"
#include "multiplayer.h"
#include <stdio.h>

static World g_testWorld;
static const int CX = 1400, CY = 5000;
static const int FLOOR = CY + 30;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static void hall(World& w) {
    w.reset();
    for (int y = FLOOR; y < FLOOR + 40; ++y)
        for (int x = CX - 200; x <= CX + 200; ++x)
            w.setCell(x, y, MAT_STONE);
    w.setLiveWindow(CX - 220, CY - 120, CX + 220, FLOOR + 60);
}

/* Walk right for `frames` and report how far the character actually got. */
static float walkRight(World& w, int frames) {
    Player& p = g_player;
    p.reset((float)(CX - 40), (float)(FLOOR - PLAYER_H));
    p.alive = true; p.hp = PLAYER_HP_MAX;
    PlayerInput in;
    in.left = false; in.right = true; in.jump = false; in.down = false;
    for (int f = 0; f < 20; ++f) p.update(w, in);   /* settle */
    const float x0 = p.centreX();
    for (int f = 0; f < frames; ++f) { p.update(w, in); w.step(); }
    return p.centreX() - x0;
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();
    World& w = g_testWorld;

    /* How far an unobstructed walk gets, as the reference every case below is
       read against. Measuring it rather than deriving it from MAX_SPEED keeps
       this honest if acceleration or friction ever move. */
    hall(w);
    devClear();
    const float clearRun = walkRight(w, 200);
    printf("clear floor: walked %.0f cells in 200 frames\n", clearRun);

    /* --- 1 & 2. every tier is walk-through ------------------------------- */
    struct Tier { const char* name; int dev; };
    const Tier tiers[] = {
        { "Workbench",      DEV_WORKBENCH },
        { "Anvil",          DEV_ANVIL     },
        { "Chemistry Bench",DEV_CHEM      },
        { "Assembly Table", DEV_ASSEMBLY  },
        { "Blast Furnace",  DEV_FORGE     },
    };
    for (int k = 0; k < 5; ++k) {
        hall(w);
        devClear();
        /* devPlace takes the CENTRE, so this is the box standing on the
           floor rather than floating a half-height above it. */
        if (!devPlace(w, (u8)tiers[k].dev, CX, FLOOR - 1 - DEV_H / 2)) {
            printf("  could not place %s\n", tiers[k].name);
            ++failures;
            continue;
        }
        const float got = walkRight(w, 200);
        char label[80];
        sprintf(label, "you walk through the %s", tiers[k].name);
        printf("    %-18s walked %.0f of %.0f cells\n",
               tiers[k].name, got, clearRun);
        check(got > clearRun * 0.9f, label);
    }

    /* --- 3. it still holds back powder ----------------------------------- */
    /* The half that must NOT change. Passable is a rule about the character;
       if it leaked into the simulation a workshop would fill with whatever was
       above it. */
    {
        hall(w);
        devClear();
        if (!devPlace(w, DEV_WORKBENCH, CX, FLOOR - 1 - DEV_H / 2)) return 2;
        const Device* d = devAt(CX, FLOOR - 1 - DEV_H / 2);
        if (!d) return 2;
        /* A column of sand dropped onto the top face. */
        for (int y = d->y - 30; y < d->y - 10; ++y)
            for (int x = d->x + 2; x < d->x + DEV_W - 2; ++x)
                w.setCell(x, y, MAT_SAND);
        /* Sand INSIDE the footprint, sampled as it runs. Not "sand on the
           floor underneath", which a first version measured and which fails on
           a solid bench too: powder poured on a block spills off the sides and
           flows back under it, and that is flow AROUND rather than through. The
           question passability could break is whether a cell of the block ever
           holds something else. */
        int through = 0;
        for (int t = 0; t < 600; ++t) {
            w.step();
            for (int y = d->y; y < d->y + DEV_H; ++y)
                for (int x = d->x; x < d->x + DEV_W; ++x)
                    if (w.at(x, y).mat == MAT_SAND) ++through;
        }
        /* The bench's own cells must survive too -- sand landing on a device
           that stopped being solid to the sim would overwrite it. */
        int intact = 0;
        for (int y = d->y; y < d->y + DEV_H; ++y)
            for (int x = d->x; x < d->x + DEV_W; ++x)
                if (w.at(x, y).mat == MAT_STATION_BENCH) ++intact;
        printf("sand on a bench: %d cells got underneath, %d of %d bench "
               "cells intact\n", through, intact, DEV_W * DEV_H);
        check(through == 0, "a station still holds back powder");
        check(intact == DEV_W * DEV_H, "and the station is not buried by it");
    }

    /* --- 4. crafting still finds it -------------------------------------- */
    {
        hall(w);
        devClear();
        if (!devPlace(w, DEV_ANVIL, CX, FLOOR - 1 - DEV_H / 2)) return 2;
        Player& p = g_player;
        p.reset((float)(CX - 6), (float)(FLOOR - PLAYER_H));
        craftScanStations(w, p);
        int anvilRecipe = -1;
        for (int r = 0; r < N_RECIPES; ++r)
            if (RECIPES[r].station == STATION_ANVIL) { anvilRecipe = r; break; }
        check(anvilRecipe >= 0 && craftHasStation(anvilRecipe),
              "standing beside an anvil still unlocks its recipes");
    }

    if (failures) {
        fprintf(stderr, "\n%d station check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
