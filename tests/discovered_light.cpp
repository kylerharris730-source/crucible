/* --- somewhere you have lit stays readable -----------------------------------

   Reported from play: "everyone hates how much light gets blocked, so, current
   darkness is reserved to undiscovered space, but once its discovered, max
   darkness is way way brighter. when i draw a blob of metal, it should look
   like a blob of metal, not a black blob."

   LIGHT_MIN_SHADE's note is a record of this complaint being answered twice
   with the same lever and rejected both times -- the floor went to 10%, to 16%,
   and back to 6%. It kept failing because that one number does two jobs: raise
   it and unexplored rock becomes readable too, so the ore in a wall and the
   shape of a cave are given away before you have lit anything.

   Splitting the two is what lets the bright version work, so the properties
   below come in pairs. Every one about explored space being brighter has a twin
   about unexplored space being exactly as dark as it was.

     a lit place is drawn far brighter than before   (the request)
     and an unvisited one is not                     (or nothing is discovered)
     walking through the dark discovers nothing      (a lamp is still the tool)
     what you never had on screen stays unknown      (the margin is not sight)
     the FIELD is untouched, only the drawing        (or nothing spawns again)

   That last one is the one with teeth. The spawner asks how dark somewhere is;
   if discovery had lifted the measured field rather than the display mapping,
   creatures would stop spawning anywhere the player had ever carried a torch,
   and nothing about the picture would say so.

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "light.h"
#include "render.h"
#include "multiplayer.h"
#include <stdio.h>

static World g_testWorld;
static const int CX = 1400, CY = 6000;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  %-56s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

/* Solid rock with a pocket of iron in it, deep enough to be genuinely unlit. */
static void buriedMetal(World& w) {
    w.reset();
    seenReset();
    /* Big enough to enclose the whole SOLVED rectangle, which is the view plus
       a 256-cell margin on every side -- 1024 by 896. At +/-300 the top of that
       rectangle sat outside the stone and daylight poured straight down the
       empty world into it, so a blob 300 cells underground measured a light of
       39 and rendered at 40% before anything had lit it. "Buried" has to mean
       buried as far as the solver is concerned, not as far as the author is. */
    for (int y = CY - 700; y <= CY + 700; ++y)
        for (int x = CX - 700; x <= CX + 700; ++x)
            if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1)
                w.setCell(x, y, MAT_STONE);
    /* A CHAMBER with the blob standing in it, which is the situation the report
       describes -- metal you have poured into a space you are looking at.

       Not metal entombed in rock, which two earlier versions of this built. The
       inside of a solid block cannot be lit by anything and never should be;
       measuring there asks whether light passes through iron, which is a
       different question with the answer "no". What a player sees of a blob is
       its SURFACE, so that is where this samples. */
    for (int y = CY - 20; y <= CY + 20; ++y)
        for (int x = CX - 30; x <= CX + 30; ++x)
            w.setCell(x, y, MAT_EMPTY);
    for (int y = CY - 4; y <= CY + 4; ++y)
        for (int x = CX - 4; x <= CX + 4; ++x)
            w.setCell(x, y, MAT_IRON);
    w.setLiveWindow(CX - 720, CY - 720, CX + 720, CY + 720);
}

/* The camera puts the blob at the centre of the view, held for long enough that
   the field has actually arrived.

   The frame count is not padding. lightUpdate ends in lightSmooth, which moves
   the field an EIGHTH of the way toward the new solve each frame -- deliberate,
   so a value that oscillates between two frames is flattened rather than
   strobing. One call therefore shows about an eighth of a torch, which is below
   the discovery threshold: an earlier version of this called it once, saw a
   field of 11 where the solve says 85, and reported that lighting a chamber
   discovered nothing. Thirty frames is half a second of standing there. */
static void look(const World& w) {
    for (int f = 0; f < 30; ++f)
        lightUpdate(w, CX - VIEW_CELLS_W / 2, CY - VIEW_CELLS_H / 2);
}

/* How bright the blob is DRAWN, as the shade multiplier the renderer would
   apply. Read through lightRow, which is the row the renderer actually walks --
   not through the field, because the whole change is the difference between
   those two. */
/* The blob's top face, which is the part of it a player actually looks at. */
static const int SAMPLE_X = CX - 4, SAMPLE_Y = CY - 4;

static int drawnShade(const World& w) {
    (void)w;
    const int vx = SAMPLE_X - (CX - VIEW_CELLS_W / 2);
    const int vy = SAMPLE_Y - (CY - VIEW_CELLS_H / 2);
    /* Through lightAt, which is uncached, and NOT through lightRow.

       lightRow keeps one row, keyed on the row index and the camera -- which is
       right for the renderer, since it walks every row in order every frame and
       so never asks for the same key twice running. It is wrong for a harness
       that holds the camera still and changes the world underneath it: an
       earlier version of this read the same row before and after placing a
       torch, got the cached answer both times, and reported that lighting a
       chamber changed nothing.

       lightAt is the same display mapping -- it is what viewShade reads to
       shade every creature and machine drawn over the world -- so this measures
       the thing that matters without the cache in the way. The row is
       cross-checked separately below. */
    return (int)g_lightShade[lightAt(vx, vy)];
}

/* And the renderer's own row, with the cache deliberately busted by asking for
   a different row first. Worth checking once: lightRow and lightAt are two
   implementations of the same mapping and could drift apart. */
static int drawnShadeViaRow() {
    const int vx = SAMPLE_X - (CX - VIEW_CELLS_W / 2);
    const int vy = SAMPLE_Y - (CY - VIEW_CELLS_H / 2);
    lightRow(vy + 1);
    const u8* row = lightRow(vy);
    return (int)g_lightShade[row[vx]];
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();
    World& w = g_testWorld;

    /* --- 1. buried and never lit: as dark as it always was ---------------- */
    int darkShade = 0;
    {
        buriedMetal(w);
        look(w);
        darkShade = drawnShade(w);
        printf("never lit:        drawn at shade %d of 255 (%d%%)\n",
               darkShade, darkShade * 100 / 255);
        /* LIGHT_MIN_SHADE is 24. Allowing a little above it because the blob
           is deep inside rock but not infinitely far from the surface. */
        check(darkShade <= LIGHT_MIN_SHADE + 8,
              "rock you have never lit is as dark as it ever was");
    }

    /* --- 2. lit once, then unlit: far brighter, permanently --------------- */
    int seenShade = 0;
    {
        /* A torch put in, looked at, then taken away again. What is being
           tested is the MEMORY: after the light is gone the place must still be
           drawn bright, or "discovered" means nothing. */
        w.setCell(CX + 10, CY, MAT_TORCH);
        look(w);
        const int litShade = drawnShade(w);
        w.setCell(CX + 10, CY, MAT_EMPTY);
        look(w);
        seenShade = drawnShade(w);
        printf("lit, then dark:   %d while lit, %d after (%d%%)\n",
               litShade, seenShade, seenShade * 100 / 255);
        /* Comfortably clear of the undiscovered floor rather than a specific
           multiple of it. The brightness has already been tuned down once --
           59% to 30% -- so a check pinned tight against whatever it happens to
           be would have to be edited every time it moves, and would fail for
           the wrong reason when it did. What must stay true is that the two are
           obviously different. */
        check(seenShade >= darkShade * 2,
              "somewhere you have lit stays far brighter than somewhere you have not");
        /* The request, stated as a number: a blob of metal has to read as
           metal. Iron is 0xA8ADB6; at the old floor of 24 that renders 0x0F0F10,
           which is black. */
        check(seenShade >= 60,
              "and bright enough to tell what the material is");
        printf("  the renderer's own row agrees: %d\n", drawnShadeViaRow());
        check(drawnShadeViaRow() == seenShade,
              "and the row the renderer walks says the same thing");
    }

    /* --- 2b. the edge of what you have seen FADES ------------------------- */
    /* Asked for after the first version shipped: "the edges of discover to non
       discovered should fade like dark to light."

       It did not. The map is one bit per light sample, so the boundary was a
       hard 4-cell staircase from shade 76 down to 24 -- and a straight edge
       with no counterpart in the world reads as a fault in the picture rather
       than as a shadow, which is the same argument the light field's own note
       makes for interpolating instead of shading in blocks.

       What is measured is the walk from inside explored space out into rock
       nobody has lit: it has to descend, and it has to do it over several
       distinct steps rather than in one drop. */
    {
        /* The chamber from buriedMetal is 61 cells wide; light it all, then
           walk right, out through the wall and into undiscovered stone. */
        buriedMetal(w);
        w.setCell(CX, CY + 8, MAT_TORCH);
        look(w);
        w.setCell(CX, CY + 8, MAT_EMPTY);
        look(w);

        const int vy = (CY + 8) - (CY - VIEW_CELLS_H / 2);
        int prev = 999, drops = 0, steps = 0, plateaus = 0;
        printf("across the edge: ");
        for (int dx = 0; dx <= 60; ++dx) {
            const int vx = (CX + dx) - (CX - VIEW_CELLS_W / 2);
            const int sh = (int)g_lightShade[lightAt(vx, vy)];
            if (dx % 6 == 0) printf("%d ", sh);
            if (prev != 999) {
                if (sh < prev) { ++drops; ++steps; }
                else if (sh == prev) ++plateaus;
            }
            prev = sh;
        }
        printf("\n  %d distinct decreasing steps over 60 cells\n", steps);
        check(drops > 0, "it gets darker as you leave what you have seen");
        /* A hard edge is ONE step. Anything that fades has many, and the count
           is what separates the two -- not the endpoints, which are the same
           either way. */
        check(steps >= 8, "and does it as a gradient rather than in one jump");
    }

    /* --- 3. walking through the dark discovers nothing --------------------- */
    /* If it did, the split would be pointless: everything would be discovered
       the moment you walked past it, and the bright floor would apply
       everywhere. A lamp has to remain the thing that reveals. */
    {
        buriedMetal(w);
        for (int k = 0; k < 20; ++k) look(w);
        printf("looked at 20 times, unlit: drawn at shade %d\n", drawnShade(w));
        check(drawnShade(w) <= LIGHT_MIN_SHADE + 8,
              "looking at unlit rock does not discover it");
    }

    /* --- 4. the margin is not eyesight ------------------------------------ */
    /* The field is solved 64 samples past every edge so an off-screen lamp
       still spills onto what you can see. A sample out there is one the player
       has never had on screen, so marking it would discover several screens of
       cave in every direction from a torch you walked past. */
    {
        buriedMetal(w);
        /* Bright enough to be solved for, far enough out to be off screen. */
        for (int y = CY - 2; y <= CY + 2; ++y)
            for (int x = CX + VIEW_CELLS_W / 2 + 40;
                 x <= CX + VIEW_CELLS_W / 2 + 44; ++x)
                w.setCell(x, y, MAT_TORCH);
        look(w);
        const bool offScreenSeen = seenAt(CX + VIEW_CELLS_W / 2 + 42, CY);
        printf("a torch 40 cells off the right edge: that spot %s\n",
               offScreenSeen ? "was discovered" : "stayed unknown");
        check(!offScreenSeen, "what was never on screen is not discovered");
    }

    /* --- 5. the FIELD is untouched ---------------------------------------- */
    /* The one with teeth. lightAtWorld is what the spawner reads to decide
       whether somewhere is dark enough for a creature. Discovery is a display
       decision and must not reach it, or lighting a cave once would stop it
       spawning anything ever again. */
    {
        buriedMetal(w);
        look(w);
        const int before = (int)lightAtWorld(SAMPLE_X, SAMPLE_Y);
        w.setCell(CX + 10, CY, MAT_TORCH);
        look(w);
        w.setCell(CX + 10, CY, MAT_EMPTY);
        look(w);
        const int after = (int)lightAtWorld(SAMPLE_X, SAMPLE_Y);
        printf("measured field at the blob: %d unlit, %d after being discovered\n",
               before, after);
        check(after == before,
              "discovery changes the drawing and not the measured field");
        check(drawnShade(w) > darkShade * 3,
              "even though the same cell is now drawn bright");
    }

    if (failures) {
        fprintf(stderr, "\n%d discovered-light check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
