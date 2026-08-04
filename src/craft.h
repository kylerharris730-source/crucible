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
   Up to CRAFT_MAX_IN ingredients, one output, and a station. That station
   field is exactly the one this comment used to say would arrive later --
   the rest of the prediction held too: it cost one field and one test in
   craftCan(), nothing pre-built.

   --- stations ---
   A station is a placed MATERIAL, not a device: see MAT_STATION_BENCH and
   friends in materials.h for why. It has no state, no update tick, no
   facing -- it only ever has to answer "is one of these near the player",
   which is a handful of world-cell reads, not an entity with a save format
   of its own. STATION_HAND is 0, so every recipe written before this field
   existed is unchanged: an aggregate initializer shorter than the struct
   zero-fills the rest, and 0 is exactly "craftable anywhere" here. */
enum CraftStation {
    STATION_HAND = 0,
    STATION_BENCH,
    STATION_ANVIL,
    STATION_CHEM,
    STATION_ASSEMBLY,
    /* The blast furnace. Appended after ASSEMBLY rather than slotted in beside
       the anvil it upgrades, because these values are written into RECIPES[]
       and renumbering them would silently move every recipe to a different
       station. The enum is no longer a strict difficulty order because of that,
       which is the price of the ordering being stable -- read the station a
       recipe needs from its row, never from the enum's arithmetic. */
    STATION_FORGE,
    STATION_COUNT
};

/* Name for the greyed-out row that is missing one -- see drawCraft() in
   main.cpp. */
extern const char* const STATION_NAMES[STATION_COUNT];

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
    /* A CraftStation. Last, and zero by default -- see the note above. */
    u8 station;
};

extern const Recipe RECIPES[];
extern const int    N_RECIPES;

/* Scans the world around `p` for the nearest station of each tier and fills
   a small cache craftCan() reads. Call this ONCE A FRAME while the menu is
   open (see layoutCraft() in main.cpp) rather than once per recipe --
   craftCan() is asked once per row every frame the menu is up, and with
   seventy recipes that would be seventy redundant sweeps of the same
   handful of cells for an answer that is the same all seventy times.

   Deliberately NOT cached against the player's position across frames: the
   world can change while the player stands still -- placing a bench and
   immediately wanting the anvil recipe it unlocks is the ordinary case, not
   an edge one -- so every call here does a fresh scan. What bounds the cost
   is calling it once a frame rather than once a recipe, not skipping frames
   where nothing moved. */
void craftScanStations(const World& w, const Player& p);

/* Does the pack hold everything recipe `r` needs, AND is its station (if
   any) within reach? Reads the cache craftScanStations() fills; does not
   scan the world itself. */
bool craftCan(const Inventory& inv, int r);

/* Just the station half of that question: is recipe `r`'s station within
   reach (always true for STATION_HAND)? Separate from craftCan because a
   greyed-out row has to say WHICH errand it is -- "go build an anvil" and
   "go find more copper" send the player to opposite ends of the map, and
   craftCan collapses both into one false. See drawCraft() in main.cpp. */
bool craftHasStation(int r);

/* Make one. Takes the ingredients and adds the output; returns false and
   changes NOTHING if the ingredients are missing or the result will not fit.

   All-or-nothing matters more here than anywhere else in the item code: a
   craft that consumed the wood and then found no room for the door would
   destroy the wood, and it would do it at exactly the moment the player was
   least able to tell what had happened. */
bool craftMake(Inventory& inv, int r);
