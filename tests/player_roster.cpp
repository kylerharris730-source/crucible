#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "player.h"
#include "identity.h"
#include "multiplayer.h"
#include "save.h"
#include <stdio.h>
#include <string.h>

/* A guest who leaves and comes back gets their pack, not a new one.

   The roster keeps tools BY VALUE rather than by handle, and that is the part
   worth testing hard. There are only MAX_TOOL_INST (32) instances in the whole
   game, so a remembered player cannot be left holding one while they are away
   -- two dozen of them would drain the pool before anybody logged in. What is
   stored is a copy of the instance's contents, and restoring allocates a fresh
   handle and pours the contents back.

   Which means the interesting failures are: instances leaking on every visit,
   the restored tool arriving blank, and two players ending up pointed at one
   instance. All three are checked.

   Compile with all source files except main.cpp. */

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

static int poolInUse() {
    int n = 0;
    for (int i = 1; i < MAX_TOOL_INST; ++i) if (g_toolInst[i].used) ++n;
    return n;
}

static int wandSlotOf(const Inventory& inv) {
    for (int i = 0; i < INV_SLOTS; ++i)
        if (inv.slot[i].item == ITEM_WARP_WAND) return i;
    return -1;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();
    rosterClear();

    const char* ALICE = "aaaaaaaabbbbbbbbccccccccdddddddd";
    const char* BOB   = "11111111222222223333333344444444";

    /* --- a guest arrives, marks their kit, and leaves --------------------- */
    PlayerId slot = playerSessionOpen(false, 500.0f, 500.0f);
    check(slot != PLAYER_NONE, "a guest session opens");
    if (slot == PLAYER_NONE) return 1;

    PlayerSession& s = g_playerSessions[slot];
    const int wand = wandSlotOf(s.inventory);
    check(wand >= 0, "the guest starts with a wand");
    if (wand < 0) return 1;

    /* Something only THIS wand could carry, so a blank replacement is not
       mistaken for the same object coming home. */
    const u16 liveInst = s.inventory.slot[wand].inst;
    check(liveInst != 0, "and it is a real tool instance");
    g_toolInst[liveInst].energy = 77;
    s.inventory.add((ItemId)MAT_IRON, 42);
    s.body.x = 1234.0f; s.body.y = 567.0f;

    const int poolBefore = poolInUse();
    rosterRemember(ALICE, s);
    playerSessionClose(slot);
    check(poolInUse() < poolBefore, "closing the session returns its tools to the pool");

    /* --- and comes back --------------------------------------------------- */
    slot = playerSessionOpen(false, 500.0f, 500.0f);
    check(slot != PLAYER_NONE, "the returning guest gets a session");
    PlayerSession& back = g_playerSessions[slot];
    check(rosterRestore(ALICE, back), "and is recognised");

    check(back.inventory.countOf((ItemId)MAT_IRON) == 42, "their iron came back");
    check(back.body.x == 1234.0f && back.body.y == 567.0f, "and so did where they stood");

    const int wand2 = wandSlotOf(back.inventory);
    check(wand2 >= 0, "the wand came back");
    if (wand2 >= 0) {
        const u16 inst2 = back.inventory.slot[wand2].inst;
        check(inst2 != 0, "as a live tool");
        check(inst2 < MAX_TOOL_INST && g_toolInst[inst2].used, "with a valid handle");
        if (inst2) check(g_toolInst[inst2].energy == 77,
                         "carrying the charge it had, not a factory reset");
    }

    /* --- somebody nobody has met gets nothing back ------------------------ */
    PlayerSession stranger;
    memset(&stranger, 0, sizeof(stranger));
    check(!rosterRestore(BOB, stranger), "an unknown identity is not recognised");

    /* --- visiting repeatedly must not leak instances ---------------------- */
    /* The failure this guards: restore overwrites an inventory that was just
       handed a starting kit, so the kit's instances have to go back first.
       Miss it and every visit costs the pool a handle until nobody can hold a
       tool at all. */
    const int settled = poolInUse();
    for (int visit = 0; visit < 12; ++visit) {
        rosterRemember(ALICE, g_playerSessions[slot]);
        playerSessionClose(slot);
        slot = playerSessionOpen(false, 500.0f, 500.0f);
        rosterRestore(ALICE, g_playerSessions[slot]);
    }
    printf("pool in use: %d after one visit, %d after twelve more\n", settled, poolInUse());
    check(poolInUse() <= settled, "twelve more visits leak nothing");

    /* And the wand is still theirs, with its charge, after all that. */
    const int wandN = wandSlotOf(g_playerSessions[slot].inventory);
    check(wandN >= 0, "the wand survives repeated visits");
    if (wandN >= 0) {
        const u16 instN = g_playerSessions[slot].inventory.slot[wandN].inst;
        check(instN != 0 && g_toolInst[instN].energy == 77,
              "still holding the same charge twelve visits later");
    }

    /* --- and it survives the host quitting -------------------------------- */
    /* The roster is only a session cache unless it is written down. This is
       the difference between "your stuff survives you leaving" and "your stuff
       survives until the host closes the game", and only one of those is worth
       having. */
    rosterRemember(ALICE, g_playerSessions[slot]);
    playerSessionClose(slot);

    g_world.reset();
    const char* path = "build/roster_test.sav";
    check(saveWrite(path, g_world), "the world writes");
    rosterClear();
    check(!rosterRestore(ALICE, stranger), "the roster really was emptied");

    if (!saveRead(path, g_world)) {
        fprintf(stderr, "FAIL: the world did not read back: %s\n", saveError());
        ++failures;
    } else {
        PlayerSession revisit;
        memset(&revisit, 0, sizeof(revisit));
        check(rosterRestore(ALICE, revisit), "the guest is still known after a reload");
        check(revisit.inventory.countOf((ItemId)MAT_IRON) == 42,
              "with their iron intact across the restart");
        const int w = wandSlotOf(revisit.inventory);
        check(w >= 0, "and their wand");
        if (w >= 0) {
            const u16 inst = revisit.inventory.slot[w].inst;
            check(inst != 0 && g_toolInst[inst].used, "restored to a live instance");
            if (inst) check(g_toolInst[inst].energy == 77, "still charged as they left it");
        }
    }
    remove(path);

    if (failures) {
        fprintf(stderr, "%d roster check(s) failed\n", failures);
        return 1;
    }
    puts("guests are remembered, tools come home charged, and it all survives a restart");
    return 0;
}
