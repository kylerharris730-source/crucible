/* --- does the moth stop, then dash? -----------------------------------------

   The roster's rule is "simple AI, distinct MOVEMENT", and the moth was
   breaking it: it followed its heat gradient continuously, which is the same
   shape the husk and the slime move in and differs only in speed. This checks
   the stalk gives it a genuinely different profile.

   Three properties, and the second is the one that makes it a TELL rather than
   just a speed change:

     it must reach a speed the bat cannot loiter at   (the dash is real)
     it must come to a near-stop before every dash    (the pause is readable)
     the pause must come BEFORE the dash, not after   (it telegraphs)

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "multiplayer.h"
#include <stdio.h>
#include <math.h>

static World g_testWorld;

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();

    World& w = g_testWorld;
    w.reset();
    /* Open air, no heat anywhere, so the drift is a plain approach and the only
       thing shaping the trace is the stalk. */
    const int cx = 1000, cy = 700;
    w.setLiveWindow(cx - 300, cy - 300, cx + 300, cy + 300);

    Player& p = g_player;
    p.x = (float)cx; p.y = (float)cy;
    p.alive = true; p.hp = PLAYER_HP_MAX;

    entReset();
    const int m = entSpawn(w, ENT_MOTH, (float)(cx + 160), (float)(cy - 40));
    if (m < 0) { fprintf(stderr, "could not place the moth\n"); return 2; }

    int fastFrames = 0, stillFrames = 0, telegraphFrames = 0;
    float peak = 0.0f;
    /* Was the frame before each burst of speed a near-stop? */
    int dashesSeen = 0, dashesPrecededByPause = 0;
    bool wasFast = false;
    int slowRun = 0;

    for (int f = 0; f < 900; ++f) {
        /* The player is pinned so the moth always has the same target and the
           trace is about the moth's own rhythm rather than about a chase. */
        p.x = (float)cx; p.y = (float)cy;
        p.alive = true; p.hp = PLAYER_HP_MAX;
        entTick(w, p, g_inv);

        const Entity& e = g_entities[m];
        if (e.type == ENT_NONE) { fprintf(stderr, "the moth died\n"); return 3; }
        const float sp = sqrtf(e.vx * e.vx + e.vy * e.vy);
        if (sp > peak) peak = sp;

        const bool fast = sp > 1.2f;
        const bool still = sp < 0.25f;
        if (fast)  ++fastFrames;
        if (still) ++stillFrames;
        if (e.telegraph > 0) ++telegraphFrames;

        if (fast && !wasFast) {
            ++dashesSeen;
            /* A pause immediately before counts as the telegraph. */
            if (slowRun >= 4) ++dashesPrecededByPause;
        }
        slowRun = still ? slowRun + 1 : 0;
        wasFast = fast;
    }

    const float base = ENT_DEFS[ENT_MOTH].speed;
    printf("moth base speed %.2f, peak reached %.2f (%.1fx)\n", base, peak, peak / base);
    printf("frames fast %d, near-still %d, telegraphing %d of 900\n",
           fastFrames, stillFrames, telegraphFrames);
    printf("dashes %d, of which preceded by a pause %d\n",
           dashesSeen, dashesPrecededByPause);

    int failures = 0;
    if (peak < base * 2.0f) {
        fprintf(stderr, "FAIL: never exceeded twice its drift speed -- there is "
                        "no dash, only a walk\n");
        ++failures;
    }
    if (stillFrames < 60) {
        fprintf(stderr, "FAIL: only %d frames near-still in 900 -- it never "
                        "stops, so there is nothing to read\n", stillFrames);
        ++failures;
    }
    if (dashesSeen < 3) {
        fprintf(stderr, "FAIL: only %d dashes in 900 frames\n", dashesSeen);
        ++failures;
    }
    /* The ordering is the whole point. A creature that dashes and THEN rests
       has a cooldown; one that rests and then dashes has a tell. */
    if (dashesSeen && dashesPrecededByPause * 2 < dashesSeen) {
        fprintf(stderr, "FAIL: only %d of %d dashes were preceded by a pause -- "
                        "the stop is a cooldown, not a telegraph\n",
                dashesPrecededByPause, dashesSeen);
        ++failures;
    }
    if (telegraphFrames < 40) {
        fprintf(stderr, "FAIL: telegraph flag set on only %d frames -- nothing "
                        "is drawn to warn the player\n", telegraphFrames);
        ++failures;
    }

    if (failures) { fprintf(stderr, "\n%d moth check(s) failed\n", failures); return 1; }
    printf("\nPASS\n");
    return 0;
}
