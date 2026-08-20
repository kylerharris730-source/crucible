/* --- can you walk up a sloped run of platforms? ------------------------------

   Reported from play: walking up a sloped platform walks THROUGH it.

   The mechanism, before the fix. A platform answers `playerSolid` with true
   only for SOLID_FLOOR -- solid when the question is "can I stand here" and
   thin air otherwise. The horizontal step in Player::update asks SOLID_ANY,
   because a sideways move is exactly the case where a platform should not stop
   you. So a platform NEVER blocks horizontal movement, the step-up code that
   handles a stone stair is never reached, and the body walks straight into the
   column. Then the vertical step notices the box is already overlapping a
   platform, sets `insidePlatform`, and correctly refuses to treat that platform
   as a floor -- because that rule is what lets you come back down through one
   you jumped up through. Every individual rule is right and the combination
   drops you through the stairs. Measured before the fix: feet ended at row
   1662, having started at 699.

   Two cases here, and the second is the one that keeps the first honest. A
   generous step-up assist that lifts you onto ANY platform your body overlaps
   would also yank you on top of a platform bridge you were trying to walk
   under, which is most of what platforms are for. So this checks both that the
   staircase is climbed and that a chest-height platform is still walked
   through.

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "player.h"
#include "multiplayer.h"
#include <stdio.h>

static World g_testWorld;

/* Walk right until the feet pass `stopX`, or `frames` run out. Returns the
   feet's row at that moment.

   Stopping at a column matters: the staircase is finite, and walking off the
   end of it into the pit is correct behaviour rather than a failure. The first
   version of this test ran a fixed 240 frames, sailed off the top step, and
   reported the resulting fall as the original bug -- with the trace showing a
   perfectly good climb from 699 to 684 immediately before it. */
static int walkRightTo(World& w, float startX, int floorY, float stopX,
                       int frames, int* lowestFeet) {
    Player& p = g_player;
    p.reset(startX, (float)(floorY - PLAYER_H));
    p.alive = true;
    p.hp = PLAYER_HP_MAX;

    PlayerInput in;
    in.left = false; in.right = true; in.jump = false; in.down = false;

    *lowestFeet = p.top() + PLAYER_H - 1;
    for (int f = 0; f < frames; ++f) {
        p.update(w, in);
        const int feet = p.top() + PLAYER_H - 1;
        if (feet > *lowestFeet) *lowestFeet = feet;
        if (p.x >= stopX) break;
    }
    return p.top() + PLAYER_H - 1;
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();

    World& w = g_testWorld;
    const int floorY = 700;
    int failures = 0;

    /* --- climbing the staircase, at two slopes ---------------------------
       `riseEvery` columns per cell of rise: 2 is the shallowest the grid can
       express, 1 is a 45-degree run, which is what somebody actually builds by
       dragging a line of platforms. Both, because the shallow one is the
       friendliest possible case and the steep one is the realistic case, and a
       fix that only handles the friendly one is a fix that does not work. */
    for (int riseEvery = 2; riseEvery >= 1; --riseEvery) {
        w.reset();
        /* A ledge to start on, and nothing at all past x = 1000 except the
           stairs. The pit is the measurement: with ordinary ground underneath,
           "walked through the platform" and "walked along the floor" look
           identical from the outside. */
        for (int x = 900; x <= 1000; ++x)
            for (int y = floorY; y <= floorY + 8; ++y)
                w.setCell(x, y, MAT_STONE);

        /* One cell of rise every two columns, which is the shallowest slope the
           grid can express and therefore the friendliest possible case -- if
           this does not work, nothing steeper will. Thirty columns, so a
           working climb is unmistakably a climb rather than one lucky step. */
        const int stairs = 30;
        for (int i = 0; i < stairs; ++i)
            w.setCell(1001 + i, floorY - 1 - i / riseEvery, MAT_PLATFORM);

        const int topStair = floorY - 1 - (stairs - 1) / riseEvery;
        int lowest = 0;
        const int feet = walkRightTo(w, 960.0f, floorY, 1001.0f + stairs - 3.0f,
                                     600, &lowest);

        printf("stairs 1-in-%d: started row %d, ended row %d, top stair is row %d\n",
               riseEvery, floorY - 1, feet, topStair);
        if (lowest > floorY - 1) {
            printf("           (dropped as low as row %d on the way)\n", lowest);
        }
        if (feet > floorY - 1) {
            fprintf(stderr, "FAIL: walked through the sloped platforms\n");
            ++failures;
        } else if (feet > topStair + 2) {
            fprintf(stderr, "FAIL: only climbed to row %d, the stairs reach %d\n",
                    feet, topStair);
            ++failures;
        }
        /* It must never have fallen BELOW where it started either. Climbing to
           the top after dropping through the first few stairs and catching the
           later ones would pass the check above and is not what was asked
           for. */
        if (lowest > floorY - 1) {
            fprintf(stderr, "FAIL: fell to row %d part way up -- the climb has a "
                            "hole in it\n", lowest);
            ++failures;
        }
    }

    /* --- walking UNDER a platform, which must still work ------------------ */
    {
        w.reset();
        for (int x = 900; x <= 1100; ++x)
            for (int y = floorY; y <= floorY + 8; ++y)
                w.setCell(x, y, MAT_STONE);

        /* A flat platform bridge at chest height -- 15 cells above the feet,
           which is half the body. Clearing it would need a lift of 16 against
           a PLAYER_PLATFORM_STEP_UP of 12, and that margin is the whole reason
           the assist can afford to be as generous as it is. */
        const int bridgeY = floorY - 1 - 15;
        for (int x = 1000; x <= 1060; ++x) w.setCell(x, bridgeY, MAT_PLATFORM);

        int lowest = 0;
        const int feet = walkRightTo(w, 960.0f, floorY, 1050.0f, 600, &lowest);
        printf("bridge:    walked to row %d, the bridge is at row %d\n", feet, bridgeY);
        if (feet != floorY - 1) {
            fprintf(stderr, "FAIL: the assist lifted the character onto a "
                            "chest-height platform instead of letting them walk "
                            "under it\n");
            ++failures;
        }
    }

    if (failures) { fprintf(stderr, "\n%d platform check(s) failed\n", failures); return 1; }
    printf("\nPASS\n");
    return 0;
}
