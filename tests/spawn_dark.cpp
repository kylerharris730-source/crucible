#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "player.h"
#include "light.h"
#include "render.h"
#include "multiplayer.h"
#include <stdio.h>
#include <string.h>

/* Creatures must not appear in a lit room -- including one that is off screen.

   The spawner draws candidate sites from the padded light rectangle and then
   throws away the ones that land on screen, so EVERY site it goes on to test
   is outside the view by construction. It was asking about those sites with
   lightRow(ly)[lx], and a view row is exactly VIEW_CELLS_W bytes long: an
   off-screen lx read past the end of it. The darkness test was reading
   whatever happened to sit next to that array, which is why creatures walked
   out of torchlight.

   So the lamp here is deliberately in the MARGIN rather than on screen. A test
   that lit the middle of the view would have passed against the broken code.

   Compile with all source files except main.cpp. */

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

static const int CX = 1800, CY = 3600;      /* underground, layer 1 */
static const int LAMP_X = CX + 340;         /* off screen: half a view is 256 */
static const int LAMP_Y = CY;
static const int CAVERN = 700;

static int camX, camY;

/* A wide open cavern with a floor, marked as underground so the surface/night
   rule does not decide the answer before the light does.

   The ROCK matters as much as the cavern. A reset world is empty, and this sits
   only ~500 cells under SURFACE_Y, so without a lid the sun reaches all the way
   down and the light field reports full daylight underground -- which rejected
   every candidate and made an earlier version of this test pass by spawning
   nothing at all. */
static void buildCavern(World& w) {
    w.reset();
    for (int y = CY - 480; y <= CY + 200; ++y)
        for (int x = CX - CAVERN - 60; x <= CX + CAVERN + 60; ++x)
            w.setCell(x, y, MAT_STONE);
    for (int y = CY - 120; y <= CY + 39; ++y)
        for (int x = CX - CAVERN; x <= CX + CAVERN; ++x)
            w.setCell(x, y, MAT_EMPTY);
    for (int y = CY + 40; y <= CY + 60; ++y)
        for (int x = CX - CAVERN; x <= CX + CAVERN; ++x)
            w.setCell(x, y, MAT_STONE);

    for (int cy = (CY - 260) >> CHUNK_SHIFT; cy <= (CY + 260) >> CHUNK_SHIFT; ++cy)
        for (int cx = (CX - CAVERN - 40) >> CHUNK_SHIFT;
             cx <= (CX + CAVERN + 40) >> CHUNK_SHIFT; ++cx)
            w.zone[cy * CHUNKS_X + cx] = ZONE_LAYER1;

    camX = CX - VIEW_CELLS_W / 2;
    camY = CY - VIEW_CELLS_H / 2;
    w.setLiveWindow(camX - LIGHT_MARGIN_CELLS - 40, camY - LIGHT_MARGIN_CELLS - 40,
                    camX + VIEW_CELLS_W + LIGHT_MARGIN_CELLS + 40,
                    camY + VIEW_CELLS_H + LIGHT_MARGIN_CELLS + 40);
}

static void lamp(World& w, bool on) {
    for (int y = LAMP_Y - 1; y <= LAMP_Y + 1; ++y)
        for (int x = LAMP_X - 1; x <= LAMP_X + 1; ++x)
            w.setCell(x, y, on ? MAT_LAMP : MAT_EMPTY);
}

/* Where does the spawner actually put things?

   One run cannot answer that: ENT_MAX_ALIVE caps a world at seven creatures, so
   a single run is seven samples and seven points landing outside one 70-cell
   disc means very little. This clears the world after each placement and asks
   again, which turns the question into a distribution.

   The field is solved ONCE. It is a function of the world and the camera, and
   neither moves while sampling -- recomputing it per tick made this take
   minutes to say the same thing. */
static void sampleSpawns(World& w, Player& p, int samples, int* total, int* near) {
    lightClearDynamic();
    lightCompute(w, camX, camY);
    *total = 0;
    *near = 0;
    for (int s = 0; s < samples; ++s) {
        entReset();
        for (int i = 0; i < 600; ++i) {
            entSpawnTick(w, p, camX, camY, true);
            int found = -1;
            for (int k = 0; k < MAX_ENTITIES; ++k)
                if (g_entities[k].type != ENT_NONE) { found = k; break; }
            if (found < 0) continue;
            ++*total;
            const float dx = g_entities[found].centreX() - (float)LAMP_X;
            const float dy = g_entities[found].centreY() - (float)LAMP_Y;
            if (dx * dx + dy * dy < 70.0f * 70.0f) ++*near;
            break;
        }
    }
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    g_lightOn = true;

    World& w = g_world;
    buildCavern(w);

    Player& p = g_player;
    p.reset((float)CX, (float)(CY + 30));
    p.alive = true;
    g_inv.clear();

    /* --- the lamp is off screen, and the field still knows about it ------- */
    lamp(w, true);
    lightClearDynamic();
    lightCompute(w, camX, camY);
    check(LAMP_X - camX >= VIEW_CELLS_W,
          "the lamp really is outside the view (else this proves nothing)");
    check(lightAtWorld(LAMP_X, LAMP_Y) > 40,
          "lightAtWorld reports the lamp in the margin as lit");
    check(lightAtWorld(CX - 600, LAMP_Y) <= 40,
          "and reports somewhere far from it as dark");

    /* --- nothing spawns in the light -------------------------------------- */
    int litTotal = 0, litNear = 0;
    sampleSpawns(w, p, 400, &litTotal, &litNear);
    printf("lamp on:  %d placements, %d of them near the lamp\n", litTotal, litNear);
    check(litTotal > 100, "the lit run placed enough creatures to mean anything");
    check(litNear == 0, "no creature spawns inside the lit area");

    /* --- and the run is not vacuous --------------------------------------- */
    /* If nothing spawned at all the check above would pass for the wrong
       reason, so the same cavern with the lamp removed has to produce some. */
    lamp(w, false);
    int darkTotal = 0, darkNear = 0;
    sampleSpawns(w, p, 400, &darkTotal, &darkNear);
    printf("lamp off: %d placements, %d of them where the lamp was\n", darkTotal, darkNear);
    check(darkTotal > 100, "creatures do spawn in this cavern when it is dark");
    check(darkNear > 0, "including where the lamp used to be");

    if (failures) {
        fprintf(stderr, "%d spawn darkness check(s) failed\n", failures);
        return 1;
    }
    puts("the dark spawns creatures and the lit margin does not");
    return 0;
}
