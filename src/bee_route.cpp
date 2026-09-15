#include "bee_route.h"
#include "world.h"
#include "player.h"     /* playerSolid and solidBox: a bee agrees with the player
                           about what is solid, like every other creature */
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* See bee_route.h for why this is a field and not a path. */

/* A node is two cells. A bee is four, and a node stands for the bee's body
   CENTRED on it: node n has its centre at cell 2n+1 and the body over cells
   2n-1 .. 2n+2, which is exactly where Entity::centreX puts a four-cell body. */
static const int CELL = 2;

/* The window, in nodes either side of the root: 240 cells. The flower search it
   replaced looked 150 cells out in a straight line, and a route round an
   obstacle bends outside that, so this is that radius and room to go round. */
static const int HALF = 120;
static const int SPAN = HALF * 2;

/* How stale a field may get, in frames. Terrain in a sand game moves, and the
   hive's own door rises as its wax stacks up, so a field is rebuilt on a clock
   rather than trusted forever. Two seconds is long for a falling pile and
   short for a base, and a bee that meets a change in between is still steering
   with local avoidance on top of this. */
static const int REBUILD = 120;

/* How far ahead along its route a bee looks for a point to fly at. */
static const int AHEAD = 24;

/* Fields cached at once. One per hive with bees out; a base with more hives
   than this rebuilds the least recently used, which costs time but never
   correctness. */
static const int FIELDS = 8;

static const u16 UNREACHED = 0xFFFF;

/* The nearest flowers the search reached, in the order it reached them. */
static const int GOALS = 32;
struct Goal { int nx, ny; int fx, fy; u16 dist; };

struct Field {
    bool used;
    int  key;
    int  rootNX, rootNY;     /* the node the search started from */
    int  ox, oy;             /* world node of dist[0] */
    u32  built, lastUse;
    int  goals;
    Goal goal[GOALS];
    u16  dist[SPAN * SPAN];
};

static Field g_fields[FIELDS];
static u32   g_clock = 0;
static int   g_builds = 0;
static u32   g_lastBuild = 0xFFFFFFFFu;   /* the clock at the last build */

void beeRouteTick() { ++g_clock; }

void beeRouteReset() {
    for (int i = 0; i < FIELDS; ++i) g_fields[i].used = false;
    g_builds = 0;
}

int beeRouteBuilds() { return g_builds; }

static int nodeOf(float c) { return (int)floor((c - 1.0f) * 0.5f + 0.5f); }
static float centreOf(int n) { return (float)(n * CELL + 1); }

/* --- building ---------------------------------------------------------------- */

/* Scratch for a build, shared: only one field is ever being built. The solid
   grid is read ONCE into a summed-area table, so "does a bee fit at this node"
   is four lookups rather than sixteen cell reads, and the terrain -- which is
   the expensive half -- is read exactly once per build. */
static const int GRID = SPAN * CELL + 4;
static int   g_sat[(GRID + 1) * (GRID + 1)];
static u8    g_flowerNear[SPAN * SPAN];
static int   g_flowerCell[SPAN * SPAN];
static int   g_queue[SPAN * SPAN];

static void build(const World& w, Field& f, int rootNX, int rootNY) {
    ++g_builds;
    g_lastBuild = g_clock;
    f.rootNX = rootNX; f.rootNY = rootNY;
    f.ox = rootNX - HALF; f.oy = rootNY - HALF;
    f.built = g_clock;
    f.goals = 0;

    /* Cell (gx, gy) of the grid is world cell (cx0 + gx, cy0 + gy). */
    const int cx0 = f.ox * CELL - 1, cy0 = f.oy * CELL - 1;
    memset(g_flowerNear, 0, sizeof(g_flowerNear));
    for (int gy = 0; gy <= GRID; ++gy) g_sat[gy * (GRID + 1)] = 0;
    for (int gx = 0; gx <= GRID; ++gx) g_sat[gx] = 0;
    for (int gy = 0; gy < GRID; ++gy) {
        const int wy = cy0 + gy;
        int row = 0;
        for (int gx = 0; gx < GRID; ++gx) {
            const int wx = cx0 + gx;
            bool solid;
            if (wx < PLAY_X0 || wx > PLAY_X1 || wy < PLAY_Y0 || wy > PLAY_Y1) {
                solid = true;
            } else {
                solid = playerSolid(w, wx, wy);
                if (w.at(wx, wy).mat == MAT_FLOWER) {
                    /* Every node whose bee would be within arrival range of
                       this flower -- two cells either way, inside the three a
                       bee needs to be to land. */
                    for (int ny = nodeOf((float)wy - 2.0f); ny <= nodeOf((float)wy + 2.0f); ++ny)
                        for (int nx = nodeOf((float)wx - 2.0f); nx <= nodeOf((float)wx + 2.0f); ++nx) {
                            const float ddx = centreOf(nx) - (float)wx;
                            const float ddy = centreOf(ny) - (float)wy;
                            if (fabsf(ddx) > 2.0f || fabsf(ddy) > 2.0f) continue;
                            const int lx = nx - f.ox, ly = ny - f.oy;
                            if (lx < 0 || ly < 0 || lx >= SPAN || ly >= SPAN) continue;
                            g_flowerNear[ly * SPAN + lx] = 1;
                            g_flowerCell[ly * SPAN + lx] = wy * SIM_W + wx;
                        }
                }
            }
            row += solid ? 1 : 0;
            g_sat[(gy + 1) * (GRID + 1) + gx + 1] = g_sat[gy * (GRID + 1) + gx + 1] + row;
        }
    }

    /* Node (lx, ly)'s body covers grid cells 2lx .. 2lx+3 (the grid starts one
       cell before node 0's body). */
    struct Fit {
        static bool at(int lx, int ly) {
            const int x0 = lx * CELL, y0 = ly * CELL, x1 = x0 + 4, y1 = y0 + 4;
            const int s = g_sat[y1 * (GRID + 1) + x1] - g_sat[y0 * (GRID + 1) + x1]
                        - g_sat[y1 * (GRID + 1) + x0] + g_sat[y0 * (GRID + 1) + x0];
            return s == 0;
        }
    };

    for (int i = 0; i < SPAN * SPAN; ++i) f.dist[i] = UNREACHED;

    /* The root is the door, and the door is chosen to have room -- but a
       four-cell body snapped to a two-cell grid can still miss by one, so take
       the nearest node that fits. */
    int sx = -1, sy = -1;
    for (int r = 0; r <= 4 && sx < 0; ++r)
        for (int dy = -r; dy <= r && sx < 0; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (imax(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy) != r) continue;
                if (Fit::at(HALF + dx, HALF + dy)) { sx = HALF + dx; sy = HALF + dy; break; }
            }
    if (sx < 0) return;

    int head = 0, tail = 0;
    f.dist[sy * SPAN + sx] = 0;
    g_queue[tail++] = sy * SPAN + sx;
    static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    static const int DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    while (head < tail) {
        const int i = g_queue[head++];
        const int lx = i % SPAN, ly = i / SPAN;
        const u16 d = f.dist[i];
        if (g_flowerNear[i] && f.goals < GOALS) {
            Goal& g = f.goal[f.goals++];
            g.nx = lx + f.ox; g.ny = ly + f.oy;
            g.fx = g_flowerCell[i] % SIM_W; g.fy = g_flowerCell[i] / SIM_W;
            g.dist = d;
        }
        if (d >= UNREACHED - 1) continue;
        for (int k = 0; k < 8; ++k) {
            const int nx = lx + DX[k], ny = ly + DY[k];
            if (nx < 0 || ny < 0 || nx >= SPAN || ny >= SPAN) continue;
            const int j = ny * SPAN + nx;
            if (f.dist[j] != UNREACHED) continue;
            if (!Fit::at(nx, ny)) continue;
            /* A diagonal step only where both square steps are open, or the
               route cuts a corner the body cannot. */
            if (k >= 4 && (!Fit::at(nx, ly) || !Fit::at(lx, ny))) continue;
            f.dist[j] = (u16)(d + 1);
            g_queue[tail++] = j;
        }
    }
}

static Field* fieldFor(const World& w, int key, float rootX, float rootY) {
    const int rnx = nodeOf(rootX), rny = nodeOf(rootY);
    Field* hit = 0;
    for (int i = 0; i < FIELDS; ++i)
        if (g_fields[i].used && g_fields[i].key == key) { hit = &g_fields[i]; break; }
    if (hit) {
        /* The root moved a long way -- a bee with no hive carries its own, and
           a door can shift -- or the field is old. A small shift is left to
           the clock: the old root is still next to the new one. */
        const bool moved = abs(hit->rootNX - rnx) > 8 || abs(hit->rootNY - rny) > 8;
        /* Merely OLD waits its turn: one refresh a frame, so a base of hives
           placed together does not rebuild all of them on the same frame every
           two seconds. A field whose root has moved is wrong rather than stale
           and is rebuilt at once. */
        const bool old = g_clock - hit->built >= (u32)REBUILD && g_lastBuild != g_clock;
        if (moved || old) build(w, *hit, rnx, rny);
        hit->lastUse = g_clock;
        return hit;
    }
    Field* slot = 0;
    for (int i = 0; i < FIELDS; ++i) {
        if (!g_fields[i].used) { slot = &g_fields[i]; break; }
        if (!slot || g_fields[i].lastUse < slot->lastUse) slot = &g_fields[i];
    }
    slot->used = true;
    slot->key = key;
    slot->lastUse = g_clock;
    build(w, *slot, rnx, rny);
    return slot;
}

static u16 distAt(const Field& f, int nx, int ny) {
    const int lx = nx - f.ox, ly = ny - f.oy;
    if (lx < 0 || ly < 0 || lx >= SPAN || ly >= SPAN) return UNREACHED;
    return f.dist[ly * SPAN + lx];
}

/* One step downhill, toward the root. Square steps first, so a route hugs the
   grid where it can and the diagonal is taken only where BFS itself took it. */
static bool downhill(const Field& f, int* nx, int* ny) {
    const u16 d = distAt(f, *nx, *ny);
    if (d == 0 || d == UNREACHED) return false;
    static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    static const int DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    for (int k = 0; k < 8; ++k) {
        const int x = *nx + DX[k], y = *ny + DY[k];
        if (distAt(f, x, y) != d - 1) continue;
        if (k >= 4 && (distAt(f, x, *ny) == UNREACHED || distAt(f, *nx, y) == UNREACHED))
            continue;
        *nx = x; *ny = y;
        return true;
    }
    return false;
}

/* Can the bee's whole body fly from a to b in a straight line? Two-cell steps
   of a four-cell body overlap, so nothing a cell thick slips between samples. */
static bool clearFlight(const World& w, float ax, float ay, float bx, float by) {
    const float dx = bx - ax, dy = by - ay;
    const int steps = (int)(fmaxf(fabsf(dx), fabsf(dy)) * 0.5f) + 1;
    for (int s = 1; s <= steps; ++s) {
        const float q = (float)s / (float)steps;
        if (solidBox(w, (int)(ax + dx * q) - 2, (int)(ay + dy * q) - 2, 4, 4)) return false;
    }
    return true;
}

/* The reached node nearest a point -- a bee pressed against a wall is legally
   placed but can sit between two nodes that fit. */
static bool nearestReached(const Field& f, float x, float y, int* nx, int* ny) {
    const int cx = nodeOf(x), cy = nodeOf(y);
    for (int r = 0; r <= 2; ++r) {
        int best = -1; u16 bestD = UNREACHED;
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (imax(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy) != r) continue;
                const u16 d = distAt(f, cx + dx, cy + dy);
                if (d < bestD) { bestD = d; best = (dy + 2) * 5 + (dx + 2); }
            }
        if (best >= 0) {
            *nx = cx + best % 5 - 2; *ny = cy + best / 5 - 2;
            return true;
        }
    }
    return false;
}

bool beeRouteFlower(const World& w, int key, float rootX, float rootY,
                    int* flowerX, int* flowerY) {
    Field* f = fieldFor(w, key, rootX, rootY);
    int n = 0;
    for (int i = 0; i < f->goals; ++i) {
        if (f->goal[i].dist > f->goal[0].dist + 12) break;
        /* Still a flower: the field can be two seconds old. */
        if (w.at(f->goal[i].fx, f->goal[i].fy).mat != MAT_FLOWER) continue;
        ++n;
    }
    if (n == 0) return false;
    int pick = (int)(rngNext() % (u32)n);
    for (int i = 0; i < f->goals; ++i) {
        if (w.at(f->goal[i].fx, f->goal[i].fy).mat != MAT_FLOWER) continue;
        if (pick-- == 0) { *flowerX = f->goal[i].fx; *flowerY = f->goal[i].fy; return true; }
    }
    return false;
}

bool beeRouteWaypoint(const World& w, int key, float rootX, float rootY,
                      float bx, float by, bool toFlower, int fx, int fy,
                      float* wayX, float* wayY) {
    Field* f = fieldFor(w, key, rootX, rootY);
    int bnx, bny;
    if (!nearestReached(*f, bx, by, &bnx, &bny)) return false;
    const u16 dBee = distAt(*f, bnx, bny);

    if (toFlower) {
        /* The flower's end of the chain: the nearest reached node in landing
           range of it. */
        int gnx = 0, gny = 0; u16 dGoal = UNREACHED;
        for (int ny = nodeOf((float)fy - 2.0f); ny <= nodeOf((float)fy + 2.0f); ++ny)
            for (int nx = nodeOf((float)fx - 2.0f); nx <= nodeOf((float)fx + 2.0f); ++nx) {
                if (fabsf(centreOf(nx) - (float)fx) > 2.0f ||
                    fabsf(centreOf(ny) - (float)fy) > 2.0f) continue;
                const u16 d = distAt(*f, nx, ny);
                if (d < dGoal) { dGoal = d; gnx = nx; gny = ny; }
            }
        if (dGoal != UNREACHED) {
            /* Walk down from the flower until the chain is within AHEAD of
               where the bee is, then take the furthest point of that stretch
               the bee can see. */
            int cnx = gnx, cny = gny;
            while (distAt(*f, cnx, cny) > dBee + AHEAD && downhill(*f, &cnx, &cny)) {}
            int chainX[AHEAD * 2 + 4], chainY[AHEAD * 2 + 4], n = 0;
            do {
                chainX[n] = cnx; chainY[n] = cny; ++n;
            } while (n < AHEAD * 2 + 4 && distAt(*f, cnx, cny) + 2 > dBee &&
                     downhill(*f, &cnx, &cny));
            for (int i = 0; i < n; ++i) {
                const float px = centreOf(chainX[i]), py = centreOf(chainY[i]);
                if (!clearFlight(w, bx, by, px, py)) continue;
                if (chainX[i] == gnx && chainY[i] == gny) { *wayX = (float)fx; *wayY = (float)fy; }
                else { *wayX = px; *wayY = py; }
                return true;
            }
        }
        /* Off the route, or the flower is outside what was searched: head
           for home, which is on every route, and pick the route up from
           there. */
    } else if (clearFlight(w, bx, by, rootX, rootY)) {
        *wayX = rootX; *wayY = rootY;
        return true;
    }

    int chainX[AHEAD + 1], chainY[AHEAD + 1], n = 0;
    int cnx = bnx, cny = bny;
    chainX[n] = cnx; chainY[n] = cny; ++n;
    while (n <= AHEAD && downhill(*f, &cnx, &cny)) { chainX[n] = cnx; chainY[n] = cny; ++n; }
    for (int i = n - 1; i >= 1; --i) {
        const float px = centreOf(chainX[i]), py = centreOf(chainY[i]);
        if (!clearFlight(w, bx, by, px, py)) continue;
        *wayX = px; *wayY = py;
        return true;
    }
    if (n >= 2) { *wayX = centreOf(chainX[1]); *wayY = centreOf(chainY[1]); return true; }
    return false;
}
