#include "door.h"
#include <string.h>

static const int DOOR_SIDE = DOOR_REACH * 2 + 1;
static u8  g_dseen[(DOOR_SIDE * DOOR_SIDE + 7) / 8];
/* Cells pushed but not popped can never exceed the number accepted, so a patch
   that is going to succeed cannot overflow this. A patch that would is over cap
   and failing is the right answer -- which is why the overflow branch is a plain
   bail rather than something defensive. */
static i32 g_dstack[DOOR_MAX_CELLS + 64];
static i32 g_dfound[DOOR_MAX_CELLS];

static const int DDX[4] = { 0, 0, -1, 1 };
static const int DDY[4] = { -1, 1, 0, 0 };

int doorToggle(World& w, int sx, int sy) {
    if (sx < PLAY_X0 || sx > PLAY_X1 || sy < PLAY_Y0 || sy > PLAY_Y1) return 0;
    const u8 seed = w.at(sx, sy).mat;
    if (!isDoor(seed)) return 0;

    /* The state the whole patch is heading for, decided once from the cell that
       was poked. See the note in door.h on why this is not per-cell. */
    const u8 target  = (seed == MAT_DOOR) ? MAT_DOOR_OPEN : MAT_DOOR;
    const bool closing = (target == MAT_DOOR);

    const int bx0 = sx - DOOR_REACH, by0 = sy - DOOR_REACH;
    memset(g_dseen, 0, sizeof(g_dseen));

    int sp = 0, n = 0;
    const int seedLocal = DOOR_REACH * DOOR_SIDE + DOOR_REACH;
    g_dseen[seedLocal >> 3] |= (u8)(1 << (seedLocal & 7));
    g_dstack[sp++] = seedLocal;

    /* Collect the whole patch BEFORE changing any of it. Converting as we go
       would leave a door half-open when the scan hits the cap or finds the
       player in the way, and a door stuck half-shut is worse than one that
       refused to move -- you would have no idea which half of it to dig. */
    while (sp > 0) {
        const int li = g_dstack[--sp];
        const int lx = li % DOOR_SIDE, ly = li / DOOR_SIDE;
        const int wx = bx0 + lx,       wy = by0 + ly;

        if (n >= DOOR_MAX_CELLS) return 0;
        g_dfound[n++] = wy * SIM_W + wx;

        /* A door will not close on you. blocksCell is the entity box -- the
           character, and anything else that occupies space -- and shutting a
           solid material into it would entomb whoever is standing in the
           doorway and leave the unstick rule to shove them somewhere.

           Only ever a reason to refuse CLOSING. Opening a door with someone in
           it is the most ordinary thing a door does. */
        if (closing && w.blocksCell(wx, wy)) return 0;

        for (int k = 0; k < 4; ++k) {
            const int nx = wx + DDX[k], ny = wy + DDY[k];
            if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1) continue;
            if (!isDoor(w.cells[ny * SIM_W + nx].mat)) continue;

            const int nlx = lx + DDX[k], nly = ly + DDY[k];
            /* Out of reach: the patch is bigger than the scratch buffer can
               describe, so this is the same failure as being over the cell cap
               and takes the same answer. Not `continue` -- carrying on would
               toggle the near half of a long door and leave the rest. */
            if (nlx < 0 || nlx >= DOOR_SIDE || nly < 0 || nly >= DOOR_SIDE) return 0;

            const int nli = nly * DOOR_SIDE + nlx;
            const int byte = nli >> 3, bit = 1 << (nli & 7);
            if (g_dseen[byte] & bit) continue;
            g_dseen[byte] |= (u8)bit;

            if (sp >= (int)(sizeof(g_dstack) / sizeof(g_dstack[0]))) return 0;
            g_dstack[sp++] = nli;
        }
    }

    /* swapMat, not setCell: opening a door does not make a new object. The
       temperature has to survive or a door in the wall of a hot workshop would
       reset to ambient every time you walked through it, which is a hole in the
       heat model you could pump; the speckle has to survive or the door would
       visibly re-grain as it swung. See the note on World::swapMat. */
    for (int i = 0; i < n; ++i) {
        const int idx = g_dfound[i];
        w.swapMat(idx % SIM_W, idx / SIM_W, target);
    }
    return n;
}
