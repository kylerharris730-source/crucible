/* --- aqua regia turns what it touches into acid, and then stops --------------

   Asked for: "an acid+ that converts things it touches into acid, and it
   converts metals too, not glass though".

   The obvious implementation is the one world.cpp has ALREADY measured and
   thrown away: eat a cell, leave two cells of acid, and a pocket generated
   inside stone goes from 16,823 cells to 551,506 in three thousand frames
   while never letting its chunks sleep. That note is why this converts the
   victim AND spends itself in the same move -- so the reaction is bounded by
   how much you poured -- and why what it leaves behind is ORDINARY acid, which
   is bounded again by being neutralised by the next thing it eats.

   Six properties, and the last is the one that matters most:

     it converts stone                         (the ordinary case)
     it converts METAL                         (asked for explicitly)
     including gold                            (the acid-proof vault is over)
     it leaves GLASS alone, untouched          (asked for explicitly)
     and refractory, which lines the furnace   (one rule, not two)
     a sealed slab does not run away           (bounded, twice over)

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "multiplayer.h"
#include <stdio.h>

static World g_testWorld;
static const int CX = 1400, CY = 5000;
static const int POURED = 81;          /* the 9x9 puddle below */

static void fill(World& w, int x0, int y0, int x1, int y1, u8 m) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1)
                w.setCell(x, y, m);
}

static int count(const World& w, u8 m, int r) {
    int n = 0;
    for (int y = CY - r; y <= CY + r; ++y)
        for (int x = CX - r; x <= CX + r; ++x)
            if (w.at(x, y).mat == m) ++n;
    return n;
}

struct Result { int regia, acid, peakAcid, targetLost; };

/* PEAK acid, not acid at the end.

   The product is ordinary acid, which is itself neutralised by the next thing
   it eats -- so a run long enough to finish shows ZERO acid and a hole in the
   wall. A first version of this measured only the final count and reported
   "stone produced no acid", when in fact 81 cells of aqua regia had made 81 of
   acid, which had then eaten 81 more cells of stone and gone. What the
   reaction produced is the high-water mark; what is left is only how far along
   it got. */
static Result soak(World& w, u8 target, int frames) {
    w.reset();
    fill(w, CX - 60, CY - 60, CX + 60, CY + 60, target);
    fill(w, CX - 4, CY - 4, CX + 4, CY + 4, MAT_AQUA_REGIA);
    w.setLiveWindow(CX - 80, CY - 80, CX + 80, CY + 80);

    const int targetAtStart = count(w, target, 70);
    int peak = 0;
    for (int f = 0; f < frames; ++f) {
        w.step();
        const int a = count(w, MAT_ACID, 70);
        if (a > peak) peak = a;
    }
    Result r;
    r.regia      = count(w, MAT_AQUA_REGIA, 70);
    r.acid       = count(w, MAT_ACID, 70);
    r.peakAcid   = peak;
    r.targetLost = targetAtStart - count(w, target, 70);
    return r;
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();
    World& w = g_testWorld;
    int failures = 0;

    struct Case { const char* name; u8 mat; bool eats; };
    const Case cases[] = {
        { "stone",      MAT_STONE,      true  },
        { "iron",       MAT_IRON,       true  },
        { "gold",       MAT_GOLD,       true  },
        { "steel",      MAT_STEEL,      true  },
        { "tungsten",   MAT_TUNGSTEN,   true  },
        { "glass",      MAT_GLASS,      false },
        { "refractory", MAT_REFRACTORY, false },
    };

    for (int k = 0; k < 7; ++k) {
        const Case& c = cases[k];
        const Result r = soak(w, c.mat, 1500);
        printf("%-11s %3d regia left, peak acid %3d, %4d cells consumed\n",
               c.name, r.regia, r.peakAcid, r.targetLost);

        if (c.eats) {
            if (r.peakAcid == 0) {
                fprintf(stderr, "FAIL: %s never produced any acid -- it is not "
                                "being converted\n", c.name);
                ++failures;
            }
            if (r.regia == POURED) {
                fprintf(stderr, "FAIL: none of the aqua regia was spent on %s\n",
                        c.name);
                ++failures;
            }
        } else {
            if (r.peakAcid != 0) {
                fprintf(stderr, "FAIL: %s produced %d acid -- it is meant to be "
                                "immune\n", c.name, r.peakAcid);
                ++failures;
            }
            if (r.targetLost != 0) {
                fprintf(stderr, "FAIL: %d cells of %s were consumed anyway\n",
                        r.targetLost, c.name);
                ++failures;
            }
            if (r.regia != POURED) {
                fprintf(stderr, "FAIL: %d of %d aqua regia went missing against "
                                "%s, which it cannot touch\n",
                        POURED - r.regia, POURED, c.name);
                ++failures;
            }
        }
    }

    /* --- the bound -------------------------------------------------------
       The whole reason this material is written the way it is. Left sealed in
       stone for a long run, it must convert at most as many cells as there
       were cells of it, and the acid it leaves must not go on making more. */
    {
        const Result r = soak(w, MAT_STONE, 6000);
        printf("bound: %d poured, peak acid %d, %d stone consumed, "
               "%d acid and %d regia left after 6000f\n",
               POURED, r.peakAcid, r.targetLost, r.acid, r.regia);

        if (r.peakAcid > POURED) {
            fprintf(stderr, "FAIL: peaked at %d acid from %d of aqua regia -- the "
                            "reaction is multiplying, which is the runaway this "
                            "material is shaped to avoid\n", r.peakAcid, POURED);
            ++failures;
        }
        /* Each cell converts one and is spent; each acid then eats one and is
           spent. Two cells of wall per cell poured, and not one more. */
        if (r.targetLost > POURED * 2) {
            fprintf(stderr, "FAIL: %d cells of stone gone from %d of aqua regia, "
                            "where the bound is %d\n",
                    r.targetLost, POURED, POURED * 2);
            ++failures;
        }
        if (r.targetLost == 0) {
            fprintf(stderr, "FAIL: nothing happened at all in 6000 frames\n");
            ++failures;
        }
    }

    if (failures) {
        fprintf(stderr, "\n%d aqua regia check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
