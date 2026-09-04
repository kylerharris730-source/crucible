/* --- the Thresher has to be able to catch somebody ---------------------------

   Asked for: "also make threshers faster".

   It scuttles: bursts of speed with stops between them (see thresherTick), and
   the stops are the design -- a creature that closes at a constant rate is one
   you back away from indefinitely at the same rate, while one that stops and
   then bursts makes you misjudge the gap. That is the only way a walker with no
   ranged attack ever catches anybody.

   Except the burst was 0.66 cells a frame against the character's 1.2, and the
   pause spends a third of the cycle standing still, so the AVERAGE was 0.45 --
   barely a third of walking pace. Neither half of the cadence could catch you:
   not the burst, not the average. The scuttle was a story the code told about a
   creature that was simply slower than you in every phase.

   Four properties, and the last two are what stop "faster" becoming "faster
   than you, forever":

     the BURST outruns a walking character      (the request)
     so it closes on someone who hesitates      (what that buys)
     the average stays under walking pace       (running away still works)
     and it still stops between bursts          (the cadence survives)

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "player.h"
#include "multiplayer.h"
#include <stdio.h>
#include <math.h>

static World g_testWorld;
static const int CX = 1400, CY = 5000;
static const int FLOOR = CY + 40;

/* The character's own top speed, from player.cpp. Copied rather than exported
   because it is a private constant there and one number does not justify
   publishing the movement model; if it moves, this harness is where the
   mismatch shows up. */
static const float PLAYER_TOP = 1.2f;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static void fill(World& w, int x0, int y0, int x1, int y1, u8 m) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1)
                w.setCell(x, y, m);
}

/* A long flat hall, the player at the left end, the creature `gap` to its
   right. The player flees rightward past the creature -- no: it starts to the
   player's RIGHT and the player runs LEFT, away from it, so "closing" is the
   creature's x falling toward the player's. */
static int setup(World& w, int gap) {
    w.reset();
    fill(w, CX - 900, CY - 60, CX + gap + 200, FLOOR + 40, MAT_STONE);
    fill(w, CX - 880, CY,      CX + gap + 180, FLOOR,      MAT_EMPTY);
    w.setLiveWindow(CX - 920, CY - 80, CX + gap + 220, FLOOR + 60);

    Player& p = g_player;
    p.reset((float)CX, (float)(FLOOR - PLAYER_H));
    p.alive = true; p.hp = PLAYER_HP_MAX;

    entReset();
    return entSpawn(w, ENT_THRESHER, (float)(CX + gap), (float)(FLOOR - 14));
}

/* Runs the creature against a player fleeing left at `flee` cells a frame, and
   reports the gap at the end plus the creature's fastest single frame. */
struct Run { float startGap, endGap, peakSpeed; int stillFrames, approachFrames; };

static Run flee(World& w, int gap, float fleeSpeed, int frames) {
    const int e = setup(w, gap);
    Run r; r.peakSpeed = 0.0f; r.stillFrames = 0; r.approachFrames = 0;
    if (e < 0) { r.startGap = r.endGap = 0.0f; return r; }
    Player& p = g_player;
    r.startGap = g_entities[e].centreX() - p.centreX();
    float prev = g_entities[e].centreX();
    for (int f = 0; f < frames; ++f) {
        p.x -= fleeSpeed;
        p.y = (float)(FLOOR - PLAYER_H);
        p.alive = true; p.hp = PLAYER_HP_MAX;
        entTick(w, p, g_inv);
        const float nx = g_entities[e].centreX();
        const float step = fabsf(nx - prev);
        if (step > r.peakSpeed) r.peakSpeed = step;
        /* Two corrections to what "paused" means, both of which a first version
           of this got wrong in the same direction -- making the creature faster
           made it look like it paused MORE.

           Only while APPROACHING, because a creature that has arrived and is
           oscillating through a stationary body registers a still frame every
           other frame and has nothing to do with the cadence.

           And "not bursting" rather than "perfectly motionless": groundChase
           sheds speed over several frames rather than stopping dead, so a
           literal zero-step test found 45 frames of a cadence that is a quarter
           pause -- it was measuring the deceleration ramp, not the stop. */
        if (step < 0.24f && fabsf(nx - p.centreX()) > 30.0f) ++r.stillFrames;
        if (fabsf(nx - p.centreX()) > 30.0f) ++r.approachFrames;
        prev = nx;
    }
    r.endGap = g_entities[e].centreX() - p.centreX();
    return r;
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();
    World& w = g_testWorld;

    /* --- 1. the burst outruns a walk -------------------------------------- */
    {
        const Run r = flee(w, 120, 0.0f, 600);
        printf("against a standing target: peak %.2f cells/frame "
               "(character walks at %.2f), gap %.0f -> %.0f\n",
               r.peakSpeed, PLAYER_TOP, r.startGap, r.endGap);
        check(r.peakSpeed > PLAYER_TOP,
              "a burst is faster than the character can walk");
    }

    /* --- 2. it closes on someone who is not sprinting --------------------- */
    /* Two thirds of walking pace: a player who is fighting, turning, or
       picking their way rather than running flat out. This is the case the
       creature exists for, and the one it used to lose. */
    {
        const Run r = flee(w, 200, PLAYER_TOP * 0.66f, 900);
        printf("against a half-committed retreat: gap %.0f -> %.0f\n",
               r.startGap, r.endGap);
        check(r.endGap < r.startGap * 0.5f,
              "it runs down a player who is not sprinting");
    }

    /* --- 3. a full sprint still escapes ----------------------------------- */
    /* The guard. A creature whose AVERAGE beats walking pace is not a scuttler,
       it is a thing you cannot get away from, and layer 2 already has the Wisp
       for the enemy you cannot outrun. */
    {
        const Run r = flee(w, 60, PLAYER_TOP, 900);
        printf("against a full sprint: gap %.0f -> %.0f\n",
               r.startGap, r.endGap);
        check(r.endGap > r.startGap,
              "running flat out still opens the gap");
    }

    /* --- 4. the cadence survives ------------------------------------------ */
    /* Faster must not quietly mean "constant". The pause is what makes the
       burst readable, and a creature that never stops is a different creature
       with the same name.

       Measured against a SPRINTING player, so the creature is chasing at full
       effort for the whole run and never arrives. Read against a standing
       target instead -- which is what this did first -- the useful window is
       only the hundred-odd frames before contact, and everything after it is
       the creature oscillating through a stationary body. */
    {
        const Run r = flee(w, 60, PLAYER_TOP, 900);
        const int pct = r.approachFrames
                      ? (r.stillFrames * 100) / r.approachFrames : 0;
        printf("cadence: %d of %d approach frames not bursting (%d%%)\n",
               r.stillFrames, r.approachFrames, pct);
        check(pct >= 15, "it still stops between bursts");
    }

    if (failures) {
        fprintf(stderr, "\n%d thresher check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
