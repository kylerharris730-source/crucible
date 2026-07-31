#pragma once
#include "world.h"
#include "sprite.h"
#include "player.h"   /* FlightSpec: what worn flight gear resolves to */
#include "device.h"   /* DeviceType: which machine an ITEMK_DEVICE places */

/* --- items and the inventory ----------------------------------------------

   Item ids SHARE the material id space. Ids below MAT_COUNT *are* materials --
   a stack of ITEM(MAT_STONE) is a stack of stone -- and everything that is not
   a material is numbered above it.

   That is worth doing rather than keeping a parallel item table with a mapping
   between the two, because in this game the overwhelming majority of items are
   simply materials you dug up. A mapping would need maintaining every time a
   material is added, would be wrong the first time somebody forgot, and would
   buy nothing: no material needs to be two different items, and no item needs
   to be two different materials. main.cpp already numbers its heat and cool
   brushes this way (TOOL_HEAT = MAT_COUNT), so the pattern is not new here.

   The consequence to remember is that ITEM_NONE and MAT_EMPTY are the same
   value, which is convenient -- an empty slot and empty space are both 0 -- but
   it does mean "is this slot occupied" and "is this cell air" are the same
   test, and a bug in one reads as a bug in the other. */

typedef u16 ItemId;

static const ItemId ITEM_NONE = MAT_EMPTY;   /* both are 0 */

enum {
    /* Non-material items start where materials stop. */
    ITEM_MULTITOOL = MAT_COUNT,   /* Mk I: 3 module slots */
    ITEM_MULTITOOL2,              /* Mk II: 5 slots, and quicker off the mark */
    /* Modules. What a tool DOES lives here, not on the tool -- an empty
       multitool is a stick. */
    ITEM_MOD_SHOT,
    ITEM_MOD_BLAST,
    /* Mining tools. A ladder with only one job: how much rock you move, and
       how fast. See ITEMK_MINING for why they gate digging and not building. */
    ITEM_DRILL,
    ITEM_AUGER,
    ITEM_LANCE,
    ITEM_DISRUPTOR,
    /* The one mining tool that is not a tier: it cuts ONLY what grew. See
       ItemDef::minePlantsOnly. */
    ITEM_SICKLE,
    /* Reach extenders. They do nothing when held and everything when carried --
       see ITEMK_CARRIED below. */
    ITEM_LENS,
    ITEM_RELAY,
    /* Sown onto exposed dirt to start a lawn. See ITEMK_SEED. */
    ITEM_GRASS_SEED,
    /* Flight. Boots go on the feet and packs on the back, so a jetpack does not
       cost you your boots -- which is what leaves room for the speed boots to
       compete for the FEET slot later, and makes combining the two a thing
       crafting can reward rather than something the slots decide for you. */
    ITEM_ROCKET_BOOTS,
    /* Hermes boots: ground speed, and the thing that finally makes the FEET
       slot a decision. Rocket boots get you UP, these get you ALONG, both go on
       the feet, and you cannot have both -- which is exactly the contest the
       slot was split off to create. See speedPct. */
    ITEM_HERMES,
    ITEM_JETPACK1,
    ITEM_JETPACK2,
    ITEM_JETPACK3,
    /* Machines. See ITEMK_DEVICE. */
    ITEM_THERMOCOUPLE,
    ITEM_CLOCK,
    ITEM_PLACER,
    ITEM_MINER,
    ITEM_TORCH_DEV,
    ITEM_COUNT
};

enum ItemKind {
    ITEMK_MATERIAL = 0,   /* stacks; one unit is one cell of world */
    ITEMK_TOOL,           /* unique, carries its own state */
    ITEMK_MODULE,         /* slots into a tool to change what it does */
    /* --- ITEMK_WORN ------------------------------------------------------
       Goes in an equipment slot and does its job from there. It used to be
       ITEMK_CARRIED and work from anywhere in the pack, which was honestly
       labelled a stopgap at the time -- the reach items needed somewhere to
       live before there was anywhere to put them.

       Equipping is the better rule for one reason: it makes the bonus a CHOICE.
       Working from the pack meant the only cost of a reach extender was a slot,
       and a slot is nearly free once you have ten of them; two named slots and
       two trinket slots mean putting one on is deciding not to wear something
       else. It also means the character's abilities are somewhere you can look
       at them, rather than being an emergent property of your luggage. */
    ITEMK_WORN,
    /* --- ITEMK_MINING ---------------------------------------------------
       Held to dig better. It gates DESTRUCTION only, never construction, and
       the asymmetry is the design rather than an oversight.

       Building is expression: capping it would mean the player wants to make
       something and the game says "not yet", which is a bad trade for any
       amount of pacing. Mining is the cost side of the same loop, so slowing
       it is what makes a better tool feel like anything at all. Bare hands
       stay deliberately poor, and every tier after that is the pleasure of
       clearing in one sweep what used to take twenty.

       There is deliberately no building tier to match. A tool you must hold in
       order to place blocks is a slot tax on the most ordinary action in the
       game, and it would make the hotbar a chore rather than a loadout. */
    ITEMK_MINING,
    /* --- ITEMK_SEED ------------------------------------------------------
       Not placed as a cell -- CONVERTS one. Sowing turns exposed dirt into
       grass, which then spreads on its own.

       A separate kind rather than a material whose "placement" happens to have
       a side effect, because everything about it differs: it goes onto a cell
       that is already occupied, it is refused by cells that are buried, and it
       leaves nothing of itself behind. Calling that "placing a block" would
       make placeFrom answer two unrelated questions. */
    ITEMK_SEED,
    /* --- ITEMK_DEVICE ----------------------------------------------------
       Places a MULTI-CELL MACHINE rather than a cell. Its own kind for the same
       reason ITEMK_SEED is: nothing about placement is shared. It covers a
       rectangle instead of a point, it snaps to a lattice rather than landing
       where you clicked, it can fail because the slot is taken rather than
       because the cell is full, and it creates an entity with state that
       outlives the click. `deviceType` says which machine. See device.h. */
    ITEMK_DEVICE
};

struct ItemDef {
    const char* name;
    u8   kind;
    /* u32, not u16, and that is forced rather than generous: a material stack
       is 100000 and a u16 stops at 65535, so the cap would silently wrap to
       34464 and a full stack would start refusing items less than half way up.
       ItemStack::count has to widen with it, for the same reason and to the
       same width -- the two are compared against each other constantly. */
    u32  maxStack;
    u32  colour;          /* for the hotbar swatch */

    /* --- ITEMK_WORN only ---------------------------------------------
       Which slot it occupies. EQ_COUNT for everything that is not worn, so
       "is this equipment" and "where does it go" are one field rather than two
       that can disagree.

       For trinkets this names the FIRST of the interchangeable trinket slots
       rather than one particular slot -- see equipFits(). Naming one exactly
       would make the second trinket slot unreachable, which is what it did at
       first: both reach items said EQ_TRINKET_A, so putting the relay on took
       the lens off and half the equipment row could never be filled. */
    u8   equipSlot;

    /* Cells of extra reach while this is worn. Bonuses do NOT add up: the
       bonus is the single largest one equipped. Summing would make two cheap
       lenses better than one good relay, which turns an upgrade ladder into a
       slot-stuffing puzzle. */
    i16  reachBonus;

    /* Extra ground speed while worn, as a PERCENTAGE. Zero on everything else.
       Resolved by Inventory::speedBonus() on the same "largest, never summed"
       rule as reachBonus, and for the same reason: two cheap pairs of boots
       should not beat one good pair, or an upgrade ladder becomes a
       slot-stuffing puzzle.

       A percentage rather than an absolute speed because the base is tuned to
       the world -- MAX_SPEED is what makes the character feel heavy against
       one-cell terrain -- and a bonus quoted in cells per frame would have to
       be retuned every time that changed. */
    i16  speedPct;

    /* Degrees C of heat and cold protection while worn. Zero on everything so
       far -- there is no armour yet -- and that is deliberate: the columns exist
       because the damage thresholds became a stat on the Player for exactly this
       purpose, and a hook with nothing on the other end of it is how you find
       out at armour time that the wiring never worked.

       Same "largest, never summed" rule as reachBonus, resolved through
       Inventory::tempResist(). Two separate numbers rather than one, because a
       diving suit and a firefighter's coat are not the same garment and a single
       "temperature resistance" would make them one item at two volumes. */
    i16  heatResist, coldResist;

    /* Thrust, for boots and jetpacks. Zero on everything else, and resolved
       through flightSpec() rather than read directly -- see the note there for
       why two pieces of flight gear do not add up either. */
    FlightSpec fly;

    /* --- ITEMK_TOOL only --------------------------------------------- */
    u8   toolSlots;   /* how many modules it holds; this IS the tier */
    u8   baseDelay;   /* frames between shots before any module says otherwise */

    /* --- ITEMK_MINING only -------------------------------------------
       Zero means "not a mining tool", which is what every other item is. */
    u8   mineRadius;
    u8   mineBite;      /* cells per action */
    u8   mineCooldown;  /* frames between actions */
    /* --- the harvesting tool -------------------------------------------
       When set, the tool passes over anything that is not g_matIsPlant. It is
       a RESTRICTION and it is the whole value of the tool: clearing a canopy
       or reaping a field with an ordinary drill takes the ground with it, and
       then you are standing in a crater putting the dirt back by hand.

       A filter rather than a separate tool kind, because everything else about
       it is a mining tool -- radius, bite, cooldown, the same cooldown clock,
       the same banking into the pack. One bit says what it will bite. */
    u8   minePlantsOnly;

    /* --- ITEMK_MODULE only ------------------------------------------- */
    /* Added to the tool's baseDelay. A module that hits harder should cost
       something, and time-between-shots is the cost that stays legible when
       several modules are stacked -- unlike, say, a damage multiplier, where
       two modules interact in a way nobody can predict from the tooltips. */
    u8   addDelay;
    u8   power;       /* highest material strength the shot can break */
    u8   pierce;      /* cells it can destroy before it is spent */
    u8   blast;       /* explosion radius on impact; 0 for an ordinary shot */
    u32  shotColour;

    /* --- ITEMK_DEVICE only -------------------------------------------
       Which DeviceType this places. 0 is a valid device type, so this field
       cannot carry its own "not a device" sentinel -- `kind` is the only thing
       that decides that, and nothing should read this without checking it. The
       same trap as equipSlot, where 0 was a real slot and the memset made every
       stack of stone wearable. */
    u8   deviceType;

    /* SpriteId, or SPR_NONE to fall back to a flat colour swatch. Materials
       deliberately have none -- see sprite.h. */
    u8   sprite;
};

extern ItemDef ITEMS[ITEM_COUNT];

/* Fills in the material half of ITEMS[] from MATS[] and the colour LUT, so a
   new material becomes a carryable item with no extra work. Call after
   initMaterials(). */
void initItems();

/* One cell of world is one unit of item. Deliberately not a bigger number:
   digging a tunnel should visibly fill your pockets, and the arithmetic between
   "cells removed" and "items gained" being 1:1 means there is never a rounding
   question about what a partial stack represents.

   That 1:1 is what sets the stack size. A late-game mining tool clears a
   radius-60 disc, which is over eleven thousand cells in one bite, so a stack
   of 9999 was less than a single sweep -- you would fill a slot, then another,
   then start leaving material in the world with a full pack and nine slots of
   dirt. 100000 is about nine such sweeps, which is a pack you empty because you
   want to rather than because the game keeps stopping you. */
static const int MATERIAL_STACK = 100000;
/* --- how much you can carry ------------------------------------------------
   The pack is ONE array and the hotbar is the first row of it, which is the
   arrangement worth having rather than two containers with rules for moving
   between them. Everything that already worked on the pack -- add, take,
   countOf, the crafting screen, the save -- keeps working on all of it, and
   "put this in the hotbar" is a drag from one slot to another rather than a
   transfer between two systems that each have their own idea of what a slot is.

   Four rows of ten. Ten across because that is the hotbar's width and the grid
   has to line up under it to read as the same container; four rows because a
   stack is 100000 and the thing that fills a pack is VARIETY rather than
   volume -- forty is enough to hold one of everything the world currently
   contains and still have room for what you dug up on the way.

   Widening this DOES cost the inventory section of a save, which is checked by
   exact size: an older file's pack is skipped and the world loads without it.
   That is the format working as designed -- see save.cpp -- and it is the right
   trade here, since the alternative is never being able to change the number. */
static const int HOTBAR_SLOTS = 10;
static const int INV_ROWS     = 4;
static const int INV_SLOTS    = HOTBAR_SLOTS * INV_ROWS;

/* --- tools carry state, materials do not -----------------------------------

   A stack of stone is fully described by "stone" and "how many". A multitool is
   not: two of them differ by what is installed in them and by where each is in
   its firing cycle. So a tool in a slot is a HANDLE -- `inst` indexes a pool of
   ToolInst records, and 0 means "no instance", which is what every material
   stack has.

   A pool rather than storing the modules inline in ItemStack, because ItemStack
   is copied around and lives ten to an inventory; making every slot big enough
   to hold six module ids so that one of them occasionally can is the wrong
   trade. The pool also gives tools an identity that survives being moved
   between slots, which is what stops a tool losing its loadout when the hotbar
   gets reorganised -- and reorganising the hotbar is the very next thing this
   inventory will grow. */
static const int TOOL_SLOTS_MAX = 6;   /* the largest tier; not every tool uses all */
static const int MAX_TOOL_INST  = 32;

struct ToolInst {
    ItemId slot[TOOL_SLOTS_MAX];
    int    cooldown;   /* frames until it can fire again */
    bool   used;
};

extern ToolInst g_toolInst[MAX_TOOL_INST];

/* Returns a fresh instance handle in 1..MAX_TOOL_INST-1, or 0 if the pool is
   full. Index 0 is deliberately never handed out so that 0 can mean "none". */
u16  toolInstNew();
void toolInstFree(u16 inst);

struct ItemStack {
    ItemId item;
    u32    count;   /* see ItemDef::maxStack for why this is not a u16 */
    u16    inst;    /* tool instance handle, 0 for everything else */
    bool empty() const { return item == ITEM_NONE || count == 0; }
};

/* --- equipment slots -------------------------------------------------------
   Named by where they go on the body, not numbered, because the whole value of
   a typed slot is that it says what belongs in it. Two named slots and two
   general ones: the named pair are for things there is obviously only one of,
   and the trinkets are for everything that is simply "worn and passive" and
   would otherwise need a slot invented per item.

   FEET and BACK being separate is a design decision and not a taxonomy. Rocket
   boots and a jetpack can be worn together, which is what leaves the FEET slot
   contested once there are speed boots to put in it. */
enum EquipSlot {
    EQ_FEET = 0,
    EQ_BACK,
    EQ_TRINKET_A,
    EQ_TRINKET_B,
    EQ_COUNT
};

extern const char* const EQ_NAMES[EQ_COUNT];

/* Whether an item may be worn in a given slot. Not simply `equipSlot ==
   eqSlot`, because the two trinket slots are interchangeable: they exist so
   that "worn and passive" needs no slot invented per item, and a trinket that
   could only go in the first of them would leave the second permanently
   empty. */
bool equipFits(ItemId item, int eqSlot);

struct Inventory {
    ItemStack slot[INV_SLOTS];
    /* Worn, and therefore active. Kept as ItemStack rather than a bare ItemId
       so that an equippable tool would keep its instance handle when worn --
       nothing needs that today, and the alternative is a second kind of
       container with its own rules for the first item that does. */
    ItemStack equip[EQ_COUNT];
    int       selected;

    void clear();

    /* Move one of `item` from the pack into the slot its definition names,
       swapping out whatever was there. Returns false if it is not worn gear, or
       if the pack has none, or if what came off could not be put away -- and in
       that last case nothing changes at all, because a full pack must never be
       a way to delete a jetpack. */
    bool equipFromPack(ItemId item);
    /* Take a slot's contents back into the pack. False if the pack is full, in
       which case the item stays on. */
    bool unequip(int eqSlot);
    /* First pack slot holding something that would go in `eqSlot`, or -1. */
    int  packWorn(int eqSlot) const;

    /* Adds up to `count`, filling existing stacks of the same item before
       opening new slots. Returns how many did NOT fit, so the caller can decide
       whether to leave the remainder in the world -- silently destroying what
       will not fit is the kind of thing players notice and resent. */
    int  add(ItemId item, int count);

    /* Removes up to `count`. Returns how many were actually removed. */
    int  take(ItemId item, int count);

    int  countOf(ItemId item) const;
    int  freeSlots() const;

    ItemStack& held() { return slot[selected]; }
    const ItemStack& held() const { return slot[selected]; }

    /* Largest reachBonus among everything WORN; 0 with nothing. */
    int  reachBonus() const;
    /* Largest speedPct among everything WORN; 0 with nothing. Same rule. */
    int  speedBonus() const;
    /* Largest heat and cold protection among everything WORN, each resolved
       independently so a heatproof helmet and cold boots both count for what
       they are. Zero on both with nothing worn, which is the bare character. */
    TempSpec tempResist() const;

    /* The first tool in the pack, or -1. The inventory screen shows one tool's
       loadout and this is the one it shows -- with a single multitool in play
       that is unambiguous, and when there are two it is at least stable. */
    int  firstToolSlot() const;
};

/* --- what a loaded tool does ----------------------------------------------
   Resolved fresh from the tool and its installed modules rather than cached on
   the instance, so pulling a module out takes effect immediately and there is
   no second copy of the truth to keep in step. */
struct ToolShot {
    bool   canFire;
    int    delay;      /* frames between shots: tool base + module */
    int    power;
    int    pierce;
    int    blast;
    u32    colour;
};
ToolShot toolResolve(const ItemStack& st);


extern Inventory g_inv;

/* --- what a digging implement can do --------------------------------------

   Bare hands are a ToolSpec too, rather than a special case with the limits
   hard-coded into the input handler. That is the whole point: the multitool is
   then not a new mechanism, it is a better row in this struct, and every place
   that asks "how fast can I dig" already reads it from somewhere.

   The three numbers are deliberately the ones a player can feel separately.
   maxRadius is how much of the world you can work on at once, cellsPerBite is
   how much of that you actually shift, and cooldown is the rhythm. A tool that
   improved all three at once would feel like one upgrade; tools that improve
   one each give a reason to own more than one. */
struct ToolSpec {
    const char* name;
    int maxRadius;      /* the brush radius is clamped to this */
    int cellsPerBite;   /* cells removed per action */
    int cooldown;       /* frames between actions */
    bool plantsOnly;    /* cuts only what grew; see ItemDef::minePlantsOnly */
};

/* Hands. Slow and small on purpose: this is the baseline every tool is measured
   against, and if bare hands were comfortable no tool would feel like progress.
   10 cells every 6 frames is 100 cells a second -- a 6-wide tunnel advances
   about a body length every second, which is workable for getting somewhere and
   genuinely tiresome for undoing a mistake. That last part is the design: it is
   what makes a precision tool worth building rather than a luxury. */
extern const ToolSpec HAND;
/* What you are digging with right now: the held item's own numbers if it is a
   mining tool, otherwise HAND. Resolved on demand rather than cached, so
   swapping hotbar slots takes effect on the same frame. */
ToolSpec miningSpec(const ItemStack& held);

/* --- what you can fly with ------------------------------------------------
   The best single piece of equipped flight gear, never the sum of two. Same
   rule as reachBonus and for the same reason: adding a jetpack's thrust to a
   pair of boots' would make wearing both strictly better than any single
   upgrade, so the ladder would stop being a ladder and every tier would just be
   another thing to hoard a slot for.

   "Best" is by rate of climb, which is the number you can feel while holding
   the key. Ranking by fuel instead would let a long-burning weak pack shadow a
   strong short one, and the strong one is what you bought. */
FlightSpec flightSpec(const Inventory& inv);


/* --- disc offsets, ordered nearest-first ----------------------------------

   A table of every (dx,dy) within DISC_MAX_R, sorted by distance from the
   centre. Two things fall out of the ordering, and both matter.

   First, a partial bite is a coherent shape. Once digging has a per-frame cell
   budget it will usually stop part-way through a disc, and walking the cells in
   scanline order would eat the top of the circle and leave the bottom -- the
   hole would grow downward-lopsided rather than outward. Nearest-first grows it
   as a circle, ring by ring, which is what "digging" looks like.

   Second, because the table is sorted by distance, every cell within radius r
   is a PREFIX of it. So g_discEnd[r] is just a length, and no per-cell distance
   test is needed at all. */
static const int DISC_MAX_R = 64;      /* matches the brush size clamp */
static const int DISC_MAX_CELLS = 13200;   /* pi*64^2 = 12868, with headroom */

struct DiscOff { i16 dx, dy; };
extern DiscOff g_disc[DISC_MAX_CELLS];
extern int     g_discEnd[DISC_MAX_R + 1];

void initDiscTable();   /* called by initItems() */

/* --- the two verbs --------------------------------------------------------
   Dig cells out of the world into a pack, and build cells out of a pack into
   the world. They live here rather than in main.cpp for two reasons: they are
   game rules rather than window code, and they are precisely what the multitool
   will drive once it has a fire rate and modules to say how big a bite it takes.
   Keeping them out of the message loop means they can be tested without a
   window, which is the only way any of this gets verified. */

/* Removes cells and banks them, working outward from the centre. Returns how
   many cells were removed. Anything that will not fit is LEFT IN THE WORLD
   rather than destroyed.

   maxCells caps how much comes out in one call; 0 means the whole disc, which
   is what the unlimited sandbox brush wants. A tool passes its cellsPerBite. */
int digInto(World& w, Inventory& inv, int cx, int cy, int r, int maxCells = 0,
            bool plantsOnly = false);

/* Places the held stack into empty cells of a disc, one item per cell, until
   the stack runs out. Returns how many cells were filled. Never overwrites
   existing material and never places inside an occupied entity box. */
int placeFrom(World& w, Inventory& inv, int cx, int cy, int r, int maxCells = 0);

/* --- the same two verbs, on the background layer ---------------------------
   Deliberately separate functions rather than a flag on the ones above. The
   rules genuinely differ: background can be placed THROUGH material (you wall
   behind a floor you are standing on), it never collides so there is no entity
   box to dodge, and scraping it off is not the same action as mining the block
   in front of it. Folding all of that into one function behind a boolean would
   make both halves harder to read than either is apart.

   Anything placed here is marked BG_PLACED, which is what will later separate a
   built room from a natural cave. */
int placeBg(World& w, Inventory& inv, int cx, int cy, int r, int maxCells = 0);
int digBg(World& w, Inventory& inv, int cx, int cy, int r, int maxCells = 0);

/* Sows the held seed onto exposed dirt, one seed per cell converted. Returns
   how many took. Dirt with no face to the air is skipped rather than consuming
   a seed -- sowing into the middle of a hill should cost nothing, because it
   achieves nothing. */
int sowSeeds(World& w, Inventory& inv, int cx, int cy, int r, int maxCells = 0);
