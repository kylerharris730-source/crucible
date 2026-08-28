/* --- a lighter liquid in a sealed vessel has to flatten out -------------------

   Reported from play: "im still getting squares forming in water... you have a
   pocket of lava with no space at the top, and it gets slag in it, the slag
   melts and forms a square at the top", and then the general form -- "any
   contained vessel of liquid with some of a less dense liquid in it, the less
   dense liquid doesn't flatten out at all".

   There was one submerged-leveling rule and it only relaxed a MOUND of the
   DENSER liquid. Nothing relaxed the opposite shape, and in a sealed vessel
   that is the shape you get: the lighter liquid rises until it meets the lid
   and stops dead, still square, because every other rule that could spread it
   needs somewhere emptier to go and a full vessel has nowhere.

   Underneath that sat a second wall. tryMove only ever let a liquid move into
   something LESS DENSE than itself -- gases invert that test, liquids did not
   -- so even with a leveling rule asking for it, a light parcel could not
   trade upward into a heavy one.

   Four properties, and the fourth is the one that keeps the widened tryMove
   honest:

     a light pocket flattens under the lid       (the report)
     including at a density gap of ten           (the reported pair exactly)
     nothing is created or destroyed             (a swap, not a spawn)
     a DENSE blob still sinks, and a light one
     still does not                              (the inverse is still refused)

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "multiplayer.h"
#include <stdio.h>

static World g_testWorld;

static const int CX = 1400, CY = 5000;
static const int WALL = 40;          /* vessel half-width, in cells */

static void fill(World& w, int x0, int y0, int x1, int y1, u8 m) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1)
                w.setCell(x, y, m);
}

/* A SEALED vessel, brim full of `host`, with a square blob of `blob` in it.
   No air gap anywhere -- that is the whole point, because an air gap gives the
   ordinary surface-flow rules somewhere to work and hides this entirely. */
static void setup(World& w, u8 host, u8 blob, int celsius) {
    w.reset();
    fill(w, CX - WALL - 10, CY - WALL - 10, CX + WALL + 10, CY + WALL + 10, MAT_STONE);
    fill(w, CX - WALL, CY - WALL, CX + WALL, CY + WALL, host);
    fill(w, CX - 6, CY + 10, CX + 6, CY + 22, blob);
    for (int y = CY - WALL - 5; y <= CY + WALL + 5; ++y)
        for (int x = CX - WALL - 5; x <= CX + WALL + 5; ++x)
            w.temp[y * SIM_W + x] = degC(celsius);
    w.setLiveWindow(CX - WALL - 20, CY - WALL - 20, CX + WALL + 20, CY + WALL + 20);
}

struct Shape { int cells, width, height, topOffset, bottomOffset; };

static Shape measure(const World& w, u8 mat) {
    Shape s; s.cells = 0;
    int minx = 1 << 30, maxx = -(1 << 30), miny = 1 << 30, maxy = -(1 << 30);
    for (int y = CY - WALL; y <= CY + WALL; ++y)
        for (int x = CX - WALL; x <= CX + WALL; ++x)
            if (w.at(x, y).mat == mat) {
                ++s.cells;
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
            }
    if (!s.cells) { s.width = s.height = s.topOffset = s.bottomOffset = -1; return s; }
    s.width = maxx - minx + 1;
    s.height = maxy - miny + 1;
    s.topOffset = miny - (CY - WALL);          /* 0 == touching the lid */
    s.bottomOffset = (CY + WALL) - maxy;       /* 0 == touching the floor */
    return s;
}

static void run(World& w, int frames, int celsius) {
    for (int f = 0; f < frames; ++f) {
        /* Held at temperature so a phase change cannot quietly end the
           experiment -- molten slag freezes below 120 C and the first version
           of this ran it at 300 and boiled it away instead. */
        for (int y = CY - WALL - 5; y <= CY + WALL + 5; ++y)
            for (int x = CX - WALL - 5; x <= CX + WALL + 5; ++x)
                w.temp[y * SIM_W + x] = degC(celsius);
        w.step();
    }
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();
    World& w = g_testWorld;
    int failures = 0;

    struct Case { const char* name; u8 host, blob; int celsius; };
    const Case cases[] = {
        /* The reported pair, and the hardest: only ten density apart. */
        { "molten slag in lava", MAT_LAVA,    MAT_SLAG_MELT, 150 },
        { "water in mercury",    MAT_MERCURY, MAT_WATER,      20 },
        { "wax in fuel",         MAT_FUEL,    MAT_WAX,        20 },
    };

    for (int k = 0; k < 3; ++k) {
        const Case& c = cases[k];
        setup(w, c.host, c.blob, c.celsius);
        const Shape before = measure(w, c.blob);
        run(w, 2000, c.celsius);
        const Shape after = measure(w, c.blob);

        printf("%-20s gap %3d: %2dx%-2d -> %2dx%-2d, %d cells, top %+d from lid\n",
               c.name,
               (int)MATS[c.host].density - (int)MATS[c.blob].density,
               before.width, before.height, after.width, after.height,
               after.cells, after.topOffset);

        if (after.cells != before.cells) {
            fprintf(stderr, "FAIL: %s went from %d cells to %d -- leveling must "
                            "be a swap, not a source or a drain\n",
                    c.name, before.cells, after.cells);
            ++failures;
        }
        if (after.topOffset != 0) {
            fprintf(stderr, "FAIL: %s did not reach the lid (top is %d cells "
                            "below it)\n", c.name, after.topOffset);
            ++failures;
        }
        /* Flattened means WIDE and THIN. A 13x13 blob of 169 cells spread over
           an 81-wide vessel is about three rows, so anything still taller than
           a quarter of its width is a square by any reasonable reading. */
        if (after.width < after.height * 4) {
            fprintf(stderr, "FAIL: %s is still %d wide by %d tall -- that is the "
                            "square that was reported\n",
                    c.name, after.width, after.height);
            ++failures;
        }
    }

    /* --- the inverse must still be refused -------------------------------
       tryMove was widened to let a light parcel trade upward into a heavy one,
       and the callers that ask for the ordinary direction rely on that test to
       reject the inverse FOR them. If the widening leaked, a dense blob would
       stop sinking and a light one would start. Both halves are checked. */
    {
        setup(w, MAT_WATER, MAT_MERCURY, 20);
        const Shape before = measure(w, MAT_MERCURY);
        run(w, 2000, 20);
        const Shape after = measure(w, MAT_MERCURY);
        printf("%-20s          : %2dx%-2d -> %2dx%-2d, %d cells, bottom %+d from floor\n",
               "mercury in water", before.width, before.height,
               after.width, after.height, after.cells, after.bottomOffset);
        if (after.cells != before.cells) {
            fprintf(stderr, "FAIL: mercury went from %d cells to %d\n",
                    before.cells, after.cells);
            ++failures;
        }
        if (after.bottomOffset != 0) {
            fprintf(stderr, "FAIL: the dense blob did not reach the floor -- the "
                            "original leveling rule has regressed\n");
            ++failures;
        }
        if (after.topOffset == 0) {
            fprintf(stderr, "FAIL: mercury reached the LID of a water vessel -- "
                            "the lighter-wins exchange has leaked into the "
                            "ordinary direction\n");
            ++failures;
        }
    }

    if (failures) {
        fprintf(stderr, "\n%d liquid layering check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
