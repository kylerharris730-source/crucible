#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "player.h"
#include "multiplayer.h"
#include <stdio.h>

/* Everybody who joins gets the same handout as the person who made the world.

   The kit used to be a main.cpp helper that only ever touched g_inv, so a
   friend arrived with an empty pack: no weapon, no fire starter, and none of
   the wood the host had. The bug was invisible in single player, which is the
   only place it was ever tested.

   The double-handout check matters as much as the first one. These are guarded
   on "not already carrying", so opening a session twice must not mint a second
   wand -- and a player who has deliberately thrown their axe into a lava pit
   must not be handed a replacement.

   Compile with all source files except main.cpp. */

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

static void expectKit(const Inventory& inv, const char* who) {
    char msg[160];
    struct { ItemId item; int least; const char* name; } want[] = {
        { ITEM_BOLTER,    1, "a weapon" },
        { ITEM_FLINT,     1, "a fire starter" },
        { ITEM_WARP_WAND, 1, "a warp wand" },
        { (ItemId)MAT_WOOD, 100, "wood" },
    };
    for (int i = 0; i < 4; ++i) {
        sprintf(msg, "%s starts with %s", who, want[i].name);
        check(inv.countOf(want[i].item) >= want[i].least, msg);
    }
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();

    /* --- the host ---------------------------------------------------------- */
    inventoryStartingKit(g_inv);
    expectKit(g_inv, "the host");

    /* --- and anyone who joins ---------------------------------------------- */
    const PlayerId guest = playerSessionOpen(false, 600.0f, 600.0f);
    check(guest != PLAYER_NONE, "a guest session opens");
    if (guest == PLAYER_NONE) return 1;
    expectKit(g_playerSessions[guest].inventory, "a guest");

    /* The wand is a tool, so it has to arrive as a real one with its own
       instance and a full battery rather than as a bare id. */
    int wandSlot = -1;
    const Inventory& gi = g_playerSessions[guest].inventory;
    for (int i = 0; i < INV_SLOTS; ++i)
        if (gi.slot[i].item == ITEM_WARP_WAND) { wandSlot = i; break; }
    check(wandSlot >= 0, "the guest's wand is in the pack");
    if (wandSlot >= 0) {
        const u16 inst = gi.slot[wandSlot].inst;
        check(inst != 0, "and carries a tool instance");
        if (inst) check(g_toolInst[inst].energy > 0, "with charge in it");
    }

    /* --- handing it out again changes nothing ------------------------------ */
    const int wandsBefore = g_inv.countOf(ITEM_WARP_WAND);
    const int woodBefore  = g_inv.countOf((ItemId)MAT_WOOD);
    inventoryStartingKit(g_inv);
    check(g_inv.countOf(ITEM_WARP_WAND) == wandsBefore, "a second handout adds no second wand");
    check(g_inv.countOf((ItemId)MAT_WOOD) == woodBefore, "and no second pile of wood");

    if (failures) {
        fprintf(stderr, "%d starting kit check(s) failed\n", failures);
        return 1;
    }
    puts("host and guest both start equipped, and only once");
    return 0;
}
