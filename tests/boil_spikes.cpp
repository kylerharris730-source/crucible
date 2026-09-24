/* --- boiling water does not grow spikes -------------------------------------

   Reported from powderlike: "steam underwater has started making one pixel
   wide columns of water rise up as it rises up". Two things did it.

   Stored steam pressure under a liquid column lifts the column (the fast path
   in updateGasPressure). Out of an open surface, every lifted parcel stood in
   the air as a one-cell spike, and since the lift restamps the whole column,
   the spike's top never had a turn to slide off before the next bubble lifted
   it again -- twice a frame once the fluid pass ran gas pressure too. Lifted
   parcels now stack only where something beside them holds them up (a dip, a
   pipe) and otherwise spill onto the surface beside the column.

   And liquidFalling called the whole column above a submerged bubble
   "falling", so the pool over a boil sat out the fluid pass and could not
   level what the lift threw up. Only air beneath a run now carries up it.

   Two checks. A pool with pressurised steam fed in along its floor must not
   stand water up in one-wide columns five or more cells tall, beyond the odd
   single frame (see the check for why it allows two). Before, it
   did on nearly every frame, the tallest sixteen. A boiling surface is froth,
   and a splash three or four cells high among the bubbles is fair; a column
   of five is a spike. And the lift must still work where it is right: steam
   under a one-wide pipe of water pushes the water up and out of the pipe.
   That one used to stall five cells up -- steam got in between the water
   parcels, and the lift only looked through unbroken liquid, so after one
   push the steam could only bubble up through the water while the water
   trickled down into the boiler. A slug in a pipe now lifts whole, bubbles
   and all (liftOutlet in world.cpp), and clears the 60-cell pipe.

   Compile with every source file except main.cpp. No socket, no window. */

#include "world.h"
#include "materials.h"
#include <stdio.h>

static World g_testWorld;

static void charge(World& w, int x, int y) {
    w.setCell(x, y, MAT_STEAM);
    w.cells[y * SIM_W + x].moisture = GAS_EXCESS_MASK;
}

/* The tallest run of water, counted down from the top of a column, whose
   cells have no liquid either side. */
static int tallestSpike(World& w, int x0, int x1, int yTop, int yBottom) {
    int worst = 0;
    for (int x = x0; x <= x1; ++x) {
        int y = yTop;
        while (y < yBottom && w.at(x, y).mat != MAT_WATER) ++y;
        int run = 0;
        for (; y < yBottom && w.at(x, y).mat == MAT_WATER; ++y) {
            const bool held = MATS[w.at(x - 1, y).mat].kind == KIND_LIQUID ||
                              MATS[w.at(x + 1, y).mat].kind == KIND_LIQUID;
            if (held) break;
            ++run;
        }
        if (run > worst) worst = run;
    }
    return worst;
}

int main() {
    initMaterials();
    World& w = g_testWorld;
    int failures = 0;

    /* The pool: 300 wide, 80 deep, in a stone basin. */
    {
        w.reset();
        const int X0 = 1100, X1 = 1400, FLOOR = 3400, SURF = FLOOR - 80;
        w.setLiveWindow(X0 - 20, SURF - 200, X1 + 20, FLOOR + 10);
        for (int y = SURF - 150; y <= FLOOR; ++y) {
            w.setCell(X0 - 1, y, MAT_STONE);
            w.setCell(X1 + 1, y, MAT_STONE);
        }
        for (int x = X0 - 1; x <= X1 + 1; ++x) w.setCell(x, FLOOR, MAT_STONE);
        for (int y = SURF; y < FLOOR; ++y)
            for (int x = X0; x <= X1; ++x) w.setCell(x, y, MAT_WATER);
        for (int f = 0; f < 60; ++f) w.step();

        const int SPIKE = 5;
        int worst = 0, worstFrame = -1, tallFrames = 0;
        for (int f = 0; f < 240; ++f) {
            if (f < 200 && f % 4 == 0)
                for (int x = X0 + 50; x <= X1 - 50; x += 7) charge(w, x, FLOOR - 2);
            w.step();
            const int s = tallestSpike(w, X0 + 1, X1 - 1, SURF - 150, FLOOR);
            if (s > worst) { worst = s; worstFrame = f; }
            if (s >= SPIKE) ++tallFrames;
        }
        printf("  pool: tallest one-wide column %d (frame %d), %d frames with a spike\n",
               worst, worstFrame, tallFrames);
        /* Two frames of 240, and nothing over SPIKE + 2. Zero was a threshold
           the ordinary splash of a boiling surface could reach: measured over
           twelve seeds, one seed in twelve throws a single five-tall splash
           for one frame, with the wind on and with it off alike, and the one
           seed this test runs landed on it when the wind changed which random
           draws happen in what order. The bug this guards against spiked on
           nearly every frame, sixteen tall, so it is still caught by a mile. */
        if (tallFrames > 2 || worst > SPIKE + 2) {
            fprintf(stderr, "boiling pool stood water up in spikes on %d frames (tallest %d)\n",
                    tallFrames, worst);
            ++failures;
        }
    }

    /* The pipe: a one-wide shaft of water 20 tall over a sealed steam
       chamber, 60 cells of empty pipe above it. Pressure has nowhere to go
       but up the pipe. */
    {
        w.reset();
        const int X = 1200, BOTTOM = 3400, TOP = BOTTOM - 30;
        w.setLiveWindow(X - 40, TOP - 80, X + 40, BOTTOM + 10);
        for (int y = TOP - 60; y <= BOTTOM + 1; ++y)
            for (int x = X - 6; x <= X + 6; ++x) w.setCell(x, y, MAT_STONE);
        for (int y = BOTTOM - 4; y <= BOTTOM; ++y)          /* chamber */
            for (int x = X - 4; x <= X + 4; ++x) w.setCell(x, y, MAT_EMPTY);
        for (int y = TOP - 60; y < BOTTOM - 4; ++y) w.setCell(X, y, MAT_EMPTY);
        for (int y = BOTTOM - 24; y < BOTTOM - 4; ++y) w.setCell(X, y, MAT_WATER);
        for (int y = BOTTOM - 4; y <= BOTTOM; ++y)
            for (int x = X - 4; x <= X + 4; ++x) charge(w, x, y);

        const int startTop = BOTTOM - 24;
        int highest = startTop;
        for (int f = 0; f < 30; ++f) {
            w.step();
            for (int y = TOP - 60; y < highest; ++y)
                if (w.at(X, y).mat == MAT_WATER) { highest = y; break; }
        }
        printf("  pipe: water top rose %d cells\n", startTop - highest);
        if (startTop - highest < 40) {
            fprintf(stderr, "steam under a pipe no longer lifts the water up it (%d cells)\n",
                    startTop - highest);
            ++failures;
        }
    }

    if (failures) return 1;
    printf("PASS: boiling does not grow spikes; steam drives water up a pipe\n");
    return 0;
}
