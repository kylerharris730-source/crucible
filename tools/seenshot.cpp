/* Two pictures of the same unlit chamber: one never discovered, one discovered.
   The whole change is the difference between them, and it is a change to how
   the world LOOKS, so it has to be looked at. */
#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "light.h"
#include "render.h"
#include "multiplayer.h"
#include <stdio.h>
#include <string.h>
#include <direct.h>

static World w;
static const int CX = 1400, CY = 6000;
static const int OW = 220, OH = 130, ZOOM = 3;

static void scene() {
    w.reset(); seenReset();
    for (int y = CY - 700; y <= CY + 700; ++y)
        for (int x = CX - 700; x <= CX + 700; ++x)
            if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1)
                w.setCell(x, y, MAT_STONE);
    for (int y = CY - 20; y <= CY + 20; ++y)
        for (int x = CX - 40; x <= CX + 40; ++x) w.setCell(x, y, MAT_EMPTY);
    /* Three blobs: iron, copper, gold. If they read as metal, this worked. */
    for (int y = CY - 5; y <= CY + 5; ++y) {
        for (int x = CX - 30; x <= CX - 20; ++x) w.setCell(x, y, MAT_IRON);
        for (int x = CX -  5; x <= CX +  5; ++x) w.setCell(x, y, MAT_COPPER);
        for (int x = CX + 20; x <= CX + 30; ++x) w.setCell(x, y, MAT_GOLD);
    }
    for (int x = CX - 40; x <= CX + 40; ++x) w.setCell(x, CY + 12, MAT_STONE);
    w.setLiveWindow(CX - 720, CY - 720, CX + 720, CY + 720);
}

static void settle() {
    for (int f = 0; f < 40; ++f)
        lightUpdate(w, CX - VIEW_CELLS_W / 2, CY - VIEW_CELLS_H / 2);
}

static void shot(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) { printf("cannot open %s\n", path); return; }
    fprintf(f, "P6\n%d %d\n255\n", OW * ZOOM, OH * ZOOM);
    const int vx0 = VIEW_CELLS_W / 2 - OW / 2, vy0 = VIEW_CELLS_H / 2 - OH / 2;
    for (int oy = 0; oy < OH * ZOOM; ++oy) {
        const int vy = vy0 + oy / ZOOM;
        for (int ox = 0; ox < OW * ZOOM; ++ox) {
            const int vx = vx0 + ox / ZOOM;
            const int wx = CX - VIEW_CELLS_W / 2 + vx;
            const int wy = CY - VIEW_CELLS_H / 2 + vy;
            const Cell& c = w.at(wx, wy);
            u32 col = MATS[c.mat].dryA;
            if (c.mat == MAT_EMPTY) col = 0x101014;
            col = shadeColor(col, viewShade(vx, vy));
            const unsigned char rgb[3] = {
                (unsigned char)((col >> 16) & 0xFF),
                (unsigned char)((col >> 8) & 0xFF),
                (unsigned char)(col & 0xFF) };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    printf("  %s\n", path);
}

int main(void) {
    _mkdir("artifacts"); _mkdir("artifacts\visual");
    initMaterials(); initItems(); initSprites(); playerSessionsReset();

    scene(); settle();
    shot("artifacts/visual/seen-before.ppm");

    /* Now light it, let it be discovered, and take the light away again. */
    scene();
    for (int y = CY - 8; y <= CY + 8; y += 8)
        for (int x = CX - 34; x <= CX + 34; x += 17) w.setCell(x, y, MAT_TORCH);
    settle();
    for (int y = CY - 8; y <= CY + 8; y += 8)
        for (int x = CX - 34; x <= CX + 34; x += 17) w.setCell(x, y, MAT_EMPTY);
    settle();
    shot("artifacts/visual/seen-after.ppm");
    return 0;
}
