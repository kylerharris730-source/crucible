/* --- the Wisp charges, then fires a flat beam --------------------------------

   Asked for: "i want more enemies that shoot at you on the second layer, make
   the purple circle guy shoot a cool beam occasionally."

   The Wisp was the one layer-2 creature with no answer at range, and the one
   that most wanted one -- it never stops coming and it ignores terrain, so
   simply backing away was safe indefinitely.

   Six properties, and the last three are what keep a flat hitscan-ish shot
   fair:

     it fires at all, from range                (the request)
     the shot is FLAT -- no arc                 (a beam, not another glob)
     it charges BEFORE firing, not after        (the tell precedes the shot)
     it will not shoot through rock             (cover works)
     it holds fire point-blank                  (its body is the threat there)
     it does not fire every frame               ("occasionally")

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

static void fill(World& w, int x0, int y0, int x1, int y1, u8 m) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1)
                w.setCell(x, y, m);
}

/* Open room, player on the left, wisp `gap` cells to its right at the same
   height, so a flat beam travels horizontally and any drop is measurable. */
static int setup(World& w, int gap) {
    w.reset();
    projClear();
    fill(w, CX - 260, CY - 120, CX + 260, CY + 120, MAT_STONE);
    fill(w, CX - 250, CY - 110, CX + 250, CY + 110, MAT_EMPTY);
    w.setLiveWindow(CX - 270, CY - 130, CX + 270, CY + 130);

    Player& p = g_player;
    p.reset((float)(CX - 100), (float)CY);
    p.alive = true; p.hp = PLAYER_HP_MAX;

    entReset();
    return entSpawn(w, ENT_WISP, (float)(CX - 100 + gap),
                    (float)(CY + PLAYER_H / 2));
}

/* Runs the creature only -- the world is not stepped, so nothing settles.

   BOTH bodies are pinned. The player obviously, but the wisp too: it is a
   chaser, so left alone it closes to point-blank within one cooldown and stops
   shooting, and a run that measured the interval would really be measuring how
   fast it walks. Pinning makes the cooldown the only variable. */
static int   g_pinned = -1;
static float g_pinX = 0.0f, g_pinY = 0.0f;

static void run(World& w, int frames) {
    Player& p = g_player;
    for (int f = 0; f < frames; ++f) {
        p.x = (float)(CX - 100); p.y = (float)CY;
        p.alive = true; p.hp = PLAYER_HP_MAX;
        if (g_pinned >= 0) {
            g_entities[g_pinned].x = g_pinX;
            g_entities[g_pinned].y = g_pinY;
            g_entities[g_pinned].vx = g_entities[g_pinned].vy = 0.0f;
        }
        entTick(w, p, g_inv);
        projUpdate(w);
    }
}

static void pin(int e) {
    g_pinned = e;
    g_pinX = g_entities[e].x;
    g_pinY = g_entities[e].y;
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

    /* --- 1. it fires, it charges first, and not every frame -------------- */
    {
        const int e = setup(w, 90);
        if (e < 0) { fprintf(stderr, "could not place the wisp\n"); return 2; }

        pin(e);
        int shots = 0, chargedBefore = 0, chargeFrames = 0;
        bool wasCharging = false;
        int lastShotFrame = -9999, minGap = 1 << 30;
        for (int f = 0; f < 900; ++f) {
            const int before = hostileShots();
            run(w, 1);
            const bool charging = wispCharging(g_entities[e]);
            if (charging) ++chargeFrames;
            if (hostileShots() > before) {
                ++shots;
                if (wasCharging) ++chargedBefore;
                if (f - lastShotFrame < minGap) minGap = f - lastShotFrame;
                lastShotFrame = f;
            }
            wasCharging = charging;
            /* Clear the sky so the count reflects new shots, not survivors. */
            projClear();
        }
        printf("900 frames at 90 cells: %d beams, %d preceded by a charge, "
               "%d charging frames, closest pair %d frames apart\n",
               shots, chargedBefore, chargeFrames, minGap);

        if (shots < 2) {
            fprintf(stderr, "FAIL: only %d beams in 900 frames -- it is not "
                            "shooting\n", shots);
            ++failures;
        }
        if (shots && chargedBefore * 2 < shots) {
            fprintf(stderr, "FAIL: only %d of %d beams came after a charge -- the "
                            "wind-up is decoration, not a tell\n",
                    chargedBefore, shots);
            ++failures;
        }
        if (shots >= 2 && minGap < 60) {
            fprintf(stderr, "FAIL: two beams only %d frames apart -- that is not "
                            "'occasionally'\n", minGap);
            ++failures;
        }
        if (chargeFrames == 0) {
            fprintf(stderr, "FAIL: never reported charging at all\n");
            ++failures;
        }
    }

    /* --- 2. the beam is FLAT ---------------------------------------------
       Measured as CONSTANT vy, not as displacement from the muzzle. A beam
       aimed diagonally legitimately changes y, and a first version of this
       measured exactly that and called a correctly-flat shot a 2.33-cell sag.
       What makes it a beam is that nothing is added to vy each frame. */
    {
        const int e = setup(w, 90);
        pin(e);
        float vy0 = 0.0f, worst = 0.0f;
        bool got = false;
        for (int f = 0; f < 900 && !got; ++f) {
            run(w, 1);
            for (int i = 0; i < MAX_PROJ; ++i)
                if (g_proj[i].alive && g_proj[i].hostile) {
                    const int which = i;
                    vy0 = g_proj[which].vy;
                    for (int k = 0; k < 40 && g_proj[which].alive; ++k) {
                        run(w, 1);
                        if (!g_proj[which].alive) break;
                        const float dv = g_proj[which].vy - vy0;
                        if (fabsf(dv) > fabsf(worst)) worst = dv;
                    }
                    got = true;
                    break;
                }
        }
        printf("beam vy drift over 40 frames: %+.4f cells/frame "
               "(gravity would add %.2f)\n", worst, PROJ_GRAVITY * 40.0f);
        if (!got) {
            fprintf(stderr, "FAIL: no beam to measure\n");
            ++failures;
        } else if (fabsf(worst) > 0.001f) {
            fprintf(stderr, "FAIL: vy moved by %.4f -- something is accelerating "
                            "it, so it is a glob and not a beam\n", worst);
            ++failures;
        }
    }

    /* --- 3. rock stops it ------------------------------------------------- */
    {
        const int e = setup(w, 90);
        pin(e);
        /* A wall between the two, thick enough that the sampled sight line
           cannot miss it. */
        fill(w, CX - 60, CY - 110, CX - 40, CY + 110, MAT_STONE);
        int shots = 0;
        for (int f = 0; f < 900; ++f) {
            const int before = hostileShots();
            run(w, 1);
            if (hostileShots() > before) ++shots;
            projClear();
        }
        printf("with a wall between them: %d beams\n", shots);
        if (shots) {
            fprintf(stderr, "FAIL: fired %d times through solid rock -- cover has "
                            "to work against a flat shot\n", shots);
            ++failures;
        }
    }

    /* --- 4. point blank it just closes ------------------------------------ */
    {
        const int e = setup(w, 6);
        pin(e);
        int shots = 0;
        for (int f = 0; f < 600; ++f) {
            const int before = hostileShots();
            run(w, 1);
            if (hostileShots() > before) ++shots;
            projClear();
        }
        printf("at 6 cells: %d beams\n", shots);
        if (shots) {
            fprintf(stderr, "FAIL: fired %d times point-blank -- its body is the "
                            "threat at that range\n", shots);
            ++failures;
        }
    }

    if (failures) {
        fprintf(stderr, "\n%d wisp beam check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
