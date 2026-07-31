#include "craft.h"

/* One cell of world is one unit of item (see MATERIAL_STACK), so a recipe's
   numbers are in CELLS. That is what makes these figures readable: four wood
   makes four platform because a platform is a cell of floor and a log is a cell
   of tree, and a player laying a walkway can count the cost by eye.

   The torch is the one that is not 1:1, and deliberately: one coal is worth
   several torches, which is what makes finding a coal seam feel like a supply
   rather than a consumable. */
const Recipe RECIPES[] = {
    /* Light, and the first reason to want coal for something other than heat. */
    { { { (ItemId)MAT_WOOD, 4 }, { (ItemId)MAT_COAL, 1 }, { ITEM_NONE, 0 } },
      ITEM_TORCH_DEV, 4, "4 Torches" },

    /* Floor you can pass through. Cheap on purpose -- a platform is scaffolding
       and scaffolding you have to ration is scaffolding you do not build. */
    { { { (ItemId)MAT_WOOD, 1 }, { ITEM_NONE, 0 }, { ITEM_NONE, 0 } },
      (ItemId)MAT_PLATFORM, 2, "2 Platform" },

    /* Rope, on the same reasoning and the same price. */
    { { { (ItemId)MAT_WOOD, 1 }, { ITEM_NONE, 0 }, { ITEM_NONE, 0 } },
      (ItemId)MAT_ROPE, 2, "2 Rope" },

    /* A door is a wall you built twice, so it costs more per cell than either
       of the above -- and you need a lot of cells to fill a doorway, which is
       what stops doors being the default way to close a room. */
    { { { (ItemId)MAT_WOOD, 2 }, { ITEM_NONE, 0 }, { ITEM_NONE, 0 } },
      (ItemId)MAT_DOOR, 1, "Door" },

    /* --- birch --------------------------------------------------------------
       One recipe, not a second copy of all four. Birch logs convert to ordinary
       wood at one for one, and everything downstream is unchanged.

       Forking every wooden recipe per species was the alternative and it is
       worse in every direction: the crafting list doubles in length, each entry
       has a twin that differs by one word, and the player has to read both to
       find out they do the same thing. A conversion says the real relationship
       -- birch is wood you happen to have in a different colour -- in one row,
       and it leaves room for birch to be worth something specific later
       without unpicking anything. */
    { { { (ItemId)MAT_BIRCH_WOOD, 1 }, { ITEM_NONE, 0 }, { ITEM_NONE, 0 } },
      (ItemId)MAT_WOOD, 1, "Wood from Birch" },
};

const int N_RECIPES = (int)(sizeof(RECIPES) / sizeof(RECIPES[0]));

bool craftCan(const Inventory& inv, int r) {
    if (r < 0 || r >= N_RECIPES) return false;
    const Recipe& rc = RECIPES[r];
    for (int i = 0; i < CRAFT_MAX_IN; ++i) {
        if (rc.in[i].item == ITEM_NONE || rc.in[i].count <= 0) continue;
        if (inv.countOf(rc.in[i].item) < rc.in[i].count) return false;
    }
    return true;
}

bool craftMake(Inventory& inv, int r) {
    if (!craftCan(inv, r)) return false;
    const Recipe& rc = RECIPES[r];

    /* Room for the result BEFORE anything is spent. Checked by actually adding
       it and putting it back if it did not fit, rather than by predicting --
       "will this fit" duplicates the stacking rules, and a duplicate of those
       rules is a duplicate that can disagree with them. */
    const int left = inv.add(rc.out, rc.outCount);
    if (left != 0) {
        /* Undo the part that did land. Cannot fail: it was just added. */
        inv.take(rc.out, rc.outCount - left);
        return false;
    }

    for (int i = 0; i < CRAFT_MAX_IN; ++i) {
        if (rc.in[i].item == ITEM_NONE || rc.in[i].count <= 0) continue;
        inv.take(rc.in[i].item, rc.in[i].count);
    }
    return true;
}
