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

   So a sapling is one MAT_SAPLING cell plus a row in a side table, exactly the
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

/* How many trees can be growing at once. Grown trees are NOT tracked -- once
   the last branch is placed the entity is retired and the tree is just cells,
   like everything else you build. So this caps saplings in progress, not
   forests, and 64 is far more than anyone plants in one minute. */
static const int MAX_TREES = 64;

/* Frames between growth steps, and how many steps a tree takes. 24 * 150 is
   about a minute -- long enough that planting is a thing you come back to and
   short enough that you do come back. Unchanged when the trees were scaled up
   five times: a bigger tree taking the same minute is a tree that grows more
   visibly, which is better, and the time was already the number that felt
   right. */
static const int TREE_TICK  = 24;
static const int TREE_STEPS = 150;

/* --- how the minute is spent -----------------------------------------------
   The trunk goes up over the first stretch and the crown fills out over the
   rest. Splitting it matters more at this scale than it did at the old one: a
   tree that grew a stick for a minute and then flashed a sixty-cell canopy into
   existence on the last tick looked like a bug, and at five times the size it
   would look like a much bigger bug. */
static const float TREE_TRUNK_FRAC = 0.65f;

/* Trunk height, in cells, before the crown. Five times what it was.

   The character is 22 cells tall, so the short end of this is eight body
   heights and the tall end is sixteen -- a thing you climb, or rope up, or fell
   for a great deal of wood. At the old 34-72 a tree was chest-high to
   waist-high scenery. */
static const int TREE_MIN_H = 170;
static const int TREE_MAX_H = 360;

/* Trunk width at the top and at the base. A 170-cell tree one cell thick would
   be a wire; these are the numbers that make it read as timber. */
static const int TREE_W_TOP  = 9;
static const int TREE_W_BASE = 15;

/* How many pods the crown carries. Up from 2-4 with the size, but not by five:
   the point of a pod is that felling one tree pays for several more, and past
   about six that stops being a supply and starts being a reason never to plant
   anything again. */
static const int TREE_POD_MIN = 3;
static const int TREE_POD_MAX = 6;

struct Tree {
    i32  x, y;         /* the cell the sapling stands in: the base of the trunk */
    i32  step;         /* 0..TREE_STEPS */
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

/* Plant one. Converts the cell to MAT_SAPLING and opens a row in the table.
   Returns false if the table is full, in which case the seed stays a seed and
   will try again next time it is looked at -- a dropped seed that silently
   vanished would be the worst possible failure here. */
bool treePlant(World& w, int x, int y);

/* One frame: roots any seed that has come to rest on wet soil, and advances
   every growing tree. Cheap by construction -- the seed scan only looks at
   cells the simulation has already woken. */
void treesTick(World& w);

/* Grow a full tree at (x, y) immediately, ignoring the timer. Used by worldgen
   to put forests in the world at generation time, and by tests that have no
   interest in waiting a minute. */
void treeGrowNow(World& w, int x, int y, u32 salt);
