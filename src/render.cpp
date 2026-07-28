#include "render.h"

/* Anything outside the world. Should never be visible once the camera is
   clamped, but rendering it as a flat colour rather than reading out of bounds
   means a clamping bug shows up as an obvious black band instead of as garbage
   pixels or a crash. */
static const u32 VOID_COLOUR = 0x000000;

/* An empty cell shows whatever is BEHIND it. Only empty cells pay for this --
   material in front hides the background completely, so the lookup sits inside
   the branch that was already testing for air. */
static inline u32 backdrop(const World& w, int wx, int wy, int i) {
    const u8 b = (u8)(w.bg[i] & BG_MAT_MASK);
    if (b) return g_bgColorLut[((u32)b << 4) | bgSpeckle(wx, wy)];

    /* Nothing behind this cell at all. What you see then is the chunk's ZONE,
       looked up as a label -- not worked out from wy. The ramp below is indexed
       by depth, but only AFTER the label has decided you are looking at sky, so
       terrain of any shape gets the backdrop generation gave it rather than one
       inferred from how high it happens to be. */
    const int cx = wx >> CHUNK_SHIFT, cy = wy >> CHUNK_SHIFT;
    if (w.zone[cy * CHUNKS_X + cx] != ZONE_SKY)
        return g_caveLut[bgSpeckle(wx, wy)];

    u32 c = g_skyLut[wy < SKY_BAND ? (wy < 0 ? 0 : wy) : SKY_BAND - 1];

    /* --- the join ---------------------------------------------------------
       Sky and underground meet on a hard 32-cell chunk edge, and anywhere that
       edge crosses open air -- a shaft, a cutting, a cave mouth -- a colour
       step draws a ruled line across the world. Rendered, it was unmistakable:
       bright blue directly above pitch black, in a straight line, at a height
       that had nothing to do with the terrain.

       Shaping the sky ramp to land on the cave colour cannot fix it, because
       the depth of the boundary is different in every column -- which is the
       entire reason zones are labels and not a depth test. So the blend goes
       where the problem is: the LAST sky chunk before underground fades across
       its own 32 rows into the cave colour. One extra array read, and only for
       sky cells. */
    const int below = cy + 1;
    if (below >= CHUNKS_Y || w.zone[below * CHUNKS_X + cx] != ZONE_SKY)
        c = lerpColor(c, g_caveLut[bgSpeckle(wx, wy)], (wy & (CHUNK - 1)) * 8);
    return c;
}

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
                row[vx] = (c.mat == MAT_EMPTY)
                        ? backdrop(w, camX + vx, wy, i)
                        : g_colorLut[((u32)c.mat << 8) | (u32)(c.moisture & 0xF0) | (u32)(c.tint >> 4)];
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
            u32 col = (c.mat == MAT_EMPTY)
                    ? backdrop(w, camX + vx, wy, i)
                    : g_colorLut[((u32)c.mat << 8) | (u32)(c.moisture & 0xF0) | (u32)(c.tint >> 4)];
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
