#pragma once
#include "world.h"

/* --- trees -----------------------------------------------------------------

   A seed you drop, that roots in wet soil and grows into a tree over about a
   minute. Trees are where wood comes from, and their canopies carry pods that
   drop more seeds, so a forest is something you keep rather than something you
   use up.

   --- why growth is an entity ---
   Same reason a device is (see device.h) and the same reason a room is: growth
   needs STATE. A tree part-way up is "this tall, this many branches done, this
   far through the current one", and a Cell is four bytes that all already mean
   something. The grass rule gets away with living in the grid because it has no
   memory at all -- every frame it re-asks "am I next to dirt" -- and a tree
   cannot be written that way without the trunk forgetting how tall it is.

   So a sapling is one MAT_OAK_SAPLING cell plus a row in a side table, exactly the
   arrangement machines use. The simulation is told only "something solid is
   here", which is all it needs.

   --- why the shape is generated from a hash ---
   Every tree is drawn from hash1() seeded on where it was planted, never from
   the global rng. Two reasons, and the second is the one that bites: worldgen
   already established that the global stream must not be consumed by anything
   optional, because then the world changes depending on whether a tree happened
   to be growing while you looked at it. And a tree that re-rolled its own shape
   each growth tick would writhe rather than grow.

   The consequence worth having: replanting in the same spot gives the same
   tree, which makes a deliberately laid-out orchard possible. */

/* How many plants can be growing at once. Grown ones are NOT tracked -- once
   the last branch is placed the entity is retired and the tree is just cells,
   like everything else you build. So this caps saplings in progress, not
   forests.

   64 was far more than anyone plants in a minute when the only thing that grew
   was a tree. A FIELD is the case that changed it: sowing a row of wheat is one
   sweep of the arm and a hundred seeds, and a cap of 64 would have most of them
   waiting on the ground for a slot. Overflow is handled gracefully -- the seed
   stays a seed and roots when one frees up -- but "gracefully" still means a
   field that comes up in patches. A Tree is 24 bytes, so 256 costs 6 KB. */
static const int MAX_TREES = 256;

/* Oak's timing, and the default for anything that does not say otherwise.
   24 * 150 is about a minute -- long enough that planting is a thing you come
   back to and short enough that you do come back. Unchanged when the trees were
   scaled up five times: a bigger tree taking the same minute is a tree that
   grows more visibly, which is better, and the time was already the number that
   felt right. Crops set their own; see TreeKind::tick. */
static const int TREE_TICK  = 24;
static const int TREE_STEPS = 150;

/* --- how the minute is spent -----------------------------------------------
   The trunk goes up over the first stretch and the crown fills out over the
   rest. Splitting it matters more at this scale than it did at the old one: a
   tree that grew a stick for a minute and then flashed a sixty-cell canopy into
   existence on the last tick looked like a bug, and at five times the size it
   would look like a much bigger bug. */
static const float TREE_TRUNK_FRAC = 0.65f;

/* --- the species ------------------------------------------------------------
   Every number that makes one tree look unlike another, in one row each. A
   table rather than two builders, because the two trees differ only in their
   MEASUREMENTS -- a trunk, some branches, a lumpy crown, pods in it -- and a
   second copy of that logic would be a second place for the crown to go hollow.

   Adding a species is a row here plus its five materials.

   "Tree" is the file's name rather than the table's limit. A wheat plant is a
   short thin trunk with a tuft on top and grain in the tuft, which is this
   description with small numbers in it -- so the crops are rows here too, and
   the alternative would have been a second grower with its own version of
   every bug this one has already had. Two things make it fit: a crop's leaf
   material and its wood material are the SAME stalk, and its pods are the
   harvest.

   `tick` and `steps` are per species because a crop that took a tree's minute
   would be a crop nobody plants twice. */
struct TreeKind {
    const char* name;
    int tick, steps;       /* frames per growth step, and how many steps */
    int minH, maxH;        /* trunk height before the crown */
    int wTop, wBase;       /* trunk width at the crown and at the root */
    int leanMin, leanSpan; /* how far the top wanders off the root */
    int branchMin, branchSpan;
    int branchLenMin, branchLenSpan;
    int tuftR;             /* leaves at a branch tip */
    int lumps;             /* overlapping blobs making the crown */
    int lumpMin, lumpSpan; /* their radii */
    int spreadX, spreadY;  /* how far they scatter from the trunk top */
    int podMin, podSpan;
    u8  seed, sapling, wood, leaf, pod;
};

enum TreeSpecies {
    TREE_OAK = 0, TREE_BIRCH,
    PLANT_WHEAT, PLANT_FLAX, PLANT_COTTON,
    TREE_SPECIES_COUNT
};
extern const TreeKind TREE_KINDS[TREE_SPECIES_COUNT];

/* The tallest any species grows, for callers that need headroom before they
   know which tree they are placing -- worldgen, mostly. */
int treeMaxHeight();

/* Which species a seed material grows, or -1. */
int treeSpeciesOfSeed(u8 mat);

struct Tree {
    u8   kind;         /* a TreeSpecies */
    i32  x, y;         /* the cell the sapling stands in: the base of the trunk */
    i32  step;         /* 0..TREE_KINDS[kind].steps */
    i32  tick;
    u32  salt;         /* the hash seed; see the note above on determinism */
    bool used;
};

extern Tree g_trees[MAX_TREES];

int  treeCount();
void treesClear();

/* Can a seed at (x,y) take root? True when it is resting on soil that is WET
   -- moist dirt or grass. Exposed as its own question because it is the whole
   of the planting rule and a test should be able to ask it directly. */
bool treeCanRoot(const World& w, int x, int y);

/* Plant one. Converts the cell to MAT_OAK_SAPLING and opens a row in the table.
   Returns false if the table is full, in which case the seed stays a seed and
   will try again next time it is looked at -- a dropped seed that silently
   vanished would be the worst possible failure here. */
bool treePlant(World& w, int x, int y, int species);

/* --- leaves without a tree -------------------------------------------------
   A leaf lives as long as something joins it to wood. Connection is through
   OTHER LEAVES as well as directly, without any distance limit, and that is
   the one place this deliberately differs from the game it is borrowed from.

   Minecraft caps the distance at four, which works because a Minecraft crown is
   about five blocks across. These crowns are two hundred. Measured on grown
   oaks, the deepest leaf sits 154 cells from the nearest wood, so any cap small
   enough to be useful would delete most of a healthy tree and any cap large
   enough to spare one would never fire. The cap is not a design choice being
   copied, it is a number scaled to a block size this game does not have.

   Unlimited connectivity is also simply the honest rule: a leaf belongs to a
   tree if you can get from it to the trunk without leaving the tree.

   Triggered by World::felled, never by the leaves. See the note there. */
void treeAudit(World& w);

/* One frame: roots any seed that has come to rest on wet soil, advances every
   growing tree, and condemns any canopy that has just lost its last wood.
   Cheap by construction -- the seed scan only looks at cells the simulation
   has already woken, and the audit only runs where wood has just gone. */
void treesTick(World& w);

/* How many leaf cells the last audit condemned, and how many cells it had to
   visit to decide. Published so a test can measure the cost of the thing that
   is meant to be cheap rather than assume it. */
int  treeAuditCondemned();
int  treeAuditVisited();

/* Grow a full tree at (x, y) immediately, ignoring the timer. Used by worldgen
   to put forests in the world at generation time, and by tests that have no
   interest in waiting a minute. */
void treeGrowNow(World& w, int x, int y, u32 salt, int species = TREE_OAK);
