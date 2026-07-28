#include "item.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ItemDef   ITEMS[ITEM_COUNT];
Inventory g_inv;
ToolInst  g_toolInst[MAX_TOOL_INST];

u16 toolInstNew() {
    for (int i = 1; i < MAX_TOOL_INST; ++i) {
        if (g_toolInst[i].used) continue;
        memset(&g_toolInst[i], 0, sizeof(ToolInst));
        g_toolInst[i].used = true;
        return (u16)i;
    }
    return 0;   /* pool full; the caller gets a tool with no state, which still
                   works as an empty tool rather than crashing */
}

void toolInstFree(u16 inst) {
    if (inst == 0 || inst >= MAX_TOOL_INST) return;
    memset(&g_toolInst[inst], 0, sizeof(ToolInst));
}

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
    initSprites();

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

    /* Tiers. What separates them is slot count first and rate of fire second,
       because slots change what the tool can BE and delay only changes how fast
       it is at being it -- and an upgrade you have to re-plan around is worth
       more than one that just multiplies a number. */
    ITEMS[ITEM_MULTITOOL].name      = "Multitool Mk I";
    ITEMS[ITEM_MULTITOOL].kind      = ITEMK_TOOL;
    ITEMS[ITEM_MULTITOOL].maxStack  = 1;
    ITEMS[ITEM_MULTITOOL].colour    = 0xC8B070;
    ITEMS[ITEM_MULTITOOL].toolSlots = 3;
    ITEMS[ITEM_MULTITOOL].baseDelay = 18;
    ITEMS[ITEM_MULTITOOL].sprite    = SPR_TOOL1;

    ITEMS[ITEM_MULTITOOL2].name      = "Multitool Mk II";
    ITEMS[ITEM_MULTITOOL2].kind      = ITEMK_TOOL;
    ITEMS[ITEM_MULTITOOL2].maxStack  = 1;
    ITEMS[ITEM_MULTITOOL2].colour    = 0xE0D090;
    ITEMS[ITEM_MULTITOOL2].toolSlots = 5;
    ITEMS[ITEM_MULTITOOL2].baseDelay = 11;
    ITEMS[ITEM_MULTITOOL2].sprite    = SPR_TOOL2;

    /* The starting module. Power STR_LOOSE means it clears sand and dirt and is
       stopped dead by stone -- which is the entire tech gate, expressed as one
       number.

       Pierce is 10 so one shot bores a visible tunnel rather than stopping at
       the first grain: it has to read as a beam, not a pebble. Even so, this is
       NOT yet a faster way to move dirt than your hands -- 10 cells every 18
       frames is 33 cells a second against the hands' 100. What it buys is
       range, and a socket for everything that comes after. Overtaking bare
       hands on raw throughput is what the ladder is for. */
    ITEMS[ITEM_MOD_SHOT].name       = "Shot Module";
    ITEMS[ITEM_MOD_SHOT].kind       = ITEMK_MODULE;
    ITEMS[ITEM_MOD_SHOT].maxStack   = 1;
    ITEMS[ITEM_MOD_SHOT].colour     = 0x70D0FF;
    ITEMS[ITEM_MOD_SHOT].addDelay   = 0;
    ITEMS[ITEM_MOD_SHOT].power      = STR_LOOSE;
    ITEMS[ITEM_MOD_SHOT].pierce     = 10;
    ITEMS[ITEM_MOD_SHOT].shotColour = 0x9CE0FF;
    ITEMS[ITEM_MOD_SHOT].sprite     = SPR_MOD_SHOT;

    /* The blast module. Power STR_ROCK breaks stone -- it is the answer to the
       wall the starting shot bounces off -- but not metal, so the ladder still
       has a rung above it.

       Pierce 1 is doing real work rather than being a small number: the shot
       must stop at the FIRST thing it touches, or it would bore inside a wall
       and detonate in the middle of it, which both looks wrong and wastes most
       of the blast on cells behind the surface. Explosions belong on the face
       of what you hit. The delay is the cost, and it is a big one: 42 frames
       against the plain shot's 18. */
    ITEMS[ITEM_MOD_BLAST].name       = "Blast Module";
    ITEMS[ITEM_MOD_BLAST].kind       = ITEMK_MODULE;
    ITEMS[ITEM_MOD_BLAST].maxStack   = 1;
    ITEMS[ITEM_MOD_BLAST].colour     = 0xFFB040;
    ITEMS[ITEM_MOD_BLAST].addDelay   = 24;
    /* Above STR_ROCK rather than equal to it. With falloff, a blast whose
       power exactly matches a material's strength only breaks the handful of
       cells at the dead centre -- see the note in explodeAt(). 120 sits between
       stone's 90 and metal's 150, so it carves a wide crater in stone, a wider
       one in loose ground, and cannot touch iron at all. */
    ITEMS[ITEM_MOD_BLAST].power      = 120;
    ITEMS[ITEM_MOD_BLAST].pierce     = 1;
    ITEMS[ITEM_MOD_BLAST].blast      = 14;
    ITEMS[ITEM_MOD_BLAST].shotColour = 0xFFC060;
    ITEMS[ITEM_MOD_BLAST].sprite     = SPR_MOD_BLAST;

    /* --- the mining ladder --------------------------------------------
       Four tiers between bare hands and "clear whatever you want".

       Both numbers move together on purpose. Radius alone would let you outline
       an enormous hole and then wait an age for it to fill in; rate alone would
       have you scrubbing a tiny brush back and forth. What each tier actually
       sells is AREA PER SECOND, and the felt difference is being able to take a
       room-sized bite in one sweep instead of forty.

       Throughput against bare hands, which move 100 cells a second:

         Hand Drill      r10   16 / 5f  =  192/s    2x
         Rock Auger      r16   30 / 5f  =  360/s    3.6x
         Thermal Lance   r24   60 / 4f  =  900/s    9x
         Disruptor       r40  140 / 3f  = 2800/s   28x

       The top of the ladder clears a full radius-40 disc -- 5025 cells -- in
       under two seconds, which is the "basically whatever size you want" end
       of it. Nothing here touches placement: see ITEMK_MINING in item.h. */
    struct MineTier { ItemId id; const char* name; u8 r, bite, cool; u32 col; u8 spr; };
    static const MineTier MINE[] = {
        { ITEM_DRILL,     "Hand Drill",     10,  16, 5, 0xB07848, SPR_MINE1 },
        { ITEM_AUGER,     "Rock Auger",     16,  30, 5, 0x9AA6B4, SPR_MINE2 },
        { ITEM_LANCE,     "Thermal Lance",  24,  60, 4, 0xE0B048, SPR_MINE3 },
        { ITEM_DISRUPTOR, "Disruptor",      40, 140, 3, 0xB070E8, SPR_MINE4 },
    };
    for (int i = 0; i < (int)(sizeof(MINE) / sizeof(MINE[0])); ++i) {
        const MineTier& t = MINE[i];
        ITEMS[t.id].name         = t.name;
        ITEMS[t.id].kind         = ITEMK_MINING;
        ITEMS[t.id].maxStack     = 1;
        ITEMS[t.id].colour       = t.col;
        ITEMS[t.id].mineRadius   = t.r;
        ITEMS[t.id].mineBite     = t.bite;
        ITEMS[t.id].mineCooldown = t.cool;
        ITEMS[t.id].sprite       = t.spr;
    }

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

/* Returning a tool's instance to the pool when the tool leaves the pack. Miss
   this and the pool leaks: pick up and drop a multitool 32 times and the next
   one comes out with no state at all, with nothing anywhere saying why. */
static void releaseStack(ItemStack& s) {
    if (s.inst) { toolInstFree(s.inst); s.inst = 0; }
}

void Inventory::clear() {
    for (int i = 0; i < INV_SLOTS; ++i) releaseStack(slot[i]);
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
        /* A tool becomes a distinct object the moment it exists. Doing this
           here rather than at every call site means there is no way to end up
           holding a multitool that cannot remember its own modules. */
        slot[i].inst  = (ITEMS[item].kind == ITEMK_TOOL) ? toolInstNew() : 0;
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
        if (slot[selected].count == 0) { releaseStack(slot[selected]); slot[selected].item = ITEM_NONE; }
        taken += got;
        count -= got;
    }
    for (int i = 0; i < INV_SLOTS && count > 0; ++i) {
        if (slot[i].item != item || slot[i].count == 0) continue;
        const int got = (count < slot[i].count) ? count : slot[i].count;
        slot[i].count = (u16)(slot[i].count - got);
        if (slot[i].count == 0) { releaseStack(slot[i]); slot[i].item = ITEM_NONE; }
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

ToolSpec miningSpec(const ItemStack& held) {
    if (!held.empty()) {
        const ItemDef& d = ITEMS[held.item];
        if (d.kind == ITEMK_MINING && d.mineRadius > 0) {
            ToolSpec s;
            s.name         = d.name;
            s.maxRadius    = d.mineRadius;
            s.cellsPerBite = d.mineBite;
            s.cooldown     = d.mineCooldown;
            return s;
        }
    }
    return HAND;
}

int Inventory::firstToolSlot() const {
    for (int i = 0; i < INV_SLOTS; ++i)
        if (!slot[i].empty() && ITEMS[slot[i].item].kind == ITEMK_TOOL) return i;
    return -1;
}

ToolShot toolResolve(const ItemStack& st) {
    ToolShot s;
    s.canFire = false; s.delay = 0; s.power = 0; s.pierce = 0; s.blast = 0;
    s.colour = 0xFFFFFF;
    if (st.empty() || ITEMS[st.item].kind != ITEMK_TOOL) return s;

    const ItemDef& tool = ITEMS[st.item];
    s.delay = tool.baseDelay;
    if (st.inst == 0 || st.inst >= MAX_TOOL_INST) return s;   /* no state: a stick */

    const ToolInst& ti = g_toolInst[st.inst];
    const int n = imin(tool.toolSlots, TOOL_SLOTS_MAX);
    for (int i = 0; i < n; ++i) {
        const ItemId m = ti.slot[i];
        if (m == ITEM_NONE || ITEMS[m].kind != ITEMK_MODULE) continue;
        /* The FIRST module decides what the shot is; later ones only add to the
           delay. That is a placeholder with a deliberate shape: it means slot
           order already matters, so when modules start combining, "leftmost is
           the shot, the rest modify it" is a rule players will already have
           learned rather than a new one being introduced. */
        if (!s.canFire) {
            s.canFire = true;
            s.power   = ITEMS[m].power;
            s.pierce  = ITEMS[m].pierce;
            s.blast   = ITEMS[m].blast;
            s.colour  = ITEMS[m].shotColour;
        }
        s.delay += ITEMS[m].addDelay;
    }
    return s;
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

/* --- the background layer --------------------------------------------------
   Same disc walk as the foreground verbs, same nearest-first ordering, same
   inventory accounting. What differs is what they touch and what stops them. */

int placeBg(World& w, Inventory& inv, int cx, int cy, int r, int maxCells) {
    ItemStack& h = inv.held();
    if (h.empty() || ITEMS[h.item].kind != ITEMK_MATERIAL) return 0;
    /* Air has no back. Placing "nothing" is the job of digBg. */
    if (h.item == MAT_EMPTY) return 0;

    int put = 0;
    const int n = g_discEnd[imax(0, imin(r, DISC_MAX_R))];
    for (int i = 0; i < n; ++i) {
        if (maxCells > 0 && put >= maxCells) break;
        const int x = cx + g_disc[i].dx, y = cy + g_disc[i].dy;
        if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) continue;
        /* Never overwrite an existing backdrop -- same rule the foreground has,
           and for the same reason: a brush that silently replaces what is
           already there destroys work you cannot get back by undoing a click.
           Scrape it off first if you want to change it. */
        if (w.bgAt(x, y) != MAT_EMPTY) continue;
        /* NO check against material in front. Walling in behind a floor you are
           standing on is the normal way to build a room, and refusing it would
           mean digging out a wall just to back it. */
        const ItemId want = h.item;
        if (inv.take(want, 1) != 1) return put;
        w.setBg(x, y, (u8)want, true);
        ++put;
    }
    return put;
}

int digBg(World& w, Inventory& inv, int cx, int cy, int r, int maxCells) {
    int dug = 0;
    const int n = g_discEnd[imax(0, imin(r, DISC_MAX_R))];
    for (int i = 0; i < n; ++i) {
        if (maxCells > 0 && dug >= maxCells) break;
        const int x = cx + g_disc[i].dx, y = cy + g_disc[i].dy;
        if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) continue;
        const u8 b = w.bgAt(x, y);
        if (b == MAT_EMPTY) continue;

        /* Everything can be broken. Only what a PLAYER put there drops.

           Those are two separate questions and it is worth keeping them
           separate: being unable to clear natural rock off the back wall would
           make half the world unfinishable to build in, while letting it drop
           items would turn every tunnel into an infinite quarry. Breaking it
           reveals the chunk's zone behind -- cave dark, or sky -- which is a
           real thing to look at rather than a hole in the world. */
        if (w.bgPlaced(x, y)) {
            if (inv.add((ItemId)b, 1) != 0) continue;   /* pack full: leave it */
        }
        w.clearBg(x, y);
        ++dug;
    }
    return dug;
}
