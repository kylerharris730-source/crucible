/* --- the crash dummy: stands there, runs from anything that would hurt YOU ---

   Asked for as a test rig: a body the size of the character's that cannot die,
   stands where it is put, and bolts from pain, heat and cold -- overshooting a
   little, "like it's actually running from the fire".

   The point of the thing is that it is HONEST about hazards, so that is what
   most of this checks. It reads the same two sources Player::update does --
   the HEAT_HURT_AT / COLD_HURT_AT lines, and g_matContactDamage -- so a hazard
   it flinches from is one that hurts you and a hazard it ignores does not. A
   dummy that fled something harmless, or stood in something lethal, would be
   worse than no dummy at all: you would learn the wrong thing about the room.

   Seven properties, and the two direction checks point OPPOSITE ways on
   purpose -- a dummy that simply always bolted one way would pass either one
   alone:

     it stands still when nothing is wrong        (it is a rig, not a wanderer)
     it runs from heat, and runs the RIGHT WAY    (away, not merely somewhere)
     it runs from a contact hazard at room temp   (acid is not a heat problem)
     it keeps running after the danger is gone    (the overshoot)
     it cannot die                                (100 hits of 9999)
     it bolts from a poke with no hazard present  (pain, away from the poker)
     it does no contact damage                    (a target, not an enemy)

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

static void fill(World& w, int x0, int y0, int x1, int y1, u8 mat) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1)
                w.setCell(x, y, mat);
}

static const int CX = 1400, CY = 5000;
static const int FLOOR = CY + 40;

/* A fresh room every time, so one experiment cannot inherit the last one's
   heat, acid, or panic timer. */
static int setup(World& w) {
    w.reset();
    fill(w, CX - 300, CY - 60, CX + 300, CY + 120, MAT_STONE);
    fill(w, CX - 260, CY, CX + 260, FLOOR, MAT_EMPTY);
    w.setLiveWindow(CX - 280, CY - 20, CX + 280, CY + 80);

    Player& p = g_player;
    p.reset((float)(CX - 200), (float)(FLOOR - PLAYER_H));
    p.alive = true; p.hp = PLAYER_HP_MAX;

    entReset();
    return entSpawn(w, ENT_DUMMY, (float)CX, (float)(FLOOR - 20));
}

static void run(World& w, int frames) {
    Player& p = g_player;
    for (int f = 0; f < frames; ++f) {
        p.x = (float)(CX - 200); p.y = (float)(FLOOR - PLAYER_H);
        p.alive = true; p.hp = PLAYER_HP_MAX;
        entTick(w, p, g_inv);
    }
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();
    World& w = g_testWorld;
    int failures = 0;

    /* --- 1. it stands where you put it ---------------------------------- */
    {
        const int d = setup(w);
        if (d < 0) { fprintf(stderr, "could not place the dummy\n"); return 2; }
        const float x0 = g_entities[d].centreX();
        run(w, 600);
        const float moved = fabsf(g_entities[d].centreX() - x0);
        printf("idle: drifted %.1f cells in 600 frames\n", moved);
        if (moved > 4.0f) {
            fprintf(stderr, "FAIL: it wandered %.1f cells with nothing wrong -- "
                            "a rig that will not stay put cannot be used to test "
                            "anything\n", moved);
            ++failures;
        }
    }

    /* --- 2. it runs from heat, and away rather than merely somewhere ----- */
    {
        const int d = setup(w);
        const float x0 = g_entities[d].centreX();
        /* Hot cells to the RIGHT. Written into the temperature field rather
           than as lava, so this tests the heat line specifically and not a
           material that happens to be both hot and dangerous to touch. */
        for (int y = CY + 10; y <= FLOOR; ++y)
            for (int x = CX + 30; x <= CX + 90; ++x)
                w.temp[y * SIM_W + x] = degC(200);
        run(w, 240);
        const float dx = g_entities[d].centreX() - x0;
        printf("heat: moved %+.1f cells (hazard was to the right)\n", dx);
        if (dx > -20.0f) {
            fprintf(stderr, "FAIL: fire on its right and it moved %+.1f -- it is "
                            "not fleeing heat\n", dx);
            ++failures;
        }
    }

    /* --- 3. and from a contact hazard at room temperature ---------------- */
    {
        const int d = setup(w);
        const float x0 = g_entities[d].centreX();
        /* Acid to the LEFT this time, so a dummy that simply always runs one
           way cannot pass both this and the previous check. */
        fill(w, CX - 90, FLOOR - 3, CX - 30, FLOOR, MAT_ACID);
        run(w, 240);
        const float dx = g_entities[d].centreX() - x0;
        printf("acid: moved %+.1f cells (hazard was to the left)\n", dx);
        if (dx < 20.0f) {
            fprintf(stderr, "FAIL: acid on its left and it moved %+.1f -- a room-"
                            "temperature hazard is not being read at all\n", dx);
            ++failures;
        }
    }

    /* --- 4. the overshoot ----------------------------------------------- */
    {
        const int d = setup(w);
        for (int y = CY + 10; y <= FLOOR; ++y)
            for (int x = CX + 30; x <= CX + 90; ++x)
                w.temp[y * SIM_W + x] = degC(200);
        run(w, 30);
        /* Danger gone, instantly and completely. Anything it does from here is
           the fright rather than the fire. */
        for (int y = CY; y <= FLOOR + 4; ++y)
            for (int x = CX - 260; x <= CX + 260; ++x)
                w.temp[y * SIM_W + x] = degC(20);
        const float x0 = g_entities[d].centreX();
        run(w, 60);
        const float after = fabsf(g_entities[d].centreX() - x0);
        printf("overshoot: ran a further %.1f cells after the heat was removed\n",
               after);
        if (after < 10.0f) {
            fprintf(stderr, "FAIL: it covered only %.1f cells once safe -- it "
                            "stops the instant the condition clears, which reads "
                            "as a machine rather than a fright\n", after);
            ++failures;
        }
    }

    /* --- 5. it cannot die ------------------------------------------------ */
    {
        const int d = setup(w);
        for (int i = 0; i < 100; ++i) {
            const Entity& e = g_entities[d];
            if (e.type == ENT_NONE) break;
            entDamageAt((int)e.centreX(), (int)e.centreY(), 9999);
            run(w, 1);
        }
        const bool alive = g_entities[d].type == ENT_DUMMY;
        printf("survival: %s after 100 hits of 9999\n",
               alive ? "still standing" : "GONE");
        if (!alive) {
            fprintf(stderr, "FAIL: the dummy died\n");
            ++failures;
        } else if (g_entities[d].hp != ENT_DEFS[ENT_DUMMY].hp) {
            fprintf(stderr, "FAIL: it survived but its health is %d of %d -- the "
                            "restore is not running every frame\n",
                    g_entities[d].hp, ENT_DEFS[ENT_DUMMY].hp);
            ++failures;
        }
    }

    /* --- 6. it runs from PAIN, not only from hazards --------------------- */
    {
        const int d = setup(w);
        const float x0 = g_entities[d].centreX();
        /* Nothing dangerous in the room at all -- this is a poke. The player is
           parked to its LEFT, and nothing records who dealt the damage, so
           "away from the character" is the whole rule being checked here. */
        const Entity& e = g_entities[d];
        entDamageAt((int)e.centreX(), (int)e.centreY(), 5);
        run(w, 90);
        const float dx = g_entities[d].centreX() - x0;
        printf("pain: moved %+.1f cells after one poke (player to the left)\n", dx);
        if (dx < 15.0f) {
            fprintf(stderr, "FAIL: poked and it moved %+.1f -- it should bolt "
                            "away from whoever hit it\n", dx);
            ++failures;
        }
    }

    /* --- 7. and it never hurts you --------------------------------------- */
    if (ENT_DEFS[ENT_DUMMY].touchDamage != 0) {
        fprintf(stderr, "FAIL: the dummy does %d contact damage -- it is a "
                        "target, not an enemy\n", ENT_DEFS[ENT_DUMMY].touchDamage);
        ++failures;
    }

    if (failures) {
        fprintf(stderr, "\n%d crash dummy check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
