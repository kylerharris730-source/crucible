/* A cropped render must match the visible part of a full render, including
   lighting, and must not touch the unused part of a strided frame buffer. */
#include "render.h"
#include "light.h"
#include <stdio.h>
#include <vector>

static const u32 GUARD = 0xDEADBEEF;
static u32 g_full[VIEW_CELLS_W * VIEW_CELLS_H];

static void paintScene(int camX, int camY) {
    const u8 mats[] = { MAT_EMPTY, MAT_WALL, MAT_STONE, MAT_WATER,
                       MAT_TORCH, MAT_SIEVE, MAT_GAS_SIEVE, MAT_LAVA };
    for (int y = imax(0, camY); y < imin(SIM_H, camY + VIEW_CELLS_H); ++y) {
        for (int x = imax(0, camX); x < imin(SIM_W, camX + VIEW_CELLS_W); ++x) {
            const int i = y * SIM_W + x;
            Cell& c = g_world.cells[i];
            const unsigned pattern = (unsigned)(x * 37 + y * 19);
            c.mat = mats[(pattern >> 3) % (sizeof(mats) / sizeof(mats[0]))];
            c.tint = (u8)pattern;
            c.moisture = c.mat == MAT_SIEVE || c.mat == MAT_GAS_SIEVE
                       ? (u8)MAT_WATER : (u8)(pattern >> 4);
            g_world.temp[i] = (u8)(pattern >> 5);
            g_world.bg[i] = pattern % 3 == 0 ? MAT_STONE
                          : pattern % 3 == 1 ? (MAT_STONE | BG_PLACED) : 0;
        }
    }
}

static int visibleCount(int camX, int camY, int width, int height) {
    int result = 0;
    for (int y = imax(0, camY); y < imin(SIM_H, camY + height); ++y)
        for (int x = imax(0, camX); x < imin(SIM_W, camX + width); ++x) {
            const int mat = g_world.cells[y * SIM_W + x].mat;
            result += mat != MAT_EMPTY && mat != MAT_WALL;
        }
    return result;
}

static bool checkCrop(int camX, int camY, int mode, bool lit,
                      int width, int height, int stride) {
    const int actualStride = stride ? stride : width;
    const int capacity = actualStride * VIEW_CELLS_H;
    std::vector<u32> cropped(capacity + 2, GUARD);
    const int count = renderView(g_world, &cropped[1], mode, camX, camY, lit,
                                 width, height, stride);
    if (count != visibleCount(camX, camY, width, height)) {
        fprintf(stderr, "wrong visible cell count for %dx%d\n", width, height);
        return false;
    }
    for (int i = 0; i < capacity + 2; ++i) {
        const int x = (i - 1) % actualStride, y = (i - 1) / actualStride;
        const bool inside = i > 0 && i <= capacity && x < width && y < height;
        const u32 expected = inside ? g_full[y * VIEW_CELLS_W + x] : GUARD;
        if (cropped[i] != expected) {
            fprintf(stderr, "crop/guard mismatch cam=(%d,%d) mode=%d lit=%d "
                            "size=%dx%d stride=%d offset=%d\n",
                    camX, camY, mode, (int)lit, width, height, stride, i);
            return false;
        }
    }
    return true;
}

int main() {
    initMaterials();
    g_world.reset(false);
    seenReset();
    for (int cy = 0; cy < CHUNKS_Y; ++cy)
        for (int cx = 0; cx < CHUNKS_X; ++cx)
            g_world.zone[cy * CHUNKS_X + cx] = (u8)((cy / 2) % ZONE_COUNT);
    const int cameras[][2] = {
        { 701, 3071 }, { -31, -17 }, { SIM_W - 128, SIM_H - 192 },
        { -VIEW_CELLS_W - 2, 96 }, { SIM_W + 2, SIM_H + 2 }
    };
    const int sizes[][2] = {
        { VIEW_CELLS_W, VIEW_CELLS_H }, { 256, 192 }, { 128, 96 },
        { 257, 193 }, { 1, 1 }
    };
    for (unsigned c = 0; c < sizeof(cameras) / sizeof(cameras[0]); ++c)
        paintScene(cameras[c][0], cameras[c][1]);

    for (int threads = 1; threads <= 8; threads += 7) {
        simSetWorkers(threads);
        for (unsigned c = 0; c < sizeof(cameras) / sizeof(cameras[0]); ++c) {
            const int camX = cameras[c][0], camY = cameras[c][1];
            g_worldTime = c * DAY_LENGTH / 5;
            lightCompute(g_world, camX, camY);
            for (int mode = 0; mode < VIEW_COUNT; ++mode) {
                for (int lit = 0; lit <= 1; ++lit) {
                    renderView(g_world, g_full, mode, camX, camY, lit != 0);
                    for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
                        const int width = sizes[s][0], height = sizes[s][1];
                        if (!checkCrop(camX, camY, mode, lit != 0, width, height, 0)
                            || !checkCrop(camX, camY, mode, lit != 0, width, height, VIEW_CELLS_W))
                            return 1;
                    }
                }
            }
        }
    }
    simSetWorkers(1);

    /* Oversized powderlike views still reject the fixed light geometry. */
    const int largeW = VIEW_CELLS_W + 1, largeH = VIEW_CELLS_H + 1;
    std::vector<u32> unlit(largeW * largeH), lit(largeW * largeH);
    renderView(g_world, &unlit[0], VIEW_NORMAL, 701, 3071, false, largeW, largeH);
    renderView(g_world, &lit[0], VIEW_NORMAL, 701, 3071, true, largeW, largeH);
    if (unlit != lit) { fprintf(stderr, "oversized view was shaded\n"); return 1; }

    u32 guard = GUARD;
    if (renderView(g_world, &guard, VIEW_NORMAL, 0, 0, false, 2, 2, 1) != 0
        || renderView(g_world, &guard, VIEW_NORMAL, 0, 0, false, 0, 2) != 0
        || renderView(g_world, &guard, VIEW_NORMAL, 0, 0, false, 2, -1) != 0
        || guard != GUARD) {
        fprintf(stderr, "invalid geometry wrote pixels\n"); return 1;
    }
    printf("PASS: cropped renders match full frames across views, lighting, strides and threads\n");
    return 0;
}
