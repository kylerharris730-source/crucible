#pragma once
#include "common.h"

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
    MAT_STEAM,
    MAT_FIRE,
    MAT_IRON,        /* static, extremely heat-conductive */
    MAT_LAVA,        /* molten stone; freezes back to stone as it cools */
    MAT_WOOD,        /* catches fire and is consumed */
    MAT_CLONE,       /* copies the first material it touches, forever */
    MAT_VOID,        /* destroys whatever it touches */
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
    /* Gases only: chance out of 255 of drifting sideways instead of rising on
       a given frame. This is what makes smoke and steam flit and billow
       rather than shoot straight up in a rigid column. */
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
