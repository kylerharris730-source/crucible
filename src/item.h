#pragma once
#include "world.h"
#include "sprite.h"
#include "player.h"   /* FlightSpec: what worn flight gear resolves to */
#include "device.h"   /* DeviceType: which machine an ITEMK_DEVICE places */
#include "projectile.h"   /* PROJ_GRAVITY: what a shot falls at unless it is a beam */

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
    ITEM_ITEM_PIPE,
    ITEM_PIPE_CROSSOVER,
    ITEM_CHEST,
    ITEM_SPOUT,
    ITEM_DRAIN,
    ITEM_BLOCK_WATCHER,
    ITEM_PULSE_BUTTON,
    ITEM_CONSTANT_COMBINATOR,
    ITEM_ARITHMETIC_COMBINATOR,
    ITEM_DECIDER_COMBINATOR,
    /* Armour: the equipment category ItemDef predicted before it existed --
       see heatResist/coldResist below, which have been sitting unused on
       every item until now. Two tiers, each a helmet and a suit sharing a
       slot with nothing else. */
    ITEM_STEEL_HELMET,
    ITEM_STEEL_SUIT,
    ITEM_TITANIUM_HELMET,
    ITEM_TITANIUM_SUIT,
    /* --- spawn eggs -------------------------------------------------------
       One per creature, and they exist for the same reason the creative
       inventory does: this is a debug tool that is honest about being one.
       Testing a creature otherwise means finding a dark cave at the right
       depth and waiting for the spawner to agree with you, which is a slow way
       to look at a sprite.

       They are ITEMK_EGG rather than a device or a seed because what they do is
       unlike either: a seed converts a cell, a device claims a rectangle, and
       an egg creates something that is not in the grid at all. Numbered in
       EntityType order for readability, but nothing DEPENDS on that order any
       more: what each one makes is stated outright in ItemDef::summons. It used
       to be index arithmetic over this range, which was a coupling between two
       enums in headers that cannot see each other -- and the startup check
       guarding it fired the moment four more creatures arrived, which is the
       coupling proving the point rather than the check being annoying. */
    ITEM_EGG_MITE,
    ITEM_EGG_MOTH,
    ITEM_EGG_SLIME,
    ITEM_EGG_HUSK,
    ITEM_EGG_BAT,
    ITEM_EGG_SPITTER,
    ITEM_EGG_BROOD,
    /* --- the forge core ---------------------------------------------------
       What the layer 1 boss drops, and the only ingredient of the Blast
       Furnace. This is the shape the layer's reward takes: a STATION you win
       once, not an ore you go back and farm. See MAT_STATION_FORGE.

       An item rather than the station itself so that the reward is a thing you
       carry home and decide where to install, which is a small moment and a
       better one than a furnace appearing where the boss died. */
    ITEM_FORGE_CORE,
    /* --- the summon ------------------------------------------------------
       What calls layer 1's boss. Crafted, never found, and consumed on use --
       so fighting her is a thing you DECIDE to do rather than something that
       happens to you in a tunnel.

       That is the Terraria shape and it is the right one here: a boss you can
       blunder into is a boss that kills you while you are carrying a full pack
       of ore, and the whole point of a summon is that you arrive having chosen
       the ground and the moment. */
    ITEM_BROOD_CALL,
    /* --- the striker ------------------------------------------------------
       Two stones knocked together. It exists because there was no other way to
       light a fire: the torch is cold, lava is behind a layer barrier, and
       sparks need iron which needs a fire. See IGNITE_MAX in world.h for why it
       reaches ignition temperature and not one degree further. */
    ITEM_FLINT,
    /* --- the starter weapon ----------------------------------------------
       You spawn holding this. Before it existed a new character could not hurt
       anything at all: damage lives on MODULES, a module needs a Multitool, and
       both need copper -- so the whole first descent, which is where the game
       puts its most dangerous creatures relative to your kit, had no answer to
       anything living.

       Deliberately feeble. It breaks no terrain (power 0), pierces one cell,
       and does four damage against the Shot Module's six. It is the floor of
       the ladder, not a rung on it: enough that a rock mite is a fight rather
       than an execution, and weak enough that building a real weapon is still
       the first thing you want. */
    ITEM_BOLTER,
    /* --- food -------------------------------------------------------------
       What wheat is FOR. It had none: you could grow it, harvest it, and turn
       it back into seed, which is a loop with nothing in the middle. */
    ITEM_BREAD,
    /* --- the workbench, as a thing you place ------------------------------
       Distinct from MAT_STATION_BENCH, which stays exactly what it was: the
       MATERIAL the bench's cells are made of. This is the ITEM you carry and
       put down, and placing it builds a DEV_WORKBENCH whose fourteen-by-
       fourteen footprint is written in that material.

       Two ids for what a player thinks of as one object, and the split is the
       point rather than an accident: craftScanStations looks for the material,
       so it keeps working without knowing devices exist, and an old save whose
       pack holds the material can still put down the single cell it always
       could. */
    ITEM_WORKBENCH,
    /* The rest of the ladder. Same split as ITEM_WORKBENCH: the MAT_STATION_*
       material stays what a station's cells are MADE of, and these are the
       things you carry and put down. */
    ITEM_ANVIL,
    ITEM_CHEMSTN,
    ITEM_ASSEMBLY,
    ITEM_FORGESTN,
    ITEM_BED,
    /* Friendly overlays rather than placed machines: equipped drones follow the
       player, so they need slots on the character instead of a block in hand. */
    ITEM_LIGHT_DRONE,
    ITEM_ATTACK_DRONE,
    ITEM_PICKUP_DRONE,
    ITEM_SHIELD_DRONE,
    /* Drone-control chips live in a drone's own loadout, never on the player. */
    ITEM_OVERCLOCK_CHIP,
    ITEM_TWIN_CONTROLLER,
    ITEM_GARLIC_FIELD_CHIP,
    ITEM_GLOW_FLARE,
    /* Player-side counterparts to drone chips. Appended so existing item ids
       remain stable; these occupy trinket slots, never a drone socket. */
    ITEM_GARLIC_ACCESSORY,
    ITEM_OVERLOAD_ACCESSORY,
    ITEM_TWIN_ACCESSORY,
    /* --- the charms ------------------------------------------------------
       One per layer-1 creature, and that pairing is the whole point rather than
       flavour. Every creature in the layer dropped MAT_CHITIN, so killing a bat
       and killing a husk were the same event with a different sprite in front
       of it -- which is the thing that made combat feel undirected. A charm the
       bat alone can give you means the world now answers "what should I go and
       fight", and it answers differently depending on what you want.

       Rare on purpose: see EntityDef::rareOneIn. A charm is a permanent change
       to how the character plays, so it has to be a thing that HAPPENS rather
       than a thing you farm -- at one in fifty you meet three or four over a
       layer, which is a loadout arriving one decision at a time.

       The last two have no creature and are pedestal loot only. Both are
       straight combat multipliers, which is exactly the kind of reward that
       should be sitting lit in a chamber you chose to walk into rather than
       falling out of whatever wandered past. */
    ITEM_CARAPACE_CHARM,    /* mite    -- armour */
    ITEM_MOTH_LANTERN,      /* moth    -- the wearer glows */
    ITEM_SLIME_MAGNET,      /* slime   -- drops come to you */
    ITEM_HUSK_HEART,        /* husk    -- slow regeneration */
    ITEM_SWIFT_CHARM,       /* bat     -- move speed */
    ITEM_SPITTER_BRACER,    /* spitter -- muzzle velocity */
    ITEM_WHETSTONE,         /* pedestal only -- damage */
    ITEM_CHRONOMETER,       /* pedestal only -- fire rate */
    /* The weapon chassis. See DroneType for why a weapon is a companion here
       rather than something you hold. */
    ITEM_LANCE_DRONE,
    ITEM_MORTAR_DRONE,
    ITEM_ORBIT_DRONE,
    /* What you stand a reward on. See DEV_PEDESTAL. */
    ITEM_PEDESTAL,
    /* --- the melee ladder --------------------------------------------------
       Seven metals, two weapons each, and the ORDER is this game's own material
       ladder rather than Terraria's -- see the note in initItems() for why gold
       sits where it does. Tin is deliberately absent: it exists to be alloyed
       into bronze, and a tin sword would be a rung whose only purpose is to be
       skipped.

       Listed sword-then-spear per tier rather than all swords then all spears,
       so the enum reads in the order somebody actually acquires them. */
    ITEM_SWORD_COPPER,   ITEM_SPEAR_COPPER,
    ITEM_SWORD_BRONZE,   ITEM_SPEAR_BRONZE,
    ITEM_SWORD_IRON,     ITEM_SPEAR_IRON,
    ITEM_SWORD_GOLD,     ITEM_SPEAR_GOLD,
    ITEM_SWORD_STEEL,    ITEM_SPEAR_STEEL,
    ITEM_SWORD_TITANIUM, ITEM_SPEAR_TITANIUM,
    ITEM_SWORD_TUNGSTEN, ITEM_SPEAR_TUNGSTEN,
    /* Appended: saves store numeric item ids, so new projectile modules never
       go beside the older pair even though that would look tidier here. */
    ITEM_MOD_BOUNCE,
    ITEM_MOD_HOMING,
    ITEM_MOD_TELEPORT,
    /* Appended equipment: numeric item ids are persisted in saves. */
    ITEM_DRONE_VISOR,
    ITEM_DRONE_HARNESS,
    ITEM_DRONE_GREAVES,
    ITEM_DRONE_BEACON,
    ITEM_COUNT
};

enum ArmourSet {
    ARMOUR_SET_NONE = 0,
    ARMOUR_SET_DRONE
};

enum ItemKind {
    ITEMK_MATERIAL = 0,   /* stacks; one unit is one cell of world */
    ITEMK_TOOL,           /* unique, carries its own state */
    /* Stackable and consumed when thrown. Flight stats reuse the shot fields,
       but it owns no ToolInst: a stack split in half must remain two ordinary
       counts, never two handles pointing at one mutable weapon. */
    ITEMK_THROWABLE,
    ITEMK_MODULE,         /* slots into a tool to change what it does */
    ITEMK_DRONE_MODULE,   /* installed in a specific companion's internal bay */
    ITEMK_ACCESSORY,      /* passive player modifier; fits a trinket slot */
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
    ITEMK_DEVICE,
    /* Spawns a CREATURE, which is neither a cell nor a machine. Its own kind
       for the same reason ITEMK_SEED and ITEMK_DEVICE are: nothing about
       placement is shared. It needs no free cell, respects no lattice, and what
       it creates does not live in the grid at all. */
    ITEMK_EGG,
    /* --- ITEMK_IGNITE ----------------------------------------------------
       Held, and clicking warms what is under the cursor toward ignition. Its
       own kind rather than a module or a device because it consumes nothing,
       places nothing and creates nothing -- it only nudges a field the
       simulation already has, which is unlike every other verb here. */
    ITEMK_IGNITE,
    /* --- ITEMK_FOOD ------------------------------------------------------
       Eaten, and gone. Its own kind because nothing else here is consumed for
       an effect on the CHARACTER -- every other item acts on the world. */
    ITEMK_FOOD,
    /* --- ITEMK_MELEE -----------------------------------------------------
       Held, and swung. Its own kind rather than an ITEMK_TOOL with a strange
       module, because nothing about it is shared: it fires no projectile, it
       has no module slots, it spends no payload, and its damage happens along
       a MOVING SHAPE over a span of frames rather than at a point on one.

       --- why melee at all, when drones are the autonomous weapons ---
       The Lance Drone already borrows the one thing a held weapon has that a
       companion does not, which is that it points where you point. So a melee
       weapon has to earn its slot with something else, and it does: it is the
       only weapon in the game with no travel time and no ammunition, and the
       only one whose cost is that you must be STANDING IN CONTACT RANGE. Every
       creature here deals contact damage, so swinging a sword is a decision to
       trade health for damage, and that is a genuinely different question from
       any the ranged ladder asks.

       --- it does not dig ---
       Deliberately, and it is the same `power` versus `damage` split the module
       table already draws (see ItemDef::damage). A sword that chewed terrain
       would be a mining tool with a worse shape, and it would make the whole
       mining ladder optional. Melee has no `power` field at all rather than a
       zero one, so the question cannot be asked. */
    ITEMK_MELEE
};

/* How a melee weapon moves, which is the whole difference between the two
   families. Not a stat -- a stat would be a number you could set to 47 and get
   something that is neither. */
enum MeleeStyle {
    /* Sword. Sweeps an arc through `meleeArc` degrees centred on where you
       aimed, so it covers a WIDTH and can catch several creatures in one
       stroke. Short, and the shortest thing in your hand at any tier. */
    MELEE_SWING = 0,
    /* Spear. Extends straight out along the aim and retracts, covering a LINE
       rather than an area. Longer reach and a faster rhythm, at the cost of
       hitting exactly what you pointed at and nothing beside it.

       The pair is the point rather than variety for its own sake: a corridor of
       mites and a single husk are different problems, and having one weapon for
       each makes the hotbar slot a choice. */
    MELEE_STAB
};

struct ItemDef {
    const char* name;
    /* Authored hover text for items whose purpose is not obvious from their
       name and numbers. Ordinary world materials deliberately leave this null:
       dirt, water and acid need a compact label, not a paragraph. */
    const char* description;
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

    /* Flat health subtracted from each contact with a creature. Zero on
       everything that is not armour.

       SUMMED across different pieces, which is the one place this file breaks its own
       "largest, never summed" rule, so it is worth being explicit about why.
       That rule exists to stop two cheap items beating one good one, and it
       bites where several slots can hold the same KIND of thing -- two trinket
       slots, boots against a jetpack. Worn armour cannot do that: a helmet only
       fits EQ_HEAD and a suit only EQ_BODY, so there is no slot to stuff and no
       ladder to shortcut. A helmet and a suit are a SET, and a game where
       wearing both protects you exactly as much as wearing one is a game that
       has taught you not to bother with the helmet. Accessory armour joins the
       total once per ItemId; duplicate accessories never stack. */
    i16  armour;

    /* Set membership for worn armour. A zero value means the piece has no set
       bonuses; sets opt in rather than every armour ladder needing filler. */
    u8   armourSet;

    /* Thrust, for boots and jetpacks. Zero on everything else, and resolved
       through flightSpec() rather than read directly -- see the note there for
       why two pieces of flight gear do not add up either. */
    FlightSpec fly;

    /* --- ITEMK_TOOL only --------------------------------------------- */
    u8   toolSlots;   /* how many modules it holds; this IS the tier */
    u8   baseDelay;   /* frames between shots before any module says otherwise */
    u16  energyCapacity; /* maximum charge stored by this multitool chassis */
    u8   energyRecharge; /* charge restored per simulation frame */

    /* --- ITEMK_MINING only -------------------------------------------
       Zero means "not a mining tool", which is what every other item is. */
    u8   mineRadius;
    u8   mineBite;      /* cells per action */
    u8   mineCooldown;  /* frames between actions */
    /* The hardest g_matStrength this tool will bite, the same threshold a shot's
       `power` means -- so "what can break this" is one question with one answer
       whether the thing asking is a projectile or a drill.

       Digging had NO such gate before the layer barriers arrived: digInto()
       removed whatever it touched, and the four mining tiers differed only in
       radius, bite and cooldown. Every existing tool is therefore set to
       STR_HARD, which is the strongest material that existed at the time, so
       nothing that could be dug yesterday resists today. The gate exists for
       exactly one material -- see MAT_STRATUM -- and the tool that beats it
       does not exist yet. */
    u8   minePower;
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
    /* Added to the tool's baseDelay for this module's turn in the firing
       sequence. Negative values make light shots faster; energyCost provides
       the other half of their sustained-fire balance. */
    i16  addDelay;
    u16  energyCost;  /* charge spent only when a projectile actually spawns */
    /* --- power is TERRAIN, damage is COMBAT ---------------------------
       Two numbers, and keeping them apart is what lets the game have a
       progression at all.

       `power` is a threshold against g_matStrength, so it is bounded by the
       material ladder: STR_HARD is 210, absolute is 255, and a shot that
       breaks everything breakable has nowhere further to go. That is a
       perfectly good terminal state for a mining stat.

       `damage` has no such ceiling, and must not: three cave layers plus a
       hardmode with four more bosses needs roughly seven steps of combat
       power, and there are four rungs left on the strength ladder. Deriving
       damage from power -- which is what a single "power" number would have
       meant -- would have capped how strong a weapon can ever be at how hard
       rock is, which are not related questions.

       An int rather than a u8 for the same reason. */
    u8   power;       /* highest material strength the shot can break */
    int  damage;      /* health taken off a creature it hits */
    u8   pierce;      /* cells it can destroy before it is spent */
    u8   blast;       /* explosion radius on impact; 0 for an ordinary shot */
    u32  shotColour;

    /* --- how fast it leaves the muzzle, in cells per frame -------------------
       This became a stat that matters the moment shots started falling. Drop
       over a distance is quadratic in flight time, so with one world gravity
       (see PROJ_GRAVITY) speed alone decides whether something flies nearly
       flat or lobs -- and it does so the way it does in reality, without a
       per-weapon gravity fudge for each new gun.

       Zero means "use the default", which keeps every module that predates this
       shooting at exactly the speed it always did. */
    float shotSpeed;

    /* Set on a shot that must fly PERFECTLY STRAIGHT, and there is currently
       exactly one: the Shot Module, whose own note says it "has to read as a
       beam, not a pebble". It is also the mining weapon, so this is not only a
       matter of look -- an arcing beam curves down into the floor a few cells
       out and stops boring the straight tunnel that is the entire point of it.

       Kept as an explicit opt-out rather than an opt-in list so that the
       ordinary case, a thing thrown into the air coming back down, is what you
       get by saying nothing. */
    u8   shotBeam;
    u8   shotBounces;
    u8   shotLife;
    u8   shotEffect;  /* ProjectileEffect */
    float shotHoming; /* steering fraction per frame; zero flies ballistically */

    /* --- ITEMK_EGG only ----------------------------------------------
       Which EntityType this spawns. ENT_NONE (0) on everything else.

       Stated per item rather than derived from where the item sits in the enum.
       The derived version was index arithmetic across two enums that cannot
       include each other, kept honest by a startup abort -- and it aborted the
       first time the creature roster grew. One field cannot fall out of step
       with itself. */
    u8   summons;

    /* --- ITEMK_FOOD only ---------------------------------------------
       Health restored per unit eaten. */
    i16  heal;

    /* --- ITEMK_DEVICE only -------------------------------------------
       Which DeviceType this places. 0 is a valid device type, so this field
       cannot carry its own "not a device" sentinel -- `kind` is the only thing
       that decides that, and nothing should read this without checking it. The
       same trap as equipSlot, where 0 was a real slot and the memset made every
       stack of stone wearable. */
    u8   deviceType;

    /* --- ITEMK_ACCESSORY only -----------------------------------------
       The passive scalars. Every one of them is a single number that moves one
       number the game already had, which is exactly what makes a rare drop feel
       good rather than swingy: you know what it does the moment you read it,
       and it changes how you play without changing what you can do.

       They resolve on the same "LARGEST, never summed" rule as reachBonus, and
       that is a real design decision rather than an inherited convention, so it
       is worth saying why against the obvious alternative. Vampire Survivors
       stacks its passives, but its passives are not competing for slots -- you
       eventually own all of them. Here there are four interchangeable trinket
       slots and eight charms, so summing would make the answer to every build
       "wear four of the best one", and the four-slot loadout would collapse
       into a single decision made once. Taking the largest makes wearing four
       DIFFERENT charms strictly better than hoarding duplicates, which is the
       loadout actually being a loadout.

       Armour is the exception and stays summed across different pieces, for the reason already written
       on ItemDef::armour -- and it now has a trinket in it, which is worth
       flagging: the Carapace stacks with a helmet and a suit, but a second
       Carapace does not. Armour is
       the one stat the game already committed to being additive, and having one
       trinket join that ladder is what stops the trinket row being a separate
       game with its own arithmetic.

       Zero on everything that is not the charm in question, so an item says
       what it does by naming one field. */
    /* Frames between one point of health returning. 0 is no regeneration,
       which is every other item and the bare character. */
    i16  regenPer;
    /* Light the WEARER emits, on the same 0..255 scale lightAddDynamic takes.
       The character had none of their own -- the Light Drone carried all of it
       -- so this is a genuinely different thing to own rather than a bigger
       number on something you already had. */
    i16  lightGlow;
    /* Extra cells of reach on loose drops, and it also turns the collection
       radius into a MAGNET: past a certain reach, waiting for items to be
       walked over stops reading as a bonus and starts reading as a chore. */
    i16  pickupRadius;
    /* Percentages, all three, and all three chosen because they are levers the
       tool ladder already pulls: a shot's muzzle speed, its damage, and the
       frames between shots. A trinket that moved a number no weapon had would
       need its own explanation on every tooltip. */
    i16  shotSpeedPct;
    i16  damagePct;
    i16  cooldownPct;   /* subtracted; 25 means "fire in three quarters the time" */

    /* --- ITEMK_MELEE only ---------------------------------------------
       Zero on everything else. `meleeDamage` is the one that is not: it shares
       the `damage` column above, because "health taken off a creature it hits"
       is the same question whether a bolt or a blade is asking, and two
       separate damage numbers would be two places to look when a weapon feels
       wrong.

       `meleeStyle` is a MeleeStyle. `meleeReach` is how far the tip gets from
       the body, in cells -- NOT related to Inventory::reachBonus, which is how
       far you can BUILD; a lens that lets you place blocks across a room has no
       business lengthening a sword, and conflating the two would make the
       reach trinkets secretly the best melee accessory in the game.

       `meleeArc` is degrees swept, and is meaningless for a stab.
       `meleeFrames` is how long the stroke takes -- the animation and the
       window in which it can hit are the same span, so what you see is exactly
       what is dangerous. `meleeCooldown` is measured from the START of a
       stroke, so it is the full rhythm of the weapon rather than a pause after
       it; that makes damage-per-second divide out of two numbers instead of
       three.

       `meleeKnock` is cells per frame of push away from the swinger. It is not
       decoration: melee means standing inside contact-damage range, and shoving
       what you hit is the only defensive thing the weapon does. */
    u8   meleeStyle;
    u8   meleeReach;
    u8   meleeArc;
    u8   meleeFrames;
    u8   meleeCooldown;
    float meleeKnock;

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

/* Moved ahead of ToolInst, which now holds one of these directly for its
   payload -- see the note there. */
struct ItemStack {
    ItemId item;
    u32    count;   /* see ItemDef::maxStack for why this is not a u16 */
    u16    inst;    /* tool instance handle, 0 for everything else */
    bool empty() const { return item == ITEM_NONE || count == 0; }
};

struct ToolInst {
    ItemId slot[TOOL_SLOTS_MAX];
    int    cooldown;   /* frames until it can fire again */
    bool   used;
    u16    energy;
    u16    energyCapacity;
    u8     energyRecharge;
    u8     shotCursor; /* next module-slot index considered, wrapping left-to-right */
    /* --- payload ---------------------------------------------------------
       A real ItemStack, not a bare ItemId, and that is the whole design: a
       module slot only ever needs to remember WHICH unique item is
       installed (see `slot` above), but a payload is ammunition and needs a
       COUNT that goes down as it fires. Being a genuine ItemStack means the
       exact same universal slotClick() gesture the pack and equipment
       screens already use -- lift, drop, merge, split-half -- works on this
       for free; nothing new had to be taught to load one. See DESIGN.md
       section 2, "weapons fire materials". Empty (ITEM_NONE, count 0) means
       an ordinary shot with no payload. */
    ItemStack payload;
};

extern ToolInst g_toolInst[MAX_TOOL_INST];

/* Returns a fresh instance handle in 1..MAX_TOOL_INST-1, or 0 if the pool is
   full. Index 0 is deliberately never handed out so that 0 can mean "none". */
u16  toolInstNew(ItemId tool = ITEM_NONE);
void toolInstFree(u16 inst);
void toolInstTick();


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
    /* Appended rather than inserted -- see the note on MAT_ALUMINUM_NITRIDE
       in materials.h for why append-only matters here too: Inventory is
       saved as one raw sized blob (see save.cpp), so widening EQ_COUNT
       changes sizeof(Inventory) and an older save's equipment section is
       skipped rather than misread, exactly the way an outgrown pack already
       is. Inserting these before the trinket slots would silently reassign
       what index 2 and 3 mean in every existing save. */
    EQ_HEAD,
    EQ_BODY,
    /* Drone bays are deliberately separate from trinkets: a light is utility,
       and combat companions are a build choice rather than jewellery. One light
       and one combat bay are innate; the other combat bays are loadout bonuses. */
    EQ_LIGHT_DRONE,
    EQ_DRONE_A,
    EQ_DRONE_B,
    /* --- two more trinket slots, appended -------------------------------
       Appended rather than tucked in beside EQ_TRINKET_A/B, for exactly the
       reason the drone bays above were: the slot's NUMBER is what a save
       stores, so inserting here would silently reassign what index 4 means in
       every existing character. The screen groups them together regardless --
       where a slot sits in this enum and where it sits on the panel are
       separate questions, and only one of them is a compatibility promise.

       Four rather than two because the trinkets are now where the creature
       drops land. With two slots and six charms in the world, five of them are
       dead weight the moment you own the two you like; with four, a loadout is
       a choice between good options rather than a shortlist. */
    EQ_TRINKET_C,
    EQ_TRINKET_D,
    /* Appended so every older equipment-slot number keeps its meaning. */
    EQ_DRONE_C,
    EQ_COUNT
};

/* One utility follower plus three possible combat followers. Combat bays B/C
   are capacity-gated; keeping the storage allocated lets old saves retain an
   item in a bay that is currently locked. */
static const int DRONE_BAY_COUNT = 4;

/* The interchangeable trinket slots, in the order the screen shows them. One
   table rather than a chain of ORs in equipFits, so adding a fifth is a line
   here instead of a condition that has to be found in three places. */
static const int EQ_TRINKETS[] = { EQ_TRINKET_A, EQ_TRINKET_B, EQ_TRINKET_C, EQ_TRINKET_D };
static const int EQ_TRINKET_COUNT = (int)(sizeof(EQ_TRINKETS) / sizeof(EQ_TRINKETS[0]));
bool eqIsTrinket(int eqSlot);

extern const char* const EQ_NAMES[EQ_COUNT];
/* The same slot named in four characters or fewer, which is what actually fits
   inside a 34-pixel square. Kept beside the long names rather than derived from
   them: an abbreviation that is generated is an abbreviation that collides. */
extern const char* const EQ_SHORT[EQ_COUNT];

/* Whether an item may be worn in a given slot. Not simply `equipSlot ==
   eqSlot`, because the trinket slots are interchangeable: they exist so
   that "worn and passive" needs no slot invented per item, and a trinket that
   could only go in the first of them would leave the rest permanently
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

    /* A drone chassis has its own small loadout. The index is the bay rather
       than the visual follower, so slot 0 is always the light bay and 1..3 are
       the three general bays. Present chassis have one socket; higher chassis
       levels can expose more without changing the saved shape. */
    static const int DRONE_MODULE_SLOTS_MAX = 3;
    ItemStack droneModule[DRONE_BAY_COUNT][DRONE_MODULE_SLOTS_MAX];
    u8        droneLevel[DRONE_BAY_COUNT];

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

    /* One combat bay is innate. Drone Armour's 2-piece bonus and one Drone
       Beacon each add one, capped by the three allocated combat bays. */
    int  combatDroneSlots() const;
    bool droneBayUnlocked(int eqSlot) const;
    int  armourSetPieces(u8 set) const;
    int  droneDamagePct() const;

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

    /* Total flat damage reduction from everything worn. Summed, unlike every
       other bonus here -- see ItemDef::armour for why this one is different. */
    int  armour() const;

    /* --- the accessory passives -----------------------------------------
       One function each rather than a struct of them all, because every caller
       wants exactly one: the light pass wants the glow, the firing site wants
       the three shot numbers, and neither has any business being handed the
       other's. Each is the LARGEST among everything worn -- see the note on
       ItemDef::regenPer for why largest and not summed. */
    int  regenPer() const;       /* frames per point; 0 with nothing worn */
    int  lightGlow() const;
    int  pickupRadius() const;   /* EXTRA cells, on top of the bare radius */
    int  shotSpeedPct() const;
    int  damagePct() const;
    int  cooldownPct() const;

    /* True only when this exact item is in an equipment slot. Kept out of the
       pack scan deliberately: accessories are choices competing for two
       trinket slots, not passive bonuses from luggage. */
    bool hasEquipped(ItemId item) const;

    /* The first tool in the pack WITH MODULE SLOTS, or -1. The inventory screen
       shows one tool's loadout and this is the one it shows -- with a single
       multitool in play that is unambiguous, and when there are two it is at
       least stable.

       "With module slots" is doing real work, and it was added after this
       function silently broke the module bench. It used to return the first
       ITEMK_TOOL of any kind, which was unambiguous while the multitools were
       the only tools in the game. The Bolt Caster is also an ITEMK_TOOL, has
       toolSlots == 0 by design, and is handed to you in slot 0 at spawn -- so
       it won this race against every multitool you ever picked up, the panel
       computed a loadout of zero slots, and the module bench stopped appearing
       at all. Nothing errored; the section simply had no height.

       A tool with no slots has no loadout to show, so it is never the right
       answer to this question. */
    int  firstToolSlot() const;
};

/* --- the invariant, restated after a load ----------------------------------
   An instance is `used` if and only if some stack in the pack or the equipment
   references it. Holds by construction while the game is running -- add()
   allocates, releaseStack() frees -- and does NOT survive a load, because the
   inventory and the instance pool are separate things written at separate
   times.

   That gap produced a genuinely baffling bug: a tool that fired exactly once
   and then never again, for the rest of the session. The inventory is saved
   whole, INCLUDING each stack's inst id, and the pool was not saved at all, so
   after loading in a fresh session every stack pointed at an instance whose
   `used` was false. fireTool then wrote its cooldown into that dead instance
   quite happily -- and the frame loop, which decrements cooldowns only for
   instances that are `used`, skipped it forever.

   Two rules disagreeing about the same flag: the WRITE path did not check
   `used` and the TICK path did. Call this after loading and both are true
   again. It also frees instances nothing references, so a load cannot leak the
   pool the way picking up and dropping a tool 32 times once did. */
void toolInstReconcile(Inventory& inv);

/* Muzzle speed for anything that does not state one, in cells per frame. This
   is the number every shot used to fly at, hard-coded at the firing site, so
   leaving it as the fallback is what makes every module that predates
   ItemDef::shotSpeed behave exactly as it did. */
static const float SHOT_SPEED_DEFAULT = 3.5f;

/* --- what a loaded tool does ----------------------------------------------
   Resolved fresh from the tool and its installed modules rather than cached on
   the instance, so pulling a module out takes effect immediately and there is
   no second copy of the truth to keep in step. */
struct ToolShot {
    bool   canFire;
    int    delay;      /* frames between shots: tool base + module */
    int    power;
    int    damage;     /* see ItemDef::damage -- not derived from power */
    int    pierce;
    int    blast;
    int    energyCost;
    int    moduleSlot;
    int    life;
    int    bounces;
    u32    colour;
    /* Cells per frame at the muzzle, and how fast it falls. Resolved here
       rather than being a constant at the firing site so that two modules in
       the same tool can handle completely differently -- which, with one world
       gravity, is the only thing that makes a lobbed grenade and a flat beam
       different objects. See ItemDef::shotSpeed and ItemDef::shotBeam. */
    float  speed;
    float  gravity;
    float  homing;
    u8     effect;
    /* A MatId loaded in the tool's payload slot, or MAT_EMPTY. Read from the
       ToolInst directly (see ToolInst::payload) rather than being a module's
       own stat -- this is "whatever you loaded it with", not a fixed part of
       the shot. Count is NOT checked here; the caller decrements it and
       must confirm there was actually something to spend before firing. */
    u8     payloadMat;
};
ToolShot toolResolve(const ItemStack& st);
bool toolShotEnergyAvailable(const ItemStack& st, const ToolShot& shot);
void toolCommitShot(ItemStack& st, const ToolShot& shot, int cooldown);


/* Session-zero compatibility alias; storage lives in multiplayer.cpp. */
extern Inventory& g_inv;

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
    int power;          /* hardest material it bites; see ItemDef::minePower */
};

/* Hands. Slow and small on purpose: this is the baseline every tool is measured
   against, and if bare hands were comfortable no tool would feel like progress.
   12 cells every 6 frames is 120 cells a second -- a 7-wide tunnel advances
   about a body length every second, which is workable for getting somewhere and
   genuinely tiresome for undoing a mistake. That last part is the design: it is
   what makes a precision tool worth building rather than a luxury. */
extern const ToolSpec HAND;
/* What you are digging with right now: the held item's own numbers if it is a
   mining tool, otherwise HAND. Resolved on demand rather than cached, so
   swapping hotbar slots takes effect on the same frame. */
/* The best ordinary miner carried in the pack. Mining tools are capabilities,
   not weapons: once found, their digging rate is available without occupying a
   hotbar slot. Plant-only tools never displace a real miner. */
ToolSpec miningSpec(const Inventory& inv);

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
/* `power` is the hardest g_matStrength that may be bitten; anything above it is
   passed over WITHOUT spending a bite, the same way plantsOnly skips what did
   not grow. Defaulted to STR_ABSOLUTE so that every existing caller -- the
   sandbox brush, the miner device, a dozen harnesses -- keeps digging exactly
   what it always dug, and only callers that opt in are gated. */
/* --- the whitelist ---------------------------------------------------------
   An optional MAT_COUNT-long table of "may I take this". Null means take
   anything, which is what every existing caller wants and gets by saying
   nothing.

   This is the third skip in a row that works the same way -- plantsOnly, power,
   and now this -- and they share the property that makes them useful: a cell
   that fails the test is passed over WITHOUT spending a bite. That is what
   turns the filter from a restriction into a TOOL. Sweeping a brush over a
   half-smelted heap with only Copper ticked takes every copper cell in reach in
   one bite and leaves the ceramic standing, rather than spending the bite on
   the first ceramic cell it touches and doing nothing.

   A parameter rather than a global, even though exactly one caller in the game
   passes it: digInto is also the miner device's and a dozen harnesses' way of
   removing cells, and a hidden mode switch that silently changed what a MACHINE
   digs would be a bug nobody could see from the machine. */
int digInto(World& w, Inventory& inv, int cx, int cy, int r, int maxCells = 0,
            bool plantsOnly = false, int power = STR_ABSOLUTE,
            const bool* whitelist = 0);

/* Places the held stack into empty cells of a disc, one item per cell, until
   the stack runs out. Returns how many cells were filled. Never overwrites
   existing material and never places inside an occupied entity box. */
int placeFrom(World& w, Inventory& inv, int cx, int cy, int r, int maxCells = 0);
/* Replace occupied cells with the held material. The displaced drop is banked
   before the held stack is charged, and the cell budget/power are supplied by
   the active mining capability so overwrite cannot outpace digging. */
int overwriteFrom(World& w, Inventory& inv, int cx, int cy, int r, int maxCells, int power);

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
