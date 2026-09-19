/* --- a liquid's sideways hop must fit inside a stripe ----------------------

   The hop at the bottom of updateLiquid reaches dispersion + PRESSURE_MAX cells
   sideways in one turn, and stripes run in parallel on the promise that no rule
   writes further than RULE_WRITE_REACH_X - 1 (see world.h). Break that promise
   and the result is not a wrong picture but a data race: two threads writing
   the same cell, a rare corrupt cell on somebody else's machine.

   dispersion lives in the material table and PRESSURE_MAX in a header, so
   raising either one quietly can break the other. This holds the sum.

   Compile with every source file except main.cpp. No socket, no window. */

#include "world.h"
#include "materials.h"
#include <stdio.h>

int main() {
    initMaterials();
    int worst = 0, worstMat = 0;
    for (int m = 0; m < MAT_COUNT; ++m) {
        if (MATS[m].kind != KIND_LIQUID) continue;
        const int reach = (int)MATS[m].dispersion + PRESSURE_MAX;
        if (reach > worst) { worst = reach; worstMat = m; }
    }
    const int limit = RULE_WRITE_REACH_X - 1;
    if (worst > limit) {
        fprintf(stderr, "%s hops %d cells sideways (dispersion %d + PRESSURE_MAX %d); "
                "stripes allow %d\n", MATS[worstMat].name, worst,
                (int)MATS[worstMat].dispersion, PRESSURE_MAX, limit);
        return 1;
    }
    printf("PASS: widest liquid hop is %d cells (%s), stripes allow %d\n",
           worst, MATS[worstMat].name, limit);
    return 0;
}
