/* --- hot air rises, and keeps rising -----------------------------------------

   Asked for: "i want more convection in air, like a lot more, hot air rises."

   It already did, one cell a frame. That is diffusion rather than convection,
   and it lost: AIR_COOL bleeds an air cell toward ambient faster than a
   one-cell-a-frame climb can carry it, so a floor held at 200 C under a
   200-cell chimney warmed the bottom forty cells and stopped there forever.

   Four properties, and the last two are what stop "more convection" from
   turning into "heat goes everywhere":

     a plume climbs a long way          (the request)
     it reaches a ceiling far above     (not just further, all the way)
     a SOLID ceiling still stops it     (heat pools under a roof)
     it does not push into warmer air   (buoyancy, not a pump)

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "multiplayer.h"
#include <stdio.h>

static World g_testWorld;
static const int CX = 1400, CY = 5000, H = 200;

static void fill(World& w, int x0, int y0, int x1, int y1, u8 m) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1)
                w.setCell(x, y, m);
}

/* A chimney `tall` cells high with a floor held hot by the caller. */
static void chimney(World& w, int tall) {
    w.reset();
    fill(w, CX - 30, CY - tall - 20, CX + 30, CY + 20, MAT_STONE);
    fill(w, CX - 10, CY - tall, CX + 10, CY, MAT_EMPTY);
    w.setLiveWindow(CX - 40, CY - tall - 30, CX + 40, CY + 30);
}

static void burn(World& w, int frames) {
    for (int f = 0; f < frames; ++f) {
        for (int x = CX - 10; x <= CX + 10; ++x)
            w.temp[CY * SIM_W + x] = degC(200);
        w.dirtyPoint(CX, CY);
        w.step();
    }
}

/* Highest air cell at or above `c` degrees, as cells above the floor. */
static int front(const World& w, int tall, int c) {
    const u8 t = degC(c);
    for (int y = CY - tall; y <= CY; ++y)
        for (int x = CX - 10; x <= CX + 10; ++x)
            if (w.at(x, y).mat == MAT_EMPTY && w.temp[y * SIM_W + x] >= t)
                return CY - y;
    return 0;
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();
    World& w = g_testWorld;
    int failures = 0;

    /* --- 1 & 2. a plume climbs, and reaches the top --------------------- */
    {
        chimney(w, H);
        burn(w, 1200);
        const int f40 = front(w, H, 40);
        const int ceiling = (int)w.temp[(CY - H + 2) * SIM_W + CX] - 40;
        printf("200-cell chimney after 1200f: 40C front %d cells up, "
               "ceiling %d C\n", f40, ceiling);
        if (f40 < 120) {
            fprintf(stderr, "FAIL: the front only reached %d cells -- before the "
                            "reach was added it stalled at 42, so anything near "
                            "that is diffusion again\n", f40);
            ++failures;
        }
        if (ceiling <= 20) {
            fprintf(stderr, "FAIL: the ceiling is still at ambient after twenty "
                            "seconds of a 200 C floor\n");
            ++failures;
        }
    }

    /* --- 3. a lid stops it ----------------------------------------------- */
    {
        chimney(w, H);
        /* Seal the shaft a third of the way up. Heat must pool under the slab
           and must not appear above it, or "warm air rises" has quietly become
           "warm air goes through walls". */
        fill(w, CX - 10, CY - 70, CX + 10, CY - 66, MAT_STONE);
        burn(w, 1200);
        int hottestAbove = 0;
        for (int y = CY - H; y < CY - 70; ++y)
            for (int x = CX - 10; x <= CX + 10; ++x)
                if (w.at(x, y).mat == MAT_EMPTY) {
                    const int t = (int)w.temp[y * SIM_W + x] - 40;
                    if (t > hottestAbove) hottestAbove = t;
                }
        const int under = (int)w.temp[(CY - 65) * SIM_W + CX] - 40;
        printf("with a slab at 70 cells: %d C just under it, hottest air above "
               "it %d C\n", under, hottestAbove);
        if (under <= 25) {
            fprintf(stderr, "FAIL: heat did not pool under the slab (%d C)\n",
                    under);
            ++failures;
        }
        if (hottestAbove > 30) {
            fprintf(stderr, "FAIL: air above a solid slab reached %d C -- the "
                            "plume is climbing through rock\n", hottestAbove);
            ++failures;
        }
    }

    /* --- 4. it does not push into warmer air ----------------------------- */
    {
        chimney(w, H);
        /* A hot cap sitting above a cooler column. Nothing below it is warmer,
           so nothing should climb into it and it should only lose heat by the
           ordinary drift -- never gain any. */
        for (int y = CY - 120; y <= CY - 100; ++y)
            for (int x = CX - 10; x <= CX + 10; ++x)
                w.temp[y * SIM_W + x] = degC(150);
        int before = 0;
        for (int y = CY - 120; y <= CY - 100; ++y)
            for (int x = CX - 10; x <= CX + 10; ++x)
                before += (int)w.temp[y * SIM_W + x];
        for (int f = 0; f < 200; ++f) w.step();     /* no floor heat at all */
        int after = 0;
        for (int y = CY - 120; y <= CY - 100; ++y)
            for (int x = CX - 10; x <= CX + 10; ++x)
                after += (int)w.temp[y * SIM_W + x];
        printf("hot cap with cool air below: total heat %d -> %d\n",
               before, after);
        if (after > before) {
            fprintf(stderr, "FAIL: a hot pocket GAINED heat from cooler air "
                            "beneath it -- convection is acting as a pump\n");
            ++failures;
        }
    }

    if (failures) {
        fprintf(stderr, "\n%d air convection check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
