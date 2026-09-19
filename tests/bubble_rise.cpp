/* --- a bubble rises through water; it does not wander across it ------------

   Reported from powderlike: "steam is traveling way farther sideways than it
   should when it rises through water". Three things did it, measured here.

   The water's own sideways hop (updateLiquid) could land on a bubble. A gas
   lighter than the liquid counted as somewhere to flow, even deep inside the
   body, and landing on it is a swap -- the bubble went to where the parcel
   had been, as far as the hop reached, which in a deep pool is forty cells.
   Twice a frame with the fluid pass. A bubble is now a wall to that hop.

   The submerged wobble used the gas's whole jitter, and Steam's is 230 of
   255 so that it fills a room: nine steps in ten of a bubble's climb were
   diagonal. It uses a quarter now.

   And a bubble held under another bubble, with water beside it, took the
   open-air random walk -- sideways, even downward -- because the cell above
   it was gas rather than water. It waits its turn now.

   The checks, in a still pool 150 deep: a single bubble released on the
   floor travels less than 60 cells sideways all told on its way up (it was
   133, now about 34), and a stream of bubbles from one point stays within a
   few cells of that point near the source (rms 12.5 before, about 2 now).

   Compile with every source file except main.cpp. No socket, no window. */

#include "world.h"
#include "materials.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static World g_testWorld;
static const int X0 = 1000, X1 = 1400, FLOOR = 3400, SURF = FLOOR - 150;

static void pool(World& w) {
    w.reset();
    w.setLiveWindow(X0 - 20, SURF - 60, X1 + 20, FLOOR + 10);
    for (int y = SURF - 40; y <= FLOOR; ++y) {
        w.setCell(X0 - 1, y, MAT_STONE);
        w.setCell(X1 + 1, y, MAT_STONE);
    }
    for (int x = X0 - 1; x <= X1 + 1; ++x) w.setCell(x, FLOOR, MAT_STONE);
    for (int y = SURF; y < FLOOR; ++y)
        for (int x = X0; x <= X1; ++x) w.setCell(x, y, MAT_WATER);
}

int main() {
    initMaterials();
    World& w = g_testWorld;
    int failures = 0;

    /* One bubble at a time: total sideways travel on the way up. */
    {
        double sumPath = 0;
        int n = 0;
        for (int trial = 0; trial < 12; ++trial) {
            pool(w);
            for (int f = 0; f < 30 + trial; ++f) w.step();
            const int sx = X0 + 100 + trial * 16, sy = FLOOR - 3;
            w.setCell(sx, sy, MAT_STEAM);
            w.temp[sy * SIM_W + sx] = 255;
            int px = sx, py = sy, path = 0;
            for (int f = 0; f < 600 && py >= SURF; ++f) {
                w.step();
                int bx = -1, by = -1, bd = 1 << 30;
                for (int y = SURF - 40; y < FLOOR; ++y)
                    for (int x = X0; x <= X1; ++x)
                        if (w.at(x, y).mat == MAT_STEAM) {
                            const int d = abs(x - px) + abs(y - py);
                            if (d < bd) { bd = d; bx = x; by = y; }
                        }
                if (bx < 0) break;
                path += abs(bx - px);
                px = bx; py = by;
            }
            if (py < SURF) { sumPath += path; ++n; }
        }
        const double mean = n ? sumPath / n : 1e9;
        printf("  single bubble: %d of 12 surfaced, %.1f cells sideways over 150 rows\n", n, mean);
        if (n < 12 || mean >= 60) {
            fprintf(stderr, "a bubble wandered %.1f cells sideways rising 150 rows (%d surfaced)\n",
                    mean, n);
            ++failures;
        }
    }

    /* A stream from one point: spread in the bottom fifth of the pool. */
    {
        pool(w);
        const int CX = 1200;
        for (int f = 0; f < 60; ++f) w.step();
        double s2 = 0, cnt = 0;
        for (int f = 0; f < 400; ++f) {
            if (f % 2 == 0) w.setCell(CX, FLOOR - 2, MAT_STEAM);
            w.step();
            if (f < 200) continue;
            for (int y = FLOOR - 33; y < FLOOR - 5; ++y)
                for (int x = X0; x <= X1; ++x)
                    if (w.at(x, y).mat == MAT_STEAM) { s2 += (double)(x - CX) * (x - CX); cnt += 1; }
        }
        const double rms = cnt ? sqrt(s2 / cnt) : 1e9;
        printf("  stream: rms %.1f cells from the source in the bottom 28 rows\n", rms);
        if (rms >= 5.0) {
            fprintf(stderr, "a stream of bubbles spread %.1f cells rms at its source\n", rms);
            ++failures;
        }
    }

    if (failures) return 1;
    printf("PASS: bubbles rise without wandering\n");
    return 0;
}
