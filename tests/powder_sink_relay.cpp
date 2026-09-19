/* --- a powder sinking into liquid displaces it one cell, not a column -------

   Reported from powderlike: dirt that fell into a pool of wax made the wax
   "teleport to the top of the dirt". It did. A sinking grain swaps with the
   liquid below it, which puts the liquid one cell up; the scan runs bottom to
   top, so the next grain up met the same parcel and swapped it again, and the
   next, and the parcel went to the top of the whole falling column in one
   pass. Measured: a 100-cell column of dirt threw wax up 100 cells in a frame.

   The scene is that measurement. Any wax cell that turns up somewhere with no
   wax nearby the frame before -- not a one-cell step, not a sideways hop along
   its row, not a fall -- has jumped, and the jump is measured to the nearest
   wax below it. One or two cells is the ordinary swap (one per scan pass);
   the relay was the whole column.

   Compile with every source file except main.cpp. No socket, no window. */

#include "world.h"
#include "materials.h"
#include <stdio.h>
#include <stdlib.h>

static World g_testWorld;
static const int X0 = 1100, Y0 = 2900, W = 240, H = 400;
static u8 prevWax[H][W];

static bool waxAt(int x, int y) { return g_testWorld.at(X0 + x, Y0 + y).mat == MAT_WAX; }

int main() {
    initMaterials();
    World& w = g_testWorld;
    w.reset();
    w.setLiveWindow(X0, Y0, X0 + W, Y0 + H);
    for (int x = X0; x < X0 + W; ++x) w.setCell(x, Y0 + H - 1, MAT_STONE);
    for (int y = Y0; y < Y0 + H; ++y) { w.setCell(X0, y, MAT_STONE); w.setCell(X0 + W - 1, y, MAT_STONE); }
    for (int y = Y0 + H - 61; y < Y0 + H - 1; ++y)
        for (int x = X0 + 1; x < X0 + W - 1; ++x) w.setCell(x, y, MAT_WAX);
    for (int y = Y0 + 100; y < Y0 + 200; ++y)
        for (int x = X0 + 110; x < X0 + 130; ++x) w.setCell(x, y, MAT_DIRT);

    int worst = 0;
    for (int f = 0; f < 400; ++f) {
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) prevWax[y][x] = waxAt(x, y);
        w.step();
        for (int y = 1; y < H - 1; ++y)
            for (int x = 1; x < W - 1; ++x) {
                if (!waxAt(x, y) || prevWax[y][x]) continue;
                bool legit = false;
                for (int dy = -1; dy <= 1 && !legit; ++dy)
                    for (int xx = 1; xx < W - 1 && !legit; ++xx)
                        if (prevWax[y + dy][xx] && (dy <= 0 || abs(xx - x) <= 1)) legit = true;
                if (legit) continue;
                int d = 2;
                while (y + d < H) {
                    bool found = false;
                    for (int xx = x - 1; xx <= x + 1; ++xx) if (prevWax[y + d][xx]) found = true;
                    if (found) break;
                    ++d;
                }
                if (d > worst) worst = d;
            }
    }
    if (worst > 3) {
        fprintf(stderr, "wax jumped %d cells up in one frame as dirt sank into it\n", worst);
        return 1;
    }
    printf("PASS: sinking dirt displaced wax at most %d cells a frame\n", worst);
    return 0;
}
