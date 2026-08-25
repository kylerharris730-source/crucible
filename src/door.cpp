#include "door.h"
#include "player.h"
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

/* A player-sized doorway needs a little anticipation: opening only after the
   body overlaps a closed door would be too late because collision has already
   stopped the walk. Four cells gives the door a beat to respond without making
   it feel as though doors across a room are moving on their own. */
static const int DOOR_AUTO_MARGIN = 4;
static const int AUTO_DOOR_MAX = 32;
static i32 g_autoDoor[AUTO_DOOR_MAX];
static int g_autoDoorN = 0;

static bool playerNearOpenDoor(const World& w, const Player& p) {
    const int x0 = imax(PLAY_X0, p.left() - DOOR_AUTO_MARGIN);
    const int x1 = imin(PLAY_X1, p.right() + DOOR_AUTO_MARGIN);
    const int y0 = imax(PLAY_Y0, p.top() - DOOR_AUTO_MARGIN);
    const int y1 = imin(PLAY_Y1, p.bottom() + DOOR_AUTO_MARGIN);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (w.at(x, y).mat == MAT_DOOR_OPEN) return true;
    return false;
}

static void rememberAutoDoor(int idx) {
    for (int i = 0; i < g_autoDoorN; ++i)
        if (g_autoDoor[i] == idx) return;
    if (g_autoDoorN < AUTO_DOOR_MAX) g_autoDoor[g_autoDoorN++] = idx;
}

/* Collect one connected patch without changing it. doorToggle and automatic
   closing both need the exact same definition of "this door"; keeping one
   flood prevents a long or bent doorway from being treated differently by
   interaction and proximity. */
static int collectDoorPatch(const World& w, int sx, int sy) {
    if (sx < PLAY_X0 || sx > PLAY_X1 || sy < PLAY_Y0 || sy > PLAY_Y1) return 0;
    if (!isDoor(w.at(sx, sy).mat)) return 0;

    const int bx0 = sx - DOOR_REACH, by0 = sy - DOOR_REACH;
    memset(g_dseen, 0, sizeof(g_dseen));
    int sp = 0, n = 0;
    const int seedLocal = DOOR_REACH * DOOR_SIDE + DOOR_REACH;
    g_dseen[seedLocal >> 3] |= (u8)(1 << (seedLocal & 7));
    g_dstack[sp++] = seedLocal;

    while (sp > 0) {
        const int li = g_dstack[--sp];
        const int lx = li % DOOR_SIDE, ly = li / DOOR_SIDE;
        const int wx = bx0 + lx,       wy = by0 + ly;
        if (n >= DOOR_MAX_CELLS) return 0;
        g_dfound[n++] = wy * SIM_W + wx;

        for (int k = 0; k < 4; ++k) {
            const int nx = wx + DDX[k], ny = wy + DDY[k];
            if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1) continue;
            if (!isDoor(w.cells[ny * SIM_W + nx].mat)) continue;
            const int nlx = lx + DDX[k], nly = ly + DDY[k];
            if (nlx < 0 || nlx >= DOOR_SIDE || nly < 0 || nly >= DOOR_SIDE) return 0;
            const int nli = nly * DOOR_SIDE + nlx;
            const int byte = nli >> 3, bit = 1 << (nli & 7);
            if (g_dseen[byte] & bit) continue;
            g_dseen[byte] |= (u8)bit;
            if (sp >= (int)(sizeof(g_dstack) / sizeof(g_dstack[0]))) return 0;
            g_dstack[sp++] = nli;
        }
    }
    return n;
}

int doorToggle(World& w, int sx, int sy) {
    if (sx < PLAY_X0 || sx > PLAY_X1 || sy < PLAY_Y0 || sy > PLAY_Y1) return 0;
    const u8 seed = w.at(sx, sy).mat;
    if (!isDoor(seed)) return 0;

    /* The state the whole patch is heading for, decided once from the cell that
       was poked. See the note in door.h on why this is not per-cell. */
    const u8 target  = (seed == MAT_DOOR) ? MAT_DOOR_OPEN : MAT_DOOR;
    const bool closing = (target == MAT_DOOR);

    /* Collect the whole patch BEFORE changing any of it. Converting as we go
       would leave a door half-open when the scan hits the cap or finds the
       player in the way, and a door stuck half-shut is worse than one that
       refused to move -- you would have no idea which half of it to dig. */
    const int n = collectDoorPatch(w, sx, sy);
    if (n <= 0) return 0;
    /* A door will not close on any player. collectDoorPatch has already
       accepted the whole patch, so this check cannot leave it half changed. */
    if (closing)
        for (int i = 0; i < n; ++i)
            if (w.blocksCell(g_dfound[i] % SIM_W, g_dfound[i] / SIM_W)) return 0;

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

void doorAutoOpen(World& w, const Player& p) {
    /* First open every closed door in the approach box. Opening one connected
       patch changes it all to MAT_DOOR_OPEN, so subsequent cells from that
       patch naturally skip it rather than immediately toggling it back. */
    const int x0 = imax(PLAY_X0, p.left() - DOOR_AUTO_MARGIN);
    const int x1 = imin(PLAY_X1, p.right() + DOOR_AUTO_MARGIN);
    const int y0 = imax(PLAY_Y0, p.top() - DOOR_AUTO_MARGIN);
    const int y1 = imin(PLAY_Y1, p.bottom() + DOOR_AUTO_MARGIN);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (w.at(x, y).mat != MAT_DOOR) continue;
            if (doorToggle(w, x, y) > 0) rememberAutoDoor(y * SIM_W + x);
        }
    }

}

static bool patchNearAnyOccupant(const World& w, int sx, int sy) {
    const int n = collectDoorPatch(w, sx, sy);
    if (n <= 0) return false;
    for (int i = 0; i < n; ++i) {
        const int x = g_dfound[i] % SIM_W, y = g_dfound[i] / SIM_W;
        for (int slot = 0; slot < World::MAX_OCCUPANTS; ++slot) {
            if (!(w.occupantMask & (1u << slot))) continue;
            const World::OccupantBox& b = w.occupant[slot];
            if (x >= b.x0 - DOOR_AUTO_MARGIN && x <= b.x1 + DOOR_AUTO_MARGIN &&
                y >= b.y0 - DOOR_AUTO_MARGIN && y <= b.y1 + DOOR_AUTO_MARGIN)
                return true;
        }
    }
    return false;
}

void doorAutoClose(World& w) {
    int kept = 0;
    for (int i = 0; i < g_autoDoorN; ++i) {
        const int idx = g_autoDoor[i];
        const int x = idx % SIM_W, y = idx / SIM_W;
        if (w.at(x, y).mat != MAT_DOOR_OPEN) continue;
        if (patchNearAnyOccupant(w, x, y)) {
            g_autoDoor[kept++] = idx;
            continue;
        }
        if (doorToggle(w, x, y) == 0) g_autoDoor[kept++] = idx;
    }
    g_autoDoorN = kept;
}

void doorAuto(World& w, const Player& p) {
    doorAutoOpen(w, p);
    /* The compatibility form is a complete single-player pass. Server code
       calls Open for every player, then Close once after all occupancy boxes
       have been updated. */
    if (!playerNearOpenDoor(w, p)) doorAutoClose(w);
}
