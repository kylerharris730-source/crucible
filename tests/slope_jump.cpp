/* --- jumping while walking down a slope ---------------------------------------

   Asked for: "add a grace period so going down slopes doesnt eat your jump".

   The same root cause two other graces in player.cpp already carry: terrain is
   a grid, so a slope is a staircase, and walking down one genuinely leaves the
   ground every step -- measured at 40-48% of frames airborne on a shallow
   descent. The walk cycle got coyote time for it, and so did the crouch. The
   JUMP did not: it asked `in.jump && onGround` fresh, so pressing jump on a
   descent did nothing about half the time, and which half was invisible.

   What is measured here is the thing the player feels: press jump on an
   arbitrary frame of a descent, and did you leave the ground?

     a jump pressed while walking down a slope is taken
     a jump pressed in real open air is NOT                  (grace, not flight)
     the grace does not hand out a second jump in one leap   (no free hop)
     a deliberate walk off a ledge still jumps briefly after (coyote time)

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
static int failures = 0;

static void check(bool ok, const char* what) {
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static const int CX = 1400, CY = 5000;

/* A staircase descending to the right: `run` cells across per `drop` down. */
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

/* Walk down `run`-in-`drop` stairs and press jump on frame `pressAt` of the
   descent. Returns true if the character actually left the ground: rising, and
   higher a few frames later than it would have been walking. */
static bool jumpTaken(World& w, int run, int drop, int pressAt) {
    staircase(w, run, drop, 60);
    Player& p = g_player;
    p.reset((float)(CX + 2), (float)(CY - PLAYER_H));
    p.alive = true; p.hp = PLAYER_HP_MAX;

    PlayerInput in;
    in.left = false; in.right = true; in.jump = false; in.down = false;
    for (int f = 0; f < 30; ++f) p.update(w, in);   /* settle onto the stairs */

    for (int f = 0; f < pressAt; ++f) p.update(w, in);
    const float before = p.y;
    in.jump = true;
    p.update(w, in);
    const bool rising = p.vy < 0.0f;
    in.jump = false;
    /* A slope descent moves DOWN every frame, so "did it jump" cannot be read
       off one frame's velocity alone -- check it is actually higher shortly
       after than where it started, which walking never is. */
    for (int f = 0; f < 6; ++f) p.update(w, in);
    return rising && p.y < before;
}

/* How many frames of an `airborne` gap a press is still answered in. */
static int graceFrames(World& w) {
    /* A flat ledge that stops: walk off the end and count how long a press is
       still taken. */
    w.reset();
    for (int x = CX - 40; x < CX + 20; ++x)
        for (int y = CY; y < CY + 40; ++y) w.setCell(x, y, MAT_STONE);
    w.setLiveWindow(CX - 80, CY - 80, CX + 80, CY + 80);

    for (int wait = 0; wait < 40; ++wait) {
        Player& p = g_player;
        p.reset((float)(CX + 10), (float)(CY - PLAYER_H));
        p.alive = true; p.hp = PLAYER_HP_MAX;
        PlayerInput in;
        in.left = false; in.right = false; in.jump = false; in.down = false;
        /* Settle STANDING STILL. Settling while walking marches the character
           straight off the ledge this measurement is about. */
        for (int f = 0; f < 30; ++f) p.update(w, in);
        in.right = true;
        int off = 0;
        while (p.onGround && off < 200) { p.update(w, in); ++off; }
        for (int f = 0; f < wait; ++f) p.update(w, in);
        const float before = p.y;
        in.jump = true;
        p.update(w, in);
        if (!(p.vy < 0.0f && p.y <= before)) return wait;   /* first refusal */
    }
    return 40;
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();
    World& w = g_testWorld;

    /* --- 1. the report: a press on a descent is answered ------------------ */
    for (int slope = 0; slope < 3; ++slope) {
        static const int RUN[3]  = { 4, 3, 2 };
        static const int DROP[3] = { 1, 1, 1 };
        int taken = 0;
        for (int at = 0; at < 24; ++at)
            if (jumpTaken(w, RUN[slope], DROP[slope], at)) ++taken;
        printf("  1-in-%d descent: %d of 24 presses jumped\n", RUN[slope], taken);
        char what[64];
        snprintf(what, sizeof(what), "every press on a 1-in-%d descent jumps", RUN[slope]);
        check(taken == 24, what);
    }

    /* --- 2. grace, not flight --------------------------------------------- */
    {
        const int grace = graceFrames(w);
        printf("  a press is still answered %d frames after walking off a ledge\n", grace);
        check(grace > 0, "walking off a ledge leaves a moment to jump");
        check(grace <= 12, "and only a moment -- this is not flight");
    }

    /* --- 3. no free second jump in one leap ------------------------------- */
    {
        staircase(w, 4, 1, 60);
        Player& p = g_player;
        p.reset((float)(CX + 2), (float)(CY - PLAYER_H));
        p.alive = true; p.hp = PLAYER_HP_MAX;
        PlayerInput in;
        in.left = false; in.right = true; in.jump = false; in.down = false;
        for (int f = 0; f < 30; ++f) p.update(w, in);
        in.jump = true;
        p.update(w, in);                     /* the jump itself */
        const float apexStart = p.vy;
        int extra = 0;
        for (int f = 0; f < 20; ++f) {       /* hammering it through the rise */
            in.jump = (f & 1) != 0;
            const float was = p.vy;
            p.update(w, in);
            if (p.vy < was - 1.0f) ++extra;  /* a second impulse mid-rise */
        }
        printf("  hammering jump through one leap: %d extra impulses (vy %.2f)\n",
               extra, apexStart);
        check(extra == 0, "a jump in progress is never re-fired by the grace");
    }

    if (failures) { fprintf(stderr, "\n%d slope jump check(s) failed\n", failures); return 1; }
    printf("PASS\n");
    return 0;
}
