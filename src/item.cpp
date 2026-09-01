#include "item.h"
#include "entity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ItemDef   ITEMS[ITEM_COUNT];
ToolInst  g_toolInst[MAX_TOOL_INST];

u16 toolInstNew(ItemId tool) {
    for (int i = 1; i < MAX_TOOL_INST; ++i) {
        if (g_toolInst[i].used) continue;
        memset(&g_toolInst[i], 0, sizeof(ToolInst));
        g_toolInst[i].used = true;
        if (tool < ITEM_COUNT && ITEMS[tool].kind == ITEMK_TOOL) {
            g_toolInst[i].energyCapacity = ITEMS[tool].energyCapacity;
            g_toolInst[i].energyRecharge = ITEMS[tool].energyRecharge;
            g_toolInst[i].energy = ITEMS[tool].energyCapacity;
        }
        return (u16)i;
    }
    return 0;   /* pool full; the caller gets a tool with no state, which still
                   works as an empty tool rather than crashing */
}

void toolInstFree(u16 inst) {
    if (inst == 0 || inst >= MAX_TOOL_INST) return;
    memset(&g_toolInst[inst], 0, sizeof(ToolInst));
}

/* See the note in item.h for what this repairs and why it cannot be skipped. */
void toolInstReconcile(Inventory& inv) {
    bool referenced[MAX_TOOL_INST];
    memset(referenced, 0, sizeof(referenced));

    ItemStack* stacks[INV_SLOTS + EQ_COUNT];
    int n = 0;
    for (int i = 0; i < INV_SLOTS; ++i) stacks[n++] = &inv.slot[i];
    for (int i = 0; i < EQ_COUNT;   ++i) stacks[n++] = &inv.equip[i];

    for (int i = 0; i < n; ++i) {
        ItemStack& s = *stacks[i];
        if (s.inst == 0) continue;
        /* A reference out of range, or from something that is not a tool, is
           corruption rather than state -- drop it rather than trust it. */
        if (s.inst >= MAX_TOOL_INST || s.empty() ||
            ITEMS[s.item].kind != ITEMK_TOOL) { s.inst = 0; continue; }
        /* Two stacks naming one instance would share modules and ammo, which is
           worse than either losing them. The first keeps it; the second gets a
           blank one. */
        if (referenced[s.inst]) { s.inst = toolInstNew(s.item); if (!s.inst) continue; }
        referenced[s.inst] = true;
        /* ADOPT it. For a save written before the pool was persisted this is
           an empty tool rather than the loadout you had -- the modules were
           never written and cannot be conjured back -- but an empty tool that
           works beats a tool frozen after one shot. */
        g_toolInst[s.inst].used = true;
        ToolInst& ti = g_toolInst[s.inst];
        const u16 capacity = ITEMS[s.item].energyCapacity;
        /* A pre-energy save has zero in all new fields. Adopt it as a fully
           charged chassis; a load should not turn a working weapon into an
           unexplained empty battery. Existing charge is clamped when balance
           changes the chassis capacity. */
        if (!ti.energyCapacity) ti.energy = capacity;
        ti.energyCapacity = capacity;
        ti.energyRecharge = ITEMS[s.item].energyRecharge;
        if (ti.energy > capacity) ti.energy = capacity;
        /* A cooldown is transient. Restoring one means loading into a fraction
           of a firing delay, which is meaningless, and a corrupt one would
           reproduce the exact bug this function exists to fix. */
        g_toolInst[s.inst].cooldown = 0;
    }

    for (int i = 1; i < MAX_TOOL_INST; ++i)
        if (!referenced[i] && g_toolInst[i].used) toolInstFree(i);
}

void toolInstTick() {
    for (int i = 1; i < MAX_TOOL_INST; ++i) {
        ToolInst& ti = g_toolInst[i];
        if (!ti.used) continue;
        if (ti.cooldown > 0) --ti.cooldown;
        if (ti.energy < ti.energyCapacity) {
            ti.energy = (u16)imin((int)ti.energyCapacity,
                                  (int)ti.energy + (int)ti.energyRecharge);
        }
    }
}

/* STR_HARD, not something lower, and that is a deliberate non-change: before
   the layer barriers there was no strength gate on digging at all, so bare
   hands could already clear tungsten given the time. Introducing a gate is not
   an excuse to quietly re-balance the mining ladder underneath it -- the only
   material anything here cannot bite is MAT_STRATUM. */
const ToolSpec HAND = { "Hands", 7, 12, 6, false, STR_HARD };

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
    /* equipSlot's "not equipment" value is EQ_COUNT, not zero, so it cannot be
       left to the memset -- zero is EQ_FEET, and every material in the game
       would have claimed the boots slot. The alternative is a separate
       "isEquipment" flag, which is two fields that can disagree about the same
       thing; one field with an out-of-range sentinel cannot. */
    for (int i = 0; i < ITEM_COUNT; ++i) ITEMS[i].equipSlot = EQ_COUNT;
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
        ITEMS[m].maxStack = MATERIAL_STACK;
        ITEMS[m].colour   = g_colorLut[(m << 8) | 0x08];
    }
    /* Air is not a thing you can carry. Leaving it named and stackable would
       make "empty slot" and "a stack of nothing" two different states that look
       identical in the UI. */
    ITEMS[MAT_EMPTY].name     = "";
    ITEMS[MAT_EMPTY].maxStack = 0;

    /* Nor is an open door, on the same grounds: it is a door that happens to be
       open. Leaving it carryable would give the world two items meaning one
       object, stacking separately and painting a doorway that can never be
       shut. g_matDropsAs is what makes this safe -- mining one still pays out,
       it just pays out a door. */
    ITEMS[MAT_DOOR_OPEN].name     = "";
    ITEMS[MAT_DOOR_OPEN].maxStack = 0;

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
    ITEMS[ITEM_MULTITOOL].energyCapacity = 120;
    ITEMS[ITEM_MULTITOOL].energyRecharge = 1;
    ITEMS[ITEM_MULTITOOL].sprite    = SPR_TOOL1;

    ITEMS[ITEM_MULTITOOL2].name      = "Multitool Mk II";
    ITEMS[ITEM_MULTITOOL2].kind      = ITEMK_TOOL;
    ITEMS[ITEM_MULTITOOL2].maxStack  = 1;
    ITEMS[ITEM_MULTITOOL2].colour    = 0xE0D090;
    ITEMS[ITEM_MULTITOOL2].toolSlots = 5;
    ITEMS[ITEM_MULTITOOL2].baseDelay = 11;
    ITEMS[ITEM_MULTITOOL2].energyCapacity = 300;
    ITEMS[ITEM_MULTITOOL2].energyRecharge = 2;
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
    ITEMS[ITEM_MOD_SHOT].energyCost = 20;
    ITEMS[ITEM_MOD_SHOT].power      = STR_LOOSE;
    /* 6 against a rock mite's 18 hp: three hits. Deliberately not two -- the
       first weapon in the game should make a layer 1 creature killable rather
       than trivial, since "get a weapon" is the whole difficulty curve of the
       first layer and it should feel like it resolved something. */
    ITEMS[ITEM_MOD_SHOT].damage     = 7;
    ITEMS[ITEM_MOD_SHOT].pierce     = 10;
    ITEMS[ITEM_MOD_SHOT].shotColour = 0x9CE0FF;
    /* The one thing in the game that does not fall. It is a BEAM -- see the
       note above about reading as one rather than as a pebble -- and it is also
       the mining weapon, so this is structural and not decorative: with pierce
       10 it is meant to bore a straight tunnel, and an arcing beam curves into
       the floor a few cells out and digs a ditch instead. */
    ITEMS[ITEM_MOD_SHOT].shotBeam   = 1;
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
    ITEMS[ITEM_MOD_BLAST].addDelay   = 20;
    ITEMS[ITEM_MOD_BLAST].energyCost = 65;
    /* Above STR_ROCK rather than equal to it. With falloff, a blast whose
       power exactly matches a material's strength only breaks the handful of
       cells at the dead centre -- see the note in explodeAt(). 120 sits between
       stone's 90 and metal's 150, so it carves a wide crater in stone, a wider
       one in loose ground, and cannot touch iron at all. */
    ITEMS[ITEM_MOD_BLAST].power      = 120;
    /* Enough to one-shot anything in layer 1, and it costs 24 frames of extra
       delay plus a hole in the floor to do it. The blast module is the answer
       to a group, not to a single creature. */
    ITEMS[ITEM_MOD_BLAST].damage     = 24;
    ITEMS[ITEM_MOD_BLAST].pierce     = 1;
    ITEMS[ITEM_MOD_BLAST].blast      = 14;
    ITEMS[ITEM_MOD_BLAST].shotColour = 0xFFC060;
    ITEMS[ITEM_MOD_BLAST].sprite     = SPR_MOD_BLAST;

    /* Cheap suppression fire. It gives up terrain power and damage for speed,
       four visible ricochets, and an energy draw the Mk I can sustain. Unlike
       a beam it arcs under ordinary gravity, making bank shots physical rather
       than a weightless pinball line. */
    ITEMS[ITEM_MOD_BOUNCE].name       = "Bounce Module";
    ITEMS[ITEM_MOD_BOUNCE].kind       = ITEMK_MODULE;
    ITEMS[ITEM_MOD_BOUNCE].maxStack   = 1;
    ITEMS[ITEM_MOD_BOUNCE].colour     = 0x72E09A;
    ITEMS[ITEM_MOD_BOUNCE].addDelay   = -5;
    ITEMS[ITEM_MOD_BOUNCE].energyCost = 7;
    ITEMS[ITEM_MOD_BOUNCE].power      = 0;
    ITEMS[ITEM_MOD_BOUNCE].damage     = 4;
    ITEMS[ITEM_MOD_BOUNCE].pierce     = 1;
    ITEMS[ITEM_MOD_BOUNCE].shotColour = 0x72E09A;
    ITEMS[ITEM_MOD_BOUNCE].shotSpeed  = 5.5f;
    ITEMS[ITEM_MOD_BOUNCE].shotBeam   = 0;
    ITEMS[ITEM_MOD_BOUNCE].shotBounces = 4;
    ITEMS[ITEM_MOD_BOUNCE].shotLife   = 150;
    ITEMS[ITEM_MOD_BOUNCE].sprite     = SPR_MOD_BOUNCE;

    /* A slow floating seeker: quick trigger cadence, deliberately expensive
       battery draw. Its steering is gentle enough to curve visibly instead of
       snapping to a target, and it cannot mine terrain. */
    ITEMS[ITEM_MOD_HOMING].name       = "Homing Module";
    ITEMS[ITEM_MOD_HOMING].kind       = ITEMK_MODULE;
    ITEMS[ITEM_MOD_HOMING].maxStack   = 1;
    ITEMS[ITEM_MOD_HOMING].colour     = 0xD59CFF;
    ITEMS[ITEM_MOD_HOMING].addDelay   = -4;
    ITEMS[ITEM_MOD_HOMING].energyCost = 55;
    ITEMS[ITEM_MOD_HOMING].power      = 0;
    ITEMS[ITEM_MOD_HOMING].damage     = 9;
    ITEMS[ITEM_MOD_HOMING].pierce     = 1;
    ITEMS[ITEM_MOD_HOMING].shotColour = 0xD59CFF;
    ITEMS[ITEM_MOD_HOMING].shotSpeed  = 1.65f;
    ITEMS[ITEM_MOD_HOMING].shotBeam   = 1;
    ITEMS[ITEM_MOD_HOMING].shotHoming = 0.085f;
    ITEMS[ITEM_MOD_HOMING].shotLife   = 180;
    ITEMS[ITEM_MOD_HOMING].sprite     = SPR_MOD_HOMING;

    /* The quick-tap R teleport stays. This is its weapon-system counterpart:
       the player moves to the last safe point before projectile impact. One
       Mk I charge buys a shot; Mk II's larger bank and recharge are what make
       repeated combat use practical. */
    ITEMS[ITEM_MOD_TELEPORT].name       = "Teleport Module";
    ITEMS[ITEM_MOD_TELEPORT].kind       = ITEMK_MODULE;
    ITEMS[ITEM_MOD_TELEPORT].maxStack   = 1;
    ITEMS[ITEM_MOD_TELEPORT].colour     = 0xFF72D8;
    ITEMS[ITEM_MOD_TELEPORT].addDelay   = 8;
    ITEMS[ITEM_MOD_TELEPORT].energyCost = 110;
    ITEMS[ITEM_MOD_TELEPORT].power      = 0;
    ITEMS[ITEM_MOD_TELEPORT].damage     = 0;
    ITEMS[ITEM_MOD_TELEPORT].pierce     = 1;
    ITEMS[ITEM_MOD_TELEPORT].shotColour = 0xFF72D8;
    ITEMS[ITEM_MOD_TELEPORT].shotSpeed  = 4.0f;
    ITEMS[ITEM_MOD_TELEPORT].shotBeam   = 1;
    ITEMS[ITEM_MOD_TELEPORT].shotLife   = 140;
    ITEMS[ITEM_MOD_TELEPORT].shotEffect = PROJ_EFFECT_TELEPORT;
    ITEMS[ITEM_MOD_TELEPORT].sprite     = SPR_MOD_TELEPORT;

    /* --- the warp wand ------------------------------------------------
       Teleportation at iron prices: the module's trick, made slow and
       rationed instead of instant and plentiful.

       What costs you is the BATTERY, not the cooldown. Capacity and cost are
       the same number, so the wand holds exactly one jump and then has to
       fill back up at one unit a frame -- three seconds where you cannot go
       anywhere, which is the whole character of the thing. baseDelay is
       shorter than that on purpose: the empty bar is what should be telling
       you to wait, and two mechanisms saying it would just be one of them
       being ignored.

       Range is what the shot's lifetime buys: 52 frames at 2.6 cells is
       about a hundred and thirty cells. Firing further than that is not a
       wasted charge -- the bolt delivers wherever it stops, including
       simply running out of flight -- it just takes you as far as it got
       rather than to the thing you were aiming at. So the honest
       description is a fixed hop with a ceiling, not a grapple.

       No damage, and a beam so it does not arc into the floor at this
       speed. Pierce 1 so it stops at the first thing it meets, which is
       where you want to arrive. */
    ITEMS[ITEM_WARP_WAND].name           = "Warp Wand";
    ITEMS[ITEM_WARP_WAND].kind           = ITEMK_TOOL;
    ITEMS[ITEM_WARP_WAND].maxStack       = 1;
    ITEMS[ITEM_WARP_WAND].colour         = 0xFF72D8;
    ITEMS[ITEM_WARP_WAND].toolSlots      = 0;
    ITEMS[ITEM_WARP_WAND].baseDelay      = 150;
    ITEMS[ITEM_WARP_WAND].power          = 0;
    ITEMS[ITEM_WARP_WAND].damage         = 0;
    ITEMS[ITEM_WARP_WAND].pierce         = 1;
    ITEMS[ITEM_WARP_WAND].energyCapacity = 180;
    ITEMS[ITEM_WARP_WAND].energyRecharge = 1;
    ITEMS[ITEM_WARP_WAND].energyCost     = 180;
    ITEMS[ITEM_WARP_WAND].shotColour     = 0xFF72D8;
    ITEMS[ITEM_WARP_WAND].shotSpeed      = 2.6f;
    ITEMS[ITEM_WARP_WAND].shotBeam       = 1;
    ITEMS[ITEM_WARP_WAND].shotLife       = 52;
    ITEMS[ITEM_WARP_WAND].shotEffect     = PROJ_EFFECT_TELEPORT;
    ITEMS[ITEM_WARP_WAND].sprite         = SPR_WARP_WAND;

    ITEMS[ITEM_SPARK].name        = "Spark";
    ITEMS[ITEM_SPARK].kind        = ITEMK_SPARK;
    ITEMS[ITEM_SPARK].maxStack    = 999;
    ITEMS[ITEM_SPARK].colour      = 0xFFD870;
    ITEMS[ITEM_SPARK].sprite      = SPR_SPARK;

    /* --- the mining ladder --------------------------------------------
       Four tiers between bare hands and "clear whatever you want".

       Both numbers move together on purpose. Radius alone would let you outline
       an enormous hole and then wait an age for it to fill in; rate alone would
       have you scrubbing a tiny brush back and forth. What each tier actually
       sells is AREA PER SECOND, and the felt difference is being able to take a
       room-sized bite in one sweep instead of forty.

       Throughput against bare hands, which move 120 cells a second:

         Hand Drill      r12   20 / 5f  =  240/s    2x
         Rock Auger      r19   36 / 5f  =  432/s    3.6x
         Thermal Lance   r29   72 / 4f  = 1080/s    9x
         Disruptor       r48  168 / 3f  = 3360/s   28x

       The top of the ladder clears a full radius-48 disc -- 7213 cells -- in
       under two seconds, which is the "basically whatever size you want" end
       of it. Nothing here touches placement: see ITEMK_MINING in item.h. */
    struct MineTier { ItemId id; const char* name; u8 r, bite, cool; u32 col; u8 spr; };
    static const MineTier MINE[] = {
        { ITEM_DRILL,     "Hand Drill",     12,  20, 5, 0xB07848, SPR_MINE1 },
        { ITEM_AUGER,     "Rock Auger",     19,  36, 5, 0x9AA6B4, SPR_MINE2 },
        { ITEM_LANCE,     "Thermal Lance",  29,  72, 4, 0xE0B048, SPR_MINE3 },
        { ITEM_DISRUPTOR, "Disruptor",      48, 168, 3, 0xB070E8, SPR_MINE4 },
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
        /* Uniform across the whole ladder, on purpose. These four tiers are a
           ladder of SPEED and REACH and always have been; making the top one
           also the only one that can bite hard rock would silently re-tier
           every material in the game. When something is built that beats
           MAT_STRATUM it will be a new tier with a new number here, not a
           quiet promotion of the Disruptor. */
        ITEMS[t.id].minePower    = STR_HARD;
        ITEMS[t.id].sprite       = t.spr;
        ITEMS[t.id].description  = "Excavates terrain in a wide area around the cursor.";
    }

    /* Grass seed. Stacks like a material because you use it by the handful,
       but it is ITEMK_SEED: it converts a cell rather than becoming one. */
    ITEMS[ITEM_GRASS_SEED].name     = "Grass Seed";
    ITEMS[ITEM_GRASS_SEED].kind     = ITEMK_SEED;
    ITEMS[ITEM_GRASS_SEED].maxStack = MATERIAL_STACK;
    ITEMS[ITEM_GRASS_SEED].colour   = 0x8FC85A;
    ITEMS[ITEM_GRASS_SEED].sprite   = SPR_SEED;

    /* --- the sickle -------------------------------------------------------
       Not a tier. It is slower per cell than the Rock Auger and it will not
       touch stone, dirt or grass at all -- what it buys is that a swing at a
       tree takes the tree.

       That is worth a slot because the alternative is what the game did
       before: harvesting anything meant a drill, a drill takes a disc out of
       whatever is behind what you were aiming at, and reaping a field left the
       field as a crater you then had to fill in. A tool that cannot damage the
       ground is a tool you can use carelessly, and carelessness is the whole
       point of a harvesting pass.

       Generous on radius (22) and bite (48) because there is nothing to be
       careful ABOUT: the filter is the safety, so the tool may as well be
       broad. That is also what makes it feel like a scythe rather than a
       smaller drill -- one sweep takes a row. */
    ITEMS[ITEM_SICKLE].name           = "Harvesting Sickle";
    ITEMS[ITEM_SICKLE].kind           = ITEMK_MINING;
    ITEMS[ITEM_SICKLE].maxStack       = 1;
    ITEMS[ITEM_SICKLE].colour         = 0x8CD44C;
    ITEMS[ITEM_SICKLE].mineRadius     = 22;
    ITEMS[ITEM_SICKLE].mineBite       = 48;
    ITEMS[ITEM_SICKLE].mineCooldown   = 5;
    ITEMS[ITEM_SICKLE].minePlantsOnly = 1;
    /* Set explicitly rather than left at the zero the table is cleared to. A
       mining tool with minePower 0 cuts NOTHING -- every plant is at least
       STR_LOOSE -- so forgetting this line does not degrade the sickle, it
       disables it. The tiers above get theirs from the MINE[] loop; this one is
       built by hand and so has to say it by hand. */
    ITEMS[ITEM_SICKLE].minePower      = STR_HARD;
    ITEMS[ITEM_SICKLE].sprite         = SPR_MINE1;

    /* Reach extenders. Two tiers so the ladder is visible; the numbers are
       relative to a base reach of 56, so the lens is "half again as far" and
       the relay is "twice as far". Both take a whole inventory slot to carry,
       which is the entire cost and is meant to bite once the pack is full of
       ore. */
    ITEMS[ITEM_LENS].name       = "Focusing Lens";
    ITEMS[ITEM_LENS].kind       = ITEMK_ACCESSORY;
    ITEMS[ITEM_LENS].equipSlot  = EQ_TRINKET_A;
    ITEMS[ITEM_LENS].maxStack   = 1;
    ITEMS[ITEM_LENS].colour     = 0x8FD8E8;
    ITEMS[ITEM_LENS].sprite     = SPR_LENS;
    ITEMS[ITEM_LENS].reachBonus = 28;

    ITEMS[ITEM_RELAY].name       = "Field Relay";
    ITEMS[ITEM_RELAY].kind       = ITEMK_ACCESSORY;
    ITEMS[ITEM_RELAY].equipSlot  = EQ_TRINKET_A;
    ITEMS[ITEM_RELAY].maxStack   = 1;
    ITEMS[ITEM_RELAY].colour     = 0xB088E0;
    ITEMS[ITEM_RELAY].sprite     = SPR_RELAY;
    ITEMS[ITEM_RELAY].reachBonus = 56;

    /* --- flight ------------------------------------------------------------
       The ladder, and the numbers are chosen against the jump they extend
       rather than against each other. A standing jump is JUMP_VEL 2.6 against
       GRAVITY 0.18, which is about 18 cells of clearance and a quarter-second
       apex (see player.cpp).

         Rocket Boots  0.55 cells/frame for 26 frames is roughly 14 cells --
                       so it about doubles a jump and no more. "A slight boost"
                       has to stay a boost: at riseCap 1.1 it stopped reading as
                       better boots and started reading as a bad jetpack, which
                       is the wrong shape for the first rung.

         Jetpack Mk I  1.1 for 90 frames is 99 cells, which is the rung where
                       the verb changes from jumping to flying: it is more than
                       the 18-cell jump by enough that you start routing around
                       terrain instead of over it.

         Mk II, Mk III faster and longer. Mk III's 320 frames is five seconds of
                       climb, well over a screen height, which is where flight
                       stops being a traversal tool and becomes the way you get
                       anywhere.

       Refuel is on the ground only and is deliberately NOT proportional to
       capacity: boots recharge in 7 frames and a Mk III takes 80, so the boots
       are something you use every jump and the big packs are something you
       spend and then have to land. Making the big tiers refill as fast would
       delete the only cost flight has. */
    ITEMS[ITEM_ROCKET_BOOTS].name       = "Rocket Boots";
    ITEMS[ITEM_ROCKET_BOOTS].kind       = ITEMK_WORN;
    ITEMS[ITEM_ROCKET_BOOTS].equipSlot  = EQ_FEET;
    ITEMS[ITEM_ROCKET_BOOTS].maxStack   = 1;
    ITEMS[ITEM_ROCKET_BOOTS].colour     = 0xE0A050;
    ITEMS[ITEM_ROCKET_BOOTS].sprite     = SPR_BOOTS;
    ITEMS[ITEM_ROCKET_BOOTS].fly.thrust  = 0.30f;
    ITEMS[ITEM_ROCKET_BOOTS].fly.riseCap = 0.55f;
    ITEMS[ITEM_ROCKET_BOOTS].fly.fuel    = 26;
    ITEMS[ITEM_ROCKET_BOOTS].fly.refuel  = 4.0f;

    /* Hermes boots. +60%, which is a big number on purpose: the base walk is
       1.2 cells a frame and anything under about half again is not something
       you notice while you are busy digging, so a small speed bonus is a stat
       you read rather than a thing you feel. 60% takes the character from 72
       cells a second to 115, which is the difference between crossing your
       base and getting somewhere.

       No flight at all, and that is the point -- see ITEM_HERMES. Wearing
       these means not wearing rocket boots, so the choice is "get out of holes"
       against "cover ground", and both are real answers depending on whether
       you are exploring or building. */
    ITEMS[ITEM_HERMES].name      = "Hermes Boots";
    ITEMS[ITEM_HERMES].kind      = ITEMK_WORN;
    ITEMS[ITEM_HERMES].equipSlot = EQ_FEET;
    ITEMS[ITEM_HERMES].maxStack  = 1;
    ITEMS[ITEM_HERMES].colour    = 0xE8D45A;
    ITEMS[ITEM_HERMES].sprite    = SPR_HERMES;
    ITEMS[ITEM_HERMES].speedPct  = 60;

    struct PackTier { ItemId id; const char* name; u32 colour; u8 sprite;
                      float thrust, riseCap; int fuel; float refuel; };
    static const PackTier PACKS[] = {
        { ITEM_JETPACK1, "Jetpack Mk I",   0x9AA6B4, SPR_PACK1, 0.40f, 1.10f,  90, 3.0f },
        { ITEM_JETPACK2, "Jetpack Mk II",  0xC8D0DC, SPR_PACK2, 0.50f, 1.50f, 180, 3.0f },
        { ITEM_JETPACK3, "Jetpack Mk III", 0xE0B048, SPR_PACK3, 0.62f, 2.00f, 320, 4.0f },
    };
    for (unsigned i = 0; i < sizeof(PACKS) / sizeof(PACKS[0]); ++i) {
        const PackTier& t = PACKS[i];
        ITEMS[t.id].name       = t.name;
        ITEMS[t.id].kind       = ITEMK_WORN;
        ITEMS[t.id].equipSlot  = EQ_BACK;
        ITEMS[t.id].maxStack   = 1;
        ITEMS[t.id].colour     = t.colour;
        ITEMS[t.id].sprite     = t.sprite;
        ITEMS[t.id].fly.thrust  = t.thrust;
        ITEMS[t.id].fly.riseCap = t.riseCap;
        ITEMS[t.id].fly.fuel    = t.fuel;
        ITEMS[t.id].fly.refuel  = t.refuel;
        ITEMS[t.id].description = "Hold jump in the air to fly. Fuel recharges while grounded.";
    }

    /* --- machines -----------------------------------------------------------
       maxStack is a real stack rather than 1: these are parts, and a contraption
       wants a dozen of them, so carrying them one to a slot would make building
       anything an inventory-management exercise. Modest rather than the 100000 a
       material gets, because a machine is a machine and not a grain of sand.

       The sprite is shared with the object in the world, which is the point of
       sizing devices to the sprite canvas -- see DEV_W in device.h. */
    ITEMS[ITEM_THERMOCOUPLE].name       = "Thermocouple";
    ITEMS[ITEM_THERMOCOUPLE].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_THERMOCOUPLE].deviceType = DEV_THERMOCOUPLE;
    ITEMS[ITEM_THERMOCOUPLE].maxStack   = 64;
    ITEMS[ITEM_THERMOCOUPLE].colour     = 0x6E7888;
    ITEMS[ITEM_THERMOCOUPLE].sprite     = SPR_THERMO;

    ITEMS[ITEM_CLOCK].name       = "Clock";
    ITEMS[ITEM_CLOCK].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_CLOCK].deviceType = DEV_CLOCK;
    ITEMS[ITEM_CLOCK].maxStack   = 64;
    ITEMS[ITEM_CLOCK].colour     = 0x8A93A6;
    ITEMS[ITEM_CLOCK].sprite     = SPR_CLOCK;

    ITEMS[ITEM_PLACER].name       = "Placer";
    ITEMS[ITEM_PLACER].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_PLACER].deviceType = DEV_PLACER;
    ITEMS[ITEM_PLACER].maxStack   = 64;
    ITEMS[ITEM_PLACER].colour     = 0xE8503A;
    ITEMS[ITEM_PLACER].sprite     = SPR_PLACER;

    ITEMS[ITEM_MINER].name       = "Miner";
    ITEMS[ITEM_MINER].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_MINER].deviceType = DEV_MINER;
    ITEMS[ITEM_MINER].maxStack   = 64;
    ITEMS[ITEM_MINER].colour     = 0x9AA6B4;
    ITEMS[ITEM_MINER].sprite     = SPR_MINER;

    /* The torch is a DEVICE now, not a material you paint. Its item id is separate
       from MAT_TORCH, which still exists as the material its cells are made of --
       see DeviceInfo::cellMat. */
    ITEMS[ITEM_TORCH_DEV].name       = "Torch";
    ITEMS[ITEM_TORCH_DEV].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_TORCH_DEV].deviceType = DEV_TORCH;
    ITEMS[ITEM_TORCH_DEV].maxStack   = 64;
    ITEMS[ITEM_TORCH_DEV].colour     = 0xFFC46A;
    ITEMS[ITEM_TORCH_DEV].sprite     = SPR_TORCH;

    ITEMS[ITEM_ITEM_PIPE].name       = "Item Pipe";
    ITEMS[ITEM_ITEM_PIPE].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_ITEM_PIPE].deviceType = DEV_PIPE;
    ITEMS[ITEM_ITEM_PIPE].maxStack   = 64;
    ITEMS[ITEM_ITEM_PIPE].colour     = 0x7AA7B8;
    ITEMS[ITEM_ITEM_PIPE].sprite     = SPR_PIPE;

    ITEMS[ITEM_PIPE_CROSSOVER].name       = "Pipe Crossover";
    ITEMS[ITEM_PIPE_CROSSOVER].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_PIPE_CROSSOVER].deviceType = DEV_CROSSOVER;
    ITEMS[ITEM_PIPE_CROSSOVER].maxStack   = 64;
    ITEMS[ITEM_PIPE_CROSSOVER].colour     = 0xA2BCD0;
    ITEMS[ITEM_PIPE_CROSSOVER].sprite     = SPR_CROSSOVER;

    /* The workbench. Placed like any other device, and the only one that is a
       crafting STATION rather than a machine -- see DEV_WORKBENCH. */
    ITEMS[ITEM_WORKBENCH].name       = "Workbench";
    ITEMS[ITEM_WORKBENCH].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_WORKBENCH].deviceType = DEV_WORKBENCH;
    ITEMS[ITEM_WORKBENCH].maxStack   = 64;
    /* Taken from the material its footprint is made of, so the hotbar swatch
       and the object in the world are the same colour by construction rather
       than by two constants agreeing. */
    ITEMS[ITEM_WORKBENCH].colour     = MATS[MAT_STATION_BENCH].dryA;
    ITEMS[ITEM_WORKBENCH].sprite     = SPR_BENCH;

    ITEMS[ITEM_ANVIL].name       = "Anvil";
    ITEMS[ITEM_ANVIL].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_ANVIL].deviceType = DEV_ANVIL;
    ITEMS[ITEM_ANVIL].maxStack   = 64;
    ITEMS[ITEM_ANVIL].colour     = MATS[MAT_STATION_ANVIL].dryA;
    ITEMS[ITEM_ANVIL].sprite     = SPR_ANVIL;

    ITEMS[ITEM_CHEMSTN].name       = "Chemistry Bench";
    ITEMS[ITEM_CHEMSTN].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_CHEMSTN].deviceType = DEV_CHEM;
    ITEMS[ITEM_CHEMSTN].maxStack   = 64;
    ITEMS[ITEM_CHEMSTN].colour     = MATS[MAT_STATION_CHEM].dryA;
    ITEMS[ITEM_CHEMSTN].sprite     = SPR_CHEMSTN;

    ITEMS[ITEM_ASSEMBLY].name       = "Assembly Table";
    ITEMS[ITEM_ASSEMBLY].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_ASSEMBLY].deviceType = DEV_ASSEMBLY;
    ITEMS[ITEM_ASSEMBLY].maxStack   = 64;
    ITEMS[ITEM_ASSEMBLY].colour     = MATS[MAT_STATION_ASSEMBLY].dryA;
    ITEMS[ITEM_ASSEMBLY].sprite     = SPR_ASSEMBLY;

    ITEMS[ITEM_FORGESTN].name       = "Blast Furnace";
    ITEMS[ITEM_FORGESTN].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_FORGESTN].deviceType = DEV_FORGE;
    ITEMS[ITEM_FORGESTN].maxStack   = 64;
    ITEMS[ITEM_FORGESTN].colour     = MATS[MAT_STATION_FORGE].dryA;
    ITEMS[ITEM_FORGESTN].sprite     = SPR_FORGESTN;

    ITEMS[ITEM_BED].name       = "Bed";
    ITEMS[ITEM_BED].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_BED].deviceType = DEV_BED;
    ITEMS[ITEM_BED].maxStack   = 16;
    ITEMS[ITEM_BED].colour     = 0xC8B070;
    ITEMS[ITEM_BED].sprite     = SPR_BED;

    ITEMS[ITEM_CHEST].name       = "Chest";
    ITEMS[ITEM_CHEST].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_CHEST].deviceType = DEV_CHEST;
    ITEMS[ITEM_CHEST].maxStack   = 64;
    ITEMS[ITEM_CHEST].colour     = 0xB87842;
    ITEMS[ITEM_CHEST].sprite     = SPR_CHEST;

    ITEMS[ITEM_SPOUT].name       = "Spout";
    ITEMS[ITEM_SPOUT].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_SPOUT].deviceType = DEV_SPOUT;
    ITEMS[ITEM_SPOUT].maxStack   = 64;
    ITEMS[ITEM_SPOUT].colour     = 0x62B8E8;
    ITEMS[ITEM_SPOUT].sprite     = SPR_SPOUT;

    ITEMS[ITEM_DRAIN].name       = "Drain";
    ITEMS[ITEM_DRAIN].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_DRAIN].deviceType = DEV_DRAIN;
    ITEMS[ITEM_DRAIN].maxStack   = 64;
    ITEMS[ITEM_DRAIN].colour     = 0x6858A4;
    ITEMS[ITEM_DRAIN].sprite     = SPR_DRAIN;

    ITEMS[ITEM_BLOCK_WATCHER].name       = "Block Watcher";
    ITEMS[ITEM_BLOCK_WATCHER].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_BLOCK_WATCHER].deviceType = DEV_BLOCK_WATCHER;
    ITEMS[ITEM_BLOCK_WATCHER].maxStack   = 64;
    ITEMS[ITEM_BLOCK_WATCHER].colour     = 0xD8A85A;
    ITEMS[ITEM_BLOCK_WATCHER].sprite     = SPR_THERMO;

    ITEMS[ITEM_PULSE_BUTTON].name       = "Pulse Button";
    ITEMS[ITEM_PULSE_BUTTON].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_PULSE_BUTTON].deviceType = DEV_PULSE_BUTTON;
    ITEMS[ITEM_PULSE_BUTTON].maxStack   = 64;
    ITEMS[ITEM_PULSE_BUTTON].colour     = 0xE85C45;
    ITEMS[ITEM_PULSE_BUTTON].sprite     = SPR_BUTTON;

    ITEMS[ITEM_CONSTANT_COMBINATOR].name       = "Constant Combinator";
    ITEMS[ITEM_CONSTANT_COMBINATOR].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_CONSTANT_COMBINATOR].deviceType = DEV_CONSTANT_COMBINATOR;
    ITEMS[ITEM_CONSTANT_COMBINATOR].maxStack   = 64;
    ITEMS[ITEM_CONSTANT_COMBINATOR].colour     = 0xB070E8;
    ITEMS[ITEM_CONSTANT_COMBINATOR].sprite     = SPR_CIRCUIT_CONSTANT;

    ITEMS[ITEM_ARITHMETIC_COMBINATOR].name       = "Arithmetic Combinator";
    ITEMS[ITEM_ARITHMETIC_COMBINATOR].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_ARITHMETIC_COMBINATOR].deviceType = DEV_ARITHMETIC_COMBINATOR;
    ITEMS[ITEM_ARITHMETIC_COMBINATOR].maxStack   = 64;
    ITEMS[ITEM_ARITHMETIC_COMBINATOR].colour     = 0xB070E8;
    ITEMS[ITEM_ARITHMETIC_COMBINATOR].sprite     = SPR_CIRCUIT_ARITH;

    ITEMS[ITEM_DECIDER_COMBINATOR].name       = "Decider Combinator";
    ITEMS[ITEM_DECIDER_COMBINATOR].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_DECIDER_COMBINATOR].deviceType = DEV_DECIDER_COMBINATOR;
    ITEMS[ITEM_DECIDER_COMBINATOR].maxStack   = 64;
    ITEMS[ITEM_DECIDER_COMBINATOR].colour     = 0xB070E8;
    ITEMS[ITEM_DECIDER_COMBINATOR].sprite     = SPR_CIRCUIT_DECIDER;

    /* --- armour --------------------------------------------------------
       The equipment category heatResist/coldResist existed for and had
       nothing on the other end of, until now -- see the field comments in
       item.h. Two tiers, a helmet and a suit each, EQ_HEAD and EQ_BODY so a
       full set is two separate choices rather than one slot fighting
       itself. Resolved through the same "largest, never summed" rule as
       reachBonus and speedPct -- see Inventory::tempResist() -- so a suit
       makes its matching helmet redundant rather than stacking with it,
       consistent with every other equipment ladder here. The suit is
       always the bigger number for exactly that reason: it is the piece
       actually worth building the tier around. */
    ITEMS[ITEM_STEEL_HELMET].name       = "Steel Helmet";
    ITEMS[ITEM_STEEL_HELMET].kind       = ITEMK_WORN;
    ITEMS[ITEM_STEEL_HELMET].equipSlot  = EQ_HEAD;
    ITEMS[ITEM_STEEL_HELMET].maxStack   = 1;
    ITEMS[ITEM_STEEL_HELMET].colour     = 0x9CA0A6;
    ITEMS[ITEM_STEEL_HELMET].heatResist = 15;
    ITEMS[ITEM_STEEL_HELMET].coldResist = 15;
    ITEMS[ITEM_STEEL_HELMET].armour     = 2;
    ITEMS[ITEM_STEEL_HELMET].sprite     = SPR_ARMOUR_HELM_STEEL;

    ITEMS[ITEM_STEEL_SUIT].name       = "Steel Suit";
    ITEMS[ITEM_STEEL_SUIT].kind       = ITEMK_WORN;
    ITEMS[ITEM_STEEL_SUIT].equipSlot  = EQ_BODY;
    ITEMS[ITEM_STEEL_SUIT].maxStack   = 1;
    ITEMS[ITEM_STEEL_SUIT].colour     = 0x9CA0A6;
    ITEMS[ITEM_STEEL_SUIT].heatResist = 30;
    ITEMS[ITEM_STEEL_SUIT].coldResist = 30;
    ITEMS[ITEM_STEEL_SUIT].armour     = 4;
    ITEMS[ITEM_STEEL_SUIT].sprite     = SPR_ARMOUR_SUIT_STEEL;

    /* Titanium: corrosion-proof and the metal DESIGN.md calls "the hull of
       the thing you leave on" -- a real jump over steel, not an increment. */
    ITEMS[ITEM_TITANIUM_HELMET].name       = "Titanium Helmet";
    ITEMS[ITEM_TITANIUM_HELMET].kind       = ITEMK_WORN;
    ITEMS[ITEM_TITANIUM_HELMET].equipSlot  = EQ_HEAD;
    ITEMS[ITEM_TITANIUM_HELMET].maxStack   = 1;
    ITEMS[ITEM_TITANIUM_HELMET].colour     = 0xC8CCD2;
    ITEMS[ITEM_TITANIUM_HELMET].heatResist = 45;
    ITEMS[ITEM_TITANIUM_HELMET].coldResist = 45;
    ITEMS[ITEM_TITANIUM_HELMET].armour     = 5;

    ITEMS[ITEM_TITANIUM_SUIT].name       = "Titanium Suit";
    ITEMS[ITEM_TITANIUM_SUIT].kind       = ITEMK_WORN;
    ITEMS[ITEM_TITANIUM_SUIT].equipSlot  = EQ_BODY;
    ITEMS[ITEM_TITANIUM_SUIT].maxStack   = 1;
    ITEMS[ITEM_TITANIUM_SUIT].colour     = 0xC8CCD2;
    ITEMS[ITEM_TITANIUM_SUIT].heatResist = 70;
    ITEMS[ITEM_TITANIUM_SUIT].coldResist = 70;
    ITEMS[ITEM_TITANIUM_SUIT].armour     = 9;

    ITEMS[ITEM_FORGE_CORE].name     = "Forge Core";
    ITEMS[ITEM_FORGE_CORE].kind     = ITEMK_COMPONENT;  /* carried, never placed */
    ITEMS[ITEM_FORGE_CORE].maxStack = 16;
    ITEMS[ITEM_FORGE_CORE].colour   = 0xE07A32;
    ITEMS[ITEM_FORGE_CORE].sprite   = SPR_FORGE_CORE;

    /* Drones are worn companions. The light occupies its own utility bay so
       illumination never competes with combat. General chassis fit all three
       combat bays, while Inventory decides which earned bays are active. */
    ITEMS[ITEM_LIGHT_DRONE].name      = "Light Drone";
    ITEMS[ITEM_LIGHT_DRONE].kind      = ITEMK_WORN;
    ITEMS[ITEM_LIGHT_DRONE].equipSlot = EQ_LIGHT_DRONE;
    ITEMS[ITEM_LIGHT_DRONE].maxStack  = 1;
    ITEMS[ITEM_LIGHT_DRONE].colour    = 0x9DEBFF;
    ITEMS[ITEM_LIGHT_DRONE].sprite    = SPR_DRONE_LIGHT;

    ITEMS[ITEM_ATTACK_DRONE].name      = "Attack Drone";
    ITEMS[ITEM_ATTACK_DRONE].kind      = ITEMK_WORN;
    ITEMS[ITEM_ATTACK_DRONE].equipSlot = EQ_DRONE_A;
    ITEMS[ITEM_ATTACK_DRONE].maxStack  = 1;
    ITEMS[ITEM_ATTACK_DRONE].colour    = 0xE8A76C;
    ITEMS[ITEM_ATTACK_DRONE].sprite    = SPR_DRONE_ATTACK;

    ITEMS[ITEM_PICKUP_DRONE].name      = "Pickup Drone";
    ITEMS[ITEM_PICKUP_DRONE].kind      = ITEMK_WORN;
    ITEMS[ITEM_PICKUP_DRONE].equipSlot = EQ_DRONE_A;
    ITEMS[ITEM_PICKUP_DRONE].maxStack  = 1;
    ITEMS[ITEM_PICKUP_DRONE].colour    = 0x8CE8B0;
    ITEMS[ITEM_PICKUP_DRONE].sprite    = SPR_DRONE_PICKUP;

    ITEMS[ITEM_SHIELD_DRONE].name      = "Shield Drone";
    ITEMS[ITEM_SHIELD_DRONE].kind      = ITEMK_WORN;
    ITEMS[ITEM_SHIELD_DRONE].equipSlot = EQ_DRONE_A;
    ITEMS[ITEM_SHIELD_DRONE].maxStack  = 1;
    ITEMS[ITEM_SHIELD_DRONE].colour    = 0x86B8FF;
    ITEMS[ITEM_SHIELD_DRONE].sprite    = SPR_DRONE_SHIELD;

    /* The weapon chassis. All three go in a GENERAL drone bay -- equipSlot
       names EQ_DRONE_A and equipFits reads that as "any" -- so earned bays
       are a weapon loadout rather than one weapon and one utility. Two orbit
       drones is a legal and quite reasonable answer; so is a mortar and a
       pickup drone, and giving that up is what carrying two weapons costs. */
    ITEMS[ITEM_LANCE_DRONE].name      = "Lance Drone";
    ITEMS[ITEM_LANCE_DRONE].kind      = ITEMK_WORN;
    ITEMS[ITEM_LANCE_DRONE].equipSlot = EQ_DRONE_A;
    ITEMS[ITEM_LANCE_DRONE].maxStack  = 1;
    ITEMS[ITEM_LANCE_DRONE].colour    = 0xBFE9FF;
    ITEMS[ITEM_LANCE_DRONE].sprite    = SPR_ACC_BRACER;

    ITEMS[ITEM_MORTAR_DRONE].name      = "Mortar Drone";
    ITEMS[ITEM_MORTAR_DRONE].kind      = ITEMK_WORN;
    ITEMS[ITEM_MORTAR_DRONE].equipSlot = EQ_DRONE_A;
    ITEMS[ITEM_MORTAR_DRONE].maxStack  = 1;
    ITEMS[ITEM_MORTAR_DRONE].colour    = 0xFFC24A;
    ITEMS[ITEM_MORTAR_DRONE].sprite    = SPR_ACC_LANTERN;

    ITEMS[ITEM_ORBIT_DRONE].name      = "Orbit Drone";
    ITEMS[ITEM_ORBIT_DRONE].kind      = ITEMK_WORN;
    ITEMS[ITEM_ORBIT_DRONE].equipSlot = EQ_DRONE_A;
    ITEMS[ITEM_ORBIT_DRONE].maxStack  = 1;
    ITEMS[ITEM_ORBIT_DRONE].colour    = 0xE4E9F2;
    ITEMS[ITEM_ORBIT_DRONE].sprite    = SPR_ACC_MAGNET;

    /* Drone Armour is deliberately a modest iron-sidegrade for now: its power
       is the loadout it enables, not raw protection. The ids are appended for
       save compatibility even though the definitions live beside the drones. */
    ITEMS[ITEM_DRONE_VISOR].name       = "Drone Visor";
    ITEMS[ITEM_DRONE_VISOR].kind       = ITEMK_WORN;
    ITEMS[ITEM_DRONE_VISOR].equipSlot  = EQ_HEAD;
    ITEMS[ITEM_DRONE_VISOR].maxStack   = 1;
    ITEMS[ITEM_DRONE_VISOR].colour     = 0x6FAFBE;
    ITEMS[ITEM_DRONE_VISOR].armour     = 1;
    ITEMS[ITEM_DRONE_VISOR].heatResist = 10;
    ITEMS[ITEM_DRONE_VISOR].coldResist = 10;
    ITEMS[ITEM_DRONE_VISOR].armourSet  = ARMOUR_SET_DRONE;
    ITEMS[ITEM_DRONE_VISOR].sprite     = SPR_ARMOUR_DRONE_VISOR;

    ITEMS[ITEM_DRONE_HARNESS].name       = "Drone Harness";
    ITEMS[ITEM_DRONE_HARNESS].kind       = ITEMK_WORN;
    ITEMS[ITEM_DRONE_HARNESS].equipSlot  = EQ_BODY;
    ITEMS[ITEM_DRONE_HARNESS].maxStack   = 1;
    ITEMS[ITEM_DRONE_HARNESS].colour     = 0x6FAFBE;
    ITEMS[ITEM_DRONE_HARNESS].armour     = 2;
    ITEMS[ITEM_DRONE_HARNESS].heatResist = 15;
    ITEMS[ITEM_DRONE_HARNESS].coldResist = 15;
    ITEMS[ITEM_DRONE_HARNESS].armourSet  = ARMOUR_SET_DRONE;
    ITEMS[ITEM_DRONE_HARNESS].sprite     = SPR_ARMOUR_DRONE_HARNESS;

    ITEMS[ITEM_DRONE_GREAVES].name      = "Drone Greaves";
    ITEMS[ITEM_DRONE_GREAVES].kind      = ITEMK_WORN;
    ITEMS[ITEM_DRONE_GREAVES].equipSlot = EQ_FEET;
    ITEMS[ITEM_DRONE_GREAVES].maxStack  = 1;
    ITEMS[ITEM_DRONE_GREAVES].colour    = 0x6FAFBE;
    ITEMS[ITEM_DRONE_GREAVES].armour    = 1;
    ITEMS[ITEM_DRONE_GREAVES].armourSet = ARMOUR_SET_DRONE;
    ITEMS[ITEM_DRONE_GREAVES].sprite    = SPR_ARMOUR_DRONE_GREAVES;

    /* Iron is the first honest armour: cheap, modest and free of a set bonus.
       It exists so the class sets are a specialization after Ichor rather than
       the first moment the equipment slots do anything. */
    ITEMS[ITEM_IRON_HELMET].name       = "Iron Helmet";
    ITEMS[ITEM_IRON_HELMET].kind       = ITEMK_WORN;
    ITEMS[ITEM_IRON_HELMET].equipSlot  = EQ_HEAD;
    ITEMS[ITEM_IRON_HELMET].maxStack   = 1;
    ITEMS[ITEM_IRON_HELMET].colour     = 0xA8ADB6;
    ITEMS[ITEM_IRON_HELMET].armour     = 1;
    ITEMS[ITEM_IRON_HELMET].heatResist = 5;
    ITEMS[ITEM_IRON_HELMET].coldResist = 5;
    ITEMS[ITEM_IRON_HELMET].sprite     = SPR_ARMOUR_IRON_HELM;

    ITEMS[ITEM_IRON_CUIRASS].name       = "Iron Cuirass";
    ITEMS[ITEM_IRON_CUIRASS].kind       = ITEMK_WORN;
    ITEMS[ITEM_IRON_CUIRASS].equipSlot  = EQ_BODY;
    ITEMS[ITEM_IRON_CUIRASS].maxStack   = 1;
    ITEMS[ITEM_IRON_CUIRASS].colour     = 0xA8ADB6;
    ITEMS[ITEM_IRON_CUIRASS].armour     = 2;
    ITEMS[ITEM_IRON_CUIRASS].heatResist = 10;
    ITEMS[ITEM_IRON_CUIRASS].coldResist = 10;
    ITEMS[ITEM_IRON_CUIRASS].sprite     = SPR_ARMOUR_IRON_CUIRASS;

    ITEMS[ITEM_IRON_GREAVES].name       = "Iron Greaves";
    ITEMS[ITEM_IRON_GREAVES].kind       = ITEMK_WORN;
    ITEMS[ITEM_IRON_GREAVES].equipSlot  = EQ_FEET;
    ITEMS[ITEM_IRON_GREAVES].maxStack   = 1;
    ITEMS[ITEM_IRON_GREAVES].colour     = 0xA8ADB6;
    ITEMS[ITEM_IRON_GREAVES].armour     = 1;
    ITEMS[ITEM_IRON_GREAVES].heatResist = 5;
    ITEMS[ITEM_IRON_GREAVES].coldResist = 5;
    ITEMS[ITEM_IRON_GREAVES].sprite     = SPR_ARMOUR_IRON_GREAVES;

    /* Ranger Armour trades plate for weapon handling. Its bonuses are applied
       only to player-fired projectiles; drones remain the Drone set's lane and
       blades remain the Vanguard's. */
    ITEMS[ITEM_RANGER_VISOR].name       = "Ranger Visor";
    ITEMS[ITEM_RANGER_VISOR].kind       = ITEMK_WORN;
    ITEMS[ITEM_RANGER_VISOR].equipSlot  = EQ_HEAD;
    ITEMS[ITEM_RANGER_VISOR].maxStack   = 1;
    ITEMS[ITEM_RANGER_VISOR].colour     = 0x9DA76A;
    ITEMS[ITEM_RANGER_VISOR].armour     = 2;
    ITEMS[ITEM_RANGER_VISOR].heatResist = 15;
    ITEMS[ITEM_RANGER_VISOR].coldResist = 15;
    ITEMS[ITEM_RANGER_VISOR].armourSet  = ARMOUR_SET_RANGED;
    ITEMS[ITEM_RANGER_VISOR].sprite     = SPR_ARMOUR_RANGER_VISOR;

    ITEMS[ITEM_RANGER_COAT].name       = "Ranger Coat";
    ITEMS[ITEM_RANGER_COAT].kind       = ITEMK_WORN;
    ITEMS[ITEM_RANGER_COAT].equipSlot  = EQ_BODY;
    ITEMS[ITEM_RANGER_COAT].maxStack   = 1;
    ITEMS[ITEM_RANGER_COAT].colour     = 0x9DA76A;
    ITEMS[ITEM_RANGER_COAT].armour     = 3;
    ITEMS[ITEM_RANGER_COAT].heatResist = 25;
    ITEMS[ITEM_RANGER_COAT].coldResist = 25;
    ITEMS[ITEM_RANGER_COAT].armourSet  = ARMOUR_SET_RANGED;
    ITEMS[ITEM_RANGER_COAT].sprite     = SPR_ARMOUR_RANGER_COAT;

    ITEMS[ITEM_RANGER_GREAVES].name       = "Ranger Greaves";
    ITEMS[ITEM_RANGER_GREAVES].kind       = ITEMK_WORN;
    ITEMS[ITEM_RANGER_GREAVES].equipSlot  = EQ_FEET;
    ITEMS[ITEM_RANGER_GREAVES].maxStack   = 1;
    ITEMS[ITEM_RANGER_GREAVES].colour     = 0x9DA76A;
    ITEMS[ITEM_RANGER_GREAVES].armour     = 1;
    ITEMS[ITEM_RANGER_GREAVES].heatResist = 10;
    ITEMS[ITEM_RANGER_GREAVES].coldResist = 10;
    ITEMS[ITEM_RANGER_GREAVES].armourSet  = ARMOUR_SET_RANGED;
    ITEMS[ITEM_RANGER_GREAVES].sprite     = SPR_ARMOUR_RANGER_GREAVES;

    /* Vanguard Armour is the close-range answer: enough plate and insulation
       to survive contact, then set bonuses that reward staying in blade range. */
    ITEMS[ITEM_VANGUARD_HELM].name       = "Vanguard Helm";
    ITEMS[ITEM_VANGUARD_HELM].kind       = ITEMK_WORN;
    ITEMS[ITEM_VANGUARD_HELM].equipSlot  = EQ_HEAD;
    ITEMS[ITEM_VANGUARD_HELM].maxStack   = 1;
    ITEMS[ITEM_VANGUARD_HELM].colour     = 0xA85A65;
    ITEMS[ITEM_VANGUARD_HELM].armour     = 4;
    ITEMS[ITEM_VANGUARD_HELM].heatResist = 40;
    ITEMS[ITEM_VANGUARD_HELM].coldResist = 40;
    ITEMS[ITEM_VANGUARD_HELM].armourSet  = ARMOUR_SET_MELEE;
    ITEMS[ITEM_VANGUARD_HELM].sprite     = SPR_ARMOUR_VANGUARD_HELM;

    ITEMS[ITEM_VANGUARD_PLATE].name       = "Vanguard Plate";
    ITEMS[ITEM_VANGUARD_PLATE].kind       = ITEMK_WORN;
    ITEMS[ITEM_VANGUARD_PLATE].equipSlot  = EQ_BODY;
    ITEMS[ITEM_VANGUARD_PLATE].maxStack   = 1;
    ITEMS[ITEM_VANGUARD_PLATE].colour     = 0xA85A65;
    ITEMS[ITEM_VANGUARD_PLATE].armour     = 7;
    ITEMS[ITEM_VANGUARD_PLATE].heatResist = 60;
    ITEMS[ITEM_VANGUARD_PLATE].coldResist = 60;
    ITEMS[ITEM_VANGUARD_PLATE].armourSet  = ARMOUR_SET_MELEE;
    ITEMS[ITEM_VANGUARD_PLATE].sprite     = SPR_ARMOUR_VANGUARD_PLATE;

    ITEMS[ITEM_VANGUARD_GREAVES].name       = "Vanguard Greaves";
    ITEMS[ITEM_VANGUARD_GREAVES].kind       = ITEMK_WORN;
    ITEMS[ITEM_VANGUARD_GREAVES].equipSlot  = EQ_FEET;
    ITEMS[ITEM_VANGUARD_GREAVES].maxStack   = 1;
    ITEMS[ITEM_VANGUARD_GREAVES].colour     = 0xA85A65;
    ITEMS[ITEM_VANGUARD_GREAVES].armour     = 3;
    ITEMS[ITEM_VANGUARD_GREAVES].heatResist = 30;
    ITEMS[ITEM_VANGUARD_GREAVES].coldResist = 30;
    ITEMS[ITEM_VANGUARD_GREAVES].armourSet  = ARMOUR_SET_MELEE;
    ITEMS[ITEM_VANGUARD_GREAVES].sprite     = SPR_ARMOUR_VANGUARD_GREAVES;

    ITEMS[ITEM_DRONE_BEACON].name      = "Drone Beacon";
    ITEMS[ITEM_DRONE_BEACON].kind      = ITEMK_ACCESSORY;
    ITEMS[ITEM_DRONE_BEACON].equipSlot = EQ_TRINKET_A;
    ITEMS[ITEM_DRONE_BEACON].maxStack  = 1;
    ITEMS[ITEM_DRONE_BEACON].colour    = 0x7ED8E8;
    ITEMS[ITEM_DRONE_BEACON].sprite    = SPR_ACC_DRONE_BEACON;

    /* The pedestal. A DEVICE rather than a material, for the reason the
       workbench is one: it covers a rectangle, it snaps to the device lattice,
       and it creates something with state that outlives the click -- namely the
       item standing on it. See DEV_PEDESTAL. */
    ITEMS[ITEM_PEDESTAL].name       = "Pedestal";
    ITEMS[ITEM_PEDESTAL].kind       = ITEMK_DEVICE;
    ITEMS[ITEM_PEDESTAL].deviceType = DEV_PEDESTAL;
    ITEMS[ITEM_PEDESTAL].maxStack   = 32;
    ITEMS[ITEM_PEDESTAL].colour     = 0x9AA6B4;
    ITEMS[ITEM_PEDESTAL].sprite     = SPR_PEDESTAL;

    /* ======================================================================
       The melee ladder
       ======================================================================

       Seven metals, a sword and a spear each. Terraria's shape -- one weapon
       family per ore, each tier a straight upgrade of the last -- with two
       departures that this game's own material table forced, and both are worth
       stating because they will look like mistakes otherwise.

       --- no tin ---
       Tin exists to be alloyed into bronze. In Terraria copper and tin are
       alternate worlds' versions of the same rung; here bronze is what tin is
       FOR, so a tin sword would be a rung whose only purpose is to be skipped.

       --- gold is the FAST tier, not a strong one ---
       MAT_GOLD is deliberately STR_SOFT and always has been (see its note in
       materials.h): it is the corrosion-proof conductor, explicitly "not meant
       to compete with steel". A gold sword that outhit steel would quietly
       overturn that, and a game whose material table says one thing while its
       weapon table says another is a game where neither can be trusted.

       So gold buys SPEED. Its sword lands about as often as anything two tiers
       above it and hits for less each time, which comes out near iron on paper
       and feels completely different in the hand -- more strokes means more
       knockback events, which is the thing that keeps you alive in contact
       range. It is a sidegrade you find rather than a rung you climb.

       --- the numbers ---
       Damage per second is damage / cooldown, and cooldown is measured from the
       START of a stroke, so the two numbers below are the whole story:

                    sword           spear
         Copper      7/33 = 13/s     6/22 = 16/s
         Bronze     10/33 = 18/s     8/22 = 22/s
         Iron       14/33 = 25/s    11/22 = 30/s
         Gold       15/24 = 38/s    12/16 = 45/s
         Steel      20/33 = 36/s    16/22 = 44/s
         Titanium   26/33 = 47/s    21/22 = 57/s
         Tungsten   34/35 = 58/s    27/24 = 68/s

       Against the Bolt Caster's 11/s and the Attack Drone's 2.1/s that looks
       enormous, and it should: those numbers are delivered from across a room
       at something that cannot reach you, and these are delivered from inside
       contact range of a creature that is hitting you back the whole time. A
       husk does 11 a touch every 34 frames; standing in that to swing a copper
       sword is very nearly a losing trade, which is exactly what makes the
       first tier feel like the first tier.

       The SPEAR always reads slightly higher per second and always hits one
       thing. The sword sweeps an arc, so its real output against three mites in
       a corridor is triple what the table says -- that is the trade, and it is
       deliberately not visible in a single number.

       --- reach ---
       From the previous pass, swords are about 15% longer and spears about 70%
       longer, rounded to whole simulation cells. Swords now run 32--44 cells
       and keep their broad sweep; spears run 39--56 and keep their narrow
       single-target thrust. The player's build reach is 56 for
       comparison, and is a completely separate number -- see ItemDef::meleeReach. */
    struct MeleeTier {
        ItemId sword, spear;
        const char* metal;          /* the name the two weapons are prefixed with */
        u8 swordDmg, swordCool, swordReach;
        u8 spearDmg, spearCool, spearReach;
        u32 colour;
        u8 swordSpr, spearSpr;
        float knock;
    };
    /* --- swords came down about 15% -------------------------------------
       Reported from play as hitting too hard, and the table already said why.
       A sword sweeps an arc, so the note above is right that "its real output
       against three mites in a corridor is three times the number in that
       column" -- and on top of that multiplier it also carried roughly a 25%
       per-hit premium over the spear of the same metal. Two advantages stacked
       on the weapon that was already the default pick.

       So the premium came off, not the arc: 7/10/14/15/20/26/34 became
       6/9/12/13/17/22/29. Swords still lead every tier except copper, where
       the two now tie at 6 and are separated by what they ARE -- the sword's
       sweep against the spear's extra reach -- rather than by a number.
       Cooldowns are untouched, so the gold-is-fast, tungsten-is-heavy
       character of the ladder is exactly as it was.

       Every spear then gained six cells, moving the gap over a sword from
       seven cells to thirteen. Reach is the entire argument for carrying
       one -- it hits less hard, shoves less, and its only answer is that it
       lands first -- and seven cells was not enough of an answer to feel
       like one. The ORDER is unchanged: gold is still the shortest past
       copper, which is the price of it being the quickest. */
    static const MeleeTier MELEE[] = {
        /*                                     ---- sword ----   ---- spear ---- */
        { ITEM_SWORD_COPPER,   ITEM_SPEAR_COPPER,   "Copper",     6, 33, 32,   6, 22, 45, 0xC87A32, SPR_SWORD_COPPER,   SPR_SPEAR_COPPER,   0.9f },
        { ITEM_SWORD_BRONZE,   ITEM_SPEAR_BRONZE,   "Bronze",     9, 33, 35,   8, 22, 47, 0xCE9B4E, SPR_SWORD_BRONZE,   SPR_SPEAR_BRONZE,   1.0f },
        { ITEM_SWORD_IRON,     ITEM_SPEAR_IRON,     "Iron",      12, 33, 35,  11, 22, 50, 0xA8ADB6, SPR_SWORD_IRON,     SPR_SPEAR_IRON,     1.1f },
        /* Gold: the fast tier. Its reach is the SHORTEST of anything past
           copper, which is the other half of paying for the speed -- a quick
           weapon that also kept you at range would have no downside at all. */
        { ITEM_SWORD_GOLD,     ITEM_SPEAR_GOLD,     "Gold",      13, 24, 32,  12, 16, 45, 0xE8C233, SPR_SWORD_GOLD,     SPR_SPEAR_GOLD,     0.8f },
        { ITEM_SWORD_STEEL,    ITEM_SPEAR_STEEL,    "Steel",     17, 33, 38,  16, 22, 54, 0x8E97A6, SPR_SWORD_STEEL,    SPR_SPEAR_STEEL,    1.3f },
        { ITEM_SWORD_TITANIUM, ITEM_SPEAR_TITANIUM, "Titanium",  22, 33, 40,  21, 22, 57, 0xD2DAE4, SPR_SWORD_TITANIUM, SPR_SPEAR_TITANIUM, 1.4f },
        /* Tungsten is the heaviest thing on the ladder and swings slowest of
           the top three, which is what stops the last tier being strictly
           better than everything at everything. It hits hardest and shoves
           furthest; titanium remains the one you pick if you want to keep
           moving. */
        { ITEM_SWORD_TUNGSTEN, ITEM_SPEAR_TUNGSTEN, "Tungsten",  29, 35, 44,  27, 24, 62, 0x6F7A86, SPR_SWORD_TUNGSTEN, SPR_SPEAR_TUNGSTEN, 1.8f },
    };

    /* Shared across the whole ladder rather than being per-tier columns,
       because they are what makes a sword a sword rather than what makes an
       iron one better than a copper one. A tier that also changed the arc would
       be changing what the weapon IS, and then the ladder stops being a ladder.

       130 degrees is a little over a right angle either side of where you
       pointed: wide enough to catch the second creature standing beside the
       first, narrow enough that "behind me" is genuinely a place you are not
       covering. The 16-frame stroke is a little over a quarter second: enough
       readable weight to slow the blade without turning the initial motion
       into input lag. Tier cooldowns rise alongside it so repeat cadence slows
       as well. */
    static const int MELEE_SWORD_ARC    = 130;
    static const int MELEE_SWORD_FRAMES = 16;
    /* A stab is quicker than a swing at every tier and covers no width at all.
       10 frames out and back is a jab rather than a lunge. */
    static const int MELEE_SPEAR_FRAMES = 10;

    for (int i = 0; i < (int)(sizeof(MELEE) / sizeof(MELEE[0])); ++i) {
        const MeleeTier& t = MELEE[i];
        static char swordName[7][32], spearName[7][32];
        sprintf(swordName[i], "%s Sword", t.metal);
        sprintf(spearName[i], "%s Spear", t.metal);

        ItemDef& sw = ITEMS[t.sword];
        sw.name          = swordName[i];
        sw.kind          = ITEMK_MELEE;
        sw.maxStack      = 1;
        sw.colour        = t.colour;
        sw.sprite        = t.swordSpr;
        sw.damage        = t.swordDmg;
        sw.meleeStyle    = MELEE_SWING;
        sw.meleeReach    = t.swordReach;
        sw.meleeArc      = MELEE_SWORD_ARC;
        sw.meleeFrames   = MELEE_SWORD_FRAMES;
        sw.meleeCooldown = t.swordCool;
        /* The old 0.8--1.8 shove technically changed velocity but disappeared
           into the creature's next movement frame. Three times that impulse
           produces a visible separation and gives the long sweep a defensive
           payoff without changing damage or attack cadence. */
        sw.meleeKnock    = t.knock * 3.0f;
        sw.description   = "A long sweeping blade that can strike several enemies and knock them back.";

        ItemDef& sp = ITEMS[t.spear];
        sp.name          = spearName[i];
        sp.kind          = ITEMK_MELEE;
        sp.maxStack      = 1;
        sp.colour        = t.colour;
        sp.sprite        = t.spearSpr;
        sp.damage        = t.spearDmg;
        sp.meleeStyle    = MELEE_STAB;
        sp.meleeReach    = t.spearReach;
        sp.meleeArc      = 0;
        sp.meleeFrames   = MELEE_SPEAR_FRAMES;
        sp.meleeCooldown = t.spearCool;
        /* A spear shoves less than a sword of the same metal. It is a point
           rather than an edge, and the whole reason to carry one is that it
           keeps things away by LENGTH instead of by force. */
        sp.meleeKnock    = t.knock * 0.6f;
        sp.description   = "A narrow thrusting weapon with quick, precise attacks.";
    }

    ITEMS[ITEM_OVERCLOCK_CHIP].name      = "Overclock Chip";
    ITEMS[ITEM_OVERCLOCK_CHIP].kind      = ITEMK_DRONE_MODULE;
    ITEMS[ITEM_OVERCLOCK_CHIP].maxStack  = 1;
    ITEMS[ITEM_OVERCLOCK_CHIP].colour    = 0xF0B85C;
    ITEMS[ITEM_OVERCLOCK_CHIP].sprite    = SPR_MOD_SHOT;

    ITEMS[ITEM_TWIN_CONTROLLER].name      = "Twin Controller";
    ITEMS[ITEM_TWIN_CONTROLLER].kind      = ITEMK_DRONE_MODULE;
    ITEMS[ITEM_TWIN_CONTROLLER].maxStack  = 1;
    ITEMS[ITEM_TWIN_CONTROLLER].colour    = 0xC080EE;
    ITEMS[ITEM_TWIN_CONTROLLER].sprite    = SPR_CIRCUIT_CONSTANT;

    ITEMS[ITEM_GARLIC_FIELD_CHIP].name     = "Garlic Field Chip";
    ITEMS[ITEM_GARLIC_FIELD_CHIP].kind     = ITEMK_DRONE_MODULE;
    ITEMS[ITEM_GARLIC_FIELD_CHIP].maxStack = 1;
    ITEMS[ITEM_GARLIC_FIELD_CHIP].colour   = 0xB8E67C;
    ITEMS[ITEM_GARLIC_FIELD_CHIP].sprite   = SPR_MOD_BLAST;

    ITEMS[ITEM_GLOW_FLARE].name       = "Glowflare";
    ITEMS[ITEM_GLOW_FLARE].kind       = ITEMK_THROWABLE;
    ITEMS[ITEM_GLOW_FLARE].maxStack   = 64;
    ITEMS[ITEM_GLOW_FLARE].power      = 0;
    ITEMS[ITEM_GLOW_FLARE].damage     = 1;
    ITEMS[ITEM_GLOW_FLARE].pierce     = 1;
    ITEMS[ITEM_GLOW_FLARE].shotSpeed  = 4.0f;
    ITEMS[ITEM_GLOW_FLARE].shotColour = 0x9AF4BE;
    ITEMS[ITEM_GLOW_FLARE].colour     = 0x9AF4BE;
    ITEMS[ITEM_GLOW_FLARE].sprite     = SPR_FLARE;

    /* Player accessories mirror the three drone-chip concepts without sharing
       their sockets or their code paths. Two trinket slots make every one a
       loadout decision rather than another always-on property of the pack. */
    ITEMS[ITEM_GARLIC_ACCESSORY].name      = "Garlic Accessory";
    ITEMS[ITEM_GARLIC_ACCESSORY].kind      = ITEMK_ACCESSORY;
    ITEMS[ITEM_GARLIC_ACCESSORY].equipSlot = EQ_TRINKET_A;
    ITEMS[ITEM_GARLIC_ACCESSORY].maxStack  = 1;
    ITEMS[ITEM_GARLIC_ACCESSORY].colour    = 0xDCE8B0;
    ITEMS[ITEM_GARLIC_ACCESSORY].sprite    = SPR_ACC_GARLIC;

    ITEMS[ITEM_OVERLOAD_ACCESSORY].name      = "Overload Accessory";
    ITEMS[ITEM_OVERLOAD_ACCESSORY].kind      = ITEMK_ACCESSORY;
    ITEMS[ITEM_OVERLOAD_ACCESSORY].equipSlot = EQ_TRINKET_A;
    ITEMS[ITEM_OVERLOAD_ACCESSORY].maxStack  = 1;
    ITEMS[ITEM_OVERLOAD_ACCESSORY].colour    = 0xF0B85C;
    ITEMS[ITEM_OVERLOAD_ACCESSORY].sprite    = SPR_ACC_OVERLOAD;

    ITEMS[ITEM_TWIN_ACCESSORY].name      = "Twin Accessory";
    ITEMS[ITEM_TWIN_ACCESSORY].kind      = ITEMK_ACCESSORY;
    ITEMS[ITEM_TWIN_ACCESSORY].equipSlot = EQ_TRINKET_A;
    ITEMS[ITEM_TWIN_ACCESSORY].maxStack  = 1;
    ITEMS[ITEM_TWIN_ACCESSORY].colour    = 0xC89AF0;
    ITEMS[ITEM_TWIN_ACCESSORY].sprite    = SPR_ACC_TWIN;

    /* --- the charms ------------------------------------------------------
       Every one of these is four lines of table and one number that matters,
       which is the shape a rare drop wants: the tooltip is the whole design
       document, and a player who finds one knows what it does before they have
       finished reading its name.

       The numbers are sized against a fight rather than against each other. A
       layer-1 creature hits for 6 to 16, so 2 points of armour is a fifth off a
       husk and a third off a mite -- felt, and nowhere near immunity. 18% move
       speed is about what Hermes boots give, deliberately, because a trinket
       that beat a dedicated boot slot would make the boot slot pointless. */

    /* Mite. Armour, and the one charm that SUMS with worn armour rather than
       taking the largest -- see the note on ItemDef::regenPer for why that is
       the rule holding rather than an exception to it. */
    ITEMS[ITEM_CARAPACE_CHARM].name      = "Carapace Charm";
    ITEMS[ITEM_CARAPACE_CHARM].kind      = ITEMK_ACCESSORY;
    ITEMS[ITEM_CARAPACE_CHARM].equipSlot = EQ_TRINKET_A;
    ITEMS[ITEM_CARAPACE_CHARM].maxStack  = 1;
    ITEMS[ITEM_CARAPACE_CHARM].armour    = 2;
    ITEMS[ITEM_CARAPACE_CHARM].colour    = 0xB07848;
    ITEMS[ITEM_CARAPACE_CHARM].sprite    = SPR_ACC_CARAPACE;

    /* Moth. Half the rebalanced light drone: enough to see your feet and the
       wall you are standing at, not enough to replace the companion whose
       whole job this is. */
    ITEMS[ITEM_MOTH_LANTERN].name      = "Moth Lantern";
    ITEMS[ITEM_MOTH_LANTERN].kind      = ITEMK_ACCESSORY;
    ITEMS[ITEM_MOTH_LANTERN].equipSlot = EQ_TRINKET_A;
    ITEMS[ITEM_MOTH_LANTERN].maxStack  = 1;
    ITEMS[ITEM_MOTH_LANTERN].lightGlow = 75;
    ITEMS[ITEM_MOTH_LANTERN].colour    = 0xFFC24A;
    ITEMS[ITEM_MOTH_LANTERN].sprite    = SPR_ACC_LANTERN;

    /* Slime. 34 extra cells on a bare radius of 20 is nearly three times the
       reach, which sounds enormous and is the correct size for this effect:
       below roughly double, a magnet is indistinguishable from walking over
       things, so a timid version of this charm would be a charm nobody could
       tell they were wearing. */
    ITEMS[ITEM_SLIME_MAGNET].name         = "Slime Magnet";
    ITEMS[ITEM_SLIME_MAGNET].kind         = ITEMK_ACCESSORY;
    ITEMS[ITEM_SLIME_MAGNET].equipSlot    = EQ_TRINKET_A;
    ITEMS[ITEM_SLIME_MAGNET].maxStack     = 1;
    ITEMS[ITEM_SLIME_MAGNET].pickupRadius = 34;
    ITEMS[ITEM_SLIME_MAGNET].colour       = 0x8FC85A;
    ITEMS[ITEM_SLIME_MAGNET].sprite       = SPR_ACC_MAGNET;

    /* Husk. One point every 90 frames is a point and a half a second, so a full
       heal from nearly dead takes two and a half minutes. That is meant to be
       slow: it is the charm that changes what happens BETWEEN fights, and one
       that healed at combat speed would quietly delete the food ladder. */
    ITEMS[ITEM_HUSK_HEART].name      = "Husk Heart";
    ITEMS[ITEM_HUSK_HEART].kind      = ITEMK_ACCESSORY;
    ITEMS[ITEM_HUSK_HEART].equipSlot = EQ_TRINKET_A;
    ITEMS[ITEM_HUSK_HEART].maxStack  = 1;
    ITEMS[ITEM_HUSK_HEART].regenPer  = 90;
    ITEMS[ITEM_HUSK_HEART].colour    = 0x6E7A52;
    ITEMS[ITEM_HUSK_HEART].sprite    = SPR_ACC_HEART;

    /* Bat. Its own overshoot is what makes a bat hard to escape, so the charm
       it drops being the answer to that is the roster teaching itself. */
    ITEMS[ITEM_SWIFT_CHARM].name      = "Swift Charm";
    ITEMS[ITEM_SWIFT_CHARM].kind      = ITEMK_ACCESSORY;
    ITEMS[ITEM_SWIFT_CHARM].equipSlot = EQ_TRINKET_A;
    ITEMS[ITEM_SWIFT_CHARM].maxStack  = 1;
    ITEMS[ITEM_SWIFT_CHARM].speedPct  = 18;
    ITEMS[ITEM_SWIFT_CHARM].colour    = 0xE8D45A;
    ITEMS[ITEM_SWIFT_CHARM].sprite    = SPR_ACC_SWIFT;

    /* Spitter. Muzzle velocity rather than damage, and under one world gravity
       that is not a small distinction: a faster shot DROPS LESS over the same
       distance (see ItemDef::shotSpeed), so this charm makes long range aiming
       easier rather than making hits hurt more. It is the charm you notice by
       hitting things you used to miss. */
    ITEMS[ITEM_SPITTER_BRACER].name         = "Spitter Bracer";
    ITEMS[ITEM_SPITTER_BRACER].kind         = ITEMK_ACCESSORY;
    ITEMS[ITEM_SPITTER_BRACER].equipSlot    = EQ_TRINKET_A;
    ITEMS[ITEM_SPITTER_BRACER].maxStack     = 1;
    ITEMS[ITEM_SPITTER_BRACER].shotSpeedPct = 35;
    ITEMS[ITEM_SPITTER_BRACER].colour       = 0xC8E060;
    ITEMS[ITEM_SPITTER_BRACER].sprite       = SPR_ACC_BRACER;

    /* Pedestal loot. Both are flat combat multipliers, which is the reward that
       should sit lit in a chamber you chose to walk into rather than falling
       out of whatever wandered past -- a charm you can go and LOOK for wants to
       be the one whose value needs no explaining. */
    ITEMS[ITEM_WHETSTONE].name      = "Whetstone";
    ITEMS[ITEM_WHETSTONE].kind      = ITEMK_ACCESSORY;
    ITEMS[ITEM_WHETSTONE].equipSlot = EQ_TRINKET_A;
    ITEMS[ITEM_WHETSTONE].maxStack  = 1;
    ITEMS[ITEM_WHETSTONE].damagePct = 25;
    ITEMS[ITEM_WHETSTONE].colour    = 0xAEB6C4;
    ITEMS[ITEM_WHETSTONE].sprite    = SPR_ACC_WHETSTONE;

    /* 20% off the delay rather than the Overload Accessory's flat quarter, so
       the two are a genuine choice at four slots rather than one shadowing the
       other. They do NOT stack: cooldownPct takes the largest and the Overload
       is resolved separately, whichever is kinder wins, and a build wearing
       both gets one of them -- which is what stops fire rate running away the
       moment the trinket row got wider. */
    ITEMS[ITEM_CHRONOMETER].name        = "Chronometer";
    ITEMS[ITEM_CHRONOMETER].kind        = ITEMK_ACCESSORY;
    ITEMS[ITEM_CHRONOMETER].equipSlot   = EQ_TRINKET_A;
    ITEMS[ITEM_CHRONOMETER].maxStack    = 1;
    ITEMS[ITEM_CHRONOMETER].cooldownPct = 20;
    ITEMS[ITEM_CHRONOMETER].colour      = 0x8A93A6;
    ITEMS[ITEM_CHRONOMETER].sprite      = SPR_ACC_CHRONO;


    /* The starter weapon. A TOOL with no module slots, which is what makes it
       the floor of the ladder rather than a rung on it -- there is nothing to
       socket into it and never will be, so the only way to shoot harder is to
       build the Multitool it is pointedly worse than. See toolResolve for how a
       slotless tool fires from its own stats. */
    ITEMS[ITEM_BOLTER].name       = "Bolt Caster";
    ITEMS[ITEM_BOLTER].kind       = ITEMK_TOOL;
    ITEMS[ITEM_BOLTER].maxStack   = 1;
    ITEMS[ITEM_BOLTER].colour     = 0x9A8A6E;
    ITEMS[ITEM_BOLTER].toolSlots  = 0;
    ITEMS[ITEM_BOLTER].baseDelay  = 22;
    /* Power 0 breaks NOTHING -- every material outranks it, so a bolt stops at
       the first wall instead of digging. That is the line between a weapon and
       a tool, and this side of it is where a starter weapon belongs. */
    ITEMS[ITEM_BOLTER].power      = 0;
    ITEMS[ITEM_BOLTER].damage     = 4;
    ITEMS[ITEM_BOLTER].pierce     = 1;
    /* Faster than the default, and that is what makes it a BOLT. Everything
       here falls at one gravity, so the only thing separating a flat shot from
       a lobbed one is time of flight: at 6.0 a bolt drops about two cells over
       thirty and eight over the full reach, which is flat enough to point and
       shoot at something in your face and arced enough that hitting a moth
       across a cavern is a thing you learn rather than a thing you are given.
       At the 3.5 default it dropped nearly seven cells at thirty, which for the
       weapon you are handed before you can build anything else read as broken
       rather than as ballistic. */
    ITEMS[ITEM_BOLTER].shotSpeed  = 6.0f;
    ITEMS[ITEM_BOLTER].shotColour = 0xE8D8A0;
    ITEMS[ITEM_BOLTER].sprite     = SPR_BOLTER;

    /* Bread. 30 of 100, so it is a meaningful recovery without being a reset
       button -- three loaves brings you back from nearly dead, and carrying
       three loaves is a decision about pack space. */
    ITEMS[ITEM_BREAD].name     = "Bread";
    ITEMS[ITEM_BREAD].kind     = ITEMK_FOOD;
    ITEMS[ITEM_BREAD].heal     = 30;
    ITEMS[ITEM_BREAD].maxStack = 32;
    ITEMS[ITEM_BREAD].colour   = 0xC89A5A;
    ITEMS[ITEM_BREAD].sprite   = SPR_BREAD;

    /* The striker. Reusable rather than consumed: it is a pair of stones, the
       cost of replacing it would be trivial, and an igniter you can run out of
       is an igniter that strands you next to an unlit furnace. */
    ITEMS[ITEM_FLINT].name     = "Flint Striker";
    ITEMS[ITEM_FLINT].kind     = ITEMK_IGNITE;
    ITEMS[ITEM_FLINT].maxStack = 1;
    ITEMS[ITEM_FLINT].colour   = 0xB8B4A6;
    ITEMS[ITEM_FLINT].sprite   = SPR_FLINT;

    /* Layer-two creature matter. It stays a non-placeable component rather
       than pretending to be a world material: adding a MatId would move
       MAT_COUNT and renumber every established tool in existing saves. */
    ITEMS[ITEM_ICHOR].name        = "Ichor";
    ITEMS[ITEM_ICHOR].kind        = ITEMK_COMPONENT;
    ITEMS[ITEM_ICHOR].maxStack    = MATERIAL_STACK;
    ITEMS[ITEM_ICHOR].colour      = 0x8F4358;
    ITEMS[ITEM_ICHOR].sprite      = SPR_ICHOR;
    ITEMS[ITEM_ICHOR].description =
        "Dense living residue from layer-two creatures. Used in advanced fabrication.";

    /* Spawn eggs, one per creature, built straight off the creature table --
       so a creature added tomorrow gets an egg with no edit here at all, and
       the egg cannot disagree with it about name or colour.

       maxStack 1: these are a debug convenience, and a slot holding ninety-nine
       of them is a slot you have to clear out. */
    static char eggNames[ENT_COUNT][40];
    for (int t = ENT_NONE + 1; t < ENT_COUNT; ++t) {
        /* Stated by the creature, not computed from it. This was
           ITEM_EGG_MITE + (t - 1), arithmetic across two enums in headers that
           cannot see each other, and the eighth creature would have run that
           sum off the end of the egg block and overwritten ITEM_FORGE_CORE. */
        const ItemId id = ENT_DEFS[t].eggItem;
        if (id == ITEM_NONE) continue;
        sprintf(eggNames[t], "%s Egg", ENT_DEFS[t].name);
        ITEMS[id].name     = eggNames[t];
        ITEMS[id].kind     = ITEMK_EGG;
        ITEMS[id].summons  = (u8)t;
        ITEMS[id].maxStack = 1;
        ITEMS[id].colour   = ENT_DEFS[t].eggColour;
        /* One shell tinted per creature, baked in initSprites from this same
           eggColour -- so the icon cannot disagree with the swatch, and an
           eighth creature needs no edit here either. */
        ITEMS[id].sprite   = (u8)(SPR_EGG_FIRST + (t - 1));
        ITEMS[id].description = "Creative testing item. Use it to spawn this creature.";
    }

    /* The boss summon. An egg by KIND -- it spawns a creature and is consumed,
       which is exactly what an egg does -- but a crafted item rather than a
       creative-menu convenience. See ITEM_BROOD_CALL. */
    ITEMS[ITEM_BROOD_CALL].name     = "Brood Call";
    ITEMS[ITEM_BROOD_CALL].kind     = ITEMK_EGG;
    ITEMS[ITEM_BROOD_CALL].summons  = ENT_BROOD;
    ITEMS[ITEM_BROOD_CALL].maxStack = 4;
    ITEMS[ITEM_BROOD_CALL].colour   = 0xC85A44;
    ITEMS[ITEM_BROOD_CALL].sprite   = SPR_BROOD_CALL;

    ITEMS[ITEM_TITANIUM_HELMET].name       = "Titanium Helmet";
    ITEMS[ITEM_TITANIUM_HELMET].kind       = ITEMK_WORN;
    ITEMS[ITEM_TITANIUM_HELMET].equipSlot  = EQ_HEAD;
    ITEMS[ITEM_TITANIUM_HELMET].maxStack   = 1;
    ITEMS[ITEM_TITANIUM_HELMET].colour     = 0xC8CCD2;
    ITEMS[ITEM_TITANIUM_HELMET].heatResist = 45;
    ITEMS[ITEM_TITANIUM_HELMET].coldResist = 45;
    ITEMS[ITEM_TITANIUM_HELMET].armour     = 5;
    ITEMS[ITEM_TITANIUM_HELMET].sprite     = SPR_ARMOUR_HELM_TITANIUM;

    ITEMS[ITEM_TITANIUM_SUIT].name       = "Titanium Suit";
    ITEMS[ITEM_TITANIUM_SUIT].kind       = ITEMK_WORN;
    ITEMS[ITEM_TITANIUM_SUIT].equipSlot  = EQ_BODY;
    ITEMS[ITEM_TITANIUM_SUIT].maxStack   = 1;
    ITEMS[ITEM_TITANIUM_SUIT].colour     = 0xC8CCD2;
    ITEMS[ITEM_TITANIUM_SUIT].heatResist = 70;
    ITEMS[ITEM_TITANIUM_SUIT].coldResist = 70;
    ITEMS[ITEM_TITANIUM_SUIT].armour     = 9;
    ITEMS[ITEM_TITANIUM_SUIT].sprite     = SPR_ARMOUR_SUIT_TITANIUM;

    ITEMS[ITEM_FORGE_CORE].name     = "Forge Core";
    ITEMS[ITEM_FORGE_CORE].kind     = ITEMK_COMPONENT;  /* carried, never placed */
    ITEMS[ITEM_FORGE_CORE].maxStack = 16;
    ITEMS[ITEM_FORGE_CORE].colour   = 0xE07A32;

    /* Hover descriptions are authored only for items with a use, equipment
       effect, or non-obvious role. Material ids keep description == 0, with
       Forge Core as the intentional exception: it looks like a carried part
       but unlocks a station and therefore needs explaining. */
    ITEMS[ITEM_MULTITOOL].description =
        "A three-slot energy weapon. Installed shot modules fire from left to right.";
    ITEMS[ITEM_MULTITOOL2].description =
        "A five-slot chassis with a larger battery, faster recharge, and quicker firing.";
    ITEMS[ITEM_MOD_SHOT].description =
        "Fires a straight beam that damages enemies and bores through loose terrain.";
    ITEMS[ITEM_MOD_BLAST].description =
        "Launches a heavy explosive shot that breaks stone and damages groups.";
    ITEMS[ITEM_MOD_BOUNCE].description =
        "Fires a cheap arcing bolt that ricochets naturally from nearby surfaces.";
    ITEMS[ITEM_MOD_HOMING].description =
        "Fires a large, slow floating orb that curves toward nearby enemies.";
    ITEMS[ITEM_MOD_TELEPORT].description =
        "Teleports you to the last safe position before the projectile hits.";

    ITEMS[ITEM_SICKLE].description =
        "Harvests plants in a broad sweep without damaging the ground beneath them.";
    ITEMS[ITEM_GRASS_SEED].description =
        "Sow on exposed dirt to grow a patch of grass.";
    ITEMS[ITEM_LENS].description = "Extends your building and interaction reach while equipped.";
    ITEMS[ITEM_RELAY].description = "Greatly extends your building and interaction reach while equipped.";
    ITEMS[ITEM_ROCKET_BOOTS].description =
        "Hold jump in the air for a short upward boost. Fuel recharges on the ground.";
    ITEMS[ITEM_HERMES].description = "Greatly increases movement speed while worn.";

    ITEMS[ITEM_THERMOCOUPLE].description = "Outputs a circuit signal based on local temperature.";
    ITEMS[ITEM_CLOCK].description = "Outputs a repeating circuit timing signal.";
    ITEMS[ITEM_PLACER].description = "Places stored material when activated by a circuit signal.";
    ITEMS[ITEM_MINER].description = "Excavates material in front of it when activated.";
    ITEMS[ITEM_TORCH_DEV].description = "A placeable light source that also provides heat.";
    ITEMS[ITEM_ITEM_PIPE].description = "Transfers stored items between adjacent machines.";
    ITEMS[ITEM_PIPE_CROSSOVER].description = "Lets two item-pipe routes cross without mixing.";
    ITEMS[ITEM_WORKBENCH].description = "A basic crafting station for early tools and components.";
    ITEMS[ITEM_ANVIL].description = "A metalworking station for equipment and mechanical parts.";
    ITEMS[ITEM_CHEMSTN].description = "A crafting station for chemical processing and advanced materials.";
    ITEMS[ITEM_ASSEMBLY].description = "An advanced station for precise machines and guided weapons.";
    ITEMS[ITEM_FORGESTN].description = "A high-temperature station for late-game alloys and equipment.";
    ITEMS[ITEM_BED].description = "Rest to pass time quickly and set your respawn point.";
    ITEMS[ITEM_CHEST].description = "Stores item stacks outside your inventory.";
    ITEMS[ITEM_SPOUT].description = "Moves liquids and gases out of connected storage.";
    ITEMS[ITEM_DRAIN].description = "Collects nearby liquids and gases into connected storage.";
    ITEMS[ITEM_BLOCK_WATCHER].description = "Outputs a signal when the watched cell changes.";
    ITEMS[ITEM_PULSE_BUTTON].description = "Sends a brief circuit pulse when pressed.";
    ITEMS[ITEM_CONSTANT_COMBINATOR].description = "Outputs a configurable constant circuit signal.";
    ITEMS[ITEM_ARITHMETIC_COMBINATOR].description = "Performs arithmetic on circuit signals.";
    ITEMS[ITEM_DECIDER_COMBINATOR].description = "Tests circuit conditions and outputs a chosen signal.";
    ITEMS[ITEM_PEDESTAL].description = "Displays and illuminates a single item placed upon it.";

    ITEMS[ITEM_STEEL_HELMET].description = "Protective headgear with armour and temperature resistance.";
    ITEMS[ITEM_STEEL_SUIT].description = "Protective body armour with strong temperature resistance.";
    ITEMS[ITEM_TITANIUM_HELMET].description = "Advanced headgear with heavy armour and temperature resistance.";
    ITEMS[ITEM_TITANIUM_SUIT].description = "Advanced body armour built for severe heat, cold, and combat.";
    ITEMS[ITEM_FORGE_CORE].description = "A Brood Queen relic used to construct a Blast Furnace.";

    ITEMS[ITEM_LIGHT_DRONE].description = "Follows you and illuminates nearby terrain.";
    ITEMS[ITEM_ATTACK_DRONE].description = "Follows you and fires bolts at enemies with a clear line of sight.";
    ITEMS[ITEM_PICKUP_DRONE].description = "Collects nearby loose items and returns them to your pack.";
    ITEMS[ITEM_SHIELD_DRONE].description = "Intercepts hostile shots and pushes nearby enemies away.";
    ITEMS[ITEM_LANCE_DRONE].description = "A close-range combat drone that drives a piercing lance through enemies.";
    ITEMS[ITEM_MORTAR_DRONE].description = "Lobs explosive shells at distant enemies.";
    ITEMS[ITEM_ORBIT_DRONE].description = "Orbits you as a damaging defensive weapon.";
    ITEMS[ITEM_DRONE_VISOR].description =
        "Drone Armour. 2 pieces: +1 combat-drone bay. 3 pieces: +50% drone damage.";
    ITEMS[ITEM_DRONE_HARNESS].description =
        "Drone Armour. 2 pieces: +1 combat-drone bay. 3 pieces: +50% drone damage.";
    ITEMS[ITEM_DRONE_GREAVES].description =
        "Drone Armour. 2 pieces: +1 combat-drone bay. 3 pieces: +50% drone damage.";
    ITEMS[ITEM_IRON_HELMET].description =
        "Basic iron head protection. It has no set bonus.";
    ITEMS[ITEM_IRON_CUIRASS].description =
        "Basic iron body armour. It has no set bonus.";
    ITEMS[ITEM_IRON_GREAVES].description =
        "Basic iron leg protection. It has no set bonus.";
    ITEMS[ITEM_RANGER_VISOR].description =
        "Ranger Armour. 2 pieces: +10% ranged damage and +15% range. 3 pieces: +20% damage and +30% range.";
    ITEMS[ITEM_RANGER_COAT].description = ITEMS[ITEM_RANGER_VISOR].description;
    ITEMS[ITEM_RANGER_GREAVES].description = ITEMS[ITEM_RANGER_VISOR].description;
    ITEMS[ITEM_VANGUARD_HELM].description =
        "Vanguard Armour. 2 pieces: +20% melee damage. 3 pieces: +20% reach and +15% swing speed.";
    ITEMS[ITEM_VANGUARD_PLATE].description = ITEMS[ITEM_VANGUARD_HELM].description;
    ITEMS[ITEM_VANGUARD_GREAVES].description = ITEMS[ITEM_VANGUARD_HELM].description;
    ITEMS[ITEM_DRONE_BEACON].description =
        "Unlocks one additional combat-drone bay. Duplicate Beacons do not stack.";
    ITEMS[ITEM_OVERCLOCK_CHIP].description = "Install in an attack drone to reduce the delay between shots.";
    ITEMS[ITEM_TWIN_CONTROLLER].description = "Install in an attack drone to fire a second bolt with each attack.";
    ITEMS[ITEM_GARLIC_FIELD_CHIP].description = "Install in a shield drone to damage enemies inside its field.";

    ITEMS[ITEM_GLOW_FLARE].description = "Throw to create a temporary glowing marker.";
    ITEMS[ITEM_GARLIC_ACCESSORY].description = "Periodically damages enemies close to you.";
    ITEMS[ITEM_OVERLOAD_ACCESSORY].description = "Reduces weapon firing delay by 25%.";
    ITEMS[ITEM_TWIN_ACCESSORY].description = "Fires a second, slightly offset projectile with each weapon shot.";
    ITEMS[ITEM_CARAPACE_CHARM].description = "A hardened trinket that adds flat armour.";
    ITEMS[ITEM_MOTH_LANTERN].description = "Makes the wearer emit a soft light.";
    ITEMS[ITEM_SLIME_MAGNET].description = "Pulls loose items toward you from farther away.";
    ITEMS[ITEM_HUSK_HEART].description = "Slowly restores health while you are injured.";
    ITEMS[ITEM_SWIFT_CHARM].description = "Increases movement speed while equipped.";
    ITEMS[ITEM_SPITTER_BRACER].description = "Increases projectile speed and reduces long-range drop.";
    ITEMS[ITEM_WHETSTONE].description = "Increases damage dealt by weapon projectiles.";
    ITEMS[ITEM_CHRONOMETER].description = "Reduces the delay between weapon shots.";

    ITEMS[ITEM_BOLTER].description = "A simple starter weapon. Damages creatures but cannot break terrain.";
    ITEMS[ITEM_WARP_WAND].description =
        "Teleports you to wherever the bolt stops -- on impact, or where it "
        "runs out of flight. Holds one charge and takes three seconds to "
        "refill.";
    ITEMS[ITEM_SPARK].description =
        "A loose electrical mote. It drifts downward and starts a pulse when it touches a conductor; right-click one to capture it.";
    ITEMS[ITEM_BREAD].description =
        "Eat to restore health. All consumable healing shares one cooldown.";
    ITEMS[ITEM_FLINT].description = "Use on a nearby flammable material to ignite it.";
    ITEMS[ITEM_BROOD_CALL].description = "Consume to summon the Brood Queen nearby.";

    /* Spawn eggs. Named from the creature table so the two can never disagree
       about what an egg makes, and swatched in each creature's own colour so
       three otherwise identical entries in the creative list are told apart the
       way everything else in that list is -- by looking at it.

       maxStack 1 rather than a real stack: these are a debug convenience, and a
       slot holding ninety-nine of them is a slot you have to clear out. */
    /* Materials remain colour swatches because their world colour is useful
       information. Every named object gets an actual fallback silhouette until
       it has bespoke pixel art, so the UI never regresses to colour-only icons. */
    for (int i = MAT_COUNT; i < ITEM_COUNT; ++i)
        if (ITEMS[i].maxStack && ITEMS[i].sprite == SPR_NONE) ITEMS[i].sprite = SPR_ITEM_GENERIC;
}

const char* const EQ_NAMES[EQ_COUNT] = { "Feet", "Back", "Trinket 1", "Trinket 2",
                                        "Head", "Body", "Light Drone", "Drone A", "Drone B",
                                        "Trinket 3", "Trinket 4", "Drone C" };
/* What is actually PAINTED in an empty slot. The long names above are for the
   tooltip, where there is room for them; drawn into the square itself they were
   clipped to "rinke" and "ht Dr", which is worse than no label at all because
   it looks like a rendering fault rather than an abbreviation. The trinkets are
   bare numerals because the panel groups them under one TRINKETS heading, so
   the word is already on screen once and repeating it four times says
   nothing. */
const char* const EQ_SHORT[EQ_COUNT] = { "Feet", "Back", "1", "2",
                                         "Head", "Body", "Lamp", "A", "B",
                                         "3", "4", "C" };

bool eqIsTrinket(int eqSlot) {
    for (int i = 0; i < EQ_TRINKET_COUNT; ++i) if (EQ_TRINKETS[i] == eqSlot) return true;
    return false;
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
    for (int i = 0; i < EQ_COUNT; ++i) releaseStack(equip[i]);
    memset(equip, 0, sizeof(equip));
    memset(droneModule, 0, sizeof(droneModule));
    memset(droneLevel, 0, sizeof(droneLevel));
    selected = 0;
}

/* A thousand, which is a blunt number and admitted as one: wood is tedious
   to gather early and this is a stopgap until it is not. */
static const int STARTING_WOOD = 1000;

void inventoryStartingKit(Inventory& inv) {
    if (inv.countOf(ITEM_BOLTER) == 0)    inv.add(ITEM_BOLTER, 1);
    if (inv.countOf(ITEM_FLINT) == 0)     inv.add(ITEM_FLINT, 1);
    /* Slow enough not to be a free ride -- one charge, three seconds to
       refill -- and convenience is worth more here than the ceremony of
       earning it. See the wand's own note for what the battery costs. */
    if (inv.countOf(ITEM_WARP_WAND) == 0) inv.add(ITEM_WARP_WAND, 1);
    if (inv.countOf((ItemId)MAT_WOOD) == 0)
        inv.add((ItemId)MAT_WOOD, STARTING_WOOD);
}

int Inventory::add(ItemId item, int count) {
    if (!g_itemsReady) {
        fprintf(stderr, "inventory used before initItems() -- every stack limit "
                        "is 0, so nothing can ever be picked up\n");
        abort();
    }
    if (item == ITEM_NONE || count <= 0) return count > 0 ? count : 0;
    const int cap = (int)ITEMS[item].maxStack;
    if (cap <= 0) return count;

    /* Top up existing stacks first. Opening a new slot for an item you are
       already carrying is how an inventory ends up with three half-stacks of
       sand and no room for anything else. */
    for (int i = 0; i < INV_SLOTS && count > 0; ++i) {
        if (slot[i].item != item || (int)slot[i].count >= cap) continue;
        const int room = cap - (int)slot[i].count;
        const int put  = (count < room) ? count : room;
        slot[i].count = (u32)(slot[i].count + put);
        count -= put;
    }
    for (int i = 0; i < INV_SLOTS && count > 0; ++i) {
        if (!slot[i].empty()) continue;
        const int put = (count < cap) ? count : cap;
        slot[i].item  = item;
        slot[i].count = (u32)put;
        /* A tool becomes a distinct object the moment it exists. Doing this
           here rather than at every call site means there is no way to end up
           holding a multitool that cannot remember its own modules. */
        slot[i].inst  = (ITEMS[item].kind == ITEMK_TOOL) ? toolInstNew(item) : 0;
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
        const int have = (int)slot[selected].count;
        const int got = (count < have) ? count : have;
        slot[selected].count = (u32)(slot[selected].count - got);
        if (slot[selected].count == 0) { releaseStack(slot[selected]); slot[selected].item = ITEM_NONE; }
        taken += got;
        count -= got;
    }
    for (int i = 0; i < INV_SLOTS && count > 0; ++i) {
        if (slot[i].item != item || slot[i].count == 0) continue;
        const int have = (int)slot[i].count;
        const int got = (count < have) ? count : have;
        slot[i].count = (u32)(slot[i].count - got);
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
    for (int i = 0; i < EQ_COUNT; ++i) {
        if (equip[i].empty()) continue;
        const int b = ITEMS[equip[i].item].reachBonus;
        if (b > best) best = b;
    }
    return best;
}

int Inventory::speedBonus() const {
    int best = 0;
    for (int i = 0; i < EQ_COUNT; ++i) {
        if (equip[i].empty()) continue;
        const int b = ITEMS[equip[i].item].speedPct;
        if (b > best) best = b;
    }
    return best;
}

TempSpec Inventory::tempResist() const {
    TempSpec t; t.heat = t.cold = 0;
    for (int i = 0; i < EQ_COUNT; ++i) {
        if (equip[i].empty()) continue;
        const ItemDef& d = ITEMS[equip[i].item];
        if (d.heatResist > t.heat) t.heat = d.heatResist;
        if (d.coldResist > t.cold) t.cold = d.coldResist;
    }
    return t;
}

/* Every accessory passive is the same question asked of a different column, so
   it is one function taking the column rather than six copies of a loop. A
   member pointer keeps the call sites reading as plainly as a field access
   would while leaving exactly one place where "largest, never summed" lives. */
static int bestWorn(const Inventory& inv, i16 ItemDef::*stat) {
    int best = 0;
    for (int i = 0; i < EQ_COUNT; ++i) {
        if (inv.equip[i].empty()) continue;
        const int v = (int)(ITEMS[inv.equip[i].item].*stat);
        if (v > best) best = v;
    }
    return best;
}

/* Regeneration is the one passive where LARGEST is the wrong word, because a
   smaller number is the better item: it counts FRAMES between points of health,
   so 90 heals faster than 180. Resolved as the smallest non-zero rather than by
   inverting the stat, so the table still reads in the unit the effect uses. */
int Inventory::regenPer() const {
    int best = 0;
    for (int i = 0; i < EQ_COUNT; ++i) {
        if (equip[i].empty()) continue;
        const int v = ITEMS[equip[i].item].regenPer;
        if (v > 0 && (best == 0 || v < best)) best = v;
    }
    return best;
}
int Inventory::lightGlow()    const { return bestWorn(*this, &ItemDef::lightGlow); }
int Inventory::pickupRadius() const { return bestWorn(*this, &ItemDef::pickupRadius); }
int Inventory::shotSpeedPct() const { return bestWorn(*this, &ItemDef::shotSpeedPct); }
int Inventory::damagePct()    const { return bestWorn(*this, &ItemDef::damagePct); }
int Inventory::cooldownPct()  const { return bestWorn(*this, &ItemDef::cooldownPct); }

int Inventory::armour() const {
    int total = 0;
    bool accessorySeen[ITEM_COUNT];
    memset(accessorySeen, 0, sizeof(accessorySeen));
    for (int i = 0; i < EQ_COUNT; ++i) {
        if (equip[i].empty()) continue;
        const ItemId item = equip[i].item;
        /* Worn armour pieces add across body slots. An accessory still joins
           that total, but a duplicate of the same accessory is never a second
           effect -- the same non-stacking rule every scalar charm uses. */
        if (ITEMS[item].kind == ITEMK_ACCESSORY) {
            if (accessorySeen[item]) continue;
            accessorySeen[item] = true;
        }
        total += ITEMS[item].armour;
    }
    return total;
}

bool Inventory::hasEquipped(ItemId item) const {
    if (item == ITEM_NONE) return false;
    for (int i = 0; i < EQ_COUNT; ++i)
        if (!equip[i].empty() && equip[i].item == item) return true;
    return false;
}

int Inventory::armourSetPieces(u8 set) const {
    if (set == ARMOUR_SET_NONE) return 0;
    int count = 0;
    for (int i = 0; i < EQ_COUNT; ++i)
        if (!equip[i].empty() && ITEMS[equip[i].item].armourSet == set) ++count;
    return count;
}

int Inventory::combatDroneSlots() const {
    int slots = 1;
    if (armourSetPieces(ARMOUR_SET_DRONE) >= 2) ++slots;
    /* hasEquipped is boolean by design: a second copy is visible equipment but
       cannot apply the Beacon effect twice. */
    if (hasEquipped(ITEM_DRONE_BEACON)) ++slots;
    return imin(slots, DRONE_BAY_COUNT - 1);
}

bool Inventory::droneBayUnlocked(int eqSlot) const {
    if (eqSlot == EQ_DRONE_A) return true;
    if (eqSlot == EQ_DRONE_B) return combatDroneSlots() >= 2;
    if (eqSlot == EQ_DRONE_C) return combatDroneSlots() >= 3;
    return true;
}

int Inventory::droneDamagePct() const {
    return armourSetPieces(ARMOUR_SET_DRONE) >= 3 ? 50 : 0;
}

int Inventory::rangedDamagePct() const {
    const int pieces = armourSetPieces(ARMOUR_SET_RANGED);
    return pieces >= 3 ? 20 : pieces >= 2 ? 10 : 0;
}

int Inventory::rangedRangePct() const {
    const int pieces = armourSetPieces(ARMOUR_SET_RANGED);
    return pieces >= 3 ? 30 : pieces >= 2 ? 15 : 0;
}

int Inventory::meleeDamagePct() const {
    return armourSetPieces(ARMOUR_SET_MELEE) >= 2 ? 20 : 0;
}

int Inventory::meleeReachPct() const {
    return armourSetPieces(ARMOUR_SET_MELEE) >= 3 ? 20 : 0;
}

int Inventory::meleeSpeedPct() const {
    return armourSetPieces(ARMOUR_SET_MELEE) >= 3 ? 15 : 0;
}

bool equipFits(ItemId item, int eqSlot) {
    if (item == ITEM_NONE || eqSlot < 0 || eqSlot >= EQ_COUNT) return false;
    const ItemDef& d = ITEMS[item];
    if ((d.kind != ITEMK_WORN && d.kind != ITEMK_ACCESSORY) || d.equipSlot >= EQ_COUNT) return false;
    if (d.equipSlot == (u8)eqSlot) return true;
    /* A trinket names EQ_TRINKET_A and means "any trinket slot" -- see the note
       on ItemDef::equipSlot. Asking the table rather than listing the pairs is
       what stopped the third and fourth slots being unreachable the moment they
       were added, which is the exact bug the second one had. */
    if (eqIsTrinket(d.equipSlot) && eqIsTrinket(eqSlot)) return true;
    return (d.equipSlot == EQ_DRONE_A &&
            (eqSlot == EQ_DRONE_B || eqSlot == EQ_DRONE_C));
}

int Inventory::packWorn(int eqSlot) const {
    for (int i = 0; i < INV_SLOTS; ++i) {
        if (slot[i].empty()) continue;
        if (equipFits(slot[i].item, eqSlot)) return i;
    }
    return -1;
}

bool Inventory::equipFromPack(ItemId item) {
    if (item == ITEM_NONE) return false;
    const ItemDef& d = ITEMS[item];
    if ((d.kind != ITEMK_WORN && d.kind != ITEMK_ACCESSORY) || d.equipSlot >= EQ_COUNT) return false;
    if (countOf(item) <= 0) return false;

    /* Prefer an empty slot it fits, and only swap when every one it fits is
       taken. Without that, putting on a second trinket always replaced the
       first while the slot next to it sat empty. */
    int target = -1;
    for (int i = 0; i < EQ_COUNT; ++i)
        if (equipFits(item, i) && droneBayUnlocked(i) && equip[i].empty()) { target = i; break; }
    if (target < 0)
        for (int i = 0; i < EQ_COUNT; ++i)
            if (equipFits(item, i) && droneBayUnlocked(i)) { target = i; break; }
    if (target < 0) return false;

    ItemStack& eq = equip[target];

    /* Put the old one away FIRST, and give up if it will not go. Doing it the
       other way round -- overwrite, then try to stow -- loses the old item
       whenever the pack is full, and the pack is fullest exactly when you are
       swapping gear because you just picked something up. */
    if (!eq.empty()) {
        if (add(eq.item, (int)eq.count) != 0) return false;
        eq.item = ITEM_NONE; eq.count = 0; eq.inst = 0;
    }
    if (take(item, 1) != 1) return false;
    eq.item = item; eq.count = 1; eq.inst = 0;
    return true;
}

bool Inventory::unequip(int eqSlot) {
    if (eqSlot < 0 || eqSlot >= EQ_COUNT) return false;
    ItemStack& eq = equip[eqSlot];
    if (eq.empty()) return false;
    if (add(eq.item, (int)eq.count) != 0) return false;   /* no room: keep it on */
    eq.item = ITEM_NONE; eq.count = 0; eq.inst = 0;
    return true;
}

FlightSpec flightSpec(const Inventory& inv) {
    FlightSpec best;
    best.thrust = best.riseCap = best.refuel = 0.0f;
    best.fuel = 0;
    for (int i = 0; i < EQ_COUNT; ++i) {
        if (inv.equip[i].empty()) continue;
        const FlightSpec& f = ITEMS[inv.equip[i].item].fly;
        if (f.any() && f.riseCap > best.riseCap) best = f;
    }
    return best;
}

ToolSpec miningSpec(const Inventory& inv) {
    ToolSpec best = HAND;
    for (int i = 0; i < INV_SLOTS; ++i) {
        if (inv.slot[i].empty()) continue;
        const ItemDef& d = ITEMS[inv.slot[i].item];
        /* A sickle is a specialist harvesting tool, not an upgrade for stone.
           Keep it available when held in the future, but never let it hide a
           real miner merely because it happens to have a large radius. */
        if (d.kind != ITEMK_MINING || d.mineRadius == 0 || d.minePlantsOnly) continue;
        const int bestRate = best.cellsPerBite * d.mineCooldown;
        const int thisRate = d.mineBite * best.cooldown;
        if (d.minePower < best.power) continue;
        if (d.minePower == best.power && thisRate < bestRate) continue;
        if (d.minePower == best.power && thisRate == bestRate && d.mineRadius <= best.maxRadius) continue;
        best.name         = d.name;
        best.maxRadius    = d.mineRadius;
        best.cellsPerBite = d.mineBite;
        best.cooldown     = d.mineCooldown;
        best.plantsOnly   = false;
        best.power        = d.minePower;
    }
    return best;
}

int Inventory::firstToolSlot() const {
    for (int i = 0; i < INV_SLOTS; ++i) {
        if (slot[i].empty()) continue;
        const ItemDef& d = ITEMS[slot[i].item];
        /* toolSlots > 0, not merely ITEMK_TOOL -- see the note in item.h. A
           slotless weapon is a tool with nothing to configure, and answering
           with it hides the bench of whatever multitool is further down. */
        if (d.kind == ITEMK_TOOL && d.toolSlots > 0) return i;
    }
    return -1;
}

/* One place that turns "what this item says about its shot" into the two
   numbers a projectile actually needs, so a slotless tool and a module cannot
   disagree about what shotSpeed 0 or shotBeam means. */
static void resolveFlight(const ItemDef& d, ToolShot& s) {
    s.speed   = (d.shotSpeed > 0.0f) ? d.shotSpeed : SHOT_SPEED_DEFAULT;
    s.gravity = d.shotBeam ? 0.0f : PROJ_GRAVITY;
}

ToolShot toolResolve(const ItemStack& st) {
    ToolShot s;
    s.canFire = false; s.delay = 0; s.power = 0; s.damage = 0; s.pierce = 0; s.blast = 0;
    s.energyCost = 0; s.moduleSlot = -1; s.life = 90; s.bounces = 0;
    s.colour = 0xFFFFFF; s.payloadMat = MAT_EMPTY;
    s.speed = SHOT_SPEED_DEFAULT; s.gravity = PROJ_GRAVITY; s.homing = 0.0f;
    s.effect = PROJ_EFFECT_NONE;
    if (st.empty() || ITEMS[st.item].kind != ITEMK_TOOL) return s;

    const ItemDef& tool = ITEMS[st.item];
    s.delay = tool.baseDelay;

    /* A tool with NO slots shoots from its own row. That is how the starter
       weapon works and the only way it could: damage lives on modules, and a
       weapon you cannot socket anything into would otherwise be a stick.

       Answered ABOVE the instance check, not below it, and that placement is
       the whole point rather than a tidiness preference. A ToolInst is where a
       tool's MUTABLE state lives -- which modules are fitted, what payload is
       loaded -- and a slotless weapon has none of that by construction. Making
       it wait for an instance would mean the starter weapon you spawn holding
       stops working the moment one fails to be allocated, and it would fail the
       way this function's own comment describes: silently, as a stick. Nothing
       below this point is read here. */
    /* `damage > 0` alone would have been the test, and it was, until a
       slotless tool wanted to do something other than hurt: a wand whose
       whole payload is an EFFECT has no damage by definition, and under the
       old condition it fell through to the module loop, found no modules,
       and reported itself unfireable -- a stick, in the words above. What
       makes a slotless tool a weapon is having anything at all to deliver. */
    if (tool.toolSlots == 0 && (tool.damage > 0 || tool.shotEffect != PROJ_EFFECT_NONE)) {
        s.canFire = true;
        s.power   = tool.power;
        s.damage  = tool.damage;
        s.pierce  = tool.pierce;
        s.blast   = tool.blast;
        s.colour  = tool.shotColour;
        /* The rest of the shot, which this branch used to leave at its
           defaults. Harmless while the only slotless tool was a plain bolt
           that wanted every one of them; now that one carries an effect and
           a price, silently dropping these would have fired a free,
           full-range shot that did nothing on arrival. */
        s.effect     = tool.shotEffect;
        s.energyCost = tool.energyCost;
        s.life       = tool.shotLife ? tool.shotLife : 90;
        s.bounces    = tool.shotBounces;
        s.homing     = tool.shotHoming;
        resolveFlight(tool, s);
        return s;
    }

    if (st.inst == 0 || st.inst >= MAX_TOOL_INST) return s;   /* no state: a stick */

    const ToolInst& ti = g_toolInst[st.inst];
    /* Only reported if there is actually something left to fire -- an empty
       payload slot is the same as none loaded, not a MatId of zero counted
       wrong. */
    if (!ti.payload.empty() && ITEMS[ti.payload.item].kind == ITEMK_MATERIAL)
        s.payloadMat = (u8)ti.payload.item;

    const int n = imin(tool.toolSlots, TOOL_SLOTS_MAX);
    const int start = n ? ti.shotCursor % n : 0;
    for (int offset = 0; offset < n; ++offset) {
        const int i = (start + offset) % n;
        const ItemId m = ti.slot[i];
        if (m == ITEM_NONE || ITEMS[m].kind != ITEMK_MODULE) continue;
        /* One socket is one shot in the firing sequence. Advancing happens
           only after a successful spawn, so a full projectile pool or empty
           battery cannot silently eat a turn in the configured order. */
        const ItemDef& d = ITEMS[m];
        s.canFire = true; s.moduleSlot = i;
        s.power = d.power; s.damage = d.damage; s.pierce = d.pierce;
        s.blast = d.blast; s.colour = d.shotColour;
        s.delay = imax(3, (int)tool.baseDelay + (int)d.addDelay);
        s.energyCost = d.energyCost;
        s.life = d.shotLife ? d.shotLife : 90;
        s.bounces = d.shotBounces; s.homing = d.shotHoming; s.effect = d.shotEffect;
        resolveFlight(d, s);
        break;
    }
    return s;
}

bool toolShotEnergyAvailable(const ItemStack& st, const ToolShot& shot) {
    return st.inst > 0 && st.inst < MAX_TOOL_INST &&
           g_toolInst[st.inst].used &&
           (int)g_toolInst[st.inst].energy >= shot.energyCost;
}

void toolCommitShot(ItemStack& st, const ToolShot& shot, int cooldown) {
    if (!toolShotEnergyAvailable(st, shot)) return;
    ToolInst& ti = g_toolInst[st.inst];
    ti.energy = (u16)((int)ti.energy - shot.energyCost);
    ti.cooldown = imax(0, cooldown);
    if (shot.moduleSlot >= 0) {
        const int slots = imin((int)ITEMS[st.item].toolSlots, TOOL_SLOTS_MAX);
        ti.shotCursor = (u8)(slots ? (shot.moduleSlot + 1) % slots : 0);
    }
}

/* Which item puts this machine down, found by asking the item table rather
   than by keeping a second list beside DEVS. Linear, and it does not matter:
   this runs when somebody digs a machine, not per cell per frame. */
ItemId itemForDeviceType(u8 deviceType) {
    for (int i = 1; i < ITEM_COUNT; ++i)
        if (ITEMS[i].kind == ITEMK_DEVICE && ITEMS[i].deviceType == deviceType)
            return (ItemId)i;
    return ITEM_NONE;
}

int digInto(World& w, Inventory& inv, int cx, int cy, int r, int maxCells,
            bool plantsOnly, int power, const bool* whitelist) {
    int dug = 0;
    const int n = g_discEnd[imax(0, imin(r, DISC_MAX_R))];
    for (int i = 0; i < n; ++i) {
        if (maxCells > 0 && dug >= maxCells) break;
        const int x = cx + g_disc[i].dx, y = cy + g_disc[i].dy;
        if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) continue;
        /* A torch is now a non-physical fixture so water can occupy its cells.
           It therefore has no material for the ordinary dig rule to see; mine
           the device itself, return its item, and leave the water untouched. */
        const int fixture = torchAt(x, y);
        if (fixture >= 0) {
            if (inv.add(ITEM_TORCH_DEV, 1) != 0) continue;
            torchRemoveAt(fixture);
            ++dug;
            continue;
        }
        /* A machine met by a dig is PICKED UP, whole, not chipped at. Same
           shape as the torch case above and for the same reason: the thing in
           these cells is an object, not material, and the only sensible answer
           to "mine it" is to hand back the object.

           Before this a dig took one cell of the footprint, banked a bogus
           "Device" item, and left the Device struct registered over a hole --
           which then either limped on or was silently destroyed by devIntact,
           losing the machine with nothing returned. */
        if (Device* dev = devAt(x, y)) {
            const ItemId back = itemForDeviceType(dev->type);
            /* No item means no way to hand it back, so it is not diggable at
               all -- better an immovable machine than one that evaporates. */
            if (back == ITEM_NONE) continue;
            /* A pedestal owns the item standing on it.  Removing the device
               without transferring that item made a very ordinary mining
               action a delete button.  Stage the complete pickup in a copy so
               this remains all-or-nothing: if either stack cannot fit, the
               real pack and the pedestal are both left untouched. */
            Inventory pickedUp = inv;
            if (pickedUp.add(back, 1) != 0) continue;
            if (dev->type == DEV_PEDESTAL) {
                const ItemId displayed = (ItemId)pedestalItem(*dev);
                const int displayedCount = pedestalCount(*dev);
                if (displayed != ITEM_NONE && displayedCount > 0 &&
                    pickedUp.add(displayed, displayedCount) != 0)
                    continue;
            }
            inv = pickedUp;
            devRemove(w, dev);
            ++dug;
            continue;
        }
        const u8 m = w.at(x, y).mat;
        if (m == MAT_EMPTY) continue;
        /* The harvesting tool passes over everything that did not grow, and
           passes over it WITHOUT spending a bite -- skipping before the counter
           rather than after is what lets one sweep take a whole row of wheat
           standing in a field of grass, instead of the bite being eaten by the
           ground between the plants. */
        if (plantsOnly && !g_matIsPlant[m]) continue;
        /* Too hard for this tool. Skipped BEFORE the bite counter, exactly like
           the plantsOnly filter above and for the same reason: a sweep that
           clips the corner of a layer barrier should still clear the rock
           beside it, not spend its whole bite failing against the barrier. */
        if ((int)g_matStrength[m] > power) continue;
        /* Not on the list. Skipped before the bite counter for the same reason
           as the two above, and here it is the entire point of the feature: a
           sweep over a heap of ceramic with copper and slag ticked should take
           every copper cell in reach, not spend its bite on the first ceramic
           it touches. See the note on `whitelist` in item.h. */
        if (whitelist && !whitelist[m]) continue;
        /* g_matDropsAs, not m: what you dig out and what you end up holding are
           two different questions for anything whose cell is a STATE. Breaking
           an open door puts a door in your pack.
           
           MAT_EMPTY there means the cell yields nothing at all -- a stalk --
           and that has to be a success rather than a full pack, or cutting a
           crop would leave every stem standing. */
        const u8 drop = g_matDropsAs[m];
        if (drop != MAT_EMPTY) {
            /* Bank it first, and only remove it from the world if it fit. The
               order matters: dig-then-store would drop material on the floor of
               a full pack, and players notice that exactly once -- when it was
               something they wanted. */
            if (inv.add((ItemId)drop, 1) != 0) continue;
        }
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

int overwriteFrom(World& w, Inventory& inv, int cx, int cy, int r, int maxCells, int power) {
    ItemStack& h = inv.held();
    if (h.empty() || ITEMS[h.item].kind != ITEMK_MATERIAL) return 0;

    int put = 0;
    const int n = g_discEnd[imax(0, imin(r, DISC_MAX_R))];
    for (int i = 0; i < n; ++i) {
        if (put >= maxCells || h.empty()) break;
        const int x = cx + g_disc[i].dx, y = cy + g_disc[i].dy;
        if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) continue;
        if (w.blocksCell(x, y) || devAt(x, y)) continue;
        const u8 old = w.at(x, y).mat;
        if (old == MAT_EMPTY || (int)g_matStrength[old] > power) continue;

        /* Deposit the displaced item first. A full inventory leaves the old
           cell untouched, which is the only order that cannot turn a full pack
           into silent deletion. Open doors and other state-cells already map to
           their honest item through g_matDropsAs. */
        const ItemId drop = (ItemId)g_matDropsAs[old];
        if (drop != ITEM_NONE && inv.add(drop, 1) != 0) continue;
        const ItemId want = h.item;
        if (inv.take(want, 1) != 1) break;
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

int sowSeeds(World& w, Inventory& inv, int cx, int cy, int r, int maxCells) {
    ItemStack& h = inv.held();
    if (h.empty() || ITEMS[h.item].kind != ITEMK_SEED) return 0;

    int sown = 0;
    const int n = g_discEnd[imax(0, imin(r, DISC_MAX_R))];
    for (int i = 0; i < n; ++i) {
        if (maxCells > 0 && sown >= maxCells) break;
        const int x = cx + g_disc[i].dx, y = cy + g_disc[i].dy;
        if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) continue;
        if (w.at(x, y).mat != MAT_DIRT) continue;
        /* Only dirt with a face to the air takes. Buried dirt would turn to
           grass and die back on the very next frame, so charging a seed for it
           would be taking payment for nothing. */
        if (!w.airWithin(x, y, GRASS_DEPTH)) continue;
        const ItemId want = h.item;
        if (inv.take(want, 1) != 1) return sown;
        /* setCell rather than convert: convert() is world.cpp's private
           phase-change helper, which deliberately preserves temperature and
           the frame stamp. A seed is an outside edit, and outside edits go
           through the same door the brush uses. */
        w.setCell(x, y, MAT_GRASS);
        ++sown;
    }
    return sown;
}
