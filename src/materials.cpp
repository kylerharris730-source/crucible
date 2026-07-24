#include "materials.h"

/* One row per material, in enum order. The zero-heavy columns read cleanly if
   you think of them as "off unless set": most materials have no phase changes.

   Heat design notes:
   - Air (Empty) and Wall conduct poorly on purpose; give air a realistic
     conductivity and a single flame heats the whole room in seconds.
   - Iron is the opposite extreme, near-perfect conduction, so a bar of it
     carries heat from a flame at one end to the far end quickly.
   - Stone melts to Lava at 220; Lava freezes back to Stone below 160. The gap
     between those two is hysteresis -- without it a cell right at the melting
     point would flicker between the two states every frame.
   - Steam conducts very little and condenses only once quite cool, so it rides
     upward a long way before turning back to water.
   - Wood ignites at 140 into HOT fire (see igniteTemp handling in world.cpp),
     so a flame conducts into neighbouring wood until it too lights: fire
     spreads through a plank on its own. */

/*  name     kind        dens slDry slWet disp jit  cap wick  cond mass spawn coolT coolsTo   boilT boilsTo   ignT burnsTo  quench       dryA      dryB      wetA      wetB   */
MatInfo MATS[MAT_COUNT] = {
  { "Empty", KIND_EMPTY,    0,   0,    0,   0,   0,   0,  0,   12,  0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0x0E0E12, 0x0E0E12, 0x0E0E12, 0x0E0E12, 0 },
  { "Wall",  KIND_STATIC, 255,   0,    0,   0,   0,   0,  0,   30,  0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0x4C5158, 0x353A41, 0x4C5158, 0x353A41, 0 },
  { "Stone", KIND_STATIC, 255,   0,    0,   0,   0,   0,  0,   85,  0,   0,    0,  MAT_EMPTY, 220, MAT_LAVA,    0, MAT_EMPTY,      0,  0x6E747C, 0x50555C, 0x6E747C, 0x50555C, 0 },
  { "Sand",  KIND_POWDER, 150, 235,   60,   0,   0, 128,  1,   90,  0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0xE2CC86, 0xC7A85E, 0x8F7038, 0x6E5528, 0 },
  { "Dirt",  KIND_POWDER, 140, 150,   12,   0,   0, 192,  2,   80,  0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0x8C6B45, 0x6B4F33, 0x4A3524, 0x33241A, 0 },
  { "Water", KIND_LIQUID, 100,   0,    0,   5,   0,   0,  0,  180,  0,   0,    0,  MAT_EMPTY, 100, MAT_STEAM,   0, MAT_EMPTY,      0,  0x3D7FD1, 0x2C5FA6, 0x3D7FD1, 0x2C5FA6, 0 },
  { "Steam", KIND_GAS,      8,   0,    0,   7, 150,   0,  0,    5,  0, 115,   45,  MAT_WATER,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0xD2DAE6, 0x9AA6B6, 0xD2DAE6, 0x9AA6B6, 0 },
  { "Fire",  KIND_GAS,      3,   0,    0,   3,  60,   0,  0,  200,  0, 235,  120,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY, MAT_WATER, 0xFFE7A0, 0xE8410C, 0xFFE7A0, 0xE8410C, 0 },
  /* Iron is a manufactured solid, so give it a single flat colour (dryA==dryB)
     rather than the per-cell speckle sand and dirt get from a colour range. */
  { "Iron",  KIND_STATIC, 220,   0,    0,   0,   0,   0,  0,  255,  0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0x8A9099, 0x8A9099, 0x8A9099, 0x8A9099, 0 },
  /* Lava spawns as hot as the u8 scale allows and stays molten all the way down
     to 100, which more than doubles the band it must fall through. The real
     work is done by the thermal mass of 3 (holds 8x the heat): it keeps a drawn
     puddle glowing for ~10s instead of ~1s while still conducting at full rate,
     so lava melts, lights and boils things exactly as before. Freezing at 100
     also guarantees any lava is hot enough to boil water. */
  { "Lava",  KIND_LIQUID, 200,   0,    0,   3,   0,   0,  0,  120,  3, 255,  100,  MAT_STONE,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0xF0641E, 0x9A2408, 0xF0641E, 0x9A2408, 0 },
  { "Wood",  KIND_STATIC, 150,   0,    0,   0,   0,   0,  0,   70,  0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,  80, MAT_FIRE,      0,  0x8A5A2C, 0x63401E, 0x8A5A2C, 0x63401E, 0 },
  /* Clone and Void are machines rather than substances, so they get flat
     colours (dryA==dryB==wetA==wetB). For Clone that is also load-bearing: it
     keeps the id of the material it copies in its unused `moisture` byte, and
     the colour LUT is indexed partly by moisture -- identical entries mean the
     stored id can never change how it renders. Both conduct heat poorly so a
     dispenser full of lava does not cook everything around it. */
  { "Clone", KIND_STATIC, 255,   0,    0,   0,   0,   0,  0,   40,  0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0x3FA66A, 0x3FA66A, 0x3FA66A, 0x3FA66A, 0 },
  { "Void",  KIND_STATIC, 255,   0,    0,   0,   0,   0,  0,   40,  0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0x6A2A7A, 0x6A2A7A, 0x6A2A7A, 0x6A2A7A, 0 },
};

u32 g_colorLut[MAT_COUNT * 256];
u32 g_heatLut[256];
u8  g_heatAlpha[256];

/* Piecewise ramp through a set of stops. */
static u32 rampColor(int t) {
    static const int   stopT[4] = {  20,       90,       160,      255       };
    static const u32   stopC[4] = { 0x2A0A04, 0x8C1A06, 0xFF6A10, 0xFFF2C0 };
    if (t <= stopT[0]) return stopC[0];
    for (int i = 1; i < 4; ++i) {
        if (t <= stopT[i]) {
            int span = stopT[i] - stopT[i - 1];
            int f    = ((t - stopT[i - 1]) * 255) / span;
            return lerpColor(stopC[i - 1], stopC[i], f);
        }
    }
    return stopC[3];
}

/* Glow strength. Capped at ~150/255 rather than near-opaque, so hot sand still
   reads as sand instead of a white blob -- the material shows through even at
   the top of the range. */
static int rampAlpha(int t) {
    static const int stopT[4] = {  20,  90, 160, 255 };
    static const int stopA[4] = {   0,  60, 110, 150 };
    if (t <= stopT[0]) return 0;
    for (int i = 1; i < 4; ++i) {
        if (t <= stopT[i]) {
            int span = stopT[i] - stopT[i - 1];
            return stopA[i - 1] + ((stopA[i] - stopA[i - 1]) * (t - stopT[i - 1])) / span;
        }
    }
    return stopA[3];
}

void initMaterials() {
    for (int m = 0; m < MAT_COUNT; ++m) {
        MatInfo& mi = MATS[m];
        /* Liquids keep their fall speed in the spare `moisture` byte, so a
           liquid that also absorbed water would have the two fight over it.
           Nothing does today; this makes the assumption fail loudly rather
           than as mysterious drifting velocities if one ever is added. */
        mi.invCapQ8 = mi.capacity ? (u16)((255 * 256) / mi.capacity) : 0;

        for (int w = 0; w < 16; ++w) {
            /* Representative moisture for this bucket. Bucket 0 must map to
               exactly 0 so genuinely dry material renders fully dry. */
            int moisture = w ? (w * 16 + 8) : 0;
            int wetF = 0;
            if (mi.capacity) {
                wetF = (moisture * mi.invCapQ8) >> 8;
                if (wetF > 255) wetF = 255;
            }
            for (int t = 0; t < 16; ++t) {
                int tintF = t * 17;   /* 0..255 */
                u32 dry = lerpColor(mi.dryA, mi.dryB, tintF);
                u32 wet = lerpColor(mi.wetA, mi.wetB, tintF);
                g_colorLut[(m << 8) | (w << 4) | t] = lerpColor(dry, wet, wetF);
            }
        }
    }

    for (int t = 0; t < 256; ++t) {
        g_heatLut[t]   = rampColor(t);
        g_heatAlpha[t] = (u8)rampAlpha(t);
    }
}
