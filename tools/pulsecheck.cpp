/* ============================================================================
   pulsecheck.cpp -- the block-allocated pulse planes behave exactly like the
   flat ones they replaced.

   The change swapped one u16 per world cell for 32x32 blocks allocated on
   demand, which freed 81 MB. Storage moved; semantics must not have. The only
   thing that can really go wrong is the index arithmetic -- a cell landing in
   the wrong block, or two cells sharing a slot -- and that class of bug is
   invisible until two circuits interfere at a distance, which is exactly the
   kind of thing nobody reproduces on purpose.

   So this checks the structure directly rather than through gameplay:

     1. (block, offset) is a BIJECTION over the cells tested -- no two cells
        share a slot, and every offset lands inside its block.
     2. Read/write matches a reference map exactly, for a sample built to
        include the corners, the edges and the seams between blocks, which is
        where index arithmetic goes wrong if it is going to.
     3. An unwritten cell reads zero without allocating, since zero is the
        unvisited stamp the flat plane returned for free.
     4. A release clears everything.

   It includes device.cpp directly, because the accessors are static and the
   point is to test THOSE and not a copy of them. Build with every src/*.cpp
   except main.cpp AND device.cpp:

     g++ -std=c++11 -O2 -Isrc tools/pulsecheck.cpp <src except main, device> \
         -o artifacts/pulsecheck.exe -lgdi32 -luser32 -lwinmm -lmsimg32 -lws2_32
   ========================================================================== */
#include "device.cpp"

#include <stdio.h>
#include <map>
#include <set>
#include <vector>

static int g_fail = 0;

static void check(bool ok, const char* what) {
    if (!ok) { printf("  FAIL  %s\n", what); ++g_fail; }
}

/* A deterministic generator, so a failure is reproducible. */
static unsigned g_testRng = 12345u;
static unsigned nextRand() { g_testRng = g_testRng * 1664525u + 1013904223u; return g_testRng; }

int main(void) {
    printf("pulse plane: %d blocks of %d cells (%d x %d)\n",
           PULSE_BLOCK_COUNT, PULSE_BLOCK_CELLS, PULSE_BLOCKS_X, PULSE_BLOCKS_Y);
    printf("flat cost would be %.1f MB; table is %.0f KB\n",
           (double)SIM_W * SIM_H * 2.0 / 1048576.0,
           (double)sizeof(g_pulseMarkBlock) / 1024.0);

    /* --- the sample ------------------------------------------------------
       Corners and seams first, because that is where block arithmetic breaks,
       then a spread of random cells for coverage. */
    std::vector<int> cells;
    const int seams[] = { 0, 1, 31, 32, 33, 63, 64, 4095 };
    for (int i = 0; i < (int)(sizeof(seams) / sizeof(seams[0])); ++i)
        for (int j = 0; j < (int)(sizeof(seams) / sizeof(seams[0])); ++j) {
            const int x = seams[i], y = seams[j];
            cells.push_back(y * SIM_W + x);
        }
    /* Every corner of the world, where a wrong shift shows up immediately. */
    const int xs[] = { 0, SIM_W - 1 }, ys[] = { 0, SIM_H - 1 };
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) cells.push_back(ys[j] * SIM_W + xs[i]);
    for (int i = 0; i < 40000; ++i) {
        const int x = (int)(nextRand() % SIM_W);
        const int y = (int)(nextRand() % SIM_H);
        cells.push_back(y * SIM_W + x);
    }

    /* --- 1. bijection ---------------------------------------------------- */
    {
        std::set<long long> seen;
        bool dup = false, oob = false;
        for (size_t i = 0; i < cells.size(); ++i) {
            const int b = pulseBlockOf(cells[i]);
            const int o = pulseOffsetOf(cells[i]);
            if (b < 0 || b >= PULSE_BLOCK_COUNT || o < 0 || o >= PULSE_BLOCK_CELLS)
                oob = true;
            const long long slot = (long long)b * PULSE_BLOCK_CELLS + o;
            if (!seen.insert(slot).second) {
                /* Only a genuine collision matters -- the sample has repeats. */
                bool sameCell = false;
                for (size_t j = 0; j < i; ++j) if (cells[j] == cells[i]) sameCell = true;
                if (!sameCell) dup = true;
            }
        }
        check(!oob, "every (block, offset) lands inside its block");
        check(!dup, "no two distinct cells share a slot");
    }

    /* --- 3. absent blocks read as zero, and reading does not allocate ----- */
    {
        pulseBlocksRelease();
        bool allZero = true;
        for (size_t i = 0; i < cells.size(); ++i)
            if (pulseMarkAt(cells[i]) != 0 || sparkRecentStamp(cells[i]) != 0)
                allZero = false;
        check(allZero, "an unwritten cell reads zero");

        int allocated = 0;
        for (int i = 0; i < PULSE_BLOCK_COUNT; ++i)
            if (g_pulseMarkBlock[i] || g_pulseRecentBlock[i]) ++allocated;
        check(allocated == 0, "reading allocated nothing");

        /* Writing zero into an absent block must also allocate nothing. */
        for (size_t i = 0; i < 100 && i < cells.size(); ++i) {
            pulseMarkSet(cells[i], 0);
            sparkSetRecentStamp(cells[i], 0);
        }
        allocated = 0;
        for (int i = 0; i < PULSE_BLOCK_COUNT; ++i)
            if (g_pulseMarkBlock[i] || g_pulseRecentBlock[i]) ++allocated;
        check(allocated == 0, "writing zero into an absent block allocated nothing");
    }

    /* --- 2. matches a reference exactly ---------------------------------- */
    {
        pulseBlocksRelease();
        std::map<int, u16> refMark;
        std::map<int, u8>  refRecent;

        for (size_t i = 0; i < cells.size(); ++i) {
            const int c = cells[i];
            const u16 v = (u16)(nextRand() & 0xFFFFu);
            const u8  r = (u8)(nextRand() & 3u);
            pulseMarkSet(c, v);        refMark[c] = v;
            sparkSetRecentStamp(c, r); refRecent[c] = r;
        }

        int badMark = 0, badRecent = 0;
        for (std::map<int, u16>::const_iterator it = refMark.begin();
             it != refMark.end(); ++it)
            if (pulseMarkAt(it->first) != it->second) ++badMark;
        for (std::map<int, u8>::const_iterator it = refRecent.begin();
             it != refRecent.end(); ++it)
            if (sparkRecentStamp(it->first) != it->second) ++badRecent;

        check(badMark == 0, "every pulse mark reads back what was written");
        check(badRecent == 0, "every recent stamp reads back what was written");
        if (badMark) printf("        %d marks differed\n", badMark);
        if (badRecent) printf("        %d stamps differed\n", badRecent);

        /* Overwrite in place, including back to zero, which must not disturb
           the neighbours packed into the same byte. */
        for (size_t i = 0; i < cells.size(); i += 3) {
            const int c = cells[i];
            pulseMarkSet(c, 0);        refMark[c] = 0;
            sparkSetRecentStamp(c, 0); refRecent[c] = 0;
        }
        badMark = badRecent = 0;
        for (std::map<int, u16>::const_iterator it = refMark.begin();
             it != refMark.end(); ++it)
            if (pulseMarkAt(it->first) != it->second) ++badMark;
        for (std::map<int, u8>::const_iterator it = refRecent.begin();
             it != refRecent.end(); ++it)
            if (sparkRecentStamp(it->first) != it->second) ++badRecent;
        check(badMark == 0, "clearing a cell leaves its neighbours alone (marks)");
        check(badRecent == 0, "clearing a cell leaves its neighbours alone (stamps)");

        int allocated = 0;
        for (int i = 0; i < PULSE_BLOCK_COUNT; ++i) if (g_pulseMarkBlock[i]) ++allocated;
        printf("touched %d cells -> %d of %d blocks allocated (%.2f MB)\n",
               (int)refMark.size(), allocated, PULSE_BLOCK_COUNT,
               allocated * (PULSE_BLOCK_CELLS * 2.0 + PULSE_BLOCK_CELLS / 4.0) / 1048576.0);
    }

    /* --- 4. release clears everything ------------------------------------ */
    {
        pulseBlocksRelease();
        bool allZero = true;
        for (size_t i = 0; i < cells.size(); ++i)
            if (pulseMarkAt(cells[i]) != 0 || sparkRecentStamp(cells[i]) != 0)
                allZero = false;
        check(allZero, "release clears every cell");
        int allocated = 0;
        for (int i = 0; i < PULSE_BLOCK_COUNT; ++i)
            if (g_pulseMarkBlock[i] || g_pulseRecentBlock[i]) ++allocated;
        check(allocated == 0, "release frees every block");
    }

    printf(g_fail ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
