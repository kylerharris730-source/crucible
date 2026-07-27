#include "item.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ItemDef   ITEMS[ITEM_COUNT];
Inventory g_inv;

/* Forgetting initItems() does not crash, it does something far worse: every
   stack limit is zero, so the inventory politely refuses every item and the
   hotbar renders ten empty slots. That looks exactly like "the inventory does
   not work yet" rather than "you skipped a setup call", and it cost a
   screenshot and a confused ten minutes before this guard existed. */
static bool g_itemsReady = false;

void initItems() {
    memset(ITEMS, 0, sizeof(ITEMS));
    g_itemsReady = true;

    /* Materials describe themselves. Name comes straight from MATS[] and the
       swatch from the colour LUT at a mid tint, which is the same sample the
       palette buttons use -- so a stack of stone in the hotbar is exactly the
       colour stone is in the world, automatically, forever. */
    for (int m = 0; m < MAT_COUNT; ++m) {
        ITEMS[m].name     = MATS[m].name;
        ITEMS[m].kind     = ITEMK_MATERIAL;
        ITEMS[m].maxStack = 999;
        ITEMS[m].colour   = g_colorLut[(m << 8) | 0x08];
    }
    /* Air is not a thing you can carry. Leaving it named and stackable would
       make "empty slot" and "a stack of nothing" two different states that look
       identical in the UI. */
    ITEMS[MAT_EMPTY].name     = "";
    ITEMS[MAT_EMPTY].maxStack = 0;

    ITEMS[ITEM_MULTITOOL].name     = "Multitool";
    ITEMS[ITEM_MULTITOOL].kind     = ITEMK_TOOL;
    ITEMS[ITEM_MULTITOOL].maxStack = 1;
    ITEMS[ITEM_MULTITOOL].colour   = 0xC8B070;
}

void Inventory::clear() {
    memset(slot, 0, sizeof(slot));
    selected = 0;
}

int Inventory::add(ItemId item, int count) {
    if (!g_itemsReady) {
        fprintf(stderr, "inventory used before initItems() -- every stack limit "
                        "is 0, so nothing can ever be picked up\n");
        abort();
    }
    if (item == ITEM_NONE || count <= 0) return count > 0 ? count : 0;
    const int cap = ITEMS[item].maxStack;
    if (cap <= 0) return count;

    /* Top up existing stacks first. Opening a new slot for an item you are
       already carrying is how an inventory ends up with three half-stacks of
       sand and no room for anything else. */
    for (int i = 0; i < INV_SLOTS && count > 0; ++i) {
        if (slot[i].item != item || slot[i].count >= cap) continue;
        const int room = cap - slot[i].count;
        const int put  = (count < room) ? count : room;
        slot[i].count = (u16)(slot[i].count + put);
        count -= put;
    }
    for (int i = 0; i < INV_SLOTS && count > 0; ++i) {
        if (!slot[i].empty()) continue;
        const int put = (count < cap) ? count : cap;
        slot[i].item  = item;
        slot[i].count = (u16)put;
        count -= put;
    }
    return count;   /* whatever would not fit */
}

int Inventory::take(ItemId item, int count) {
    if (item == ITEM_NONE || count <= 0) return 0;
    int taken = 0;

    /* Drain the SELECTED slot first if it matches, so that holding a stack and
       using it empties the one you are looking at rather than some other slot
       -- otherwise the count under the cursor sits still while a different
       number ticks down, which reads as a bug. */
    if (slot[selected].item == item && slot[selected].count > 0) {
        const int got = (count < slot[selected].count) ? count : slot[selected].count;
        slot[selected].count = (u16)(slot[selected].count - got);
        if (slot[selected].count == 0) slot[selected].item = ITEM_NONE;
        taken += got;
        count -= got;
    }
    for (int i = 0; i < INV_SLOTS && count > 0; ++i) {
        if (slot[i].item != item || slot[i].count == 0) continue;
        const int got = (count < slot[i].count) ? count : slot[i].count;
        slot[i].count = (u16)(slot[i].count - got);
        if (slot[i].count == 0) slot[i].item = ITEM_NONE;
        taken += got;
        count -= got;
    }
    return taken;
}

int Inventory::countOf(ItemId item) const {
    if (item == ITEM_NONE) return 0;
    int n = 0;
    for (int i = 0; i < INV_SLOTS; ++i)
        if (slot[i].item == item) n += slot[i].count;
    return n;
}

int Inventory::freeSlots() const {
    int n = 0;
    for (int i = 0; i < INV_SLOTS; ++i) if (slot[i].empty()) ++n;
    return n;
}

int digInto(World& w, Inventory& inv, int cx, int cy, int r) {
    int dug = 0;
    const int r2 = r * r;
    const int x0 = imax(cx - r, PLAY_X0), x1 = imin(cx + r, PLAY_X1);
    const int y0 = imax(cy - r, PLAY_Y0), y1 = imin(cy + r, PLAY_Y1);
    for (int y = y0; y <= y1; ++y) {
        const int dy = y - cy;
        for (int x = x0; x <= x1; ++x) {
            const int dx = x - cx;
            if (dx * dx + dy * dy > r2) continue;
            const u8 m = w.at(x, y).mat;
            if (m == MAT_EMPTY) continue;
            /* Bank it first, and only remove it from the world if it fit. The
               order matters: dig-then-store would drop material on the floor of
               a full pack, and players notice that exactly once -- when it was
               something they wanted. */
            if (inv.add((ItemId)m, 1) != 0) continue;
            w.setCell(x, y, MAT_EMPTY);
            ++dug;
        }
    }
    return dug;
}

int placeFrom(World& w, Inventory& inv, int cx, int cy, int r) {
    ItemStack& h = inv.held();
    if (h.empty() || ITEMS[h.item].kind != ITEMK_MATERIAL) return 0;

    int put = 0;
    const int r2 = r * r;
    const int x0 = imax(cx - r, PLAY_X0), x1 = imin(cx + r, PLAY_X1);
    const int y0 = imax(cy - r, PLAY_Y0), y1 = imin(cy + r, PLAY_Y1);
    for (int y = y0; y <= y1; ++y) {
        const int dy = y - cy;
        for (int x = x0; x <= x1; ++x) {
            const int dx = x - cx;
            if (dx * dx + dy * dy > r2) continue;
            if (w.at(x, y).mat != MAT_EMPTY) continue;   /* never overwrite */
            /* Never build inside an occupied entity box, or you entomb the
               character in their own material and the unstick rule has to
               shove them back out again. */
            if (w.blocksCell(x, y)) continue;
            const ItemId want = h.item;
            if (inv.take(want, 1) != 1) return put;      /* ran out mid-disc */
            w.setCell(x, y, (u8)want);
            ++put;
        }
    }
    return put;
}
