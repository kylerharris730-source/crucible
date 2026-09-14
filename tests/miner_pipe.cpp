/* --- a miner and a placer on an item pipe -----------------------------------

   Reported in play: "miner cant connect to item pipe". It could not: pipes
   join only machines on the logistics list, and the miner and placer were never
   on it, so a pipe laid against one did nothing at all and said nothing about
   why.

   This builds the contraption somebody actually wants -- a miner digging stone,
   a pipe carrying it, and a placer laying it somewhere else -- and checks each
   link of the chain rather than only the end, so a failure says WHICH link:

     the miner snaps to the same lattice as the pipe   (or they can never touch)
     the stone leaves the miner's buffer               (the miner sends)
     it arrives in the placer's buffer                 (the placer receives)
     and comes out of the placer into the world        (the whole chain works)

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "device.h"
#include "multiplayer.h"
#include <stdio.h>

static World g_testWorld;
static int failures = 0;

static void check(bool ok, const char* what) {
    printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

/* The centre of lattice slot (col, row). Logistics machines snap to a DEV_W
   grid anchored at the play origin, so aiming at a slot's centre lands exactly
   on it whatever the snapping rule does with an off-centre click. */
static int slotX(int col) { return PLAY_X0 + col * DEV_W + DEV_W / 2; }
static int slotY(int row) { return PLAY_Y0 + row * DEV_H + DEV_H / 2; }

int main() {
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();
    World& w = g_testWorld;
    w.reset();
    devClear();

    const int COL = 64, ROW = 44;

    /* The miner, then a pipe directly to its right, then the placer to the
       right of the pipe: three machines edge to edge in one row. */
    const bool placedMiner  = devPlace(w, DEV_MINER,  slotX(COL),     slotY(ROW));
    const bool placedPipe   = devPlace(w, DEV_PIPE,   slotX(COL + 1), slotY(ROW));
    const bool placedPlacer = devPlace(w, DEV_PLACER, slotX(COL + 2), slotY(ROW));
    check(placedMiner && placedPipe && placedPlacer, "a miner, a pipe and a placer can be placed in a row");
    if (!(placedMiner && placedPipe && placedPlacer)) return 1;

    Device* miner  = devAt(slotX(COL),     slotY(ROW));
    Device* pipe   = devAt(slotX(COL + 1), slotY(ROW));
    Device* placer = devAt(slotX(COL + 2), slotY(ROW));
    if (!miner || !pipe || !placer) { fprintf(stderr, "lost a machine\n"); return 2; }

    /* Exactly edge to edge. If the miner were free-placed while the pipe
       snapped, these could differ by a few cells and never join. */
    check(miner->x + DEV_W == pipe->x && miner->y == pipe->y,
          "the miner sits exactly against the pipe");
    check(pipe->x + DEV_W == placer->x && pipe->y == placer->y,
          "and so does the placer");

    /* The miner digs a 4x4 of stone below itself, always on. */
    miner->face = 0;
    miner->value = 4;
    devSetRunMode(*miner, DEVRUN_ALWAYS_ON);
    for (int layer = 0; layer < 4; ++layer)
        for (int a = 0; a < 4; ++a) {
            int x, y; devWorkCell(*miner, a, layer, &x, &y);
            w.setCell(x, y, MAT_STONE);
        }

    /* The placer lays into a 3x3 below itself, always on. Stone is static, so
       what it places stays where it lands and can be counted. */
    placer->face = 0;
    placer->value = 3;
    devSetRunMode(*placer, DEVRUN_ALWAYS_ON);

    w.setLiveWindow(miner->x - 40, miner->y - 40, placer->x + DEV_W + 40, placer->y + DEV_H + 40);

    int minerPeak = 0, placerPeak = 0;
    for (int t = 0; t < 120; ++t) {
        devTick(w);
        if (miner->count > minerPeak) minerPeak = miner->count;
        if (placer->count > placerPeak) placerPeak = placer->count;
    }

    int placed = 0, dugLeft = 0;
    for (int layer = 0; layer < 3; ++layer)
        for (int a = 0; a < 3; ++a) {
            int x, y; devWorkCell(*placer, a, layer, &x, &y);
            if (w.at(x, y).mat == MAT_STONE) ++placed;
        }
    for (int layer = 0; layer < 4; ++layer)
        for (int a = 0; a < 4; ++a) {
            int x, y; devWorkCell(*miner, a, layer, &x, &y);
            if (w.at(x, y).mat != MAT_EMPTY) ++dugLeft;
        }

    printf("after 120 frames: miner holds %d (peak %d), placer holds %d (peak %d), "
           "%d of 9 cells placed, %d of 16 cells left undug\n",
           (int)miner->count, minerPeak, (int)placer->count, placerPeak, placed, dugLeft);

    check(dugLeft == 0, "the miner dug its square");
    check(minerPeak > 0 && miner->count < minerPeak,
          "and the pipe took the stone back out of its buffer");
    check(placed > 0, "the stone came out of the placer on the far side");
    check(placed == 9, "enough of it to fill the placer's whole square");

    if (failures) { fprintf(stderr, "\n%d miner/pipe check(s) failed\n", failures); return 1; }
    printf("\nPASS\n");
    return 0;
}
