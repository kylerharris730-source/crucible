#include "craft.h"

const char* const STATION_NAMES[STATION_COUNT] = {
    "Hand", "Bench", "Anvil", "Chemistry Bench", "Assembly Table", "Blast Furnace"
};

/* One cell of world is one unit of item (see MATERIAL_STACK), so a recipe's
   numbers are in CELLS. That is what makes these figures readable: four wood
   makes four platform because a platform is a cell of floor and a log is a cell
   of tree, and a player laying a walkway can count the cost by eye.

   The torch is the one that is not 1:1, and deliberately: one coal is worth
   several torches, which is what makes finding a coal seam feel like a supply
   rather than a consumable. */
const Recipe RECIPES[] = {
    /* Light, and the first reason to want coal for something other than heat. */
    { { { (ItemId)MAT_WOOD, 4 }, { (ItemId)MAT_COAL, 1 }, { ITEM_NONE, 0 } },
      ITEM_TORCH_DEV, 4, "4 Torches", STATION_HAND },

    /* Floor you can pass through. Cheap on purpose -- a platform is scaffolding
       and scaffolding you have to ration is scaffolding you do not build. */
    { { { (ItemId)MAT_WOOD, 1 }, { ITEM_NONE, 0 }, { ITEM_NONE, 0 } },
      (ItemId)MAT_PLATFORM, 2, "2 Platform", STATION_HAND },

    /* Rope, on the same reasoning and the same price. */
    { { { (ItemId)MAT_WOOD, 1 }, { ITEM_NONE, 0 }, { ITEM_NONE, 0 } },
      (ItemId)MAT_ROPE, 2, "2 Rope", STATION_HAND },

    /* Void is a convenience disposal block, not a progression material. Coal
       gives the otherwise mundane stone a small cost, while four blocks per
       craft makes sealing a bad pipe run or clearing a jam practical. */
    { { { (ItemId)MAT_STONE, 2 }, { (ItemId)MAT_COAL, 1 }, { ITEM_NONE, 0 } },
      (ItemId)MAT_VOID, 4, "4 Void", STATION_HAND },

    /* A door is a wall you built twice, so it costs more per cell than either
       of the above -- and you need a lot of cells to fill a doorway, which is
       what stops doors being the default way to close a room. */
    { { { (ItemId)MAT_WOOD, 2 }, { ITEM_NONE, 0 }, { ITEM_NONE, 0 } },
      (ItemId)MAT_DOOR, 1, "Door", STATION_HAND },

    /* A bed is early shelter: wood makes the frame and flax the bedding. It is
       hand-crafted because the benefit is preparing for the first night, not
       reaching the first station. */
    { { { (ItemId)MAT_WOOD, 6 }, { (ItemId)MAT_FLAX, 6 }, { ITEM_NONE, 0 } },
      ITEM_BED, 1, "Bed", STATION_HAND },

    /* --- birch --------------------------------------------------------------
       One recipe, not a second copy of all four. Birch logs convert to ordinary
       wood at one for one, and everything downstream is unchanged.

       Forking every wooden recipe per species was the alternative and it is
       worse in every direction: the crafting list doubles in length, each entry
       has a twin that differs by one word, and the player has to read both to
       find out they do the same thing. A conversion says the real relationship
       -- birch is wood you happen to have in a different colour -- in one row,
       and it leaves room for birch to be worth something specific later
       without unpicking anything. */
    { { { (ItemId)MAT_BIRCH_WOOD, 1 }, { ITEM_NONE, 0 }, { ITEM_NONE, 0 } },
      (ItemId)MAT_WOOD, 1, "Wood from Birch", STATION_HAND },

    /* --- sowing again ---------------------------------------------------
       A crop drops its head and nothing else, so replanting has to come from
       somewhere and this is it: part of the harvest goes back in the ground.

       Two seeds for one head, which is the number that decides whether farming
       is worth doing at all. At one for one a field is a treadmill -- you spend
       a harvest to plant the next one and end where you started. At two you
       double every season, so the first handful of seed is genuinely a
       beginning, and the surplus is what you actually keep. It also makes the
       decision legible every time you craft: half of this goes back.

       Three near-identical rows rather than one clever one, and deliberately.
       The alternative is a "seeds from any crop" recipe, which cannot exist
       here -- a recipe has one output, so it could only ever hand back one
       species' seed and two thirds of every harvest would come back wrong.
       That is the same reason each tree has its own pod. */
    { { { (ItemId)MAT_WHEAT, 1 }, { ITEM_NONE, 0 }, { ITEM_NONE, 0 } },
      (ItemId)MAT_WHEAT_SEED, 2, "2 Wheat Seed", STATION_HAND },
    { { { (ItemId)MAT_FLAX, 1 }, { ITEM_NONE, 0 }, { ITEM_NONE, 0 } },
      (ItemId)MAT_FLAX_SEED, 2, "2 Flax Seed", STATION_HAND },
    { { { (ItemId)MAT_COTTON, 1 }, { ITEM_NONE, 0 }, { ITEM_NONE, 0 } },
      (ItemId)MAT_COTTON_SEED, 2, "2 Cotton Seed", STATION_HAND },

    /* Grass had no way back into a bag at all -- cutting a lawn gives you
       grass (which dies back to dirt the moment it is buried, so banking it
       is pointless) rather than something you can sow elsewhere. One grass
       makes two seed, the same "half the harvest replants" ratio the three
       crops already use. */
    { { { (ItemId)MAT_GRASS, 1 }, { ITEM_NONE, 0 }, { ITEM_NONE, 0 } },
      ITEM_GRASS_SEED, 2, "2 Grass Seed", STATION_HAND },

    /* Wild wheat and flax now generate in meadows, so only cotton retains the
       grass conversion until its own biome/source exists. */
    { { { (ItemId)MAT_GRASS, 2 }, { ITEM_NONE, 0 }, { ITEM_NONE, 0 } },
      (ItemId)MAT_COTTON_SEED, 1, "Cotton Seed from Grass", STATION_HAND },

    /* --- the striker --------------------------------------------------------
       By HAND and out of nothing but stone, which is the point: it is the very
       first thing a player makes, before the bench, and it has to be craftable
       with what you can pick up in the opening thirty seconds. Everything
       thermal in this game is downstream of being able to light something. */
    { { { (ItemId)MAT_STONE, 4 }, { ITEM_NONE, 0 }, { ITEM_NONE, 0 } },
      ITEM_FLINT, 1, "Flint Striker", STATION_HAND },

    /* --- stations -------------------------------------------------------
       HAND -> BENCH -> {ANVIL, CHEM} -> ASSEMBLY. Each station is craftable
       at the tier below it, which is what makes the ladder a ladder rather
       than four unlocks that happen to exist: you build the bench with your
       hands, then build the anvil AT the bench because a woodworking surface
       is a reasonable place to fit metal parts together, and so on up. */
    /* Yields the ITEM, which places a 14x14 device, rather than the single cell
       of MAT_STATION_BENCH it used to. The material still exists and is still
       what the footprint is made of -- see ITEM_WORKBENCH. */
    { { { (ItemId)MAT_WOOD, 4 }, { (ItemId)MAT_STONE, 2 }, { ITEM_NONE, 0 } },
      ITEM_WORKBENCH, 1, "Workbench", STATION_HAND },
    { { { (ItemId)MAT_COPPER, 3 }, { (ItemId)MAT_WOOD, 2 }, { ITEM_NONE, 0 } },
      ITEM_ANVIL, 1, "Anvil", STATION_BENCH },
    { { { (ItemId)MAT_GLASS, 3 }, { (ItemId)MAT_STONE, 2 }, { ITEM_NONE, 0 } },
      ITEM_CHEMSTN, 1, "Chemistry Bench", STATION_BENCH },
    { { { (ItemId)MAT_GOLD, 2 }, { (ItemId)MAT_STEEL, 2 }, { (ItemId)MAT_GLASS, 1 } },
      ITEM_ASSEMBLY, 1, "Assembly Table", STATION_ANVIL },

    /* --- the blast furnace, and what it costs ------------------------------
       The layer 1 reward. One Forge Core plus a real quantity of the layer's
       own materials, built AT the anvil it supersedes -- so the upgrade reads
       as the next rung of the same ladder rather than a parallel one.

       Everything at the steel tier moved behind it: the Thermal Lance, both
       pieces of steel armour, and the Mk II jetpack. Note what did NOT move --
       SMELTING steel is still an iron+coal contact reaction and still happens
       in a vessel you built out of pixels. The forge gates turning a steel bar
       into an OBJECT, which is fabrication. See PROGRESSION.md section 2 for
       why the line is drawn exactly there. */
    { { { ITEM_FORGE_CORE, 1 }, { (ItemId)MAT_CERAMIC, 8 }, { (ItemId)MAT_IRON, 6 } },
      ITEM_FORGESTN, 1, "Blast Furnace", STATION_ANVIL },

    /* --- calling the boss ---------------------------------------------------
       Chitin off the things that live in layer 1, plus the layer's two metals.
       Assembled from what the place is MADE of, which is the point: you gather
       the summon incidentally, while doing something else, and the decision to
       fight her arrives gradually rather than as a menu entry.

       The Forge Core recipe that used to sit here is gone. It existed because
       the gate had shipped without its key -- the Blast Furnace needed a Core
       and there was no boss to drop one -- and it was labelled as the
       placeholder it was. The Brood Mother drops the Core now, so the only way
       to the steel tier is through her, which is what the reward was always
       meant to be. */
    { { { (ItemId)MAT_CHITIN, 12 }, { (ItemId)MAT_IRON, 6 }, { (ItemId)MAT_BRONZE, 4 } },
      ITEM_BROOD_CALL, 1, "Brood Call", STATION_ANVIL },

    /* --- the bench --------------------------------------------------------
       Wood, stone and the earliest metal: everything a workbench can put
       together with hand tools. This is also where the mining ladder's
       first rung and the multitool's first tier live, because both are
       meant to be the thing that gets you off bare hands quickly. */
    { { { (ItemId)MAT_COPPER, 4 }, { (ItemId)MAT_WOOD, 2 }, { ITEM_NONE, 0 } },
      ITEM_MULTITOOL, 1, "Multitool Mk I", STATION_BENCH },
    { { { (ItemId)MAT_COPPER, 2 }, { (ItemId)MAT_WOOD, 1 }, { ITEM_NONE, 0 } },
      ITEM_MOD_SHOT, 1, "Shot Module", STATION_BENCH },
    { { { (ItemId)MAT_COPPER, 2 }, { (ItemId)MAT_RUBBER, 1 }, { ITEM_NONE, 0 } },
      ITEM_MOD_BOUNCE, 1, "Bounce Module", STATION_BENCH },
    { { { (ItemId)MAT_COPPER, 3 }, { (ItemId)MAT_WOOD, 2 }, { ITEM_NONE, 0 } },
      ITEM_DRILL, 1, "Hand Drill", STATION_BENCH },
    /* The sickle deliberately does NOT want iron -- see ItemDef::minePlantsOnly
       in item.h: it is not a tier, it is a restriction, and gating a
       restriction behind a harder metal than the tool it is meant to replace
       would make careful harvesting the EXPENSIVE option. */
    { { { (ItemId)MAT_COPPER, 3 }, { (ItemId)MAT_WOOD, 2 }, { ITEM_NONE, 0 } },
      ITEM_SICKLE, 1, "Harvesting Sickle", STATION_BENCH },
    { { { (ItemId)MAT_COPPER, 2 }, { ITEM_NONE, 0 }, { ITEM_NONE, 0 } },
      ITEM_ITEM_PIPE, 3, "3 Item Pipe", STATION_BENCH },
    { { { (ItemId)MAT_COPPER, 3 }, { ITEM_NONE, 0 }, { ITEM_NONE, 0 } },
      ITEM_PIPE_CROSSOVER, 1, "Pipe Crossover", STATION_BENCH },
    { { { (ItemId)MAT_WOOD, 4 }, { (ItemId)MAT_IRON, 2 }, { ITEM_NONE, 0 } },
      ITEM_CHEST, 1, "Chest", STATION_BENCH },
    { { { (ItemId)MAT_COPPER, 2 }, { (ItemId)MAT_WOOD, 1 }, { ITEM_NONE, 0 } },
      ITEM_PULSE_BUTTON, 1, "Pulse Button", STATION_BENCH },

    /* --- rubber -----------------------------------------------------------
       Rubber had NO source at all: not generated anywhere in the world, not
       a phase change of anything, not craftable -- while six recipes needed
       it (rocket boots, all three jetpacks, and both suits). Every one of
       those was permanently uncraftable in a survival game, and the
       "every recipe can be made" check in build.cpp could not see it,
       because that test hands itself the ingredients rather than asking
       whether a player could ever hold them.

       Latex boiled out of timber and cured with coal, which is close enough
       to how rubber is actually got to be worth saying in one line. At the
       BENCH rather than the chemistry bench on purpose: rocket boots and the
       Mk I jetpack are anvil-tier, and the chemistry bench needs glass, so
       putting rubber behind glass would gate the early flight items on a
       material two tiers above them. */
    { { { (ItemId)MAT_WOOD, 3 }, { (ItemId)MAT_COAL, 1 }, { ITEM_NONE, 0 } },
      (ItemId)MAT_RUBBER, 2, "2 Rubber", STATION_BENCH },
    /* Filters are deliberate plumbing pieces: the coarse mesh catches a
       powder while letting fluid through; the finer one admits gas only. */
    { { { (ItemId)MAT_WOOD, 2 }, { (ItemId)MAT_IRON, 1 }, { ITEM_NONE, 0 } },
      (ItemId)MAT_SIEVE, 4, "4 Sieve", STATION_BENCH },
    { { { (ItemId)MAT_COPPER, 2 }, { (ItemId)MAT_GLASS, 1 }, { ITEM_NONE, 0 } },
      (ItemId)MAT_GAS_SIEVE, 4, "4 Gas Sieve", STATION_BENCH },

    /* --- bread ------------------------------------------------------------
       What wheat is FOR, and the first thing in this game that acts on the
       CHARACTER rather than on the world. Four wheat a loaf: a planted row
       yields several harvests, so a field is a few loaves rather than one, and
       food is something you keep a stock of rather than something you make when
       you are already dying. */
    { { { (ItemId)MAT_WHEAT, 4 }, { ITEM_NONE, 0 }, { ITEM_NONE, 0 } },
      ITEM_BREAD, 1, "Bread", STATION_BENCH },

    /* --- the anvil, bronze tier ---------------------------------------------
       The mining ladder's second rung, and the first machines: sensing and
       automation, which is what a placer/miner/clock/thermocouple pair is
       for. All of them want iron rather than bronze specifically -- they are
       precision parts, and bronze's whole pitch is toughness and heat, not
       fine work. */
    { { { (ItemId)MAT_BRONZE, 4 }, { (ItemId)MAT_WOOD, 2 }, { ITEM_NONE, 0 } },
      ITEM_AUGER, 1, "Rock Auger", STATION_ANVIL },
    { { { (ItemId)MAT_IRON, 3 }, { (ItemId)MAT_COAL, 1 }, { ITEM_NONE, 0 } },
      ITEM_MOD_BLAST, 1, "Blast Module", STATION_ANVIL },
    { { { (ItemId)MAT_COPPER, 2 }, { (ItemId)MAT_IRON, 1 }, { ITEM_NONE, 0 } },
      ITEM_THERMOCOUPLE, 1, "Thermocouple", STATION_ANVIL },
    /* Copper alone, no iron. The clock is the thing that makes electricity DO
       anything -- without a pulse on a period, a circuit is a wire you poke by
       hand -- so it sits at the point where a player is first curious about
       wiring, which is when they have copper and have not yet smelted iron.
       Charging iron for it put the entire automation branch behind the metal
       ladder's second rung for no reason anyone could feel. */
    { { { (ItemId)MAT_COPPER, 3 }, { ITEM_NONE, 0 }, { ITEM_NONE, 0 } },
      ITEM_CLOCK, 1, "Clock", STATION_ANVIL },
    { { { (ItemId)MAT_IRON, 3 }, { (ItemId)MAT_COPPER, 1 }, { ITEM_NONE, 0 } },
      ITEM_PLACER, 1, "Placer", STATION_ANVIL },
    { { { (ItemId)MAT_IRON, 3 }, { (ItemId)MAT_COPPER, 1 }, { ITEM_NONE, 0 } },
      ITEM_MINER, 1, "Miner", STATION_ANVIL },
    /* The first minions are deliberately iron-tier: they are useful companions,
       not an endgame automation prize. The light is cheap utility; the attack
       frame costs extra copper because it is a weapon with its own firing rig. */
    { { { (ItemId)MAT_IRON, 4 }, { (ItemId)MAT_GLASS, 1 }, { ITEM_NONE, 0 } },
      ITEM_LIGHT_DRONE, 1, "Light Drone", STATION_ANVIL },
    { { { (ItemId)MAT_IRON, 5 }, { (ItemId)MAT_COPPER, 3 }, { ITEM_NONE, 0 } },
      ITEM_ATTACK_DRONE, 1, "Attack Drone", STATION_ANVIL },
    { { { (ItemId)MAT_IRON, 3 }, { (ItemId)MAT_COPPER, 2 }, { ITEM_NONE, 0 } },
      ITEM_PICKUP_DRONE, 1, "Pickup Drone", STATION_ANVIL },
    { { { (ItemId)MAT_IRON, 5 }, { (ItemId)MAT_GLASS, 2 }, { ITEM_NONE, 0 } },
      ITEM_SHIELD_DRONE, 1, "Shield Drone", STATION_ANVIL },
    /* Placeholder early recipes. These can later become layer-2 drops without
       changing the set mechanics or invalidating crafted pieces in saves. */
    { { { (ItemId)MAT_IRON, 4 }, { (ItemId)MAT_COPPER, 2 }, { ITEM_NONE, 0 } },
      ITEM_DRONE_VISOR, 1, "Drone Visor", STATION_ANVIL },
    { { { (ItemId)MAT_IRON, 6 }, { (ItemId)MAT_COPPER, 3 }, { ITEM_NONE, 0 } },
      ITEM_DRONE_HARNESS, 1, "Drone Harness", STATION_ANVIL },
    { { { (ItemId)MAT_IRON, 4 }, { (ItemId)MAT_RUBBER, 2 }, { ITEM_NONE, 0 } },
      ITEM_DRONE_GREAVES, 1, "Drone Greaves", STATION_ANVIL },
    { { { (ItemId)MAT_COPPER, 4 }, { (ItemId)MAT_GLASS, 2 }, { ITEM_NONE, 0 } },
      ITEM_DRONE_BEACON, 1, "Drone Beacon", STATION_ANVIL },
    /* The weapon chassis, and every one of them costs CHITIN on top of metal.
       That is the recipe saying what the item is: chitin comes off the things
       you fight, so an autonomous weapon is paid for out of the fighting it is
       going to do more of, and the first one is a decision to commit to combat
       rather than a slot you fill because it was free.

       Priced against each other by how much of the fight they take over. The
       lance is the cheapest and asks the most of the player; the mortar is the
       dearest because "hits things behind cover" is a capability nothing else
       in the game has at any price. */
    { { { (ItemId)MAT_IRON, 4 }, { (ItemId)MAT_CHITIN, 6 }, { ITEM_NONE, 0 } },
      ITEM_LANCE_DRONE, 1, "Lance Drone", STATION_ANVIL },
    { { { (ItemId)MAT_IRON, 6 }, { (ItemId)MAT_CHITIN, 10 }, { (ItemId)MAT_COAL, 4 } },
      ITEM_MORTAR_DRONE, 1, "Mortar Drone", STATION_ANVIL },
    { { { (ItemId)MAT_IRON, 5 }, { (ItemId)MAT_CHITIN, 8 }, { ITEM_NONE, 0 } },
      ITEM_ORBIT_DRONE, 1, "Orbit Drone", STATION_ANVIL },
    /* Stone and glass: a plinth and a case. Cheap on purpose -- a pedestal is
       how you SHOW something, and a display case you have to save up for is a
       display case nobody builds. */
    { { { (ItemId)MAT_STONE, 12 }, { (ItemId)MAT_GLASS, 2 }, { ITEM_NONE, 0 } },
      ITEM_PEDESTAL, 1, "Pedestal", STATION_BENCH },
    { { { (ItemId)MAT_COPPER, 3 }, { (ItemId)MAT_RUBBER, 1 }, { ITEM_NONE, 0 } },
      ITEM_OVERCLOCK_CHIP, 1, "Overclock Chip", STATION_ANVIL },
    { { { (ItemId)MAT_COPPER, 4 }, { (ItemId)MAT_GLASS, 1 }, { ITEM_NONE, 0 } },
      ITEM_TWIN_CONTROLLER, 1, "Twin Controller", STATION_ANVIL },
    { { { (ItemId)MAT_CHITIN, 4 }, { (ItemId)MAT_COPPER, 2 }, { ITEM_NONE, 0 } },
      ITEM_GARLIC_FIELD_CHIP, 1, "Garlic Field Chip", STATION_ANVIL },
    /* The player wears accessories; drones socket chips. Building the player
       version from its matching chip makes the relationship obvious while the
       extra casing is the cost of making it survive on a moving body. */
    { { { ITEM_GARLIC_FIELD_CHIP, 1 }, { (ItemId)MAT_CHITIN, 4 }, { ITEM_NONE, 0 } },
      ITEM_GARLIC_ACCESSORY, 1, "Garlic Accessory", STATION_ANVIL },
    { { { ITEM_OVERCLOCK_CHIP, 1 }, { (ItemId)MAT_RUBBER, 2 }, { ITEM_NONE, 0 } },
      ITEM_OVERLOAD_ACCESSORY, 1, "Overload Accessory", STATION_ANVIL },
    { { { ITEM_TWIN_CONTROLLER, 1 }, { (ItemId)MAT_GLASS, 2 }, { ITEM_NONE, 0 } },
      ITEM_TWIN_ACCESSORY, 1, "Twin Accessory", STATION_ANVIL },
    { { { (ItemId)MAT_GLOWFLUID, 2 }, { (ItemId)MAT_GLASS, 1 }, { ITEM_NONE, 0 } },
      ITEM_GLOW_FLARE, 1, "Glowflare", STATION_CHEM },
    { { { (ItemId)MAT_COPPER, 2 }, { (ItemId)MAT_IRON, 1 }, { ITEM_NONE, 0 } },
      ITEM_SPOUT, 1, "Spout", STATION_ANVIL },
    { { { (ItemId)MAT_COPPER, 2 }, { (ItemId)MAT_IRON, 1 }, { ITEM_NONE, 0 } },
      ITEM_DRAIN, 1, "Drain", STATION_ANVIL },
    { { { (ItemId)MAT_COPPER, 2 }, { (ItemId)MAT_IRON, 1 }, { ITEM_NONE, 0 } },
      ITEM_BLOCK_WATCHER, 1, "Block Watcher", STATION_ANVIL },
    { { { (ItemId)MAT_IRON, 3 }, { (ItemId)MAT_RUBBER, 2 }, { ITEM_NONE, 0 } },
      ITEM_ROCKET_BOOTS, 1, "Rocket Boots", STATION_ANVIL },
    { { { (ItemId)MAT_BRONZE, 3 }, { (ItemId)MAT_GOLD, 1 }, { ITEM_NONE, 0 } },
      ITEM_HERMES, 1, "Hermes Boots", STATION_ANVIL },
    { { { (ItemId)MAT_IRON, 4 }, { (ItemId)MAT_RUBBER, 2 }, { (ItemId)MAT_FUEL, 1 } },
      ITEM_JETPACK1, 1, "Jetpack Mk I", STATION_ANVIL },

    /* --- the anvil, steel tier -----------------------------------------
       Steel exists, so the lance -- the mining tool one rung above the
       auger -- and the jetpack's second tier both ask for it rather than
       for iron a second time. */
    { { { (ItemId)MAT_STEEL, 4 }, { (ItemId)MAT_COPPER, 2 }, { ITEM_NONE, 0 } },
      ITEM_LANCE, 1, "Thermal Lance", STATION_FORGE },
    { { { (ItemId)MAT_STEEL, 4 }, { (ItemId)MAT_RUBBER, 2 }, { (ItemId)MAT_FUEL, 2 } },
      ITEM_JETPACK2, 1, "Jetpack Mk II", STATION_FORGE },
    { { { (ItemId)MAT_STEEL, 4 }, { ITEM_NONE, 0 }, { ITEM_NONE, 0 } },
      ITEM_STEEL_HELMET, 1, "Steel Helmet", STATION_FORGE },
    { { { (ItemId)MAT_STEEL, 8 }, { (ItemId)MAT_RUBBER, 2 }, { ITEM_NONE, 0 } },
      ITEM_STEEL_SUIT, 1, "Steel Suit", STATION_FORGE },

    /* --- the melee ladder -----------------------------------------------
       Metal and wood, and nothing else. Every other recipe in this file mixes
       two or three materials because the thing it makes is a MACHINE with parts;
       a sword is a piece of metal on a stick, and pricing it like a jetpack
       would be the recipe arguing with the object.

       The costs climb with the tier and the spear is always cheaper than the
       sword of the same metal -- it is mostly shaft. That ordering matters more
       than the absolute numbers: it means the spear is what you make FIRST at
       each new metal, which is also the weapon that keeps you at arm's length,
       so the cheap option is the safe one rather than the trap.

       Split across stations by the metal, matching where that metal is already
       worked everywhere else in this table: copper and bronze at the anvil,
       iron and gold at the anvil, and the three that need real heat at the
       forge. Nothing here invents a new gate -- if you can smelt the metal you
       can make the weapon. */
    { { { (ItemId)MAT_COPPER, 6 }, { (ItemId)MAT_WOOD, 2 }, { ITEM_NONE, 0 } },
      ITEM_SWORD_COPPER, 1, "Copper Sword", STATION_ANVIL },
    { { { (ItemId)MAT_COPPER, 3 }, { (ItemId)MAT_WOOD, 4 }, { ITEM_NONE, 0 } },
      ITEM_SPEAR_COPPER, 1, "Copper Spear", STATION_ANVIL },
    { { { (ItemId)MAT_BRONZE, 6 }, { (ItemId)MAT_WOOD, 2 }, { ITEM_NONE, 0 } },
      ITEM_SWORD_BRONZE, 1, "Bronze Sword", STATION_ANVIL },
    { { { (ItemId)MAT_BRONZE, 3 }, { (ItemId)MAT_WOOD, 4 }, { ITEM_NONE, 0 } },
      ITEM_SPEAR_BRONZE, 1, "Bronze Spear", STATION_ANVIL },
    { { { (ItemId)MAT_IRON, 8 }, { (ItemId)MAT_WOOD, 2 }, { ITEM_NONE, 0 } },
      ITEM_SWORD_IRON, 1, "Iron Sword", STATION_ANVIL },
    { { { (ItemId)MAT_IRON, 4 }, { (ItemId)MAT_WOOD, 4 }, { ITEM_NONE, 0 } },
      ITEM_SPEAR_IRON, 1, "Iron Spear", STATION_ANVIL },
    /* Gold is scarce -- small pockets rather than veins, see MAT_GOLD -- so its
       weapons cost less metal than iron's despite sitting above them. The
       rarity is the price. */
    { { { (ItemId)MAT_GOLD, 5 }, { (ItemId)MAT_WOOD, 2 }, { ITEM_NONE, 0 } },
      ITEM_SWORD_GOLD, 1, "Gold Sword", STATION_ANVIL },
    { { { (ItemId)MAT_GOLD, 3 }, { (ItemId)MAT_WOOD, 4 }, { ITEM_NONE, 0 } },
      ITEM_SPEAR_GOLD, 1, "Gold Spear", STATION_ANVIL },
    { { { (ItemId)MAT_STEEL, 8 }, { (ItemId)MAT_WOOD, 2 }, { ITEM_NONE, 0 } },
      ITEM_SWORD_STEEL, 1, "Steel Sword", STATION_FORGE },
    { { { (ItemId)MAT_STEEL, 4 }, { (ItemId)MAT_WOOD, 4 }, { ITEM_NONE, 0 } },
      ITEM_SPEAR_STEEL, 1, "Steel Spear", STATION_FORGE },
    { { { (ItemId)MAT_TITANIUM, 8 }, { (ItemId)MAT_WOOD, 2 }, { ITEM_NONE, 0 } },
      ITEM_SWORD_TITANIUM, 1, "Titanium Sword", STATION_FORGE },
    { { { (ItemId)MAT_TITANIUM, 4 }, { (ItemId)MAT_WOOD, 4 }, { ITEM_NONE, 0 } },
      ITEM_SPEAR_TITANIUM, 1, "Titanium Spear", STATION_FORGE },
    { { { (ItemId)MAT_TUNGSTEN, 10 }, { (ItemId)MAT_WOOD, 2 }, { ITEM_NONE, 0 } },
      ITEM_SWORD_TUNGSTEN, 1, "Tungsten Sword", STATION_FORGE },
    { { { (ItemId)MAT_TUNGSTEN, 5 }, { (ItemId)MAT_WOOD, 4 }, { ITEM_NONE, 0 } },
      ITEM_SPEAR_TUNGSTEN, 1, "Tungsten Spear", STATION_FORGE },
    { { { (ItemId)MAT_VOID, 3 }, { (ItemId)MAT_TUNGSTEN, 2 }, { (ItemId)MAT_GOLD, 2 } },
      ITEM_MOD_TELEPORT, 1, "Teleport Module", STATION_FORGE },

    /* --- the chemistry bench -----------------------------------------------
       Glass work: optics, a bulb, and the two materials this whole plan
       exists to unlock a source for. Refractory earns its "fabricated, not
       melted" framing here literally -- see the note on MAT_REFRACTORY in
       materials.h. */
    { { { (ItemId)MAT_GLASS, 2 }, { (ItemId)MAT_COPPER, 1 }, { ITEM_NONE, 0 } },
      ITEM_LENS, 1, "Focusing Lens", STATION_CHEM },
    { { { (ItemId)MAT_GLASS, 2 }, { (ItemId)MAT_COPPER, 1 }, { ITEM_NONE, 0 } },
      (ItemId)MAT_LAMP, 2, "2 Lamp", STATION_CHEM },
    { { { (ItemId)MAT_CERAMIC, 2 }, { (ItemId)MAT_TIN, 1 }, { ITEM_NONE, 0 } },
      (ItemId)MAT_ALUMINUM_NITRIDE, 1, "Aluminum Nitride", STATION_CHEM },
    { { { (ItemId)MAT_CERAMIC, 2 }, { (ItemId)MAT_GRAPHENE, 1 }, { ITEM_NONE, 0 } },
      (ItemId)MAT_REFRACTORY, 1, "Refractory Lining", STATION_CHEM },

    /* --- the assembly table -------------------------------------------
       Gold and precision: the second multitool tier, reach, the signal
       hardware, and the last two jetpacks. Titanium is what makes the
       Mk III and the disruptor the true top of their ladders. */
    { { { (ItemId)MAT_STEEL, 4 }, { (ItemId)MAT_GOLD, 2 }, { ITEM_NONE, 0 } },
      ITEM_MULTITOOL2, 1, "Multitool Mk II", STATION_ASSEMBLY },
    { { { (ItemId)MAT_GOLD, 2 }, { (ItemId)MAT_CHITIN, 4 }, { (ItemId)MAT_GLASS, 1 } },
      ITEM_MOD_HOMING, 1, "Homing Module", STATION_ASSEMBLY },
    { { { (ItemId)MAT_TITANIUM, 3 }, { (ItemId)MAT_GOLD, 2 }, { ITEM_NONE, 0 } },
      ITEM_DISRUPTOR, 1, "Disruptor", STATION_ASSEMBLY },
    { { { (ItemId)MAT_GLASS, 2 }, { (ItemId)MAT_GOLD, 2 }, { ITEM_NONE, 0 } },
      ITEM_RELAY, 1, "Field Relay", STATION_ASSEMBLY },
    { { { (ItemId)MAT_TITANIUM, 4 }, { (ItemId)MAT_RUBBER, 3 }, { (ItemId)MAT_FUEL, 3 } },
      ITEM_JETPACK3, 1, "Jetpack Mk III", STATION_ASSEMBLY },
    { { { (ItemId)MAT_TITANIUM, 4 }, { (ItemId)MAT_GOLD, 1 }, { ITEM_NONE, 0 } },
      ITEM_TITANIUM_HELMET, 1, "Titanium Helmet", STATION_ASSEMBLY },
    { { { (ItemId)MAT_TITANIUM, 8 }, { (ItemId)MAT_RUBBER, 2 }, { (ItemId)MAT_GOLD, 2 } },
      ITEM_TITANIUM_SUIT, 1, "Titanium Suit", STATION_ASSEMBLY },
    { { { (ItemId)MAT_COPPER, 2 }, { (ItemId)MAT_GOLD, 1 }, { ITEM_NONE, 0 } },
      ITEM_CONSTANT_COMBINATOR, 1, "Constant Combinator", STATION_ASSEMBLY },
    { { { (ItemId)MAT_COPPER, 2 }, { (ItemId)MAT_GOLD, 2 }, { ITEM_NONE, 0 } },
      ITEM_ARITHMETIC_COMBINATOR, 1, "Arithmetic Combinator", STATION_ASSEMBLY },
    { { { (ItemId)MAT_COPPER, 2 }, { (ItemId)MAT_GOLD, 2 }, { ITEM_NONE, 0 } },
      ITEM_DECIDER_COMBINATOR, 1, "Decider Combinator", STATION_ASSEMBLY },
};

const int N_RECIPES = (int)(sizeof(RECIPES) / sizeof(RECIPES[0]));

/* How far from the player a placed station still counts. Bigger than
   PLAYER_REACH on purpose -- a bench is furniture you stand NEAR, not a
   block you are mid-swing at, and a radius tied to reach would make
   stepping back to admire your workshop lock you out of your own anvil. */
static const int STATION_RANGE = 10;

/* True for every station tier currently within STATION_RANGE, plus
   STATION_HAND which is always true since it means "no station needed".
   Filled by craftScanStations(), read by craftCan().

   Rescanned unconditionally on every call rather than cached against the
   player's last position -- a position cache was the first version of
   this, and it was wrong in a way only a placed-while-standing-still
   station reveals: build a bench and reach for the anvil recipe WITHOUT
   moving, and a cache keyed on position alone still reports the world as
   it was before the bench existed. The world can change under a player who
   has not, so position was never the right key. What actually bounds the
   cost is that this runs once a frame while the menu is open (see the call
   in layoutCraft()), not once per recipe -- a few hundred cell reads a
   frame is nothing, and it is what the caching comment in craft.h always
   meant by "once a frame", not "once per player step". */
static bool g_nearStation[STATION_COUNT];

void craftScanStations(const World& w, const Player& p) {
    for (int s = 0; s < STATION_COUNT; ++s) g_nearStation[s] = (s == STATION_HAND);
    const int x0 = imax(0, p.left()   - STATION_RANGE);
    const int x1 = imin(SIM_W - 1, p.right()  + STATION_RANGE);
    const int y0 = imax(0, p.top()    - STATION_RANGE);
    const int y1 = imin(SIM_H - 1, p.bottom() + STATION_RANGE);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const u8 s = g_matStation[w.at(x, y).mat];
            if (s) g_nearStation[s] = true;
        }
}

bool craftHasStation(int r) {
    if (r < 0 || r >= N_RECIPES) return false;
    const u8 st = RECIPES[r].station;
    /* STATION_HAND never consults the cache -- see the note in craftCan. */
    if (st == STATION_HAND) return true;
    return st < STATION_COUNT && g_nearStation[st];
}

bool craftCan(const Inventory& inv, int r) {
    if (r < 0 || r >= N_RECIPES) return false;
    const Recipe& rc = RECIPES[r];
    /* STATION_HAND never consults the cache at all, rather than relying on
       craftScanStations() having populated g_nearStation[STATION_HAND] as
       true. That distinction is not pedantry: g_nearStation is a static
       array with no constructor, so a caller that never scans -- every
       headless test, and any tool that links craft.cpp without the game
       loop around it -- finds it zero-initialised, and a hand recipe would
       silently fail alongside every gated one. "Craftable anywhere" should
       not be able to depend on a scan having run first. */
    if (rc.station != STATION_HAND
        && (rc.station >= STATION_COUNT || !g_nearStation[rc.station])) return false;
    for (int i = 0; i < CRAFT_MAX_IN; ++i) {
        if (rc.in[i].item == ITEM_NONE || rc.in[i].count <= 0) continue;
        if (inv.countOf(rc.in[i].item) < rc.in[i].count) return false;
    }
    return true;
}

bool craftMake(Inventory& inv, int r) {
    if (!craftCan(inv, r)) return false;
    const Recipe& rc = RECIPES[r];

    /* Room for the result BEFORE anything is spent. Checked by actually adding
       it and putting it back if it did not fit, rather than by predicting --
       "will this fit" duplicates the stacking rules, and a duplicate of those
       rules is a duplicate that can disagree with them. */
    const int left = inv.add(rc.out, rc.outCount);
    if (left != 0) {
        /* Undo the part that did land. Cannot fail: it was just added. */
        inv.take(rc.out, rc.outCount - left);
        return false;
    }

    for (int i = 0; i < CRAFT_MAX_IN; ++i) {
        if (rc.in[i].item == ITEM_NONE || rc.in[i].count <= 0) continue;
        inv.take(rc.in[i].item, rc.in[i].count);
    }
    return true;
}
