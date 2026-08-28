/* --- can a spout dispense against a head of its own output? ------------------

   Asked for: "I want spouts to be able to push water and other liquids and
   fluids, and honestly even powders up to dispense... it can just transpose the
   pixels above it up one."

   Before this, a spout pointed up stopped the instant one cell of its own
   output settled on its face -- which is the exact moment you wanted a pump.
   It could only ever fill a space that was already empty, which made it a tap
   rather than a pump, and made filling a shaft impossible.

   Five properties, and the last two are the ones that keep the piston from
   being a wrecking ball:

     it fills a shaft it could not fill before   (the point of the change)
     it lifts powders too, not only liquids      (asked for explicitly)
     it refuses to shove rock                    (no boring upward)
     it refuses to shove a DEVICE                (no dragging a machine off
                                                  its own footprint)
     the lift is up-only                         (a down spout is unchanged)

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "device.h"
#include "player.h"
#include "multiplayer.h"
#include <stdio.h>
#include <time.h>

static World g_testWorld;

static void fill(World& w, int x0, int y0, int x1, int y1, u8 mat) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1)
                w.setCell(x, y, mat);
}

static const int CX = 1400, CY = 5000;

/* A spout sitting at the bottom of a vertical shaft, aimed up and loaded. */
static Device* setup(World& w, u8 carry, int load) {
    w.reset();
    devClear();
    fill(w, CX - 60, CY - 200, CX + 60, CY + 60, MAT_STONE);
    /* The shaft, wider than the device so the spout is not sealed by its own
       walls, and tall enough that the fill has somewhere to go. */
    fill(w, CX - DEV_W, CY - 180, CX + DEV_W * 2, CY + DEV_H - 1, MAT_EMPTY);
    w.setLiveWindow(CX - 80, CY - 220, CX + 80, CY + 80);

    if (!devPlace(w, DEV_SPOUT, CX + DEV_W / 2, CY + DEV_H / 2)) return 0;
    Device* d = devAt(CX + DEV_W / 2, CY + DEV_H / 2);
    if (!d) return 0;
    d->face    = 1;              /* up */
    d->enabled = true;
    d->mat     = carry;
    d->count   = (u16)load;
    d->value   = DEV_W;          /* full-face rate */
    return d;
}

static int countAbove(const World& w, u8 mat) {
    int n = 0;
    for (int y = CY - 180; y < CY; ++y)
        for (int x = CX - DEV_W; x <= CX + DEV_W * 2; ++x)
            if (w.at(x, y).mat == mat) ++n;
    return n;
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();
    World& w = g_testWorld;
    int failures = 0;

    /* --- 1. it fills a shaft, which needs lifting its own output --------- */
    {
        Device* d = setup(w, MAT_WATER, 4000);
        if (!d) { fprintf(stderr, "could not place the spout\n"); return 2; }
        const clock_t t0 = clock();
        for (int f = 0; f < 400; ++f) { devTick(w); w.step(); }
        const double ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC / 400.0;
        const int placed = countAbove(w, MAT_WATER);
        printf("water: %d cells delivered in 400 frames, %d left in the spout, "
               "%.3f ms/frame for the whole world step\n",
               placed, (int)d->count, ms);
        if (placed < 200) {
            fprintf(stderr, "FAIL: only %d cells got out -- the spout is still "
                            "stalling on its own output\n", placed);
            ++failures;
        }
    }

    /* --- 2. powders lift too ------------------------------------------- */
    {
        Device* d = setup(w, MAT_SAND, 4000);
        for (int f = 0; f < 400; ++f) { devTick(w); w.step(); }
        const int placed = countAbove(w, MAT_SAND);
        printf("sand:  %d cells delivered\n", placed);
        if (placed < 200) {
            fprintf(stderr, "FAIL: only %d cells of sand got out -- powders are "
                            "not being lifted\n", placed);
            ++failures;
        }
        (void)d;
    }

    /* --- 3. it will not bore through rock -------------------------------- */
    {
        Device* d = setup(w, MAT_WATER, 4000);
        /* A stone lid one cell above the face. There is no empty cell to shove
           the column into, so the piston must fail rather than shift rock. */
        fill(w, CX - DEV_W, CY - 2, CX + DEV_W * 2, CY - 1, MAT_STONE);
        int stoneBefore = 0;
        for (int y = CY - 2; y <= CY - 1; ++y)
            for (int x = CX - DEV_W; x <= CX + DEV_W * 2; ++x)
                if (w.at(x, y).mat == MAT_STONE) ++stoneBefore;
        for (int f = 0; f < 200; ++f) { devTick(w); w.step(); }
        int stoneAfter = 0;
        for (int y = CY - 2; y <= CY - 1; ++y)
            for (int x = CX - DEV_W; x <= CX + DEV_W * 2; ++x)
                if (w.at(x, y).mat == MAT_STONE) ++stoneAfter;
        printf("rock:  lid was %d cells, is %d cells, spout still holds %d\n",
               stoneBefore, stoneAfter, (int)d->count);
        if (stoneAfter < stoneBefore) {
            fprintf(stderr, "FAIL: the lid lost %d cells -- the piston is moving "
                            "static material\n", stoneBefore - stoneAfter);
            ++failures;
        }
    }

    /* --- 4. and it will not drag a device off its own footprint ---------- */
    {
        Device* d = setup(w, MAT_WATER, 4000);
        /* A second machine parked directly over the spout. Its footprint is
           MAT_DEVICE cells, which are static; if the piston moved them the
           Device struct would still point at the cells it used to own. */
        const bool placed = devPlace(w, DEV_CHEST, CX + DEV_W / 2, CY - DEV_H);
        if (!placed) { fprintf(stderr, "could not place the chest\n"); return 2; }
        Device* chest = devAt(CX + DEV_W / 2, CY - DEV_H);
        const int chestY = chest ? chest->y : -1;
        for (int f = 0; f < 200; ++f) { devTick(w); w.step(); }
        Device* still = devAt(CX + DEV_W / 2, CY - DEV_H);
        printf("device: chest at row %d, still found at its own cells: %s\n",
               chestY, still ? "yes" : "NO");
        if (!still || still->y != chestY) {
            fprintf(stderr, "FAIL: the chest moved or lost its footprint -- the "
                            "piston is shoving machines\n");
            ++failures;
        }
        (void)d;
    }

    /* --- 5. the lift is UP only ------------------------------------------ */
    {
        Device* d = setup(w, MAT_WATER, 4000);
        d->face = 0;                                  /* down */
        /* Seal the cells below the face so a down spout has nowhere to put
           anything. If the piston were direction-blind it would clear them. */
        fill(w, CX - DEV_W, CY + DEV_H, CX + DEV_W * 2, CY + DEV_H + 3, MAT_STONE);
        int before = 0;
        for (int y = CY + DEV_H; y <= CY + DEV_H + 3; ++y)
            for (int x = CX - DEV_W; x <= CX + DEV_W * 2; ++x)
                if (w.at(x, y).mat == MAT_STONE) ++before;
        for (int f = 0; f < 200; ++f) { devTick(w); w.step(); }
        int after = 0;
        for (int y = CY + DEV_H; y <= CY + DEV_H + 3; ++y)
            for (int x = CX - DEV_W; x <= CX + DEV_W * 2; ++x)
                if (w.at(x, y).mat == MAT_STONE) ++after;
        printf("down:  sealed floor was %d cells, is %d\n", before, after);
        if (after < before) {
            fprintf(stderr, "FAIL: a spout aimed DOWN moved material -- the lift "
                            "is not direction-gated\n");
            ++failures;
        }
    }

    if (failures) {
        fprintf(stderr, "\n%d spout lift check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
