#include "tree.h"
#include <string.h>

Tree g_trees[MAX_TREES];

/* Oak first, because oak is the one the world grows on its own.

   Birch is a THIRD the height and half the crown, and that is the whole point
   of it: a wood made only of 320-cell oaks is a wood you cannot see out of.
   Its numbers are not oak's scaled down uniformly -- the trunk is slighter than
   the height reduction alone would give, which is what makes it read as a
   different tree rather than as a distant one. */
const TreeKind TREE_KINDS[TREE_SPECIES_COUNT] = {
  /* name    minH maxH wTop wBase lean   branch  branchLen tuft lumps lumpR  spread  pods */
  { "Oak",   170, 360,   9,  15,  15, 30,  4, 5,   30, 50,  20,   5,  45, 36, 75, 55,  3, 4,
    MAT_OAK_SEED,   MAT_OAK_SAPLING,   MAT_WOOD,       MAT_OAK_LEAF,   MAT_OAK_POD },
  { "Birch",  70, 120,   4,   7,   8, 16,  3, 3,   14, 20,  10,   4,  20, 14, 32, 24,  2, 3,
    MAT_BIRCH_SEED, MAT_BIRCH_SAPLING, MAT_BIRCH_WOOD, MAT_BIRCH_LEAF, MAT_BIRCH_POD },
};

int treeMaxHeight() {
    int m = 0;
    for (int i = 0; i < TREE_SPECIES_COUNT; ++i)
        if (TREE_KINDS[i].maxH > m) m = TREE_KINDS[i].maxH;
    return m;
}

int treeSpeciesOfSeed(u8 mat) {
    for (int i = 0; i < TREE_SPECIES_COUNT; ++i)
        if (TREE_KINDS[i].seed == mat) return i;
    return -1;
}

/* The same mixer worldgen uses, kept here rather than shared through a header
   because the two want it for opposite reasons -- worldgen hashes an index into
   terrain, this hashes a position into a plant -- and a shared "utility hash"
   invites somebody to change it for one caller and silently reshape the other. */
static u32 hash1(int x, u32 seed) {
    u32 h = (u32)x * 374761393u + seed * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
/* A value in [0, n) from a position and a stream label. `k` names WHICH
   question is being asked -- height, lean, this branch's length -- so two
   questions about the same tree never come back correlated. */
static u32 pick(u32 salt, u32 k, u32 n) { return n ? hash1((int)k, salt) % n : 0; }

int treeCount() {
    int n = 0;
    for (int i = 0; i < MAX_TREES; ++i) if (g_trees[i].used) ++n;
    return n;
}

void treesClear() { memset(g_trees, 0, sizeof(g_trees)); }

/* Wet enough to grow in. Half of dirt's capacity -- damp ground rather than
   merely "has ever seen water", so a seed dropped on a dry hillside sits there
   and waits, which is the mechanic the player is meant to notice: water your
   crop.

   Grass counts as well as dirt. Turf is where a seed would land in practice,
   and a rule that refused it would read as broken rather than as strict. */
static bool wetSoil(const World& w, int x, int y) {
    if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) return false;
    const Cell& c = w.at(x, y);
    if (c.mat != MAT_DIRT && c.mat != MAT_GRASS) return false;
    return (int)c.moisture * 2 >= (int)MATS[c.mat].capacity;
}

bool treeCanRoot(const World& w, int x, int y) {
    if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1 - 4) return false;
    if (treeSpeciesOfSeed(w.at(x, y).mat) < 0) return false;
    /* Resting ON the soil, not buried in it: the cell below must be wet ground
       and the cell above must be open, or a seed swallowed by a landslide
       sprouts a tree through six metres of rock. */
    if (!wetSoil(w, x, y + 1)) return false;
    return w.at(x, y - 1).mat == MAT_EMPTY;
}

bool treePlant(World& w, int x, int y, int species) {
    if (species < 0 || species >= TREE_SPECIES_COUNT) return false;
    for (int i = 0; i < MAX_TREES; ++i) {
        if (g_trees[i].used) continue;
        Tree& t = g_trees[i];
        t.kind = (u8)species;
        t.x = x; t.y = y;
        t.step = 0; t.tick = 0;
        /* Seeded on WHERE, not on when or on a counter, so the same spot always
           grows the same tree -- see the note in tree.h. */
        t.salt = (u32)(x * 73856093) ^ (u32)(y * 19349663);
        t.used = true;
        w.setCell(x, y, TREE_KINDS[species].sapling);
        return true;
    }
    return false;   /* table full: the seed stays a seed and tries again */
}

/* --- what a tree looks like -------------------------------------------------
   A leaning trunk, a few branches off it in the upper half, and a lumpy canopy
   with pods in it. Every number below comes from the salt, so the whole shape
   is decided the moment the seed lands.

   Written as "given a tree and a fraction of the way through, which cells are
   filled" rather than as an incremental builder. That is what lets growth and
   worldgen share one description: growth calls it repeatedly with a rising
   fraction, worldgen calls it once with 1.0, and there is no second copy of the
   shape to keep in step. */
static int trunkHeight(const TreeKind& k, u32 salt) {
    return k.minH + (int)pick(salt, 1u, (u32)(k.maxH - k.minH + 1));
}

/* Horizontal offset of the trunk at height h above the base. A slow lean, so a
   stand of trees does not read as a row of posts. */
static int trunkLean(const TreeKind& k, u32 salt, int h, int height) {
    const int amp = k.leanMin + (int)pick(salt, 2u, (u32)k.leanSpan);
    const int dir = pick(salt, 3u, 2) ? 1 : -1;
    /* Quadratic, so the base stays put and the top does the moving -- a trunk
       that leaned linearly would look like it had been knocked over. */
    const int num = h * h;
    const int den = height * height;
    return dir * (amp * num) / (den ? den : 1);
}

static void put(World& w, int x, int y, u8 mat, bool overGrow) {
    if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) return;
    const u8 cur = w.at(x, y).mat;
    if (cur == MAT_EMPTY) { w.setCell(x, y, mat); return; }
    /* Growing INTO something is refused, with one exception: a tree may
       overwrite its own leaves with wood, since a branch thickening through the
       canopy it already sprouted is a tree growing, not a tree eating a wall.

       Refusing everything else is what keeps a tree from bulldozing a house
       built beside it -- it grows around obstructions instead, which is both
       the polite behaviour and the one that produces more interesting shapes. */
    /* g_matSheer rather than the two names: it is exactly the set of things a
       tree grows that a branch may thicken through, and it stays right when a
       species is added. Naming the materials meant birch branches stopping dead
       at oak leaves and at their own. */
    if (overGrow && g_matSheer[cur]) w.setCell(x, y, mat);
}

/* Build the tree up to `frac` of its full size. Idempotent: calling it with a
   larger fraction adds cells and never removes any, which is exactly what an
   incremental grower needs and what makes the one-shot worldgen call correct. */
/* `from` is how far the tree had already grown, `to` is how far it has grown
   now. The pair is what lets the crown fill only the ring it has just reached
   instead of the whole disc every step -- and it must be PASSED rather than
   inferred from a step size, which is the mistake the first version made: it
   assumed one step's worth of progress, so the one-shot call used by worldgen
   (from 0 straight to 1) skipped everything inside 98% of the radius and grew
   trees whose crowns were hollow rings. They looked like wire sculptures. */
static void buildTree(World& w, int bx, int by, u32 salt, int species,
                      float from, float frac) {
    if (frac <= 0.0f) return;
    if (frac > 1.0f) frac = 1.0f;
    if (from < 0.0f) from = 0.0f;

    const TreeKind& k = TREE_KINDS[species];
    const int height = trunkHeight(k, salt);

    /* Two phases, not one. `up` is how far the trunk has risen, `out` is how
       far the crown has spread, and only one of them is moving at a time --
       see TREE_TRUNK_FRAC. */
    const float up  = frac < TREE_TRUNK_FRAC ? frac / TREE_TRUNK_FRAC : 1.0f;
    const float out = frac < TREE_TRUNK_FRAC ? 0.0f
                    : (frac - TREE_TRUNK_FRAC) / (1.0f - TREE_TRUNK_FRAC);
    /* The crown spread the tree had ALREADY reached, from the caller's `from`.
       Zero for a one-shot build, which is what makes that case fill solid. */
    const float outWas = from < TREE_TRUNK_FRAC ? 0.0f
                       : (from - TREE_TRUNK_FRAC) / (1.0f - TREE_TRUNK_FRAC);
    const int grown = (int)((float)height * up);

    /* The trunk. Tapered: TREE_W_BASE at the root down to TREE_W_TOP at the
       crown, interpolated over the height. A parallel-sided trunk at this size
       reads as a pillar rather than a tree, and the taper costs one lerp.

       The width also grows with the tree rather than being final from the
       first tick, so a young trunk is a stem that thickens. */
    for (int h = 0; h <= grown; ++h) {
        const int tx = bx + trunkLean(k, salt, h, height);
        const int ty = by - h;
        const int full = k.wBase - (k.wBase - k.wTop) * h / imax(1, height);
        int wide = (int)((float)full * (0.35f + 0.65f * up));
        if (wide < 2) wide = 2;
        for (int q = 0; q < wide; ++q) put(w, tx + q - wide / 2, ty, k.wood, true);
    }
    if (out <= 0.0f) {
        /* Still climbing: cap the stem with a tuft so a half-grown tree looks
           like a young tree rather than an amputated one. Sized with the
           trunk, so the tuft grows too.

           The tuft is the one TRANSIENT thing buildTree draws, and forgetting
           that is what furred every trunk in the game with leaves. Everything
           else here is purely additive -- put() never removes -- which is what
           makes repeated calls with a rising fraction correct. An additive
           tuft, though, is stamped fresh at the top of a stem that climbs a
           hundred and fifty times and leaves a copy behind at every height it
           passed through, so the finished tree wears a sleeve of leaves from
           root to crown.

           Measured before this: a grown birch carried 42 leaf cells within 12
           of its stem in the lowest eighth of its height, and 106 in the next,
           where a birch built in ONE call -- the worldgen path, which never
           enters this branch at all -- carried none in either. That asymmetry
           is what named the culprit. It showed worst on birch because a birch
           stem is four cells wide against a tuft twenty-eight across; oak's
           fifteen-cell trunk swallowed more of the evidence.

           So the previous tuft is erased before the new one is drawn. Only
           cells that are this species' leaf, and only inside the disc that
           tuft occupied, so a neighbour's canopy and anything the player built
           are both out of reach. The last tuft of all is deliberately left
           standing: the crown grows over it. */
        const float upWas    = from < TREE_TRUNK_FRAC ? from / TREE_TRUNK_FRAC : 1.0f;
        const int   wasGrown = (int)((float)height * upWas);
        if (wasGrown < grown) {
            const int wx = bx + trunkLean(k, salt, wasGrown, height);
            const int wy = by - wasGrown;
            const int wr = 4 + (int)(10.0f * upWas);
            for (int dy = -wr; dy <= wr / 2; ++dy)
                for (int dx = -wr; dx <= wr; ++dx) {
                    if (dx * dx + dy * dy > wr * wr) continue;
                    const int px = wx + dx, py = wy + dy;
                    if (px < PLAY_X0 || px > PLAY_X1 || py < PLAY_Y0 || py > PLAY_Y1) continue;
                    if (w.at(px, py).mat == k.leaf) w.setCell(px, py, MAT_EMPTY);
                }
        }
        const int tx = bx + trunkLean(k, salt, grown, height);
        const int ty = by - grown;
        const int r  = 4 + (int)(10.0f * up);
        for (int dy = -r; dy <= r / 2; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                if (dx * dx + dy * dy <= r * r) put(w, tx + dx, ty + dy, k.leaf, false);
        return;
    }

    /* --- branches ---------------------------------------------------------
       Off the upper half only, alternating sides, each one shorter than the
       trunk is tall at that point. Trees do not branch at ground level and a
       tree that did would be a bush. */
    const int nBranch = k.branchMin + (int)pick(salt, 4u, (u32)k.branchSpan);
    for (int b = 0; b < nBranch; ++b) {
        const int h  = height / 2 + (int)pick(salt, 10u + (u32)b, (u32)imax(1, height / 2 - 20));
        const int dir = ((int)pick(salt, 40u + (u32)b, 2)) ? 1 : -1;
        const int full = k.branchLenMin + (int)pick(salt, 70u + (u32)b, (u32)k.branchLenSpan);
        /* Branches extend with the crown, so the tree spreads rather than
           unfolding limbs it did not have a moment ago. */
        const int len = (int)((float)full * out);
        if (len < 2) continue;
        const int tx = bx + trunkLean(k, salt, h, height);
        const int ty = by - h;
        for (int s = 1; s <= len; ++s) {
            /* Rising as it goes out, which is what makes a branch read as a
               branch rather than as a shelf. Thicker at the trunk end, on the
               same reasoning as the trunk taper. */
            const int bw = 1 + ((k.wTop / 3) * (len - s)) / imax(1, len);
            for (int q = 0; q <= bw; ++q)
                put(w, tx + dir * s, ty - s / 2 + q, k.wood, true);
        }
        /* A tuft at the end of each. */
        const int ex = tx + dir * len, ey = ty - len / 2;
        const int tr = (int)((float)k.tuftR * out);
        for (int dy = -tr; dy <= tr; ++dy)
            for (int dx = -tr; dx <= tr; ++dx)
                if (dx * dx + dy * dy <= tr * tr) put(w, ex + dx, ey + dy, k.leaf, false);
    }

    /* --- the canopy -------------------------------------------------------
       Three overlapping lumps rather than one disc. A single circle of leaves
       reads as a lollipop; three offset ones at different radii read as
       foliage, and it costs nothing but two more hash draws. */
    const int cx = bx + trunkLean(k, salt, height, height);
    const int cy = by - height;
    for (int lump = 0; lump < k.lumps; ++lump) {
        const int rFull = k.lumpMin + (int)pick(salt, 100u + (u32)lump, (u32)k.lumpSpan);
        const int r  = (int)((float)rFull * out);
        if (r < 2) continue;
        const int ox = (int)pick(salt, 110u + (u32)lump, (u32)k.spreadX) - k.spreadX / 2;
        const int oy = (int)pick(salt, 120u + (u32)lump, (u32)k.spreadY) - k.spreadY * 2 / 3;
        /* Only the cells the crown has just reached. Filling the whole disc
           every step is O(r^2) per step and at r = 80 that is sixty thousand
           cell tests fifty times over per tree; the annulus is O(r) worth of
           new area and gets the identical result, because `put` never
           overwrites what earlier steps already placed. */
        const int rPrev = (int)((float)rFull * outWas);
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                const int d2 = dx * dx + dy * dy;
                if (d2 > r * r) continue;
                if (rPrev > 1 && d2 < (rPrev - 1) * (rPrev - 1)) continue;
                put(w, cx + ox + dx, cy + oy + dy, k.leaf, false);
            }
    }

    /* --- the pods ---------------------------------------------------------
       Placed LAST and only over leaves that exist, so a pod is always genuinely
       part of the canopy rather than hanging in the air where a lump happened
       not to reach. */
    if (out < 1.0f) return;   /* pods last, on a finished crown */
    const int nPod = k.podMin + (int)pick(salt, 5u, (u32)k.podSpan);
    int placed = 0;
    for (int tryN = 0; tryN < 400 && placed < nPod; ++tryN) {
        const int px = cx + (int)pick(salt, 200u + (u32)tryN,
                                     (u32)(k.spreadX + k.lumpMin * 2)) - (k.spreadX + k.lumpMin * 2) / 2;
        const int py = cy + (int)pick(salt, 300u + (u32)tryN,
                                     (u32)(k.spreadY + k.lumpMin * 2)) - (k.spreadY + k.lumpMin * 2) * 2 / 3;
        if (px < PLAY_X0 || px > PLAY_X1 || py < PLAY_Y0 || py > PLAY_Y1) continue;
        if (w.at(px, py).mat != k.leaf) continue;
        w.setCell(px, py, k.pod);
        ++placed;
    }
}

void treeGrowNow(World& w, int x, int y, u32 salt, int species) {
    if (species < 0 || species >= TREE_SPECIES_COUNT) return;
    /* From nothing to everything in one call, so no ring is skipped. */
    buildTree(w, x, y, salt, species, 0.0f, 1.0f);
}

/* --- the audit -------------------------------------------------------------
   A flood through leaves from where a wood cell just was. It stops the instant
   it touches any wood, and that early exit is what makes the whole thing
   affordable: while you are cutting a trunk there is wood a few cells away in
   every direction, so nearly every audit dies within a dozen visits. Only the
   swing that removes the LAST wood pays for a full canopy walk, and it pays
   once, because everything it condemns stops counting as a leaf.

   The visited set is a bitset rather than a flag on the cell, because a Cell
   has no spare bit -- and it is cleared by walking the queue afterwards rather
   than by wiping 1.5 MB, so an audit that visits nine cells costs nine. */
static const int MAX_BLOB = 1 << 17;     /* 131072; an oak canopy is ~28,000 */
static i32 g_bq[MAX_BLOB];
static u32 g_seen[(SIM_W * SIM_H + 31) / 32];
static int g_condemned = 0, g_visited = 0;

int treeAuditCondemned() { return g_condemned; }
int treeAuditVisited()   { return g_visited; }

static inline bool seen(i32 i)  { return (g_seen[i >> 5] >> (i & 31)) & 1u; }
static inline void mark(i32 i)  { g_seen[i >> 5] |= 1u << (i & 31); }
static inline void unmark(i32 i){ g_seen[i >> 5] &= ~(1u << (i & 31)); }

/* A leaf that is already dying is not part of anybody's canopy: it does not
   conduct support and it must not be condemned twice. That is what stops the
   second and third report from one swing costing anything. */
static inline bool liveLeaf(const World& w, i32 i) {
    return g_matIsLeaf[w.cells[i].mat] && w.cells[i].moisture == 0;
}

/* Flood the leaf blob containing `from`, appending everything it marks to g_bq
   at `tail`. Returns the new tail. Marks are LEFT SET: they are cleared once at
   the end of the pass, so a blob already walked this frame is skipped rather
   than walked again for every leaf of it a chunk sweep happens to find. */
static int auditBlob(World& w, i32 from, int tail) {
    static const int DX[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };
    static const int DY[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
    const int start = tail;
    mark(from); g_bq[tail++] = from;

    bool supported = false;
    int head = start;
    while (head < tail && !supported) {
        const i32 i = g_bq[head++];
        const int x = i % SIM_W, y = i / SIM_W;
        for (int k = 0; k < 8; ++k) {
            const int nx = x + DX[k], ny = y + DY[k];
            if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1) continue;
            const i32 ni = ny * SIM_W + nx;
            if (g_matIsWood[w.cells[ni].mat]) { supported = true; break; }
            if (!liveLeaf(w, ni) || seen(ni)) continue;
            if (tail >= MAX_BLOB) { supported = true; break; }  /* see below */
            mark(ni); g_bq[tail++] = ni;
        }
    }
    g_visited += tail - start;

    /* Running out of queue means a leaf mass bigger than any tree grows -- so
       somebody built it, and deleting a hundred and thirty thousand cells of
       somebody's hedge because the audit could not finish measuring it is the
       worst possible way to be wrong. Bailing counts as supported. */
    if (supported) return tail;

    /* --- when each one falls ---------------------------------------------
       Staggered by BFS ORDER, which costs nothing because the queue is already
       in it: g_bq holds the blob sorted by distance from where the wood went,
       so spreading the countdown along it makes the canopy come apart from the
       cut outward, like something letting go.

       This was a hash of the position first, and a picture is what rejected it.
       A uniform random countdown is not a shower -- every part of the crown
       thins at the same rate everywhere, so the tree does not fall, it FADES,
       and it read as the canopy being deleted with a dissolve on it. Nothing in
       the numbers could have said so: the same cells went in the same total
       time either way.

       The hash survives as a jitter of a few frames on top, so the wavefront is
       ragged rather than a line sweeping across the tree. */
    const int span = tail - start;
    for (int k = start; k < tail; ++k) {
        const i32 i = g_bq[k];
        const int wave = 1 + (int)((long long)(k - start) * (LEAF_FALL_MAX - 8) / imax(1, span));
        const int jit  = (int)(hash1(i, 0x1EAFu) % 8u);
        w.cells[i].moisture = (u8)imin(LEAF_FALL_MAX, wave + jit);
        w.dirtyPoint(i % SIM_W, i / SIM_W);
    }
    g_condemned += tail - start;
    return tail;
}

void treeAudit(World& w) {
    g_condemned = g_visited = 0;
    int tail = 0;
    for (int k = 0; k < w.felledCount; ++k) {
        const int ch = w.felled[k];
        w.felledMark[ch] = 0;
        /* The chunk plus a one-cell skirt: a leaf touching wood that was in
           this chunk can itself be in the next one. */
        const int cx = (ch % CHUNKS_X) << CHUNK_SHIFT;
        const int cy = (ch / CHUNKS_X) << CHUNK_SHIFT;
        const int x0 = imax(PLAY_X0, cx - 1), x1 = imin(PLAY_X1, cx + CHUNK);
        const int y0 = imax(PLAY_Y0, cy - 1), y1 = imin(PLAY_Y1, cy + CHUNK);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const i32 i = y * SIM_W + x;
                if (!liveLeaf(w, i) || seen(i)) continue;
                if (tail + 8 >= MAX_BLOB) { y = y1; break; }
                tail = auditBlob(w, i, tail);
            }
    }
    for (int k = 0; k < tail; ++k) unmark(g_bq[k]);
    w.felledCount = 0;
}

void treesTick(World& w) {
    treeAudit(w);

    /* --- root anything that has landed --------------------------------
       The list is whatever the simulation reported this frame; the decision
       about whether each one qualifies is made here, because the moisture rule
       is a tree rule. Drained unconditionally, even where planting is refused,
       or a seed that failed once would be re-offered every frame for ever. */
    for (int s = 0; s < w.sproutCount; ++s) {
        const int i = w.sprout[s];
        const int x = i % SIM_W, y = i / SIM_W;
        if (!treeCanRoot(w, x, y)) continue;
        treePlant(w, x, y, treeSpeciesOfSeed(w.at(x, y).mat));
    }
    w.sproutCount = 0;

    for (int i = 0; i < MAX_TREES; ++i) {
        Tree& t = g_trees[i];
        if (!t.used) continue;

        /* Dug up, burned down, or buried: the entity goes with it. Checked
           every tick rather than hooked into digging, for the reason rooms
           revalidate rather than subscribe -- there are too many ways for a
           cell to stop being what it was, and a rule that has to be told about
           all of them will miss one. */
        const TreeKind& tk = TREE_KINDS[t.kind < TREE_SPECIES_COUNT ? t.kind : 0];
        if (w.at(t.x, t.y).mat != tk.sapling && w.at(t.x, t.y).mat != tk.wood) {
            t.used = false;
            continue;
        }

        if (++t.tick < TREE_TICK) continue;
        t.tick = 0;
        ++t.step;

        buildTree(w, t.x, t.y, t.salt, t.kind,
                  (float)(t.step - 1) / (float)TREE_STEPS,
                  (float)t.step       / (float)TREE_STEPS);

        /* Retired at full size. A grown tree is just cells -- there is nothing
           left for the table to remember, and holding the row would cap how
           many trees a world may contain rather than how many may be growing. */
        if (t.step >= TREE_STEPS) t.used = false;
    }
}
