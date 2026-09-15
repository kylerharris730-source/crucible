/* --- can a bee get round a base? -------------------------------------------

   Reported from play: "bee pathfinding is still too dumb".

   Bees steered straight at what they wanted with a sixteen-cell look-ahead to
   dodge what was directly in front of them. That handles a pillar. It does not
   handle what a player actually builds round a hive: a wall, a room with a
   door, a ledge overhead, a rack of flowers, a pair of baffles. This puts a
   five-bee colony in each and counts completed round trips over six thousand
   frames.

   Measured before the fix, and each line below is what it guards:

                                   before   after
     open field, bed 90 cells off      10     109
     wall, way round over the top       0      75
     room, door on the far side         0      45
     flowers on a ledge overhead        0      77
     shelves of flowers                 9     106
     over one baffle, under the next    0      61

   The open field was broken for a different reason: the flower search read
   every second cell, and a bed of flowers is one row, so three scans in four
   saw nothing. The rest is bee_route.cpp.

   Plus the negative that makes the field worth having over a straight line:
   a flower sealed in a box is never chosen, so the colony stays home instead
   of pressing itself against the box.

   Compile with every source file except main.cpp. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "entity.h"
#include "device.h"
#include "player.h"
#include "multiplayer.h"
#include "light.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static const int HX = 1200, HY = 5000;
static Player g_p;
static Inventory g_beeInv;

static void rect(int x0, int y0, int x1, int y1, u8 m) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) g_world.setCell(x, y, m);
}

/* A flower bed: a stone strip with a flower on every other cell. */
static void bed(int x0, int x1, int y) {
    rect(x0, y + 1, x1, y + 1, MAT_STONE);
    for (int x = x0; x <= x1; x += 2) g_world.setCell(x, y, MAT_FLOWER);
}

struct Tally { int trips; int gathers; long nearHome; long beeFrames; };

static Tally runColony(Device* d, int frames) {
    static u8 lastPhase[MAX_ENTITIES];
    memset(lastPhase, 0xFF, sizeof(lastPhase));
    Tally t = { 0, 0, 0, 0 };
    for (int f = 0; f < frames; ++f) {
        devTick(g_world);
        entTick(g_world, g_p, g_beeInv);
        g_world.step();
        const float hx = d->x + DEV_W * 0.5f, hy = d->y + DEV_H * 0.5f;
        for (int i = 0; i < MAX_ENTITIES; ++i) {
            Entity& e = g_entities[i];
            if (!e.alive() || (e.type != ENT_BEE && e.type != ENT_COAL_BEE)) {
                lastPhase[i] = 0xFF; continue;
            }
            if (lastPhase[i] != 0xFF) {
                if (lastPhase[i] == 1 && e.phase == 0) ++t.trips;
                if (lastPhase[i] == 0 && e.phase == 1) ++t.gathers;
            }
            lastPhase[i] = (u8)e.phase;
            const float dx = e.centreX() - hx, dy = e.centreY() - hy;
            ++t.beeFrames;
            if (dx * dx + dy * dy < 45.0f * 45.0f) ++t.nearHome;
        }
    }
    return t;
}

/* Everything is built relative to the hive's own box. */
struct Site { int x0, y0, x1, y1, floor; };

static Device* setup(Site* s) {
    entReset();
    devClear();
    g_world.reset();
    g_world.setLiveWindow(HX - 500, HY - 500, HX + 500, HY + 500);
    g_p.reset((float)HX, (float)(HY - 300));
    g_beeInv.clear();
    for (u32 t = 0; t < DAY_LENGTH; t += 60) { g_worldTime = t; if (!isNight()) break; }
    const int floorY = HY + DEV_H / 2 + 1;
    rect(HX - 450, floorY, HX + 450, floorY + 3, MAT_STONE);
    if (!devPlace(g_world, DEV_HIVE, HX, HY)) return 0;
    Device* d = devAt(HX, HY);
    if (!d) return 0;
    d->value = 5;
    s->x0 = d->x; s->y0 = d->y; s->x1 = d->x + DEV_W - 1; s->y1 = d->y + DEV_H - 1;
    s->floor = floorY;
    return d;
}

typedef void (*Build)(const Site&);

static void openField(const Site& s) { bed(s.x1 + 90, s.x1 + 100, s.floor - 1); }

static void wallOver(const Site& s) {
    rect(s.x1 + 30, s.floor - 70, s.x1 + 33, s.floor - 1, MAT_STONE);
    bed(s.x1 + 60, s.x1 + 70, s.floor - 1);
}

/* The hive in a room whose only door is on the side away from the flowers. */
static void roomDoor(const Site& s) {
    const int L = s.x0 - 40, R = s.x1 + 40, T = s.y0 - 40;
    rect(L, T, R, T + 3, MAT_STONE);
    rect(R, T, R + 3, s.floor - 1, MAT_STONE);
    rect(L - 3, T, L, s.floor - 16, MAT_STONE);
    bed(R + 40, R + 50, s.floor - 1);
}

/* A ledge overhead with the flowers on top and the way up at its far end. */
static void ledge(const Site& s) {
    const int y = s.y0 - 30;
    rect(s.x0 - 120, y, s.x1 + 60, y + 3, MAT_STONE);
    bed(s.x0 - 20, s.x1 + 20, y - 1);
}

/* Four shelves open toward the hive, flowers at the back of each. */
static void shelves(const Site& s) {
    const int X0 = s.x1 + 50, X1 = s.x1 + 110;
    rect(X1, s.floor - 80, X1 + 3, s.floor - 1, MAT_STONE);
    for (int k = 0; k < 4; ++k) {
        const int y = s.floor - 1 - k * 20;
        rect(X0, y, X1, y, MAT_STONE);
        for (int x = X1 - 12; x < X1; x += 2) g_world.setCell(x, y - 1, MAT_FLOWER);
    }
}

/* Over the first baffle, under the second, with a roof so the long way round
   the top is not available. */
static void baffles(const Site& s) {
    rect(s.x1 + 30, s.floor - 80, s.x1 + 33, s.floor - 1, MAT_STONE);
    rect(s.x1 + 70, s.floor - 160, s.x1 + 73, s.floor - 30, MAT_STONE);
    rect(s.x0 - 60, s.floor - 160, s.x1 + 140, s.floor - 157, MAT_STONE);
    bed(s.x1 + 100, s.x1 + 110, s.floor - 1);
}

/* A bed sealed in a stone box, sixty cells off. */
static void sealed(const Site& s) {
    const int X0 = s.x1 + 50, X1 = s.x1 + 80;
    rect(X0, s.floor - 20, X1, s.floor - 17, MAT_STONE);
    rect(X0, s.floor - 20, X0 + 3, s.floor - 1, MAT_STONE);
    rect(X1 - 3, s.floor - 20, X1, s.floor - 1, MAT_STONE);
    bed(X0 + 8, X1 - 8, s.floor - 1);
}

static Tally play(Build build) {
    Site s;
    Device* d = setup(&s);
    if (!d) { fprintf(stderr, "could not place a hive\n"); Tally none = { -1, -1, 0, 0 }; return none; }
    build(s);
    runColony(d, 300 * 6);        /* let the colony come out */
    return runColony(d, 6000);
}

static void report(const char* name, const Tally& t) {
    printf("  %-30s trips %4d  gathers %4d  near home %5.1f%%\n", name, t.trips, t.gathers,
           t.beeFrames ? 100.0 * t.nearHome / t.beeFrames : 0.0);
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();

    const Tally open = play(openField);   report("open field", open);
    const Tally wall = play(wallOver);    report("wall", wall);
    const Tally room = play(roomDoor);    report("room with a far door", room);
    const Tally up   = play(ledge);       report("flowers on a ledge", up);
    const Tally rack = play(shelves);     report("shelves", rack);
    const Tally baf  = play(baffles);     report("two baffles", baf);
    const Tally box  = play(sealed);      report("sealed flowers", box);
    printf("\n");

    /* Thresholds at roughly half of what was measured, so a real regression
       fails and ordinary run-to-run wander does not. Before the fix every one
       of these but the shelves was at or near zero. */
    check(open.trips >= 60, "an open bed ninety cells off is worked");
    check(wall.trips >= 35, "a colony flies over a wall to its flowers");
    check(room.trips >= 20, "and out of a room by its door");
    check(up.trips   >= 35, "and round the end of a ledge to flowers on top");
    check(rack.trips >= 50, "and into shelves of flowers");
    check(baf.trips  >= 30, "and over one baffle and under the next");

    check(box.gathers == 0, "a sealed-off flower is never reached");
    check(box.beeFrames > 0 && box.nearHome * 10 >= box.beeFrames * 9,
          "and the colony stays home rather than pressing at it");

    if (failures) { fprintf(stderr, "\n%d bee route check(s) failed\n", failures); return 1; }
    printf("PASS\n");
    return 0;
}
