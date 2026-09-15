/* --- steel can actually be made ------------------------------------------------

   Asked from play: "how do i make steel, and why cant i find that info in the
   steel page of the wiki".

   Steel has no recipe. It is a contact rule (g_matWetInto[MAT_IRON_MELT]):
   molten iron touching a burning coal fire -- Ember -- becomes molten steel,
   the ember is spent, and molten steel sets into Steel below 170 C. Nothing in
   the suite exercised it, so before the wiki and the smelting guide say how to
   make steel, this says it works in the simulation, the way a player would do
   it:

     a pool of molten iron with lit coal dropped on it makes steel
     and the same pool with no coal makes none              (the control)
     the molten steel sets as solid Steel when it cools

   Compile with every src/*.cpp except main.cpp. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include <stdio.h>

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static const int X = 1200, Y = 1600;

static int count(u8 mat) {
    int n = 0;
    for (int y = Y - 40; y <= Y + 10; ++y)
        for (int x = X - 30; x <= X + 30; ++x) n += g_world.at(x, y).mat == mat;
    return n;
}

/* A refractory basin holding a pool of molten iron. */
static void basin() {
    g_world.reset();
    g_world.setLiveWindow(X - 60, Y - 60, X + 60, Y + 30);
    for (int x = X - 20; x <= X + 20; ++x)
        for (int y = Y + 1; y <= Y + 4; ++y) g_world.setCell(x, y, MAT_REFRACTORY);
    for (int y = Y - 10; y <= Y; ++y) {
        for (int k = 0; k < 4; ++k) {
            g_world.setCell(X - 20 - k, y, MAT_REFRACTORY);
            g_world.setCell(X + 20 + k, y, MAT_REFRACTORY);
        }
    }
    for (int y = Y - 4; y <= Y; ++y)
        for (int x = X - 16; x <= X + 16; ++x) {
            g_world.setCell(x, y, MAT_IRON_MELT);
            g_world.temp[y * SIM_W + x] = degC(205);
        }
}

/* Keep every molten cell at a furnace's heat. A small pool in a cold
   refractory basin freezes solid within a hundred frames, and what is under test is the
   contact rule, not whether this particular basin is well insulated. */
static void heatFloor() {
    for (int y = Y - 12; y <= Y + 4; ++y)
        for (int x = X - 20; x <= X + 20; ++x) {
            const u8 m = g_world.at(x, y).mat;
            if (m == MAT_IRON_MELT || m == MAT_STEEL_MELT || m == MAT_REFRACTORY)
                g_world.temp[y * SIM_W + x] = degC(205);
        }
}

int main() {
    initMaterials();
    initItems();

    /* --- control: no coal ------------------------------------------------- */
    basin();
    for (int f = 0; f < 600; ++f) { heatFloor(); g_world.step(); }
    const int plain = count(MAT_STEEL_MELT) + count(MAT_STEEL);
    printf("  no coal: %d steel cells, %d molten iron\n", plain, count(MAT_IRON_MELT));
    check(count(MAT_IRON_MELT) > 100, "control: the pool is still molten after the run");
    check(plain == 0, "and molten iron alone makes no steel");

    /* --- coal dropped onto the pool ----------------------------------------
       Raw coal, as a player would pour it: it falls, lands on metal at 205 C,
       catches, and it is the burning ember touching the melt that makes steel.
       (Ember itself is static, so placing embers above the pool is a scene in
       which nothing ever touches.) */
    basin();
    for (int x = X - 10; x <= X + 10; ++x)
        for (int y = Y - 14; y <= Y - 12; ++y) g_world.setCell(x, y, MAT_COAL);
    int most = 0;
    for (int f = 0; f < 600; ++f) {
        heatFloor(); g_world.step();
        const int s = count(MAT_STEEL_MELT) + count(MAT_STEEL);
        if (s > most) most = s;
    }
    printf("  with coal: up to %d steel cells, %d molten iron left\n", most, count(MAT_IRON_MELT));
    check(most > 0, "molten iron touching burning coal becomes molten steel");

    /* --- and it sets ------------------------------------------------------- */
    for (int f = 0; f < 4000; ++f) g_world.step();   /* heat off: let it cool */
    const int solid = count(MAT_STEEL);
    printf("  after cooling: %d solid steel, %d molten steel\n", solid, count(MAT_STEEL_MELT));
    check(solid > 0, "molten steel sets as solid Steel when it cools");

    if (failures) { fprintf(stderr, "\n%d steel check(s) failed\n", failures); return 1; }
    printf("PASS\n");
    return 0;
}
