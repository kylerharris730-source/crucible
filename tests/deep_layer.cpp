/* --- layer 3 is a place now --------------------------------------------------

   Asked for: "lets do layer 3 materials and hazards first... lets make the
   zone 3 enemies hot and almost hell themed, it just makes sense."

   ROADMAP called this the single biggest content hole: worldgen labelled
   ZONE_LAYER3 and put tungsten in it, and nothing else about standing there
   differed from layer 2. Four materials answer it, and what they have in common
   is that every one of them works through the THERMAL model rather than adding
   a system beside it -- layers 1 and 2 are about what is hunting you, and the
   deep is about the rock itself being hostile.

   Seven properties. The first three are that the layer exists at all, and the
   last four are the ones that stop it being a hazard nobody can survive or a
   fire that eats the world:

     the deep generates brimstone, and a lot of it   (it is a different place)
     it is WARM without anything being lit           (the thermal identity)
     and it is only down there                       (layers 1-2 are unchanged)
     brimstone catches from lava, not from a torch   (the gap is the mechanic)
     a burning seam burns OUT, into ash              (bounded, and visibly spent)
     a fumarole erupts, and keeps erupting           (the sleeping-chunk trap)
     but the fire it makes does not run away         (it is a hazard, not a fuse)

   The sixth is the one with history. Every emitter and reaction in world.cpp
   has to dirty its own cell or a settled chunk is never handed back to
   updateCell -- the acid rule documents it at length, and the material decay
   rule was caught by it after the fact. A vent that erupted once when the world
   was generated and never again would look exactly like a vent that works.

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "worldgen.h"
#include "item.h"
#include "sprite.h"
#include "player.h"
#include "multiplayer.h"
#include <stdio.h>

static World g_testWorld;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

/* Counts a material across a depth band, sampled rather than exhaustive: the
   world is 4096 x 9216 and this runs four times. Every fourth column and row is
   plenty to tell "thousands" from "none", which is the only distinction any of
   these checks needs. */
/* EXACT, for the local scenes. countBand samples one cell in sixteen, which is
   right for "is the deep made of this" across a 4096 x 9216 world and hopeless
   for a fire in a forty-cell box -- a nine-cell ignition reads as either zero or
   one there, and the difference between fizzling and spreading disappears. */
static int countBox(const World& w, u8 mat, int cx, int cy, int r) {
    int n = 0;
    for (int y = cy - r; y <= cy + r; ++y)
        for (int x = cx - r; x <= cx + r; ++x)
            if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1 &&
                w.at(x, y).mat == mat) ++n;
    return n;
}

static int countBand(const World& w, u8 mat, int y0, int y1) {
    int n = 0;
    for (int y = imax(PLAY_Y0, y0); y <= imin(PLAY_Y1, y1); y += 4)
        for (int x = PLAY_X0 + 1; x < PLAY_X1; x += 4)
            if (w.at(x, y).mat == mat) ++n;
    return n;
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();
    World& w = g_testWorld;

    /* --- 1, 2 & 3. the deep generates, and only the deep ------------------ */
    int deepBrim = 0;
    {
        generateWorld(w);

        /* The bands, taken off one representative column's stone line. The
           layers undulate with the terrain, so this is approximate by
           construction -- which is fine for "thousands here, none there" and
           would not be for anything finer. */
        const int stone = g_stoneY[SIM_W / 2];
        const int l3top = stone + LAYER2_DEPTH + STRATUM_THICK;

        deepBrim = countBand(w, MAT_BRIMSTONE, l3top, PLAY_Y1);
        const int shallowBrim = countBand(w, MAT_BRIMSTONE, PLAY_Y0, stone + LAYER2_DEPTH);
        const int vents = countBand(w, MAT_FUMAROLE, l3top, PLAY_Y1);
        printf("brimstone: %d sampled cells below the layer 2 seal, %d above\n",
               deepBrim, shallowBrim);
        printf("fumaroles: %d sampled cells in the deep\n", vents);

        check(deepBrim > 500, "the deep is made of brimstone, not just seeded with it");
        check(shallowBrim == 0, "and there is none of it above the second seal");
        /* Vents are sparse by design and the count sampled at one in sixteen,
           so this asks only that they exist at all. */
        check(vents > 0, "the deep has vents in it");
    }

    /* --- and it is warm, before anything is lit --------------------------- */
    {
        const int stone = g_stoneY[SIM_W / 2];
        const int l3top = stone + LAYER2_DEPTH + STRATUM_THICK;
        /* Averaged over brimstone cells found in the deep, against ambient.
           The point is not that any one cell is hot -- lava would do that --
           but that the ROCK is, everywhere, with nothing burning. */
        long long sum = 0; int n = 0;
        for (int y = l3top; y <= imin(PLAY_Y1, l3top + 3000) && n < 4000; y += 3)
            for (int x = PLAY_X0 + 1; x < PLAY_X1 && n < 4000; x += 7)
                if (w.at(x, y).mat == MAT_BRIMSTONE) {
                    sum += (int)w.temp[y * SIM_W + x]; ++n;
                }
        const int avg = n ? (int)(sum / n) : 0;
        printf("brimstone temperature: %d C over %d cells (ambient is %d C)\n",
               avg - 40, n, (int)AMBIENT_TEMP - 40);
        check(n > 0 && avg > (int)AMBIENT_TEMP + 20,
              "and the rock down there is warm on its own");
    }

    /* --- and it is somewhere a player can still go ----------------------- */
    /* The guard on all of the above. Brimstone sits at 58 C and the character
       starts taking heat damage at 45, so "the deep is warm" is one tuning
       nudge away from "the deep cannot be entered without gear you can only
       make by going there". This asserts the layer is HOSTILE, not sealed.

       What it measures is open cave air, because that is where a player walks.
       The rock being warmer than the threshold is fine and intended -- you feel
       it when you cut into a wall or stand by a vent, which is the point. */
    {
        const int stone = g_stoneY[SIM_W / 2];
        const int l3top = stone + LAYER2_DEPTH + STRATUM_THICK + 200;
        int fx = -1, fy = -1;
        for (int y = l3top; y < l3top + 2500 && fx < 0; y += 3)
            for (int x = PLAY_X0 + 200; x < PLAY_X1 - 200; x += 7) {
                bool air = true;
                for (int k = 0; k < PLAYER_H && air; ++k)
                    air = w.at(x, y - k).mat == MAT_EMPTY;
                if (!air || w.at(x, y + 1).mat == MAT_EMPTY) continue;
                fx = x; fy = y; break;
            }
        if (fx < 0) { fprintf(stderr, "no layer 3 cave to stand in\n"); return 2; }
        w.setLiveWindow(fx - 300, fy - 300, fx + 300, fy + 300);
        Player& p = g_player;
        p.reset((float)fx, (float)(fy - PLAYER_H));
        p.alive = true; p.hp = PLAYER_HP_MAX;
        PlayerInput in;
        in.left = false; in.right = false; in.jump = false; in.down = false;
        for (int f = 0; f < 600; ++f) { p.update(w, in); w.step(); }
        printf("ten seconds standing in a layer 3 cave: %d of %d hp\n",
               p.hp, (int)PLAYER_HP_MAX);
        check(p.alive && p.hp > PLAYER_HP_MAX / 2,
              "the deep can be walked into without heat gear");
    }

    /* --- and touching it DOES hurt -------------------------------------- */
    /* Reported from play: "touching brimstone actually didnt hurt."

       It did not, and the cause was not the material. bodyTemp scanned the
       cells the collision box OVERLAPS plus a special row under the feet --
       added, its own note says, because "the body scan cannot see the thing the
       player is standing on". The identical hole sideways had never been
       noticed because until now nothing in the game was a hot WALL: lava and
       fire are things you stand in, so they always overlapped the box.

       Pressed against a warm wall in still air, with nothing burning. */
    {
        const int CX = 1400, CY = 5000;
        w.reset();
        for (int y = CY - 40; y <= CY + 40; ++y)
            for (int x = CX - 40; x <= CX + 40; ++x)
                w.setCell(x, y, MAT_BRIMSTONE);
        /* A slot just wide enough to stand in, against the right-hand face, and
           TALLER than the character -- a first version carved twenty rows for a
           thirty-cell body, so the placement was inside solid rock and the
           collision resolver ejected the player up and out over the top of the
           block, where they walked away across open air measuring nothing. */
        for (int y = CY - PLAYER_H - 4; y <= CY; ++y)
            for (int x = CX - PLAYER_W; x < CX; ++x)
                w.setCell(x, y, MAT_EMPTY);
        w.setLiveWindow(CX - 60, CY - 60, CX + 60, CY + 60);

        Player& p = g_player;
        /* reset() takes the CENTRE, not the corner -- which is worth stating
           because passing a corner puts the body half its height too high and
           the failure looks like the material rather than the placement. */
        p.reset((float)CX - PLAYER_W * 0.5f, (float)CY - PLAYER_H * 0.5f);
        p.alive = true; p.hp = PLAYER_HP_MAX;
        PlayerInput in;
        in.left = false; in.right = true; in.jump = false; in.down = false;
        int before = p.hp;
        for (int f = 0; f < 600; ++f) { p.update(w, in); w.step(); }
        printf("ten seconds pressed against a brimstone wall: %d -> %d hp\n",
               before, p.hp);
        check(p.hp < before, "a hot wall hurts to lean on");
    }

    /* --- 4. it catches from lava, and not from a torch -------------------- */
    /* The gap between the two is the whole mechanic. Too low and layer 3 is
       permanently alight; too high and nothing ever happens. */
    {
        const int CX = 1400, CY = 5000;
        w.reset();
        for (int y = CY - 40; y <= CY + 40; ++y)
            for (int x = CX - 40; x <= CX + 40; ++x)
                w.setCell(x, y, MAT_BRIMSTONE);
        w.setLiveWindow(CX - 60, CY - 60, CX + 60, CY + 60);
        /* A torch against one face. */
        w.setCell(CX - 20, CY, MAT_TORCH);
        for (int f = 0; f < 900; ++f) w.step();
        const int fromTorch = countBox(w, MAT_BRIMFIRE, CX, CY, 40);

        w.reset();
        for (int y = CY - 40; y <= CY + 40; ++y)
            for (int x = CX - 40; x <= CX + 40; ++x)
                w.setCell(x, y, MAT_BRIMSTONE);
        w.setLiveWindow(CX - 60, CY - 60, CX + 60, CY + 60);
        for (int y = CY - 2; y <= CY + 2; ++y)
            for (int x = CX - 2; x <= CX + 2; ++x) w.setCell(x, y, MAT_LAVA);
        int fromLava = 0;
        for (int f = 0; f < 900; ++f) {
            w.step();
            const int n = countBox(w, MAT_BRIMFIRE, CX, CY, 40);
            if (n > fromLava) fromLava = n;
        }
        printf("brimstone: %d sampled cells alight from a torch, %d from lava\n",
               fromTorch, fromLava);
        check(fromTorch == 0, "a torch does not set the walls of layer 3 alight");
        check(fromLava > 0, "lava does");
    }

    /* --- 5 & 7. a seam burns out, into ash, without eating the world ------ */
    {
        const int CX = 1400, CY = 5000;
        w.reset();
        /* A seam with a stone jacket round it, so what is measured is the fire
           stopping on its own and not running out of brimstone to burn. */
        for (int y = CY - 60; y <= CY + 60; ++y)
            for (int x = CX - 60; x <= CX + 60; ++x)
                w.setCell(x, y, MAT_STONE);
        for (int y = CY - 20; y <= CY + 20; ++y)
            for (int x = CX - 20; x <= CX + 20; ++x)
                w.setCell(x, y, MAT_BRIMSTONE);
        w.setLiveWindow(CX - 80, CY - 80, CX + 80, CY + 80);
        const int seam = countBox(w, MAT_BRIMSTONE, CX, CY, 20);

        /* Light one corner by HEATING it past the ignition point, which is how
           the game lights it -- the ignite path in updateCell converts the cell
           and then sets the product at least as hot as its spawnTemp, so the
           fire starts hot enough to take its neighbours with it.

           Placing MAT_BRIMFIRE cells directly is what a first version did, and
           it does not work for a reason worth recording: setCell does not apply
           spawnTemp, so the cells arrived at ambient, which is below brimfire's
           own coolTemp, and every one of them turned to ash on the first frame
           without ever heating anything. The measurement said fire does not
           spread; what it had actually built was a seam of cold ash.

           205 rather than a comfortably-past-it 240, because degC() OVERFLOWS:
           a cell temperature is one byte and the scale runs to 215 C, which is
           why lava sits at degC(215) and its own note calls that two below the
           ceiling. degC(240) wraps to a value below ambient, so the second
           version of this chilled the corner it meant to light and measured
           nothing igniting at all.

           And lit in the MIDDLE of the seam rather than at its corner, which
           is where the third version put it and is the worst spot on the whole
           block: a corner cell has cold stone on two of its four sides, the
           jacket sinks the heat straight out of it, and the nine cells burned
           to ash without taking anything with them. That is correct behaviour
           and a useless measurement -- it says a fire against a heat sink goes
           out, not whether a seam carries one. */
        for (int y = CY - 1; y <= CY + 1; ++y)
            for (int x = CX - 1; x <= CX + 1; ++x)
                w.temp[y * SIM_W + x] = degC(205);
        w.dirtyPoint(CX, CY);

        int peak = 0;
        for (int f = 0; f < 4000; ++f) {
            w.step();
            const int n = countBox(w, MAT_BRIMFIRE, CX, CY, 60);
            if (n > peak) peak = n;
        }
        const int left = countBox(w, MAT_BRIMFIRE, CX, CY, 60);
        const int ash  = countBox(w, MAT_ASH, CX, CY, 60);
        printf("a lit seam of %d cells: peaked at %d alight, %d still burning after "
               "4000 frames, %d ash\n", seam, peak, left, ash);
        check(peak > 40, "fire spreads through a brimstone seam");
        check(left < peak, "and burns out rather than staying lit forever");
        check(ash > 0, "leaving ash, so a spent seam is visibly spent");
    }

    /* --- 6. a vent keeps venting ----------------------------------------- */
    /* The sleeping-chunk trap, which every emitter in world.cpp has to dodge
       and which the material decay rule did not. A vent that fires once and
       then never again looks exactly like one that works. */
    {
        const int CX = 1400, CY = 5000;
        w.reset();
        for (int y = CY; y <= CY + 40; ++y)
            for (int x = CX - 40; x <= CX + 40; ++x)
                w.setCell(x, y, MAT_STONE);
        w.setCell(CX, CY, MAT_FUMAROLE);
        w.setLiveWindow(CX - 60, CY - 60, CX + 60, CY + 60);

        int early = 0, late = 0;
        for (int f = 0; f < 600; ++f) {
            w.step();
            if (w.at(CX, CY - 1).mat == MAT_FIRE ||
                w.at(CX, CY - 2).mat == MAT_FIRE) ++early;
        }
        /* A long quiet stretch, then look again. This is where a vent that
           depended on its chunk staying awake for another reason would have
           gone silent. */
        for (int f = 0; f < 3000; ++f) w.step();
        for (int f = 0; f < 600; ++f) {
            w.step();
            if (w.at(CX, CY - 1).mat == MAT_FIRE ||
                w.at(CX, CY - 2).mat == MAT_FIRE) ++late;
        }
        printf("vent: fire above it on %d of the first 600 frames, %d of 600 "
               "more after a 3000-frame wait\n", early, late);
        check(early > 0, "a fumarole erupts");
        check(late > 0, "and is still erupting long after its chunk settled");
    }

    if (failures) {
        fprintf(stderr, "\n%d deep layer check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
