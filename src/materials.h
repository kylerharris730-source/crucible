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
    /* --- machinery -------------------------------------------------------
       The cells a multi-cell DEVICE occupies. One material for every device
       type, because the grid only needs to know "something solid and man-made is
       here"; which device it is, and all of its state, live in the device list
       (see device.h). Putting a material per type in here would mean the sim
       learning about machines it has no business knowing about, and there is
       nowhere in a 4-byte Cell to keep a setpoint anyway. */
    MAT_DEVICE,
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
    STR_METAL   = 150,   /* iron, copper, frozen mercury */
    STR_HARD    = 210,   /* graphene */
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

/* --- what carries a spark --------------------------------------------------
   True for a material electricity travels through. See the electricity note in
   device.h for the model.

   SOLID metals only. Molten iron and molten copper are better conductors in real
   life and are deliberately excluded, because a wire that FLOWS is not a wire --
   a circuit drawn in a liquid would rearrange itself between one spark and the
   next, and "my machine stopped working because the wiring ran downhill" is a
   failure nobody could diagnose. Mercury is out for the same reason.

   Graphene is in, and it is the interesting one: it is already the material with
   no melting point, so it is the only wire that survives being run through a
   furnace. That is a real reason to want the expensive conductor rather than a
   bigger number on a stat sheet.

   Deliberately NOT derived from heatCond, even though the same three materials
   top both lists. They are different physical properties that happen to correlate,
   and tying them together would mean a future insulator that conducts
   electricity, or a heat sink that shorts a circuit, could not be expressed. */
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

   16% rather than the 10% it started at, because "under the soil is dark" was
   a fair complaint about a picture with no information in it. At this level
   unlit rock still reads as rock -- you can tell stone from a shaft you are
   about to fall down, and aim your first lamp -- while a lit room is
   unmistakably a different place. Any higher and there is no reason to carry a
   lamp at all. */
static const int LIGHT_MIN_SHADE = 40;

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
extern u32 g_caveLut[16];

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
