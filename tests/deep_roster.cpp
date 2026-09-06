/* --- layer 3 has inhabitants -------------------------------------------------

   Asked for: "lets have more fast enemies, still quite a few that shoot, lets
   also have an enemy thats like the bat, those are annoying and scary. when i
   say like the bat i mean that fast persistent pathfinding. lets make the zone
   3 enemies hot and almost hell themed."

   The roster is four, and the risk with four new creatures is not that any one
   of them is broken -- it is that they are RESKINS. The game already has a fast
   flier, a planted shooter, a chaser and a trail-layer, so each of these has to
   be measurably different from the thing it most resembles or it is content by
   the yard.

   So the checks below are mostly comparisons against the existing creature each
   one would otherwise be:

     all four spawn in layer 3, and nothing else does   (the hole is filled)
     and all four survive their own layer               (58 C rock, hot vents)
     the Ashhound outruns a Husk and never stops        (fast, persistent)
     the Emberwing ARRIVES where the Bat overshoots     (the request, exactly)
     and is faster than the Wisp, which also arrives    (not a slow Wisp)
     the Slagmaw leaves fire where its shots land       (it denies ground)
     the Cinderling sets the floor alight as it runs    (the trail is the threat)

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

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static void fill(World& w, int x0, int y0, int x1, int y1, u8 m) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1)
                w.setCell(x, y, m);
}

/* A long flat hall with the player at the left end. `back` is floor BEHIND
   them, and it has to exceed anything a fleeing player can cover: at 40 cells
   the retreat below ran off the end of the world, the pursuer could not follow
   onto ground that was not there, and the harness reported both the Ashhound
   and a Husk losing 467 cells -- which measures the length of the hall. */
static void hall(World& w, int len, int back = 40) {
    w.reset();
    projClear();
    fill(w, CX - back - 20, CY - 80, CX + len + 80, FLOOR + 40, MAT_STONE);
    fill(w, CX - back,      CY - 60, CX + len + 60, FLOOR,      MAT_EMPTY);
    w.setLiveWindow(CX - back - 40, CY - 100, CX + len + 100, FLOOR + 60);
    Player& p = g_player;
    p.reset((float)CX, (float)(FLOOR - PLAYER_H / 2));
    p.alive = true; p.hp = PLAYER_HP_MAX;
    entReset();
}

/* Runs a creature against a player fleeing at `flee` cells a frame and reports
   how much of the gap it took. Positive means it closed. */
static float closed(World& w, int type, int gap, float flee, int frames) {
    hall(w, gap + 400, 1400);
    const int e = entSpawn(w, (u8)type, (float)(CX + gap), (float)(FLOOR - 12));
    if (e < 0) return -1e9f;
    Player& p = g_player;
    const float start = g_entities[e].centreX() - p.centreX();
    for (int f = 0; f < frames; ++f) {
        p.x -= flee;
        p.y = (float)(FLOOR - PLAYER_H);
        p.alive = true; p.hp = PLAYER_HP_MAX;
        entTick(w, p, g_inv);
        projUpdate(w);
    }
    return start - (g_entities[e].centreX() - p.centreX());
}

static int countMat(const World& w, u8 m, int x0, int x1) {
    int n = 0;
    for (int y = CY - 60; y <= FLOOR + 4; ++y)
        for (int x = x0; x <= x1; ++x)
            if (x > PLAY_X0 && x < PLAY_X1 && w.at(x, y).mat == m) ++n;
    return n;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();
    World& w = g_testWorld;

    const int L3 = 1 << 2;   /* caveLayerOf(ZONE_LAYER3) is 2 */
    const int deep[4] = { ENT_ASHHOUND, ENT_EMBERWING, ENT_SLAGMAW, ENT_CINDERLING };

    /* --- 1 & 2. the layer is populated, by things that can live in it ----- */
    {
        int inLayer3 = 0;
        for (int t = ENT_NONE + 1; t < ENT_COUNT; ++t)
            if (ENT_DEFS[t].layerMask & L3) ++inLayer3;
        printf("creatures that spawn in layer 3: %d\n", inLayer3);
        check(inLayer3 >= 4, "the deep has a roster at all");

        /* Every one of them has to survive its own layer. Brimstone alone runs
           at 58 C and the default cook threshold is 60, so a creature left on
           the default would die in the rock it lives in -- a bug that shows up
           as an empty layer, not as an error. */
        bool tough = true;
        for (int k = 0; k < 4; ++k) {
            const EntityDef& d = ENT_DEFS[deep[k]];
            if (d.heatTolerance <= 60) { printf("  %s cooks at %u C\n",
                                                d.name, (unsigned)d.heatTolerance);
                                         tough = false; }
            if (!(d.layerMask & L3)) { printf("  %s is not in layer 3\n", d.name);
                                       tough = false; }
        }
        check(tough, "and every one of them can survive where it lives");
    }

    /* --- 3. the Ashhound runs you down ----------------------------------- */
    /* Against the Husk, which is the layer 1 chaser and the thing this would
       otherwise be a bigger version of. Both are pursuers with no ranged
       attack; the difference has to be measurable or there is no new creature
       here. Fleeing at two thirds of a sprint -- the pace of somebody fighting
       rather than running for the exit. */
    {
        const float hound = closed(w, ENT_ASHHOUND, 220, 0.8f, 900);
        const float husk  = closed(w, ENT_HUSK,     220, 0.8f, 900);
        printf("against a 0.8 retreat over 900 frames: Ashhound closed %+.0f "
               "cells, Husk %+.0f\n", hound, husk);
        check(hound > 0.0f, "the Ashhound closes on a retreating player");
        check(hound > husk * 2.0f, "and does it far faster than a Husk");
    }

    /* --- 4 & 5. the Emberwing arrives ------------------------------------ */
    /* THE request, and it is a comparison rather than a number. The Bat is fast
       and cannot steer -- it commits to a heading and sails past. The Wisp
       steers and is slow. This has to beat both at the thing each of them is
       bad at, or it is one of them with a new sprite.

       Measured against a player who keeps MOVING, because a committed flier
       looks perfect against a stationary one -- it only misses what has gone
       somewhere since it aimed. */
    {
        /* DWELL, not closest approach. A first version measured the nearest
           the creature ever got, and it cannot tell these apart: the Bat
           reaches the player too -- once, on its way past. Overshooting is a
           statement about how much of the time it is a threat, so what
           separates them is the number of frames spent inside contact range.
           `best` is kept only for the report. */
        struct Flier { const char* name; int type; float best; int nearFrames; };
        Flier f[3] = { { "Emberwing", ENT_EMBERWING, 1e9f, 0 },
                       { "Bat",       ENT_BAT,       1e9f, 0 },
                       { "Wisp",      ENT_WISP,      1e9f, 0 } };
        const float NEAR = 20.0f;
        for (int k = 0; k < 3; ++k) {
            hall(w, 400);
            const int e = entSpawn(w, (u8)f[k].type, (float)(CX + 200),
                                   (float)(CY - 20));
            if (e < 0) { fprintf(stderr, "could not place %s\n", f[k].name); return 2; }
            Player& p = g_player;
            for (int t = 0; t < 900; ++t) {
                /* A player who keeps moving, which is what a committed flier
                   misses and a routing one does not. */
                p.x = (float)CX + 40.0f * sinf((float)t * 0.02f);
                p.y = (float)(FLOOR - PLAYER_H);
                p.alive = true; p.hp = PLAYER_HP_MAX;
                entTick(w, p, g_inv);
                const float dx = g_entities[e].centreX() - p.centreX();
                const float dy = g_entities[e].centreY() - p.centreY();
                const float d = sqrtf(dx * dx + dy * dy);
                if (d < f[k].best) f[k].best = d;
                if (d < NEAR) ++f[k].nearFrames;
            }
            printf("  %-10s closest %.0f cells, within %.0f for %d of 900 frames\n",
                   f[k].name, f[k].best, NEAR, f[k].nearFrames);
        }
        check(f[0].nearFrames > f[1].nearFrames * 2,
              "the Emberwing stays on a moving player where the Bat sails past");
        check(f[0].nearFrames > 200, "and is on them for much of the fight");

        /* And it is faster than the Wisp, the other flier that arrives. */
        printf("speeds: Emberwing %.2f, Bat %.2f, Wisp %.2f\n",
               ENT_DEFS[ENT_EMBERWING].speed, ENT_DEFS[ENT_BAT].speed,
               ENT_DEFS[ENT_WISP].speed);
        check(ENT_DEFS[ENT_EMBERWING].speed > ENT_DEFS[ENT_WISP].speed &&
              ENT_DEFS[ENT_EMBERWING].speed > ENT_DEFS[ENT_BAT].speed,
              "and is faster than either flier it is not");
    }

    /* --- 6. the Slagmaw leaves fire where it shoots ----------------------- */
    /* The one field that separates it from the Culverin: the glob carries fire
       as its payload, so a miss denies ground rather than being a miss. */
    {
        hall(w, 300);
        const int e = entSpawn(w, ENT_SLAGMAW, (float)(CX + 90), (float)(FLOOR - 14));
        if (e < 0) return 2;
        Player& p = g_player;
        int fire = 0;
        for (int t = 0; t < 900; ++t) {
            p.x = (float)CX; p.y = (float)(FLOOR - PLAYER_H);
            p.alive = true; p.hp = PLAYER_HP_MAX;
            entTick(w, p, g_inv);
            projUpdate(w);
            const int n = countMat(w, MAT_FIRE, CX - 40, CX + 140);
            if (n > fire) fire = n;
        }
        printf("Slagmaw over 900 frames: %d cells of fire on the ground at peak\n",
               fire);
        check(fire > 0, "a Slagmaw's shots leave fire where they land");
    }

    /* --- and their fire does NOT set the layer alight --------------------- */
    /* Written expecting the opposite, and the measurement said no: a Slagmaw
       firing onto a brimstone floor for twelve hundred frames lit not one cell
       of seam. That is the design working rather than failing, and it is worth
       pinning as a property so nobody "fixes" it later.

       Brimstone ignites at 140 C. Its own note says why: "a torch will not set
       a wall off, a thermal lance or a lava neighbour will" -- too low and the
       layer is permanently alight. A single cell of fire is nowhere near it,
       and it must not be, because the two creatures that leave fire behind are
       both COMMON. Cinderlings running the corridors of a layer whose walls
       catch from open flame would burn the whole place down within minutes of
       generation, unattended, before the player ever arrived.

       So the fuel is real and the fuse is not: what lights a seam is lava, or a
       tool you brought down on purpose. The creatures deny ground with fire;
       they do not get to redecorate the layer with it. */
    {
        hall(w, 300);
        fill(w, CX - 40, FLOOR - 2, CX + 300, FLOOR, MAT_BRIMSTONE);
        const int e = entSpawn(w, ENT_SLAGMAW, (float)(CX + 90), (float)(FLOOR - 16));
        if (e < 0) return 2;
        Player& p = g_player;
        int lit = 0;
        for (int t = 0; t < 1200; ++t) {
            p.x = (float)CX; p.y = (float)(FLOOR - PLAYER_H);
            p.alive = true; p.hp = PLAYER_HP_MAX;
            entTick(w, p, g_inv);
            projUpdate(w);
            w.step();
            const int n = countMat(w, MAT_BRIMFIRE, CX - 40, CX + 300);
            if (n > lit) lit = n;
        }
        printf("1200 frames of Slagmaw fire onto brimstone: %d cells of seam alight\n",
               lit);
        check(lit == 0,
              "creature fire does not set layer 3 alight, or it would burn unattended");
    }

    /* --- 7. the Cinderling sets the floor alight -------------------------- */
    {
        hall(w, 300);
        const int e = entSpawn(w, ENT_CINDERLING, (float)(CX + 200),
                               (float)(FLOOR - 8));
        if (e < 0) return 2;
        Player& p = g_player;
        int fire = 0;
        for (int t = 0; t < 600; ++t) {
            p.x = (float)CX; p.y = (float)(FLOOR - PLAYER_H);
            p.alive = true; p.hp = PLAYER_HP_MAX;
            entTick(w, p, g_inv);
            const int n = countMat(w, MAT_FIRE, CX, CX + 260);
            if (n > fire) fire = n;
        }
        printf("Cinderling over 600 frames: %d cells of fire behind it at peak\n",
               fire);
        check(fire > 0, "a Cinderling burns the ground it runs over");
    }

    if (failures) {
        fprintf(stderr, "\n%d deep roster check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
