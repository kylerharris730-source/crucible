#include "render.h"
#include "light.h"

/* Anything outside the world. Should never be visible once the camera is
   clamped, but rendering it as a flat colour rather than reading out of bounds
   means a clamping bug shows up as an obvious black band instead of as garbage
   pixels or a crash. */
static const u32 VOID_COLOUR = 0x000000;

/* An empty cell shows whatever is BEHIND it. Only empty cells pay for this --
   material in front hides the background completely, so the lookup sits inside
   the branch that was already testing for air. */
/* `openSky` comes back true only for the one case that must NOT be shaded: a
   cell with nothing behind it at all, in a chunk labelled sky. The sky is
   effectively infinitely far away, so nothing in the world can cast a shadow
   on it -- and once skylight came from several directions, something did. A
   slab floating over open ground drew a soft grey cone onto the sky beneath
   itself, which reads as fog rather than as shadow.

   Everything else still shades: placed and natural background, and the cave
   backdrop underground. Looking up out of a shaft you see real sky and it is
   bright; looking at the back wall of the shaft you see stone and it is dark.
   That is the distinction that matters and it is the one this makes. */
static inline u32 backdrop(const World& w, int wx, int wy, int i, u32 sky, bool* openSky) {
    const u8 raw = w.bg[i];
    const u8 b = (u8)(raw & BG_MAT_MASK);
    if (b) {
        /* Built or natural. The bit is already in hand, so telling the two apart
           costs one test -- see g_bgPlacedLut for why they should not look the
           same. */
        const u32 idx = ((u32)b << 4) | bgSpeckle(wx, wy);
        return (raw & BG_PLACED) ? g_bgPlacedLut[idx] : g_bgColorLut[idx];
    }

    /* Nothing behind this cell at all. What you see then is the chunk's ZONE,
       looked up as a label -- not worked out from wy. The ramp below is indexed
       by depth, but only AFTER the label has decided you are looking at sky, so
       terrain of any shape gets the backdrop generation gave it rather than one
       inferred from how high it happens to be. */
    const int cx = wx >> CHUNK_SHIFT, cy = wy >> CHUNK_SHIFT;

    if (w.zone[cy * CHUNKS_X + cx] == ZONE_SKY) { *openSky = true; return sky; }

    /* --- the join ---------------------------------------------------------
       Sky and underground meet on a hard 32-cell chunk edge, and anywhere that
       edge crosses open air -- a shaft, a cave mouth -- a colour step would
       draw a ruled line across the world. So the FIRST UNDERGROUND chunk fades
       from sky to cave across its own 32 rows.

       It has to be the first underground chunk, not the last sky one. Fading
       the last sky chunk is what this did first, and it was wrong the moment
       terrain stopped being flat: that chunk is the one the SURFACE passes
       through, so most of it is open air, and the fade painted a dark band
       across the sky above every hillside. Below the boundary there is nothing
       but rock -- generation puts the boundary under the deepest ground in the
       column -- so fading downward from it is always hidden. */
    const int zone = w.zone[cy * CHUNKS_X + cx];
    const u32* lut = g_caveLut[caveLayerOf(zone)];

    const int above = cy - 1;
    if (above >= 0 && w.zone[above * CHUNKS_X + cx] == ZONE_SKY)
        return lerpColor(sky, lut[bgSpeckle(wx, wy)], (wy & (CHUNK - 1)) * 8);

    /* A layer boundary gets the same treatment the sky/underground join does,
       and needs it for the same reason: the zones change on a hard 32-cell
       chunk edge, so wherever that edge crosses open air an abrupt change of
       backdrop would draw a ruled line across the world. Fading down through
       the first chunk of the new layer hides the step in the same place the
       barrier itself sits, which is solid rock. */
    if (above >= 0) {
        const int azone = w.zone[above * CHUNKS_X + cx];
        if (azone != zone)
            return lerpColor(g_caveLut[caveLayerOf(azone)][bgSpeckle(wx, wy)],
                             lut[bgSpeckle(wx, wy)], (wy & (CHUNK - 1)) * 8);
    }

    return lut[bgSpeckle(wx, wy)];
}

/* One linear pass per visible row. The material colour is a single lookup into
   the precomputed palette -- no arithmetic beyond shifts. */
int renderView(const World& w, u32* out, int view, int camX, int camY, bool lit) {
    const Cell* cells = w.cells;
    const u8*   temp  = w.temp;
    int count = 0;
    /* Constant for the entire frame. backdrop() used to call dayLight() for
       every empty visible cell, then repeat the identical sky blend across all
       512 cells of a row. A large excavated cavern made that nearly 200,000
       floating-point daylight calculations per frame even though the result
       only varies with Y. */
    const int daylight = dayLight();

    for (int vy = 0; vy < VIEW_CELLS_H; ++vy) {
        const int wy = camY + vy;
        u32* row = out + vy * VIEW_CELLS_W;
        const u8* lrow = lit ? lightRow(vy) : 0;

        if (wy < 0 || wy >= SIM_H) {
            for (int vx = 0; vx < VIEW_CELLS_W; ++vx) row[vx] = VOID_COLOUR;
            continue;
        }

        const u32 daySky = g_skyLut[wy < SKY_BAND ? (wy < 0 ? 0 : wy) : SKY_BAND - 1];
        /* Lighting already makes night dim; tinting the backdrop as well makes
           it read as night before the player has a roof over their head. The
           moon and sun are drawn later as distant overlays, so this is just the
           row's sky colour, shared by every empty cell on it. */
        const u32 rowSky = lerpColor(0x07101F, daySky, daylight);

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
                bool openSky = false;
                u32 col = g_matUnseen[c.mat]
                        ? backdrop(w, camX + vx, wy, i, rowSky, &openSky)
                        : g_colorLut[((u32)c.mat << 8) | (u32)(c.moisture & 0xF0) | (u32)(c.tint >> 4)];
                if ((c.mat == MAT_SIEVE || c.mat == MAT_GAS_SIEVE) && c.moisture) {
                    const u32 fluid = g_colorLut[((u32)c.moisture << 8) | (u32)(c.tint >> 4)];
                    col = lerpColor(col, fluid, 112);
                }
                row[vx] = (lrow && !openSky) ? shadeColor(col, g_lightShade[lrow[vx]]) : col;
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
            bool openSky = false;
            /* g_matUnseen rather than a test against MAT_EMPTY: a torch's cells
               are drawn by its sprite, not by the material, so they take the
               backdrop like empty air. See g_matUnseen in materials.h. */
            u32 col = g_matUnseen[c.mat]
                    ? backdrop(w, camX + vx, wy, i, rowSky, &openSky)
                    : g_colorLut[((u32)c.mat << 8) | (u32)(c.moisture & 0xF0) | (u32)(c.tint >> 4)];
            /* A sieve can hold one coexisting fluid parcel in moisture. Keep
               the mesh visible, but tint it toward its contents so water,
               glowfluid or steam advancing through it is observable rather
               than hidden state. Both sieve palettes have identical dry/wet
               endpoints, so the overloaded moisture byte does not otherwise
               distort their own colour lookup above. */
            if ((c.mat == MAT_SIEVE || c.mat == MAT_GAS_SIEVE) && c.moisture) {
                const u32 fluid = g_colorLut[((u32)c.moisture << 8) | (u32)(c.tint >> 4)];
                col = lerpColor(col, fluid, 112);
            }
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
            /* Shading comes AFTER the heat glow, so a hot cell in the dark is
               dimmed like everything else rather than punching through the
               shadow at full brightness. It does not go dark: anything hot
               enough to glow is in g_matLight and lights its own cell. */
            row[vx] = (lrow && !openSky) ? shadeColor(col, g_lightShade[lrow[vx]]) : col;
            count += (c.mat != MAT_EMPTY && c.mat != MAT_WALL);
        }
    }
    return count;
}
