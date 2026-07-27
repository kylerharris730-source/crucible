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
    ITEM_COUNT
};

enum ItemKind {
    ITEMK_MATERIAL = 0,   /* stacks; one unit is one cell of world */
    ITEMK_TOOL,           /* unique, carries its own state */
    ITEMK_MODULE          /* slots into a tool to change what it does */
};

struct ItemDef {
    const char* name;
    u8   kind;
    u16  maxStack;
    u32  colour;          /* for the hotbar swatch */
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
};

extern Inventory g_inv;

/* --- the two verbs --------------------------------------------------------
   Dig cells out of the world into a pack, and build cells out of a pack into
   the world. They live here rather than in main.cpp for two reasons: they are
   game rules rather than window code, and they are precisely what the multitool
   will drive once it has a fire rate and modules to say how big a bite it takes.
   Keeping them out of the message loop means they can be tested without a
   window, which is the only way any of this gets verified. */

/* Removes a disc of cells and banks them. Returns how many cells were removed.
   Anything that will not fit is LEFT IN THE WORLD rather than destroyed. */
int digInto(World& w, Inventory& inv, int cx, int cy, int r);

/* Places the held stack into empty cells of a disc, one item per cell, until
   the stack runs out. Returns how many cells were filled. Never overwrites
   existing material and never places inside an occupied entity box. */
int placeFrom(World& w, Inventory& inv, int cx, int cy, int r);
