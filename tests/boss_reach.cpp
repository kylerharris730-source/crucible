/* --- can the boss reach somebody standing above her? ------------------------

   The complaint this measures (#74) is that a rope lets you float over the
   Brood Mother and shoot down at her, because her charge was a ground chase at
   a multiplier: faster along the floor, and never off it. A player two body
   lengths up was simply in a place she had no move that could reach.

   The harness is deliberately the crudest possible version of that fight: a
   flat stone floor, the boss standing on it, and a player pinned in the air
   directly overhead who never moves. Nothing about it is a real encounter -- it
   is the ONE geometric question the fix has to answer, isolated from the rest.

   Two numbers come out of it, and both matter:

     - the closest she ever got, which is the actual complaint. Against the old
       ground chase this could never fall below the height of the platform,
       because nothing in her repertoire left the floor.
     - how far she travelled, which is the OTHER complaint. A boss that is
       wedged also reports a small closest-distance if she happened to spawn
       near you, so "did she reach" is not a test on its own -- being stuck and
       being adjacent look identical from one measurement.

   Compile with every src/*.cpp except main.cpp. It never opens a socket or a
   window, so it will not raise a firewall prompt. Do not name the output
   *_test.exe -- build.bat deletes those. */

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
    entReset();

    World& w = g_testWorld;
    w.reset();

    /* A floor, and a roof far enough up that a dash has somewhere to go. Stone
       rather than anything harder: the dash is supposed to plough through rock,
       and a floor she cannot bite would be testing the wrong thing. */
    const int floorY = 700;
    for (int x = PLAY_X0; x <= PLAY_X1; ++x)
        for (int y = floorY; y <= floorY + 40 && y <= PLAY_Y1; ++y)
            w.setCell(x, y, MAT_STONE);

    /* The player: overhead, and pinned. A rope is not modelled because the rope
       is not the mechanism -- what beat her was being ABOVE her, and a body
       that simply never falls is a stricter version of the same thing. */
    Player& p = g_player;
    p.x = 1000.0f;
    p.y = (float)(floorY - 70);
    p.alive = true;
    p.hp = PLAYER_HP_MAX;

    const int boss = entSpawn(w, ENT_BROOD, 1000.0f, (float)(floorY - 13));
    if (boss < 0) { fprintf(stderr, "could not place the boss\n"); return 2; }

    float closest = 1e30f;
    float travelled = 0.0f;
    float highest = 0.0f;             /* cells above the floor she reached */
    float prevX = g_entities[boss].x, prevY = g_entities[boss].y;
    int   dashFrames = 0;

    for (int frame = 0; frame < 900; ++frame) {
        /* Pinned every frame: contact damage would otherwise kill the player,
           and a dead player stops being a target. This is a geometry test, not
           a survivability one. */
        p.x = 1000.0f;
        p.y = (float)(floorY - 70);
        p.alive = true;
        p.hp = PLAYER_HP_MAX;

        entTick(w, p, g_inv);

        const Entity& e = g_entities[boss];
        if (e.type == ENT_NONE) { fprintf(stderr, "the boss stopped existing\n"); return 3; }

        const float dx = e.centreX() - p.centreX();
        const float dy = e.centreY() - p.centreY();
        const float d  = sqrtf(dx * dx + dy * dy);
        if (d < closest) closest = d;

        const float up = (float)floorY - e.centreY();
        if (up > highest) highest = up;

        travelled += fabsf(e.x - prevX) + fabsf(e.y - prevY);
        prevX = e.x; prevY = e.y;
        if (e.weightless) ++dashFrames;
    }

    printf("closest approach   %.1f cells\n", closest);
    printf("highest above floor %.1f cells\n", highest);
    printf("distance travelled %.1f cells\n", travelled);
    printf("frames airborne    %d\n", dashFrames);

    /* --- what would have to be true to call this fixed ---------------------
       The player's centre sits 70 cells over the floor and the boss is 24 tall,
       so a creature standing on the ground has its centre 58 cells short. The
       old behaviour was not quite zero, because the ground chase does contain
       one hop: it sets vy to -2.6 against an ENT_GRAVITY of 0.18, which peaks
       2.6*2.6/(2*0.18) = 18.8 cells up and closes the gap to about 39 at the
       apex of a perfectly timed jump.

       So the bar is 20 rather than 40. Anything under it cannot be reached by
       hopping at all, and is only explicable by a move that leaves the floor
       plane deliberately. 40 would have been passed by the BROKEN version,
       which is the whole trap in writing a threshold against an argument
       instead of against the arithmetic. */
    if (closest > 20.0f) {
        fprintf(stderr, "FAIL: never got closer than %.1f cells -- "
                        "a player above her is still unreachable\n", closest);
        return 4;
    }
    /* And she has to have MOVED to do it. Being stuck next to the target is not
       the same as reaching it, and the two are indistinguishable from the
       distance alone. */
    if (travelled < 200.0f) {
        fprintf(stderr, "FAIL: only travelled %.1f cells in 900 frames -- wedged\n",
                travelled);
        return 5;
    }
    if (dashFrames <= 0) {
        fprintf(stderr, "FAIL: never went airborne; the dash did not fire\n");
        return 6;
    }
    printf("PASS\n");
    return 0;
}
