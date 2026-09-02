/* NOTE: writing temp[] directly is not enough to make anything happen -- the
   simulation only visits cells in chunks something has dirtied, so an
   undirtied hot cell sits there and is never updated. Every injection below
   calls dirtyPoint for that reason. */

/* --- does warm air rise? ---------------------------------------------------

   Conduction is the same in every direction, and for a solid that is right.
   For a room it is not: heat put into the air used to spread as a slowly
   growing disc, so a fire warmed the floor beside it exactly as readily as the
   ceiling above it, and nothing ever felt like it was rising.

   Air cannot use updateConvection -- that swaps whole parcels and air is
   MAT_EMPTY, which has no parcel -- so the buoyancy is applied to the heat
   instead. These are the properties that says it worked:

     1. Heat reaches FURTHER UP than sideways, and further up than down.
     2. It is conserved on the way. A plume that invents heat is a bug that
        ends with the whole map at 255.
     3. A ceiling stops it, so heat pools under a roof instead of leaking
        through it.
     4. The field still comes to rest. A bias with no floor under it shuffles
        the last degree upward forever and the simulation never settles.

   Compile with every source file except main.cpp. */

#include "world.h"
#include "materials.h"
#include <stdio.h>

static int failures = 0;

static void check(bool ok, const char* what) {
    printf("  %-56s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static const int CX = 1200, CY = 5000;

static int tempAt(int x, int y) { return (int)g_world.temp[y * SIM_W + x]; }

static void openAir() {
    g_world.reset();
    g_world.setLiveWindow(CX - 90, CY - 90, CX + 90, CY + 90);
}

/* How far the warmth got in one direction: the last cell along it that is
   still measurably above ambient. */
static int reach(int dx, int dy) {
    int far = 0;
    for (int d = 1; d <= 70; ++d) {
        const int x = CX + dx * d, y = CY + dy * d;
        if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) break;
        if (tempAt(x, y) > AMBIENT_TEMP + 1) far = d;
    }
    return far;
}

static int totalHeat(int r) {
    int sum = 0;
    for (int y = CY - r; y <= CY + r; ++y)
        for (int x = CX - r; x <= CX + r; ++x)
            sum += tempAt(x, y) - AMBIENT_TEMP;
    return sum;
}

int main() {
    initMaterials();

    /* --- 1. the plume goes up --------------------------------------------- */
    {
        openAir();
        /* A small hot patch of air, held for a while the way a fire would. */
        for (int f = 0; f < 400; ++f) {
            for (int y = CY - 1; y <= CY + 1; ++y)
                for (int x = CX - 1; x <= CX + 1; ++x)
                    { g_world.temp[y * SIM_W + x] = degC(180);
                      g_world.dirtyPoint(x, y); }
            g_world.step();
        }
        const int up = reach(0, -1), down = reach(0, 1);
        const int left = reach(-1, 0), right = reach(1, 0);
        printf("    reach: up %d, down %d, left %d, right %d\n", up, down, left, right);
        check(up > down,  "heat reaches further up than down");
        check(up > left && up > right, "and further up than sideways");
        /* Not a token difference -- a one-cell edge would be noise.

           Measured in the FAR field, which is where convection actually shows.
           Close to the source the upward cells read COOLER than the downward
           ones, and that is the mechanism rather than a contradiction: the
           column above is a conveyor, so heat passes through it instead of
           pooling, while heat below has nowhere to go but sideways. The
           difference is what happens at the far end. */
        check(up >= down + 5, "and it is a plume, not a rounding error");
        const int farUp   = tempAt(CX, CY - 30) - AMBIENT_TEMP;
        const int farDown = tempAt(CX, CY + 30) - AMBIENT_TEMP;
        printf("    thirty cells out: up %d above ambient, down %d\n",
               farUp, farDown);
        check(farUp > farDown * 3, "and the far field is dominated by the rise");
    }

    /* --- 2. conserved, not invented --------------------------------------- */
    {
        openAir();
        /* One hot cell, then left alone. Nothing may create heat: the total
           surplus must not grow once the source is removed. */
        g_world.temp[CY * SIM_W + CX] = degC(200);
        g_world.dirtyPoint(CX, CY);
        const int before = totalHeat(80);
        for (int f = 0; f < 200; ++f) g_world.step();
        const int after = totalHeat(80);
        check(after <= before, "a plume never invents heat");
    }

    /* --- 3. a ceiling stops it -------------------------------------------- */
    {
        openAir();
        for (int x = CX - 40; x <= CX + 40; ++x) g_world.setCell(x, CY - 10, MAT_STONE);
        for (int f = 0; f < 400; ++f) {
            for (int y = CY - 1; y <= CY + 1; ++y)
                for (int x = CX - 1; x <= CX + 1; ++x)
                    { g_world.temp[y * SIM_W + x] = degC(180);
                      g_world.dirtyPoint(x, y); }
            g_world.step();
        }
        const int under = tempAt(CX, CY - 9)  - AMBIENT_TEMP;
        const int above = tempAt(CX, CY - 20) - AMBIENT_TEMP;
        printf("    under the ceiling %d above ambient, over it %d\n", under, above);
        check(under > 1, "heat pools under a ceiling");
        /* Some gets through, and should: stone conducts, and air now exchanges
           with solids properly instead of behaving as an insulator. The ceiling
           is a strong barrier, not a perfect one. */
        check(above * 3 < under, "and only a fraction of it gets past");
    }

    /* --- 4. it still settles ---------------------------------------------- */
    {
        openAir();
        g_world.temp[CY * SIM_W + CX] = degC(120);
        g_world.dirtyPoint(CX, CY);
        for (int f = 0; f < 4000; ++f) g_world.step();
        int hottest = 0;
        for (int y = CY - 80; y <= CY + 80; ++y)
            for (int x = CX - 80; x <= CX + 80; ++x) {
                const int t = tempAt(x, y) - AMBIENT_TEMP;
                if (t > hottest) hottest = t;
            }
        check(hottest <= 1, "the field comes back to rest when the source stops");
    }

    if (failures == 0) { puts("PASS"); return 0; }
    fprintf(stderr, "%d convection check(s) failed\n", failures);
    return 1;
}
