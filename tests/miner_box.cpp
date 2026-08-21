/* --- does the miner clear a box, respect its filter, and follow the wire? ----

   The three things the redesign is for, and each has a way of silently not
   working that this is meant to catch.

     THE BOX     -- a miner used to take the single 14-wide row against its
                    face, which is why clearing the solids out of a furnace was
                    impractical: it could only ever scrape the surface. Depth is
                    the whole feature.
     THE FILTER  -- a filter that spends its per-tick budget on cells it then
                    refuses is a filter that does nothing in a mixed pile, which
                    is the only kind of pile worth filtering.
     THE TRIGGER -- "while on" and "per pulse" must actually differ. If both act
                    every frame the wire is high, the mode button is a lie.

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "device.h"
#include "multiplayer.h"
#include <stdio.h>

static World g_testWorld;

/* Put a miner facing down with a block of material beneath it, and return its
   index. The material block is `rows` deep and spans the whole footprint. */
static int setup(World& w, int mx, int my, int rows, u8 fill, u8 speckle) {
    w.reset();
    devClear();
    if (!devPlace(w, DEV_MINER, mx + DEV_W / 2, my + DEV_H / 2)) return -1;
    Device* d = devAt(mx + DEV_W / 2, my + DEV_H / 2);
    if (!d) return -1;
    for (int r = 0; r < rows; ++r)
        for (int x = d->x; x < d->x + DEV_W; ++x)
            w.setCell(x, d->y + DEV_H + r, (r & 1) && speckle ? speckle : fill);
    d->face = 0;                    /* down */
    d->value = 14;                  /* a full row per action */
    w.setLiveWindow(d->x - 20, d->y - 20, d->x + DEV_W + 20, d->y + DEV_H + rows + 20);
    return (int)(d - g_devices);
}

static int remaining(World& w, const Device& d, int rows) {
    int n = 0;
    for (int r = 0; r < rows; ++r)
        for (int x = d.x; x < d.x + DEV_W; ++x)
            if (w.at(x, d.y + DEV_H + r).mat != MAT_EMPTY) ++n;
    return n;
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();
    World& w = g_testWorld;
    const int mx = 900, my = 640;
    int failures = 0;

    /* --- depth: one row versus four ------------------------------------- */
    {
        const int rows = 4;
        int idx = setup(w, mx, my, rows, MAT_SAND, 0);
        if (idx < 0) { fprintf(stderr, "could not place the miner\n"); return 2; }
        Device& d = g_devices[idx];

        devSetBoxDepth(d, 1);
        for (int t = 0; t < 8; ++t) { d.poked = true; devTick(w); }
        const int afterShallow = remaining(w, d, rows);

        idx = setup(w, mx, my, rows, MAT_SAND, 0);
        Device& d2 = g_devices[idx];
        devSetBoxDepth(d2, 4);
        for (int t = 0; t < 8; ++t) { d2.poked = true; devTick(w); }
        const int afterDeep = remaining(w, d2, rows);

        printf("depth 1 left %d of %d cells; depth 4 left %d\n",
               afterShallow, rows * DEV_W, afterDeep);
        /* Sand falls, so a shallow miner does eventually eat the pile as it
           slumps -- the point is that the deep one is decisively faster. */
        if (afterDeep >= afterShallow) {
            fprintf(stderr, "FAIL: a depth-4 box cleared no more than a depth-1 one\n");
            ++failures;
        }
    }

    /* --- the filter ------------------------------------------------------ */
    {
        const int rows = 4;
        /* Alternating rows of sand and stone. Stone is static so it cannot
           slump into the gaps and confuse the count. */
        const int idx = setup(w, mx, my, rows, MAT_STONE, MAT_SAND);
        if (idx < 0) { fprintf(stderr, "could not place the miner\n"); return 2; }
        Device& d = g_devices[idx];
        devSetBoxDepth(d, rows);
        devSetFilterMat(d, MAT_STONE);

        for (int t = 0; t < 12; ++t) { d.poked = true; devTick(w); }

        int stone = 0, sand = 0;
        for (int r = 0; r < rows; ++r)
            for (int x = d.x; x < d.x + DEV_W; ++x) {
                const u8 m = w.at(x, d.y + DEV_H + r).mat;
                if (m == MAT_STONE) ++stone;
                else if (m == MAT_SAND) ++sand;
            }
        printf("filter=Stone: %d stone left, %d sand left, buffer holds %s\n",
               stone, sand, d.count ? MATS[d.mat].name : "nothing");
        if (stone != 0) {
            fprintf(stderr, "FAIL: the filter did not clear the material it names\n");
            ++failures;
        }
        if (sand == 0) {
            fprintf(stderr, "FAIL: the filter ate a material it does not name\n");
            ++failures;
        }
        if (d.count > 0 && d.mat != MAT_STONE) {
            fprintf(stderr, "FAIL: the buffer holds %s, not Stone\n", MATS[d.mat].name);
            ++failures;
        }
    }

    /* --- the two trigger modes must differ -------------------------------
       Driven by a REAL wired source rather than by poking the device's input
       array, because a device does not read its own output: circuitSetOutput
       writes what this machine is putting ON the network, and circuitInput
       reads what the network delivered last frame. Writing a signal into the
       miner itself and expecting it to see one measured as "a held signal
       never ran the miner at all", which is the network working correctly.

       So: a constant combinator holding 1, wired to the miner. That is also
       the arrangement somebody actually builds. */
    {
        const int rows = 6;
        int cleared[2] = { 0, 0 };
        for (int mode = 0; mode < 2; ++mode) {
            const int idx = setup(w, mx, my, rows, MAT_STONE, 0);
            if (idx < 0) { fprintf(stderr, "could not place the miner\n"); return 2; }
            Device& d = g_devices[idx];
            devSetBoxDepth(d, rows);
            devSetRunMode(d, mode);

            /* Well clear of the miner and its spoil, so the two footprints
               cannot overlap and refuse placement. */
            if (!devPlace(w, DEV_CONSTANT_COMBINATOR, mx + 60, my)) {
                fprintf(stderr, "could not place the source\n"); return 2;
            }
            Device* src = devAt(mx + 60, my);
            if (!src) { fprintf(stderr, "lost the source\n"); return 2; }
            const int srcIdx = (int)(src - g_devices);
            src->value = 1;
            g_circuitConfig[srcIdx].signal = g_circuitConfig[idx].signal;
            if (!circuitToggleWire(srcIdx, idx)) {
                fprintf(stderr, "could not wire the source to the miner\n"); return 2;
            }
            /* Widened to take in the SOURCE as well. devTick skips any machine
               whose centre is outside the electrically live window, so a source
               left out of it never ticks, never puts anything on the wire, and
               the miner correctly sees nothing -- which reads exactly like the
               trigger being broken. */
            w.setLiveWindow(d.x - 20, d.y - 20,
                            src->x + DEV_W + 20, d.y + DEV_H + rows + 20);

            const int before = remaining(w, d, rows);
            /* Sixteen frames of a wire that never drops. WHILE_ON should act on
               every one of them; ON_EDGE should act once, on the first. */
            for (int t = 0; t < 16; ++t) devTick(w);
            cleared[mode] = before - remaining(w, d, rows);
        }
        printf("held-high wire: while-on cleared %d, per-pulse cleared %d\n",
               cleared[0], cleared[1]);
        if (cleared[0] <= cleared[1]) {
            fprintf(stderr, "FAIL: the two trigger modes behave the same on a "
                            "signal that never drops\n");
            ++failures;
        }
        if (cleared[0] == 0) {
            fprintf(stderr, "FAIL: a held signal never ran the miner at all\n");
            ++failures;
        }
    }

    if (failures) { fprintf(stderr, "\n%d miner check(s) failed\n", failures); return 1; }
    printf("\nPASS\n");
    return 0;
}
