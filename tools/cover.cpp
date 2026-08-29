/* ============================================================================
   cover.cpp -- generates the share image, using the game's own renderer.

   The first cover was a screenshot, and a screenshot can only ever show the
   place the camera happened to be: the spawn point, half empty sky, none of
   the heat or light the game is actually about. This builds the shot instead.
   It generates a real world, writes CINDERLIFT into it out of molten rock,
   and renders it through renderView() and the real lighting solver -- so the
   glow around the letters is the game's light field doing its ordinary job,
   not an effect painted on afterwards.

   Nothing here is a mock-up. Change a material colour or the light falloff and
   this image changes with it, because it is the same code path the game draws
   with.

   Build (all of src except main.cpp, exactly like the test harnesses):

     g++ -std=c++11 -O2 -Isrc tools/cover.cpp <src/*.cpp except main> \
         -o build/cover.exe -lgdi32 -luser32 -lwinmm -lmsimg32 -lws2_32

   Writes PPM, which is a header and raw bytes -- no PNG encoder, no image
   library, and scripts/ppm_to_png.py turns them into the real thing.
   ========================================================================== */
#include "world.h"
#include "materials.h"
#include "render.h"
#include "light.h"
#include "worldgen.h"
#include "item.h"
#include "sprite.h"
#include "multiplayer.h"
#include "web/font8x8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static World g_coverWorld;
static u32   g_view[VIEW_CELLS_W * VIEW_CELLS_H];

/* --- the crop -------------------------------------------------------------
   1200x630 is the Open Graph size. Rather than scale a 512x384 view by some
   awkward fraction to reach it, take a 400x210 window out of the view and
   enlarge it by exactly 3. Integer scaling keeps every cell a crisp square
   block; 2.34x would smear the whole image into mush, and this is a game
   whose entire look is that the cells are visible. */
static const int CROP_W = 400, CROP_H = 210, ZOOM = 3;
static const int CROP_X = (VIEW_CELLS_W - CROP_W) / 2;   /* 56  */
static const int CROP_Y = (VIEW_CELLS_H - CROP_H) / 2;   /* 87  */
static const int OUT_W = CROP_W * ZOOM;                  /* 1200 */
static const int OUT_H = CROP_H * ZOOM;                  /* 630  */

/* Letters are stamped as CELLS, so they are made of the same molten rock
   everything else is and light the scene the same way. Five columns per glyph
   plus one of spacing -- see web/font8x8.h, which this shares with the browser
   build's text rendering rather than carrying a second copy of the alphabet. */
/* Temperature is set explicitly rather than left at the material's spawn
   value, and that is the difference between molten rock and a pale smear.
   VIEW_NORMAL tints a cell toward white as it heats, and lava spawns at
   degC(215) -- the top of the scale -- so letters stamped and left alone come
   out washed to salmon, with the material's own 0xF0641E orange nowhere in
   sight. Cooling them to a few hundred degrees below their spawn point keeps
   the glow while letting the colour through. Nothing here steps the
   simulation, so they never freeze on the way. */
static void stampText(World& w, const char* s, int camX, int camY,
                      int vx, int vy, int scale, u8 mat, int tempC) {
    for (int i = 0; s[i]; ++i) {
        int c = (unsigned char)s[i];
        if (c < FONT_FIRST || c > FONT_LAST) continue;
        const unsigned char* g = FONT8X8 + (size_t)(c - FONT_FIRST) * FONT_ROWS;
        const int ox = vx + i * (FONT_COLS + 1) * scale;
        for (int row = 0; row < FONT_ROWS; ++row) {
            if (!g[row]) continue;
            for (int col = 0; col < FONT_COLS; ++col) {
                if (!(g[row] & (0x40 >> col))) continue;
                for (int sy = 0; sy < scale; ++sy)
                    for (int sx = 0; sx < scale; ++sx) {
                        const int wx = camX + ox + col * scale + sx;
                        const int wy = camY + vy + row * scale + sy;
                        if (wx < 0 || wx >= SIM_W || wy < 0 || wy >= SIM_H) continue;
                        w.setCell(wx, wy, mat);
                        w.temp[(size_t)wy * SIM_W + wx] = (u8)degC(tempC);
                    }
            }
        }
    }
}

/* Clear the band the letters occupy so a variant never inherits the previous
   one's lettering. */
static void clearBand(World& w, int camX, int camY, int vx, int vy, int vw, int vh) {
    for (int y = vy; y < vy + vh; ++y)
        for (int x = vx; x < vx + vw; ++x)
            w.setCell(camX + x, camY + y, MAT_EMPTY);
}

static bool writePPM(const char* path, const u32* view) {
    FILE* f = fopen(path, "wb");
    if (!f) { printf("  cannot open %s\n", path); return false; }
    fprintf(f, "P6\n%d %d\n255\n", OUT_W, OUT_H);
    for (int y = 0; y < OUT_H; ++y) {
        const int sy = CROP_Y + y / ZOOM;
        for (int x = 0; x < OUT_W; ++x) {
            const int sx = CROP_X + x / ZOOM;
            const u32 c = view[sy * VIEW_CELLS_W + sx];
            const unsigned char rgb[3] = {
                (unsigned char)((c >> 16) & 0xFF),
                (unsigned char)((c >> 8) & 0xFF),
                (unsigned char)(c & 0xFF)
            };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    return true;
}

struct Variant {
    const char* name;
    u32         worldTime;   /* see dayLight(): t<0.42 day, 0.50..0.92 night */
    u8          mat;
    int         tempC;       /* letter temperature; see stampText */
    int         camDrop;     /* rows below spawn, to show more underground */
};

int main(void) {
    initMaterials();
    initItems();
    g_coverWorld.reset();
    printf("generating world...\n");
    generateWorld(g_coverWorld);

    float sx = 0.0f, sy = 0.0f;
    worldSpawnPoint(&sx, &sy);
    printf("spawn at %.0f, %.0f\n", sx, sy);

    const char* TEXT = "CINDERLIFT";
    const int SCALE = 6;
    const int textW = (int)strlen(TEXT) * (FONT_COLS + 1) * SCALE;   /* 360 */
    const int textH = FONT_ROWS * SCALE;                             /* 48  */
    const int textX = CROP_X + (CROP_W - textW) / 2;
    const int textY = CROP_Y + 30;

    static const Variant variants[] = {
        { "night-hot",    (u32)(DAY_LENGTH * 0.70f), MAT_LAVA,        190, 0 },
        { "night-warm",   (u32)(DAY_LENGTH * 0.70f), MAT_LAVA,        140, 0 },
        { "night-cool",   (u32)(DAY_LENGTH * 0.70f), MAT_LAVA,        110, 0 },
        { "dusk-warm",    (u32)(DAY_LENGTH * 0.47f), MAT_LAVA,        140, 0 },
        { "day-warm",     (u32)(DAY_LENGTH * 0.20f), MAT_LAVA,        140, 0 },
        { "night-iron",   (u32)(DAY_LENGTH * 0.70f), MAT_IRON_MELT,   140, 0 },
        { "night-copper", (u32)(DAY_LENGTH * 0.70f), MAT_COPPER_MELT, 140, 0 },
    };
    const int N = (int)(sizeof(variants) / sizeof(variants[0]));

    for (int i = 0; i < N; ++i) {
        const Variant& v = variants[i];

        int camX = (int)sx - VIEW_CELLS_W / 2;
        int camY = (int)sy - VIEW_CELLS_H / 2 + v.camDrop;
        if (camX < 0) camX = 0;
        if (camY < 0) camY = 0;
        if (camX > SIM_W - VIEW_CELLS_W) camX = SIM_W - VIEW_CELLS_W;
        if (camY > SIM_H - VIEW_CELLS_H) camY = SIM_H - VIEW_CELLS_H;

        clearBand(g_coverWorld, camX, camY, textX - 4, textY - 4, textW + 8, textH + 8);
        stampText(g_coverWorld, TEXT, camX, camY, textX, textY, SCALE, v.mat, v.tempC);

        g_worldTime = v.worldTime;
        lightClearDynamic();
        lightInvalidate();
        lightCompute(g_coverWorld, camX, camY);
        renderView(g_coverWorld, g_view, VIEW_NORMAL, camX, camY, true);

        char path[128];
        sprintf(path, "build/cover-%s.ppm", v.name);
        if (writePPM(path, g_view))
            printf("  %-11s daylight=%3d  cam=%d,%d  -> %s\n",
                   v.name, dayLight(), camX, camY, path);
    }
    printf("done\n");
    return 0;
}
