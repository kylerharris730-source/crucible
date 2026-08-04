#include "worldgen.h"
#include "tree.h"
#include "player.h"
#include <string.h>
#include <math.h>

int g_surfaceY[SIM_W];
int g_stoneY[SIM_W];

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

/* --- the lake ---------------------------------------------------------------
   A basin in the western plains, left of where the character starts, because the
   two things it supplies -- water and the clay in its bed -- are both first-hour
   materials and should be found before the mountain rather than after it.

   Dug as a bowl in the heightmap rather than as a hole punched afterwards, so the
   shore slopes into it and the shape blends with the surrounding country the same
   way the mountain does. The water level is worked out from the RIM once the
   columns exist -- see fillLake -- because a level chosen in advance either floods
   the plains or leaves a dry pit, depending on where the terrain noise happened to
   put the ground that day. */
static const float R_LAKE_C    = 0.105f;   /* centre, as a fraction of the world */
static const float R_LAKE_HALF = 0.048f;   /* half-width */
static const int   LAKE_DEPTH  = 120;      /* cells the bowl drops at its middle */
static const int   LAKE_FREEBOARD = 8;     /* cells of dry rim above the water */
static const int   CLAY_DEPTH  = 26;       /* clay under the lake bed */
/* Sand on the dry shore, over the clay. The world's only source of glass --
   see the note in fillLake for why that matters more than a beach normally
   would. Shallow, because it is a skin over the clay rather than a deposit
   of its own, and the clay under it must stay reachable. */
static const int   SHORE_SAND_DEPTH = 2;

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

    /* The lake basin. Two ramps like the mountain's hump, so both shores slope in
       smoothly and there is no edge anywhere. Multiplied by (1 - hump) so it
       cannot fight the mountain if the two ever move near each other. */
    const float lakeUp   = ramp(u, R_LAKE_C - R_LAKE_HALF, R_LAKE_C - R_LAKE_HALF * 0.35f);
    const float lakeDown = 1.0f - ramp(u, R_LAKE_C + R_LAKE_HALF * 0.35f, R_LAKE_C + R_LAKE_HALF);
    const float bowl = lakeUp * lakeDown;

    float y = (float)SURFACE_Y
            + bowl * (float)LAKE_DEPTH
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


/* Fill the basin, lay clay in its bed, and put a shoreline of sand round it.

   The water level is derived from the RIM rather than chosen: scan the columns
   just outside the bowl, take the LOWEST of them -- the largest surfaceY, since y
   grows downward -- and sit the water LAKE_FREEBOARD below it. That guarantees the
   lake cannot spill over its lowest lip whatever the terrain noise did, which a
   fixed level cannot: the plains wander nearly a hundred cells and any constant
   would flood them on some seeds and leave a dry pit on others.

   Called after the column pass and before caves, so a cave worm's roof clamp sees
   the real ground, and so nothing carves into a body of water from underneath. */
static void fillLake(World& w) {
    const int x0 = (int)((R_LAKE_C - R_LAKE_HALF) * (float)SIM_W);
    const int x1 = (int)((R_LAKE_C + R_LAKE_HALF) * (float)SIM_W);
    if (x0 < PLAY_X0 + 4 || x1 > PLAY_X1 - 4) return;

    /* The lowest point of the rim, taken from a band just outside the bowl on
       each side. */
    int rim = 0;
    for (int x = x0 - 30; x < x0; ++x)      if (g_surfaceY[x] > rim) rim = g_surfaceY[x];
    for (int x = x1 + 1; x <= x1 + 30; ++x) if (g_surfaceY[x] > rim) rim = g_surfaceY[x];
    const int waterY = rim + LAKE_FREEBOARD;

    for (int x = x0; x <= x1; ++x) {
        const int bed = g_surfaceY[x];
        if (bed <= waterY) continue;              /* above the waterline: dry shore */

        /* Clay under the bed. Replaces whatever soil is there, and only soil --
           clay is a powder, so laying it deeper than the soil would leave a
           powder seam inside solid rock waiting to collapse when the world is
           first simulated, which is the same trap caves fell into. */
        const int clayTo = imin(bed + CLAY_DEPTH, g_stoneY[x]);
        for (int y = bed; y < clayTo; ++y) {
            w.setCell(x, y, MAT_CLAY);
            w.setBg(x, y, MAT_CLAY, false);
        }
        /* And the water itself. */
        for (int y = waterY; y < bed; ++y) w.setCell(x, y, MAT_WATER);
    }

    /* A band of clay along the shore just above the waterline too, so you can
       find it without swimming for it -- under a skin of SAND, which is the
       beach this function's own opening comment has always promised and never
       actually laid.

       That omission stopped being cosmetic when glass arrived. Sand is the
       only source of glass, glass is an ingredient of both the Chemistry
       Bench and the Assembly Table, and those two gate the entire upper half
       of the crafting ladder -- so with no sand anywhere in the world, a
       survival game could reach bronze and steel and then stop dead. Measured
       before this: a generated world contained seventeen distinct materials
       and sand was not among them.

       Two cells deep and above the waterline only. Deep enough to be worth
       digging and shallow enough to leave the clay beneath reachable, and dry
       so the beach reads as a beach rather than as a silted lake bed -- the
       clay in the basin proper is untouched, so nothing about the existing
       clay supply moves. */
    for (int x = x0 - 24; x <= x1 + 24; ++x) {
        if (x < PLAY_X0 || x > PLAY_X1) continue;
        const int bed = g_surfaceY[x];
        if (bed > waterY + 40 || bed < waterY - 40) continue;
        const int clayTo = imin(bed + 10, g_stoneY[x]);
        for (int y = bed; y < clayTo; ++y) w.setCell(x, y, MAT_CLAY);
        /* Capped at the soil line for the same reason the clay is: sand is a
           POWDER, and a powder seam laid inside solid rock collapses the
           moment the world is first simulated. */
        const int sandTo = imin(bed + SHORE_SAND_DEPTH, g_stoneY[x]);
        for (int y = bed; y < sandTo; ++y) {
            w.setCell(x, y, MAT_SAND);
            w.setBg(x, y, MAT_SAND, false);
        }
    }
}

/* ==========================================================================
   Caves
   ==========================================================================

   Carved by WORMS -- a few dozen wandering agents, each cutting a disc as it
   goes -- rather than by thresholding 3D noise.

   Noise is the usual answer and it is the wrong one here for two reasons. It
   produces disconnected pockets ("cheese"), and a cave you cannot reach is
   indistinguishable from solid rock, so connectivity would then need solving
   separately. And it gives you no handle on the thing that was actually asked
   for: a cave going into the base of the mountain is one particular tunnel with
   a mouth in a particular place, which is a worm with a start point and a
   heading, and is not expressible as a noise threshold at all. Worms also cost
   only the cells they carve, where a threshold costs a test per underground
   cell -- 7.6M of them.

   --- the number that decides everything ---
   CAVE_MIN_R. The character is PLAYER_H = 30 cells tall, so a tunnel narrower
   than that in its tight dimension is not a cave, it is a wall you are allowed
   to look at. This is the trap in porting cave-generation intuition from games
   whose player is two tiles tall: radius 5 sounds like a generous tunnel and is
   less than half the height of the person meant to walk down it. Every radius
   here is therefore quoted against the body, not against the world.

   --- determinism ---
   Same discipline as columnAt: everything is derived from hash1/fbm on the
   worm's index and its own arc length, and the global rng is never touched.
   Generation must not depend on how many random numbers were drawn before it, or
   the world would change shape according to what the player did last session. */

/* Radius floor, in cells. With CAVE_TALL below this gives a 30x51 bore against
   the current 11x30 body: enough headroom to jump inside a tunnel rather than
   merely fit in one, which is the difference between a cave you traverse and a
   cave you occupy. */
static const int   CAVE_MIN_R    = 15;
/* Caves are ELLIPSES, taller than they are wide, by this factor. "Taller" is the
   thing you actually want more of underground -- headroom to jump, room to drop
   down a chamber without it feeling like a corridor -- and simply raising the
   radius buys height at the cost of hollowing the map out sideways, since area
   goes as the square.

   1.4 -> 1.7. The character grew from 22 to 30 cells tall when it became a rig
   (see PLAYER_H), and this number did not move with it: the minimum bore held
   at 42 cells, which had been "two body-heights of headroom" against the old
   body and quietly became 1.4 against the new one -- the caves did not get
   smaller, the character got bigger under them. 1.7 puts a floor of 51 cells
   under a 30-tall body, which is most of the way back to the original two
   body-heights without changing the width at all: CAVE_MIN_R stretches only
   the vertical axis, so the 30-cell bore width -- already 2.7 bodies across --
   is untouched. */
static const float CAVE_TALL     = 1.7f;
/* How much the radius noise is allowed to swell a tunnel. Chambers are the same
   worm running fat for a while, not a separate feature -- which is why the
   variation is noise along arc length rather than an event. */
static const float CAVE_FAT      = 0.60f;
/* Cells of rock always left between a cave roof and the open air above it, so
   caves do not open the surface into sinkholes. The entrance adit is the one
   exception and asks for it explicitly. */
static const int   CAVE_ROOF     = 40;
/* Cells of STONE always left above a cave roof. Smaller than CAVE_ROOF because
   rock does not need much to hold itself up -- it needs to be rock at all. The
   job of this number is only to keep the ceiling clear of the soil boundary, so
   that a grain of dirt is never the thing above a void. */
static const int   CAVE_ROCK     = 12;
/* Deepest a cave may reach, clear of the world's border wall. */
static void generateTrees(World& w);

static const int   CAVE_FLOOR_Y  = SIM_H - 60;
/* How far a worm turns off its base heading, in radians. Held well under a
   quarter turn so tunnels meander rather than doubling back -- a worm that can
   reverse spends its length re-carving what it already cut. */
static const float CAVE_SWING    = 1.15f;

/* Carve one disc. Cells only: the BACKGROUND is deliberately left alone, so a
   cave shows the natural rock behind it exactly as a dug tunnel does. Setting
   bg to 0 here would open every cave onto the void -- see the note on World::bg
   -- and setting it to placed stone would make the whole underground read as
   somebody's masonry, and would count as a room. */
static void carveDisc(World& w, int cx, int cy, int r) {
    const int ry = (int)((float)r * CAVE_TALL);
    const int r2 = r * r;
    const int x0 = imax(PLAY_X0, cx - r),  x1 = imin(PLAY_X1, cx + r);
    const int y0 = imax(PLAY_Y0, cy - ry), y1 = imin(PLAY_Y1, cy + ry);
    for (int y = y0; y <= y1; ++y) {
        /* Scaled back onto a circle rather than evaluating an ellipse, so the
           test stays the same integer compare it always was. */
        const int dy = (int)((float)(y - cy) / CAVE_TALL);
        for (int x = x0; x <= x1; ++x) {
            const int dx = x - cx;
            if (dx * dx + dy * dy > r2) continue;
            w.setCell(x, y, MAT_EMPTY);
        }
    }
}

/* Walk a worm, carving as it goes. Returns the number of cells visited, and
   optionally samples its path so branches can be started ON it -- which is how
   the entrance is guaranteed to lead somewhere rather than hoping two
   independent worms happen to cross.

   `breachFor` is how many steps at the start may cut through to open sky. Zero
   for every ordinary cave; the entrance adit is the exception. */
struct CavePoint { int x, y; };

static void carveWorm(World& w, u32 seed, float x, float y, float baseAng,
                      int steps, float rBase, int breachFor,
                      CavePoint* path = 0, int* pathLen = 0, int pathMax = 0) {
    const int sampleEvery = steps / (pathMax > 0 ? pathMax : 1) + 1;
    int pinned = 0;
    for (int s = 0; s < steps; ++s) {
        const float t = (float)s;
        /* Heading is the base direction PLUS a noise offset, not an integrated
           random walk. Integrating turns produces spirals: the angle drifts
           without a restoring force and the worm eats its own tail. Offsetting
           from a fixed base keeps a tunnel going somewhere while still
           wandering. */
        const float ang = baseAng + fbm(t / 90.0f, seed, 3) * CAVE_SWING;
        float r = rBase * (1.0f + fbm(t / 55.0f, seed + 4441u, 2) * CAVE_FAT);
        if (r < (float)CAVE_MIN_R) r = (float)CAVE_MIN_R;

        x += cosf(ang);
        y += sinf(ang);

        /* Stay inside the world with room for the disc. */
        if (x < (float)(PLAY_X0 + 4)  || x > (float)(PLAY_X1 - 4)) break;
        if (y > (float)CAVE_FLOOR_Y) y = (float)CAVE_FLOOR_Y;

        /* Keep the roof on. Clamping rather than stopping, because a worm that
           halted the instant it grazed the ceiling would leave caves ending
           abruptly just under the surface, where the player is most likely to be
           looking.

           But clamping alone is wrong too, and the overview showed why: a worm
           heading upward gets held at the ceiling and then runs ALONG it,
           carving a tunnel that follows the mountainside for hundreds of cells at
           a constant depth. Nothing in nature draws a line parallel to a
           hillside, and it was the most artificial thing in the picture.

           So a worm may graze the roof but not live there: once it has been
           pinned for PIN_LIMIT consecutive steps it has genuinely run out of
           room upward and stops. Consecutive is the operative word -- the
           counter resets whenever the worm gets clear, so a tunnel that dips
           under a rise and comes back is unaffected. */
        static const int PIN_LIMIT = 25;
        const int ix = (int)x;
        if (s >= breachFor) {
            /* Measured across the whole width of the disc, not just the column
               under its centre. The centre-only version is the obvious one and it
               leaks: the disc is up to 24 cells wide either side, and on the
               mountain the surface climbs about 1.5 cells per cell, so the ground
               at the disc's uphill edge sits ~36 cells above the ground at its
               centre -- comparable to the entire 40-cell roof. Caves duly poked
               out of the hillside, and the topsoil check measured 158 open cells
               in the first 20 below the surface where it should have found only
               the adit mouth.

               The column that binds is the one with the LARGEST surfaceY, which
               is worth pausing on because the intuition runs the other way: y
               grows downward, so high ground has a SMALL surfaceY and the
               constraint it imposes is weak. Taking the minimum -- "the highest
               ground the disc spans" -- reads correctly in English and is the
               loosest bound of the lot; it made this worse rather than better,
               158 open cells becoming 266. It is the LOWEST ground in the span
               that the disc has to stay beneath.

               Conservative rather than exact: the true bound at horizontal offset
               dx is surfaceY + CAVE_ROOF + sqrt(r^2 - dx^2), since a disc is only
               one cell tall at its edge. Using the full r everywhere buys a
               slightly deeper cave for far less arithmetic, and erring deep is
               the safe direction. */
            int lowSurf = g_surfaceY[ix], lowRock = g_stoneY[ix];
            const int sx0 = imax(PLAY_X0, ix - (int)r), sx1 = imin(PLAY_X1, ix + (int)r);
            for (int sx = sx0; sx <= sx1; ++sx) {
                if (g_surfaceY[sx] > lowSurf) lowSurf = g_surfaceY[sx];
                if (g_stoneY[sx]   > lowRock) lowRock = g_stoneY[sx];
            }
            /* BOTH rules, whichever is stricter. They guard different things and
               neither implies the other. The surface rule keeps caves from showing
               through a hillside; the ROCK rule keeps the ceiling standing up,
               because dirt is a powder and a cave roofed in soil collapses to the
               surface the first time the world is simulated -- see g_stoneY in
               worldgen.h for the measured damage. On the plains the rock rule binds
               (52 cells of soil plus CAVE_ROCK), on the bare mountain the surface
               rule does (no soil at all, so the two coincide and CAVE_ROOF wins). */
            /* Against the disc's HALF-HEIGHT, not its radius: caves are ellipses
               now (see CAVE_TALL) and using r would leave the top of a tall one
               poking CAVE_TALL-1 radii above where the clamp thinks it is. */
            const float halfH = r * CAVE_TALL;
            const float floorY = fmaxf((float)(lowSurf + CAVE_ROOF),
                                       (float)(lowRock + CAVE_ROCK)) + halfH;
            if (y < floorY) { y = floorY; if (++pinned > PIN_LIMIT) break; }
            else pinned = 0;
        }
        if (y > (float)CAVE_FLOOR_Y) break;

        carveDisc(w, ix, (int)y, (int)r);

        if (path && pathLen && *pathLen < pathMax && (s % sampleEvery) == 0) {
            path[*pathLen].x = ix; path[*pathLen].y = (int)y; ++*pathLen;
        }
    }
}

/* The way in. A single adit starting on open ground at the mountain's NEAR foot
   -- the side the player walks in from, since they spawn at a fifth of the way
   across and the mountain begins at 0.46 -- heading into the rock and down.

   It is placed rather than generated, and that is the point of having worms at
   all: "there is a cave you can find at the bottom of the mountain" is a
   statement about one specific place, and no amount of tuning a global cave
   density expresses it. Everything else about it is noise-driven like any other
   worm, so it does not read as a corridor somebody installed. */
static void carveEntrance(World& w, CavePoint* path, int* pathLen, int pathMax) {
    const int mouthX = (int)(R_FOOT_END * (float)SIM_W) + 40;
    /* Start a little UNDER the surface, not on it. Starting exactly at ground
       level puts the disc's top half in the sky and leaves a crater around the
       mouth rather than an opening in a hillside. */
    const float y0 = (float)(g_surfaceY[mouthX] + CAVE_MIN_R);
    /* Rightward and gently down: into the mountain, which rises to the right. */
    carveWorm(w, 20260729u, (float)mouthX, y0, 0.42f, 900, 15.0f,
              /* breachFor */ 60, path, pathLen, pathMax);
}

static void generateCaves(World& w) {
    /* The entrance first, so branches can hang off it. */
    static CavePoint trunk[24];
    int trunkLen = 0;
    carveEntrance(w, trunk, &trunkLen, 24);

    /* Branches off the trunk, so the entrance leads into a system rather than to
       a dead end. Started at sampled points ON the adit, which is the whole
       reason carveWorm reports its path -- relying on independent worms to
       intersect is relying on luck, and the one cave the player is certain to
       find is the one that must not be a cul-de-sac.

       SPARSELY, and at continuous angles, and both of those are corrections to a
       version that produced two obvious starbursts. It branched every 3rd sample
       -- about every 110 cells -- and drew the heading from four discrete
       diagonals, so a short stretch of trunk sprayed tunnels up-left, up-right,
       down-left and down-right at once and the eye read a hub with spokes. The
       origins were not crowded (checked: 2 and 0 worm origins within 120 cells of
       each hub) and the trunk was not doubling back (96% efficient) -- it was the
       BRANCHING RATE against the discreteness of the angles.

       Every 7th sample is roughly one junction per 270 cells, which at 26-cell
       bores is far enough apart to read as separate junctions.

       Headings are biased DOWNWARD, into the rock. An adit's branches wanting to
       climb is what fed the roof-clamp problem above, and it is also just wrong:
       a cave leading in from a hillside should take you deeper, and the way down
       is the thing the player is looking for. */
    for (int i = 2; i < trunkLen; i += 7) {
        const u32 seed = 5150u + (u32)i * 9173u;
        /* 0.35 .. 2.79 rad: every direction with a downward component, excluding
           straight up and the two near-horizontals that would run alongside the
           trunk. Continuous, so no two branches share a heading. */
        const float f = (float)(hash1(i, 771u) % 1000u) / 1000.0f;
        const float ang = 0.35f + f * 2.44f;
        carveWorm(w, seed, (float)trunk[i].x, (float)trunk[i].y, ang,
                  500 + (int)(hash1(i, 313u) % 600u), 14.0f, 0);
    }

    /* And caves everywhere else, so the underground is worth exploring away
       from the one entrance too. Spread by index rather than placed at random so
       coverage is even without any rejection sampling. */
    /* Sparser. 26 worms left the underground reading as more tunnel than rock;
       15 taller ones cover less of it while each being a more substantial thing to
       walk into, which is the trade worth making -- what you want underground is
       fewer, better caves rather than a sponge. */
    const int WORMS = 15;
    for (int i = 0; i < WORMS; ++i) {
        const u32 seed = 31u + (u32)i * 6151u;
        const float fx = ((float)i + 0.5f) / (float)WORMS * (float)SIM_W
                       + (float)(int)(hash1(i, 4523u) % 200u) - 100.0f;
        const int ix = imin(PLAY_X1 - 8, imax(PLAY_X0 + 8, (int)fx));
        /* Depth spread through the rock under this column, never in the soil. */
        const int top  = g_surfaceY[ix] + CAVE_ROOF + 2 * CAVE_MIN_R;
        const int span = imax(1, CAVE_FLOOR_Y - top - 100);
        const float fy = (float)(top + (int)(hash1(i, 8677u) % (u32)span));
        /* Mostly horizontal headings: a vertical tunnel is a shaft you fall
           down, and a cave you can walk along is worth more of them. */
        const float ang = ((hash1(i, 2287u) & 1) ? 3.14159f : 0.0f)
                        + ((float)(int)(hash1(i, 7717u) % 100u) / 100.0f - 0.5f) * 0.7f;
        carveWorm(w, seed, (float)ix, fy, ang,
                  600 + (int)(hash1(i, 1543u) % 900u), 14.0f, 0);
    }
}

/* ==========================================================================
   Ore veins
   ==========================================================================

   Short fat worms, the same machinery as caves but writing rock instead of air,
   and with one rule that matters more than any of the tuning: a vein only
   replaces STONE.

   That single condition does all the work. It keeps ore out of the soil, where a
   powder vein would collapse the moment the world ticked, for the same reason
   caves had to be clamped into rock (see g_stoneY). It keeps ore out of caves, so
   a vein crossing a tunnel does not fill it in with rubble. And -- the good part
   -- it means a vein that happens to cross a cave leaves ore EXPOSED IN THE CAVE
   WALL, which is exactly how a player should find one. None of that needed
   arranging; it is what "stone only" produces.

   Depth bands put copper above iron, so walking down is progression rather than a
   lottery. They overlap deliberately: a hard floor under iron would read as a
   line ruled across the world, and finding your first iron just above where you
   expected copper is a better moment than finding it exactly on schedule. */
static const int VEIN_MIN_R = 3;

static void carveVein(World& w, u32 seed, float x, float y, float baseAng,
                      int steps, float rBase, u8 mat) {
    for (int s = 0; s < steps; ++s) {
        const float t = (float)s;
        const float ang = baseAng + fbm(t / 22.0f, seed, 3) * 2.2f;
        float r = rBase * (1.0f + fbm(t / 14.0f, seed + 733u, 2) * 0.7f);
        if (r < (float)VEIN_MIN_R) r = (float)VEIN_MIN_R;
        x += cosf(ang);
        y += sinf(ang);
        if (x < (float)(PLAY_X0 + 4) || x > (float)(PLAY_X1 - 4)) break;
        if (y < (float)PLAY_Y0 || y > (float)(PLAY_Y1 - 4)) break;

        const int cx = (int)x, cy = (int)y, ir = (int)r, r2 = ir * ir;
        const int x0 = imax(PLAY_X0, cx - ir), x1 = imin(PLAY_X1, cx + ir);
        const int y0 = imax(PLAY_Y0, cy - ir), y1 = imin(PLAY_Y1, cy + ir);
        for (int yy = y0; yy <= y1; ++yy) {
            const int dy = yy - cy;
            for (int xx = x0; xx <= x1; ++xx) {
                const int dx = xx - cx;
                if (dx * dx + dy * dy > r2) continue;
                /* Stone only. See the note above -- this one test is what keeps
                   ore out of soil and out of caves, and what leaves veins showing
                   in the walls of the caves they cross. */
                if (w.at(xx, yy).mat != MAT_STONE) continue;
                w.setCell(xx, yy, mat);
            }
        }
    }
}

static void generateOre(World& w) {
    /* Counts and bands. Copper is shallower and more common; iron is deeper and
       scarcer, matching the yields in initSmelting(). */
    /* --- ore against the three layers --------------------------------------

       Every band here is bounded by a LAYER, not merely by a depth, and that is
       the change the layering forced. It was not true before: iron ran from 420
       to 1900 and, measured over a generated world, 92% of it sat below what is
       now the layer 1 boundary -- its mean depth was 1145 against gold's 1166,
       so the two were the same tier by every measure except the table's
       intention. "Layer 1 has copper and iron" was a description of a world
       that did not exist.

       So: copper, tin, coal and iron are layer 1 and stop above LAYER1_DEPTH;
       gold and titanium are layer 2; tungsten is layer 3 and is the reason
       layer 3 is worth reaching. Nothing crosses a barrier, because an ore that
       crossed one would let a player mine the next tier's metal out of the
       ceiling above it without ever breaking through.

       Iron starts deeper than copper and tin WITHIN layer 1, which is what
       keeps the early ladder a ladder: you meet copper and tin near the top of
       the caves and iron further in, so bronze genuinely precedes iron rather
       than the two arriving together. */
    struct OreBand { u8 mat; int veins; int fromStone; int toStone; float r; u32 salt; };
    static const OreBand BANDS[] = {
        { MAT_COPPER_ORE, 90,   40,  950, 5.0f, 0x1234u },
        { MAT_IRON_ORE,   70,  240, 1000, 4.6f, 0x9ABCu },
        /* Coal. Shallower and more common than either metal, because it is the
           thing you need FIRST and in bulk -- a fuel you have to go as deep for as
           the iron it is meant to smelt would defeat the point of the ladder.
           Fatter veins too: coal comes in seams. */
        { MAT_COAL,      110,   60,  980, 6.0f, 0x5E7Du },
        /* Tin sits right beside copper, shallower than iron -- it exists to be
           alloyed into bronze and the whole point is that both halves of that
           alloy should be reachable at the same time you first go looking for
           metal at all. Nearly as plentiful as copper for the same reason. */
        { MAT_TIN_ORE,    80,   30,  920, 4.4f, 0x2A6Fu },
        /* Gold: rare and in SMALL pockets rather than long veins -- a lower
           radius, not just fewer of them, so finding one reads as a nugget
           rather than a thin smear of ordinary ore. Deeper than either early
           metal, because it answers "copper corrodes here, now what" and that
           question should not arrive before the corrosion does. */
        { MAT_GOLD_ORE,   30, 1150, 1950, 3.0f, 0x77E1u },
        /* Titanium: past iron, sharing the depth band tungsten and the
           hotspots start in -- the smelting note on its MATS row explains why
           that overlap is deliberate rather than incidental. */
        { MAT_TITANIUM_ORE, 48, 1250, 1980, 4.0f, 0xB4A2u },
        /* Tungsten: the deepest ore in the game and sparse to match, sitting
           right where HOT_MIN_DEP begins -- see the note on its MATS row for
           why that proximity to the hotspots is the point, not a coincidence. */
        { MAT_TUNGSTEN_ORE, 26, 2200, 3050, 3.4f, 0xF00Du },
    };
    for (int b = 0; b < (int)(sizeof(BANDS) / sizeof(BANDS[0])); ++b) {
        const OreBand& ob = BANDS[b];
        for (int i = 0; i < ob.veins; ++i) {
            const u32 seed = ob.salt + (u32)i * 4547u;
            /* Spread by index across the world, jittered, like the cave worms --
               even coverage without rejection sampling. */
            const float fx = ((float)i + 0.5f) / (float)ob.veins * (float)SIM_W
                           + (float)(int)(hash1(i, ob.salt + 11u) % 260u) - 130.0f;
            const int ix = imin(PLAY_X1 - 8, imax(PLAY_X0 + 8, (int)fx));
            const int top  = g_stoneY[ix] + ob.fromStone;
            const int span = imax(1, ob.toStone - ob.fromStone);
            int iy = top + (int)(hash1(i, ob.salt + 29u) % (u32)span);
            if (iy > PLAY_Y1 - 20) iy = PLAY_Y1 - 20;
            const float ang = (float)(hash1(i, ob.salt + 41u) % 628u) / 100.0f;
            carveVein(w, seed, (float)ix, (float)iy, ang,
                      40 + (int)(hash1(i, ob.salt + 53u) % 90u), ob.r, ob.mat);
        }
    }
}


/* ==========================================================================
   Lava hotspots
   ==========================================================================

   Blobs of deep rock generated with a MOLTEN BACKDROP, which pins them above
   stone's melting point so they turn to lava on their own and stay that way. See
   g_bgHeat in materials.h for the mechanism; this only chooses where.

   They are the natural high-temperature source, and the balance question they
   raise -- why bother making fuel if you can dig out a lava pocket and use it as a
   furnace -- answers itself from a measurement rather than from a rule. Lava
   delivers about 175 C into a charge, which smelts copper and cannot touch iron's
   190. It sits on exactly the same rung as coal. So a hotspot is a fine free
   copper furnace and a dead end for iron, and fuel remains the only route to the
   top of the ladder. Nothing had to be nerfed to make that true.

   Deliberately DEEP and SPARSE. Deep because a hotspot near the surface would be
   the first thing anyone found and would skip the coal step entirely; sparse
   because the interesting version of a free heat source is one you build a
   settlement around, not one you trip over.

   The depth floor started at 900 and moved to 1300 after playing with it. The
   world gives roughly 1750 cells between the stone line and the bottom, so 900
   put hotspots anywhere in the lower five-sixths of it -- which is deep in
   absolute terms and not deep in the only sense that matters, which is how far
   past everything else you have to go to reach one. At 1300 they sit in the
   bottom quarter, below every ore vein and every cave, and getting to one is a
   trip you outfit for rather than something that happens on the way past. */
static const int HOT_COUNT   = 20;     /* blobs in the whole world */
/* Layer 3's characteristic hazard, and the reason to go there: lava is the only
   thing hot enough to smelt tungsten (213 C, two below the temperature byte's
   ceiling), and tungsten is layer 3's ore. Putting the two in the same layer is
   not a coincidence to be tidied up -- it is what makes the deepest layer worth
   the trip, and what makes it dangerous in the same breath. */
static const int HOT_MIN_DEP = 2200;   /* cells below the stone line, at least */
static const int HOT_R       = 46;     /* radius, in cells */

static void generateHotspots(World& w) {
    for (int i = 0; i < HOT_COUNT; ++i) {
        const float fx = ((float)i + 0.5f) / (float)HOT_COUNT * (float)SIM_W
                       + (float)(int)(hash1(i, 0xB00Bu) % 300u) - 150.0f;
        const int cx = imin(PLAY_X1 - HOT_R - 4, imax(PLAY_X0 + HOT_R + 4, (int)fx));
        const int top = g_stoneY[cx] + HOT_MIN_DEP;
        const int span = imax(1, (PLAY_Y1 - 80) - top);
        int cy = top + (int)(hash1(i, 0xF17Eu) % (u32)span);
        if (cy > PLAY_Y1 - HOT_R - 20) cy = PLAY_Y1 - HOT_R - 20;
        if (cy < top) continue;

        /* An irregular blob rather than a disc -- a perfectly round pocket of lava
           reads as something that was placed. Radius wobbles with angle. */
        const int r2max = (HOT_R * 3 / 2) * (HOT_R * 3 / 2);
        for (int y = cy - HOT_R * 3 / 2; y <= cy + HOT_R * 3 / 2; ++y) {
            if (y < PLAY_Y0 || y > PLAY_Y1) continue;
            for (int x = cx - HOT_R * 3 / 2; x <= cx + HOT_R * 3 / 2; ++x) {
                if (x < PLAY_X0 || x > PLAY_X1) continue;
                const int dx = x - cx, dy = y - cy;
                const int d2 = dx * dx + dy * dy;
                if (d2 > r2max) continue;
                const float wob = 1.0f + fbm((float)(dx + dy) / 18.0f, 0xC0DEu + (u32)i, 3) * 0.35f;
                const float rr = (float)HOT_R * wob;
                if ((float)d2 > rr * rr) continue;
                /* Only where there is rock. A hotspot that reached into a cave
                   would fill it with lava from nowhere; one that reached the soil
                   would cook the surface. */
                if (w.at(x, y).mat != MAT_STONE) continue;
                w.setBg(x, y, MAT_LAVA, false);
                /* Lit here, not left to the backdrop to light. updateHeat is only
                   entered for a cell that is already off ambient, so a hotspot that
                   started cold would never be visited to be warmed -- the floor in
                   g_bgHeat MAINTAINS the heat, it cannot start it. Generation
                   provides the initial state and the backdrop stops it fading. */
                w.temp[y * SIM_W + x] = g_bgHeat[MAT_LAVA];
                w.dirtyPoint(x, y);
            }
        }
    }
}

/* ==========================================================================
   Acid pockets
   ==========================================================================

   The same blob-carving geometry as generateHotspots just above, reused
   rather than copied by eye -- irregular radius wobble, "only where there is
   rock" so a pocket cannot breach into a cave or the soil, everything except
   what actually fills the cell.

   A hotspot PINS a backdrop hot; a pocket REPLACES stone with a real,
   foreground liquid, because acid is something you mine and carry, not a
   temperature a wall holds. That is the one genuine difference and it is
   why this is its own function rather than a flag on the one above.

   Deep and sparse for the same reason hotspots are: found, not tripped over,
   and the chemical route past terrain should cost a real trip to reach.
   Shallower than the hotspot floor, though -- acid does not need to sit next
   to the deepest heat the way tungsten does, and gold, its first real payoff
   material, lives well above HOT_MIN_DEP too. */
/* Layer 2's characteristic hazard, so the floor beneath the first barrier is
   where acid starts. It was 700, which in the taller world with layers is
   halfway up layer 1 -- the early game would meet a pool of something that
   dissolves the world before it had a way to contain or cross one. 1300 puts
   the first pocket just past the first barrier. */
static const int ACID_COUNT = 14;
static const int ACID_MIN_DEP = 1150;
static const int ACID_R = 24;

static void generateAcidPockets(World& w) {
    for (int i = 0; i < ACID_COUNT; ++i) {
        const float fx = ((float)i + 0.5f) / (float)ACID_COUNT * (float)SIM_W
                       + (float)(int)(hash1(i, 0xACDCu) % 300u) - 150.0f;
        const int cx = imin(PLAY_X1 - ACID_R - 4, imax(PLAY_X0 + ACID_R + 4, (int)fx));
        const int top = g_stoneY[cx] + ACID_MIN_DEP;
        const int span = imax(1, (PLAY_Y1 - 80) - top);
        int cy = top + (int)(hash1(i, 0x1ACDu) % (u32)span);
        if (cy > PLAY_Y1 - ACID_R - 20) cy = PLAY_Y1 - ACID_R - 20;
        if (cy < top) continue;

        const int r2max = (ACID_R * 3 / 2) * (ACID_R * 3 / 2);
        for (int y = cy - ACID_R * 3 / 2; y <= cy + ACID_R * 3 / 2; ++y) {
            if (y < PLAY_Y0 || y > PLAY_Y1) continue;
            for (int x = cx - ACID_R * 3 / 2; x <= cx + ACID_R * 3 / 2; ++x) {
                if (x < PLAY_X0 || x > PLAY_X1) continue;
                const int dx = x - cx, dy = y - cy;
                const int d2 = dx * dx + dy * dy;
                if (d2 > r2max) continue;
                const float wob = 1.0f + fbm((float)(dx + dy) / 14.0f, 0xACE5u + (u32)i, 3) * 0.35f;
                const float rr = (float)ACID_R * wob;
                if ((float)d2 > rr * rr) continue;
                /* Stone only -- same reason a vein and a hotspot both insist
                   on it: keeps acid out of caves and out of soil, and what
                   is left showing in a cave wall it happens to cross is
                   exactly how a player should find one. */
                if (w.at(x, y).mat != MAT_STONE) continue;
                w.setCell(x, y, MAT_ACID);
            }
        }
    }
}

/* ==========================================================================
   The layer barriers
   ==========================================================================

   Two solid bands of MAT_STRATUM, at LAYER1_DEPTH and LAYER2_DEPTH below the
   local stone line, sealing one cave layer off from the next.

   Run LAST, after the caves, the ore, the hotspots and the acid, and that order
   is the whole of the guarantee. Every one of those passes carves or replaces
   material, so anything laid before them can be cut through -- a cave worm
   crossing the boundary would open a hole straight to the next layer and there
   would be no way to notice except by falling through one. Laying the band last
   means it OVERWRITES whatever crossed it, and the seal is total by
   construction rather than by every other generator agreeing to respect it.

   It follows the stone line rather than sitting at a fixed y for the same
   reason the ore bands are quoted that way: the surface wanders about a hundred
   cells and the mountain a great deal more, so a flat band would surface
   halfway up a hillside on one seed and sit below its own layer's ore on
   another. The band undulates with the terrain above it, which also reads far
   better -- it looks like geology instead of like a floor somebody installed.

   The wobble is fbm on x alone, so the band's thickness varies a little without
   ever breaking: STRATUM_THICK is a minimum that the noise only ever adds to.
   A barrier with a thin spot is a barrier with a hole in it as soon as somebody
   finds the thin spot. */
static void generateStrata(World& w) {
    for (int x = PLAY_X0; x <= PLAY_X1; ++x) {
        for (int band = 0; band < 2; ++band) {
            const int depth = band == 0 ? LAYER1_DEPTH : LAYER2_DEPTH;
            /* Two octaves at different scales so the band has both a long roll
               and a little local roughness, rather than reading as a sine wave
               somebody stretched across the world. */
            const float wob = fbm((float)x / 260.0f, 0x5A11u + (u32)band * 977u, 3) * 46.0f
                            + fbm((float)x / 70.0f,  0xB0C4u + (u32)band * 331u, 2) * 12.0f;
            const int top = g_stoneY[x] + depth + (int)wob;
            /* fabsf, and it matters: fbm is signed, so adding it raw made the
               band THINNER than STRATUM_THICK wherever the noise went negative.
               Measured before this, the thinnest run was 19 cells against a
               nominal 24 -- not a hole, but the comment above promises a
               minimum the code was not keeping, and a barrier's minimum
               thickness is the only number about it that matters. */
            const int bot = top + STRATUM_THICK
                          + (int)(fabsf(fbm((float)x / 120.0f, 0x77E3u + (u32)band, 2)) * 7.0f);
            for (int y = imax(PLAY_Y0, top); y <= imin(PLAY_Y1, bot); ++y) {
                /* Unconditional. Not "stone only" like every other underground
                   pass -- those are looking for somewhere to put something and
                   should leave caves alone, whereas this one exists precisely
                   to close whatever is in its way. A cave, an ore vein, a lava
                   pocket or an acid pocket that reaches the band all get walled
                   through, which is what makes the seal a guarantee. */
                w.setCell(x, y, MAT_STRATUM);
                /* Backed too, so a band crossing an existing cave does not show
                   that cave's dark through it as if it were a window. */
                w.setBg(x, y, MAT_STRATUM, false);
            }
        }
    }
}

void generateWorld(World& w) {
    w.reset();

    /* --- columns --------------------------------------------------------- */
    for (int x = PLAY_X0; x <= PLAY_X1; ++x) {
        int surf, soil;
        columnAt(x, &surf, &soil);
        g_surfaceY[x] = surf;

        const int stoneTop = surf + soil;
        g_stoneY[x] = stoneTop;
        for (int y = surf; y <= PLAY_Y1; ++y) {
            const u8 m = (y < stoneTop) ? MAT_DIRT : MAT_STONE;
            w.setCell(x, y, m);
            /* Natural backdrop behind natural ground. Without it, digging in
               opens a hole onto the void -- see the note on World::bg. */
            w.setBg(x, y, m, false);
        }

        /* The top of the soil is turf, a BAND of it rather than the single row
           this used to lay down. One row is a green line drawn on the dirt --
           at two screen pixels a cell you cannot tell turf from an outline --
           and the depth is what makes it read as ground with something growing
           in it. Varied by its own noise so the underside of the band is not a
           ruled line parallel to the surface.

           Capped at GRASS_DEPTH because that is how far from air grass can
           live (see world.h): anything laid deeper than that would die back to
           dirt on its first simulated frame, which would look like generation
           getting it wrong and be invisible to read in the code. Capped at the
           soil depth too, so thin mountain soil gets a thin skin and none at
           all where there is no soil -- which is what draws the treeline
           without anything having to know where the treeline is. */
        if (soil > 0) {
            const int band = imin(imin(soil, GRASS_DEPTH),
                                  3 + (int)(fbm((float)x / 34.0f, 6421u, 2) * 2.4f));
            for (int y = surf; y < surf + imax(1, band); ++y) w.setCell(x, y, MAT_GRASS);
        }
    }
    g_surfaceY[0] = g_surfaceY[PLAY_X0];
    g_surfaceY[SIM_W - 1] = g_surfaceY[PLAY_X1];
    g_stoneY[0] = g_stoneY[PLAY_X0];
    g_stoneY[SIM_W - 1] = g_stoneY[PLAY_X1];

    /* The lake before the caves, so a worm's roof clamp is reading real ground and
       nothing tunnels up into a body of water from beneath. */
    fillLake(w);

    /* Caves, after the columns exist -- every worm reads g_surfaceY to know how
       much roof to leave, so this cannot run earlier. Before the zone pass only
       by convention: zones depend on the heightmap, which caves do not change. */
    generateCaves(w);
    /* Ore AFTER caves, and the order is the whole reason veins work: carveVein
       only replaces stone, so by running second a vein that crosses a tunnel
       leaves the tunnel open and the ore showing in its wall. Reversed, caves
       would carve the ore back out again and nothing would ever be visible from
       inside one. */
    generateOre(w);
    /* Hotspots and acid pockets last, so they can test for stone and skip
       anything caves or veins already claimed. */
    generateHotspots(w);
    generateAcidPockets(w);

    /* After every pass that carves or replaces underground material, so the
       seal cannot be cut by one of them. See generateStrata. */
    generateStrata(w);

    generateTrees(w);

    /* --- zones ------------------------------------------------------------
       A chunk is underground only if it is ENTIRELY below the ground, using
       the deepest surface across its own columns. That keeps the sky/cave join
       buried inside rock: the chunk the surface passes through stays sky, and
       the transition happens at its lower edge, under up to 32 cells of
       material. Doing it per chunk column rather than from one global depth is
       the entire point of zones being labels -- the mountain's summit is 780
       cells above the plains, so no single depth could serve both. */
    for (int cx = 0; cx < CHUNKS_X; ++cx) {
        int deepest = 0, stoneDeepest = 0;
        const int x0 = cx << CHUNK_SHIFT;
        for (int x = x0; x < x0 + CHUNK && x < SIM_W; ++x) {
            if (g_surfaceY[x] > deepest)      deepest = g_surfaceY[x];
            if (g_stoneY[x]   > stoneDeepest) stoneDeepest = g_stoneY[x];
        }
        const int firstUnder = ((deepest >> CHUNK_SHIFT) + 1);
        /* Which cave layer a chunk belongs to, on the same "entirely below the
           line" rule the sky/underground split uses -- a chunk straddling a
           barrier counts as the SHALLOWER layer, so the boundary lands at the
           lower edge of the barrier's own chunk and a layer never starts in mid
           air. Measured from the deepest stone line in the chunk's columns, for
           the same reason the barriers themselves are: the layers have to
           undulate with the terrain or they do not line up with the rock they
           are supposed to be dividing. */
        const int layer2Cy = ((stoneDeepest + LAYER1_DEPTH) >> CHUNK_SHIFT) + 1;
        const int layer3Cy = ((stoneDeepest + LAYER2_DEPTH) >> CHUNK_SHIFT) + 1;
        for (int cy = 0; cy < CHUNKS_Y; ++cy) {
            u8 z = ZONE_SKY;
            if      (cy >= layer3Cy)  z = ZONE_LAYER3;
            else if (cy >= layer2Cy)  z = ZONE_LAYER2;
            else if (cy >= firstUnder) z = ZONE_LAYER1;
            w.zone[cy * CHUNKS_X + cx] = z;
        }
    }
}

/* ==========================================================================
   Trees
   ==========================================================================

   Sparse, on turf, and nowhere else. A tree is about 200 cells across and 320
   tall, so "sparse" is not a stylistic preference here -- at anything closer
   than a couple of hundred cells the crowns merge into one continuous roof and
   the plains stop being plains.

   Placed on a JITTERED LATTICE rather than by rejection sampling. A lattice
   guarantees the minimum spacing that keeps trees from merging, and the jitter
   is what stops it reading as an orchard; picking positions at random and
   discarding the ones too close to a neighbour gets the same look for more work
   and gives no guarantee at all about the worst case.

   Everything is drawn from hash1 on the lattice index, so a world's trees are
   as reproducible as its terrain -- see the note on determinism in tree.h. */
static const int TREE_SPACING = 620;   /* lattice pitch, in cells */
static const int TREE_JITTER  = 240;   /* how far a tree may wander from it */
/* Chance out of 255 that a given lattice cell has a tree at all.

   These three together give three or four oaks in the whole world, which is
   what the world wants rather than what looks generous on paper. An oak is 320
   cells tall with a 200-cell crown and it throws a shadow to match: six of them
   made the plains somewhere you walked between trees rather than open country
   with landmarks in it. At this density a tree is a place. */
static const int TREE_CHANCE  = 150;
/* Tries per lattice cell before giving up on it.
   Without retries, a cell whose one jittered position happened to land on
   rock, on a slope or in the lake produced nothing -- and since only 43% of
   the world's columns can hold a tree, most cells produced nothing. Measured,
   the whole world got ONE tree. Retrying is what turns "43% of columns are
   usable" into "usable country gets planted". */
static const int TREE_TRIES   = 14;
/* No two trunks closer than this. Enforced against the last tree placed rather
   than left to lattice arithmetic, which is what lets the jitter be generous:
   a pitch and a jitter that guarantee a gap on their own have to be so tight
   that the row reads as an orchard. A crown is about 200 across, so this keeps
   most neighbours clear of each other while allowing the occasional pair that
   have grown together -- which is what a wood actually looks like. */
static const int TREE_MIN_GAP = 190;

/* Where trees may stand. Deliberately strict, and every clause is a place a
   tree looked wrong rather than a place it was merely unusual. */
static bool treeSpotOk(const World& w, int x) {
    if (x < PLAY_X0 + 120 || x > PLAY_X1 - 120) return false;

    const int surf = g_surfaceY[x];
    /* Turf, not bare rock or sand: the treeline draws itself from where the
       grass pass decided the soil was too thin, which is exactly the line a
       tree should stop at. */
    if (w.at(x, surf).mat != MAT_GRASS) return false;

    /* Not in the lake, and not on its shore where a trunk would stand in
       water. The lake is filled by now, so this can simply look. */
    for (int d = -40; d <= 40; d += 8)
        for (int dy = -2; dy <= 6; ++dy) {
            const int sx = imin(PLAY_X1, imax(PLAY_X0, x + d));
            const int sy = imin(PLAY_Y1, g_surfaceY[sx] + dy);
            if (MATS[w.at(sx, sy).mat].kind == KIND_LIQUID) return false;
        }

    /* Flat enough. A tree on a steep slope buries half its trunk on the uphill
       side and hangs the other half over nothing, and the mountain flank is
       where that happens. 30 cells of rise across its own width is about the
       limit before it reads as a mistake. */
    int lo = SIM_H, hi = 0;
    for (int d = -80; d <= 80; d += 10) {
        const int sx = imin(PLAY_X1, imax(PLAY_X0, x + d));
        if (g_surfaceY[sx] < lo) lo = g_surfaceY[sx];
        if (g_surfaceY[sx] > hi) hi = g_surfaceY[sx];
    }
    if (hi - lo > 30) return false;

    /* Room overhead. A crown that would be clipped by the top of the world is a
       tree with its head cut off, and the mountain's summit is high enough for
       that to be a real case rather than a theoretical one. */
    if (surf - treeMaxHeight() - 60 < PLAY_Y0) return false;

    /* Nothing hollow underneath. A cave roof will not hold a tree up, and a
       trunk hanging into a cavern is the sort of thing you notice once and
       never stop noticing. */
    for (int dy = 1; dy <= 40; ++dy)
        if (w.at(x, imin(PLAY_Y1, surf + dy)).mat == MAT_EMPTY) return false;

    return true;
}

int g_treesPlanted = 0;

static void generateTrees(World& w) {
    treesClear();
    int planted = 0;
    int lastX = -TREE_MIN_GAP * 4;
    for (int i = 0; i * TREE_SPACING < SIM_W; ++i) {
        if ((int)(hash1(i, 0x7EEEu) % 255u) >= TREE_CHANCE) continue;

        const int base = i * TREE_SPACING + TREE_SPACING / 2;
        for (int t = 0; t < TREE_TRIES; ++t) {
            const u32 h = hash1(i * 64 + t, 0xB4A5u);
            const int x = base + (int)(h % (u32)(TREE_JITTER * 2 + 1)) - TREE_JITTER;
            if (x < PLAY_X0 || x > PLAY_X1) continue;
            if (x - lastX < TREE_MIN_GAP) continue;
            if (!treeSpotOk(w, x)) continue;

            /* Grown to full size immediately -- this is a world that has been
               here a while, not one that starts as saplings. treeGrowNow leaves
               no entity behind, so a generated forest costs nothing per frame.

               Salted on POSITION, so the same world always grows the same wood
               and two trees that land near each other still differ. */
            /* Oak only, for now. Birch exists as a seed you plant rather than
               as scenery -- see TREE_KINDS. */
            treeGrowNow(w, x, g_surfaceY[x] - 1, hash1(x, 0x3F0Eu), TREE_OAK);
            lastX = x;
            ++planted;
            break;
        }
    }
    g_treesPlanted = planted;
}

void worldSpawnPoint(float* outX, float* outY) {
    /* A fifth of the way in: well inside the plains, with the mountain
       visible off to the right as something to walk toward. */
    const int x = SIM_W / 5;
    if (outX) *outX = (float)x;
    if (outY) *outY = (float)(g_surfaceY[x] - PLAYER_H);
}
