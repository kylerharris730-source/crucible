#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "light.h"
#include "render.h"
#include "multiplayer.h"
#include <stdio.h>

/* Steam drifting through sunlight must not strobe what is lit beneath it.

   Gas is opacity 5 against air's 1 and it moves every frame. The field is
   solved from nothing each frame, so every one of those frames is correct and
   the sequence of them is unwatchable.

   The scene is a shaft cut down through rock into a chamber -- which is where
   this is actually seen, because it is what a mine looks like after water finds
   lava. Filling the shaft with steam takes the chamber from 199 to 81, so the
   thing being measured is not subtle.

   Two notes for anyone changing this. Steam does NOT attenuate the vertical
   skylight probe at all: an open sky column stays at full brightness however
   much gas is in it, and an earlier version of this test measured a plume
   under open sky and found a swing of exactly zero. The gas has to be in the
   PATH light propagates along, which is what the shaft is for. And a wide
   opaque lid over open ground only dimmed the sample from 255 to 232, because
   light arrives from the open sky to either side -- the shaft is narrow for
   that reason too.

   What is measured is the SEQUENCE, not any single frame: the peak-to-peak
   swing of one cell while the shaft flickers. That is what the player sees and
   the only thing that can show the smoothing worked, since each individual
   value was already right.

   The remaining checks are what stops the cure being worse than the disease --
   the filter must still converge exactly, and must not drag a replaced world
   toward the one before it.

   Compile with all source files except main.cpp. */

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

static const int GX = 1200;            /* the shaft's column */
static const int ROOF_Y0 = 500, ROOF_Y1 = 580;
static const int SAMPLE_Y = 606;       /* in the chamber under the shaft */
static const int SHAFT_HALF = 6;

static int camX, camY, vx, vy;

static void fill(World& w, int x0, int y0, int x1, int y1, u8 m) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) w.setCell(x, y, m);
}

static void buildScene(World& w) {
    w.reset();
    fill(w, GX - 400, ROOF_Y0, GX + 400, ROOF_Y1, MAT_STONE);   /* the rock above */
    fill(w, GX - 400, 611, GX + 400, 660, MAT_STONE);            /* the chamber floor */
    camX = GX - VIEW_CELLS_W / 2;
    camY = SAMPLE_Y - VIEW_CELLS_H / 2;
    vx = GX - camX;
    vy = SAMPLE_Y - camY;
    w.setLiveWindow(camX - 64, camY - 64,
                    camX + VIEW_CELLS_W + 64, camY + VIEW_CELLS_H + 64);
}

/* Real steam drifts rather than blinking, but blinking is the worst case of
   the same thing and it is reproducible, which drifting gas is not. */
static void shaft(World& w, bool steamed) {
    fill(w, GX - SHAFT_HALF, ROOF_Y0, GX + SHAFT_HALF, SAMPLE_Y,
         steamed ? MAT_STEAM : MAT_EMPTY);
}

/* Peak-to-peak at the sampled cell, after a warm-up so the smoothed run is
   measured in its steady state rather than while it climbs out of frame one. */
static int swing(World& w, bool smoothed, int frames, int warmup) {
    int lo = 255, hi = 0;
    for (int f = 0; f < frames; ++f) {
        shaft(w, (f & 1) != 0);
        lightClearDynamic();
        if (smoothed) lightUpdate(w, camX, camY);
        else          lightCompute(w, camX, camY);
        if (f < warmup) continue;
        const int v = lightAt(vx, vy);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    return hi - lo;
}

int main() {
    initMaterials();
    initItems();
    initSprites();

    /* Full daylight -- the whole effect is about the sun getting down there. */
    g_worldTime = (u32)(DAY_LENGTH / 5);
    check(dayLight() > 200, "the test scene is in daylight");

    buildScene(g_world);

    /* --- the complaint ---------------------------------------------------- */
    lightInvalidate();
    const int raw = swing(g_world, false, 80, 30);
    lightInvalidate();
    const int smooth = swing(g_world, true, 80, 30);

    printf("flicker at one cell: raw %d, smoothed %d\n", raw, smooth);
    check(raw > 40, "the unsmoothed field really does swing (else this proves nothing)");
    check(smooth * 3 <= raw, "smoothing cuts the swing to a third or less");

    /* --- it must still arrive --------------------------------------------- */
    /* A filter that never converges trades a flicker for a permanent error,
       and integer division truncating to a zero step is exactly how that
       happens. Settle a static scene, then demand the same answer the honest
       solve gives across a whole band of the field rather than one cell. */
    shaft(g_world, false);
    lightInvalidate();
    for (int f = 0; f < 500; ++f) { lightClearDynamic(); lightUpdate(g_world, camX, camY); }

    static u8 settled[VIEW_CELLS_H][64];
    for (int y = 0; y < VIEW_CELLS_H; ++y)
        for (int x = 0; x < 64; ++x) settled[y][x] = lightAt(x + 220, y);

    lightClearDynamic();
    lightCompute(g_world, camX, camY);
    int mismatched = 0;
    for (int y = 0; y < VIEW_CELLS_H; ++y)
        for (int x = 0; x < 64; ++x)
            if (settled[y][x] != lightAt(x + 220, y)) ++mismatched;
    check(mismatched == 0, "a settled field equals the honest solve exactly");
    if (mismatched) fprintf(stderr, "  %d of %d samples differ\n",
                            mismatched, VIEW_CELLS_H * 64);

    /* --- a replaced world must not be haunted ----------------------------- */
    /* lightInvalidate() is called on load and on a received world. If it did
       not drop the history, the new scene would be averaged toward the old one
       for a few frames -- the previous world showing through the new one. */
    shaft(g_world, true);
    lightInvalidate();
    lightClearDynamic();
    lightUpdate(g_world, camX, camY);
    const int first = lightAt(vx, vy);
    lightClearDynamic();
    lightCompute(g_world, camX, camY);
    check(first == lightAt(vx, vy),
          "the first frame after lightInvalidate carries no history");

    if (failures) {
        fprintf(stderr, "%d lighting check(s) failed\n", failures);
        return 1;
    }
    puts("steam no longer strobes the chamber, and the field still settles exactly");
    return 0;
}
