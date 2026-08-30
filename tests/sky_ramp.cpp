#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "multiplayer.h"
#include <stdio.h>

/* The sky is blue nearly everywhere, and space is a thin cap at the very top.

   Worth pinning because the two ends of this ramp are easy to break in
   opposite directions and neither break is visible in the source. Pull the
   everyday stop toward grey and the sky goes back to the washed-out haze this
   replaced; widen the dark end and night creeps down over a daylit world. The
   table in initZoneColours() is positioned rather than evenly spaced purely to
   stop the second one, and an even-spacing "simplification" would reintroduce
   it while still compiling and still looking like a gradient.

   Compile with all source files except main.cpp. */

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

static int R(u32 c) { return (int)((c >> 16) & 0xFF); }
static int G(u32 c) { return (int)((c >> 8) & 0xFF); }
static int B(u32 c) { return (int)(c & 0xFF); }

int main() {
    initMaterials();
    initItems();
    initSprites();

    /* The colour the player sees standing on the surface. render.cpp clamps
       every row past the table to this entry, and the ground is far below the
       band, so this one entry is the whole daytime sky in normal play. */
    const u32 day = g_skyLut[SKY_BAND - 1];
    check(B(day) > 120, "the everyday sky is a strong blue");
    check(B(day) - R(day) > 80, "the everyday sky is blue, not grey");
    check(B(day) > G(day) + 40, "blue leads green in the everyday sky");

    /* Space. Dark enough to read as black against the world, not merely dim. */
    const u32 top = g_skyLut[0];
    check(R(top) + G(top) + B(top) < 60, "the top of the world is near black");

    /* The cap is thin. If this band ever reaches a quarter of the way down,
       the ramp has been re-spaced evenly and the fix has been undone. */
    int firstLit = SKY_BAND - 1;
    for (int y = 0; y < SKY_BAND; ++y) {
        const u32 c = g_skyLut[y];
        if (R(c) + G(c) + B(c) >= 120) { firstLit = y; break; }
    }
    check(firstLit < SKY_BAND / 4, "space is a thin cap, not the top quarter");
    check(firstLit > 20, "space is actually present at the very top");

    /* Monotone: brightness only ever increases going down. A stop typed out of
       order would give a band that gets darker as it descends, which reads as
       a seam rather than as sky. */
    int prev = -1, dips = 0;
    for (int y = 0; y < SKY_BAND; ++y) {
        const u32 c = g_skyLut[y];
        const int lum = R(c) + G(c) + B(c);
        if (lum < prev) ++dips;
        prev = lum;
    }
    check(dips == 0, "the ramp never darkens on the way down");

    if (failures) {
        fprintf(stderr, "%d sky check(s) failed\n", failures);
        return 1;
    }
    printf("sky: space %06X at the top, blue %06X everywhere below\n", top, day);
    return 0;
}
