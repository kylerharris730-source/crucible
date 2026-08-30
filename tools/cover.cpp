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
         -o artifacts/cover.exe -lgdi32 -luser32 -lwinmm -lmsimg32 -lws2_32

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
#include <direct.h>
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
    /* Empty means no lettering: a plain screenshot of the world rather than
       the share image. The page wants both, and they differ only in this. */
    const char* text;
    u32         worldTime;   /* see dayLight(): t<0.42 day, 0.50..0.92 night */
    u8          mat;
    int         tempC;       /* letter temperature; see stampText */
    int         camDrop;     /* rows below spawn, to show more underground */
    /* Underground has no sun, so an unlit cavern renders as the black it
       genuinely is -- correct, and a useless screenshot. A lava pool is what
       actually lights a deep cavern in play, so the scene gets one rather than
       the shot getting a brightness cheat. */
    bool        molten;
    /* Frames of REAL SIMULATION before the shot is taken.
       The other scenes are posed: cells placed and rendered immediately. That
       is honest for a backdrop but it cannot show the thing this game is
       actually about, because every interesting sight here is a RESULT --
       steam is water that met something hot, obsidian is lava that met water,
       and neither exists until the simulation has run. So a scene that wants
       to show thermodynamics has to be allowed to happen rather than drawn.
       Needs a live window, or the chunks it touches are asleep and nothing
       moves however long it is stepped. */
    int         simSteps;
};

int main(void) {
    _mkdir("artifacts");
    _mkdir("artifacts\\visual");
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
        /* The share image. */
        { "night-warm",  "CINDERLIFT", (u32)(DAY_LENGTH * 0.70f), MAT_LAVA, 140,    0, false, 0 },
        /* Screenshots for the page: no lettering, just the world at depths
           that show what the game is actually about. */
        { "shot-surface", "", (u32)(DAY_LENGTH * 0.20f), MAT_LAVA, 140,    0, false, 0 },
        { "shot-dusk",    "", (u32)(DAY_LENGTH * 0.46f), MAT_LAVA, 140,    0, false, 0 },
        { "shot-under",   "", (u32)(DAY_LENGTH * 0.20f), MAT_LAVA, 140,  520, true, 0 },
        { "shot-deep",    "", (u32)(DAY_LENGTH * 0.20f), MAT_LAVA, 140, 1250, true, 0 },
        /* The one that shows the actual game rather than its scenery: water
           poured onto a lava pool and then LET GO for 240 frames. Whatever is
           in this image -- steam, a chilled crust, the glow through it -- is
           what the simulation did, not what was drawn. */
        { "shot-thermal", "", (u32)(DAY_LENGTH * 0.20f), MAT_LAVA, 140,  520, true, 240 },
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

        if (v.molten) {
            /* A pool along the floor of the view, hollowed above so the glow
               has somewhere to travel. Placed in the world like anything else
               and lit by the ordinary solver. */
            /* The hollow runs PAST the crop on every side. Carving it inside
               the frame put two straight edges and two corners in shot, which
               read as a box someone dug rather than as a cavern -- the one
               thing a screenshot of a cave must not look like. Only its
               contents should be visible, never its boundary. */
            const int poolY = CROP_Y + CROP_H - 30;
            for (int y = poolY - 90; y < poolY; ++y)
                for (int x = CROP_X - 40; x < CROP_X + CROP_W + 40; ++x)
                    g_coverWorld.setCell(camX + x, camY + y, MAT_EMPTY);
            for (int y = poolY; y < poolY + 8; ++y)
                for (int x = CROP_X - 40; x < CROP_X + CROP_W + 40; ++x) {
                    const int wx = camX + x, wy = camY + y;
                    g_coverWorld.setCell(wx, wy, MAT_LAVA);
                    g_coverWorld.temp[(size_t)wy * SIM_W + wx] = (u8)degC(150);
                }
        }
        /* ALWAYS clear, even for a variant that writes nothing. The world is
           shared across variants, so this band is how a shot avoids inheriting
           the previous one's lettering -- which is the whole reason clearBand
           exists, and putting it behind the text check meant every plain
           screenshot came out with CINDERLIFT still hanging in the sky. */
        clearBand(g_coverWorld, camX, camY, textX - 4, textY - 4, textW + 8, textH + 8);
        if (v.text && v.text[0])
            stampText(g_coverWorld, v.text, camX, camY, textX, textY, SCALE, v.mat, v.tempC);

        if (v.simSteps > 0) {
            /* Water above the pool, with a gap so it arrives as a fall rather
               than starting already touching -- the contact is the event worth
               photographing. */
            const int waterY = CROP_Y + CROP_H - 78;
            for (int y = waterY; y < waterY + 14; ++y)
                for (int x = CROP_X + 120; x < CROP_X + CROP_W - 120; ++x) {
                    const int wx = camX + x, wy = camY + y;
                    g_coverWorld.setCell(wx, wy, MAT_WATER);
                }
            /* Without a live window the chunks are asleep and stepping does
               nothing at all, however many frames it is given -- the world
               simulates what is near a player, and here there is no player. */
            g_coverWorld.setLiveWindow(camX - 64, camY - 64,
                                       camX + VIEW_CELLS_W + 64,
                                       camY + VIEW_CELLS_H + 64);
            for (int f = 0; f < v.simSteps; ++f) g_coverWorld.step();
        }

        g_worldTime = v.worldTime;
        lightClearDynamic();
        lightInvalidate();
        lightCompute(g_coverWorld, camX, camY);
        renderView(g_coverWorld, g_view, VIEW_NORMAL, camX, camY, true);

        char path[128];
        sprintf(path, "artifacts/visual/cover-%s.ppm", v.name);
        if (writePPM(path, g_view))
            printf("  %-11s daylight=%3d  cam=%d,%d  -> %s\n",
                   v.name, dayLight(), camX, camY, path);
    }
    printf("done\n");
    return 0;
}
