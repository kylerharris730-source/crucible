/* --- can a walker get to you when the straight line is a wall? ---------------

   Reported from play: "enemy pathfinding, dumb, they just walk in a straight
   line to you, running into walls forever very often."

   That is exactly what the code does. groundChase asks one question -- is the
   player to my left or my right -- and moveAxis answers a blocked step by
   zeroing vx and flipping `facing`. The flip looks like an escape and is not:
   groundChase re-derives facing from the player's position on the very next
   frame, so the creature presses into the wall, turns, turns back, and does
   that until it despawns. A cave with any structure at all is a cage.

   The layout here is two chambers cut out of solid rock, side by side, with
   rock between them. The only route from one to the other leaves by the far
   end of the enemy's chamber, drops to a lower corridor, runs back underneath
   and climbs a staircase into the player's chamber. So the first move is
   AWAY from the player, which no gradient follower will ever make.

   Every climb here is a staircase the creature can actually walk: a husk steps
   up 11 cells (moveAxis, d.h/2) and hops about 13 (vy 2.2 against gravity
   0.18), so 6-cell risers are comfortably within it. If this test ever fails
   after a change, check that first -- an unwalkable staircase looks exactly
   like a pathfinding failure.

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "player.h"
#include "multiplayer.h"
#include "navigate.h"
#include <stdio.h>
#include <math.h>

static World g_testWorld;

static void fill(World& w, int x0, int y0, int x1, int y1, u8 mat) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1)
                w.setCell(x, y, mat);
}

/* A ramp between two heights: air above a stepped floor, and SOLID underneath.

   Two things here were each found by the walkability guard below rather than
   by reasoning, and both are worth keeping written down.

   The riser is capped at 6 cells rather than split into a fixed number of
   steps. A husk steps up d.h/2 = 11, so an 11-cell riser sits exactly on its
   limit and climbs or fails depending on where in the step its feet land.

   The ceiling is FLAT, taken from the high end, instead of following the floor
   down. A tunnel whose roof descends with its steps is a constant-height tube,
   and a 22-cell husk straddling two steps has its feet on the high one and its
   head in the low one's roof. Measured: wedged at the first riser for 3,800
   frames, which is indistinguishable from a pathfinding failure.

   And the fill underneath is what makes a ramp a ramp. The corridor is carved
   before this runs and spans the ramp's columns, so without it the ramp is a
   SHELF with open air below -- the husk ran along the corridor underneath the
   slope it was meant to climb and stalled at the dead end. Refilling below the
   step floor closes that, and closes it only where the ramp is above the
   corridor: at the base the step floor IS the corridor floor, nothing is
   filled, and the two stay connected. */
static void stair(World& w, int x0, int yFrom, int x1, int yTo, int headroom) {
    const int rise = yTo > yFrom ? yTo - yFrom : yFrom - yTo;
    int steps = (rise + 5) / 6;
    if (steps < 1) steps = 1;
    const int run = (x1 - x0) / steps;
    const int drop = (yTo - yFrom) / steps;
    const int roofY = (yFrom < yTo ? yFrom : yTo) - headroom;
    const int deep  = (yFrom > yTo ? yFrom : yTo);
    for (int s = 0; s <= steps; ++s) {
        const int x = x0 + run * s, y = yFrom + drop * s;
        const int xa = run > 0 ? x : x + run;
        const int xb = run > 0 ? x + run : x;
        fill(w, xa, roofY,  xb, y,    MAT_EMPTY);
        fill(w, xa, y + 1,  xb, deep, MAT_STONE);
    }
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();

    World& w = g_testWorld;
    w.reset();

    const int cx = 1400, cy = 5000;
    /* Solid rock, so a chamber is a chamber rather than open sky. */
    fill(w, cx - 460, cy - 60, cx + 280, cy + 200, MAT_STONE);
    w.setLiveWindow(cx - 480, cy - 80, cx + 300, cy + 220);

    const int ROOF = cy, FLOOR = cy + 40;      /* both chambers */
    const int LOWY = cy + 90;                  /* the corridor underneath */

    /* Two chambers with solid rock between them, and a corridor running back
       underneath. The route out of the enemy's chamber is a shaft at its FAR
       end, so the first move toward the player is away from the player.

       Kept deliberately small: at ENT_DESPAWN_DIST = 700 a creature that walks
       far enough away simply stops existing, and an arena wide enough to make
       the detour dramatic would delete the subject mid-experiment. Widest
       separation on the intended route here is about 550.

       Why the enemy's exit is a SHAFT and the player's is a ramp: a corridor
       that doubles back has to pass beneath whatever the creature climbed, and
       a ramp with open corridor under it is a shelf rather than a ramp. Only
       the return climb needs to be climbable, so only that one is a ramp, and
       its column band ends exactly where the corridor begins. */
    fill(w, cx +  40, ROOF, cx + 220, FLOOR, MAT_EMPTY);   /* enemy's chamber */
    fill(w, cx - 400, ROOF, cx - 200, FLOOR, MAT_EMPTY);   /* player's chamber */
    fill(w, cx + 180, ROOF, cx + 220, LOWY, MAT_EMPTY);    /* the shaft down */
    fill(w, cx - 200, LOWY - 40, cx + 200, LOWY, MAT_EMPTY);
    stair(w, cx - 200, LOWY, cx - 260, FLOOR, 40);         /* the climb back */

    /* --- is the maze even walkable? -------------------------------------
       Checked BEFORE the real run, and it stays here permanently. A husk that
       fails to arrive proves nothing if the staircase is a cliff, and the two
       failures look identical from the outside. So the creature is first LURED
       through the intended route by a player who teleports along it: if it
       cannot follow a lure it is standing next to, the geometry is wrong and
       that is what the test should say. */
    int ledBy = 0;
    {
        Player& lure = g_player;
        entReset();
        const int probe = entSpawn(w, ENT_HUSK, (float)(cx + 100), (float)(FLOOR - 15));
        if (probe < 0) { fprintf(stderr, "could not place the probe husk\n"); return 2; }
        const int wpx[] = { cx + 200, cx + 200, cx,   cx - 195, cx - 280, cx - 330 };
        const int wpy[] = { FLOOR,    LOWY,     LOWY, LOWY,     FLOOR,    FLOOR    };
        const int wpn = (int)(sizeof(wpx) / sizeof(wpx[0]));
        int wp = 0, held = 0;
        for (int f = 0; f < 4000 && wp < wpn; ++f) {
            lure.x = (float)wpx[wp];
            lure.y = (float)(wpy[wp] - PLAYER_H);
            lure.alive = true; lure.hp = PLAYER_HP_MAX;
            entTick(w, lure, g_inv);
            const Entity& e = g_entities[probe];
            if (e.type == ENT_NONE) { fprintf(stderr, "the probe husk died\n"); return 3; }
            const float dx = e.centreX() - lure.centreX();
            const float dy = e.centreY() - lure.centreY();
            if (dx * dx + dy * dy < 40.0f * 40.0f) { ++wp; held = f; }
        }
        if (wp < wpn) {
            fprintf(stderr, "FAIL: the LAYOUT is unwalkable -- a husk led by the "
                            "hand only reached waypoint %d of %d. Fix the "
                            "staircases before reading anything into the "
                            "pathfinding result.\n", wp, wpn);
            return 2;
        }
        printf("layout is walkable: a lured husk ran the whole detour by frame %d\n", held);
        ledBy = held;
    }

    Player& p = g_player;
    p.reset((float)(cx - 330), (float)(FLOOR - PLAYER_H));
    p.alive = true; p.hp = PLAYER_HP_MAX;

    entReset();
    const int h = entSpawn(w, ENT_HUSK, (float)(cx + 100), (float)(FLOOR - 15));
    if (h < 0) { fprintf(stderr, "could not place the husk\n"); return 2; }

    const int LIMIT = 6000;
    int arrived = -1, grinding = 0;
    float closest = 1e30f;
    float lastX = g_entities[h].x;
    int stalledFrames = 0;

    for (int f = 0; f < LIMIT; ++f) {
        /* Pinned, and immortal for the duration: this measures whether the
           creature can REACH, not whether it can win. */
        p.x = (float)(cx - 330); p.y = (float)(FLOOR - PLAYER_H);
        p.alive = true; p.hp = PLAYER_HP_MAX;
        entTick(w, p, g_inv);

        const Entity& e = g_entities[h];
        if (e.type == ENT_NONE) { fprintf(stderr, "the husk died or despawned at frame %d\n", f); return 3; }

        const float dx = e.centreX() - p.centreX(), dy = e.centreY() - p.centreY();
        const float dist = sqrtf(dx * dx + dy * dy);
        if (dist < closest) closest = dist;
        if (dist < 20.0f && arrived < 0) arrived = f;

        if (fabsf(e.x - lastX) < 0.02f) ++stalledFrames; else stalledFrames = 0;
        if (stalledFrames > 0) ++grinding;
        lastX = e.x;
    }

    printf("closest approach %.1f cells, arrived at frame %d, "
           "frames not moving horizontally %d of %d\n",
           closest, arrived, grinding, LIMIT);

    int failures = 0;
    if (arrived < 0) {
        fprintf(stderr, "FAIL: the husk never got within 20 cells in %d frames -- "
                        "closest it managed was %.1f\n", LIMIT, closest);
        ++failures;
    } else if (arrived > ledBy * 3) {
        /* Arriving eventually is not the whole of it. A creature that wanders
           into the right room by accident also arrives eventually, and the
           difference between that and routing is how long it takes relative to
           somebody walking the route on purpose. Three times the lured run is
           generous -- measured, it comes in at about 1.04x -- and it is here to
           catch a field that has degraded into a random walk rather than to
           police the tuning. */
        fprintf(stderr, "FAIL: arrived at frame %d, but a husk led by the hand "
                        "did it in %d -- that is wandering, not routing\n",
                arrived, ledBy);
        ++failures;
    }
    if (grinding > LIMIT / 4) {
        /* The original symptom, asserted directly. Some standing still is
           normal -- it falls, it climbs, it arrives and stops -- but a quarter
           of the run is the shape of a creature pressed against rock. Measured
           before the fix: 5,867 of 6,000. */
        fprintf(stderr, "FAIL: %d frames of %d with no horizontal movement -- "
                        "it is grinding on something\n", grinding, LIMIT);
        ++failures;
    }

    if (failures) { fprintf(stderr, "\n%d routing check(s) failed\n", failures); return 1; }
    printf("\nPASS\n");
    return 0;
}
