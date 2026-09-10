/* --- the hull is a picture of a rocket ---------------------------------------

   The art half of stage one. A 28 x 80 canvas is far too big to check by
   reading the character rows -- that is exactly the size at which a picture
   stops being legible as text -- so this does the two things a harness can
   honestly do: it asserts the properties the DRAW depends on, and it dumps the
   canvas for scripts/preview_rocket.py to render, which is how it gets looked
   at. See the house rule: look at the picture.

   What is asserted is the contract between the art and devDraw. The hull is
   baked from ART_ROCKET together with a mask saying which pixels belong to the
   core window and which to the fuel line, and devDraw dims the parts whose
   cargo is not aboard. If a future edit recolours the core window with some
   other character, the mask silently empties, the window stops lighting up
   when a core is installed, and nothing anywhere complains -- the rocket just
   quietly stops reporting its own state. That is what these counts are for.

   Run with no arguments it checks and says so; with a path it also writes the
   dump. Compile with every src cpp file except main.cpp. */

#include "sprite.h"
#include "materials.h"
#include "device.h"
#include "item.h"
#include <stdio.h>

int main(int argc, char** argv) {
    initMaterials();
    initItems();
    initSprites();

    int drawn = 0, core = 0, fuel = 0, widestTop = 0, widestBottom = 0;
    for (int y = 0; y < ROCKET_SPR_H; ++y) {
        int wide = 0;
        for (int x = 0; x < ROCKET_SPR_W; ++x) {
            const int k = y * ROCKET_SPR_W + x;
            if (!g_rocketHull[k]) continue;
            ++drawn; ++wide;
            if (g_rocketPart[k] == ROCKET_PART_CORE) ++core;
            if (g_rocketPart[k] == ROCKET_PART_FUEL) ++fuel;
        }
        if (y < 8  && wide > widestTop)    widestTop = wide;
        if (y > 71 && wide > widestBottom) widestBottom = wide;
    }
    printf("hull %d x %d: %d pixels drawn, %d of them the core window, %d the "
           "fuel line\n", ROCKET_SPR_W, ROCKET_SPR_H, drawn, core, fuel);
    printf("widest row in the top eight: %d; in the bottom eight: %d\n",
           widestTop, widestBottom);

    int bad = 0;
    /* Roughly half the canvas, which is what a tall object with two fins and a
       flared bell comes to. Far below this is a sliver; far above it is a
       rectangle, and either means the silhouette has been lost. */
    if (drawn < ROCKET_SPR_W * ROCKET_SPR_H / 4) {
        fprintf(stderr, "FAIL: only %d pixels -- that is not a rocket\n", drawn);
        ++bad;
    }
    if (!core) {
        fprintf(stderr, "FAIL: no pixel is tagged ROCKET_PART_CORE, so "
                        "installing the Ascent Core would light nothing\n");
        ++bad;
    }
    if (!fuel) {
        fprintf(stderr, "FAIL: no pixel is tagged ROCKET_PART_FUEL, so loading "
                        "fuel would light nothing\n");
        ++bad;
    }
    /* It stands on a base and tapers to a nose. This is the cheapest possible
       statement of "it is the right way up", and it is worth having: the
       canvas is written top row first, and an inverted table would otherwise
       only be noticed by eye. */
    if (widestBottom <= widestTop) {
        fprintf(stderr, "FAIL: the top is as wide as the base -- this is "
                        "upside down or it is a tube\n");
        ++bad;
    }
    /* And the five cells devIntact samples are all hull. If one of them is
       ever drawn over with sky, a placed rocket reports itself broken and is
       deleted on its first frame -- see the note in device.h. */
    for (int k = 0; k < rocketProbeCount(); ++k) {
        int dx = 0, dy = 0;
        rocketProbe(k, &dx, &dy);
        const bool solid = g_rocketHull[dy * ROCKET_SPR_W + dx] != 0;
        printf("  intactness probe %d at (%2d,%2d): %s\n", k, dx, dy,
               solid ? "hull" : "EMPTY");
        if (!solid) {
            fprintf(stderr, "FAIL: probe %d is transparent, so a placed rocket "
                            "would delete itself on its first frame\n", k);
            ++bad;
        }
    }
    if (bad) return 1;

    if (argc > 1) {
        FILE* out = fopen(argv[1], "w");
        if (!out) return 2;
        fprintf(out, "%d %d\n", ROCKET_SPR_W, ROCKET_SPR_H);
        for (int k = 0; k < ROCKET_SPR_W * ROCKET_SPR_H; ++k)
            fprintf(out, "%06x:%d%c", g_rocketHull[k], (int)g_rocketPart[k],
                    (k % ROCKET_SPR_W == ROCKET_SPR_W - 1) ? '\n' : ' ');
        fclose(out);
    }
    puts("PASS: the hull is drawn, and it knows which parts of itself light up.");
    return 0;
}
