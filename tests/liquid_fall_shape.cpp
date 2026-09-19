/* --- falling water keeps its shape until it lands --------------------------

   Reported from powderlike: "water starts spreading out as it falls, it seems
   to think it's on land". It did. The fluid pass (FLUID_SUBSTEPS in world.h)
   gives RESTING liquid a second turn a frame and was meant to leave falling
   liquid alone, but it judged "falling" by the one cell underneath. Inside a
   falling blob that cell is more water, so every row but the bottom took the
   resting turn -- whose sideways hop found the open air either side of the
   blob and threw parcels out into it.

   The check: a 40 x 40 block of water let go well above the floor, looked at
   while it is still in the air. It must not have widened, and it must have
   fallen at one cell a frame -- the other thing the fluid pass must not do is
   make liquid fall faster.

   Compile with every source file except main.cpp. No socket, no window. */

#include "world.h"
#include "materials.h"
#include <stdio.h>

static World g_testWorld;

int main() {
    initMaterials();
    World& w = g_testWorld;
    w.reset();
    const int X = 1200, TOP = 3000, SIZE = 40, FLOOR = TOP + 600;
    w.setLiveWindow(X - 200, TOP - 20, X + SIZE + 200, FLOOR + 10);
    for (int x = X - 200; x <= X + SIZE + 200; ++x) w.setCell(x, FLOOR, MAT_STONE);
    for (int y = TOP; y < TOP + SIZE; ++y)
        for (int x = X; x < X + SIZE; ++x) w.setCell(x, y, MAT_WATER);

    const int FRAMES = 200;   /* lands after about 560 */
    for (int f = 0; f < FRAMES; ++f) w.step();

    int x0 = 1 << 30, x1 = -1, yTop = 1 << 30, yBottom = -1;
    for (int y = TOP - 20; y < FLOOR; ++y)
        for (int x = X - 200; x <= X + SIZE + 200; ++x)
            if (w.at(x, y).mat == MAT_WATER) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < yTop) yTop = y;
                if (y > yBottom) yBottom = y;
            }
    const int width = x1 - x0 + 1;
    const int fell = yBottom - (TOP + SIZE - 1);

    int failures = 0;
    /* A little ragged-edge allowance: a falling body is not a rigid sprite. */
    if (width > SIZE + 4) {
        fprintf(stderr, "falling water spread to %d wide (started %d)\n", width, SIZE);
        ++failures;
    }
    if (fell > FRAMES + 2) {
        fprintf(stderr, "water fell %d cells in %d frames -- faster than one a frame\n",
                fell, FRAMES);
        ++failures;
    }
    if (fell < FRAMES - 2) {
        fprintf(stderr, "water fell only %d cells in %d frames\n", fell, FRAMES);
        ++failures;
    }
    if (failures) return 1;
    printf("PASS: a %d-wide block of water fell %d cells in %d frames and stayed %d wide\n",
           SIZE, fell, FRAMES, width);
    return 0;
}
