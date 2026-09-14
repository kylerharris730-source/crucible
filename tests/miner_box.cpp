/* --- the miner and placer: a square, a facing, and when they act ------------

   Asked for in play: "lets redesign miner and placer, theyve always confused
   me, they should have a facing direction and a cube that they eat/place size.
   only 2 main parameters, then we need to say when it triggers, we should have
   an always on and a per pulse option from a circuit network, they should
   project a pale aura over the area they will effect".

   What this file holds them to, and the way each can silently not work:

     THE SQUARE    one pulse at size N clears exactly the N x N block against
                   the face, centred on it -- not a cell outside it, and not a
                   cell short.
     THE FACING    all four directions, because a square that is only ever
                   tested facing down is a square that is only known to work
                   facing down.
     THE TRIGGER   per pulse acts ONCE on a wire held high; always on acts
                   every frame with no wire at all. If both behaved the same the
                   mode button would be a lie.
     OLD SAVES     neither legacy stored mode wakes a machine by itself, and nor
                   does a fresh placement. Getting this wrong eats a base the
                   moment an old world is opened.
     THE PLACER    fills its square, and does not suck what it placed straight
                   back in through the face -- which it used to.
     THE AURA      the pale square drawn over the world is exactly the square
                   the tick works on. Rendered for real through devDraw.

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "device.h"
#include "render.h"
#include "multiplayer.h"
#include <stdio.h>
#include <string.h>

static World g_testWorld;
static int failures = 0;

static void check(bool ok, const char* what) {
    printf("  %-66s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static const int MX = 900, MY = 640;

/* A machine of `type` with its top-left at (MX, MY), in a world that is empty
   apart from what the caller fills in. */
static Device* fresh(World& w, u8 type) {
    w.reset();
    devClear();
    if (!devPlace(w, type, MX + DEV_W / 2, MY + DEV_H / 2)) return NULL;
    Device* d = devAt(MX + DEV_W / 2, MY + DEV_H / 2);
    if (!d) return NULL;
    w.setLiveWindow(d->x - 60, d->y - 60, d->x + DEV_W + 60, d->y + DEV_H + 60);
    return d;
}

/* Is (x, y) one of the cells of d's working square? */
static bool inSquare(const Device& d, int x, int y) {
    const int n = devWorkSize(d);
    for (int layer = 0; layer < n; ++layer)
        for (int a = 0; a < n; ++a) {
            int cx, cy;
            devWorkCell(d, a, layer, &cx, &cy);
            if (cx == x && cy == y) return true;
        }
    return false;
}

/* Fill a generous region around the machine with stone, leaving the machine's
   own footprint alone. Stone is static, so nothing slumps into a hole and
   confuses a count. */
static void surroundWithStone(World& w, const Device& d, int margin) {
    for (int y = d.y - margin; y < d.y + DEV_H + margin; ++y)
        for (int x = d.x - margin; x < d.x + DEV_W + margin; ++x) {
            if (x >= d.x && x < d.x + DEV_W && y >= d.y && y < d.y + DEV_H) continue;
            w.setCell(x, y, MAT_STONE);
        }
}

/* Put a constant 1 on a wire to device `idx`. Returns false if it could not. */
static bool wireConstantOne(World& w, int idx) {
    const Device& d = g_devices[idx];
    const int sx = d.x + DEV_W + 80, sy = d.y + DEV_H / 2;
    if (!devPlace(w, DEV_CONSTANT_COMBINATOR, sx, sy)) return false;
    Device* src = devAt(sx, sy);
    if (!src) return false;
    const int srcIdx = (int)(src - g_devices);
    src->value = 1;
    g_circuitConfig[srcIdx].signal = g_circuitConfig[idx].signal;
    if (!circuitToggleWire(srcIdx, idx)) return false;
    /* Widened to take in the source: a machine whose centre is outside the live
       window never ticks, so a source left out never drives the wire and the
       miner correctly sees nothing -- which reads exactly like a broken trigger. */
    w.setLiveWindow(d.x - 60, d.y - 60, src->x + DEV_W + 20, d.y + DEV_H + 60);
    return true;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();
    World& w = g_testWorld;

    /* --- the square, in every direction ----------------------------------- */
    {
        static const char* NAME[4] = { "down", "up", "left", "right" };
        const int sizes[] = { 1, 6, 14, 20 };
        int wrongInside = 0, wrongOutside = 0, runs = 0;
        for (int face = 0; face < 4; ++face) {
            for (unsigned k = 0; k < sizeof(sizes) / sizeof(sizes[0]); ++k) {
                Device* d = fresh(w, DEV_MINER);
                if (!d) { fprintf(stderr, "could not place the miner\n"); return 2; }
                d->face = (u8)face;
                d->value = sizes[k];
                surroundWithStone(w, *d, 40);
                d->poked = true;
                devTick(w);
                ++runs;

                int leftInside = 0, eatenOutside = 0;
                for (int y = d->y - 40; y < d->y + DEV_H + 40; ++y)
                    for (int x = d->x - 40; x < d->x + DEV_W + 40; ++x) {
                        if (x >= d->x && x < d->x + DEV_W && y >= d->y && y < d->y + DEV_H) continue;
                        const bool empty = w.at(x, y).mat == MAT_EMPTY;
                        if (inSquare(*d, x, y)) { if (!empty) ++leftInside; }
                        else if (empty) ++eatenOutside;
                    }
                if (leftInside || eatenOutside) {
                    printf("  facing %s size %d: %d left inside, %d eaten outside\n",
                           NAME[face], sizes[k], leftInside, eatenOutside);
                }
                wrongInside += leftInside;
                wrongOutside += eatenOutside;
            }
        }
        printf("%d runs across four facings and sizes 1, 6, 14, 20\n", runs);
        check(wrongInside == 0, "one pulse clears every cell of its square");
        check(wrongOutside == 0, "and not a single cell outside it");
    }

    /* --- the square is centred on the face ------------------------------- */
    {
        Device* d = fresh(w, DEV_MINER);
        if (!d) return 2;
        d->face = 0;
        d->value = 6;
        int x0, y0, x1, y1;
        devWorkCell(*d, 0, 0, &x0, &y0);
        devWorkCell(*d, 5, 5, &x1, &y1);
        const int gapLeft = x0 - d->x, gapRight = (d->x + DEV_W - 1) - x1;
        printf("size 6 facing down spans x %d..%d under a machine at %d..%d\n",
               x0, x1, d->x, d->x + DEV_W - 1);
        check(gapLeft == gapRight, "a square narrower than the machine sits centred under it");
        check(y0 == d->y + DEV_H && y1 == d->y + DEV_H + 5,
              "and starts against the face, running away from it");
    }

    /* --- the size is clamped to what the stepper offers ------------------- */
    {
        Device* d = fresh(w, DEV_MINER);
        if (!d) return 2;
        d->value = 0;    check(devWorkSize(*d) == 1, "a size below 1 reads as 1");
        d->value = 999;  check(devWorkSize(*d) == DEV_WORK_MAX, "a size above the cap reads as the cap");
        check(DEVS[DEV_MINER].vMax == DEV_WORK_MAX && DEVS[DEV_PLACER].vMax == DEV_WORK_MAX,
              "and the panel's range is that same cap");
    }

    /* --- the trigger ------------------------------------------------------
       Counted in ACTIONS, not cells. The square is refilled and the buffer
       emptied before every frame, so each frame the machine acts shows up as a
       cleared square and a frame it does not act leaves the stone standing. */
    {
        const int FRAMES = 20;
        int acted[DEVRUN_COUNT][2] = { { 0, 0 }, { 0, 0 } };   /* [mode][wired] */
        for (int mode = 0; mode < DEVRUN_COUNT; ++mode) {
            for (int wired = 0; wired < 2; ++wired) {
                Device* d = fresh(w, DEV_MINER);
                if (!d) return 2;
                const int idx = (int)(d - g_devices);
                d->face = 0;
                d->value = 4;
                devSetRunMode(*d, mode);
                if (wired && !wireConstantOne(w, idx)) {
                    fprintf(stderr, "could not wire the source\n"); return 2;
                }
                for (int t = 0; t < FRAMES; ++t) {
                    Device& m = g_devices[idx];
                    for (int layer = 0; layer < 4; ++layer)
                        for (int a = 0; a < 4; ++a) {
                            int x, y; devWorkCell(m, a, layer, &x, &y);
                            w.setCell(x, y, MAT_STONE);
                        }
                    m.count = 0;
                    devTick(w);
                    int x, y; devWorkCell(m, 0, 0, &x, &y);
                    if (w.at(x, y).mat == MAT_EMPTY) ++acted[mode][wired];
                }
            }
        }
        printf("over %d frames:  per pulse acted %d unwired, %d on a held 1;"
               "  always on acted %d unwired, %d wired\n", FRAMES,
               acted[DEVRUN_PER_PULSE][0], acted[DEVRUN_PER_PULSE][1],
               acted[DEVRUN_ALWAYS_ON][0], acted[DEVRUN_ALWAYS_ON][1]);
        check(acted[DEVRUN_PER_PULSE][0] == 0, "per pulse does nothing with no signal");
        check(acted[DEVRUN_PER_PULSE][1] == 1, "and acts exactly once on a signal held high");
        check(acted[DEVRUN_ALWAYS_ON][0] == FRAMES, "always on acts every frame with no signal at all");
        check(acted[DEVRUN_ALWAYS_ON][1] == FRAMES, "and a signal on its wire changes nothing");
    }

    /* --- a spark is a pulse ------------------------------------------------ */
    {
        Device* d = fresh(w, DEV_MINER);
        if (!d) return 2;
        d->face = 0; d->value = 3;
        devSetRunMode(*d, DEVRUN_PER_PULSE);
        surroundWithStone(w, *d, 10);
        d->poked = true;
        devTick(w);
        int x, y; devWorkCell(*d, 1, 1, &x, &y);
        check(w.at(x, y).mat == MAT_EMPTY, "a spark arriving fires a per-pulse miner");
    }

    /* --- old saves and fresh placements never wake up by themselves -------- */
    {
        const i32 stored[] = { 0, 1 };   /* the two modes before the redesign */
        bool anyWoke = false;
        for (int k = 0; k < 2; ++k) {
            Device* d = fresh(w, DEV_MINER);
            if (!d) return 2;
            d->count2 = stored[k];
            d->face = 0; d->value = 4;
            surroundWithStone(w, *d, 10);
            for (int t = 0; t < 10; ++t) devTick(w);
            int x, y; devWorkCell(*d, 0, 0, &x, &y);
            if (w.at(x, y).mat == MAT_EMPTY) anyWoke = true;
            if (devRunMode(*d) != DEVRUN_PER_PULSE) anyWoke = true;
        }
        check(!anyWoke, "both legacy trigger values load as per pulse and stay idle");

        Device* d = fresh(w, DEV_MINER);
        if (!d) return 2;
        check(devRunMode(*d) == DEVRUN_PER_PULSE, "a freshly placed miner starts per pulse");
        d->face = 0;
        surroundWithStone(w, *d, 10);
        for (int t = 0; t < 10; ++t) devTick(w);
        int x, y; devWorkCell(*d, 0, 0, &x, &y);
        check(w.at(x, y).mat != MAT_EMPTY, "and does not dig until it is told to");
    }

    /* --- the filter still takes one material from a mixed pile ------------- */
    {
        Device* d = fresh(w, DEV_MINER);
        if (!d) return 2;
        d->face = 0; d->value = 6;
        devSetFilterMat(*d, MAT_STONE);
        for (int layer = 0; layer < 6; ++layer)
            for (int a = 0; a < 6; ++a) {
                int x, y; devWorkCell(*d, a, layer, &x, &y);
                w.setCell(x, y, ((a + layer) & 1) ? MAT_STONE : MAT_IRON);
            }
        d->poked = true;
        devTick(w);
        int stone = 0, iron = 0;
        for (int layer = 0; layer < 6; ++layer)
            for (int a = 0; a < 6; ++a) {
                int x, y; devWorkCell(*d, a, layer, &x, &y);
                if (w.at(x, y).mat == MAT_STONE) ++stone;
                if (w.at(x, y).mat == MAT_IRON) ++iron;
            }
        check(stone == 0 && iron == 18, "a filtered miner clears its material and leaves the rest");
    }

    /* --- the placer fills its square, and keeps what it placed ------------- */
    {
        Device* d = fresh(w, DEV_PLACER);
        if (!d) { fprintf(stderr, "could not place the placer\n"); return 2; }
        d->face = 0; d->value = 5;
        d->mat = MAT_SAND; d->count = 200;
        /* A stone cup around the square, so placed sand has nowhere to fall and
           a count of it is a count of what the placer did. */
        for (int layer = -1; layer <= 5; ++layer)
            for (int a = -1; a <= 5; ++a) {
                if (layer >= 0 && layer < 5 && a >= 0 && a < 5) continue;
                if (layer < 0) continue;
                int x, y; devWorkCell(*d, a, layer, &x, &y);
                w.setCell(x, y, MAT_STONE);
            }
        d->poked = true;
        devTick(w);
        int filled = 0;
        for (int layer = 0; layer < 5; ++layer)
            for (int a = 0; a < 5; ++a) {
                int x, y; devWorkCell(*d, a, layer, &x, &y);
                if (w.at(x, y).mat == MAT_SAND) ++filled;
            }
        const int heldAfterPlacing = d->count;
        check(filled == 25 && heldAfterPlacing == 175, "one pulse fills its whole square from the buffer");

        for (int t = 0; t < 60; ++t) devTick(w);
        printf("placer holds %d after placing, %d after 60 more frames\n",
               heldAfterPlacing, (int)d->count);
        check(d->count == heldAfterPlacing,
              "and does not suck what it placed back in through its face");
    }

    /* --- the aura is the square ---------------------------------------------
       Rendered for real. A flat grey frame, devDraw over it, and every pixel
       that changed outside the machine's own housing must be a cell of the
       square -- and every cell of the square must have changed. */
    {
        for (int type = 0; type < 2; ++type) {
            const u8 t = type ? DEV_PLACER : DEV_MINER;
            Device* d = fresh(w, t);
            if (!d) return 2;
            d->face = 3;       /* right, so the square is off to one side */
            d->value = 9;
            const int camX = d->x - 100, camY = d->y - 100;
            static u32 px[VIEW_CELLS_W * VIEW_CELLS_H];
            const u32 GREY = 0x404040;
            for (int i = 0; i < VIEW_CELLS_W * VIEW_CELLS_H; ++i) px[i] = GREY;
            devDraw(w, px, camX, camY, false);

            int stray = 0, missed = 0;
            for (int vy = 0; vy < VIEW_CELLS_H; ++vy)
                for (int vx = 0; vx < VIEW_CELLS_W; ++vx) {
                    const int x = vx + camX, y = vy + camY;
                    if (x >= d->x && x < d->x + DEV_W && y >= d->y && y < d->y + DEV_H) continue;
                    const bool changed = px[vy * VIEW_CELLS_W + vx] != GREY;
                    const bool want = inSquare(*d, x, y);
                    if (changed && !want) ++stray;
                    if (want && !changed) ++missed;
                }
            printf("%s aura: %d stray pixels, %d square cells not tinted\n",
                   type ? "placer" : "miner", stray, missed);
            check(stray == 0 && missed == 0,
                  type ? "the placer's pale square is exactly where it will place"
                       : "the miner's pale square is exactly what it will break");
        }
    }

    if (failures) { fprintf(stderr, "\n%d miner/placer check(s) failed\n", failures); return 1; }
    printf("\nPASS\n");
    return 0;
}
