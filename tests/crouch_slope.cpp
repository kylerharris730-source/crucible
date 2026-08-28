/* --- crouching down a slope should not strobe --------------------------------

   Reported from play: "lets add some grace to crouching, right now when you do
   it down a slope its jumpy."

   Same root cause the ANIMATION coyote time already documents at the top of
   player.cpp: terrain is a grid, so a slope is a staircase, and walking down
   one means genuinely leaving the ground every step. That note measured the
   character airborne 48% of frames on a 1-in-4 descent.

   Crouch asked `in.down && onGround` fresh every frame, so on a descent it
   dropped out for roughly half of them. And crouch is not a pose -- it is a
   BOX, PLAYER_H tall against CROUCH_H, feet fixed. Losing it for one frame
   grows the body six cells upward and regaining it shrinks it six cells back,
   so the head pumps up and down several times a second. That is the jumpiness.

   Three properties:

     the crouch survives a descent without flipping   (the report)
     the head stops pumping                           (what it looks like)
     a real drop still stands you up                  (grace, not a latch)

   The third is what keeps this honest. A grace that never expires is just
   "crouch is sticky", and walking off a ledge while holding down would leave
   you crouched in mid-air all the way to the ground.

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "player.h"
#include "device.h"
#include "multiplayer.h"
#include <stdio.h>

static World g_testWorld;

static const int CX = 1400, CY = 5000;

/* A staircase descending to the right: `run` cells across per `drop` down.
   1-in-4 is the shallow case the animation note calls the worst, because the
   airborne and grounded frames are evenly matched and it strobes hardest. */
static void staircase(World& w, int run, int drop, int steps) {
    w.reset();
    for (int s = 0; s < steps; ++s) {
        const int x0 = CX + s * run, y0 = CY + s * drop;
        for (int x = x0; x < x0 + run; ++x)
            for (int y = y0; y < y0 + 60; ++y)
                if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1)
                    w.setCell(x, y, MAT_STONE);
    }
    w.setLiveWindow(CX - 40, CY - 80, CX + run * steps + 40, CY + drop * steps + 80);
}

struct Trace { int flips; int maxHeadJump; int crouchedFrames; int frames; };

static Trace walkDown(World& w, int run, int drop, int steps, bool holdDown) {
    staircase(w, run, drop, steps);
    Player& p = g_player;
    p.reset((float)(CX + 2), (float)(CY - PLAYER_H));
    p.alive = true; p.hp = PLAYER_HP_MAX;

    PlayerInput in;
    in.left = false; in.right = true; in.jump = false; in.down = holdDown;

    /* Settle onto the first step before measuring, so the initial fall is not
       counted as a flip. */
    for (int f = 0; f < 30; ++f) p.update(w, in);

    Trace t; t.flips = 0; t.maxHeadJump = 0; t.crouchedFrames = 0; t.frames = 0;
    bool wasCrouched = p.crouching;
    int prevHead = p.top();
    const int stopX = CX + run * (steps - 2);
    for (int f = 0; f < 1200 && p.centreX() < (float)stopX; ++f) {
        p.update(w, in);
        ++t.frames;
        if (p.crouching != wasCrouched) { ++t.flips; wasCrouched = p.crouching; }
        if (p.crouching) ++t.crouchedFrames;
        /* The head is what you watch. Feet stay on the stairs either way; it is
           the TOP of the box that snaps six cells when the crouch drops out. */
        const int head = p.top();
        const int jump = head > prevHead ? head - prevHead : prevHead - head;
        if (jump > t.maxHeadJump) t.maxHeadJump = jump;
        prevHead = head;
    }
    return t;
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();
    World& w = g_testWorld;
    int failures = 0;

    /* --- 1. the descent, at the slope that strobes worst ----------------- */
    {
        const Trace t = walkDown(w, 4, 1, 60, true);
        printf("1-in-4 crouched descent: %d flips in %d frames, crouched %d%% of "
               "them, worst head jump %d cells\n",
               t.flips, t.frames,
               t.frames ? t.crouchedFrames * 100 / t.frames : 0, t.maxHeadJump);
        if (t.flips > 2) {
            fprintf(stderr, "FAIL: the crouch flipped %d times going down one "
                            "slope -- that is the pumping that was reported\n",
                    t.flips);
            ++failures;
        }
        if (t.maxHeadJump > 2) {
            fprintf(stderr, "FAIL: the head moved %d cells in a single frame; the "
                            "crouch box is growing and shrinking under you\n",
                    t.maxHeadJump);
            ++failures;
        }
        if (t.frames && t.crouchedFrames * 100 / t.frames < 90) {
            fprintf(stderr, "FAIL: crouched for only %d%% of the descent while "
                            "holding down\n", t.crouchedFrames * 100 / t.frames);
            ++failures;
        }
    }

    /* --- 2. a steeper one, where more frames are genuinely airborne ------- */
    {
        const Trace t = walkDown(w, 2, 1, 60, true);
        printf("1-in-2 crouched descent: %d flips, crouched %d%% of %d frames\n",
               t.flips, t.frames ? t.crouchedFrames * 100 / t.frames : 0, t.frames);
        if (t.flips > 2) {
            fprintf(stderr, "FAIL: %d flips on the steeper slope\n", t.flips);
            ++failures;
        }
    }

    /* --- 3. a real drop still stands you up ------------------------------ */
    {
        /* One step, then nothing: the character walks off the end into open
           air. Grace must expire -- otherwise this is not grace, it is a latch,
           and you would sail down crouched. */
        w.reset();
        for (int x = CX; x < CX + 60; ++x)
            for (int y = CY; y < CY + 20; ++y)
                w.setCell(x, y, MAT_STONE);
        w.setLiveWindow(CX - 40, CY - 300, CX + 200, CY + 60);

        Player& p = g_player;
        p.reset((float)(CX + 2), (float)(CY - PLAYER_H));
        p.alive = true; p.hp = PLAYER_HP_MAX;
        PlayerInput in;
        in.left = false; in.right = true; in.jump = false; in.down = true;
        for (int f = 0; f < 40; ++f) p.update(w, in);
        const bool crouchedOnGround = p.crouching;

        /* Measured from the frame it actually LEAVES the ledge, not from the
           start of the walk -- a first version counted the stroll to the edge
           and reported 56 frames for what is really one or two. */
        int leftAt = -1, stoodAfter = -1;
        for (int f = 0; f < 400; ++f) {
            p.update(w, in);
            if (!p.onGround && leftAt < 0) leftAt = f;
            if (leftAt >= 0 && !p.crouching && stoodAfter < 0) stoodAfter = f - leftAt;
            if (p.onGround && leftAt >= 0 && f > leftAt + 2) break;
        }
        printf("walked off a ledge holding down: was crouched on the ledge %s, "
               "stood up %d frames into the fall\n",
               crouchedOnGround ? "yes" : "NO", stoodAfter);
        if (!crouchedOnGround) {
            fprintf(stderr, "FAIL: never crouched on the flat, so the drop test "
                            "proves nothing\n");
            ++failures;
        }
        if (stoodAfter < 0) {
            fprintf(stderr, "FAIL: still crouched all the way down a real drop -- "
                            "the grace never expires, which makes it a latch\n");
            ++failures;
        }
    }

    if (failures) {
        fprintf(stderr, "\n%d crouch check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
