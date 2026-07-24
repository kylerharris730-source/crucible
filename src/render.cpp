#include "render.h"

/* One linear pass. The material colour is a single lookup into the
   precomputed palette -- no arithmetic beyond shifts. */
int renderWorld(const World& w, u32* out, int view) {
    const Cell* cells = w.cells;
    const u8*   temp  = w.temp;
    const int   n     = SIM_W * SIM_H;
    int count = 0;

    if (view == VIEW_HEAT) {
        for (int i = 0; i < n; ++i) {
            out[i] = g_heatLut[temp[i]];
            count += (cells[i].mat != MAT_EMPTY && cells[i].mat != MAT_WALL);
        }
        return count;
    }

    if (view == VIEW_MATERIAL) {
        /* Material only -- no glow at all. Cheapest path, and the one to use
           when the heat overlay is getting in the way of seeing the sim. */
        for (int i = 0; i < n; ++i) {
            Cell c = cells[i];
            out[i] = g_colorLut[((u32)c.mat << 8) | (u32)(c.moisture & 0xF0) | (u32)(c.tint >> 4)];
            count += (c.mat != MAT_EMPTY && c.mat != MAT_WALL);
        }
        return count;
    }

    /* VIEW_NORMAL: material plus a heat glow. Anything at ambient skips the
       blend entirely, and since almost every cell in a typical scene is at
       ambient, that branch predicts nearly perfectly. The glow alpha is capped
       in the LUT so hot material stays visible rather than washing to white. */
    for (int i = 0; i < n; ++i) {
        Cell c = cells[i];
        u32 col = g_colorLut[((u32)c.mat << 8) | (u32)(c.moisture & 0xF0) | (u32)(c.tint >> 4)];
        const u8 t = temp[i];
        if (t > AMBIENT_TEMP) col = lerpColor(col, g_heatLut[t], g_heatAlpha[t]);
        out[i] = col;
        count += (c.mat != MAT_EMPTY && c.mat != MAT_WALL);
    }
    return count;
}
