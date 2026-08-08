#pragma once
#include "common.h"

/* --- how temperature is stored -------------------------------------------
   A cell's temperature is one byte holding **degrees Celsius plus
   TEMP_OFFSET**, so the scale runs -40 C .. +215 C instead of 0 .. 255.

   The offset exists because ice does. With 0 meaning 0 C there was nowhere to
   put anything frozen, and the heat view had no cold half to colour. Paying
   for that in headroom is unavoidable: the byte was already full at the top
   (lava sat at exactly 255), so every degree of cold has to come off the hot
   end. 40 buys a usable frozen range while costing only the three hottest
   values, which were the only ones with slack -- see materials.cpp.

   Write temperatures as degC(100), never as a bare 140. Everything the sim
   compares -- thresholds, ambient, setpoints -- is in stored units, so the
   raw numbers are meaningless on their own and shift if the offset ever
   changes again.

   One sharp edge: 0 is the "feature disabled" sentinel in every temperature
   column of MATS[], and degC(-40) is also 0. A material therefore cannot have
   a threshold at exactly -40 C. Nothing wants one, and the cooler reaches it
   by being pinned rather than by a table entry. */
static const int TEMP_OFFSET = 40;
static constexpr u8 degC(int c) { return (u8)(c + TEMP_OFFSET); }
static const int TEMP_MIN_C = -TEMP_OFFSET;        /* -40 */
static const int TEMP_MAX_C = 255 - TEMP_OFFSET;   /* +215 */

/* Adding a material:
     1. add an id here, above MAT_COUNT
     2. add a row to MATS[] in materials.cpp (rows must stay in id order)
     3. only if it needs behaviour no existing kind covers, add a rule in world.cpp
   Most materials need steps 1 and 2 only. Phase changes -- boiling, melting,
   freezing, burning, quenching -- are all table-driven, so "melts into lava
   when hot enough" or "catches fire" costs nothing but a row. */
enum MatId {
    MAT_EMPTY = 0,
    MAT_WALL,        /* the indestructible border, and a placeable block */
    MAT_STONE,       /* melts into lava when hot enough */
    MAT_SAND,
    MAT_DIRT,
    MAT_GRASS,       /* dirt with something living on it: spreads across exposed
                        dirt, and dies back to dirt when buried */
    MAT_WATER,
    MAT_ICE,         /* frozen water; melts back above freezing */
    MAT_STEAM,
    MAT_FIRE,
    MAT_PLASMA,      /* fire's hotter sibling: holds its heat instead of
                        spending it, and water cannot put it out */
    MAT_COLDFIRE,    /* fire's mirror image: rises and decays like a flame, but
                        chills everything it touches instead of burning it */
    MAT_NITROGEN,    /* liquid nitrogen; boils away into cold fire */
    MAT_MERCURY,     /* the only liquid metal -- boils to vapour, freezes solid */
    MAT_MERCURY_GAS, /* mercury vapour; condenses back to mercury as it cools */
    MAT_MERCURY_ICE, /* frozen mercury; melts back into mercury */
    MAT_IRON,        /* static, extremely heat-conductive -- but it melts */
    MAT_IRON_MELT,   /* molten iron; freezes back to iron as it cools */
    MAT_COPPER,      /* better than iron -- carries heat further per frame */
    MAT_COPPER_MELT, /* molten copper; freezes back to copper */
    MAT_GRAPHENE,    /* near-instant along a sheet, and the only conductor with
                        no melting point at all */
    MAT_LAVA,        /* molten stone; freezes back to stone as it cools */
    MAT_WOOD,        /* catches fire and is consumed */
    MAT_RUBBER,      /* the best insulator there is, until it melts */
    MAT_RUBBER_MELT, /* molten rubber; sets again as it cools */
    MAT_CLONE,       /* copies the first material it touches, forever */
    MAT_VOID,        /* destroys whatever it touches */
    MAT_HEATER,      /* holds itself at max temperature, forever */
    MAT_COOLER,      /* holds itself at min temperature, forever */
    MAT_LAMP,        /* the only light source you can build: bright, cold, and
                        it stays where you put it */
    MAT_TORCH,       /* the cheap light: dimmer than a lamp, and the only solid
                        in the table you can WALK THROUGH */
    /* --- ores and what comes out of them ---------------------------------
       Ore does not become metal. It becomes a MIXTURE of molten metal and
       molten slag, and getting the metal out is the player's problem. See
       g_matSmeltYield below. */
    MAT_COPPER_ORE,  /* the early ore: smelts cooler and yields more */
    MAT_IRON_ORE,    /* the later one: hotter, meaner, more slag */
    MAT_SLAG_MELT,   /* molten slag. LIGHTER than either molten metal, which is
                        the entire separation mechanism -- it floats */
    MAT_SLAG,        /* frozen slag: the crust you have to break to get at what
                        settled underneath it */
    /* --- the heat ladder --------------------------------------------------
       What you burn, and what you build the thing you burn it in out of. See the
       note on g_matWetInto below for how coal becomes fuel, and the ember/fuelfire
       rows in materials.cpp for why a hot flame is not the same as a hot fire. */
    MAT_CLAY,        /* fires into ceramic at a temperature a wood fire can reach */
    MAT_CERAMIC,     /* the early-game furnace lining: insulates, and never melts */
    MAT_COAL,        /* burns long and hot -- but not hot enough for iron */
    MAT_EMBER,       /* burning coal: static, enormous heat capacity, slow to spend */
    MAT_FUEL,        /* coal slaked in water. The step that unlocks metal */
    MAT_FUELFIRE,    /* burning fuel: hot enough to melt what coal cannot */
    /* --- the door --------------------------------------------------------
       Two materials rather than one with a flag, because there is nowhere to
       keep the flag. A Cell is mat, moisture, tint and flags, and every byte
       already means something -- the same wall the devices ran into. Open and
       closed are the whole of a door's state, so spending an id on each buys
       the entire mechanism for nothing but a table row.

       They differ in exactly three ways, and each one is deliberate:

         PASSABLE. The open one is in g_matPassable, the closed one is not. That
         is the door.

         OPAQUE. A closed door is dark behind, an open one is not, so light
         through a doorway comes out of the existing light model rather than
         needing a case.

         What they are NOT is a difference in KIND. Both are KIND_STATIC, so
         both are solid to the simulation and both seal a room. An open door
         holds back sand and still lets you walk through, which is precisely the
         material room.h was waiting for and the reason a room with its door
         open is still your room.

       Only the closed one is an item. See g_matDropsAs: mining an open door
       gives you a door, because "open door" is a state a door is in, not a
       thing you own. */
    MAT_DOOR,
    MAT_DOOR_OPEN,
    /* --- the two ways of getting about ------------------------------------
       Rope you climb, platform you stand on. Both are solid to the SIMULATION
       and not to you, which is the trick MAT_TORCH established and the reason
       g_matPassable is a table rather than a MatKind: sand piles on a platform
       and water pools on it, while you walk through the side of it.

       They are the movement half of what doors started. A door is a hole in a
       wall you control; these are a hole in the FLOOR and a hole in the
       ceiling, and between the three of them a base becomes something you lay
       out rather than something you tunnel. */
    MAT_ROPE,
    MAT_PLATFORM,
    /* --- the tree ---------------------------------------------------------
       A seed you drop, what it becomes, and what a grown tree is made of.
       MAT_WOOD already existed and is the trunk, so nothing new is needed for
       the part you actually want.

       The pod is the renewable half: it is a cell of the canopy that drops a
       SEED rather than itself (see g_matDropsAs), so every tree you fell pays
       for the next two or three. That is a table entry rather than a rule. */
    MAT_OAK_SEED,
    MAT_OAK_SAPLING,
    MAT_OAK_LEAF,
    MAT_OAK_POD,
    /* --- the second species ----------------------------------------------
       Birch: shorter, paler, slighter. It has its OWN seed, sapling, leaf,
       pod and wood rather than sharing oak's, and the pod is the reason that
       is not fussiness -- g_matDropsAs maps one material to one item, so a
       shared pod could only ever drop one species' seed and half of every
       harvest would come back as the wrong tree.

       Its wood is a separate material too, so a birch log reads as birch in
       the pack. See the note on the recipes in craft.cpp for why that does not
       fork every wooden recipe in two. */
    MAT_BIRCH_SEED,
    MAT_BIRCH_SAPLING,
    MAT_BIRCH_WOOD,
    MAT_BIRCH_LEAF,
    MAT_BIRCH_POD,
    /* --- crops -------------------------------------------------------------
       Three plants you sow, and only FIVE materials between them, which is the
       whole reason they are listed together.

       A crop is a stalk with something edible or spinnable in it, and the stalk
       is the same stalk whichever crop it belongs to -- so wheat and flax share
       one green stem and cotton gets a brown one because a cotton plant does
       not look like a cereal. Splitting the stem per species would be three
       materials that differ in nothing, and the reason trees DO split theirs is
       specific and does not apply here: an oak's leaf and a birch's leaf have
       to differ because each carries a pod that must drop its own species'
       seed, and a shared one could only drop one of them. Nothing drops off a
       stalk at all.

       That is the other half of it. A stalk drops NOTHING when you cut it --
       see g_matDropsAs -- so a harvested field leaves you the grain and not a
       pile of straw you have to throw away. What you take off the plant is the
       head: wheat, flax, cotton, each dropping itself.

       Growing them is the tree machinery with small numbers in it, because a
       wheat plant genuinely is a short thin trunk with a tuft on top and grain
       in the tuft. See TREE_KINDS in tree.h. */
    MAT_STALK,        /* green: wheat and flax */
    MAT_STALK_DRY,    /* brown: cotton */
    MAT_WHEAT_SEED,
    MAT_WHEAT,
    MAT_FLAX_SEED,
    MAT_FLAX,
    MAT_COTTON_SEED,
    MAT_COTTON,
    /* --- machinery -------------------------------------------------------
       The cells a multi-cell DEVICE occupies. One material for every device
       type, because the grid only needs to know "something solid and man-made is
       here"; which device it is, and all of its state, live in the device list
       (see device.h). Putting a material per type in here would mean the sim
       learning about machines it has no business knowing about, and there is
       nowhere in a 4-byte Cell to keep a setpoint anyway. */
    MAT_DEVICE,
    /* Appended rather than inserted among the heat materials so existing save
       files keep every older numeric material id. AlN is a ceramic thermal bus:
       excellent for moving heat, deliberately useless for wiring. */
    MAT_ALUMINUM_NITRIDE,

    /* --- crafting stations -------------------------------------------------
       Placed material, not a device: a station has no state, no update tick,
       no signals, no facing -- it only ever has to answer "is one of these
       within reach", which is a world-cell scan, not an entity. See
       g_matStation and craft.h's CraftStation. Ordered HAND-up-to-ASSEMBLY so
       the enum value already IS the tier for anything that wants to compare. */
    MAT_STATION_BENCH,
    MAT_STATION_ANVIL,
    MAT_STATION_CHEM,
    MAT_STATION_ASSEMBLY,

    /* --- glass --------------------------------------------------------------
       Sand melted and cooled. Its whole reason for existing is one property:
       opacity 0 while KIND_STATIC -- see g_matOpacity -- which makes it the
       first solid the light field passes through. That is a vessel wall you
       can watch a reaction behind, and it is the reason glass comes before
       acid rather than after it: acid needs somewhere to be CONTAINED. */
    MAT_GLASS_MELT,
    MAT_GLASS,

    /* --- tin and bronze ------------------------------------------------------
       Tin exists to be alloyed and for almost nothing else -- shallow, low
       melting point, structurally unremarkable on its own. Bronze is the
       first alloy and the tutorial for the mechanism every deeper metal reuses:
       two molten metals that happen to touch become a third, table-driven,
       through g_matWetInto/g_matWetBy exactly the way coal becomes fuel. No
       new engine code, just two more rows in an existing table. */
    MAT_TIN_ORE,
    MAT_TIN,
    MAT_TIN_MELT,
    MAT_BRONZE_MELT,
    MAT_BRONZE,

    /* --- steel ----------------------------------------------------------------
       Molten iron plus carbon -- coal, touching -- becomes molten steel, the
       same contact-reaction mechanism bronze uses. Stronger than iron, and it
       wants its own strength constant between STR_METAL and STR_HARD; see
       MatStrength. */
    MAT_STEEL_MELT,
    MAT_STEEL,

    /* --- acid -------------------------------------------------------------
       A liquid found in deep, sealed pockets the same way lava hotspots are
       placed -- see generateAcidPockets. It dissolves what heat cannot touch:
       soil, stone, wood, anything loose-or-softer, through g_matDissolvedBy, a
       new table shaped exactly like quenchedBy but GRADUAL (gated by
       ACID_DISSOLVE_CHANCE) rather than instant, because a whole wall of stone
       vanishing in the one frame it first touched acid would read as a glitch,
       not corrosion. Metal is untouched -- acid is the CHEMICAL route past
       materials the thermal route cannot reach, not a second way to cut metal
       -- and glass, gold and ceramic are deliberately left off the dissolvable
       list, which is the entire "acid-proof container" mechanic and costs
       nothing extra to express. */
    MAT_ACID,

    /* --- gold ---------------------------------------------------------------
       The best conductor in the game and immune to acid, which is the point:
       gold is not "better copper", it is the contact material for anything
       that has to keep working somewhere corrosive. Deliberately soft and
       low-melting -- it is not meant to compete with steel structurally.
       Rare, small pockets rather than veins. */
    MAT_GOLD_ORE,
    MAT_GOLD,
    MAT_GOLD_MELT,

    /* --- titanium -------------------------------------------------------------
       Light, STR_HARD, corrosion-proof (left off g_matDissolvedBy, same as
       gold), and the first metal whose smelting point genuinely requires a
       good furnace rather than a working one. See the note on its MATS row
       for how that gate is expressed given the temperature byte's own ceiling. */
    MAT_TITANIUM_ORE,
    MAT_TITANIUM,      /* STR_ALLOY, same tier as steel -- see MatStrength */
    MAT_TITANIUM_MELT,

    /* --- tungsten -------------------------------------------------------------
       The highest melting point in the game, deliberately sitting at the very
       top of what the temperature byte can express -- see the note on its row.
       What you build a crucible's hottest lining out of, which is the whole
       reason the game is called that. */
    MAT_TUNGSTEN_ORE,
    MAT_TUNGSTEN,
    MAT_TUNGSTEN_MELT,

    /* --- refractory -----------------------------------------------------------
       The high-temperature insulator DESIGN.md deliberately withheld, and
       deliberately NOT a new phase change: it is fabricated from ceramic and
       graphene at a station, because "a manufactured composite lining" is the
       honest description of what it is, and the simulation has nothing left to
       teach about a material that is mostly "ceramic, but better". See
       craft.cpp. */
    MAT_REFRACTORY,

    /* --- stratum --------------------------------------------------------------
       The sealed band between one cave layer and the next. Not decoration and
       not ordinary rock: it is the thing that makes the layers LAYERS rather
       than depth ranges with different ore in them, because it cannot be dug
       until the endgame.

       STR_SEALED rather than STR_ABSOLUTE is the whole design. Wall is the edge
       of the universe and is meant to be permanent; this is meant to be
       permanent UNTIL IT IS NOT. Putting it one rung below absolute leaves it
       breakable by a tool that does not exist yet, which is what turns "I can
       finally dig through the world's floors" into a reward hardmode can hand
       out. See MatStrength.

       Corrosion-proof (deliberately absent from g_matDissolvedBy) and with no
       phase change at any temperature the byte can express: a barrier that
       could be melted or dissolved would be a barrier with a trivial bypass,
       and the whole point is that the ONLY way past is a tool strong enough. */
    MAT_STRATUM,

    /* --- the blast furnace ---------------------------------------------------
       The layer 1 boss's reward, and a STATION rather than a material or an
       ore. That choice is the design: making the reward an exclusive ore turns
       the boss into something you farm, and a boss you farm is a chore with a
       health bar. A station is won ONCE, changes what you can build forever
       after, and cannot be ground for.

       It sits above the anvil in the same ladder every other station is on --
       see g_matStation -- and it is what the steel tier is fabricated at. Note
       that this does NOT gate the SMELTING of steel: steel is an iron+coal
       contact reaction and stays one, because heat and chemistry belong to the
       simulation (see PROGRESSION.md section 2). What the forge gates is
       turning a steel bar into an object, which is fabrication and therefore a
       bench's business. */
    MAT_STATION_FORGE,

    /* --- the spring ----------------------------------------------------------
       A rock that makes water, forever, into any empty cell beside it.

       Bounded by the shape of the world rather than by a budget, which is what
       makes an infinite source safe here: it only ever fills cells that are
       EMPTY, so it floods its chamber up to its own level and then has nothing
       left to do and lets its chunk sleep. Drain the pool and it refills. That
       is the entire mechanism, and it is self-limiting by construction rather
       than by a counter somebody has to keep correct.

       Deliberately not a device: it has no state, no tick of its own, no facing
       and no panel, and a cell that answers one question about its neighbours is
       what a material is for. See the spring rule in world.cpp. */
    MAT_SPRING,

    /* --- chitin --------------------------------------------------------------
       What comes off the things living in layer 1, and the main ingredient of
       the item that calls the layer's boss.

       A MATERIAL rather than a bare item, and that is forced rather than
       stylistic: a creature's drop is placed into the world as a CELL where it
       died (see entDie -- loot you collect, not loot that teleports into your
       pack), and only a MatId can be a cell. Making it a material also means it
       stacks, renders and saves with no extra work, which is the whole reason
       item ids share the material id space to begin with. */
    MAT_CHITIN,

    /* A dense luminous liquid. Appended so old saved material ids retain their
       meaning; it sinks through water but is otherwise an ordinary fluid. */
    MAT_GLOWFLUID,

    /* One-cell filter blocks. Kept at the end so adding them never renumbers
       materials in existing saves. Their permeability is handled by world.cpp:
       Sieve passes liquids and gases; Gas Sieve passes gases only. */
    MAT_SIEVE,
    MAT_GAS_SIEVE,

    /* Lamp wax is the first material whose buoyancy changes visibly with
       temperature. Appended for save-id stability. */
    MAT_WAX,

    MAT_COUNT
};

enum MatKind {
    KIND_EMPTY = 0,
    KIND_STATIC,   /* never moves */
    KIND_POWDER,   /* falls, piles up at an angle of repose */
    KIND_LIQUID,   /* falls, then spreads sideways to find its level */
    KIND_GAS       /* rises, wanders, spreads along the ceiling */
};

struct MatInfo {
    const char* name;
    u8  kind;
    /* Heavier sinks through lighter. Gases invert the test and rise through
       anything denser, which is what lets steam bubble up out of water. */
    u8  density;

    /* --- movement ---------------------------------------------------- */
    /* Powders: chance out of 255 of sliding down a diagonal when blocked
       below. High = flows like dry sand, low = stacks in steep columns.
       Interpolated between the two by how wet the cell is. */
    u8  slideDry;
    u8  slideWet;
    /* Liquids and gases: max cells travelled sideways in one frame. Higher =
       spreads out faster, at slightly more cost per cell. */
    u8  dispersion;
    /* One byte, two meanings, split by kind -- nothing reads both.

       GASES: chance out of 255 of drifting sideways instead of rising on a
       given frame. This is what makes smoke and steam flit and billow rather
       than shoot straight up in a rigid column.

       LIQUIDS: viscosity. Chance out of 255 that the cell refuses to flow
       sideways at all this frame. 0 is a freely running liquid, 230 is a
       90%-of-the-time-stuck ooze. It falls straight down regardless -- gravity
       is not what viscosity resists.

       Sharing the byte is deliberate rather than stingy. `dispersion` alone
       could not express this: it was already at its floor of 1 for molten
       rubber, and 0 does not mean "very thick", it means "not a liquid" --
       measured, a dispersion-0 pool poured down one side of a container never
       reached the far side in 6000 frames and left cells welded to the walls of
       a draining vessel. Viscosity as a probability is a separate axis with 255
       steps of room, and it degrades gracefully: a stuck cell simply tries
       again next frame, so the liquid still levels and still drains, just
       slowly. Adding a proper column instead would have meant editing every
       row of MATS[] to dodge -Wmissing-field-initializers, for a field one
       material uses. */
    u8  jitter;

    /* --- moisture ---------------------------------------------------- */
    /* capacity 0 means "does not absorb water at all". It should be a multiple
       of MOISTURE_UNIT so absorbing a water cell never has to split it.
       wick controls how far hydration reaches; smaller = water spreads
       further. See world.cpp for the full model. */
    u8  capacity;
    u8  wick;

    /* --- heat -------------------------------------------------------- */
    /* Temperature is a plain u8 of degrees, carried by every cell including
       air: ambient 20, water boils at 100, fire/lava burn near 235. */
    u8  heatCond;    /* 0..255. How readily heat crosses into this cell. Air is
                        deliberately low or fire would flash-heat the room. */
    /* Thermal mass, as a right-shift: 0 = normal, 3 = holds 8x the heat. It
       splits apart two things `heatCond` alone conflates -- how fast a material
       DELIVERS heat to its neighbours, and how fast it LOSES its own. A cell
       hands over the full amount but its own temperature drops by that amount
       shifted down, so lava can heat everything it touches at full rate while
       still staying molten for a long time. Turning conductivity down instead
       buys the same longevity but leaves lava unable to light or boil anything.
       (This is not energy-conserving, deliberately: it stands in for latent
       heat, which for rock is enormous.) */
    u8  heatMassShift;
    /* How many cells along an unbroken run of conductive material this cell
       ALSO exchanges heat with each frame, on top of its four neighbours.
       0 = ordinary material, conducts only to what it touches.

       This column exists because `heatCond` ran out of room, and the reason is
       worth understanding before reaching for either number. Conduction moves
       (adiff * cond) >> 9, capped at half the gap so a value can never cross
       the pairwise average -- that cap is what keeps the in-place symmetric
       exchange stable without a second buffer. It also means cond saturates:
       measured over every gap, cond 255 already achieves 99.2% of the cap, and
       256, 400 and 100000 are all bit-identical to each other. Iron sat at 255
       from the beginning, so it was ALREADY the most conductive material
       expressible, and nothing could be placed above it.

       Reach is the axis with headroom left. Ordinary conduction only ever
       touches the 4-neighbourhood, so a heat front crawls at one cell per frame
       however conductive the material is -- that limit is geometric, not
       thermal, and it is the honest thing to relax for a better conductor.
       A material with spread N moves its front up to N cells per frame instead.

       Each extra exchange uses exactly the same capped, symmetric, mass-scaled
       formula as a neighbour exchange, so energy still moves rather than
       appearing, and temperatures still cannot leave [0,255].

       Cost is O(spread) per cell per frame and is paid ONLY by materials that
       set it, so ordinary scenery is unaffected. Keep graphene's value sane for
       that reason -- it is the one number here that can make a large sheet
       expensive. */
    u8  heatSpread;
    u8  spawnTemp;   /* temperature when placed by hand; 0 means ambient */
    u8  coolTemp;    /* below this it turns into coolsTo; 0 disables */
    u8  coolsTo;
    u8  boilTemp;    /* at or above this it turns into boilsTo, shedding latent
                        heat; 0 disables. Covers boiling and melting. */
    u8  boilsTo;
    u8  igniteTemp;  /* at or above this it combusts into burnsTo, which is lit
                        HOT so fire spreads; 0 disables */
    u8  burnsTo;
    u8  quenchedBy;  /* touching this material destroys it; 0 disables */

    /* --- colour ------------------------------------------------------ */
    /* A range; each cell picks a fixed point in it via its tint byte, which is
       what gives piles their speckle instead of a flat slab of one colour.
       Then dry->wet is blended by moisture. */
    u32 dryA, dryB;
    u32 wetA, wetB;

    /* Derived at startup: (255 * 256) / capacity, so the wetness fraction is a
       multiply and a shift rather than a division in the hot loop. */
    u16 invCapQ8;
};

extern MatInfo MATS[MAT_COUNT];

/* Density is compared in Q8 fixed point so thermal expansion can matter well
   before it rounds to a whole density unit. The coefficient is Q8 density
   lost per stored degree above 20 C; zero keeps legacy fixed density exactly. */
extern u8 g_matThermalExpansionQ8[MAT_COUNT];
static inline int materialDensityQ8(u8 mat, u8 temperature) {
    int density = (int)MATS[mat].density << 8;
    const u8 expansion = g_matThermalExpansionQ8[mat];
    if (expansion)
        density -= (int)expansion * ((int)temperature - (int)degC(20));
    if (density < 1) density = 1;
    if (density > 65535) density = 65535;
    return density;
}

/* Total free-space cells represented when one liquid cell boils into this gas.
   One means legacy one-for-one conversion; Steam deliberately expands. */
extern u8 g_matGasExpansion[MAT_COUNT];

/* Cell -> pixel, precomputed. Indexed by
      (mat << 8) | (moisture & 0xF0) | (tint >> 4)
   which is 16 wetness levels x 16 tint levels per material. */
extern u32 g_colorLut[MAT_COUNT * 256];

/* Temperature -> glow colour and how strongly to blend it over the material
   colour. Ambient and below blend nothing at all. The alpha is capped well
   short of opaque so even white-hot material stays recognisable. */
extern u32 g_heatLut[256];
extern u8  g_heatAlpha[256];

/* Whether a material takes the heat glow at all, filled in by initMaterials().
   Almost everything does -- it is how you read temperature at a glance. Plasma
   is the one exception, for a reason worth stating: the glow ramp is orange at
   working temperatures, and blending orange over blue does not make a hotter
   blue, it makes mud. Measured before this existed, a plasma cell rendered
   #5652B4 -- a murky purple -- when its material colour is #3866FF. A material
   that only ever exists at extreme heat also gains nothing from an overlay
   whose whole job is to announce "this is hot"; its own colour already says so.
   So plasma keeps its blue in the Glow view, and the Heat view still reports
   its true temperature like everything else.

   A standalone array rather than a MatInfo field on purpose. As a field it
   would be the one column absent from all nineteen MATS[] rows, which trips
   -Wmissing-field-initializers on every row under -Wextra; and at MAT_COUNT
   bytes this whole table sits in a single cache line, where reading it in the
   render loop is cheaper than pulling in the much larger MatInfo. */
extern u8 g_matGlows[MAT_COUNT];

/* Chance out of 255 that a cell of this material simply expires each frame.
   0 for everything except cold fire, and the reason it has to exist is worth
   understanding before reaching for it again.

   Fire needs no such thing: it is 185 degrees above ambient, and conduction
   moves (adiff * cond) >> 9, so even against air's near-insulating cond of 6 a
   flame sheds (185*6)>>9 = 2 degrees per neighbour per frame. It cools itself
   to death in about 90 frames, and every degree it loses went into something.

   Cold fire cannot do that, and not because it is merely weaker. The cold half
   of the scale is 60 degrees against the hot half's 195, so cold fire sits only
   58 degrees below ambient -- and (58*6)>>9 truncates to **zero**. It exchanges
   literally nothing with open air. Its only route to expiry is the slow drift
   toward ambient, which is why it used to live 546 frames against fire's 94 and
   ride 359 cells up against fire's 68, piling up under anything it could not
   get past instead of being spent on it.

   Since temperature cannot govern its lifetime, lifetime becomes its own knob.
   That also lets the two halves be tuned independently, which is what the
   material actually needs: heatMassShift decides how much cold ONE cell
   delivers before it warms, and this decides how long it hangs around. Cold
   fire wants both -- a big payload and a short life. */
extern u8 g_matDecay[MAT_COUNT];

/* --- how hard a material is to break ---------------------------------------

   A projectile carries a `power`; it destroys any cell whose strength is at or
   below that and is stopped by anything above it. So strength is not "hit
   points", it is a THRESHOLD -- a tier list, not a health bar. That is the
   right shape for this game because the interesting question is always "can
   this tool get through that", and a tool that eventually chews through granite
   given enough time answers it with "yes, tediously" instead of "no".

   The named tiers exist so gradation can be added between them without renaming
   anything: there is room for six more steps between LOOSE and SOFT, and the
   whole scale is deliberately spread across 0..255 rather than 0..10.

   Standalone rather than a MatInfo column, matching g_matGlows and g_matDecay
   above, and for the same two reasons: as a field it would be the one entry
   missing from all twenty-eight MATS[] rows, which trips
   -Wmissing-field-initializers on every one of them under -Wextra; and having
   the whole durability ladder in one readable block is how you tune a ladder.
   Scanning a 25-column row for the strength value would make the relative
   ordering -- which is the only thing that matters here -- invisible. */
enum MatStrength {
    STR_NOTHING = 0,     /* air and gases: a shot passes through untouched */
    /* Liquids. Weaker than anything solid, but NOT free passage: a shot that
       sailed through a lake as if it were not there was the single most
       "phasing through the world" thing in the game. A shot now spends its
       pierce crossing water, so depth stops it -- which is the behaviour
       everyone expects and costs nothing to express in the same threshold model
       as everything else. Gases stay at zero, because a shot slowing down in
       smoke would be a nuisance with nothing to recommend it. */
    STR_FLUID   = 5,
    STR_LOOSE   = 10,    /* sand, dirt: what the starting shot is meant to clear */
    STR_SOFT    = 40,    /* ice, wood, rubber */
    STR_ROCK    = 90,    /* stone */
    STR_METAL   = 150,   /* iron, copper, frozen mercury, bronze */
    /* Steel and titanium: stronger than an ordinary metal, short of graphene's
       tier. Named rather than a bare number between 150 and 210 for the same
       reason every other rung here is named -- so the next thing that goes
       between two tiers has somewhere to look for the convention. */
    STR_ALLOY   = 180,   /* steel, titanium */
    STR_HARD    = 210,   /* graphene, tungsten */
    /* The layer barriers, and nothing else. Above every tool and every shot
       that currently exists, below absolute -- so it reads as unbreakable for
       the whole of the game as it stands, and stops reading that way the moment
       something with power 235 is built.

       That gap is the last of the ladder's headroom and it is being spent
       deliberately on one thing. Anything else that wants to sit between HARD
       and ABSOLUTE from here on should ask whether it really wants to be a
       layer barrier, because there is no room left for a third opinion. */
    STR_SEALED  = 235,   /* MAT_STRATUM: the floor between cave layers */
    STR_ABSOLUTE = 255   /* wall, and the machines: nothing breaks these */
};

extern u8 g_matStrength[MAT_COUNT];

/* --- light -----------------------------------------------------------------
   Two tables, and they are the entire material-side contract with light.cpp:
   how much light a cell MAKES, and how much light it EATS per cell crossed.

   Emission is not derived from temperature, and that is a deliberate split
   rather than a missed simplification. Tying the two together reads well for
   lava and falls apart everywhere else: a red-hot iron bar would floodlight a
   room while a lamp -- the one thing whose job is light -- would have to be hot
   to work, and lighting a wooden room would mean setting fire to it. Fire and
   lava are bright BECAUSE they are listed as bright, and a cold lamp is the
   brightest thing in the table.

   Opacity is per cell crossed, out of LIGHT_MAX. It sets the reach of every
   source at once: at attenuation a, light of strength s carries s/a cells. The
   air figure is therefore the single most consequential number in the lighting
   system -- it decides how far daylight reaches into a cave mouth and how big a
   room one lamp lights -- and the solid figure decides how thick a wall has to
   be before it is properly dark behind it. */
extern u8 g_matLight[MAT_COUNT];
extern u8 g_matOpacity[MAT_COUNT];

/* --- what an entity can walk through ---------------------------------------
   True for a cell the PLAYER passes through as if it were air. Only the torch
   is, so far.

   This is about ENTITIES ONLY, and the distinction is the whole reason it is a
   separate table rather than a new MatKind. A torch still occupies its cell for
   every other purpose: sand piles on top of it, a shot has to break it, light
   reads it, and the movement rules will not shove material into it. What it does
   not do is stop a person -- which is a fact about collision, not about physics,
   and the falling-sand rules should not have to learn a new kind to express it.

   A new KIND_ would have been the wrong shape twice over. Every switch on kind
   in world.cpp would need a case for something that behaves exactly like
   KIND_STATIC, and the property is not exclusive: a torch is a static solid AND
   passable, so it wants an adjective, not a category. Compare cellFalling() in
   world.h, which is the other half of "solid to the sim, not to you" -- that one
   is per-cell and temporary, this one is per-material and permanent.

   Standalone rather than a MatInfo column for the reason the tables above it all
   give: as a field it would be the one entry missing from every MATS[] row, which
   trips -Wmissing-field-initializers on all of them under -Wextra. */
extern u8 g_matPassable[MAT_COUNT];

/* --- what you can climb ----------------------------------------------------
   True for a cell you can move through in any direction under your own power:
   gravity stops applying while you are touching one, and up and down become
   things you simply do rather than things you jump or fall into.

   Rope only, so far. Separate from g_matPassable because the two are genuinely
   independent -- a torch is passable and not climbable, and a ladder built into
   a wall would want to be climbable while still stopping sand -- and because
   the moment they share a table somebody adds a passable material and gets
   free flight through it. */
extern u8 g_matClimb[MAT_COUNT];

/* --- what holds you up from above and nowhere else -------------------------
   A platform: you walk through it sideways, jump up through it, and land on
   top of it. Holding down drops you through.

   This cannot be a property of the CELL alone, which is why it is a table read
   by a collision test that also knows which way you are going -- see SolidMode
   in player.h. Every other material in the game answers "am I solid" with one
   bit; this one answers "solid to what". */
extern u8 g_matPlatform[MAT_COUNT];

/* --- what dims light without blocking it -----------------------------------
   Per-cell attenuation of a SUNBEAM, out of 256, for material that daylight
   filters through rather than stopping at. Zero for everything solid.

   Leaves need this and nothing else in the game did. The skylight model is
   binary -- a ray either reaches a cell or was stopped somewhere above it --
   which is right for rock and hopeless for a canopy: measured on a grown tree,
   a 221x181 crown read ZERO across its entire middle, and so did the ground
   underneath. Trees were black cut-outs.

   The two obvious fixes are both wrong. Leaving leaves opaque and lowering
   g_matOpacity only helps the propagation sweeps, which crawl in from the edges
   and cannot reach the centre of a hundred-cell crown. Making leaves count as
   OPEN passes every ray at full strength, so the crown is uniformly bright, the
   tree cast no shadow at all, and foliage stops being foliage.

   Attenuating instead gives the thing itself: a ray loses this fraction per
   leaf cell it crosses, so the outside of a crown is bright, the depths are
   dim, and the ground beneath is dappled rather than either black or
   untouched. It is the same distinction lightOpen() already draws for smoke,
   which is why that note is worth reading beside this one. */
extern u8 g_matSheer[MAT_COUNT];

/* --- what will sprout ------------------------------------------------------
   True for a material that becomes a plant when it settles on the right
   ground. The SIMULATION is the only thing that knows a powder has come to
   rest, so it has to be able to ask the question -- but which plant, and on
   what, is tree.cpp's business entirely. One bit here keeps world.cpp from
   learning what a tree is. */
extern u8 g_matIsSeed[MAT_COUNT];

/* --- what a canopy is made of, and what holds one up -----------------------
   Leaves die when nothing connects them to wood -- see treeAudit(). These two
   bits are what the rule is written in terms of, and they are here rather than
   as names in tree.cpp for the same reason g_matIsSeed is: the simulation has
   to notice a wood cell going away without knowing what a tree is, and adding
   a species must not mean finding every place two material names were spelled
   out.

   Any wood holds up any leaves. That is not sloppiness about species: a
   birch growing through an oak's canopy really is holding those leaves up, and
   the alternative -- a support rule that has to agree with itself about which
   trunk a given leaf belongs to -- needs an ownership model that nothing else
   here has. It also means a wooden building keeps a canopy alive, which is the
   same answer Minecraft gives and for the same reason.

   Pods count as leaves. A pod is a leaf with a seed in it, it hangs off the
   same canopy, and a rule that let pods float where their leaves had gone
   would leave a felled tree as a constellation of dots. */
extern u8 g_matIsLeaf[MAT_COUNT];
extern u8 g_matIsWood[MAT_COUNT];

/* --- what grew ------------------------------------------------------------
   Everything a harvesting tool will cut, and nothing else. DERIVED from the
   tables above rather than listed by hand, so a species added later is covered
   by the rows that define it -- the alternative is a sickle that silently
   refuses to cut whatever somebody forgot to add, which looks like a bug in the
   tool rather than a missing table entry.

   The point of it is a tool that takes the tree and leaves the hillside. Wood,
   leaves, pods, saplings, stalks, grain and loose seed are in; dirt, grass and
   stone are out, so clearing a canopy over your field cannot take the field
   with it. */
extern u8 g_matIsPlant[MAT_COUNT];

/* --- seeds that do not fall straight ---------------------------------------
   Chance out of 255 that a powder in FREE FALL steps sideways-and-down instead
   of straight down. Zero for everything that is not a seed, which is every
   other powder in the game: sand poured down a shaft should land in a pile
   under the shaft.

   A seed should not. Six pods off one felled oak dropped six seeds into the
   footprint of the crown they came from, and what grew back was a clump of
   trees in the hole where a tree used to be -- which is the opposite of what a
   seed is for. Wind is what stops that happening to a real tree and this is the
   cheapest honest version of it.

   The drift is DIAGONAL, so a seed still descends while it wanders and can
   never hover; the most it can manage is 45 degrees, which over a crown three
   hundred cells up is three hundred cells of spread. Applied only when the cell
   below is genuinely open, so this can never turn a seed resting on the ground
   into one that crawls -- that would be a slide, and a slide is settling rather
   than falling. See updatePowder.

   --- why it is not one number per material ---
   Every seed of a species reading the same chance would give every seed the
   same trajectory, and identical trajectories from one spot is a clump moved
   sideways rather than a clump broken up. So the table sets the MAXIMUM and
   each individual seed scales it by a few bits of its own tint -- which is a
   random byte it already carries, which the swap in tryMove already moves with
   it, and whose bottom nibble the renderer does not read (it draws from
   tint >> 4). One seed drifts hard and lands far out, the next barely drifts
   and lands near the trunk, and a handful of them spread along a line instead
   of arriving together. */
extern u8 g_matDrift[MAT_COUNT];

/* --- what the renderer does not draw ----------------------------------------
   True for a cell whose material is NOT painted: the backdrop is drawn in its
   place, exactly as if the cell were empty.

   This exists for devices whose cells carry a mechanism rather than a picture.
   A device is a 14x14 block of cells plus a sprite drawn over them, and for the
   boxed machines that works because the sprite is a box -- it covers what it
   stands on. The torch is deliberately not a box (see ART_TORCH in sprite.cpp),
   so every cell its silhouette does not cover was showing bare MAT_TORCH: a
   14x14 yellow square with a torch drawn in the middle of it.

   Painting the material in a colour that reads as "nothing" would not fix it.
   The cell has to be genuinely see-through, because what belongs behind a torch
   is whatever the torch is bolted to -- rock, a built wall, open sky -- and only
   the backdrop knows which.

   Rendering is the ONLY thing this turns off. The cells are still there for
   collision, light, heat and mining; MAT_TORCH is passable as well, but that is
   g_matPassable's business and the two are independent. Anything unseen must be
   drawn by something else or it becomes invisible, which is why this is a table
   with two entries and not a property materials can casually opt into.

   MAT_EMPTY is set too, so the renderer's inner loop tests one array instead of
   a comparison plus an array -- the empty case is by far the most common, and it
   wants to stay the cheap one. */
extern u8 g_matUnseen[MAT_COUNT];

/* --- what a cell gives you when you break it -------------------------------
   The item a material yields when mined. The identity for almost everything --
   stone gives stone -- and the table exists for the cases where the thing in the
   world is a STATE rather than a possession.

   The door is the one that needs it: mining an open door has to give you a door.
   Without this you get an "Open Door" in your pack, which is a second item that
   means the same object, stacks separately from the first, and paints a
   permanently-open doorway when you place it. None of that is a door.

   Deliberately not the same idea as a smelting product or a phase change: those
   happen in the world, to the cell, and are already table-driven. This is about
   what the PLAYER ends up holding, which is the one transformation the
   simulation never sees. */
extern u8 g_matDropsAs[MAT_COUNT];

/* --- smelting --------------------------------------------------------------
   Chance out of 255 that a smelting ore cell yields its METAL rather than slag.
   0 means "not an ore" and is the sentinel, so every non-ore row costs nothing.

   Ore is otherwise an ordinary table-driven phase change: boilTemp is the
   smelting point and boilsTo is the molten metal, so ore gets the existing
   latent-heat behaviour for free -- which matters, because latent heat is what
   stops one heater flashing a whole pile at once and makes a furnace something
   you wait on. The only new behaviour is that the product is chosen, per cell,
   between the metal and MAT_SLAG_MELT.

   Doing it per cell rather than per pile is what makes this work. A single ore
   cell is a coin flip and tells you nothing; a pile of two hundred is a
   dependable ratio, and the yield becomes a property of ORE VOLUME. That is the
   whole reason to bother mining a lot of it.

   The separation is then not implemented at all -- it falls out of physics
   already present. Molten slag is lighter than either molten metal, and tryMove
   already lets a denser fluid sink through a lighter one, so the metal collects
   underneath and the slag floats. Each then freezes by its own coolTemp/coolsTo
   row: metal into metal, slag into the brittle crust you have to break. Nothing
   in world.cpp knows that a furnace is a thing.

   Smelting points sit BELOW the pure metal's own melting point on purpose
   (copper ore 165 C against copper's 175, iron ore 190 against iron's 200).
   That is roughly true of real smelting, and it means processing ore is easier
   than remelting finished metal -- so the ore route is the attractive one rather
   than a chore you do because the game insists. */
extern u8 g_matSmeltYield[MAT_COUNT];

/* --- crafting stations -------------------------------------------------
   0 for everything except the four station materials, where it holds the
   CraftStation tier that material IS (see craft.h). A placed cell of
   MAT_STATION_ANVIL answers "what station is this" with one table read,
   which is what lets craftCan() scan the world around the player for a
   tier without knowing the first thing about crafting -- it only ever
   asks "is there a material near me whose g_matStation equals N". */
extern u8 g_matStation[MAT_COUNT];

/* --- what acid dissolves ------------------------------------------------
   0 for immune; otherwise the MatId of the acid that dissolves this
   material on contact. Shaped exactly like `quenchedBy` above -- a
   material touching its listed reagent is destroyed -- and deliberately a
   SEPARATE column rather than reusing quenchedBy, for the same reason
   g_matWetInto is not folded into quenchedBy either: quenchedBy is an
   instant, unconditional heat-extinguish (fire touching water dies THAT
   frame, every time, which is correct for fire), and reusing it here would
   make a whole wall of stone vanish in the single frame it first touched a
   pool of acid. Corrosion is not supposed to look like that.

   See ACID_DISSOLVE_CHANCE in world.h for the probability gate that keeps
   this gradual instead of instant, and g_matDropsAs for why dissolving
   destroys the cell outright rather than banking anything -- acid is not a
   mining tool, it is a way past terrain nothing else gets past.

   Deliberately absent from glass, gold and ceramic even though some of them
   share a strength tier with materials that ARE dissolvable -- that
   omission, and nothing else, is the entire "acid-proof container"
   mechanic. */
extern u8 g_matDissolvedBy[MAT_COUNT];

/* --- what carries a spark --------------------------------------------------
   True for a material electricity travels through. See the electricity note in
   device.h for the model.

   Metallic phases conduct: iron, copper and mercury remain conductive when
   molten or frozen. A liquid-metal circuit is deliberately unstable -- it can
   flow between fronts, form a bad short, then overheat and rupture -- which is
   now a useful simulation outcome rather than an invisible exception.

   Graphene is in, and it is the interesting one: it is already the material with
   no melting point, so it is the only wire that survives being run through a
   furnace. That is a real reason to want the expensive conductor rather than a
   bigger number on a stat sheet.

   Deliberately NOT derived from heatCond: aluminum nitride has metal-grade
   thermal conduction and no electrical conduction. They are different physical
   properties, so a heat sink does not have to short a circuit. */
extern u8 g_matConducts[MAT_COUNT];

/* --- slaking ---------------------------------------------------------------
   What a material becomes when it touches water. 0 means "nothing happens",
   which is every material but one.

   This is the coal-into-fuel step, and it is a table rather than a rule in
   world.cpp because it is the same shape as everything else there: a condition on
   a neighbour and a conversion. The water is CONSUMED, which is what makes fuel
   cost something you have to go and get -- there is a lake for exactly this.

   Deliberately not folded into `quenchedBy`, which is next to it in world.cpp and
   looks similar. That destroys the cell; this transforms it, and the two want
   opposite things from the heat: a quench dumps its heat into the water that put
   it out, while slaking is a cold process on cold coal. One column doing both
   would need a flag to say which. */
extern u8 g_matWetInto[MAT_COUNT];

/* --- alloying -------------------------------------------------------------
   What this material becomes when it touches g_matAlloyWith[m], and the
   material it becomes into g_matAlloysTo[m]. Shaped like g_matWetInto just
   above, and deliberately a SEPARATE mechanism rather than the same one,
   because the two reactions are opposite in the one way that matters: wetInto
   CONSUMES its neighbour (the steam that slakes coal is spent, and that is the
   entire cost of fuel), while alloying must not -- two ingots of a precious
   metal poured together should not make one vanish. Both sides of an alloy
   pair get their own row (copper melt reacts to tin melt and tin melt reacts
   to copper melt), so as a mixed pool churns, every cell of EITHER metal that
   ever touches the other converts, and nothing is destroyed in the process.

   Steel is deliberately NOT built this way -- see g_matWetInto[MAT_IRON_MELT]
   in initMaterials() -- because carbon really is consumed into the melt
   rather than surviving alongside it, which is exactly what wetInto already
   expresses. Bronze is the one alloy in the game where BOTH ingredients are
   metals worth keeping, which is the whole reason this table exists rather
   than reusing wetInto for it too. */
extern u8 g_matAlloyWith[MAT_COUNT];
extern u8 g_matAlloysTo[MAT_COUNT];

/* What it has to TOUCH for that to happen. Coal wants STEAM, not water: standing
   in a puddle is not a process, and requiring steam means a boiler -- water, a
   heat source, and somewhere for the vapour to meet the coal. That is the smallest
   apparatus that is actually an apparatus, and it puts a real step between "found
   coal" and "can melt iron". */
extern u8 g_matWetBy[MAT_COUNT];

/* --- natural heat -----------------------------------------------------------
   A temperature the BACKGROUND holds its cell at, as a floor. 0 for almost
   everything.

   This is what makes a lava hotspot a place rather than a decoration. Deep chunks
   generated with a molten backdrop pin their cells hot, so the stone in them melts
   on its own and stays melted -- and if you tap one, what drains out cools and
   freezes like any other lava, while the pocket itself stays hot and refills the
   space you opened. It is a permanent geological feature you build around, not a
   puddle that evaporates the moment you find it.

   A floor rather than a setpoint, so it can only ever ADD heat. Something hotter
   sitting in a hotspot is left alone, which matters because a furnace built inside
   one should still work normally.

   Costs nothing extra: it is read from the same bg byte g_bgRetain already
   fetches, in the same branch. And a cell already at or above the floor is not
   written, so a settled pocket stops dirtying its chunk and goes to sleep like
   anything else. */
extern u8 g_bgHeat[MAT_COUNT];

/* --- what the BACKGROUND does to heat --------------------------------------
   How strongly the scenery behind a cell stops it drifting back to ambient,
   0..255, where 0 is "no effect" and 255 would be perfect insulation.

   The background has been purely cosmetic until now -- world.cpp's note on the bg
   array says outright that the simulation never reads it -- so this is a real
   change to that contract and it earns its place: it is the difference between a
   furnace being a pile of hot material and a furnace being a ROOM you built. Line
   a chamber with ceramic and it holds temperature; leave it open to bare rock and
   it does not.

   It suppresses the drift toward ambient only. It does not touch conduction
   between cells, which is where heat actually travels: making a backdrop conduct
   would mean heat flowing through the wall BEHIND things, which is both a
   surprising physical claim and a second heat network to debug.

   Cost is why it is a retention factor and not something richer. The drift is a
   single rngChance roll per cell (see updateHeat), and this is read only when that
   roll has already come up -- so on the ~92% of frames where nothing was going to
   happen anyway, the bg array is never touched at all. */
extern u8 g_bgRetain[MAT_COUNT];

/* --- forcing heat into a neighbour ------------------------------------------
   Degrees per frame this material drives its four orthogonal neighbours toward
   its own temperature, on top of ordinary conduction. 0 for everything that is
   not actively burning.

   This exists because conduction alone cannot smelt, and the heater's note in
   world.h already explains why: conduction runs at min(condA, condB), so the rate
   is set by the POORER conductor -- the ore -- and the charge settles into a
   gradient that levels off well below the source. Measured, a firebox of burning
   fuel sitting at 215 C could only hold a charge at 156 C, and feeding it
   continuously made no difference (154 -> 156) because it was never running out
   of fuel, it was in equilibrium. The heater beats that only because
   MACHINE_DRIVE lets it push past the neighbour's own conductivity.

   Burning fuel is a heat SOURCE in the same sense a heater is, so it gets the
   same mechanism at a smaller number -- which is also what puts the ladder in
   order: ember drives less than fuelfire, so coal reaches copper and stops, and
   only fuel gets you iron.

   Stable for the same reason MACHINE_DRIVE is: the step is clamped at the
   source's own temperature and can only close the gap, so a neighbour converges
   and stops rather than climbing. It cannot be written as a plain "+= N". */
extern u8 g_matDrive[MAT_COUNT];

/* Full brightness. A byte, so it is also the sun. */
static const int LIGHT_MAX = 255;
/* Unlit is genuinely zero in the light field. "Never quite dark" is a display
   decision, not a physical one, and it lives in LIGHT_MIN_SHADE below -- see
   the note there for why keeping it out of the field mattered. */
static const int LIGHT_NONE = 0;

/* --- light to brightness ---------------------------------------------------
   How much of a cell's colour survives at a given light level, 0..255.

   This is a separate table from the light field itself, and keeping the two
   apart is the point. The field is physical: light strength, falling off
   linearly with distance, which is what makes it something you can reason
   about and test in units of cells. Brightness is perceptual, and the mapping
   between them is not the identity -- multiplying a colour by light/255 gives
   a picture where everything except the immediate vicinity of a lamp reads as
   black, because half the light is nothing like half as bright to look at.

   That is not a theoretical worry; it is what the first version did. A lamp on
   the ceiling of an ordinary room put the floor at light 75, which is a
   perfectly reasonable a third of full daylight, and rendered it at 29% of the
   material colour -- so a lit room looked like a dark room with two white dots
   in it, and the screenshot was the only way to find out.

   The curve is a plain gamma over the WHOLE range, 0..255, and getting that
   wrong is what made ground look like a cliff edge rather than a fade.

   It used to be a gamma applied above a floor of 26, with everything below the
   floor clamped flat. That sounds harmless and is not, because a gamma under 1
   has an infinite slope at zero: the first light level above the floor already
   jumped most of the way up the curve. Going down into soil rendered

       89  76  62  46  22  10  10  10  10 ...

   -- steps of 13, 14, 16, 24, and then a wall. The 22-to-10 step was twice any
   other and everything past it was identical, so the eye read a hard edge with
   flat black behind it, which is exactly what it was. Nothing about the light
   field caused that; the field was a clean linear ramp the whole way down.

   With no knee and a gentler gamma the same field renders

       89  78  67  55  43  28  16  16 ...

   -- even steps all the way to the floor. The lesson worth keeping is that the
   discontinuity was in the DISPLAY MAPPING, and every instinct said to go
   looking at the propagation.

   0.85 rather than 0.62 for the same reason: with a knee, a steep curve was
   compensating for light that ran out too fast. Without one, a gentle lift is
   all that is wanted -- enough that half the light does not read as a tenth of
   the brightness, and not so much that everything flattens toward white. */
extern u8 g_lightShade[256];
static const double LIGHT_GAMMA = 0.85;

/* What unlit renders at. This, not the light field, is where "never quite
   pitch black" belongs: zero light means zero light, and how dark you choose
   to draw it is a separate question with a separate answer.

   6%, down from 16%, and the reason is that 16% was answering the wrong
   question. It was raised from 10% because "under the soil is dark" was a fair
   complaint -- but lifting the floor does not make an unlit cave READABLE, it
   makes it a uniform grey wash, and the wash turned out to cost more than the
   darkness ever did.

   At 16% you could make out every ore vein and the full shape of every cave
   through solid stone. That is bright enough to give the layout away and far
   too dim to be pleasant, so the underground read as murky AND held no
   surprises: there was nothing to discover by lighting it, because you could
   already see it, and a lamp changed a dark grey picture into a slightly less
   dark grey one. Both halves of that came from this one number, which is why it
   never looked like a single fault.

   At 6% unlit rock is nearly black and a lit tunnel is unmistakably a different
   place. What you get back is the thing the lighting is FOR: the ore in a wall
   is genuinely hidden until something lights it, and carrying a lamp is how you
   find out what is down there. The cost is that the first minute underground,
   before anything is placed, is dark -- which is the correct price, and is
   exactly what the lamp is for.

   Note this is not "make it darker". The mid-tones are untouched; only the
   bottom of the range moved. Lit space is as bright as it ever was, and the
   whole change is in how far apart lit and unlit sit. */
static const int LIGHT_MIN_SHADE = 16;

/* --- background colours ----------------------------------------------------
   What a material looks like when it is BEHIND you rather than in front: the
   far wall of a tunnel, the back of a room. Darkened well down and shifted
   cool, because the only job this colour has is to read as "not reachable" at
   a glance -- a background you can mistake for material is worse than none.

   Indexed (mat << 4) | speckle, 16 shades per material. The speckle comes from
   a hash of the cell's position rather than from its tint byte, because an
   empty cell's tint is whatever the last material there happened to leave
   behind -- so it would change when you dug, and a wall that shimmers as you
   mine in front of it looks broken. A position hash is fixed forever. */
extern u32 g_bgColorLut[MAT_COUNT * 16];

/* The same thing for background a PLAYER PUT THERE, and it is a good deal
   lighter. Two LUTs rather than one because the BG_PLACED bit already tells the
   two apart and nothing was using it for colour.

   The dark version above is right for what it was written for -- the natural rock
   face behind a tunnel you dug, which should read as "not reachable" and stay out
   of the way. It is wrong for masonry. At 34% toward the material's own colour, a
   wall you built in daylight rendered as a near-black rectangle against the sky:
   not dim, but reading as a HOLE, or as a shadow cast by something that was not
   there. That is the opposite of what building a wall should look like.

   Placed background is finished work and should look like it. Still clearly
   behind the foreground -- it must never compete with material you can touch --
   but plainly a surface rather than an absence. */
extern u32 g_bgPlacedLut[MAT_COUNT * 16];

/* --- zone backdrops --------------------------------------------------------
   What shows when there is no wall behind a cell at all. Which of these two
   applies is a per-chunk label, never a depth test -- see ZoneId in world.h.

   The sky is a gradient by depth, which is not a contradiction: the LABEL
   decides that you are looking at sky, and the gradient only decides what
   shade of it. Clamped, so a sky chunk deep underground simply sits at the
   bottom of the ramp.

   The last entry of the sky ramp is EXACTLY the cave colour, and that is load
   bearing. A sky chunk and an underground chunk meet on a hard 32-cell chunk
   boundary, and anywhere that boundary crosses open air -- a shaft, a cave
   mouth -- a colour step would draw a visible ruled line across the world.
   Meeting at the same value makes the join invisible without any blending. */
/* Cells over which the sky ramp runs. It has to reach at least as low as the
   ground does, or every column below it clamps to the last entry and the whole
   visible sky is one flat colour -- which is exactly what 768 did once the
   plains sat at y=1200: the ramp was spent 400 cells above the horizon and the
   sky rendered as a grey wash. Comfortably past SURFACE_Y. */
static const int SKY_BAND = 1400;
extern u32 g_skyLut[SKY_BAND];
extern u32 g_caveLut[3][16];   /* one per cave layer; index with caveLayerOf() */

/* Static per-cell speckle. Cheap, and stable for a given cell for all time. */
static inline u32 bgSpeckle(int x, int y) {
    u32 h = (u32)(x * 73856093) ^ (u32)(y * 19349663);
    h ^= h >> 13;
    return h & 15;
}

/* Blend two packed 0xRRGGBB colours. t is 0..255. */
static inline u32 lerpColor(u32 a, u32 b, int t) {
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r  = ar + (((br - ar) * t) >> 8);
    int g  = ag + (((bg - ag) * t) >> 8);
    int bl = ab + (((bb - ab) * t) >> 8);
    return ((u32)r << 16) | ((u32)g << 8) | (u32)bl;
}

void initMaterials();
/* Verifies the property Clone's id-in-moisture packing relies on. Called by
   initMaterials(); see the definition for what it protects. */
void checkCloneColorInvariant();
