#include "tree.h"
#include <string.h>

Tree g_trees[MAX_TREES];

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
    if (w.at(x, y).mat != MAT_TREESEED) return false;
    /* Resting ON the soil, not buried in it: the cell below must be wet ground
       and the cell above must be open, or a seed swallowed by a landslide
       sprouts a tree through six metres of rock. */
    if (!wetSoil(w, x, y + 1)) return false;
    return w.at(x, y - 1).mat == MAT_EMPTY;
}

bool treePlant(World& w, int x, int y) {
    for (int i = 0; i < MAX_TREES; ++i) {
        if (g_trees[i].used) continue;
        Tree& t = g_trees[i];
        t.x = x; t.y = y;
        t.step = 0; t.tick = 0;
        /* Seeded on WHERE, not on when or on a counter, so the same spot always
           grows the same tree -- see the note in tree.h. */
        t.salt = (u32)(x * 73856093) ^ (u32)(y * 19349663);
        t.used = true;
        w.setCell(x, y, MAT_SAPLING);
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
static int trunkHeight(u32 salt) {
    return TREE_MIN_H + (int)pick(salt, 1u, TREE_MAX_H - TREE_MIN_H + 1);
}

/* Horizontal offset of the trunk at height h above the base. A slow lean, so a
   stand of trees does not read as a row of posts. */
static int trunkLean(u32 salt, int h, int height) {
    const int amp = 15 + (int)pick(salt, 2u, 30);
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
    if (overGrow && (cur == MAT_LEAF || cur == MAT_SEEDPOD)) w.setCell(x, y, mat);
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
static void buildTree(World& w, int bx, int by, u32 salt, float from, float frac) {
    if (frac <= 0.0f) return;
    if (frac > 1.0f) frac = 1.0f;
    if (from < 0.0f) from = 0.0f;

    const int height = trunkHeight(salt);

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
        const int tx = bx + trunkLean(salt, h, height);
        const int ty = by - h;
        const int full = TREE_W_BASE - (TREE_W_BASE - TREE_W_TOP) * h / imax(1, height);
        int wide = (int)((float)full * (0.35f + 0.65f * up));
        if (wide < 2) wide = 2;
        for (int k = 0; k < wide; ++k) put(w, tx + k - wide / 2, ty, MAT_WOOD, true);
    }
    if (out <= 0.0f) {
        /* Still climbing: cap the stem with a tuft so a half-grown tree looks
           like a young tree rather than an amputated one. Sized with the
           trunk, so the tuft grows too. */
        const int tx = bx + trunkLean(salt, grown, height);
        const int ty = by - grown;
        const int r  = 4 + (int)(10.0f * up);
        for (int dy = -r; dy <= r / 2; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                if (dx * dx + dy * dy <= r * r) put(w, tx + dx, ty + dy, MAT_LEAF, false);
        return;
    }

    /* --- branches ---------------------------------------------------------
       Off the upper half only, alternating sides, each one shorter than the
       trunk is tall at that point. Trees do not branch at ground level and a
       tree that did would be a bush. */
    const int nBranch = 4 + (int)pick(salt, 4u, 5);
    for (int b = 0; b < nBranch; ++b) {
        const int h  = height / 2 + (int)pick(salt, 10u + (u32)b, (u32)imax(1, height / 2 - 20));
        const int dir = ((int)pick(salt, 40u + (u32)b, 2)) ? 1 : -1;
        const int full = 30 + (int)pick(salt, 70u + (u32)b, 50);
        /* Branches extend with the crown, so the tree spreads rather than
           unfolding limbs it did not have a moment ago. */
        const int len = (int)((float)full * out);
        if (len < 2) continue;
        const int tx = bx + trunkLean(salt, h, height);
        const int ty = by - h;
        for (int s = 1; s <= len; ++s) {
            /* Rising as it goes out, which is what makes a branch read as a
               branch rather than as a shelf. Thicker at the trunk end, on the
               same reasoning as the trunk taper. */
            const int bw = 1 + (3 * (len - s)) / imax(1, len);
            for (int k = 0; k <= bw; ++k)
                put(w, tx + dir * s, ty - s / 2 + k, MAT_WOOD, true);
        }
        /* A tuft at the end of each. */
        const int ex = tx + dir * len, ey = ty - len / 2;
        const int tr = (int)(20.0f * out);
        for (int dy = -tr; dy <= tr; ++dy)
            for (int dx = -tr; dx <= tr; ++dx)
                if (dx * dx + dy * dy <= tr * tr) put(w, ex + dx, ey + dy, MAT_LEAF, false);
    }

    /* --- the canopy -------------------------------------------------------
       Three overlapping lumps rather than one disc. A single circle of leaves
       reads as a lollipop; three offset ones at different radii read as
       foliage, and it costs nothing but two more hash draws. */
    const int cx = bx + trunkLean(salt, height, height);
    const int cy = by - height;
    for (int lump = 0; lump < 5; ++lump) {
        const int rFull = 45 + (int)pick(salt, 100u + (u32)lump, 36);
        const int r  = (int)((float)rFull * out);
        if (r < 2) continue;
        const int ox = (int)pick(salt, 110u + (u32)lump, 75) - 37;
        const int oy = (int)pick(salt, 120u + (u32)lump, 55) - 35;
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
                put(w, cx + ox + dx, cy + oy + dy, MAT_LEAF, false);
            }
    }

    /* --- the pods ---------------------------------------------------------
       Placed LAST and only over leaves that exist, so a pod is always genuinely
       part of the canopy rather than hanging in the air where a lump happened
       not to reach. */
    if (out < 1.0f) return;   /* pods last, on a finished crown */
    const int nPod = TREE_POD_MIN + (int)pick(salt, 5u, TREE_POD_MAX - TREE_POD_MIN + 1);
    int placed = 0;
    for (int tryN = 0; tryN < 400 && placed < nPod; ++tryN) {
        const int px = cx + (int)pick(salt, 200u + (u32)tryN, 190) - 95;
        const int py = cy + (int)pick(salt, 300u + (u32)tryN, 150) - 100;
        if (px < PLAY_X0 || px > PLAY_X1 || py < PLAY_Y0 || py > PLAY_Y1) continue;
        if (w.at(px, py).mat != MAT_LEAF) continue;
        w.setCell(px, py, MAT_SEEDPOD);
        ++placed;
    }
}

void treeGrowNow(World& w, int x, int y, u32 salt) {
    /* From nothing to everything in one call, so no ring is skipped. */
    buildTree(w, x, y, salt, 0.0f, 1.0f);
}

void treesTick(World& w) {
    /* --- root anything that has landed --------------------------------
       The list is whatever the simulation reported this frame; the decision
       about whether each one qualifies is made here, because the moisture rule
       is a tree rule. Drained unconditionally, even where planting is refused,
       or a seed that failed once would be re-offered every frame for ever. */
    for (int s = 0; s < w.sproutCount; ++s) {
        const int i = w.sprout[s];
        const int x = i % SIM_W, y = i / SIM_W;
        if (!treeCanRoot(w, x, y)) continue;
        treePlant(w, x, y);
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
        if (w.at(t.x, t.y).mat != MAT_SAPLING && w.at(t.x, t.y).mat != MAT_WOOD) {
            t.used = false;
            continue;
        }

        if (++t.tick < TREE_TICK) continue;
        t.tick = 0;
        ++t.step;

        buildTree(w, t.x, t.y, t.salt,
                  (float)(t.step - 1) / (float)TREE_STEPS,
                  (float)t.step       / (float)TREE_STEPS);

        /* Retired at full size. A grown tree is just cells -- there is nothing
           left for the table to remember, and holding the row would cap how
           many trees a world may contain rather than how many may be growing. */
        if (t.step >= TREE_STEPS) t.used = false;
    }
}
