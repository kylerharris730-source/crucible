/* --- the Skirmisher shoots while walking backwards ---------------------------

   Asked for: "lets add that third shooter that retreats while firing".

   Layer 2's other two shooters both hand the player the range -- the Culverin
   plants itself and makes you come, the Wisp closes regardless -- so backing
   away beats one and buys time against the other. This one withdraws while it
   fires, which takes that away.

   Five properties, and the last three are entirely about being CATCHABLE. A
   creature that cannot be escaped and cannot be cornered is not an enemy, it
   is a tax:

     it fires from range                        (it is a shooter)
     it moves AWAY while doing it               (the request)
     it stops retreating when it cannot see you (cover is a tool)
     it gives up retreating once cornered       (no grinding into rock)
     it closes when you are far off             (it engages, not evades)

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "player.h"
#include "projectile.h"
#include "multiplayer.h"
#include <stdio.h>
#include <math.h>

static World g_testWorld;
static const int CX = 1400, CY = 5000;
static const int FLOOR = CY + 40;

static void fill(World& w, int x0, int y0, int x1, int y1, u8 m) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1)
                w.setCell(x, y, m);
}

/* A long flat hall. The player stands at the left end; the creature starts
   `gap` cells to its right with room behind it to retreat into. */
static int setup(World& w, int gap, int roomBehind) {
    w.reset();
    projClear();
    fill(w, CX - 60, CY - 60, CX + gap + roomBehind + 60, FLOOR + 40, MAT_STONE);
    fill(w, CX - 40, CY, CX + gap + roomBehind + 40, FLOOR, MAT_EMPTY);
    w.setLiveWindow(CX - 80, CY - 80, CX + gap + roomBehind + 80, FLOOR + 60);

    Player& p = g_player;
    p.reset((float)CX, (float)(FLOOR - PLAYER_H));
    p.alive = true; p.hp = PLAYER_HP_MAX;

    entReset();
    return entSpawn(w, ENT_SKIRMISHER, (float)(CX + gap), (float)(FLOOR - 10));
}

/* The player PURSUES, at a walking pace, unless `chase` is off.

   A kiter only kites if something is coming at it. Pinning the player makes
   the creature simply run out of range and stop shooting, which measures its
   walking speed rather than its behaviour -- a first version of this did
   exactly that and found one shot in ten taken while withdrawing. Chasing is
   the situation the creature exists for. */
static bool g_chase = true;

static void run(World& w, int frames) {
    Player& p = g_player;
    for (int f = 0; f < frames; ++f) {
        if (g_chase) {
            const float toward = g_entities[0].centreX() - p.centreX();
            /* Just UNDER the creature's own 0.58, so it can hold its band and
               actually operate. Chasing faster than it retreats corners it in
               a couple of hundred frames and measures being cornered rather
               than kiting -- which a first version of this did, and reported
               two shots where the interval says twelve. */
            if (toward > 2.0f)  p.x += 0.50f;
            else if (toward < -2.0f) p.x -= 0.50f;
        } else {
            p.x = (float)CX;
        }
        p.y = (float)(FLOOR - PLAYER_H);
        p.alive = true; p.hp = PLAYER_HP_MAX;
        entTick(w, p, g_inv);
        projUpdate(w);
    }
}

static int hostileShots() {
    int n = 0;
    for (int i = 0; i < MAX_PROJ; ++i)
        if (g_proj[i].alive && g_proj[i].hostile) ++n;
    return n;
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();
    World& w = g_testWorld;
    int failures = 0;

    /* --- 1 & 2. it shoots, and it retreats while doing it ---------------- */
    {
        const int e = setup(w, 60, 900);
        if (e < 0) { fprintf(stderr, "could not place the skirmisher\n"); return 2; }
        const float x0 = g_entities[e].centreX();
        int shots = 0, firedWhileRetreating = 0;
        float prevX = x0;
        for (int f = 0; f < 900; ++f) {
            const int before = hostileShots();
            run(w, 1);
            if (g_entities[e].type == ENT_NONE) break;
            const float nx = g_entities[e].centreX();
            if (hostileShots() > before) {
                ++shots;
                /* Retreating means moving AWAY from the player, who is to the
                   left -- so the creature's x should be rising. */
                if (nx > prevX) ++firedWhileRetreating;
            }
            prevX = nx;
            projClear();
        }
        const float moved = g_entities[e].centreX() - x0;
        printf("open hall: %d shots, %d of them while backing away, "
               "net movement %+.0f cells (away is positive)\n",
               shots, firedWhileRetreating, moved);

        if (shots < 3) {
            fprintf(stderr, "FAIL: only %d shots in 900 frames -- it is not a "
                            "shooter\n", shots);
            ++failures;
        }
        if (moved < 40.0f) {
            fprintf(stderr, "FAIL: it only moved %+.0f cells -- it is not "
                            "retreating\n", moved);
            ++failures;
        }
        if (shots && firedWhileRetreating * 2 < shots) {
            fprintf(stderr, "FAIL: only %d of %d shots came while moving away -- "
                            "it shoots and retreats separately rather than at "
                            "once\n", firedWhileRetreating, shots);
            ++failures;
        }
    }

    /* --- 3. it stops retreating when it cannot see you ------------------- */
    {
        const int e = setup(w, 60, 400);
        g_chase = false;
        /* A pillar between them, floor to ceiling. */
        fill(w, CX + 25, CY, CX + 33, FLOOR, MAT_STONE);
        const float x0 = g_entities[e].centreX();
        run(w, 600);
        const float moved = g_entities[e].centreX() - x0;
        printf("behind cover: net movement %+.0f cells\n", moved);
        if (moved > 20.0f) {
            fprintf(stderr, "FAIL: it withdrew %+.0f cells with a wall in the way "
                            "-- losing sight has to stop the retreat, or it can "
                            "never be caught\n", moved);
            ++failures;
        }
    }

    /* --- 4. cornered, it stops trying to walk through the wall ----------- */
    {
        const int e = setup(w, 60, 6);      /* almost no room behind it */
        g_chase = true;
        run(w, 400);
        if (g_entities[e].type == ENT_NONE) {
            fprintf(stderr, "FAIL: it despawned\n");
            ++failures;
        } else {
            /* Cornered is not a position, it is a decision: the creature has
               stopped spending every frame pushing into rock. Read it off the
               same counter the tick uses. */
            const int stuckFor = g_entities[e].actTimer;
            float drift = 0.0f;
            const float atx = g_entities[e].centreX();
            run(w, 120);
            drift = fabsf(g_entities[e].centreX() - atx);
            printf("cornered: blocked-frame counter %d, drifted %.1f cells in the "
                   "next 120 frames\n", stuckFor, drift);
            if (stuckFor <= 0) {
                fprintf(stderr, "FAIL: never registered as cornered with six "
                                "cells behind it\n");
                ++failures;
            }
        }
    }

    /* --- 5. far away, it closes ------------------------------------------ */
    {
        const int e = setup(w, 300, 200);
        g_chase = false;
        const float x0 = g_entities[e].centreX();
        run(w, 600);
        const float moved = g_entities[e].centreX() - x0;
        printf("from 300 cells: net movement %+.0f cells (toward is negative)\n",
               moved);
        if (moved > -20.0f) {
            fprintf(stderr, "FAIL: it did not close from long range -- a kiter "
                            "that also runs from a distant player never fights\n");
            ++failures;
        }
    }

    if (failures) {
        fprintf(stderr, "\n%d skirmisher check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
