#include "item.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ItemDef   ITEMS[ITEM_COUNT];
Inventory g_inv;

const ToolSpec HAND = { "Hands", 6, 10, 6 };

DiscOff g_disc[DISC_MAX_CELLS];
int     g_discEnd[DISC_MAX_R + 1];

/* Sorted by squared distance, so the ordering is exact integer arithmetic --
   sorting by a float distance would put cells at the same true radius in an
   order that depends on rounding, and the ring boundaries would fray. */
static int discCmp(const void* a, const void* b) {
    const DiscOff* p = (const DiscOff*)a;
    const DiscOff* q = (const DiscOff*)b;
    const int pd = p->dx * p->dx + p->dy * p->dy;
    const int qd = q->dx * q->dx + q->dy * q->dy;
    if (pd != qd) return pd - qd;
    /* Ties broken by position rather than left to qsort, so the table is the
       same on every run and a test that depends on bite order is repeatable. */
    if (p->dy != q->dy) return p->dy - q->dy;
    return p->dx - q->dx;
}

void initDiscTable() {
    int n = 0;
    const int r2 = DISC_MAX_R * DISC_MAX_R;
    for (int dy = -DISC_MAX_R; dy <= DISC_MAX_R; ++dy)
        for (int dx = -DISC_MAX_R; dx <= DISC_MAX_R; ++dx) {
            if (dx * dx + dy * dy > r2) continue;
            if (n >= DISC_MAX_CELLS) {
                fprintf(stderr, "disc table overflow -- raise DISC_MAX_CELLS\n");
                abort();
            }
            g_disc[n].dx = (i16)dx;
            g_disc[n].dy = (i16)dy;
            ++n;
        }
    qsort(g_disc, n, sizeof(DiscOff), discCmp);

    /* Prefix lengths. One walk of the sorted table fills every radius at once,
       since a bigger radius can only ever extend a smaller one's prefix. */
    int i = 0;
    for (int r = 0; r <= DISC_MAX_R; ++r) {
        const int rr = r * r;
        while (i < n && g_disc[i].dx * g_disc[i].dx + g_disc[i].dy * g_disc[i].dy <= rr) ++i;
        g_discEnd[r] = i;
    }
}

/* Forgetting initItems() does not crash, it does something far worse: every
   stack limit is zero, so the inventory politely refuses every item and the
   hotbar renders ten empty slots. That looks exactly like "the inventory does
   not work yet" rather than "you skipped a setup call", and it cost a
   screenshot and a confused ten minutes before this guard existed. */
static bool g_itemsReady = false;

void initItems() {
    memset(ITEMS, 0, sizeof(ITEMS));
    g_itemsReady = true;
    initDiscTable();

    /* Materials describe themselves. Name comes straight from MATS[] and the
       swatch from the colour LUT at a mid tint, which is the same sample the
       palette buttons use -- so a stack of stone in the hotbar is exactly the
       colour stone is in the world, automatically, forever. */
    for (int m = 0; m < MAT_COUNT; ++m) {
        ITEMS[m].name     = MATS[m].name;
        ITEMS[m].kind     = ITEMK_MATERIAL;
        ITEMS[m].maxStack = 9999;
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

    /* Reach extenders. Two tiers so the ladder is visible; the numbers are
       relative to a base reach of 56, so the lens is "half again as far" and
       the relay is "twice as far". Both take a whole inventory slot to carry,
       which is the entire cost and is meant to bite once the pack is full of
       ore. */
    ITEMS[ITEM_LENS].name       = "Focusing Lens";
    ITEMS[ITEM_LENS].kind       = ITEMK_CARRIED;
    ITEMS[ITEM_LENS].maxStack   = 1;
    ITEMS[ITEM_LENS].colour     = 0x8FD8E8;
    ITEMS[ITEM_LENS].reachBonus = 28;

    ITEMS[ITEM_RELAY].name       = "Field Relay";
    ITEMS[ITEM_RELAY].kind       = ITEMK_CARRIED;
    ITEMS[ITEM_RELAY].maxStack   = 1;
    ITEMS[ITEM_RELAY].colour     = 0xB088E0;
    ITEMS[ITEM_RELAY].reachBonus = 56;
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

int Inventory::reachBonus() const {
    int best = 0;
    for (int i = 0; i < INV_SLOTS; ++i) {
        if (slot[i].empty()) continue;
        const int b = ITEMS[slot[i].item].reachBonus;
        if (b > best) best = b;
    }
    return best;
}

int digInto(World& w, Inventory& inv, int cx, int cy, int r, int maxCells) {
    int dug = 0;
    const int n = g_discEnd[imax(0, imin(r, DISC_MAX_R))];
    for (int i = 0; i < n; ++i) {
        if (maxCells > 0 && dug >= maxCells) break;
        const int x = cx + g_disc[i].dx, y = cy + g_disc[i].dy;
        if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) continue;
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
    return dug;
}

int placeFrom(World& w, Inventory& inv, int cx, int cy, int r, int maxCells) {
    ItemStack& h = inv.held();
    if (h.empty() || ITEMS[h.item].kind != ITEMK_MATERIAL) return 0;

    int put = 0;
    const int n = g_discEnd[imax(0, imin(r, DISC_MAX_R))];
    for (int i = 0; i < n; ++i) {
        if (maxCells > 0 && put >= maxCells) break;
        const int x = cx + g_disc[i].dx, y = cy + g_disc[i].dy;
        if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) continue;
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
    return put;
}
