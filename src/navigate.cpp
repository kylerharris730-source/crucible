#include "world.h"
#include "materials.h"
#include "player.h"
#include "navigate.h"
#include <string.h>

/* --- the tuning, and what each number is actually describing -----------------

   NAV_PERIOD is how stale a route may get. The field is rebuilt on a clock
   rather than when the terrain changes, because in a falling-sand world the
   terrain changes every frame and "rebuild on change" means "rebuild always".
   Twelve frames is a fifth of a second, and a creature acting on a fifth-of-a-
   second-old map is not a creature anyone can see is wrong.

   NAV_CLIMB is the real capability of a walker, converted. moveAxis steps a
   creature up d.h/2 -- eleven cells for a husk -- and a husk's hop clears
   about thirteen (vy 2.2 against ENT_GRAVITY 0.18). Three nodes is twelve
   cells, which sits between the two. Raise it and the field will happily route
   things up walls they cannot climb; lower it and they refuse stairs they can.

   NAV_LEVEL is how far above the floor a sideways move may happen. It is what
   keeps routes hugging surfaces instead of striking out across open air: a
   walker crossing a chasm does not cross it, it falls into it. Falls are not
   restricted this way -- see the edge rules below -- because falling IS how a
   walker gets down, and it survives any drop. */
static const int NAV_PERIOD = 12;
static const int NAV_CLIMB  = 3;
static const int NAV_LEVEL  = 2;
static const int NAV_HEIGHT[NAV_CLASSES] = { 3, 6 };   /* nodes: 12 and 24 cells */

static const u16 NAV_FAR = 0xFFFF;

static u8  g_open [NAV_W * NAV_H];   /* no solid cell anywhere in the block */
static u8  g_head [NAV_W * NAV_H];   /* open nodes ending here, counting UP */
static u8  g_floor[NAV_W * NAV_H];   /* open nodes between here and the floor */
static u16 g_dist [NAV_CLASSES][NAV_W * NAV_H];
static int g_queue[NAV_W * NAV_H];
static int g_reached[NAV_CLASSES];

static int  g_navX0 = 0, g_navY0 = 0;   /* world cell of node (0,0) */
static bool g_navValid = false;
static int  g_navClock = 0;

void navReset() {
    g_navValid = false;
    g_navClock = 0;
    for (int c = 0; c < NAV_CLASSES; ++c) g_reached[c] = 0;
}

int navReached(int cls) {
    if (cls < 0 || cls >= NAV_CLASSES) return 0;
    return g_reached[cls];
}

/* Diagnostics only. -1 for "outside the window", -2 for "no route". */
int navDistAt(int worldX, int worldY, int cls) {
    if (!g_navValid || cls < 0 || cls >= NAV_CLASSES) return -1;
    const int nx = (worldX - g_navX0) >> NAV_SHIFT;
    const int ny = (worldY - g_navY0) >> NAV_SHIFT;
    if (nx < 0 || nx >= NAV_W || ny < 0 || ny >= NAV_H) return -1;
    const u16 d = g_dist[cls][ny * NAV_W + nx];
    return d == NAV_FAR ? -2 : (int)d;
}

int navFloorAt(int worldX, int worldY) {
    if (!g_navValid) return -1;
    const int nx = (worldX - g_navX0) >> NAV_SHIFT;
    const int ny = (worldY - g_navY0) >> NAV_SHIFT;
    if (nx < 0 || nx >= NAV_W || ny < 0 || ny >= NAV_H) return -1;
    return g_open[ny * NAV_W + nx] ? (int)g_floor[ny * NAV_W + nx] : -3;
}

bool navCovers(int worldX, int worldY) {
    if (!g_navValid) return false;
    const int nx = (worldX - g_navX0) >> NAV_SHIFT;
    const int ny = (worldY - g_navY0) >> NAV_SHIFT;
    return nx >= 0 && nx < NAV_W && ny >= 0 && ny < NAV_H;
}

/* --- reading the terrain ---------------------------------------------------
   A node is open only if EVERY cell in its block is passable. Conservative on
   purpose, and cheap despite the sixteen samples because it stops at the first
   solid one -- rock, which is most of the window, costs a single test.

   playerSolid rather than a fresh opinion about what blocks a creature: it is
   the same function moveAxis reaches through solidBox, so the field and the
   collision agree by construction. A router that disagreed with the physics
   about platforms, or pipes, or powder in free fall would send creatures into
   things they then bounce off, and the bug would look like bad routing and
   actually be two tables drifting apart. */
static bool blockOpen(const World& w, int wx, int wy) {
    for (int y = 0; y < NAV; ++y)
        for (int x = 0; x < NAV; ++x)
            if (playerSolid(w, wx + x, wy + y, SOLID_ANY)) return false;
    return true;
}

static void readTerrain(const World& w) {
    for (int ny = 0; ny < NAV_H; ++ny) {
        const int wy = g_navY0 + (ny << NAV_SHIFT);
        for (int nx = 0; nx < NAV_W; ++nx)
            g_open[ny * NAV_W + nx] =
                blockOpen(w, g_navX0 + (nx << NAV_SHIFT), wy) ? 1 : 0;
    }

    /* Headroom, swept downward: how many open nodes end at this one counting
       upward. An agent H nodes tall standing here fills this node and the H-1
       above it, so it fits exactly when g_head >= H. Solid resets the run to
       zero, and the row above the window is treated as solid, which is the
       conservative direction to be wrong in. */
    for (int nx = 0; nx < NAV_W; ++nx) {
        int run = 0;
        for (int ny = 0; ny < NAV_H; ++ny) {
            const int i = ny * NAV_W + nx;
            run = g_open[i] ? run + 1 : 0;
            g_head[i] = (u8)(run > 255 ? 255 : run);
        }
    }

    /* Height above the floor, swept upward: open nodes between this one and
       the first solid below it, so a node resting directly on rock is 0. The
       row below the window counts as grounded rather than as a bottomless pit,
       so a creature at the very bottom edge still gets level moves. */
    for (int nx = 0; nx < NAV_W; ++nx) {
        int air = 0;
        for (int ny = NAV_H - 1; ny >= 0; --ny) {
            const int i = ny * NAV_W + nx;
            if (!g_open[i]) { air = 0; g_floor[i] = 255; continue; }
            g_floor[i] = (u8)(air > 255 ? 255 : air);
            if (air < 255) ++air;
        }
    }
}

/* --- the search ------------------------------------------------------------

   Breadth-first from the players OUTWARD, which means every edge it walks is
   the REVERSE of a move a creature would make. That inversion is the only
   subtle thing in this file, so the rules are written from the creature's side
   and then read backwards.

   A creature moves from `a` into an adjoining `b` when:

     b is BELOW a   -- it FELL, and it may always fall: any drop is survivable
                       and gravity does the work
     b is LEVEL     -- it WALKED, which requires it to have been standing, so
                       `a` must be near the floor (NAV_LEVEL). Note this is a
                       condition on where it came FROM, not on where it lands:
                       walking off a ledge into open air is exactly this move,
                       and testing `b` instead forbids it. That mistake cost a
                       run of the harness -- the husk stood at the lip of a
                       shaft it was supposed to step into and never moved.
     b is ABOVE a   -- it CLIMBED, so it must have been standing (`a` near the
                       floor) and must arrive somewhere a step or a hop can
                       actually reach (`b` within NAV_CLIMB of the floor). That
                       second half is what makes it take stairs and refuse
                       sheer walls.

   Read backwards from b, none of those becomes harder: each is a test on one
   of the two nodes, both of which are in hand. That is what makes the reverse
   sweep exactly as cheap as a forward one, and it is why the field can serve
   every creature at once. */
static void sweep(int cls, const int* seeds, int seedCount) {
    u16* dist = g_dist[cls];
    const int H = NAV_HEIGHT[cls];
    for (int i = 0; i < NAV_W * NAV_H; ++i) dist[i] = NAV_FAR;

    int head = 0, tail = 0;
    for (int s = 0; s < seedCount; ++s) {
        const int i = seeds[s];
        if (i < 0 || dist[i] != NAV_FAR) continue;
        if (!g_open[i] || g_head[i] < H) continue;
        dist[i] = 0;
        g_queue[tail++] = i;
    }

    while (head < tail) {
        const int b = g_queue[head++];
        const int bx = b % NAV_W, by = b / NAV_W;
        const u16 nd = (u16)(dist[b] + 1);
        const bool bClimbable = g_floor[b] <= NAV_CLIMB;

        for (int dy = -1; dy <= 1; ++dy) {
            /* dy is where `a` sits relative to b, so it reads inverted: a
               ABOVE b (dy < 0) means the creature fell from a into b. */
            if (dy > 0 && !bClimbable) continue;      /* it climbed up into b */
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                const int ax = bx + dx, ay = by + dy;
                if (ax < 0 || ax >= NAV_W || ay < 0 || ay >= NAV_H) continue;
                const int a = ay * NAV_W + ax;
                if (dist[a] != NAV_FAR) continue;
                if (!g_open[a] || g_head[a] < H) continue;
                /* Walking and climbing both start from a standing creature.
                   Falling does not, which is why dy < 0 skips this. */
                if (dy >= 0 && g_floor[a] > NAV_LEVEL) continue;
                dist[a] = nd;
                g_queue[tail++] = a;
            }
        }
    }
    g_reached[cls] = tail;
}

void navUpdate(const World& w, const float* seedX, const float* seedY, int seeds) {
    if (seeds <= 0) { g_navValid = false; return; }
    if (g_navValid && --g_navClock > 0) return;
    g_navClock = NAV_PERIOD;

    /* Centred on the FIRST seed, which is the player whose view this is. With
       several players the window still follows one of them; the others seed
       the search wherever they fall inside it, and a creature standing next to
       a player who is outside the window keeps the straight-line chase it has
       today. */
    const int cx = (int)seedX[0], cy = (int)seedY[0];
    g_navX0 = ((cx - (NAV_W << NAV_SHIFT) / 2) >> NAV_SHIFT) << NAV_SHIFT;
    g_navY0 = ((cy - (NAV_H << NAV_SHIFT) / 2) >> NAV_SHIFT) << NAV_SHIFT;

    readTerrain(w);

    int idx[16];
    int n = 0;
    for (int s = 0; s < seeds && n < 8; ++s) {
        const int nx = ((int)seedX[s] - g_navX0) >> NAV_SHIFT;
        const int ny = ((int)seedY[s] - g_navY0) >> NAV_SHIFT;
        if (nx < 0 || nx >= NAV_W || ny < 0 || ny >= NAV_H) continue;
        idx[n++] = ny * NAV_W + nx;
    }
    /* A player standing with their feet in the floor block -- which is where
       feet normally are -- would seed a solid node and the search would die on
       the spot. So each seed also offers the node above it and sweep() takes
       whichever is actually open. */
    for (int s = 0, had = n; s < had; ++s)
        if (idx[s] >= NAV_W) idx[n++] = idx[s] - NAV_W;

    for (int c = 0; c < NAV_CLASSES; ++c) sweep(c, idx, n);
    g_navValid = true;
}

bool navHeading(int footX, int footY, int agentH, int* dirX, bool* climb) {
    *dirX = 0;
    *climb = false;
    if (!g_navValid) return false;

    const int cls = agentH > (NAV_HEIGHT[NAV_SHORT] << NAV_SHIFT) ? NAV_TALL : NAV_SHORT;
    const u16* dist = g_dist[cls];
    const int H = NAV_HEIGHT[cls];

    const int nx = (footX - g_navX0) >> NAV_SHIFT;
    const int ny = (footY - g_navY0) >> NAV_SHIFT;
    if (nx < 1 || nx >= NAV_W - 1 || ny < 1 || ny >= NAV_H - 1) return false;

    /* The creature's own node is often solid -- its feet stand IN the floor
       block -- so the node above is offered as well, and the route is read
       from whichever of the two the field actually knows about. */
    int here = ny * NAV_W + nx;
    if (dist[here] == NAV_FAR && dist[here - NAV_W] != NAV_FAR) here -= NAV_W;
    if (dist[here] == NAV_FAR) return false;

    const int hx = here % NAV_W, hy = here / NAV_W;
    int best = here, bestDx = 0;
    u16 bestD = dist[here];
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            const int i = (hy + dy) * NAV_W + (hx + dx);
            if (!g_open[i] || g_head[i] < H) continue;
            /* Strictly closer wins, and on a TIE a step straight down beats
               a diagonal one. That preference is load-bearing rather than
               cosmetic. A wide shaft gives its whole width the same distance,
               so a creature at the lip sees down and down-left and down-right
               all equal; picking a diagonal invents a horizontal instruction
               out of an arbitrary loop order, and since the loop order does
               not change while the creature drifts, it invents the OPPOSITE
               one a cell later. Measured, that was a husk oscillating over a
               three-cell span at the shaft mouth for 3,800 frames.

               Reporting no horizontal preference is the honest answer, and it
               is also the useful one: the caller stops steering, the creature
               carries the momentum that brought it here, and it walks off the
               ledge it was already walking toward. */
            const bool better = dist[i] < bestD
                             || (dist[i] == bestD && dx == 0 && bestDx != 0
                                 && best != here);
            if (!better) continue;
            bestD = dist[i]; best = i; bestDx = dx;
        }
    if (best == here) return false;    /* standing on the player, or in a pit */

    const int bx = best % NAV_W, by = best / NAV_W;
    *dirX  = bx > hx ? 1 : (bx < hx ? -1 : 0);
    *climb = by < hy;
    return true;
}
