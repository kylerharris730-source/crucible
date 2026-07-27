#include "render.h"

/* Anything outside the world. Should never be visible once the camera is
   clamped, but rendering it as a flat colour rather than reading out of bounds
   means a clamping bug shows up as an obvious black band instead of as garbage
   pixels or a crash. */
static const u32 VOID_COLOUR = 0x000000;

/* One linear pass per visible row. The material colour is a single lookup into
   the precomputed palette -- no arithmetic beyond shifts. */
int renderView(const World& w, u32* out, int view, int camX, int camY) {
    const Cell* cells = w.cells;
    const u8*   temp  = w.temp;
    int count = 0;

    for (int vy = 0; vy < VIEW_CELLS_H; ++vy) {
        const int wy = camY + vy;
        u32* row = out + vy * VIEW_CELLS_W;

        if (wy < 0 || wy >= SIM_H) {
            for (int vx = 0; vx < VIEW_CELLS_W; ++vx) row[vx] = VOID_COLOUR;
            continue;
        }

        /* The horizontal span that is actually inside the world, so the inner
           loops carry no bounds test at all. */
        int vx0 = 0, vx1 = VIEW_CELLS_W;
        if (camX < 0)                   vx0 = -camX;
        if (camX + vx1 > SIM_W)         vx1 = SIM_W - camX;
        if (vx0 > VIEW_CELLS_W) vx0 = VIEW_CELLS_W;
        if (vx1 < vx0)          vx1 = vx0;

        for (int vx = 0; vx < vx0; ++vx)               row[vx] = VOID_COLOUR;
        for (int vx = vx1; vx < VIEW_CELLS_W; ++vx)    row[vx] = VOID_COLOUR;

        const int base = wy * SIM_W + camX;

        if (view == VIEW_HEAT) {
            for (int vx = vx0; vx < vx1; ++vx) {
                const int i = base + vx;
                row[vx] = g_heatLut[temp[i]];
                count += (cells[i].mat != MAT_EMPTY && cells[i].mat != MAT_WALL);
            }
            continue;
        }

        if (view == VIEW_MATERIAL) {
            /* Material only -- no glow at all. Cheapest path, and the one to
               use when the heat overlay is getting in the way of seeing the
               sim. */
            for (int vx = vx0; vx < vx1; ++vx) {
                const int i = base + vx;
                Cell c = cells[i];
                row[vx] = g_colorLut[((u32)c.mat << 8) | (u32)(c.moisture & 0xF0) | (u32)(c.tint >> 4)];
                count += (c.mat != MAT_EMPTY && c.mat != MAT_WALL);
            }
            continue;
        }

        /* VIEW_NORMAL: material plus a temperature tint -- a heat glow above
           ambient and a cold blue below it. Anything AT ambient skips the blend
           entirely, and since almost every cell in a typical scene is at
           ambient, that branch predicts nearly perfectly. The test is != rather
           than > because the scale now has a cold half; with > it, everything
           frozen rendered as untinted material and ice was invisible as ice.
           The alpha is capped in the LUT at both ends, so material stays
           recognisable rather than washing out to white or to flat blue. */
        for (int vx = vx0; vx < vx1; ++vx) {
            const int i = base + vx;
            Cell c = cells[i];
            u32 col = g_colorLut[((u32)c.mat << 8) | (u32)(c.moisture & 0xF0) | (u32)(c.tint >> 4)];
            const u8 t = temp[i];
            /* g_matGlows lets a material opt out of the overlay entirely
               (plasma does; see materials.h). Deliberately a flag and not a
               0..255 scale: a scale would mean an extra multiply and shift on
               the alpha, which changes every OTHER material's blend by a
               rounding step for no benefit, since nothing wants a partial glow.
               The test also sits second in an && whose first half is already
               false for the overwhelming majority of cells. */
            if (t != AMBIENT_TEMP && g_matGlows[c.mat])
                col = lerpColor(col, g_heatLut[t], g_heatAlpha[t]);
            row[vx] = col;
            count += (c.mat != MAT_EMPTY && c.mat != MAT_WALL);
        }
    }
    return count;
}
