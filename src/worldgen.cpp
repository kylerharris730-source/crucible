#include "worldgen.h"
#include "player.h"
#include <string.h>

int g_surfaceY[SIM_W];

/* --- 1D value noise --------------------------------------------------------
   Hash the integer lattice, smoothstep between neighbours. Deterministic from
   the seed alone and independent of the global rng, which matters: generation
   must not depend on how many random numbers happened to be drawn before it,
   or the same world would come out different depending on what the player did
   in the previous session. */
static u32 hash1(int x, u32 seed) {
    u32 h = (u32)x * 374761393u + seed * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static float noiseAt(float x, u32 seed) {
    const int i = (int)(x < 0 ? x - 1.0f : x);
    const float f = x - (float)i;
    const float a = (float)(hash1(i,     seed) & 0xFFFF) / 65535.0f;
    const float b = (float)(hash1(i + 1, seed) & 0xFFFF) / 65535.0f;
    const float t = f * f * (3.0f - 2.0f * f);   /* smoothstep */
    return a + (b - a) * t;
}

/* Several octaves, each half the amplitude and twice the frequency. Returns
   roughly -1..1. */
static float fbm(float x, u32 seed, int octaves) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f, freq = 1.0f;
    for (int o = 0; o < octaves; ++o) {
        sum  += (noiseAt(x * freq, seed + (u32)o * 7919u) * 2.0f - 1.0f) * amp;
        norm += amp;
        amp  *= 0.5f;
        freq *= 2.0f;
    }
    return sum / norm;
}

/* Smooth 0..1 ramp between two x positions. Used to blend one region's terrain
   parameters into the next, so no boundary is ever a straight edge. */
static float ramp(float x, float a, float b) {
    if (b <= a) return x >= b ? 1.0f : 0.0f;
    float t = (x - a) / (b - a);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

/* --- the regions -----------------------------------------------------------
   Fractions of the world's width. They overlap at the edges, which is where
   the blending happens.

       0.00 .. 0.34   grassy plains -- gentle, wavy, deep soil
       0.34 .. 0.46   foothills     -- the plains rising, soil thinning
       0.46 .. 0.72   the mountain  -- bare stone, and the way underground
       0.72 .. 1.00   high flats    -- dry, thin-soiled, quiet

   The mountain is the tall landmark you can see from the plains and walk to,
   which is the whole reason it is a quarter of the world wide rather than a
   spike: at 512 cells of view you should be able to see it from a long way
   off and still take a while to get there. */
static const float R_PLAINS_END   = 0.34f;
static const float R_FOOT_END     = 0.46f;
static const float R_MOUNT_PEAK   = 0.59f;
static const float R_MOUNT_END    = 0.72f;

static const int MOUNTAIN_HEIGHT = 780;   /* cells above the plains */
static const int SOIL_PLAINS     = 46;    /* dirt depth before stone */
static const int SOIL_FLATS      = 16;

/* Height of the ground at a given column, and how deep the soil is there. */
static void columnAt(int x, int* surfaceOut, int* soilOut) {
    const float u  = (float)x / (float)SIM_W;
    const float fx = (float)x;

    /* Plains: broad rolling swells, a medium wave over them, and a fine
       wobble on top. Three scales rather than one because a single frequency
       reads as corrugation -- the eye finds the repeat immediately -- and
       because rolling country is exactly the sum of a long slow shape and a
       short sharp one.

       The amplitude is large on purpose: at 34 cells the plains measured flat
       from anywhere you could stand, since a 34-cell rise spread over 600
       cells of walking is a gradient you cannot see. 90 gives hills you crest. */
    const float plains = fbm(fx / 700.0f, 1337u, 2) * 58.0f
                       + fbm(fx / 260.0f, 4211u, 2) * 26.0f
                       + fbm(fx / 90.0f,  8837u, 2) * 9.0f;

    /* The mountain: a single broad hump centred on R_MOUNT_PEAK, roughened so
       its flanks are not a clean curve. The hump is built from two ramps --
       up into the peak, down out of it -- which keeps it smooth at both feet
       without needing a separate blend. */
    const float up   = ramp(u, R_FOOT_END - 0.04f, R_MOUNT_PEAK);
    const float down = 1.0f - ramp(u, R_MOUNT_PEAK, R_MOUNT_END + 0.03f);
    const float hump = up * down;
    const float rough = fbm(fx / 210.0f, 99173u, 4) * 70.0f * hump
                      + fbm(fx / 70.0f,  55411u, 3) * 22.0f * hump;

    /* High flats past the mountain: a plateau well above the plains, quiet. */
    const float flats = ramp(u, R_MOUNT_END, R_MOUNT_END + 0.12f);
    const float flatH = fbm(fx / 400.0f, 7717u, 2) * 16.0f;

    float y = (float)SURFACE_Y
            + plains * (1.0f - hump)
            - hump * (float)MOUNTAIN_HEIGHT
            - rough
            - flats * 110.0f + flats * flatH;

    if (y < 40.0f)               y = 40.0f;
    if (y > (float)SIM_H - 400)  y = (float)SIM_H - 400;
    *surfaceOut = (int)y;

    /* Soil thins as the ground rises: the mountain is bare stone, which is
       both how mountains look and what makes it read as the way into the rock
       rather than as a very large hill. */
    const float bare = hump > 0.25f ? 1.0f : hump / 0.25f;
    float soil = (float)SOIL_PLAINS * (1.0f - bare)
               + (float)SOIL_FLATS * flats * (1.0f - bare);
    soil += fbm(fx / 90.0f, 31337u, 2) * 6.0f;
    if (soil < 0.0f) soil = 0.0f;
    *soilOut = (int)soil;
}

void generateWorld(World& w) {
    w.reset();

    /* --- columns --------------------------------------------------------- */
    for (int x = PLAY_X0; x <= PLAY_X1; ++x) {
        int surf, soil;
        columnAt(x, &surf, &soil);
        g_surfaceY[x] = surf;

        const int stoneTop = surf + soil;
        for (int y = surf; y <= PLAY_Y1; ++y) {
            const u8 m = (y < stoneTop) ? MAT_DIRT : MAT_STONE;
            w.setCell(x, y, m);
            /* Natural backdrop behind natural ground. Without it, digging in
               opens a hole onto the void -- see the note on World::bg. */
            w.setBg(x, y, m, false);
        }

        /* The very top of soil is turf. Only where there IS soil: the mountain
           is stone to the surface and gets none, which is what makes the
           treeline read without anything having to draw one. */
        if (soil > 0) w.setCell(x, surf, MAT_GRASS);
    }
    g_surfaceY[0] = g_surfaceY[PLAY_X0];
    g_surfaceY[SIM_W - 1] = g_surfaceY[PLAY_X1];

    /* --- zones ------------------------------------------------------------
       A chunk is underground only if it is ENTIRELY below the ground, using
       the deepest surface across its own columns. That keeps the sky/cave join
       buried inside rock: the chunk the surface passes through stays sky, and
       the transition happens at its lower edge, under up to 32 cells of
       material. Doing it per chunk column rather than from one global depth is
       the entire point of zones being labels -- the mountain's summit is 780
       cells above the plains, so no single depth could serve both. */
    for (int cx = 0; cx < CHUNKS_X; ++cx) {
        int deepest = 0;
        const int x0 = cx << CHUNK_SHIFT;
        for (int x = x0; x < x0 + CHUNK && x < SIM_W; ++x)
            if (g_surfaceY[x] > deepest) deepest = g_surfaceY[x];
        const int firstUnder = ((deepest >> CHUNK_SHIFT) + 1);
        for (int cy = 0; cy < CHUNKS_Y; ++cy)
            w.zone[cy * CHUNKS_X + cx] = (u8)(cy >= firstUnder ? ZONE_UNDER : ZONE_SKY);
    }
}

void worldSpawnPoint(float* outX, float* outY) {
    /* A fifth of the way in: well inside the plains, with the mountain
       visible off to the right as something to walk toward. */
    const int x = SIM_W / 5;
    if (outX) *outX = (float)x;
    if (outY) *outY = (float)(g_surfaceY[x] - PLAYER_H);
}
