#pragma once
#include "item.h"

/* --- crafting --------------------------------------------------------------

   A list of recipes, and a menu that shows you the ones you can make right now.
   Click one and it is made.

   That "right now" is the whole design, and it is Terraria's rather than
   Minecraft's on purpose. A grid you arrange ingredients in is a puzzle about
   remembering shapes; a filtered list is a question about what you have, and
   this game already asks plenty of questions. It also means the recipe list
   doubles as the tutorial: the first time you carry wood, the menu tells you
   what wood is for, and nobody has to be told to look anything up.

   --- what a recipe is ---
   Up to CRAFT_MAX_IN ingredients and one output. No stations, no tiers, no
   unlock flags -- none of those are needed yet and every one of them would be a
   second thing to check before showing a row. When a furnace-only recipe
   arrives it wants a `station` field here and one more test in craftCan(), and
   that is a smaller change than pre-building the machinery for it now. */

static const int CRAFT_MAX_IN = 3;

struct CraftIn {
    ItemId item;
    int    count;
};

struct Recipe {
    CraftIn in[CRAFT_MAX_IN];
    ItemId  out;
    int     outCount;
    /* Shown on the row. Derived from the output's name everywhere so far, but
       kept explicit because a recipe that makes four of something wants to say
       so in a way "Torch" cannot. */
    const char* label;
};

extern const Recipe RECIPES[];
extern const int    N_RECIPES;

/* Does the pack hold everything recipe `r` needs? */
bool craftCan(const Inventory& inv, int r);

/* Make one. Takes the ingredients and adds the output; returns false and
   changes NOTHING if the ingredients are missing or the result will not fit.

   All-or-nothing matters more here than anywhere else in the item code: a
   craft that consumed the wood and then found no room for the door would
   destroy the wood, and it would do it at exactly the moment the player was
   least able to tell what had happened. */
bool craftMake(Inventory& inv, int r);
