/* --- how a plank burns -------------------------------------------------------

   Reported from play: "wood has always burned a bit weirdly, it should turn to
   ember, then burn away so the fire propagates down."

   It burned STRAIGHT TO FLAME, and flame is a gas that rises. So a cell of wood
   lit at the top of a trunk turned into something that immediately left, and
   the only thing carrying the burn downward was heat conducted through one
   random neighbour per frame against open air stripping it off. A fire went up
   a tree and hardly came down it.

   The fix is a state between the plank and the smoke -- a static, hot cell that
   stays where the wood was, keeps its neighbours above the ignition point long
   enough for them to catch, and then spends itself. Downward spread stops being
   a race against a rising flame and becomes the thing that happens on its own.

   Five properties:

     wood becomes ember, not flame          (the state exists)
     an ember burns out on its own          (a lit tree is not permanent)
     a burn front travels DOWN a plank      (the report, measured)
     a lit plank is consumed entirely       (it does not stall halfway)
     wood is still not a smelting fuel      (the heat ladder is unchanged)

   The last is the one to be careful of. Coal ignites at 90 C because a wood
   fire reaches about 98 and a bare flame does not -- that number IS the
   kindling step, and an ember hot enough to smelt copper would collapse the
   whole ladder into "burn some planks".

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "player.h"
#include "multiplayer.h"
#include <stdio.h>

static World g_testWorld;
static const int CX = 900, CY = 900;

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

static int countBox(const World& w, u8 mat, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (w.at(x, y).mat == mat) ++n;
    return n;
}

/* The lowest row still holding any of `mat`, or -1. This is the measurement the
   whole file is about: a burn front's depth. */
static int lowestRow(const World& w, u8 mat, int x0, int y0, int x1, int y1) {
    for (int y = y1; y >= y0; --y)
        for (int x = x0; x <= x1; ++x)
            if (w.at(x, y).mat == mat) return y;
    return -1;
}

/* A plank standing in open air on a stone floor, lit at the TOP. Nothing else
   in the scene: an enclosure would hold heat in and answer the question for
   the wood. */
static const int PLANK_W = 3, PLANK_H = 40;
static const int PX0 = CX, PX1 = CX + PLANK_W - 1;
static const int PY0 = CY, PY1 = CY + PLANK_H - 1;

static void plank(World& w) {
    w.reset();
    fill(w, CX - 40, PY1 + 1, CX + 40, PY1 + 6, MAT_STONE);
    fill(w, PX0, PY0, PX1, PY1, MAT_WOOD);
    w.setLiveWindow(CX - 60, PY0 - 40, CX + 60, PY1 + 20);
}

static void light(World& w, int x0, int y0, int x1, int y1) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            w.temp[y * SIM_W + x] = degC(205);
            w.dirtyPoint(x, y);
        }
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();
    World& w = g_testWorld;

    /* --- 1. wood becomes ember ------------------------------------------- */
    {
        const MatInfo& wood = MATS[MAT_WOOD];
        printf("wood ignites at %d C into %s\n",
               (int)wood.igniteTemp - TEMP_OFFSET, MATS[wood.burnsTo].name);
        check(wood.burnsTo == MAT_WOOD_EMBER, "wood burns to an ember, not to flame");
        check(MATS[MAT_WOOD_EMBER].kind == KIND_STATIC,
              "and the ember stays where the wood was");
        check(g_matDecay[MAT_WOOD_EMBER] > 0,
              "and it is spent on a timer, so a lit tree is not permanent");
        /* Birch is wood. It burned to flame beside the trunk and would have
           been the one plank in the world that behaved the old way. */
        check(MATS[MAT_BIRCH_WOOD].burnsTo == MAT_WOOD_EMBER,
              "and birch does the same thing, being wood");
    }

    /* --- 2. it burns out ------------------------------------------------- */
    {
        w.reset();
        fill(w, CX - 6, CY + 6, CX + 6, CY + 10, MAT_STONE);
        fill(w, CX - 2, CY, CX + 2, CY + 5, MAT_WOOD);
        w.setLiveWindow(CX - 30, CY - 30, CX + 30, CY + 30);
        light(w, CX - 2, CY, CX + 2, CY + 5);
        int lit = 0;
        for (int f = 0; f < 4000; ++f) {
            w.step();
            const int n = countBox(w, MAT_WOOD_EMBER, CX - 6, CY - 4, CX + 6, CY + 6);
            if (n > lit) lit = n;
        }
        const int left = countBox(w, MAT_WOOD_EMBER, CX - 6, CY - 4, CX + 6, CY + 6);
        const int wood = countBox(w, MAT_WOOD, CX - 6, CY - 4, CX + 6, CY + 6);
        printf("a 30-cell block lit through: peaked at %d ember, after 4000 "
               "frames %d ember and %d wood left\n", lit, left, wood);
        check(lit > 0, "a lit block does become ember");
        check(left == 0 && wood == 0, "and burns away to nothing");
    }

    /* --- 3. the front travels DOWN --------------------------------------- */
    {
        plank(w);
        light(w, PX0, PY0, PX1, PY0 + 1);
        int reached = PY0;
        for (int f = 0; f < 3000; ++f) {
            w.step();
            const int e = lowestRow(w, MAT_WOOD_EMBER, PX0, PY0, PX1, PY1);
            if (e > reached) reached = e;
        }
        const int depth = reached - PY0;
        const int left  = countBox(w, MAT_WOOD, PX0, PY0, PX1, PY1);
        printf("a %d-cell plank lit at the top: burnt %d cells down, %d cells "
               "of wood left after 3000 frames\n", PLANK_H, depth, left);
        /* The whole plank, not "some progress". Lighting the top of a tree and
           finding it still standing from the knees down is the report. */
        check(depth >= PLANK_H - 4, "a burn front travels down a standing plank");
        check(left == 0, "and the plank is consumed rather than stalling");
    }

    /* --- 4. and it is still not a fuel ----------------------------------- */
    {
        /* Coal against a burning plank: it must LIGHT (that is kindling, and
           the reason coal ignites at 90). Copper ore against the same fire must
           NOT smelt, because that is what coal is for. */
        w.reset();
        fill(w, CX - 10, CY + 8, CX + 10, CY + 12, MAT_STONE);
        fill(w, CX - 4, CY, CX + 4, CY + 7, MAT_WOOD);
        fill(w, CX + 5, CY + 4, CX + 6, CY + 7, MAT_COAL);
        fill(w, CX - 6, CY + 4, CX - 5, CY + 7, MAT_COPPER_ORE);
        w.setLiveWindow(CX - 40, CY - 30, CX + 40, CY + 30);
        light(w, CX - 4, CY, CX + 4, CY + 1);
        int hottest = 0, emberCoal = 0;
        for (int f = 0; f < 3000; ++f) {
            w.step();
            for (int y = CY + 4; y <= CY + 7; ++y) {
                const int t = (int)w.temp[y * SIM_W + (CX + 5)] - TEMP_OFFSET;
                if (t > hottest) hottest = t;
            }
            /* PEAK, not the count at the end. Coal ember is on a timer of its
               own, so a run long enough to be sure the plank has finished is
               also long enough for the coal it lit to have burnt out -- the
               first version of this measured the aftermath and reported that
               the kindling step does not work. */
            const int n = countBox(w, MAT_EMBER, CX + 4, CY, CX + 8, CY + 8);
            if (n > emberCoal) emberCoal = n;
        }
        const int melt = countBox(w, MAT_COPPER_MELT, CX - 8, CY, CX - 4, CY + 8)
                       + countBox(w, MAT_SLAG_MELT,   CX - 8, CY, CX - 4, CY + 8);
        printf("beside a burning plank: coal reached %d C and made %d ember, "
               "copper ore yielded %d melt\n", hottest, emberCoal, melt);
        check(emberCoal > 0, "a wood fire lights coal, which is what kindling is");
        /* Ore BESIDE a fire, which is the shape a player builds. Ore BURIED in
           the middle of a large pile lit all over does smelt -- 25 cells of
           melt, hottest 204 C -- and it is worth recording that this is not
           something the ember introduced: the same scene on the old build gave
           25 cells of melt at 197 C, because fire has always spawned at 205 and
           a big enough pile has always delivered it. The ladder rests on it
           being hard to light a woodpile through, not on an arithmetic ceiling,
           and the ember spawning at 130 does not change that either way. */
        check(melt == 0, "and still cannot smelt copper alongside it");
    }

    /* --- 5. and it still looks like a fire -------------------------------
       The ember does the work, but a burning forest made entirely of glowing
       blocks with no flame above it reads as an orange disease rather than a
       fire. So the ember vents flame upward on the fumarole's rule -- see
       g_matVentsFire -- and this is that claim, measured. */
    {
        plank(w);
        light(w, PX0, PY0, PX1, PY0 + 1);
        /* Counted only over the frames the plank is actually ALIGHT. A flat
           count over the run measures how long the plank took to burn as much
           as it measures whether it flamed -- the first version of this scored
           257 of 600 and most of the missing frames were before the front got
           going and after it had finished. */
        int lit = 0, flameFrames = 0;
        for (int f = 0; f < 600; ++f) {
            w.step();
            if (countBox(w, MAT_WOOD_EMBER, PX0, PY0, PX1, PY1) == 0) continue;
            ++lit;
            if (countBox(w, MAT_FIRE, PX0 - 4, PY0 - 6, PX1 + 4, PY1) > 0) ++flameFrames;
        }
        printf("a burning plank showed flame on %d of the %d frames it was "
               "alight\n", flameFrames, lit);
        check(lit > 100 && flameFrames * 4 > lit * 3,
              "a burning plank has flames above it, not just a glow");
    }

    if (failures) {
        fprintf(stderr, "\n%d wood-burn check(s) failed\n", failures);
        return 1;
    }
    printf("\nwood burns downward\n");
    return 0;
}
