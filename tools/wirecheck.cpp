/* ============================================================================
   wirecheck.cpp -- a behavioural signature of the spark system.

   pulsecheck.cpp proves the block-allocated pulse planes store and return what
   the flat ones did. This proves the CIRCUITS built on them behave the same,
   which is a different claim: the subtle parts of sparkStep are about what a
   front does when it meets another front's trail, and those read the planes
   through indices spanning many cells and several blocks at once.

   The scenes are chosen for the cases the pulse marks exist to decide:

     straight   a plain run, the baseline
     thick      two-cell wire, where sibling fronts must fill the cross-section
                exactly once rather than flood back down each other's trail
     ring       a loop, where fronts going opposite ways must meet and stop
     headon     two pulses sent at each other, which is the exact case the
                recent-wake logic was added for
     spanning   a wire long enough to cross several 32-cell block boundaries,
                so an indexing error shows up as a spark vanishing at a seam

   It prints a signature -- every live spark, every frame -- so the output can
   be diffed against a build from before the change. Identical output over
   these scenes is the evidence; anything else is a regression.

   Build with every src/*.cpp except main.cpp.
   ========================================================================== */
#include "world.h"
#include "materials.h"
#include "device.h"
#include "render.h"
#include "light.h"
#include "item.h"
#include "sprite.h"
#include "multiplayer.h"
#include "projectile.h"
#include "room.h"
#include "tree.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

static World g_w;
static bool g_quiet = false;   /* bench mode: measure, do not print */

static void wireH(int x0, int x1, int y) {
    for (int x = x0; x <= x1; ++x) g_w.setCell(x, y, MAT_COPPER);
}
static void wireV(int x, int y0, int y1) {
    for (int y = y0; y <= y1; ++y) g_w.setCell(x, y, MAT_COPPER);
}

static void clearArea(int x0, int y0, int x1, int y1) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) g_w.setCell(x, y, MAT_EMPTY);
}

/* Sorted, so the signature does not depend on which slot a spark happens to
   occupy in the array -- only on where the sparks actually are. */
static void dumpFrame(const char* scene, int frame) {
    struct Row { int x, y, dx, dy; };
    Row rows[MAX_SPARKS];
    int n = 0;
    for (int i = 0; i < MAX_SPARKS; ++i) {
        if (!g_sparks[i].used) continue;
        rows[n].x = g_sparks[i].x;   rows[n].y = g_sparks[i].y;
        rows[n].dx = g_sparks[i].dx; rows[n].dy = g_sparks[i].dy;
        ++n;
    }
    for (int i = 1; i < n; ++i) {
        Row k = rows[i];
        int j = i - 1;
        while (j >= 0 && (rows[j].y > k.y || (rows[j].y == k.y && rows[j].x > k.x))) {
            rows[j + 1] = rows[j]; --j;
        }
        rows[j + 1] = k;
    }
    if (g_quiet) return;
    printf("%s f%03d n=%d", scene, frame, n);
    for (int i = 0; i < n; ++i)
        printf(" (%d,%d;%d,%d)", rows[i].x, rows[i].y, rows[i].dx, rows[i].dy);
    printf("\n");
}

/* Ticks only. sparkClear is timed separately because the two changed for
   different reasons: the clear used to memset 81 MB of flat plane and now
   frees a few blocks, which is a large win that says nothing about whether
   the per-step ACCESS got faster or slower. Conflating them would credit the
   indirection with a saving that belongs entirely to the clear. */
static double g_tickSeconds = 0.0;
static double g_clearSeconds = 0.0;
static LARGE_INTEGER g_benchFreq;

static void timedSparkClear() {
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    sparkClear();
    QueryPerformanceCounter(&b);
    g_clearSeconds += (double)(b.QuadPart - a.QuadPart) / (double)g_benchFreq.QuadPart;
}

static void runScene(const char* name, int frames) {
    for (int f = 0; f < frames; ++f) {
        LARGE_INTEGER a, b;
        QueryPerformanceCounter(&a);
        devTick(g_w);
        QueryPerformanceCounter(&b);
        g_tickSeconds += (double)(b.QuadPart - a.QuadPart) / (double)g_benchFreq.QuadPart;
        dumpFrame(name, f);
        if (sparkCount() == 0) break;
    }
}

int main(int argc, char** argv) {
    const bool bench = (argc > 1 && strcmp(argv[1], "bench") == 0);
    g_quiet = bench;
    const int reps = bench ? 40 : 1;
    QueryPerformanceFrequency(&g_benchFreq);
    LARGE_INTEGER fq = g_benchFreq;
    LARGE_INTEGER t0; QueryPerformanceCounter(&t0);
  for (int rep = 0; rep < reps; ++rep) {
    initMaterials();
    initItems();
    g_w.reset();

    /* A quiet patch of world, built rather than generated: worldgen is seeded
       and stable, but this way the scenes do not depend on it at all. */
    const int BX = 1024, BY = 4096;

    {   /* straight: a plain 120-cell run */
        clearArea(BX - 4, BY - 8, BX + 200, BY + 8);
        wireH(BX, BX + 120, BY);
        timedSparkClear();
        sparkAdd(BX, BY, 1, 0);
        runScene("straight", 140);
    }
    {   /* thick: two-cell wire, sibling fronts across the cross-section */
        clearArea(BX - 4, BY + 20, BX + 200, BY + 40);
        wireH(BX, BX + 120, BY + 30);
        wireH(BX, BX + 120, BY + 31);
        timedSparkClear();
        sparkAdd(BX, BY + 30, 1, 0);
        runScene("thick", 140);
    }
    {   /* ring: fronts round a loop must meet and stop */
        clearArea(BX - 4, BY + 60, BX + 120, BY + 140);
        wireH(BX, BX + 80, BY + 70);
        wireH(BX, BX + 80, BY + 130);
        wireV(BX, BY + 70, BY + 130);
        wireV(BX + 80, BY + 70, BY + 130);
        timedSparkClear();
        sparkAdd(BX + 40, BY + 70, 1, 0);
        runScene("ring", 160);
    }
    {   /* headon: the case the recent wake exists for */
        clearArea(BX - 4, BY + 160, BX + 200, BY + 180);
        wireH(BX, BX + 160, BY + 170);
        timedSparkClear();
        sparkAdd(BX, BY + 170, 1, 0);
        sparkAdd(BX + 160, BY + 170, -1, 0);
        runScene("headon", 140);
    }
    {   /* spanning: crosses many 32-cell block seams, in both axes */
        clearArea(BX - 4, BY + 200, BX + 400, BY + 400);
        wireH(BX, BX + 380, BY + 210);
        wireV(BX + 380, BY + 210, BY + 390);
        wireH(BX, BX + 380, BY + 390);
        timedSparkClear();
        sparkAdd(BX, BY + 210, 1, 0);
        runScene("spanning", 400);
    }
  }
    if (bench) {
        LARGE_INTEGER t1; QueryPerformanceCounter(&t1);
        const double ms = 1000.0 * (double)(t1.QuadPart - t0.QuadPart) / (double)fq.QuadPart;
        printf("bench over %d reps:  total %7.3f  devTick %7.3f  sparkClear %7.3f   (ms/rep)\n",
               reps, ms / reps,
               1000.0 * g_tickSeconds / reps, 1000.0 * g_clearSeconds / reps);
    } else {
        printf("done\n");
    }
    return 0;
}
