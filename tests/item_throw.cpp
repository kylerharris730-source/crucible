#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "player.h"
#include "multiplayer.h"
#include <stdio.h>
#include <string.h>

/* Loot is an object, not terrain -- and a thrown stack must actually leave.

   The Cinder Moth's coal used to be written into the world as CELLS, so every
   dead moth left one or two solid blocks wherever it died and the surface
   filled up with lumps the player caught on while walking. The Drip Slime's
   acid is written the same way ON PURPOSE, because a puddle of acid is a
   hazard it left behind rather than a prize. Both halves are pinned here: it
   would be easy to fix the first by making every drop a collectible and
   quietly delete the second.

   The throw checks are about the one thing that makes throwing work at all.
   A thrown stack starts inside the thrower's own collection radius, so without
   a delay the magnet pulls it straight back and the key appears to be dead.

   Compile with all source files except main.cpp. */

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

static const int WX = 1400, WY = 700;

static void clearArea(World& w) {
    for (int y = WY - 60; y <= WY + 60; ++y)
        for (int x = WX - 60; x <= WX + 60; ++x)
            w.setCell(x, y, MAT_EMPTY);
    /* A floor, so a walker has somewhere to stand and loot has somewhere to
       land instead of falling out of the tested region. */
    for (int y = WY + 20; y <= WY + 26; ++y)
        for (int x = WX - 60; x <= WX + 60; ++x)
            w.setCell(x, y, MAT_STONE);
}

static int cellsOf(const World& w, u8 mat) {
    int n = 0;
    for (int y = WY - 40; y <= WY + 19; ++y)
        for (int x = WX - 40; x <= WX + 40; ++x)
            if (w.at(x, y).mat == mat) ++n;
    return n;
}

static int pickupsOf(ItemId item) {
    int n = 0;
    for (int i = 0; i < MAX_PICKUPS; ++i)
        if (g_pickups[i].used && g_pickups[i].item == item) ++n;
    return n;
}

/* Walk over and pick it up.

   Moving the player is not a shortcut around the test: there is no magnet
   unless the player is carrying a pickup-radius bonus (see pickupMagnet, which
   returns immediately when the reach is only the base one), so a thrown stack
   that lands a few cells away is collected by WALKING TO IT and nothing else.
   Standing still and waiting would test that gravity works. */
static bool walkToAndCollect(World& w, Player& p, ItemId item) {
    for (int f = 0; f < 600; ++f) {
        int found = -1;
        for (int i = 0; i < MAX_PICKUPS; ++i)
            if (g_pickups[i].used && g_pickups[i].item == item) { found = i; break; }
        if (found < 0) return true;                 /* gone: collected */
        if (f >= PICKUP_THROW_DELAY)                /* only once it is collectable */
            p.reset(g_pickups[found].x, g_pickups[found].y);
        p.alive = true;
        entTick(w, p, g_inv);
    }
    return false;
}

/* Spawn one, kill it, and let the tick run the death. entDie is static, and
   reaching it the way the game does is the point rather than an obstacle. */
static bool killOne(World& w, Player& p, EntityType type, float x, float y) {
    const int slot = entSpawn(w, type, x, y);
    if (slot < 0) return false;
    Entity& e = g_entities[slot];
    entDamageAt((int)e.centreX(), (int)e.centreY(), e.hp + 1000);
    entTick(w, p, g_inv);
    return true;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    g_world.reset();

    World& w = g_world;
    clearArea(w);
    w.setLiveWindow(WX - 300, WY - 300, WX + 300, WY + 300);

    Player& p = g_player;
    /* Far away and dead, so nothing is collected while the drops are being
       counted -- the magnet reaches further than people expect. */
    p.reset((float)(WX + 900), (float)WY);
    p.alive = false;
    g_inv.clear();

    /* --- the complaint: coal was terrain -------------------------------- */
    entReset();
    memset(g_pickups, 0, sizeof(g_pickups));
    check(killOne(w, p, ENT_MOTH, (float)WX, (float)(WY - 10)), "a Cinder Moth spawns");
    check(cellsOf(w, MAT_COAL) == 0, "a dead moth leaves no coal blocks to catch on");
    check(pickupsOf((ItemId)MAT_COAL) > 0, "its coal is a collectible instead");

    /* --- what did NOT change: acid is still a puddle --------------------- */
    entReset();
    memset(g_pickups, 0, sizeof(g_pickups));
    clearArea(w);
    check(killOne(w, p, ENT_SLIME, (float)WX, (float)(WY + 16)), "a Drip Slime spawns");
    check(cellsOf(w, MAT_ACID) > 0, "a dead slime still leaves acid in the world");
    check(pickupsOf((ItemId)MAT_ACID) == 0, "and its acid is not a collectible");

    /* --- a thrown stack leaves the thrower ------------------------------- */
    entReset();
    memset(g_pickups, 0, sizeof(g_pickups));
    clearArea(w);
    g_inv.clear();
    p.reset((float)WX, (float)(WY + 14));
    p.alive = true;

    check(pickupThrow((ItemId)MAT_WOOD, 5, 0, p.centreX() + 3.0f, p.centreY(), 2.1f, -1.5f),
          "a throw finds a pickup slot");
    /* Standing right on top of it. Before the delay existed this was collected
       on the very next tick and throwing did nothing at all. */
    bool heldOut = true;
    for (int f = 0; f < PICKUP_THROW_DELAY - 5; ++f) {
        entTick(w, p, g_inv);
        if (g_inv.countOf((ItemId)MAT_WOOD) != 0) { heldOut = false; break; }
    }
    check(heldOut, "it is not sucked straight back into the thrower");

    check(walkToAndCollect(w, p, (ItemId)MAT_WOOD), "it can be walked to");
    check(g_inv.countOf((ItemId)MAT_WOOD) == 5, "and it can be picked up once the delay passes");

    /* --- a thrown tool keeps its upgrades -------------------------------- */
    /* The reason Pickup carries an instance at all: handing someone a tool is
       what people want throwing FOR, and Inventory::add merges by item id and
       would have arrived with a factory-fresh one. */
    entReset();
    memset(g_pickups, 0, sizeof(g_pickups));
    clearArea(w);
    g_inv.clear();
    p.reset((float)WX, (float)(WY + 14));
    p.alive = true;

    const u16 inst = (u16)toolInstNew(ITEM_BOLTER);
    if (!inst) {
        fprintf(stderr, "could not allocate a tool instance\n");
        return 2;
    }
    check(pickupThrow(ITEM_BOLTER, 1, inst, p.centreX() + 3.0f, p.centreY(), 2.1f, -1.5f),
          "a tool can be thrown");
    check(walkToAndCollect(w, p, ITEM_BOLTER), "the tool can be walked to");

    int carried = -1;
    for (int i = 0; i < INV_SLOTS; ++i)
        if (g_inv.slot[i].item == ITEM_BOLTER) { carried = i; break; }
    check(carried >= 0, "the thrown tool is picked up");
    if (carried >= 0)
        check(g_inv.slot[carried].inst == inst,
              "and it is the SAME tool, upgrades and all");

    if (failures) {
        fprintf(stderr, "%d throw/drop check(s) failed\n", failures);
        return 1;
    }
    puts("loot is collectible, acid still pools, and a thrown stack leaves the hand");
    return 0;
}
