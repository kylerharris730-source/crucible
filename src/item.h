#pragma once
#include "world.h"

/* --- items and the inventory ----------------------------------------------

   Item ids SHARE the material id space. Ids below MAT_COUNT *are* materials --
   a stack of ITEM(MAT_STONE) is a stack of stone -- and everything that is not
   a material is numbered above it.

   That is worth doing rather than keeping a parallel item table with a mapping
   between the two, because in this game the overwhelming majority of items are
   simply materials you dug up. A mapping would need maintaining every time a
   material is added, would be wrong the first time somebody forgot, and would
   buy nothing: no material needs to be two different items, and no item needs
   to be two different materials. main.cpp already numbers its heat and cool
   brushes this way (TOOL_HEAT = MAT_COUNT), so the pattern is not new here.

   The consequence to remember is that ITEM_NONE and MAT_EMPTY are the same
   value, which is convenient -- an empty slot and empty space are both 0 -- but
   it does mean "is this slot occupied" and "is this cell air" are the same
   test, and a bug in one reads as a bug in the other. */

typedef u16 ItemId;

static const ItemId ITEM_NONE = MAT_EMPTY;   /* both are 0 */

enum {
    /* Non-material items start where materials stop. */
    ITEM_MULTITOOL = MAT_COUNT,
    /* Reach extenders. They do nothing when held and everything when carried --
       see ITEMK_CARRIED below. */
    ITEM_LENS,
    ITEM_RELAY,
    ITEM_COUNT
};

enum ItemKind {
    ITEMK_MATERIAL = 0,   /* stacks; one unit is one cell of world */
    ITEMK_TOOL,           /* unique, carries its own state */
    ITEMK_MODULE,         /* slots into a tool to change what it does */
    ITEMK_CARRIED         /* passive: works from anywhere in the pack */
};

struct ItemDef {
    const char* name;
    u8   kind;
    u16  maxStack;
    u32  colour;          /* for the hotbar swatch */

    /* Cells of extra reach while this is anywhere in the pack. Bonuses do NOT
       add up: the pack's bonus is the single largest one carried. Summing would
       make ten cheap lenses better than one good relay, which turns an upgrade
       ladder into an inventory-stuffing puzzle -- and the slot pressure that is
       supposed to make carrying one a real choice would evaporate. */
    i16  reachBonus;
};

extern ItemDef ITEMS[ITEM_COUNT];

/* Fills in the material half of ITEMS[] from MATS[] and the colour LUT, so a
   new material becomes a carryable item with no extra work. Call after
   initMaterials(). */
void initItems();

/* One cell of world is one unit of item. Deliberately not a bigger number:
   digging a tunnel should visibly fill your pockets, and the arithmetic between
   "cells removed" and "items gained" being 1:1 means there is never a rounding
   question about what a partial stack represents. */
static const int INV_SLOTS = 10;

struct ItemStack {
    ItemId item;
    u16    count;
    bool empty() const { return item == ITEM_NONE || count == 0; }
};

struct Inventory {
    ItemStack slot[INV_SLOTS];
    int       selected;

    void clear();

    /* Adds up to `count`, filling existing stacks of the same item before
       opening new slots. Returns how many did NOT fit, so the caller can decide
       whether to leave the remainder in the world -- silently destroying what
       will not fit is the kind of thing players notice and resent. */
    int  add(ItemId item, int count);

    /* Removes up to `count`. Returns how many were actually removed. */
    int  take(ItemId item, int count);

    int  countOf(ItemId item) const;
    int  freeSlots() const;

    ItemStack& held() { return slot[selected]; }
    const ItemStack& held() const { return slot[selected]; }

    /* Largest reachBonus among everything carried; 0 with nothing. */
    int  reachBonus() const;
};

extern Inventory g_inv;

/* --- what a digging implement can do --------------------------------------

   Bare hands are a ToolSpec too, rather than a special case with the limits
   hard-coded into the input handler. That is the whole point: the multitool is
   then not a new mechanism, it is a better row in this struct, and every place
   that asks "how fast can I dig" already reads it from somewhere.

   The three numbers are deliberately the ones a player can feel separately.
   maxRadius is how much of the world you can work on at once, cellsPerBite is
   how much of that you actually shift, and cooldown is the rhythm. A tool that
   improved all three at once would feel like one upgrade; tools that improve
   one each give a reason to own more than one. */
struct ToolSpec {
    const char* name;
    int maxRadius;      /* the brush radius is clamped to this */
    int cellsPerBite;   /* cells removed per action */
    int cooldown;       /* frames between actions */
};

/* Hands. Slow and small on purpose: this is the baseline every tool is measured
   against, and if bare hands were comfortable no tool would feel like progress.
   10 cells every 6 frames is 100 cells a second -- a 6-wide tunnel advances
   about a body length every second, which is workable for getting somewhere and
   genuinely tiresome for undoing a mistake. That last part is the design: it is
   what makes a precision tool worth building rather than a luxury. */
extern const ToolSpec HAND;

/* --- disc offsets, ordered nearest-first ----------------------------------

   A table of every (dx,dy) within DISC_MAX_R, sorted by distance from the
   centre. Two things fall out of the ordering, and both matter.

   First, a partial bite is a coherent shape. Once digging has a per-frame cell
   budget it will usually stop part-way through a disc, and walking the cells in
   scanline order would eat the top of the circle and leave the bottom -- the
   hole would grow downward-lopsided rather than outward. Nearest-first grows it
   as a circle, ring by ring, which is what "digging" looks like.

   Second, because the table is sorted by distance, every cell within radius r
   is a PREFIX of it. So g_discEnd[r] is just a length, and no per-cell distance
   test is needed at all. */
static const int DISC_MAX_R = 64;      /* matches the brush size clamp */
static const int DISC_MAX_CELLS = 13200;   /* pi*64^2 = 12868, with headroom */

struct DiscOff { i16 dx, dy; };
extern DiscOff g_disc[DISC_MAX_CELLS];
extern int     g_discEnd[DISC_MAX_R + 1];

void initDiscTable();   /* called by initItems() */

/* --- the two verbs --------------------------------------------------------
   Dig cells out of the world into a pack, and build cells out of a pack into
   the world. They live here rather than in main.cpp for two reasons: they are
   game rules rather than window code, and they are precisely what the multitool
   will drive once it has a fire rate and modules to say how big a bite it takes.
   Keeping them out of the message loop means they can be tested without a
   window, which is the only way any of this gets verified. */

/* Removes cells and banks them, working outward from the centre. Returns how
   many cells were removed. Anything that will not fit is LEFT IN THE WORLD
   rather than destroyed.

   maxCells caps how much comes out in one call; 0 means the whole disc, which
   is what the unlimited sandbox brush wants. A tool passes its cellsPerBite. */
int digInto(World& w, Inventory& inv, int cx, int cy, int r, int maxCells = 0);

/* Places the held stack into empty cells of a disc, one item per cell, until
   the stack runs out. Returns how many cells were filled. Never overwrites
   existing material and never places inside an occupied entity box. */
int placeFrom(World& w, Inventory& inv, int cx, int cy, int r, int maxCells = 0);
