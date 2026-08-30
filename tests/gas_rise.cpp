/* --- Steam's rise boost must not turn fire into a rocket -------------------

   Steam was deliberately given a four-cell clear-air climb so it can escape a
   boiler despite spending most turns diffusing. The climb originally lived in
   the shared gas path, however, so low-jitter Fire and Plasma took that same
   four-cell move on nearly every turn and visibly outran Steam.

   Remove diffusion from one turn to isolate strict buoyancy. Steam must retain
   its four-cell boost, while Fire and Plasma retain the original one-cell rise.

   Compile with every source file except main.cpp. No socket, no window. */

#include "world.h"
#include "materials.h"
#include <stdio.h>

static World g_testWorld;

static int riseInOneTurn(u8 mat, int temperature) {
    const int x = 1200, y = 5000;
    g_testWorld.reset();
    g_testWorld.setLiveWindow(x - 8, y - 8, x + 8, y + 8);
    g_testWorld.setCell(x, y, mat);
    g_testWorld.temp[y * SIM_W + x] = temperature;
    g_testWorld.step();

    for (int ny = y - 8; ny <= y + 2; ++ny)
        if (g_testWorld.at(x, ny).mat == mat) return y - ny;
    return -999;
}

int main() {
    initMaterials();

    const u8 steamJitter = MATS[MAT_STEAM].jitter;
    const u8 fireJitter = MATS[MAT_FIRE].jitter;
    const u8 plasmaJitter = MATS[MAT_PLASMA].jitter;
    MATS[MAT_STEAM].jitter = 0;
    MATS[MAT_FIRE].jitter = 0;
    MATS[MAT_PLASMA].jitter = 0;

    const int steamRise = riseInOneTurn(MAT_STEAM, degC(115));
    const int fireRise = riseInOneTurn(MAT_FIRE, degC(205));
    const int plasmaRise = riseInOneTurn(MAT_PLASMA, degC(215));

    MATS[MAT_STEAM].jitter = steamJitter;
    MATS[MAT_FIRE].jitter = fireJitter;
    MATS[MAT_PLASMA].jitter = plasmaJitter;

    if (steamRise != 4 || fireRise != 1 || plasmaRise != 1) {
        fprintf(stderr, "gas rise mismatch: Steam %d, Fire %d, Plasma %d\n",
                steamRise, fireRise, plasmaRise);
        return 1;
    }

    printf("PASS: Steam rises 4 cells; Fire and Plasma rise 1\n");
    return 0;
}
