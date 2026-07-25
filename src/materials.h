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
