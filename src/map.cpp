#include "map.h"
#include "light.h"    /* VIEW_CELLS_W/H: what "the camera can see" means */
#include <string.h>

u8 g_map[MAP_W * MAP_H];

void mapClear() { memset(g_map, MAP_UNSEEN, sizeof(g_map)); }

/* --- which material represents 8x8 cells -----------------------------------

   Not "the most common one", which sounds right and produces a grey map: an
   ore vein is a few cells in a block of stone, so the majority answer is stone
   every time and the one thing you wanted to see from the map is the one thing
   it hides.

   So: the most NOTABLE thing present, ranked. Anything you would walk across a
   cave to reach beats the rock it is embedded in. This is the same reasoning
   the world overview harness uses, and it is why a vein reads as a vein on the
   map rather than as a slightly different shade of grey. */
static int notability(u8 m) {
    if (m == MAT_EMPTY) return 0;
    const u8 k = MATS[m].kind;
    if (g_matStation[m])          return 90;   /* your own workshop */
    if (m == MAT_DEVICE)          return 85;
    if (g_matSmeltYield[m])       return 80;   /* anything that smelts: ore */
    if (k == KIND_LIQUID)         return 40;   /* water and lava are landmarks */
    if (g_matIsPlant[m])          return 30;
    if (k == KIND_GAS)            return 5;
    return 10;                                 /* rock, dirt, the background */
}

/* --- revealing ------------------------------------------------------------

   Every map pixel covers 64 cells, and the visible area is 64 x 48 map pixels,
   so a full refresh of the view is about 196,000 reads. That is affordable once
   and wasteful sixty times a second for a picture that only changes when the
   player walks.

   So the view is refreshed in SLICES: one row band of map pixels per call,
   cycling. The whole view is covered every MAP_BANDS calls -- a fifth of a
   second -- which is far faster than anyone can outrun, and costs a fixed
   ~4,000 reads a frame.

   The cursor is a static rather than a member because there is exactly one map
   and exactly one camera; threading it through the call would be ceremony. */
static const int MAP_BANDS = 12;
static int g_band = 0;

void mapReveal(const World& w, int camX, int camY) {
    const int mx0 = camX >> MAP_SHIFT, my0 = camY >> MAP_SHIFT;
    const int mw  = (VIEW_CELLS_W >> MAP_SHIFT) + 1;
    const int mh  = (VIEW_CELLS_H >> MAP_SHIFT) + 1;

    g_band = (g_band + 1) % MAP_BANDS;

    for (int by = g_band; by < mh; by += MAP_BANDS) {
        const int my = my0 + by;
        if (my < 0 || my >= MAP_H) continue;
        for (int bx = 0; bx < mw; ++bx) {
            const int mx = mx0 + bx;
            if (mx < 0 || mx >= MAP_W) continue;

            const int x0 = mx << MAP_SHIFT, y0 = my << MAP_SHIFT;
            u8  best = MAP_AIR;
            int bestN = -1;
            for (int y = y0; y < y0 + MAP_CELL; ++y)
                for (int x = x0; x < x0 + MAP_CELL; ++x) {
                    const u8 m = w.at(x, y).mat;
                    const int n = notability(m);
                    if (n > bestN) { bestN = n; best = (n == 0) ? MAP_AIR : m; }
                }
            g_map[my * MAP_W + mx] = best;
        }
    }
}

bool mapColour(int mx, int my, u32* out) {
    if (mx < 0 || mx >= MAP_W || my < 0 || my >= MAP_H) return false;
    const u8 m = g_map[my * MAP_W + mx];
    if (m == MAP_UNSEEN) return false;
    if (m == MAP_AIR) {
        /* Explored emptiness. Deliberately NOT black -- black is what
           unexplored looks like, and "I have been here" is the whole point of
           the distinction. A dark blue-grey reads as open space. */
        *out = 0x1A1F28;
        return true;
    }
    /* Straight through the material LUT at a mid tint, so the map is the same
       colour as the world and nobody has to learn a second palette. */
    *out = g_colorLut[((u32)m << 8) | 0x08];
    return true;
}

int mapSeenCount() {
    int n = 0;
    for (int i = 0; i < MAP_W * MAP_H; ++i) if (g_map[i] != MAP_UNSEEN) ++n;
    return n;
}
