#include "world.h"
#include <string.h>
#include <windows.h>

World g_world;

/* ======================================================================
   Simulation lanes
   ----------------------------------------------------------------------
   The scan is split into full-height vertical stripes that run on several
   threads at once. See RULE_WRITE_REACH_X in world.h for why stripes and
   not tiles, and for the audit that says two stripes a stripe apart cannot
   touch the same cell.

   Everything the scan writes that is NOT a cell has to be dealt with here,
   because those are the places where two lanes would otherwise collide:

     the RNG          per thread, seeded per stripe (see common.h)
     pocket budget    per stripe rather than per world
     active count     per thread, summed afterwards
     sprouts, felled  staged per STRIPE and merged in stripe order

   Staged per stripe rather than per thread, and that distinction is the
   whole reason the result does not depend on the core count: which thread
   picks up a stripe is a race, so merging in thread order would make the
   answer depend on who got there first. Stripe order is fixed.

   A lane's stripe is -1 everywhere outside the parallel scan -- worldgen,
   the brush, devices, entities -- and those callers, holding the main lane,
   write straight through to the world arrays exactly as they always did.
   ====================================================================== */
/* Which stripe this lane is on, or -1 when the lane is not in the scan at
   all -- worldgen, the brush, devices, entities. Those callers get the main
   lane, whose stripe stays -1, and their sprouts and felled chunks go
   straight into the world arrays exactly as they always did. */


static i32 g_stripeSprout[STRIPE_COUNT][World::MAX_SPROUTS];
static int g_stripeSproutN[STRIPE_COUNT];
static i32 g_stripeFelled[STRIPE_COUNT][World::MAX_FELLED];
static int g_stripeFelledN[STRIPE_COUNT];
static int g_stripeActive[STRIPE_COUNT];


/* ======================================================================
   Dirty rectangle bookkeeping
   ====================================================================== */

static void clearDirty(Chunk* c) {
    for (int i = 0; i < CHUNK_COUNT; ++i) {
        c[i].minX = SIM_W; c[i].minY = SIM_H;
        c[i].maxX = -1;    c[i].maxY = -1;
    }
}

/* ======================================================================
   Per-lane dirty rectangles
   ----------------------------------------------------------------------
   Two lanes running at once can want to widen the SAME chunk's rectangle
   even when the cells they wrote are nowhere near each other, because a
   rectangle is recorded per chunk and a chunk is 32 cells wide. That, and
   not the physics reach, was what forced stripes to be six chunks across --
   and six-chunk stripes are why threading did nothing for anything smaller
   than the screen.

   So each lane accumulates into its own plane and they are unioned into
   next[] once the scan is done. Union of min/max is commutative and
   associative, so merge order cannot change the answer -- which is why
   these are per THREAD, cheaply, where sprouts and felled chunks have to be
   per stripe.

   `touched` is what makes the merge affordable: without it every merge
   would sweep 36,864 entries per lane per frame, which is exactly the kind
   of whole-grid pass world.h forbids. A rectangle that is empty is not in
   the list, so emptiness IS the membership test and no epoch is needed.
   ====================================================================== */
struct DirtyLog {
    Chunk rect[CHUNK_COUNT];
    i32   touched[CHUNK_COUNT];
    int   n;
};
static DirtyLog g_dirtyLog[MAX_LANE_THREADS];

/* 0 means "write straight to next[]", which is what everything outside the
   parallel scan does -- worldgen, the brush, devices, entities. */
/* Set while a lane is inside the scan; null means "write straight to
   next[]", which is what the main lane does between steps. */

/* The planes start as bss, so every rectangle reads minX 0, maxX 0 -- and
   that is NOT empty, it is a one-cell rectangle in the corner of the world.
   Emptiness is the membership test for `touched`, so an uninitialised plane
   silently absorbs every update and offers none of them to the merge.
   Measured, that looks exactly like the simulation switching itself off:
   0.18 ms a frame and ten live chunks in a scene that should have three
   hundred. */
static void dirtyLogInit(void) {
    static bool done = false;
    if (done) return;
    for (int i = 0; i < MAX_LANE_THREADS; ++i) clearDirty(g_dirtyLog[i].rect);
    done = true;
}

static void dirtyLogClear(DirtyLog& d) {
    for (int i = 0; i < d.n; ++i) {
        Chunk& c = d.rect[d.touched[i]];
        c.minX = SIM_W; c.minY = SIM_H; c.maxX = -1; c.maxY = -1;
    }
    d.n = 0;
}

/* Mark a span and the ring of cells around it as needing simulation next
   frame. The margin matters: when a cell moves away, whatever was resting on
   it has to get another look. The box can straddle several chunks, so each
   overlapped chunk absorbs only its own clipped slice of it. */
/* Pressure can lift a reasonably deep column cheaply, while the more
   expensive sideways search stays tightly local. These are deliberately two
   limits: the common case is a straight column of water above a steam pocket,
   and making that pay for a square flood fill would punish boilers for no
   visual benefit.

   The pocket ray is now two limits rather than one, and the split is what
   makes the parallel scan possible. Both were 512, and 512 was never a
   measured number -- it was "far enough that no boiler anyone builds hits
   it". That is a fine way to pick a bound right until the bound decides
   whether the sim can be split across cores.

   VERTICALLY it has to stay long. A steam pocket under a deep lake routes
   pressure up its own column to the surface, and tests/live_grace_test.cpp
   builds exactly that: 160 cells of water over a 100-cell steam column,
   with the charge injected 75 cells down inside it. At 64 the ray cannot
   see out of the steam, the pocket never finds an outlet, and it re-searches
   every frame -- measured, 12.9% of frames missed the budget where the same
   scene at 512 missed 1.9%. Cutting this was a straight loss twice over.

   HORIZONTALLY it can be short, and that costs nothing anyone will see: a
   flat pocket looking 64 cells sideways for a vent is already looking
   further than the widest boiler in the game. What it buys is the whole
   parallel scan -- the sim is split into full-height vertical stripes, so a
   write straight up or down stays inside its own stripe however long it is,
   and only the SIDEWAYS reach has to fit in a stripe. See RULE_WRITE_REACH_X
   in world.h for the audit this feeds. */
static const int GAS_PRESSURE_VERTICAL_REACH = 512;
static const int GAS_PRESSURE_LIQUID_RADIUS  = 16;
static const int GAS_PRESSURE_POWDER_REACH   = 8;
static const int GAS_PRESSURE_POCKET_RAY_V   = 512;
static const int GAS_PRESSURE_POCKET_RAY_H   = 40;
static const int GAS_PRESSURE_POCKET_RADIUS  = 32;
static const int GAS_PRESSURE_POCKET_NODES   = 2048;
static const int GAS_PRESSURE_POCKET_BUDGET  = 128;
static const int GAS_PRESSURE_EXPANSION_BURST = 5;

/* Every scratch buffer the gas rules use, in one thread-local object.

   ONE object, and one reference fetched at the top of each function that
   needs it, because thread-local here is __emutls_get_address -- an opaque
   call the compiler cannot hoist out of a loop. Left as separate
   `static __thread` arrays, the breadth-first walks paid that call on every
   single indexed access, and the sim went from 10.5 ms a frame to 18.2. The
   arrays are identical; only the number of times their address is asked for
   changed.

   pressureRoutes rides along for the same reason: it is read and decremented
   from two different functions, and one fetch each is cheaper than three. */
struct LaneScratch {
    int pressureRoutes;
    u16 pocketSeen[(GAS_PRESSURE_POCKET_RADIUS * 2 + 1) *
                   (GAS_PRESSURE_POCKET_RADIUS * 2 + 1)];
    i16 pocketQueue[GAS_PRESSURE_POCKET_NODES];
    u16 pocketEpoch;
    i16 parent[(GAS_PRESSURE_LIQUID_RADIUS * 2 + 1) *
               (GAS_PRESSURE_LIQUID_RADIUS * 2 + 1)];
    u16 parentEpoch[(GAS_PRESSURE_LIQUID_RADIUS * 2 + 1) *
                    (GAS_PRESSURE_LIQUID_RADIUS * 2 + 1)];
    i16 bfsQueue[(GAS_PRESSURE_LIQUID_RADIUS * 2 + 1) *
                 (GAS_PRESSURE_LIQUID_RADIUS * 2 + 1)];
    u16 bfsEpoch;
};
/* ONE per lane, passed by reference rather than found in thread-local
   storage, and that is a performance decision with a number behind it.
   MinGW's __thread is emulated -- __emutls_get_address, a real call on every
   single access -- and the first version of this reached for the lane that
   way. Measured on the lava-into-water transient, the calls in dirtyArea
   alone cost 1.52 ms of a 14.9 ms step, and all the thread-local reads
   together came to about three. A reference costs a register.

   It is also the better design for a reason that has nothing to do with
   speed: a function that can touch lane state now says so in its signature,
   so adding a rule that needs a lane is a compile error rather than a race
   nobody notices until it corrupts a cell on somebody else's machine. */
struct Lane {
    /* A POINTER, and that is the whole design of it. A stripe lane points at
       its own storage, because two stripes drawing from one stream would be
       a race and would make the world depend on the core count. The MAIN
       lane points at g_rng, because everything holding the main lane --
       devices, entities, the brush, the save loader -- is drawing from the
       world's real stream, the one codec.cpp serialises. Pointing it at lane
       storage instead was measured: two behaviour tests went red, and the
       reason they did is that a device placing a cell had stopped advancing
       the stream a save or a join would later restore from. */
    u32         rngOwn;
    u32*        rng;
    int         stripe;          /* -1 outside the scan */
    DirtyLog*   dirty;           /* null: write through to next[] */
    LaneScratch scratch;

    /* Constructed rather than left as bss, and both fields it sets are
       load-bearing in a way that is invisible until it is not.

       A stripe of 0 is a real stripe, so a zeroed main lane makes every
       call from worldgen, the brush and the devices look like it came from
       inside the scan -- sprouts and felled chunks get staged for a merge
       that only happens during a step, and callers that never step at all
       simply lose them. And an xorshift seeded with zero stays zero for
       ever, so every "random" choice on the main lane collapses to the same
       branch. Measured by two tests going red: a hive stopped extruding
       honey and a dropped spark stopped starting its pulse.

       The scratch arrays stay untouched here on purpose: static storage is
       zeroed before any constructor runs, which is exactly what their epoch
       counters want. */
    Lane() : rngOwn(0x9E3779B9u), rng(&rngOwn), stripe(-1), dirty(0) {}
};
static Lane g_lane[MAX_LANE_THREADS];

/* Lane zero is the main thread's, and it is also one of the scan's lanes --
   the caller of step() works as a lane rather than watching the others do
   it. Rebinding here rather than trusting whoever ran last: outside the scan
   the main lane draws from the world's stream. */
Lane& simMainLane(void) { g_lane[0].rng = &g_rng; return g_lane[0]; }

/* --- the entry points for callers with no lane ------------------------
   Everything outside the simulation runs on the main thread, so it gets
   the main lane. Kept to the handful the rest of the game actually calls:
   a forwarder for every method would throw away what the Lane parameter is
   for, which is saying in the signature which code can run in a stripe. */
void World::dirtyArea(int x0, int y0, int x1, int y1) {
    dirtyArea(simMainLane(), x0, y0, x1, y1);
}
void World::dirtyPoint(int x, int y) { dirtyPoint(simMainLane(), x, y); }
void World::setCell(int x, int y, u8 mat) { setCell(simMainLane(), x, y, mat); }
void World::swapMat(int x, int y, u8 mat) { swapMat(simMainLane(), x, y, mat); }
void World::breakCell(int x, int y) { breakCell(simMainLane(), x, y); }
bool World::liftColumn(int x, int y, int maxLift) {
    return liftColumn(simMainLane(), x, y, maxLift);
}

void World::dirtyArea(Lane& L, int x0, int y0, int x1, int y1) {
    x0 -= 1; y0 -= 1; x1 += 1; y1 += 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SIM_W - 1) x1 = SIM_W - 1;
    if (y1 > SIM_H - 1) y1 = SIM_H - 1;

    int cx0 = x0 >> CHUNK_SHIFT, cx1 = x1 >> CHUNK_SHIFT;
    int cy0 = y0 >> CHUNK_SHIFT, cy1 = y1 >> CHUNK_SHIFT;

    /* Fetched once for the whole rectangle rather than once per chunk:
       thread-local is a function call on this toolchain, and this is one of
       the most-called functions in the engine. */
    DirtyLog* const log = L.dirty;

    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            const int idx = cy * CHUNKS_X + cx;
            int bx0 = cx << CHUNK_SHIFT, by0 = cy << CHUNK_SHIFT;
            int ax0 = imax(x0, bx0),               ay0 = imax(y0, by0);
            int ax1 = imin(x1, bx0 + CHUNK - 1),   ay1 = imin(y1, by0 + CHUNK - 1);
            Chunk& c = log ? log->rect[idx] : next[idx];
            if (log && c.minX > c.maxX) log->touched[log->n++] = idx;
            if (ax0 < c.minX) c.minX = ax0;
            if (ay0 < c.minY) c.minY = ay0;
            if (ax1 > c.maxX) c.maxX = ax1;
            if (ay1 > c.maxY) c.maxY = ay1;
        }
    }
}

/* ======================================================================
   Setup and editing
   ====================================================================== */

/* 4-neighbourhood, shared by everything that samples around a cell. */
static const int NB_DX[4] = {  0,  0, -1,  1 };
static const int NB_DY[4] = { -1,  1,  0,  0 };

/* 8-neighbourhood, for the few rules that want diagonal contact too. */
static const int NB8_DX[8] = {  0,  0, -1,  1, -1,  1, -1,  1 };
static const int NB8_DY[8] = { -1,  1,  0,  0, -1, -1,  1,  1 };

/* A quarter of one legacy density point. Fixed-point thermal density would
   otherwise make two almost-identical parcels swap every time heat jitters by
   one degree, turning a calm interface into permanent pixel vibration. */
static const int DENSITY_SWAP_EPS_Q8 = 64;


/* The stream, moved off the global and onto the lane. xorshift32, unchanged
   -- three shifts and three xors, and the same sequence a given seed always
   produced. */
static inline u32 lrand(Lane& L) {
    u32 x = *L.rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *L.rng = x;
    return x;
}
static inline bool lchance(Lane& L, u32 p) { return (lrand(L) & 0xFF) < p; }
static inline u32  lbits(Lane& L, u32 n)   { return lrand(L) >> (32 - n); }
/* --- how far a buoyant gas climbs in one frame ------------------------------
   Cells, when the run is clear. One, before this, and that is what made steam
   feel sluggish: a parcel spends about eight frames in nine doing a diffusion
   step (see the jitter note in materials.cpp), so a one-cell rise on the
   remaining frame gave a plume that climbed 40 cells in 2.73 seconds -- roughly
   a sixth of walking pace, for something that is supposed to be racing upward
   out of a boiler.

   Four is a considered number rather than a big one. It was chosen against the
   thing that must NOT change: a body of gas that has piled up against a ceiling
   still presses back down, which is the behaviour the diffusion table exists to
   produce. Measured at equilibrium with 4000 cells in a sealed room, the depth
   of the half-full body is 20 rows at run lengths of 1, 3, 4 and 5 alike -- the
   run only shortens the TRANSIT, it does not flatten the settled cloud. Rise
   time over the same range: 2.73 s, 1.22 s, 1.00 s, 0.88 s.

   So this is deliberately the one lever that speeds ascent without touching the
   diffusion mix at all. Raising the upward weight in that table would have got
   a similar rise time and cost some of the sideways and downward wandering,
   which is the part that makes a plume look like a plume.

   It applies only through genuinely EMPTY cells. A gas still climbs through
   another gas or a liquid one cell at a time, by the ordinary tryMove below --
   a bubble that teleported four cells up a water column would outrun the
   displacement rules that make bubbles behave.

   Fire and Plasma are deliberately excluded. This run was introduced to make
   Steam escape boilers promptly, but putting it on the shared gas path also
   made flames and plasma leap four cells on nearly every frame because their
   jitter is much lower than Steam's. They keep the original one-cell climb:
   visibly buoyant, without outrunning the steam plume that carries heat. */
static const int GAS_RISE_RUN = 4;
static const int COMBUSTION_RISE_RUN = 1;

static inline int gasRiseRun(u8 mat) {
    return (mat == MAT_FIRE || mat == MAT_PLASMA)
         ? COMBUSTION_RISE_RUN : GAS_RISE_RUN;
}
static const int FLUID_CONVECTION_REACH       = 3;
static const int WAX_CONVECTION_REACH         = 1;
static const int FLUID_CONVECTION_DELTA       = 2;
/* Forty rather than sixty-four, and the reason is the parallel scan rather
   than the physics: this is one of the two longest SIDEWAYS writes in the
   engine, and the stripe width has to clear it. See RULE_WRITE_REACH_X in
   world.h. A mound still levels -- a very wide one takes another frame or
   two about it. */
static const int SUBMERGED_LEVEL_REACH         = 40;
static const int SUBMERGED_SINK_REACH          = 8;

/* Divide a heat transfer by 2^shift to get the temperature change a material of
   that thermal mass actually feels, carrying the remainder stochastically so
   small transfers average out correctly instead of truncating to nothing.
   `move` is always non-negative here; the caller applies the sign. */
static inline int scaleByMass(Lane& L, int move, int shift) {
    if (!shift) return move;
    const int q = move >> shift;
    const int r = move & ((1 << shift) - 1);
    return q + ((r && (int)lbits(L, (u32)shift) < r) ? 1 : 0);
}

/* A phase change consumes latent heat: pull the cell LATENT_HEAT toward
   ambient, and never past it.

   The "toward ambient" part is what melting made necessary. This used to be a
   plain imax(AMBIENT, t - LATENT_HEAT), which is right for every transition
   that happens above ambient -- boiling, evaporating -- but ice melts BELOW
   it, and subtracting there drove the new water colder than the freeze
   threshold it had just crossed. The cell refroze on the next frame, so ice
   sat flickering between the two states instead of melting. Moving toward
   ambient from whichever side the cell is on makes the two directions
   symmetric and leaves the hot path behaving exactly as it always did. */
static inline u8 latentDrain(int t) {
    if (t > AMBIENT_TEMP) return (u8)imax(AMBIENT_TEMP, t - LATENT_HEAT);
    if (t < AMBIENT_TEMP) return (u8)imin(AMBIENT_TEMP, t + LATENT_HEAT);
    return (u8)t;
}

static void markLiveCore(World& w, int x0, int y0, int x1, int y1) {
    const int cx0 = imax(0, x0 >> CHUNK_SHIFT);
    const int cy0 = imax(0, y0 >> CHUNK_SHIFT);
    const int cx1 = imin(CHUNKS_X - 1, x1 >> CHUNK_SHIFT);
    const int cy1 = imin(CHUNKS_Y - 1, y1 >> CHUNK_SHIFT);
    if (cx0 > cx1 || cy0 > cy1) return;
    for (int cy = cy0; cy <= cy1; ++cy)
        for (int cx = cx0; cx <= cx1; ++cx)
            w.liveCoreMask[cy * CHUNKS_X + cx] = 1;
}

void World::setLiveWindow(int x0, int y0, int x1, int y1) {
    memset(liveCoreMask, 0, sizeof(liveCoreMask));
    liveWindowCount = 1;
    liveX0 = x0; liveY0 = y0; liveX1 = x1; liveY1 = y1;
    const i32 cx0 = imax(0, x0 >> CHUNK_SHIFT), cx1 = imin(CHUNKS_X - 1, x1 >> CHUNK_SHIFT);
    const i32 cy0 = imax(0, y0 >> CHUNK_SHIFT), cy1 = imin(CHUNKS_Y - 1, y1 >> CHUNK_SHIFT);
    if (cx0 != liveCoreCX0 || cx1 != liveCoreCX1 || cy0 != liveCoreCY0 || cy1 != liveCoreCY1) {
        fingerTop = fingerBottom = fingerLeft = fingerRight = 0;
        liveCoreCX0 = cx0; liveCoreCX1 = cx1; liveCoreCY0 = cy0; liveCoreCY1 = cy1;
    }
    markLiveCore(*this, x0, y0, x1, y1);
}

void World::addLiveWindow(int x0, int y0, int x1, int y1) {
    ++liveWindowCount;
    markLiveCore(*this, x0, y0, x1, y1);
}

void World::reset() {
    clearBlockBoxes();
    /* setLiveWindow() compares the chunk-rounded core to decide whether old
       fingers remain valid, so establish a known sentinel before its first
       call.  World is also used as an uninitialised stack object by tests. */
    fingerTop = fingerBottom = fingerLeft = fingerRight = 0;
    liveCoreCX0 = liveCoreCY0 = -1;
    liveCoreCX1 = liveCoreCY1 = -1;
    clearLiveWindow();
    memset(cells, 0, sizeof(cells));
    memset(temp, AMBIENT_TEMP, sizeof(temp));
    memset(bg, 0, sizeof(bg));
    /* A blank world is open air all the way down. Generation labels the
       rock it makes; nothing here should assume a surface exists yet. */
    memset(zone, ZONE_SKY, sizeof(zone));
    memset(keepAlive, 0, sizeof(keepAlive));
    memset(liveGrace, 0, sizeof(liveGrace));
    keptChunks = 0;
    sproutCount = 0;
    felledCount = 0;
    memset(felledMark, 0, sizeof(felledMark));
    frame  = 0;
    activeChunks = 0;
    clearDirty(cur);
    clearDirty(next);

    for (int x = 0; x < SIM_W; ++x) {
        cells[x].mat = MAT_WALL;
        cells[(SIM_H - 1) * SIM_W + x].mat = MAT_WALL;
    }
    for (int y = 0; y < SIM_H; ++y) {
        cells[y * SIM_W].mat = MAT_WALL;
        cells[y * SIM_W + SIM_W - 1].mat = MAT_WALL;
    }
    /* The GLOBAL stream, not the lane's, and deliberately so. This draws
       37.7 million times, and where it leaves g_rng is where worldgen starts
       -- so moving it onto a lane would quietly re-roll every world from a
       different point in the stream. Nothing here runs in a stripe. */
    for (int i = 0; i < SIM_W * SIM_H; ++i) cells[i].tint = (u8)rngBits(8);
}

/* A wood cell is about to become something else. See World::felled -- this is
   the only moment a canopy can lose its support, so it is the only moment
   anything needs to look. Cheap by construction: two table lookups on a write
   that already happens, and a push on the rare one that matters. */
void World::reportFelled(Lane& L, int x, int y, u8 was, u8 now) {
    if (!g_matIsWood[was] || g_matIsWood[now]) return;
    const int ch = (y >> CHUNK_SHIFT) * CHUNKS_X + (x >> CHUNK_SHIFT);
    /* Inside the parallel scan this only stages the chunk index; felledMark
       is the world's dedup set and two lanes must not both be deciding what
       is in it. The merge applies the mark, so a chunk reported twice by two
       stripes still lands in felled[] once. */
    if (L.stripe >= 0) {
        int& n = g_stripeFelledN[L.stripe];
        if (n >= MAX_FELLED) return;
        g_stripeFelled[L.stripe][n++] = ch;
        return;
    }
    if (felledMark[ch]) return;
    if (felledCount >= MAX_FELLED) return;
    felledMark[ch] = 1;
    felled[felledCount++] = ch;
}

/* Warm a disc toward IGNITE_MAX, never past it. Cells already hotter are left
   alone rather than dragged down -- a striker cannot cool anything, and running
   one over an ember should not put it out. */
void World::ignite(int cx, int cy, int r) {
    Lane& L = simMainLane();
    const int x0 = imax(0, cx - r), x1 = imin(SIM_W - 1, cx + r);
    const int y0 = imax(0, cy - r), y1 = imin(SIM_H - 1, cy + r);
    const int r2 = r * r;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > r2) continue;
            const int i = y * SIM_W + x;
            const int t = temp[i];
            if (t >= IGNITE_MAX) continue;
            const int nt = t + IGNITE_STEP;
            temp[i] = (u8)(nt > IGNITE_MAX ? IGNITE_MAX : nt);
            dirtyPoint(L, x, y);
        }
}

void World::setCell(Lane& L, int x, int y, u8 mat) {
    const int i = y * SIM_W + x;
    Cell& c = cells[i];
    reportFelled(L, x, y, c.mat, mat);
    c.mat      = mat;
    c.moisture = 0;
    c.tint     = (u8)lbits(L, 8);
    /* Stamped with the previous frame so a freshly painted cell is eligible on
       the very next step rather than sitting still for one, which would make
       the brush feel laggy. */
    c.flags    = (u8)(((frame - 1) & STAMP_MASK) << STAMP_SHIFT);
    const MatInfo& m = MATS[mat];
    temp[i] = m.spawnTemp ? m.spawnTemp : (u8)AMBIENT_TEMP;
    dirtyPoint(L, x, y);
}

void World::breakCell(Lane& L, int x, int y) {
    const u8 m    = cells[y * SIM_W + x].mat;
    const u8 drop = g_matDropsAs[m];
    setCell(L, x, y, (drop != m && g_matIsSeed[drop]) ? drop : (u8)MAT_EMPTY);
}

/* See the note in world.h. The shift itself is the same three lines the gas
   pressure route uses further up this file -- copy the cell, copy its
   temperature, restamp it so the movement pass does not treat it as a cell that
   has not moved yet -- because it is the same operation and should not be a
   second implementation of it. */
bool World::liftColumn(Lane& L, int x, int y, int maxLift) {
    if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) return false;

    /* Find the roof of the column: the first empty cell above. Everything
       between here and there gets shoved into it. */
    int top = -1;
    for (int d = 1; d <= maxLift; ++d) {
        const int r = y - d;
        if (r < PLAY_Y0) return false;
        const u8 m = cells[r * SIM_W + x].mat;
        if (m == MAT_EMPTY) {
            /* Refuse to shove material into a body. The player and the
               creatures publish a collision box to the grid, and the ordinary
               movement rules already will not move into one; a piston that
               ignored that would push a column of sand through somebody. */
            if (blocksCell(x, r)) return false;
            top = r;
            break;
        }
        /* Static means static. A lift that shunted rock would let a spout bore
           upward through the world, and one that shunted a device would drag a
           machine off its own footprint and leave the Device struct pointing at
           cells it no longer owns. */
        if (MATS[m].kind == KIND_STATIC) return false;
    }
    if (top < 0) return false;      /* solid to the limit: no head left */

    const u8 st = (u8)(stamp() << STAMP_SHIFT);
    for (int r = top; r < y; ++r) {
        const int dst = r * SIM_W + x, src = (r + 1) * SIM_W + x;
        cells[dst] = cells[src];
        temp[dst]  = temp[src];
        cells[dst].flags = (u8)((cells[dst].flags & F_DIR) | st);
    }
    setCell(L, x, y, MAT_EMPTY);
    dirtyArea(L, x, top, x, y);
    return true;
}

void World::swapMat(Lane& L, int x, int y, u8 mat) {
    cells[y * SIM_W + x].mat = mat;
    dirtyPoint(L, x, y);
}

/* Swap material without disturbing temperature: a phase change carries its
   heat across, and the stamp is left alone so the new material waits until
   next frame to move. */
void World::convert(Lane& L, int x, int y, u8 mat) {
    Cell& c = cells[y * SIM_W + x];
    reportFelled(L, x, y, c.mat, mat);
    c.mat      = mat;
    c.moisture = 0;
    c.tint     = (u8)lbits(L, 8);
    dirtyPoint(L, x, y);
}

/* Clamped to the play area, so the border box can never be painted over. */
void World::paint(int cx, int cy, int r, u8 mat, bool replace) {
    Lane& L = simMainLane();
    int r2 = r * r;
    int x0 = imax(cx - r, PLAY_X0), x1 = imin(cx + r, PLAY_X1);
    int y0 = imax(cy - r, PLAY_Y0), y1 = imin(cy + r, PLAY_Y1);
    for (int y = y0; y <= y1; ++y) {
        int dy = y - cy;
        for (int x = x0; x <= x1; ++x) {
            int dx = x - cx;
            if (dx * dx + dy * dy > r2) continue;
            /* Erasing always applies -- "do not overwrite" is about pouring
               new material in, not about being unable to clear space. */
            if (!replace && mat != MAT_EMPTY && cells[y * SIM_W + x].mat != MAT_EMPTY) continue;
            setCell(L, x, y, mat);
        }
    }
}

void World::paintBg(int cx, int cy, int r, u8 mat) {
    const int r2 = r * r;
    const int x0 = imax(cx - r, PLAY_X0), x1 = imin(cx + r, PLAY_X1);
    const int y0 = imax(cy - r, PLAY_Y0), y1 = imin(cy + r, PLAY_Y1);
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
        const int dx = x - cx, dy = y - cy;
        if (dx * dx + dy * dy <= r2) setBg(x, y, mat, true);
    }
}

void World::heat(int cx, int cy, int r, int delta) {
    Lane& L = simMainLane();
    int r2 = r * r;
    int x0 = imax(cx - r, PLAY_X0), x1 = imin(cx + r, PLAY_X1);
    int y0 = imax(cy - r, PLAY_Y0), y1 = imin(cy + r, PLAY_Y1);
    for (int y = y0; y <= y1; ++y) {
        int dy = y - cy;
        for (int x = x0; x <= x1; ++x) {
            int dx = x - cx;
            if (dx * dx + dy * dy > r2) continue;
            const int i = y * SIM_W + x;
            int t = (int)temp[i] + delta;
            temp[i] = (u8)(t < 0 ? 0 : (t > 255 ? 255 : t));
            dirtyPoint(L, x, y);
        }
    }
}

/* ======================================================================
   Movement
   ====================================================================== */

/* Move source into target, either because target is empty or because target
   is a fluid the source is heavy enough to sink through. Both cells are
   stamped with the current parity so neither gets a second turn this frame. */
static bool filterAllows(u8 filter, u8 moving) {
    const u8 kind = MATS[moving].kind;
    if (filter == MAT_SIEVE) return kind == KIND_LIQUID || kind == KIND_GAS;
    if (filter == MAT_GAS_SIEVE) return kind == KIND_GAS;
    return false;
}

/* A reactive powder may briefly share its cell with the gas that transforms
   it. This is deliberately narrower than generic porosity: Coal accepts Steam
   because that exact pair is already registered as Coal -> Fuel, while fire,
   mercury vapour, and gases with no powder reaction remain ordinary cells.
   Every accepted occupant is therefore guaranteed to be consumed on the
   powder's next turn rather than becoming a second long-lived simulation
   layer hidden in Cell::moisture. */
static bool reactivePowderAllowsGas(u8 powder, u8 moving) {
    return MATS[powder].kind == KIND_POWDER &&
           g_matWetInto[powder] != MAT_EMPTY &&
           g_matWetBy[powder] == moving &&
           moving < MAT_COUNT && MATS[moving].kind == KIND_GAS;
}

/* A sieve or reactive-powder occupant packs one gas provenance bit above its
   material id. This is the only current reason the material table must remain
   below 128 entries; pressure itself still has the full byte while the gas is
   in an ordinary cell. If the catalog approaches this bound, sparse occupants
   need a sidecar rather than silently stealing another bit. */
static_assert(MAT_COUNT <= 128,
              "sparse occupants reserve bit 7 for gas volume provenance");
static inline u8 occupantMat(u8 packed) { return (u8)(packed & GAS_EXCESS_MASK); }

/* Let a denser liquid advance into a gas pocket without deleting the gas.

   Gas normally owns liquid/gas exchange on its upward turn. That prevents an
   in-place scan from relaying one bubble through a whole lake, but it also made
   the opposite boundary unrealistically rigid: a broad pour of Water could sit
   against a huge pressurised Steam pocket as though the gas were stone.

   Displacement here moves a PRESSURE WAVE rather than repeatedly swapping one
   gas cell. Starting at the touched gas cell, prefer a straight open outlet
   above, then search a small connected part of the gas pocket for any opening.
   Shift every gas parcel along that path by one cell and put the liquid into
   the vacated boundary. Cell payload and temperature travel with each parcel,
   so Steam volume and condensation ownership are exactly conserved.

   A completely sealed expanded pocket has no cell to shift into. In that case
   one representable expansion volume may be folded into an adjacent parcel as
   stored pressure. If even that cannot represent the pocket exactly, Water and
   the touched Steam parcel trade places. That last rule is intentionally the
   simple gameplay invariant: liquid treats gas as displacement space, while a
   movement stamp guarantees the same gas parcel can be pushed only once in the
   frame rather than relayed through an entire lake. */
bool World::displaceGasForLiquid(Lane& L, int sx, int sy, int tx, int ty) {
    const int si = sy * SIM_W + sx, ti = ty * SIM_W + tx;
    Cell& liquid = cells[si];
    Cell& gas = cells[ti];
    if (MATS[liquid.mat].kind != KIND_LIQUID || MATS[gas.mat].kind != KIND_GAS)
        return false;
    if (materialDensityQ8(liquid.mat, temp[si]) <=
        materialDensityQ8(gas.mat, temp[ti]) + DENSITY_SWAP_EPS_Q8)
        return false;

    const u8 now = stamp();
    if (((gas.flags >> STAMP_SHIFT) & STAMP_MASK) == now) return false;

    const u8 gasMat = gas.mat;
    const u8 st = (u8)(now << STAMP_SHIFT);
    const auto freshGas = [&](int i) {
        return cells[i].mat == gasMat &&
               ((cells[i].flags >> STAMP_SHIFT) & STAMP_MASK) != now;
    };
    const auto finishLiquid = [&]() {
        const Cell moving = liquid;
        const u8 movingTemp = temp[si];
        gas = moving;
        gas.flags = (u8)((gas.flags & F_DIR) | st);
        temp[ti] = movingTemp;
        liquid.mat = MAT_EMPTY;
        liquid.moisture = 0;
        liquid.tint = 0;
        liquid.flags = st;
        temp[si] = AMBIENT_TEMP;
        dirtyPoint(L, sx, sy);
        dirtyPoint(L, tx, ty);
    };

    if (L.scratch.pressureRoutes > 0) {
        --L.scratch.pressureRoutes;
        /* The pictured boiler case: a tall Steam body with a real outlet above.
           This costs a straight ray rather than a flood over the whole pocket. */
        for (int d = 1; d <= GAS_PRESSURE_VERTICAL_REACH && ty - d >= PLAY_Y0; ++d) {
            const int di = (ty - d) * SIM_W + tx;
            if (cells[di].mat == MAT_EMPTY && !blocksCell(tx, ty - d)) {
                for (int k = d; k >= 1; --k) {
                    const int dst = (ty - k) * SIM_W + tx;
                    const int src = (ty - k + 1) * SIM_W + tx;
                    cells[dst] = cells[src];
                    temp[dst] = temp[src];
                    cells[dst].flags = (u8)((cells[dst].flags & F_DIR) | st);
                }
                finishLiquid();
                dirtyArea(L, tx, ty - d, tx, ty);
                return true;
            }
            if (!freshGas(di)) break;
        }
    }

    /* Bent local pockets. parent[] is both the visited set and the route from
       the outlet back to the touched boundary, keeping work strictly bounded. */
    if (L.scratch.pressureRoutes > 0) {
        --L.scratch.pressureRoutes;
        static const int R = GAS_PRESSURE_LIQUID_RADIUS;
        static const int SIDE = R * 2 + 1;
        static const int CAP = SIDE * SIDE;
        i16 parent[CAP], queue[CAP];
        for (int k = 0; k < CAP; ++k) parent[k] = -2;
        const int center = R * SIDE + R;
        int head = 0, tail = 0, goal = -1, outlet = -1;
        parent[center] = -1;
        queue[tail++] = (i16)center;
        const int orderX[4] = { 0, -1, 1, 0 };
        const int orderY[4] = { -1, 0, 0, 1 };

        while (head < tail && goal < 0) {
            const int li = queue[head++];
            const int lx = li % SIDE, ly = li / SIDE;
            const int wx = tx - R + lx, wy = ty - R + ly;
            for (int k = 0; k < 4; ++k) {
                const int nx = wx + orderX[k], ny = wy + orderY[k];
                if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1)
                    continue;
                const int ni = ny * SIM_W + nx;
                if (cells[ni].mat == MAT_EMPTY && !blocksCell(nx, ny)) {
                    goal = li; outlet = ni; break;
                }
            }
            if (goal >= 0) break;
            for (int k = 0; k < 4; ++k) {
                const int nlx = lx + orderX[k], nly = ly + orderY[k];
                if (nlx < 0 || nlx >= SIDE || nly < 0 || nly >= SIDE) continue;
                const int niLocal = nly * SIDE + nlx;
                if (parent[niLocal] != -2) continue;
                const int nx = tx - R + nlx, ny = ty - R + nly;
                if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1)
                    continue;
                if (!freshGas(ny * SIM_W + nx)) continue;
                parent[niLocal] = (i16)li;
                queue[tail++] = (i16)niLocal;
            }
        }

        if (goal >= 0) {
            int dst = outlet, path = goal;
            while (path >= 0) {
                const int px = tx - R + path % SIDE;
                const int py = ty - R + path / SIDE;
                const int src = py * SIM_W + px;
                cells[dst] = cells[src];
                temp[dst] = temp[src];
                cells[dst].flags = (u8)((cells[dst].flags & F_DIR) | st);
                dirtyPoint(L, dst % SIM_W, dst / SIM_W);
                dst = src;
                path = parent[path];
            }
            finishLiquid();
            return true;
        }
    }

    /* Sealed pocket: compress only when the one-bit condensation provenance
       remains exactly representable after the merge. */
    const int volumes = 1 + (gas.moisture & GAS_EXCESS_MASK);
    const bool gasVolumeOnly = (gas.moisture & GAS_VOLUME_ONLY) != 0;
    static const int RX[4] = { 0, -1, 1, 0 };
    static const int RY[4] = { -1, 0, 0, 1 };
    for (int k = 0; k < 4; ++k) {
        const int nx = tx + RX[k], ny = ty + RY[k];
        const int ri = ny * SIM_W + nx;
        if (!freshGas(ri)) continue;
        Cell& receiver = cells[ri];
        const int receiverExcess = receiver.moisture & GAS_EXCESS_MASK;
        if (receiverExcess + volumes > GAS_EXCESS_MASK) continue;
        const bool receiverVolumeOnly = (receiver.moisture & GAS_VOLUME_ONLY) != 0;
        if (!gasVolumeOnly && !receiverVolumeOnly) continue;
        receiver.moisture = (u8)((gasVolumeOnly ? (receiver.moisture & GAS_VOLUME_ONLY) : 0) |
                                 (receiverExcess + volumes));
        receiver.flags = (u8)((receiver.flags & F_DIR) | st);
        if (temp[ti] > temp[ri]) temp[ri] = temp[ti];
        dirtyPoint(L, nx, ny);
        finishLiquid();
        return true;
    }
    /* Ordinary owner-bearing Steam in a sealed pocket cannot be merged without
       losing condensation mass. Swapping is the exact conservative fallback:
       Water advances, the complete Steam cell moves into the space it vacated,
       and both are stamped so neither can be relayed again this frame.

       A lone submerged bubble is deliberately excluded from SIDEWAYS swaps.
       Water surrounding one pixel of Steam would otherwise shove it left and
       right before its own buoyant turn. Downward liquid still pushes any gas
       upward, while lateral displacement requires a connected gas BODY -- the
       boiler/chamber case this rule exists to solve. */
    {
        bool connectedBody = false;
        for (int k = 0; k < 4; ++k) {
            const int nx = tx + NB_DX[k], ny = ty + NB_DY[k];
            if (cells[ny * SIM_W + nx].mat == gasMat) {
                connectedBody = true;
                break;
            }
        }
        const bool liquidFallingOntoGas = tx == sx && ty > sy;
        if (!liquidFallingOntoGas && !connectedBody) return false;
        const Cell displaced = gas;
        const u8 displacedTemp = temp[ti];
        const Cell moving = liquid;
        const u8 movingTemp = temp[si];
        gas = moving;
        gas.flags = (u8)((gas.flags & F_DIR) | st);
        temp[ti] = movingTemp;
        liquid = displaced;
        liquid.flags = (u8)((liquid.flags & F_DIR) | st);
        temp[si] = displacedTemp;
        dirtyPoint(L, sx, sy);
        dirtyPoint(L, tx, ty);
        return true;
    }
}

bool World::tryMove(Lane& L, int sx, int sy, int tx, int ty,
                    int liquidSwap) {
    /* Nothing moves into an occupied entity box -- see the note in world.h.
       First test in the function and first comparison of the box test, so the
       common case (no entity, or nowhere near it) costs one predictable
       compare against a constant.

       Material in FREE FALL is the one exception, and it is the other half of
       the rule in player.cpp: the player passes through a falling stream, so a
       falling stream has to pass through the player. Anything settled still
       piles against the body exactly as before, which is what keeps a heap
       something you stand on and a rising pile something that buries you -- what
       changed is only that you no longer wear the stream on your shoulders on
       its way down.

       Ordered so the exception costs nothing when it does not apply: blocksCell
       fails on its first compare against a constant almost everywhere in the
       world, and the second test is only reached for cells actually inside the
       box. */
    const int si = sy * SIM_W + sx, ti = ty * SIM_W + tx;
    Cell& s = cells[si];
    Cell& t = cells[ti];

    /* Filters and reactive powders have one sparse occupant slot in moisture.
       A permitted fluid enters while the host remains the cell's material.
       Filter occupants advance through connected mesh; a reactive-powder
       occupant is consumed into its registered product on the next turn. */
    const bool enteringFilter = filterAllows(t.mat, s.mat);
    const bool enteringReactivePowder = reactivePowderAllowsGas(t.mat, s.mat);
    if (enteringFilter || enteringReactivePowder) {
        if (t.moisture) {
            /* A free gas immediately beneath an occupied sieve can exchange
               with its denser occupant. The gas enters the mesh and the
               displaced liquid falls into the cell it vacated. Ownership is
               deliberately gas-only, matching ordinary gas/liquid buoyancy
               and preventing a liquid from relaying one gas parcel through
               several cells during the in-place scan. */
            const u8 displacedPacked = t.moisture;
            const u8 displaced = occupantMat(displacedPacked);
            const bool gasExchange = enteringFilter && MATS[s.mat].kind == KIND_GAS;
            if (!gasExchange || !displaced ||
                materialDensityQ8(displaced, temp[ti]) <=
                    materialDensityQ8(s.mat, temp[si]) + DENSITY_SWAP_EPS_Q8)
                return false;

            const u8 sourceMat = s.mat;
            const u8 sourceVolumeOnly = (u8)(s.moisture & GAS_VOLUME_ONLY);
            const int sourceExcess = s.moisture & GAS_EXCESS_MASK;

            /* The sieve occupant slot holds one gas volume, not a pressure
               count. Before the source cell becomes the falling liquid, move
               any stored excess into adjacent parcels of the same connected
               gas pocket. This conserves every steam volume while allowing a
               pressurized chamber boundary to exchange; the old one-volume
               restriction was the reason a full boiler still plugged. */
            int pressureReceiver[4], pressureReceiverCount = 0, pressureRoom = 0;
            for (int k = 0; k < 4; ++k) {
                const int nx = sx + NB_DX[k], ny = sy + NB_DY[k];
                if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1)
                    continue;
                const int ni = ny * SIM_W + nx;
                if (cells[ni].mat != sourceMat) continue;
                pressureReceiver[pressureReceiverCount++] = ni;
                pressureRoom += GAS_EXCESS_MASK -
                                (cells[ni].moisture & GAS_EXCESS_MASK);
            }
            if (pressureRoom < sourceExcess) return false;

            const u8 sourceDir = (u8)(s.flags & F_DIR);
            const u8 targetDir = (u8)(t.flags & F_DIR);
            const u8 st = (u8)(stamp() << STAMP_SHIFT);
            int excessLeft = sourceExcess;
            for (int k = 0; k < pressureReceiverCount && excessLeft; ++k) {
                Cell& receiver = cells[pressureReceiver[k]];
                const int receiverExcess = receiver.moisture & GAS_EXCESS_MASK;
                const int give = imin(excessLeft, GAS_EXCESS_MASK - receiverExcess);
                receiver.moisture = (u8)((receiver.moisture & GAS_VOLUME_ONLY) |
                                         (receiverExcess + give));
                receiver.flags = (u8)((receiver.flags & F_DIR) | st);
                dirtyPoint(L, pressureReceiver[k] % SIM_W,
                           pressureReceiver[k] / SIM_W);
                excessLeft -= give;
            }
            s.mat = displaced;
            s.moisture = MATS[displaced].kind == KIND_GAS
                       ? (u8)(displacedPacked & GAS_VOLUME_ONLY) : 0;
            t.moisture = (u8)(sourceMat | sourceVolumeOnly);
            const u8 tt = temp[ti]; temp[ti] = temp[si]; temp[si] = tt;
            s.flags = (u8)(targetDir | st);
            t.flags = (u8)(sourceDir | st);
            dirtyPoint(L, sx, sy); dirtyPoint(L, tx, ty);
            return true;
        }
        const bool gas = MATS[s.mat].kind == KIND_GAS;
        const u8 volumeOnly = gas ? (u8)(s.moisture & GAS_VOLUME_ONLY) : 0;
        const u8 excess = gas ? (u8)(s.moisture & GAS_EXCESS_MASK) : 0;
        /* A compressed parcel emits one expansion volume into the mesh rather
           than moving all its stored pressure into a one-cell occupant slot. */
        t.moisture = (u8)(s.mat | (gas ? GAS_VOLUME_ONLY : 0));
        temp[ti] = temp[si];
        if (excess) {
            s.moisture = (u8)(volumeOnly | (excess - 1));
        } else {
            t.moisture = (u8)(s.mat | volumeOnly);
            s.mat = MAT_EMPTY;
            s.moisture = 0;
            temp[si] = AMBIENT_TEMP;
        }
        const u8 st = (u8)(stamp() << STAMP_SHIFT);
        /* A sieve borrows bit zero for its occupant's flow direction. A powder
           owns that same bit as F_FALL, so preserve the powder's state while
           the steam waits for its reaction turn. */
        const u8 targetLow = enteringFilter ? (u8)(s.flags & F_DIR)
                                            : (u8)(t.flags & F_FALL);
        t.flags = (u8)(targetLow | st);
        s.flags = (u8)((s.flags & F_DIR) | st);
        dirtyPoint(L, sx, sy); dirtyPoint(L, tx, ty);
        return true;
    }

    if (blocksCell(tx, ty) && !cellFalling(s)) return false;

    if (t.mat != MAT_EMPTY) {
        const MatInfo& tm = MATS[t.mat];
        const MatInfo& sm = MATS[s.mat];
        /* A seed falls through anything that GREW, and this is the one
           exception to "powders do not enter solids". Without it the whole
           harvest is stuck: a pod broken high in a crown leaves its seed
           resting three hundred cells up and out of reach, which is not a seed
           you can pick up or water -- it is a seed you can look at.

           Leaves alone was the first version and it was not enough once seeds
           began to DRIFT sideways on the way down. Drifting sweeps a seed
           across the branches instead of dropping past them, and measured, ten
           of twenty seeds released in a live crown were still sitting on wood
           two and a half minutes later -- collected in the crotch of a branch
           with no open cell to either side, so nothing could shift them. A roll
           rule was tried first and moved them along the branch into exactly
           that pocket, which is where looking at the cells rather than guessing
           corrected the design: the fix is not to help a seed off a branch, it
           is that a branch does not hold one.

           Seeds are excluded from the set they pass through, or a stack of them
           would swap places with each other for ever instead of piling.

           A swap rather than a hole, like every other move here. Traced
           through, a seed falling past a column of leaves leaves them each one
           cell higher and one empty cell where it came out, so a crown it
           passes through keeps its shape and its cell count. */
        const bool throughFoliage = g_matIsSeed[s.mat]
                                 && g_matIsPlant[t.mat] && !g_matIsSeed[t.mat];
        if (!throughFoliage) {
            /* Gas may percolate straight upward through one denser powder cell
               on the gas parcel's own turn. Restricting this exception to
               (0,-1) prevents flit/dispersion from tunnelling sideways through
               a pile and lets the movement stamp cap it at one cell per frame. */
            const bool gasPercolatingUp = sm.kind == KIND_GAS &&
                                          tm.kind == KIND_POWDER &&
                                          tx == sx && ty == sy - 1;
            if (tm.kind != KIND_LIQUID && tm.kind != KIND_GAS &&
                !gasPercolatingUp) return false;
            /* General unlike-liquid exchange is owned by updateConvection's
               LOWER, lighter parcel. A separate, explicit call below may let a
               supported denser liquid level sideways/downward through a
               lighter one. In that case the target parcel must not already
               have moved or taken its turn this frame. That stamp check is the
               piece that prevents a row of Water cells from relay-displacing
               one Wax parcel sixteen times along a vessel wall. */
            if (sm.kind == KIND_LIQUID && tm.kind == KIND_LIQUID) {
                /* General gravity moves do not exchange unlike liquids. The
                   explicit submerged-leveling calls in updateLiquid may opt
                   in when they have proved that THIS source is the denser,
                   exposed parcel moving toward a supported lower level. */
                if (!liquidSwap) return false;
            }
            const int sourceDensity = materialDensityQ8(s.mat, temp[si]);
            const int targetDensity = materialDensityQ8(t.mat, temp[ti]);
            /* Gases invert the density test: they displace anything *heavier*,
               which is how steam bubbles up through water. */
            if (sm.kind == KIND_GAS) {
                if (targetDensity <= sourceDensity + DENSITY_SWAP_EPS_Q8) return false;
            }
            else if (liquidSwap == LIQ_SWAP_LIGHTER_WINS &&
                     sm.kind == KIND_LIQUID && tm.kind == KIND_LIQUID) {
                /* The inverse exchange, and ONLY between two liquids with a
                   caller that asked for it by name. A light parcel rising into
                   a denser one is how a pocket under a lid spreads out; it is
                   never something the ordinary gravity path should do, and it
                   must not extend to gases -- a liquid still has to go through
                   displaceGasForLiquid to enter one. */
                if (sourceDensity + DENSITY_SWAP_EPS_Q8 >= targetDensity) return false;
            }
            else {
                if (sourceDensity <= targetDensity + DENSITY_SWAP_EPS_Q8) return false;
                /* A liquid does not directly swap with a gas: it advances only
                   when the gas can be conserved by shifting or compression.
                   The routed pressure wave is stamped as a unit, so no parcel
                   can relay repeatedly during this in-place scan. */
                if (tm.kind == KIND_GAS)
                    return displaceGasForLiquid(L, sx, sy, tx, ty);
            }
        }
    }

    Cell tmp = t;
    t = s;
    s = tmp;
    /* Heat rides along with the material -- a hot particle carries its warmth
       rather than leaving it behind in the cell it vacated. */
    u8 tt = temp[ti]; temp[ti] = temp[si]; temp[si] = tt;

    const u8 st = (u8)(stamp() << STAMP_SHIFT);
    t.flags = (u8)((t.flags & F_DIR) | st);
    s.flags = (u8)((s.flags & F_DIR) | st);
    dirtyPoint(L, sx, sy);
    dirtyPoint(L, tx, ty);
    return true;
}

/* Move a fluid parcel already inside a sieve either into the next compatible
   mesh cell or back out into empty world space. The sieve material itself never
   moves. Temperature follows the parcel through the mesh; while occupied, the
   sieve and fluid share that one temperature, which also lets ordinary heat
   conduction act on the contents without another world-sized array. */
bool World::moveFilterFluid(Lane& L, int sx, int sy, int tx, int ty) {
    if (tx < PLAY_X0 || tx > PLAY_X1 || ty < PLAY_Y0 || ty > PLAY_Y1) return false;
    const int si = sy * SIM_W + sx, ti = ty * SIM_W + tx;
    Cell& s = cells[si];
    Cell& t = cells[ti];
    const u8 packed = s.moisture;
    const u8 moving = occupantMat(packed);
    if (!moving) return false;

    if (filterAllows(t.mat, moving)) {
        if (t.moisture) {
            const u8 displacedPacked = t.moisture;
            const u8 displaced = occupantMat(displacedPacked);
            if (!displaced || !filterAllows(s.mat, displaced)) return false;

            /* An occupied run of mesh must still permit buoyancy exchange.
               Give ownership to the lower-density gas, just as tryMove does
               in open cells: it rises into a denser occupant and carries that
               parcel back into its old sieve slot. This prevents a liquid
               produced in a steam-filled sieve (fuel from slaked coal, for
               example) from becoming a permanent plug. Requiring both sieve
               types to accept the displaced parcel preserves Gas Sieve's
               liquid rejection rule. */
            if (MATS[moving].kind != KIND_GAS) return false;
            const int movingDensity = materialDensityQ8(moving, temp[si]);
            const int displacedDensity = materialDensityQ8(displaced, temp[ti]);
            if (displacedDensity <= movingDensity + DENSITY_SWAP_EPS_Q8) return false;

            s.moisture = displacedPacked;
            t.moisture = packed;
            const u8 tt = temp[ti]; temp[ti] = temp[si]; temp[si] = tt;
            const u8 sourceDir = (u8)(s.flags & F_DIR);
            const u8 targetDir = (u8)(t.flags & F_DIR);
            const u8 st = (u8)(stamp() << STAMP_SHIFT);
            s.flags = (u8)(targetDir | st);
            t.flags = (u8)(sourceDir | st);
            dirtyPoint(L, sx, sy);
            dirtyPoint(L, tx, ty);
            return true;
        }
        t.moisture = packed;
    } else {
        if (t.mat != MAT_EMPTY) {
            const u8 displaced = t.mat;
            const u8 displacedKind = MATS[displaced].kind;
            /* Leaving the mesh is also a fluid boundary. A gas occupant may
               rise into a denser ordinary liquid/gas cell while that parcel
               drops into the sieve slot it vacated. Without this half of the
               exchange, the real boiler stack -- Coal, Fuel, Sieve, Steam --
               stops at Fuel even though Steam entered the sieve correctly. */
            if (blocksCell(tx, ty) || MATS[moving].kind != KIND_GAS ||
                (displacedKind != KIND_LIQUID && displacedKind != KIND_GAS) ||
                !filterAllows(s.mat, displaced) ||
                materialDensityQ8(displaced, temp[ti]) <=
                    materialDensityQ8(moving, temp[si]) + DENSITY_SWAP_EPS_Q8)
                return false;

            s.moisture = (u8)(displaced |
                (displacedKind == KIND_GAS ? (t.moisture & GAS_VOLUME_ONLY) : 0));
            t.mat = moving;
            t.moisture = MATS[moving].kind == KIND_GAS
                       ? (u8)(packed & GAS_VOLUME_ONLY) : 0;
            const u8 tt = temp[ti]; temp[ti] = temp[si]; temp[si] = tt;
            const u8 sourceDir = (u8)(s.flags & F_DIR);
            const u8 targetDir = (u8)(t.flags & F_DIR);
            const u8 st = (u8)(stamp() << STAMP_SHIFT);
            s.flags = (u8)(targetDir | st);
            t.flags = (u8)(sourceDir | st);
            dirtyPoint(L, sx, sy);
            dirtyPoint(L, tx, ty);
            return true;
        }
        if (blocksCell(tx, ty)) return false;
        t.mat = moving;
        t.moisture = MATS[moving].kind == KIND_GAS
                   ? (u8)(packed & GAS_VOLUME_ONLY) : 0;
    }

    s.moisture = 0;
    temp[ti] = temp[si];
    temp[si] = AMBIENT_TEMP;
    const u8 st = (u8)(stamp() << STAMP_SHIFT);
    t.flags = (u8)((s.flags & F_DIR) | st);
    s.flags = (u8)((s.flags & F_DIR) | st);
    dirtyPoint(L, sx, sy);
    dirtyPoint(L, tx, ty);
    return true;
}

void World::updateFilterFluid(Lane& L, int x, int y) {
    const int i = y * SIM_W + x;
    Cell& c = cells[i];
    const u8 packed = c.moisture;
    const u8 moving = occupantMat(packed);
    if (!moving) return;

    /* Contact reactions see through the mesh. This is table-driven in the same
       direction as the ordinary coal-side rule: a neighbour names what fluid
       transforms it, the neighbour changes, and the fluid parcel is spent. */
    for (int k = 0; k < 4; ++k) {
        const int nx = x + NB_DX[k], ny = y + NB_DY[k];
        const u8 neighbour = cells[ny * SIM_W + nx].mat;
        if (!g_matWetInto[neighbour] || g_matWetBy[neighbour] != moving) continue;
        convert(L, nx, ny, g_matWetInto[neighbour]);
        c.moisture = 0;
        temp[i] = AMBIENT_TEMP;
        dirtyPoint(L, x, y);
        return;
    }

    const MatInfo& m = MATS[moving];
    int dx = (c.flags & F_DIR) ? 1 : -1;
    if (m.kind == KIND_GAS) {
        if (moveFilterFluid(L, x, y, x, y - 1)) return;
        if (moveFilterFluid(L, x, y, x + dx, y - 1)) return;
        if (moveFilterFluid(L, x, y, x - dx, y - 1)) return;
        if (moveFilterFluid(L, x, y, x + dx, y)) return;
        if (moveFilterFluid(L, x, y, x - dx, y)) return;
    } else { /* only liquids can enter the other permitted branch */
        if (moveFilterFluid(L, x, y, x, y + 1)) return;
        if (moveFilterFluid(L, x, y, x + dx, y + 1)) return;
        if (moveFilterFluid(L, x, y, x - dx, y + 1)) return;
        if (moveFilterFluid(L, x, y, x + dx, y)) return;
        if (moveFilterFluid(L, x, y, x - dx, y)) return;
    }

    c.flags ^= F_DIR;
    dirtyPoint(L, x, y);   /* occupied mesh retries until it can leave or react */
}

void World::updatePowder(Lane& L, int x, int y) {
    Cell& c = cells[y * SIM_W + x];
    const MatInfo& m = MATS[c.mat];

    /* F_FALL is maintained here and nowhere else: set on the cell the material
       LANDED IN after a straight drop, cleared on any frame the drop failed.
       Setting it after the move rather than before is not a detail -- tryMove
       swaps, so by the time it returns the material is at (x, y+1) and `c` is
       whatever used to be there.

       Clearing it on failure is what lets a landed stream go quiet correctly. A
       cell that stops falling is dirty from its own last move, so it is visited
       once more, fails here, drops the flag, schedules nothing, and sleeps as
       ordinary solid ground. Nothing has to sweep the flag, and a settled pile
       is never revisited to have it cleared -- it was already cleared on the way
       to settling. */
    /* --- drift -----------------------------------------------------------
       A seed wanders sideways on its way down instead of dropping in a line.
       See g_matDrift for why, and for why the strength comes off the cell's own
       tint rather than out of the table alone.

       Gated on the cell BELOW being open, which is the honest test for "in free
       fall" and is what keeps this from becoming a slide: a seed sitting on the
       ground with a hole beside it must settle, not crawl into it. Everything
       past that gate is genuinely falling, so the diagonal is marked F_FALL for
       the same reason a straight drop is -- a shower of seed is something you
       walk through, not a wall.

       If the diagonal is blocked the straight drop below still runs, so a seed
       drifting into a wall carries on down it rather than stopping dead. */
    const u8 drift = g_matDrift[c.mat];
    if (drift && cells[(y + 1) * SIM_W + x].mat == MAT_EMPTY) {
        const int dir   = (c.tint & 1) ? 1 : -1;
        const u32 scale = ((c.tint >> 1) & 7) + 1;          /* 1..8 of 8 */
        if (lchance(L, (u32)drift * scale / 8) && tryMove(L, x, y, x + dir, y + 1)) {
            cells[(y + 1) * SIM_W + x + dir].flags |= F_FALL;
            return;
        }
    }

    if (tryMove(L, x, y, x, y + 1)) {
        cells[(y + 1) * SIM_W + x].flags |= F_FALL;
        return;
    }

    /* --- eviction ------------------------------------------------------
       A falling cell is allowed THROUGH the entity box, so it can also be
       inside it when its fall ends -- and if it simply settled there it would
       turn back into solid ground in the exact cells the player occupies. That
       is not a corner case: measured, a wide pour left 164 settled sand cells
       inside the body, which is essentially the whole figure, and the unstick in
       Player::update could not lift them out from under a continuous pour, so
       they read as BURIED. Passing through has to mean passing through, not
       "phasing in".

       So a falling cell that cannot continue and is inside the box gets pushed
       out instead of settling, preferring the side it is already nearer to.
       Diagonals first, because that is still descending; a purely sideways shove
       only if it must, which is a move no powder makes on its own and is
       justified here as eviction rather than flow.

       If it cannot get out this frame it stays marked as falling and dirties
       itself to try again. Both halves of that matter. Keeping the flag is what
       keeps the player unblocked in the meantime, and re-dirtying is what stops a
       trapped cell from sleeping in a state it must not stay in. Neither can spin
       forever: the condition is "inside the box", so it ends when the player
       moves, and then the ordinary path below clears the flag and lets the cell
       settle like anything else.

       Note this leaves pile behaviour completely alone, which is the point. Sand
       still rests on the head and shoulders, a drift still walls you in -- what
       changed is only that material in transit does not become part of you. */
    const int blocker = blockerAt(x, y);
    if (blocker >= 0) {
        const OccupantBox& box = occupant[blocker];
        const int out = (x * 2 < box.x0 + box.x1) ? -1 : 1;
        static const int TRY[4][2] = { {1,1}, {-1,1}, {1,0}, {-1,0} };
        for (int t = 0; t < 4; ++t) {
            const int tx = x + TRY[t][0] * out, ty = y + TRY[t][1];
            if (tryMove(L, x, y, tx, ty)) {
                cells[ty * SIM_W + tx].flags |= F_FALL;
                return;
            }
        }
        dirtyPoint(L, x, y);
        return;                 /* F_FALL deliberately left set */
    }

    c.flags = (u8)(c.flags & ~F_FALL);

    /* Blocked below, so consider sliding off to a diagonal. The chance of
       doing so is what sets the angle of repose: dry sand nearly always
       slides and so spreads into a shallow cone, while wet material clings
       and stacks steeply. */
    int slide = m.slideDry;
    if (m.capacity && c.moisture) {
        int wetF = (c.moisture * m.invCapQ8) >> 8;
        if (wetF > 255) wetF = 255;
        slide += (((int)m.slideWet - (int)m.slideDry) * wetF) >> 8;
    }
    if (!lchance(L, (u32)slide)) return;

    int dx = (lrand(L) & 1) ? 1 : -1;
    if (tryMove(L, x, y, x + dx, y + 1)) return;
    tryMove(L, x, y, x - dx, y + 1);
}

/* Buoyant parcel convection. Ordinary same-material liquids and gases use the
   three-cell vertical reach and two-degree threshold first tuned for Water.
   This carries an actual hot parcel upward rather than teleporting heat,
   making a heated fluid body form a warm upper layer without globally
   accelerating conduction through walls or solids. Different fluids exchange
   when the lower parcel's effective thermal density is meaningfully lighter.

   Wax is the visible exception: its colour and coherent blobs make a
   three-cell parcel swap read as matter teleporting, especially in the narrow
   column against a vessel wall. Its internal convection is adjacent-only.
   Wax/Water buoyancy was already adjacent-only below, so this changes neither
   its density crossover nor its ability to rise through the surrounding pool.

   Alternating source-row parity and movement stamps keep each parcel to one
   bounded convection move of at most three cells per frame.
   No RNG is consumed, preserving the deterministic movement silhouette. */
bool World::updateConvection(Lane& L, int x, int y) {
    const int i = y * SIM_W + x;
    const u8 mat = cells[i].mat;
    const u8 kind = MATS[mat].kind;
    if ((kind != KIND_LIQUID && kind != KIND_GAS) ||
        (((u32)y ^ frame) & 1u) != 0u) return false;

    int above = i - SIM_W;
    const u8 aboveMat = cells[above].mat;
    if (MATS[aboveMat].kind != kind) return false;
    if (aboveMat == mat) {
        const int reach = mat == MAT_WAX ? WAX_CONVECTION_REACH
                                         : FLUID_CONVECTION_REACH;
        const int delta = FLUID_CONVECTION_DELTA;
        int target = i;
        for (int d = 1; d <= reach && y - d >= PLAY_Y0; ++d) {
            const int candidate = i - d * SIM_W;
            if (cells[candidate].mat != mat) break;
            /* Conduction runs before movement and can smooth the immediately
               adjacent pair below the threshold while a cooler parcel still
               exists two or three cells up. Inspect the whole short column;
               only a parcel at least as hot as this one blocks its rise. */
            if ((int)temp[candidate] >= (int)temp[i]) break;
            if ((int)temp[i] > (int)temp[candidate] + delta)
                target = candidate;
        }
        if (target == i) return false;
        above = target;
    } else {
        const int belowDensity = materialDensityQ8(mat, temp[i]);
        const int aboveDensity = materialDensityQ8(aboveMat, temp[above]);
        if (belowDensity + DENSITY_SWAP_EPS_Q8 >= aboveDensity) return false;
    }

    Cell parcel = cells[i];
    cells[i] = cells[above];
    cells[above] = parcel;
    const u8 parcelTemp = temp[i];
    temp[i] = temp[above];
    temp[above] = parcelTemp;
    const u8 st = (u8)(stamp() << STAMP_SHIFT);
    cells[i].flags = (u8)((cells[i].flags & F_DIR) | st);
    cells[above].flags = (u8)((cells[above].flags & F_DIR) | st);
    dirtyArea(L, x, above / SIM_W, x, y);
    return true;
}

void World::updateLiquid(Lane& L, int x, int y) {
    const int i = y * SIM_W + x;
    Cell& c = cells[i];
    const MatInfo& m = MATS[c.mat];

    /* The interior of a large single-material pool has no possible gravity or
       flow move, yet the ordinary path asks tryMove several times, scans for
       hydrostatic reach, and may walk sideways through more of the same fluid
       for every hot cell on every frame. Eight matching neighbours prove this
       parcel is not on an interface. Convection still gets its full turn—this
       optimization removes only movement attempts that cannot immediately
       change the local arrangement. Boundary and mixed-fluid cells keep the
       complete path below. */
    const u8 mat = c.mat;
    const bool packedSame = cells[i - SIM_W - 1].mat == mat &&
                            cells[i - SIM_W    ].mat == mat &&
                            cells[i - SIM_W + 1].mat == mat &&
                            cells[i - 1].mat == mat &&
                            cells[i + 1].mat == mat &&
                            cells[i + SIM_W - 1].mat == mat &&
                            cells[i + SIM_W    ].mat == mat &&
                            cells[i + SIM_W + 1].mat == mat;
    if (packedSame) {
        updateConvection(L, x, y);
        return;
    }

    if (tryMove(L, x, y, x, y + 1)) return;
    /* A lone/exposed parcel may sink directly into a lighter liquid. Requiring
       no same-material parcel immediately above is the anti-relay ownership
       rule: the bottom cell of a Water column cannot repeatedly push one hot
       Wax parcel upward through the whole column. Thick layers are sorted from
       below by updateConvection instead. */
    const u8 belowMat = cells[i + SIM_W].mat;
    if (belowMat != c.mat && MATS[belowMat].kind == KIND_LIQUID &&
        cells[i - SIM_W].mat != c.mat &&
        tryMove(L, x, y, x, y + 1, true)) return;
    if (updateConvection(L, x, y)) return;

    int dx = (c.flags & F_DIR) ? 1 : -1;

    /* Submerged density leveling. Vertical swaps correctly sort materials but
       leave the denser liquid as a steep mound once its bottom row is full.
       Only a TOP parcel with the same liquid directly below enters this path.
       It looks across the lower row for its first edge against a lighter
       liquid, follows that lighter column downward for up to eight cells, then
       trades places with the deepest parcel reached. Following the column is
       what makes this work in a bowl rather than only on the perfectly flat
       floor the first regression happened to use. This is the same bounded
       pressure-flow approximation ordinary liquids use in air, now applied to
       an immiscible interface.

       Several top parcels can find successive edge cells in one frame, making
       a mound relax promptly instead of opening and filling one blocky hole at
       a time. A one-cell-deep layer has no source parcel above it, so it stops
       exactly flat rather than diffusing sideways forever. */
    const u8 aboveLevelMat = cells[i - SIM_W].mat;
    const int sourceDensity = materialDensityQ8(mat, temp[i]);
    if (aboveLevelMat != mat && MATS[aboveLevelMat].kind == KIND_LIQUID &&
        cells[i + SIM_W].mat == mat &&
        sourceDensity > materialDensityQ8(aboveLevelMat, temp[i - SIM_W]) +
                        DENSITY_SWAP_EPS_Q8) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            const int dir = attempt == 0 ? dx : -dx;
            for (int step = 1; step <= SUBMERGED_LEVEL_REACH; ++step) {
                const int tx = x + dir * step;
                if (tx < PLAY_X0 || tx > PLAY_X1) break;
                const int ti = (y + 1) * SIM_W + tx;
                const u8 target = cells[ti].mat;
                if (target == mat) continue;
                if (MATS[target].kind != KIND_LIQUID ||
                    sourceDensity <= materialDensityQ8(target, temp[ti]) + DENSITY_SWAP_EPS_Q8)
                    break;
                int targetY = y + 1;
                for (int sink = 1; sink < SUBMERGED_SINK_REACH; ++sink) {
                    const int nextY = targetY + 1;
                    if (nextY > PLAY_Y1) break;
                    const int nextI = nextY * SIM_W + tx;
                    const u8 next = cells[nextI].mat;
                    if (MATS[next].kind != KIND_LIQUID ||
                        sourceDensity <= materialDensityQ8(next, temp[nextI]) +
                                         DENSITY_SWAP_EPS_Q8)
                        break;
                    targetY = nextY;
                }
                if (tryMove(L, x, y, tx, targetY, true)) {
                    dirtyArea(L, imin(x, tx), y, imax(x, tx), targetY);
                    return;
                }
                break;
            }
        }
    }

    /* --- and the same thing for a POCKET of the lighter liquid -------------
       The block above relaxes a MOUND of the denser liquid, and it is the only
       leveling rule there was. Nothing relaxed the opposite shape, and in a
       SEALED vessel that is exactly the shape you get: the lighter liquid rises
       until it meets the lid and then stops dead, still square, because every
       other rule that could spread it needs somewhere emptier to go and a full
       vessel has nowhere.

       Reported from play as a pocket of molten slag in a lava chamber forming a
       square at the top, and stated more generally as "any contained vessel of
       liquid with some of a less dense liquid in it, the less dense liquid
       doesn't flatten out at all". Measured: a 13x13 blob of water released in
       a sealed vessel of mercury reached the lid and was still 13 wide by 13
       tall two thousand frames later.

       This is the block above reflected in the horizontal, deliberately line
       for line. A BOTTOM parcel with its own liquid directly ABOVE looks across
       the row above for its first edge against something DENSER, follows that
       denser column upward, and trades with the highest parcel it reaches.
       Same reach, same epsilon, same single move per frame.

       The mirror inherits the property that makes the original stop: a
       one-cell-thick layer has no source parcel below it, so a pocket relaxes
       to flat and then stays there rather than smearing sideways forever. */
    const u8 belowLevelMat = cells[i + SIM_W].mat;
    if (y - 1 >= PLAY_Y0 && belowLevelMat != mat &&
        MATS[belowLevelMat].kind == KIND_LIQUID &&
        cells[i - SIM_W].mat == mat &&
        sourceDensity + DENSITY_SWAP_EPS_Q8 <
            materialDensityQ8(belowLevelMat, temp[i + SIM_W])) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            const int dir = attempt == 0 ? dx : -dx;
            for (int step = 1; step <= SUBMERGED_LEVEL_REACH; ++step) {
                const int tx = x + dir * step;
                if (tx < PLAY_X0 || tx > PLAY_X1) break;
                const int ti = (y - 1) * SIM_W + tx;
                const u8 target = cells[ti].mat;
                if (target == mat) continue;
                if (MATS[target].kind != KIND_LIQUID ||
                    sourceDensity + DENSITY_SWAP_EPS_Q8 >=
                        materialDensityQ8(target, temp[ti]))
                    break;
                int targetY = y - 1;
                for (int rise = 1; rise < SUBMERGED_SINK_REACH; ++rise) {
                    const int nextY = targetY - 1;
                    if (nextY < PLAY_Y0) break;
                    const int nextI = nextY * SIM_W + tx;
                    const u8 next = cells[nextI].mat;
                    if (MATS[next].kind != KIND_LIQUID ||
                        sourceDensity + DENSITY_SWAP_EPS_Q8 >=
                            materialDensityQ8(next, temp[nextI]))
                        break;
                    targetY = nextY;
                }
                if (tryMove(L, x, y, tx, targetY, LIQ_SWAP_LIGHTER_WINS)) {
                    dirtyArea(L, imin(x, tx), targetY, imax(x, tx), y);
                    return;
                }
                break;
            }
        }
    }

    /* Viscosity. For a liquid the `jitter` byte is a chance out of 255 that the
       cell simply refuses to flow sideways this frame -- see the note on the
       field in materials.h for why it lives there. Straight-down falling above
       is deliberately outside the gate: viscosity resists flow, not gravity, so
       a thick liquid still drops at full speed and only crawls once it lands.

       The dirtying here is the subtle part, and getting it wrong is the trap
       this file warns about twice already. Refusing to move is not motion, so
       it schedules nothing -- and a pool whose cells all refused on the same
       frame would put its own chunk to sleep with somewhere left to flow,
       freezing mid-ooze. So a cell that still has an empty neighbour dirties
       itself to guarantee another attempt. A cell packed solid on all sides has
       nothing to retry and is left to sleep, which is what keeps a settled
       viscous pool free rather than spinning forever. */
    if (m.jitter && lchance(L, m.jitter)) {
        if (cells[y * SIM_W + x - 1].mat == MAT_EMPTY ||
            cells[y * SIM_W + x + 1].mat == MAT_EMPTY ||
            cells[(y + 1) * SIM_W + x - 1].mat == MAT_EMPTY ||
            cells[(y + 1) * SIM_W + x + 1].mat == MAT_EMPTY) dirtyPoint(L, x, y);
        return;
    }

    if (tryMove(L, x, y, x + dx, y + 1)) return;
    if (tryMove(L, x, y, x - dx, y + 1)) return;

    /* Nothing below, so run sideways looking for somewhere to fall. Scanning
       ahead several cells in one frame is what makes a puddle flatten out
       promptly instead of oozing one pixel per frame.

       If the remembered direction is walled in, turn around and try the other
       one within this same frame. Deferring the retry to next frame would be
       a trap: a flag flip is not motion, so it dirties nothing, and a pool
       whose cells all flipped on the same frame would put its own chunk to
       sleep while it still had somewhere left to flow. */
    /* Hydrostatic pressure, approximated as how much of the same liquid is
       stacked directly overhead (counted only to a small cap, so it stays a
       handful of reads, and only for a cell that has come to rest). Buried
       liquid throws further than surface liquid, which is what makes a body
       slump into a dome. With a single reach for every cell, every row drains
       at the same rate and a poured pile keeps dead-vertical walls -- the
       flat-topped mesa that made liquids read as blocky. */
    int pressure = 0;
    for (int k = 1; k <= PRESSURE_MAX; ++k) {
        if (y - k < PLAY_Y0 || cells[(y - k) * SIM_W + x].mat != c.mat) break;
        ++pressure;
    }

    const int reach = (int)m.dispersion + pressure;
    for (int attempt = 0; attempt < 2; ++attempt) {
        int destX = x;
        for (int s = 1; s <= reach; ++s) {
            int nx = x + dx * s;
            if (nx < PLAY_X0 || nx > PLAY_X1) break;
            const Cell& n = cells[y * SIM_W + nx];
            if (n.mat != MAT_EMPTY) {
                /* Same liquid: see through it, but it is not a destination. */
                if (n.mat == c.mat) continue;
                const MatInfo& nm = MATS[n.mat];
                if (nm.kind != KIND_GAS || nm.density >= m.density) break;
            }
            destX = nx;
            if (cells[(y + 1) * SIM_W + nx].mat == MAT_EMPTY) break;  /* found a hole */
        }

        if (destX != x) {
            tryMove(L, x, y, destX, y);
            /* tryMove only dirties the two endpoints, but this hop can cover
               several cells at once. Anything resting along the swept path
               just lost its support and has to be woken too. */
            dirtyArea(L, imin(x, destX), y, imax(x, destX), y);
            return;
        }
        dx = -dx;
        c.flags ^= F_DIR;   /* remember whichever way it ends up going */
    }
    /* Boxed in both ways: nothing was dirtied, so this pool can go to sleep. */
}

/* Spend or redistribute one compressed gas parcel's excess volume. Expansion
   is local and visible: up to five connected cells through open air or a
   straight liquid column, never a teleport to a distant opening. Bent liquid
   and powder paths still move one conserved volume at a time. If the parcel is
   sealed, its excess remains in the cell and costs nothing once equalized;
   changing a boundary dirties the neighbourhood and wakes it again. */
bool World::updateGasPressure(Lane& L, int x, int y) {
    const int i = y * SIM_W + x;
    Cell& c = cells[i];
    const u8 excess = (u8)(c.moisture & GAS_EXCESS_MASK);
    if (!excess) return false;

    const int dir = (c.flags & F_DIR) ? 1 : -1;
    const int dx[4] = { 0, dir, -dir, 0 };
    const int dy[4] = { -1, 0, 0, 1 };
    const u8 gasMat = c.mat;
    const u8 gasTemp = temp[i];
    const u8 pressureStamp = (u8)(stamp() << STAMP_SHIFT);
    const u8 gasDir = (u8)(c.flags & F_DIR);

    /* Open space has essentially no resistance, so release a whole ordinary
       boiling charge instead of growing one pixel per frame. This is a tiny
       connected flood rooted at the source: every new cell is reached through
       a cell spawned earlier in this same burst, never placed across a wall or
       disconnected pocket. Radius three is already far more room than the
       largest ordinary gas-expansion charge can consume. */
    {
        static const int R = 3, SIDE = R * 2 + 1, CAP = SIDE * SIDE;
        i16 queue[CAP];
        u8 seen[CAP] = { 0 };
        int head = 0, tail = 0, spawned = 0;
        const int center = R * SIDE + R;
        queue[tail++] = (i16)center;
        seen[center] = 1;
        const int burst = imin((int)excess, GAS_PRESSURE_EXPANSION_BURST);
        while (head < tail && spawned < burst) {
            const int li = queue[head++];
            const int lx = li % SIDE, ly = li / SIDE;
            for (int k = 0; k < 4 && spawned < burst; ++k) {
                const int nlx = lx + dx[k], nly = ly + dy[k];
                if (nlx < 0 || nlx >= SIDE || nly < 0 || nly >= SIDE) continue;
                const int niLocal = nly * SIDE + nlx;
                if (seen[niLocal]) continue;
                seen[niLocal] = 1;
                const int nx = x - R + nlx, ny = y - R + nly;
                if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1) continue;
                const int ni = ny * SIM_W + nx;
                Cell& n = cells[ni];
                if (n.mat != MAT_EMPTY || blocksCell(nx, ny)) continue;
                n.mat = gasMat;
                n.moisture = GAS_VOLUME_ONLY;
                n.tint = (u8)lbits(L, 8);
                n.flags = (u8)(gasDir | pressureStamp);
                temp[ni] = gasTemp;
                dirtyPoint(L, nx, ny);
                queue[tail++] = (i16)niLocal;
                ++spawned;
            }
        }
        if (spawned) {
            c.moisture = (u8)((c.moisture & GAS_VOLUME_ONLY) |
                              ((int)excess - spawned));
            c.flags = (u8)(gasDir | pressureStamp);
            dirtyPoint(L, x, y);
            return true;
        }
    }

    /* A compressed parcel under liquid should not have to wait for its owner
       cell to bubble all the way to the surface before any of the stored
       volume can appear. Pressure shifts the connected vertical liquid column
       by as many as five cells in one burst and occupies the vacated cells.
       Whole Cells and temperatures move together, just like tryMove(). */
    const auto finishExpansion = [&](int di) {
        Cell& d = cells[di];
        d.mat = gasMat;
        d.moisture = GAS_VOLUME_ONLY;
        d.tint = (u8)lbits(L, 8);
        d.flags = (u8)(gasDir | pressureStamp);
        temp[di] = gasTemp;
        c.moisture = (u8)((c.moisture & GAS_VOLUME_ONLY) | (excess - 1));
        c.flags = (u8)(gasDir | pressureStamp);
        dirtyPoint(L, x, y);
        dirtyPoint(L, di % SIM_W, di / SIM_W);
    };

    /* Fast path: the overwhelmingly common boiler geometry is liquid directly
       above the gas with open air above that column. Shift from the surface
       downward so no parcel is overwritten before it has been copied. */
    int outletY = -1;
    for (int d = 1; d <= GAS_PRESSURE_VERTICAL_REACH && y - d >= PLAY_Y0; ++d) {
        const u8 mat = cells[(y - d) * SIM_W + x].mat;
        if (mat == MAT_EMPTY) { outletY = y - d; break; }
        if (MATS[mat].kind != KIND_LIQUID) break;
    }
    if (outletY >= 0 && !blocksCell(x, outletY)) {
        int burst = 0;
        const int wanted = imin((int)excess, GAS_PRESSURE_EXPANSION_BURST);
        for (; burst < wanted; ++burst) {
            const int ey = outletY - burst;
            if (ey < PLAY_Y0 || cells[ey * SIM_W + x].mat != MAT_EMPTY ||
                blocksCell(x, ey)) break;
        }
        for (int sy = outletY + 1; sy < y; ++sy) {
            const int my = sy - burst;
            const int dst = my * SIM_W + x;
            const int src = sy * SIM_W + x;
            cells[dst] = cells[src];
            temp[dst] = temp[src];
            cells[dst].flags = (u8)((cells[dst].flags & F_DIR) | pressureStamp);
        }
        for (int gy = y - burst; gy < y; ++gy) {
            const int gi = gy * SIM_W + x;
            Cell& g = cells[gi];
            g.mat = gasMat;
            g.moisture = GAS_VOLUME_ONLY;
            g.tint = (u8)lbits(L, 8);
            g.flags = (u8)(gasDir | pressureStamp);
            temp[gi] = gasTemp;
        }
        if (burst) {
            c.moisture = (u8)((c.moisture & GAS_VOLUME_ONLY) |
                              ((int)excess - burst));
            c.flags = (u8)(gasDir | pressureStamp);
            dirtyArea(L, x, outletY - burst, x, y);
            return true;
        }
    }

    /* Powders transmit a shove only along a straight, short line. Unlike the
       liquid search below this deliberately does not turn corners: pressure
       can lift a plug or slide a small bank into free space, but it cannot find
       a winding route through a mountain and make the far side jump.

       Resistance is a THRESHOLD, not a number of volumes consumed. The gas
       still expands by one volume when the line yields; adding another powder
       cell raises the required pressure by one, so long packed masses stop the
       search even when every individual grain is easy to move. */
    int powderDir = -1, powderCount = 0, powderRequired = 256;
    for (int k = 0; k < 4; ++k) {
        int count = 0, required = 0;
        for (int d = 1; d <= GAS_PRESSURE_POWDER_REACH + 1; ++d) {
            const int nx = x + dx[k] * d, ny = y + dy[k] * d;
            if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1) break;
            const Cell& n = cells[ny * SIM_W + nx];
            if (n.mat == MAT_EMPTY) {
                if (count && !blocksCell(nx, ny) && required <= (int)excess &&
                    (required < powderRequired ||
                     (required == powderRequired && count < powderCount))) {
                    powderDir = k;
                    powderCount = count;
                    powderRequired = required;
                }
                break;
            }
            if (MATS[n.mat].kind != KIND_POWDER) break;
            const int resistance = g_matPressureResistance[n.mat];
            if (resistance == 255) break;
            required = imax(required, resistance + count);
            ++count;
            if (required > (int)excess) break;
        }
    }

    if (powderDir >= 0) {
        const int pdx = dx[powderDir], pdy = dy[powderDir];
        for (int d = powderCount + 1; d >= 2; --d) {
            const int tx = x + pdx * d, ty = y + pdy * d;
            const int sx = x + pdx * (d - 1), sy = y + pdy * (d - 1);
            const int dst = ty * SIM_W + tx, src = sy * SIM_W + sx;
            cells[dst] = cells[src];
            temp[dst] = temp[src];
            /* Only a downward shove is a straight fall for collision purposes;
               upward and sideways movement must not leave F_FALL stale. */
            const u8 fall = (pdx == 0 && pdy == 1) ? F_FALL : 0;
            cells[dst].flags = (u8)(fall | pressureStamp);
            dirtyPoint(L, tx, ty);
        }
        finishExpansion((y + dy[powderDir]) * SIM_W + x + dx[powderDir]);
        return true;
    }

    /* Pressure belongs to a connected gas pocket, not to whichever pixel
       happened to inherit the hidden volume when liquid boiled. The old
       adjacent equalizer made an interior charge crawl through a large Steam
       blob one cell per frame; only after reaching the skin could it displace
       Water, producing the long-lived dense blob seen in large boilers.

       Route pressure directly to a lower-pressure boundary parcel. Straight
       rays cover the common large round pocket for a handful of reads. A
       bounded local flood handles crooked pockets without a world-sized
       pressure plane or an unbounded component walk. The receiver performs the
       ordinary visible expansion/displacement on its own turn, so this moves
       no material and manufactures no volume. */
    const auto hasPressureRelief = [&](int gx, int gy) {
        for (int k = 0; k < 4; ++k) {
            const int nx = gx + dx[k], ny = gy + dy[k];
            const Cell& n = cells[ny * SIM_W + nx];
            if (n.mat == MAT_EMPTY && !blocksCell(nx, ny)) return true;
            if (filterAllows(n.mat, gasMat) ||
                reactivePowderAllowsGas(n.mat, gasMat)) return true;
        }

        /* Merely touching liquid is not relief. In a deep lake that mistake
           labels the sides and bottom of a Steam blob as outlets even though
           neither can create volume; shared pressure then piles up there and
           waits for bubbles to crawl upward. A straight liquid column counts
           only when it actually reaches open space within the lift bound. Bent
           local outlets are still handled by the bounded liquid search below. */
        for (int d = 1; d <= GAS_PRESSURE_VERTICAL_REACH && gy - d >= PLAY_Y0; ++d) {
            const int ny = gy - d;
            const Cell& n = cells[ny * SIM_W + gx];
            if (n.mat == MAT_EMPTY) return !blocksCell(gx, ny);
            if (MATS[n.mat].kind != KIND_LIQUID) break;
        }

        /* A non-reactive powder face is relief only when the complete short
           plug can move into a real empty cell. Treating any adjacent powder
           as an outlet strands pressure against packed terrain; ignoring it
           makes interior pressure take many frames to reach a movable pile. */
        for (int k = 0; k < 4; ++k) {
            int count = 0, required = 0;
            for (int d = 1; d <= GAS_PRESSURE_POWDER_REACH + 1; ++d) {
                const int nx = gx + dx[k] * d, ny = gy + dy[k] * d;
                if (nx < PLAY_X0 || nx > PLAY_X1 ||
                    ny < PLAY_Y0 || ny > PLAY_Y1) break;
                const Cell& n = cells[ny * SIM_W + nx];
                if (n.mat == MAT_EMPTY) {
                    if (count && !blocksCell(nx, ny) &&
                        required <= (int)excess) return true;
                    break;
                }
                if (MATS[n.mat].kind != KIND_POWDER) break;
                const int resistance = g_matPressureResistance[n.mat];
                if (resistance == 255) break;
                required = imax(required, resistance + count);
                ++count;
                if (required > (int)excess) break;
            }
        }
        return false;
    };

    if (!hasPressureRelief(x, y) && L.scratch.pressureRoutes > 0) {
        --L.scratch.pressureRoutes;
        int receiver = -1, receiverDistance = 1000000, receiverExcess = 256;
        for (int k = 0; k < 4; ++k) {
            /* Long up and down, short sideways -- see the note on the two
               constants. dx[k] is non-zero exactly for the two sideways
               rays. */
            const int rayMax = dx[k] ? GAS_PRESSURE_POCKET_RAY_H
                                     : GAS_PRESSURE_POCKET_RAY_V;
            for (int d = 1; d <= rayMax; ++d) {
                const int nx = x + dx[k] * d, ny = y + dy[k] * d;
                if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1) break;
                const int ni = ny * SIM_W + nx;
                const Cell& n = cells[ni];
                if (n.mat != gasMat) break;
                const int nExcess = n.moisture & GAS_EXCESS_MASK;
                if (nExcess < (int)excess && hasPressureRelief(nx, ny) &&
                    (d < receiverDistance ||
                     (d == receiverDistance && nExcess < receiverExcess))) {
                    receiver = ni;
                    receiverDistance = d;
                    receiverExcess = nExcess;
                    break;
                }
            }
        }

        if (receiver < 0) {
            static const int R = GAS_PRESSURE_POCKET_RADIUS;
            static const int SIDE = R * 2 + 1;
            /* Sized in LaneScratch, one per thread. */
            u16* const seen  = L.scratch.pocketSeen;
            i16* const queue = L.scratch.pocketQueue;
            const u16 epoch = ++L.scratch.pocketEpoch ? L.scratch.pocketEpoch
                                              : (L.scratch.pocketEpoch = 1);
            if (epoch == 1 && L.scratch.pocketSeen[0] != 0)
                memset(seen, 0, sizeof(L.scratch.pocketSeen));

            const int center = R * SIDE + R;
            int head = 0, tail = 0;
            seen[center] = epoch;
            queue[tail++] = (i16)center;
            while (head < tail && receiver < 0) {
                const int li = queue[head++];
                const int lx = li % SIDE, ly = li / SIDE;
                for (int k = 0; k < 4; ++k) {
                    const int nlx = lx + dx[k], nly = ly + dy[k];
                    if (nlx < 0 || nlx >= SIDE || nly < 0 || nly >= SIDE) continue;
                    const int niLocal = nly * SIDE + nlx;
                    if (seen[niLocal] == epoch) continue;
                    seen[niLocal] = epoch;
                    const int nx = x - R + nlx, ny = y - R + nly;
                    if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1) continue;
                    const int ni = ny * SIM_W + nx;
                    const Cell& n = cells[ni];
                    if (n.mat != gasMat) continue;
                    const int nExcess = n.moisture & GAS_EXCESS_MASK;
                    if (nExcess < (int)excess && hasPressureRelief(nx, ny)) {
                        receiver = ni;
                        receiverExcess = nExcess;
                        break;
                    }
                    if (tail < GAS_PRESSURE_POCKET_NODES)
                        queue[tail++] = (i16)niLocal;
                }
            }
        }

        if (receiver >= 0) {
            Cell& r = cells[receiver];
            const int room = GAS_EXCESS_MASK - receiverExcess;
            /* Raise the boundary to the donor's pressure level. An empty skin
               cell therefore receives the whole charge and the next donor
               seeks another lower-pressure outlet instead of piling onto the
               same column. A partially charged receiver only takes the
               difference, preserving the ordinary equalization behaviour. */
            int give = imax(1, (int)excess - receiverExcess);
            give = imin(give, room);
            give = imin(give, (int)excess);
            c.moisture = (u8)((c.moisture & GAS_VOLUME_ONLY) | ((int)excess - give));
            r.moisture = (u8)((r.moisture & GAS_VOLUME_ONLY) | (receiverExcess + give));
            dirtyPoint(L, x, y);
            dirtyPoint(L, receiver % SIM_W, receiver / SIM_W);
            return true;
        }
    }

    /* A short bent pipe, cavity, or sloping shoreline needs more than a
       vertical ray. Search only a 33x33 box around the gas. parent[] is both
       the visited set and the path back to the first adjacent liquid, so the
       work and the storage have hard upper bounds independent of world or
       lake size. */
    static const int R = GAS_PRESSURE_LIQUID_RADIUS;
    static const int SIDE = R * 2 + 1;

    const int orderDx[4] = { 0, dir, -dir, 0 };
    const int orderDy[4] = { -1, 0, 0, 1 };

    /* Nothing to search without a liquid FACE. The seed loop below can only
       push adjacent liquid, so a cell with none of it walks nowhere and finds
       nothing -- but it used to pay for the whole apparatus first. That is not
       a rare case: it is every interior cell of a steam blob, and a boiling
       pool has thousands of them per frame.

       Measured, lava dropped into a water pool: this search was 4.28 ms of a
       12.5 ms sim step, called 6,597 times a frame. See tools/steamprof.cpp,
       which is the harness that found it -- tools/profile.cpp measures steady
       states and this cost only exists during the transient. */
    bool anyLiquidFace = false;
    for (int k = 0; k < 4 && !anyLiquidFace; ++k) {
        const int nx = x + orderDx[k], ny = y + orderDy[k];
        if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1) continue;
        if (MATS[cells[ny * SIM_W + nx].mat].kind == KIND_LIQUID) anyLiquidFace = true;
    }

    if (anyLiquidFace) {
        /* parent[] is STAMPED rather than cleared, exactly as the pocket flood
           above stamps its `seen`. Clearing it was 1,089 stores on every call
           whatever the search then cost -- 1.70 ms/frame of the 4.28, spent
           before the first cell was even looked at. The two arrays are static
           for the same reason they can be: this is one thread, and the search
           never re-enters itself.

           parent[k] is meaningful only where parentEpoch[k] == epoch; the
           wrap clears once every 65,536 searches and costs one memset. */
        i16* const parent      = L.scratch.parent;
        u16* const parentEpoch = L.scratch.parentEpoch;
        i16* const queue       = L.scratch.bfsQueue;
        u16 epoch = ++L.scratch.bfsEpoch;
        if (epoch == 0) {
            memset(parentEpoch, 0, sizeof(L.scratch.parentEpoch));
            epoch = L.scratch.bfsEpoch = 1;
        }

        int head = 0, tail = 0;
        for (int k = 0; k < 4; ++k) {
            const int nx = x + orderDx[k], ny = y + orderDy[k];
            if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1) continue;
            if (MATS[cells[ny * SIM_W + nx].mat].kind != KIND_LIQUID) continue;
            const int li = (ny - (y - R)) * SIDE + (nx - (x - R));
            if (parentEpoch[li] == epoch) continue;
            parentEpoch[li] = epoch;
            parent[li] = -1;
            queue[tail++] = (i16)li;
        }

        int goal = -1, outletX = 0, outletSearchY = 0;
        while (head < tail && goal < 0) {
            const int li = queue[head++];
            const int lx = li % SIDE, ly = li / SIDE;
            const int wx = x - R + lx, wy = y - R + ly;
            for (int k = 0; k < 4; ++k) {
                const int nx = wx + orderDx[k], ny = wy + orderDy[k];
                if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1) continue;
                const Cell& n = cells[ny * SIM_W + nx];
                if (n.mat == MAT_EMPTY && !blocksCell(nx, ny)) {
                    goal = li; outletX = nx; outletSearchY = ny; break;
                }
            }
            if (goal >= 0) break;

            for (int k = 0; k < 4; ++k) {
                const int nlx = lx + orderDx[k], nly = ly + orderDy[k];
                if (nlx < 0 || nlx >= SIDE || nly < 0 || nly >= SIDE) continue;
                const int ni = nly * SIDE + nlx;
                if (parentEpoch[ni] == epoch) continue;
                const int nx = x - R + nlx, ny = y - R + nly;
                if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1) continue;
                if (MATS[cells[ny * SIM_W + nx].mat].kind != KIND_LIQUID) continue;
                parentEpoch[ni] = epoch;
                parent[ni] = (i16)li;
                queue[tail++] = (i16)ni;
            }
        }

        if (goal >= 0) {
            int dst = outletSearchY * SIM_W + outletX;
            int path = goal;
            while (path >= 0) {
                const int px = x - R + path % SIDE;
                const int py = y - R + path / SIDE;
                const int src = py * SIM_W + px;
                cells[dst] = cells[src];
                temp[dst] = temp[src];
                cells[dst].flags = (u8)((cells[dst].flags & F_DIR) | pressureStamp);
                dirtyPoint(L, dst % SIM_W, dst / SIM_W);
                dst = src;
                path = parent[path];
            }
            finishExpansion(dst);
            return true;
        }
    }

    /* No free volume here. Move pressure toward the least-compressed adjacent
       parcel of the same gas so a connected pocket can reach an opening at its
       edge. Stamp the receiver to prevent one unit relaying across a room in a
       single bottom-to-top scan. */
    int best = -1, bestExcess = 256;
    for (int k = 0; k < 4; ++k) {
        const int nx = x + NB_DX[k], ny = y + NB_DY[k];
        const int ni = ny * SIM_W + nx;
        const Cell& n = cells[ni];
        if (n.mat != c.mat) continue;
        const int nExcess = n.moisture & GAS_EXCESS_MASK;
        if (nExcess < bestExcess) { bestExcess = nExcess; best = ni; }
    }
    if (best < 0 || (int)excess <= bestExcess + 1) return false;

    Cell& n = cells[best];
    const int give = imax(1, ((int)excess - bestExcess) / 2);
    c.moisture = (u8)((c.moisture & GAS_VOLUME_ONLY) | ((int)excess - give));
    n.moisture = (u8)((n.moisture & GAS_VOLUME_ONLY) | (bestExcess + give));
    const u8 st = (u8)(stamp() << STAMP_SHIFT);
    n.flags = (u8)((n.flags & F_DIR) | st);
    dirtyPoint(L, x, y);
    dirtyPoint(L, best % SIM_W, best / SIM_W);
    return true;
}

void World::updateGas(Lane& L, int x, int y) {
    Cell& c = cells[y * SIM_W + x];
    const MatInfo& m = MATS[c.mat];

    if (updateGasPressure(L, x, y)) return;

    /* A submerged bubble gets one upward step per turn, but that step may be
       diagonal. This widens a plume naturally without restoring the old bug:
       unrestricted sideways swaps let water relay one steam cell across a pool
       during an in-place scan. Here lateral travel is bounded by vertical
       travel -- after rising N cells a bubble can be at most N cells sideways. */
    const u8 aboveKind = MATS[cells[(y - 1) * SIM_W + x].mat].kind;
    if (aboveKind == KIND_LIQUID) {
        if (m.jitter && lchance(L, m.jitter)) {
            const int jd = (lrand(L) & 1u) ? 1 : -1;
            if (tryMove(L, x, y, x + jd, y - 1)) return;
            if (tryMove(L, x, y, x - jd, y - 1)) return;
        }
        if (tryMove(L, x, y, x, y - 1)) return;
    }

    /* --- diffusion --------------------------------------------------------
       Before trying to rise, a gas has a per-material chance of taking one step
       in a RANDOM direction instead. This is what makes a gas fill a space
       rather than draw a line through it.

       It used to be a pure sideways hop, and the measurement that replaced it
       is worth keeping. A plume released on the floor of a sealed 160x80 room
       was, by frame 240, a SINGLE ROW against the ceiling at about a fifth
       density, with the other 79 rows completely empty -- it reads as the gas
       evaporating rather than expanding.

       Every individual rule was right. Buoyancy in this simulation is
       UNCONDITIONAL -- a gas whose cell above is empty always rises -- because
       empty is VACUUM rather than air: there is no medium to diffuse through
       and nothing to hold a parcel up. So every parcel climbs to the roof, the
       lateral run spreads it along the roof, and nothing ever brings any of it
       back down. That is not a gas, it is a rising line that turns into a
       shelf.

       What was missing is that a real gas has no preferred direction, only a
       BIAS. The table below is therefore NEARLY ISOTROPIC rather than strongly
       upward: seven of nineteen draws go up, six go sideways, six go down. Net
       drift is still upward -- steam still leaves a boiler and smoke still
       leaves a fire -- but the parcel spends most of its time doing a random
       walk, so the plume expands as a body. Same scene, same frame, after: a
       cloud fourteen rows deep.

       This only works together with a HIGH jitter chance on the material (see
       the note beside Steam in materials.cpp). The table decides where a
       diffusing parcel goes; the jitter chance decides how often strict
       buoyancy gets a turn at all. A wider table with the old 150/255 chance
       measured as doing nothing, because a parcel that wandered down simply
       rose again on the next frame it did not jitter.

       Downward draws are excluded while SUBMERGED. A bubble under water that
       wandered downward would be a bubble sinking, which is both wrong and
       exactly the thing the branch above this exists to get right. */
    if (m.jitter && lchance(L, m.jitter)) {
        static const i8 DIFFUSE_DX[19] = {  0,  0,  0, -1, -1,  1,  1,
                                           -1, -1, -1,  1,  1,  1,
                                           -1, -1,  0,  0,  1,  1 };
        static const i8 DIFFUSE_DY[19] = { -1, -1, -1, -1, -1, -1, -1,
                                            0,  0,  0,  0,  0,  0,
                                            1,  1,  1,  1,  1,  1 };
        /* The first 13 are level or upward, so restricting a submerged bubble
           to that prefix is the whole guard -- no second table. */
        const int span = (aboveKind == KIND_LIQUID) ? 13 : 19;
        const int k = (int)(lrand(L) % (u32)span);
        if (tryMove(L, x, y, x + DIFFUSE_DX[k], y + DIFFUSE_DY[k])) return;
    }

    /* The buoyant climb. Scans up through clear air first and takes the whole
       material-specific run in one move; see gasRiseRun. Empty cells only, so
       this can never shortcut a swap with a liquid or a denser gas -- those
       still go one cell at a time through the tryMove below, which is what
       keeps the displacement rules that make a bubble behave in charge of a
       bubble. */
    {
        const int riseRun = gasRiseRun(c.mat);
        int top = y;
        for (int s = 1; s <= riseRun; ++s) {
            const int ny = y - s;
            if (ny < PLAY_Y0) break;
            if (cells[ny * SIM_W + x].mat != MAT_EMPTY || blocksCell(x, ny)) break;
            top = ny;
        }
        if (top != y && tryMove(L, x, y, x, top)) { dirtyPoint(L, x, top); return; }
    }

    /* A mirror of updateLiquid with the vertical sense flipped. */
    if (tryMove(L, x, y, x, y - 1)) return;
    if (updateConvection(L, x, y)) return;

    int dx = (c.flags & F_DIR) ? 1 : -1;
    if (tryMove(L, x, y, x + dx, y - 1)) return;
    if (tryMove(L, x, y, x - dx, y - 1)) return;

    for (int attempt = 0; attempt < 2; ++attempt) {
        int moveFromX = x, moveToX = x;
        for (int s = 1; s <= (int)m.dispersion; ++s) {
            int nx = x + dx * s;
            if (nx < PLAY_X0 || nx > PLAY_X1) break;
            const Cell& n = cells[y * SIM_W + nx];
            if (n.mat != MAT_EMPTY) {
                if (filterAllows(n.mat, c.mat) ||
                    reactivePowderAllowsGas(n.mat, c.mat)) {
                    const int beyond = nx + dx;
                    /* Mesh traversal still wants an exit beyond the filter.
                       Reactive powder is itself the destination: Steam only
                       needs to touch Coal to be consumed into Fuel. */
                    if (reactivePowderAllowsGas(n.mat, c.mat) ||
                        (beyond >= PLAY_X0 && beyond <= PLAY_X1 &&
                         cells[y * SIM_W + beyond].mat == MAT_EMPTY))
                        moveToX = nx;
                    break;
                }
                if (n.mat == c.mat) { moveFromX = nx; continue; }
                const MatInfo& nm = MATS[n.mat];
                if (nm.kind != KIND_GAS || nm.density <= m.density) break;
            }
            moveToX = nx;
            break;
        }
        if (moveToX != x && tryMove(L, moveFromX, y, moveToX, y)) {
            dirtyArea(L, imin(x, moveToX), y, imax(x, moveToX), y);
            return;
        }
        dx = -dx;
        c.flags ^= F_DIR;
    }
}

/* ======================================================================
   Heat

   Runs for every cell in a dirty rect, air included, so warmth can cross open
   space. The cost is kept off settled scenery by a single early-out: a cell
   already at ambient does nothing. Conduction is symmetric, so a cell sitting
   at ambient still gets warmed by a hot neighbour on that neighbour's turn --
   it just does not pay to look for one itself. Anything off-ambient dirties
   itself, so a hot region stays awake exactly until it has cooled.
   ====================================================================== */
/* One symmetric exchange between two cells. Split out of updateHeat so the
   long-range pass below can reuse it EXACTLY -- the cap and the mass scaling
   are what keep temperatures inside [0,255] with no second buffer, and a
   second hand-written copy of that arithmetic is precisely the kind of thing
   that drifts out of sync and starts quietly manufacturing heat. */
void World::heatPair(Lane& L, int i, int jx, int jy) {
    const int j = jy * SIM_W + jx;
    const int ti = temp[i], tj = temp[j];
    int diff = ti - tj;
    int adiff = diff < 0 ? -diff : diff;
    if (adiff <= HEAT_MIN_DIFF) return;

    {
        /* The poorer of the two conductors sets the rate, so metal-on-metal is
           quick while a hot cell next to air barely bleeds. Air touching air is
           the one exception, and uses AIR_MIX instead -- see the note there for
           why the same number cannot serve both cases. */
        const bool airPair = cells[i].mat == MAT_EMPTY && cells[j].mat == MAT_EMPTY;
        const int cond = airPair ? AIR_MIX
                                 : imin((int)MATS[cells[i].mat].heatCond,
                                        (int)MATS[cells[j].mat].heatCond);
        int move = (adiff * cond) >> 9;
        if (move > (adiff >> 1)) move = adiff >> 1;   /* never cross the average */
        /* Round up to 1 only for real conductors. A near-insulator -- steam,
           air -- keeps its true (often zero) rate so it holds heat and cools
           mainly by drift, which is what lets steam ride far up before it
           condenses instead of shedding a degree a frame into cold air.

           Air-to-air is excluded from the round-up as well, despite AIR_MIX
           being well over 40, and that exclusion is what makes AIR_MIX a real
           dial instead of an on/off switch. With the round-up, every value from
           40 to 96 behaved identically -- the flat 1-degree floor swamped the
           proportional term, so air always mixed at exactly one degree per
           neighbour per frame. Without it, mixing is proportional to the
           gradient: brisk across a sharp front, gentle once the field has
           smoothed out, which is both what diffusion should look like and what
           lets a warm patch settle instead of grinding to ambient. */
        if (move < 1) { if (cond < 40 || airPair) return; move = 1; }

        /* Thermal mass. The same quantity of heat moves, but each side's
           TEMPERATURE responds in proportion to its own mass -- so lava barely
           cools while whatever it touches heats at full rate.

           Both sides must be scaled by their own mass, not just the cell being
           updated. Every cell runs this loop, so if only the updater were
           scaled, a neighbouring stone pulling heat out of lava on its own turn
           would take the full amount and undo the effect entirely.

           The remainder is carried stochastically rather than truncated: plain
           integer division rounds small transfers to zero, which would make a
           heavy material a perpetual heat source that never cools and never
           lets its chunk sleep. */
        const int deltaI = scaleByMass(L, move, (int)MATS[cells[i].mat].heatMassShift);
        const int deltaJ = scaleByMass(L, move, (int)MATS[cells[j].mat].heatMassShift);

        if (diff < 0) { temp[i] = (u8)(ti + deltaI); temp[j] = (u8)(tj - deltaJ); }
        else          { temp[i] = (u8)(ti - deltaI); temp[j] = (u8)(tj + deltaJ); }
        dirtyPoint(L, jx, jy);
    }
}

void World::updateHeat(Lane& L, int x, int y) {
    const int i = y * SIM_W + x;

    /* Exchange with every neighbour, not one random one. The old single-random
       version only pushed heat a quarter of the time in any given direction,
       and with each cell also drifting toward ambient every frame the drift
       won past about ten cells -- so a heated iron bar stayed cold two
       centimetres from the flame. Touching all four neighbours makes the front
       advance a full cell per frame. */
    for (int k = 0; k < 4; ++k) {
        const int nx = x + NB_DX[k], ny = y + NB_DY[k];
        if (nx < 0 || nx >= SIM_W || ny < 0 || ny >= SIM_H) continue;
        heatPair(L, i, nx, ny);
    }

    /* --- warm air rises ---------------------------------------------
       Applied AFTER the symmetric exchange above, so this is a bias on top
       of ordinary conduction rather than a replacement for it. See the
       note on AIR_CONVECT_SHIFT in world.h for why air needs its own rule
       instead of joining updateConvection.

       Both cells must be air. A ceiling stops the plume, which is correct
       and is also what makes heat pool under a roof instead of leaking
       through it. */
    if (cells[i].mat == MAT_EMPTY && y > PLAY_Y0) {
        const int up = i - SIM_W;
        if (cells[up].mat == MAT_EMPTY) {
            const int here = (int)temp[i], there = (int)temp[up];
            const int diff = here - there;
            if (diff >= AIR_CONVECT_MIN) {
                int move = diff >> AIR_CONVECT_SHIFT;
                if (move < 1) move = 1;
                if (there + move > 255) move = 255 - there;
                if (move > 0) {
                    temp[i]  = (u8)(here - move);
                    temp[up] = (u8)(there + move);
                    dirtyPoint(L, x, y - 1);
                }
            }
        }
    }

    /* Long-range conduction, for the good conductors only.
       ------------------------------------------------------------------
       The neighbour loop above advances a heat front at exactly one cell per
       frame, and that ceiling is geometric rather than thermal: it holds no
       matter how conductive the material is, which is why iron at the maximum
       possible heatCond still warms a long bar visibly slowly. Relaxing it is
       the only way to make one conductor genuinely better than another once
       heatCond has saturated -- see the heatSpread note in materials.h.

       So a material with heatSpread also exchanges with the cell `spread` steps
       away along each axis, provided the whole run between them is conductive
       too. Walking to the FAR end and exchanging once, rather than exchanging
       with every cell along the way, is deliberate: it is O(1) exchanges for
       O(spread) steps of walking, and the intermediate cells are filled in by
       their own neighbour loops on the same frame anyway.

       The run is "any material with heatSpread > 0", not "the same material",
       so a copper bar bolted to an iron one conducts across the joint. It stops
       at the first non-conductor, so a gap of air or a wooden handle insulates
       exactly as you would expect -- the long-range hop cannot jump a break. */
    const int spread = (int)MATS[cells[i].mat].heatSpread;
    if (spread >= 2) {
        for (int k = 0; k < 4; ++k) {
            const int dx = NB_DX[k], dy = NB_DY[k];

            /* --- the fast path: probe, do not walk -------------------------
               The walk below exists for one reason -- to find where the
               conductive run ends -- and in the INTERIOR of a solid block the
               answer is always "the full distance". Walking every cell to
               rediscover that is what makes graphene expensive: at spread 28
               it is 112 steps per cell per frame, and two of the four
               directions stride SIM_W, so most of those steps are a cache miss.

               So sample a few points along the run instead of every one. If
               they are all conductive, take the full hop; if any is not, fall
               through to the exact walk. Boundaries are a small fraction of any
               blob worth worrying about, so the expensive path is rare.

               This is an APPROXIMATION and the failure is worth stating: an
               insulating barrier thinner than the gap between probes can be
               jumped, so heat crosses it as if it were not there. With probes
               at quarters of a 28-cell span the widest thing that can hide is
               six cells. Anything a player builds as insulation is either
               thicker than that or is up against the conductor, where the
               immediate-neighbour pass sees it regardless. */
            const int fxFull = x + dx * spread, fyFull = y + dy * spread;
            bool clear = fxFull >= 0 && fxFull < SIM_W && fyFull >= 0 && fyFull < SIM_H;
            if (clear) {
                for (int q = 1; q <= SPREAD_PROBES && clear; ++q) {
                    const int t = spread * q / SPREAD_PROBES;
                    const int px = x + dx * t, py = y + dy * t;
                    if (MATS[cells[py * SIM_W + px].mat].heatSpread == 0) clear = false;
                }
            }
            if (clear) {
                if (spread > 1) heatPair(L, i, fxFull, fyFull);
                continue;
            }

            /* --- the exact walk, for cells at a boundary ------------------- */
            int cx = x, cy = y, fx = -1, fy = -1;
            for (int s = 0; s < spread; ++s) {
                cx += dx; cy += dy;
                if (cx < 0 || cx >= SIM_W || cy < 0 || cy >= SIM_H) break;
                if (MATS[cells[cy * SIM_W + cx].mat].heatSpread == 0) break;
                fx = cx; fy = cy;
            }
            /* Only if we got past the immediate neighbour -- that pair was
               already handled above, and doing it twice would just double
               iron's rate to its nearest neighbour for no reason. */
            if (fx >= 0 && (fx != x + dx || fy != y + dy)) heatPair(L, i, fx, fy);
        }
    }

    /* Heat leaves mainly through the air: air cells drift toward ambient fast,
       standing in for an open room that carries warmth away. Solids and liquids
       drift far more slowly (SOLID_COOL << AIR_COOL), so they lose heat mostly
       by conducting it into a cooler neighbour, air included, and therefore
       carry it long distances -- which is what finally lets heat creep the
       whole length of an iron bar. The old model bled every cell equally each
       frame, so the far end of a bar never warmed. The slow solid term is not
       there for reach; it just guarantees a hot region eventually reaches
       ambient and lets its chunk sleep, instead of glowing faintly forever. */
    int t = temp[i];
    if (t != AMBIENT_TEMP) {
        const int kind = MATS[cells[i].mat].kind;
        int rate = (cells[i].mat == MAT_EMPTY) ? AIR_COOL
                 : (kind == KIND_GAS)          ? GAS_COOL
                 :                               SOLID_COOL;
        if (lchance(L, rate)) {
            /* The BACKGROUND gets a say, and this is the only place in the whole
               simulation that reads the bg array -- see the note on World::bg,
               which until now said nothing ever did. A ceramic-lined chamber holds
               its heat; bare rock holds a little; open sky holds none.

               Read INSIDE the roll rather than before it, which is what makes it
               affordable. The drift fires on a few percent of cell-frames, so on
               the overwhelming majority of them the bg array is never touched and
               the extra cache line is never pulled in. Putting the lookup outside
               would make every hot cell in the world pay it every frame.

               A second roll rather than an adjusted rate: `rate` is compared
               against the rng by rngChance and scaling it would need a divide in
               the hot path, where a second chance test is another compare against
               a byte already in a register. */
            const u8 back = (u8)(bg[i] & BG_MAT_MASK);
            const u8 hold = g_bgRetain[back];
            if (!hold || !lchance(L, hold))
                temp[i] = (u8)(t > AMBIENT_TEMP ? t - 1 : t + 1);
        }
    }

    /* --- natural heat ---------------------------------------------------
       A molten backdrop holds its cells at a floor temperature, which is what
       makes a lava hotspot a permanent feature rather than a pocket that cools
       out the first time you look at it. See g_bgHeat in materials.h.

       Outside the cooling roll above, because this has to hold whether or not
       the cell was drifting -- but still only a single table lookup on a byte
       already fetched for retention, and it writes NOTHING when the cell is
       already at or above the floor, so a settled pocket dirties no chunk and
       sleeps like anything else. */
    {
        const u8 floorMat = (u8)(bg[i] & BG_MAT_MASK);
        const u8 floorT = g_bgHeat[floorMat];
        if (floorT && temp[i] < floorT) {
            temp[i] = floorT;
            dirtyPoint(L, x, y);
        }
    }

    /* --- why this wakes unconditionally ---------------------------------

       It looks like waste and it was investigated as such: every cell whose
       heat is updated re-arms itself for the next frame whether or not a degree
       moved, so no thermal field in this game ever comes fully to rest. A
       settled lava pocket costs about 64 chunks a frame indefinitely.

       Two ways of making it conditional were tried and both are wrong.

       A TOLERANCE on the change ("only wake if it moved more than N") is what
       actually reduces wakefulness -- with N=3 a graphene column over lava went
       from 24 ms a frame to 9 -- and it stalls slow work. Measured: `cold`
       failed, because liquid nitrogen chilling an iron bar moves each cell one
       degree a frame, none re-armed, and the chill stopped twenty cells short.
       N=4 and N=5 failed harder and took `melt` and `heat` with them.

       A plain "did it change at all" avoids that and breaks something else: a
       PINNED source has a constant temperature by definition. Lava held at
       215 C by its molten backdrop never changes, so it would stop waking
       itself, sleep, and quietly stop heating anything -- `heat` and `lava4`
       both caught it.

       The real fix is a signal over TIME rather than magnitude, per chunk:
       accumulate the signed sum of temperature deltas and a count of cells that
       moved, and treat a chunk with plenty of movement and near-zero net drift
       as converged. At equilibrium the changes cancel -- total heat drifted
       0.0037% over 600 frames -- while genuine transfer drifts one way, and a
       pinned source keeps its neighbours drifting so it never looks converged.
       Until that exists, this stays unconditional on purpose. */
    dirtyPoint(L, x, y);
}

/* Table-driven phase conversion plus gas expansion charge. `convert` remains
   the right operation for chemistry and combustion; only a LIQUID becoming a
   GAS owns the volume increase that later becomes pressure. */
void World::phaseChange(Lane& L, int x, int y, u8 mat) {
    Cell& c = cells[y * SIM_W + x];
    const u8 from = c.mat;
    const bool expands = MATS[from].kind == KIND_LIQUID &&
                         MATS[mat].kind == KIND_GAS;
    convert(L, x, y, mat);
    if (!expands) return;
    const int volumes = imax(1, (int)g_matGasExpansion[mat]);
    c.moisture = (u8)imin((int)GAS_EXCESS_MASK, volumes - 1);
}

/* ======================================================================
   Moisture: absorption, percolation, drainage

   The whole model is O(1) per wet cell per frame and rides along inside the
   normal update pass -- there is no separate diffusion sweep. Each cell soaks
   up at most one touching water cell, then exchanges moisture with exactly
   one randomly chosen neighbour. Averaged over frames that behaves like
   diffusion with a downward bias, which is all we need it to look like.
   ====================================================================== */
void World::updateMoisture(Lane& L, int x, int y) {
    Cell& c = cells[y * SIM_W + x];
    const MatInfo& m = MATS[c.mat];

    /* --- absorb a touching water cell ---------------------------------- */
    if ((int)c.moisture + MOISTURE_UNIT <= (int)m.capacity) {
        static const int OFF[4][2] = { {0,-1}, {-1,0}, {1,0}, {0,1} };
        u32 start = lbits(L, 2);   /* rotate the scan order, else water always
                                     gets eaten from the same side first */
        for (u32 k = 0; k < 4; ++k) {
            const int* o = OFF[(start + k) & 3];
            int nx = x + o[0], ny = y + o[1];
            Cell& n = cells[ny * SIM_W + nx];
            if (n.mat == MAT_WATER) {
                n.mat      = MAT_EMPTY;
                n.moisture = 0;
                c.moisture = (u8)(c.moisture + MOISTURE_UNIT);
                dirtyPoint(L, nx, ny);
                dirtyPoint(L, x, y);
                break;
            }
        }
    }

    if (c.moisture == 0) return;

    /* --- exchange with one neighbour ----------------------------------- */
    u32 r = lrand(L) & 7;
    int nx = x, ny = y, dirShift;
    if      (r == 4) { ny = y - 1; dirShift = WICK_UP_SHIFT;   }  /* 1/8 up   */
    else if (r == 5) { nx = x - 1; dirShift = WICK_SIDE_SHIFT; }  /* 1/8 left */
    else if (r == 6) { nx = x + 1; dirShift = WICK_SIDE_SHIFT; }  /* 1/8 right*/
    else             { ny = y + 1; dirShift = WICK_DOWN_SHIFT; }  /* 5/8 down */

    Cell& n = cells[ny * SIM_W + nx];
    const MatInfo& nm = MATS[n.mat];

    if (nm.capacity == 0) {
        /* Material near saturation with open space beneath it sheds a drop, so
           water still drains through the underside of a thin layer and pools
           up again below. A thick layer never gets wet enough that far down to
           trigger this, which is the point. Exactly one unit leaves, so the
           drop that appears is the one that was absorbed earlier. */
        if (ny > y && n.mat == MAT_EMPTY &&
            (int)c.moisture > (int)m.capacity - MOISTURE_UNIT) {
            n.mat      = MAT_WATER;
            n.moisture = 0;
            n.tint     = (u8)lbits(L, 8);
            n.flags    = (u8)(stamp() << STAMP_SHIFT);   /* waits a frame */
            c.moisture = (u8)(c.moisture - MOISTURE_UNIT);
            dirtyPoint(L, x, y);
            dirtyPoint(L, nx, ny);
        }
        return;
    }

    int room = (int)nm.capacity - (int)n.moisture;
    if (room <= 0) return;

    /* Every direction, including downward, is purely gradient driven, and that
       one rule is what bounds the wet band. Moisture only moves into a cell
       drier than this one by more than the threshold, so at rest neighbours
       differ by about `wick`, hydration falls off linearly with distance from
       the water, and it stops entirely after roughly capacity/wick cells --
       leaving dry material beyond.
       Draining downward without consulting the neighbour is what made water
       tunnel to the floor: each cell shoved its excess down, the next did the
       same, and the front never ran out of anywhere to go. */
    /* Half the excess, not a quarter. The shift sets a floor on the resting
       gradient -- a transfer needs `diff - threshold` to reach 1<<shift, so
       neighbouring cells settle about `wick + (1<<shift)` apart. At >>2 that
       floor is 4 and swamps small wick values, which caps how far hydration
       can reach however low you tune it. >>1 halves the floor and so roughly
       doubles the available reach. */
    int diff   = (int)c.moisture - (int)n.moisture;
    int amount = (diff - ((int)m.wick << dirShift)) >> 1;
    if (amount <= 0) return;
    if (amount > MAX_TRANSFER) amount = MAX_TRANSFER;
    if (amount > room) amount = room;

    c.moisture = (u8)(c.moisture - amount);
    n.moisture = (u8)(n.moisture + amount);
    dirtyPoint(L, x, y);
    dirtyPoint(L, nx, ny);
}

/* ======================================================================
   Clone and Void

   Both are machines: static blocks that act on their 4-neighbourhood. Each is
   careful to dirty itself ONLY when it actually did something, so an idle
   dispenser or drain lets its chunk fall asleep. Waking again is automatic --
   any cell that moves or is painted dirties the 3x3 around it, which covers
   the machine next door.
   ====================================================================== */

/* Clone keeps the id of the material it copies in its `moisture` byte, which
   is otherwise dead weight for a material with no capacity. That costs no
   extra memory and keeps Cell at a tidy 4 bytes. */
/* This used to assert MAT_COUNT <= 16, on the grounds that the colour LUT
   indexes moisture as (moisture & 0xF0), so an id of 16 or more sets a nonzero
   wetness nibble and a latched clone would render from a different bucket.
   Adding Copper and Graphene took the count to 18 and tripped it.

   16 was a proxy for the real invariant, though, and a stricter one than
   necessary. What actually has to hold is that all 16 wetness buckets of
   MAT_CLONE are the SAME COLOUR -- which materials.cpp already guarantees on
   purpose, by giving Clone a flat palette (dryA == dryB == wetA == wetB) and no
   capacity. Given that, which bucket a stored id selects cannot matter.

   So the bound here is now just the byte, and the invariant the code truly
   depends on is checked directly, at startup, in initMaterials(). That is the
   better place for it: it fails if someone gives Clone a colour range or a
   capacity -- the changes that would genuinely break this -- rather than when
   the material list happens to get longer. */
static_assert(MAT_COUNT <= 256,
              "Clone stores its material id in the moisture byte, so ids must "
              "fit in a u8. See checkCloneColorInvariant() in materials.cpp for "
              "the rendering constraint that goes with it.");

/* Place a brand new cell of `mat`, as a source would: it gets the material's
   spawn temperature (so cloned fire arrives genuinely alight rather than
   instantly burning out) and is stamped with the CURRENT frame, so it waits
   until next frame to move. Stamping it as already-handled matters because the
   scan runs bottom-to-top: a cell emitted upward sits in a row this frame has
   not reached yet, and without the stamp it would get a free extra step. */
void World::spawnCell(Lane& L, int x, int y, u8 mat) {
    const int i = y * SIM_W + x;
    Cell& c = cells[i];
    c.mat      = mat;
    c.moisture = 0;
    c.tint     = (u8)lbits(L, 8);
    c.flags    = (u8)(stamp() << STAMP_SHIFT);
    const MatInfo& m = MATS[mat];
    temp[i] = m.spawnTemp ? m.spawnTemp : (u8)AMBIENT_TEMP;
    dirtyPoint(L, x, y);
}

void World::updateClone(Lane& L, int x, int y) {
    const int i = y * SIM_W + x;
    Cell& c = cells[i];

    /* Latch the first thing it touches, and keep it forever after -- that is
       what makes a permanent dispenser: the source can be taken away and the
       clone carries on producing.

       Latching looks at all 8 neighbours while emission (below) uses only the
       4 orthogonals. The wider catch is deliberate: the scan runs bottom-to-top,
       so a cell below the clone has already moved by the time the clone takes
       its turn, and a single drop falling past could otherwise slip through
       unnoticed. Emission stays orthogonal so material never squeezes through a
       diagonal gap between two solid blocks. */
    if (c.moisture == MAT_EMPTY) {
        for (int k = 0; k < 8; ++k) {
            const int nx = x + NB8_DX[k], ny = y + NB8_DY[k];
            if (nx < 0 || nx >= SIM_W || ny < 0 || ny >= SIM_H) continue;
            const u8 nm = cells[ny * SIM_W + nx].mat;
            /* Machines and scenery are not cloneable: copying a wall would let
               you wall off the box, and copying another machine would make
               self-replicating machines. Heater and cooler matter most here --
               a clone loaded with heaters emits them into every empty
               neighbour, and each new one is itself a permanent source that
               holds a chunk awake, so the thing grows without bound in both
               temperature and cost. Clone and void have always been excluded
               for the same reason; these two just make it expensive as well as
               silly. */
            if (nm == MAT_EMPTY || nm == MAT_WALL || nm == MAT_CLONE || nm == MAT_VOID
                || nm == MAT_HEATER || nm == MAT_COOLER) continue;
            c.moisture = nm;
            dirtyPoint(L, x, y);
            break;
        }
        return;   /* spend the frame latching; emit from the next one on */
    }

    /* Fill every empty neighbour. Deliberately not rate-limited: a dispenser
       should keep up with whatever drains it, and it is self-limiting anyway --
       a clone loaded with sand buries itself and then has nowhere left to emit,
       while one loaded with water or fire keeps going because the product
       flows or rises away. */
    for (int k = 0; k < 4; ++k) {
        const int nx = x + NB_DX[k], ny = y + NB_DY[k];
        if (cells[ny * SIM_W + nx].mat == MAT_EMPTY) spawnCell(L, nx, ny, c.moisture);
    }
}

void World::updateVoid(Lane& L, int x, int y) {
    for (int k = 0; k < 4; ++k) {
        const int nx = x + NB_DX[k], ny = y + NB_DY[k];
        const int j = ny * SIM_W + nx;
        const u8 nm = cells[j].mat;
        if (nm == MAT_EMPTY || nm == MAT_VOID) continue;
        /* Never eat the border. The movement rules do no bounds checking at
           all -- they rely on that ring of wall to stop a cell walking off the
           grid -- so a void that could chew through it would corrupt the sim. */
        if (nm == MAT_WALL) continue;
        cells[j].mat      = MAT_EMPTY;
        cells[j].moisture = 0;
        dirtyPoint(L, nx, ny);
        /* Temperature is left alone: the matter is gone but its warmth lingers
           in the air and dissipates normally, so a drain does not double as a
           perfect heat sink. */
    }
}

/* ======================================================================
   Evaporation

   Only a free surface evaporates, which the user's mental model wants and
   which happens to be the cheap option too: one 4-neighbour scan, and only for
   liquid cells that are awake. A surface cell keeps itself dirty so this goes
   on working after the pool has otherwise settled -- that keeps a thin strip
   alive rather than the whole body of water, which is a few hundred cells even
   for a pool spanning the box.

   Rate climbs with the square of how far above ambient the water is, so room
   temperature gives the occasional wisp and a hot pan steams hard, with no
   branchy special-casing between the two. Actual boiling is handled by the
   table-driven boilTemp path in updateCell and does not need a free surface.
   ====================================================================== */
void World::updateEvaporation(Lane& L, int x, int y) {
    const int i = y * SIM_W + x;
    const MatInfo& m = MATS[cells[i].mat];

    bool open = false;
    for (int k = 0; k < 4; ++k) {
        if (cells[(y + NB_DY[k]) * SIM_W + (x + NB_DX[k])].mat == MAT_EMPTY) { open = true; break; }
    }
    if (!open) return;
    dirtyPoint(L, x, y);

    int over = (int)temp[i] - AMBIENT_TEMP;
    if (over < 0) over = 0;
    /* The base is per-material now -- see g_matVolatility. Heat still adds the
       same square on top, so a hot pan of water behaves exactly as it did and
       the only thing the table changes is what a liquid does when nothing is
       heating it at all. */
    const u32 chance = (u32)g_matVolatility[cells[i].mat] + (u32)(over * over);
    if ((lrand(L) & 0xFFFF) >= chance) return;

    phaseChange(L, x, y, m.boilsTo);
    temp[i] = latentDrain((int)temp[i]);
}

/* ======================================================================
   Dispatch and the frame step
   ====================================================================== */

/* --- grass -----------------------------------------------------------------
   Two rules, both about air. Grass needs a face open to it, and dirt next to
   grass with a face open to it becomes grass.

   The interesting part is how this cooperates with the chunk system rather
   than fighting it. A spreading edge dirties itself every frame, so it stays
   awake and keeps working; a lawn with nothing left to grow onto dirties
   nothing and its chunks go to sleep, costing zero forever after. And because
   setCell already dirties a one-cell ring around whatever it touches, burying
   a grass cell or exposing new dirt beside one wakes it again on its own --
   there is no need for grass to poll, and no need for a separate list of it. */
/* Chance out of 255, per eligible neighbour, per frame. Measured, this is what
   sets how fast a lawn creeps: at 3 a single seeded cell reached 118 cells in
   4000 frames -- about nine-tenths of a cell per second in each direction,
   which is slow enough that sowing a patch and watching it looks like nothing
   is happening. 8 gives roughly two and a half cells a second, so a patch
   visibly takes over the ground around it within a few seconds and a whole
   hillside still takes a couple of minutes. */
static const int GRASS_SPREAD = 8;

bool World::airWithin(int x, int y, int r) const {
    if (r <= 1) {
        for (int k = 0; k < 4; ++k)
            if (cells[(y + NB_DY[k]) * SIM_W + (x + NB_DX[k])].mat == MAT_EMPTY) return true;
        return false;
    }
    /* Nearest rows first, so the common answer -- air just above -- is found
       before the scan has walked the far side of the disc. */
    const int x0 = imax(PLAY_X0, x - r), x1 = imin(PLAY_X1, x + r);
    const int y0 = imax(PLAY_Y0, y - r), y1 = imin(PLAY_Y1, y + r);
    const int rr = r * r;
    for (int d = 1; d <= r; ++d) {
        for (int sy = -1; sy <= 1; sy += 2) {
            const int ny = y + d * sy;
            if (ny < y0 || ny > y1) continue;
            const Cell* row = cells + ny * SIM_W;
            for (int nx = x0; nx <= x1; ++nx) {
                const int dx = nx - x, dy = ny - y;
                if (dx * dx + dy * dy > rr) continue;
                if (row[nx].mat == MAT_EMPTY) return true;
            }
        }
        /* The cell's own row, out to d, checked alongside so a horizontal face
           is found as early as a vertical one. */
        if (x - d >= x0 && cells[y * SIM_W + x - d].mat == MAT_EMPTY) return true;
        if (x + d <= x1 && cells[y * SIM_W + x + d].mat == MAT_EMPTY) return true;
    }
    return false;
}

/* One frame of a condemned leaf. Falls at zero: leaves leave nothing behind,
   and a pod leaves its seed -- the same thing breaking one by hand gives you,
   because losing a tree's seeds for felling it is exactly the punishment that
   would make nobody fell trees. The seed is a powder, so it drops out of the
   dying canopy and lands where you can pick it up. */
void World::updateLeafFall(Lane& L, int x, int y) {
    Cell& c = cells[y * SIM_W + x];
    if (--c.moisture) { dirtyPoint(L, x, y); return; }
    breakCell(L, x, y);
    /* The neighbourhood, not the cell: the light that was blocked by this leaf
       now reaches past it, and the cells under a vanishing canopy have to be
       given a look or the hole stays dark until something else wakes them. */
    dirtyArea(L, x - 1, y - 1, x + 1, y + 1);
}

void World::updateGrass(Lane& L, int x, int y) {
    /* Buried: nothing living survives out of reach of the air. */
    if (!airWithin(x, y, GRASS_DEPTH)) { convert(L, x, y, MAT_DIRT); return; }

    /* Spread along the 8-neighbourhood, not the 4. Ground in this world is
       rarely flat, and orthogonal-only spread stops dead at every one-cell
       step -- grass would climb a staircase and refuse a slope. */
    bool moreToDo = false;
    for (int k = 0; k < 8; ++k) {
        const int nx = x + NB8_DX[k], ny = y + NB8_DY[k];
        if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1) continue;
        if (cells[ny * SIM_W + nx].mat != MAT_DIRT) continue;
        /* Buried dirt stays dirt. Using the same reach the survival test above
           uses is what lets turf THICKEN on its own: the surface row greens
           first, and each row under it is within reach of the same air, so the
           band grows downward to GRASS_DEPTH and then stops because the rule
           runs out rather than because anything counts layers. */
        if (!airWithin(nx, ny, GRASS_DEPTH)) continue;
        moreToDo = true;
        if (lchance(L, GRASS_SPREAD)) convert(L, nx, ny, MAT_GRASS);
    }
    /* --- blossom ------------------------------------------------------
       A rare flower on turf that has room above it, so a fresh world grows
       its own bee food and nobody has to be told to plant some before a
       hive will do anything.

       Deliberately inside the spreading path, which means it happens while
       turf is still MOVING and stops when the field settles. That is what
       keeps it from being a slow leak: an old world does not accumulate
       flowers forever, it has however many it grew while it was greening.
       Planting more is what ITEM_FLOWER_SEED is for. */
    if (moreToDo && lchance(L, GRASS_FLOWER) &&
        y - 1 >= PLAY_Y0 && cells[(y - 1) * SIM_W + x].mat == MAT_EMPTY)
        setCell(L, x, y - 1, MAT_FLOWER);

    /* Only stay awake while there is still somewhere to go. */
    if (moreToDo) dirtyPoint(L, x, y);
}

void World::updateCell(Lane& L, int x, int y) {
    const int i = y * SIM_W + x;

    /* Heat first, and for every cell -- air and walls included -- so warmth
       crosses open space. Cells already at ambient cost a single compare. */
    if (temp[i] != AMBIENT_TEMP) updateHeat(L, x, y);

    Cell& c = cells[i];
    if (c.mat == MAT_EMPTY || c.mat == MAT_WALL) return;

    const u8 st = stamp();
    if (((c.flags >> STAMP_SHIFT) & STAMP_MASK) == st) {
        /* Already had its turn this frame -- it moved here from somewhere the
           scan had not reached yet. Dirty it so it is guaranteed another look
           next frame: that costs nothing here (whatever moved it already
           dirtied these cells) and it is what makes a stale stamp, on the rare
           frame one aliases, heal itself instead of stranding the cell. */
        dirtyPoint(L, x, y);
        return;
    }
    c.flags = (u8)((c.flags & F_DIR) | (st << STAMP_SHIFT));

    const MatInfo& m = MATS[c.mat];

    /* Sieve moisture is a coexisting fluid material id, not absorbed water.
       Give that parcel its turn before applying phase/reaction tables to the
       mesh material itself. */
    if ((c.mat == MAT_SIEVE || c.mat == MAT_GAS_SIEVE) && c.moisture) {
        updateFilterFluid(L, x, y);
        return;
    }

    /* Reactive powders have one equally sparse gas-occupant state. Unlike a
       sieve this is never long-lived: the admission rule above only accepts
       the exact gas named by wetBy, so consuming the occupant and converting
       the powder is unconditional here. It runs before ignition for the same
       reason the adjacent slaking rule does -- 115 C Steam must make Fuel, not
       light the Coal before the wet reaction gets a chance. */
    if (c.moisture && reactivePowderAllowsGas(c.mat, occupantMat(c.moisture))) {
        const u8 into = g_matWetInto[c.mat];
        convert(L, x, y, into);          /* also clears the consumed gas occupant */
        return;
    }

    /* --- a seed that has come to rest ---------------------------------
       Reported, not acted on: whether this becomes a tree is tree.cpp's
       business and depends on a table this file has no reason to know about.
       See World::sprout.

       The test is deliberately here rather than in the powder rules, because
       it wants a cell that is being LOOKED AT rather than one that has just
       moved -- a seed still falling past wet ground should not root in mid-air,
       and a seed that has settled is exactly a seed whose chunk is still awake
       from the frame it landed on. */
    /* Staged into this stripe's slot, merged in stripe order once the scan
       is done -- see the lane block at the top of this file. The cap is per
       stripe now, so a world full of falling seeds can report a few more of
       them per frame than it used to; tree.cpp takes what it is given and
       the rest keep their chunks awake until next frame either way. */
    if (g_matIsSeed[c.mat]) {
        const int st = L.stripe;
        int& sproutN = (st >= 0) ? g_stripeSproutN[st] : sproutCount;
        i32* const sproutTo = (st >= 0) ? g_stripeSprout[st] : sprout;
        const u8 below = (y < PLAY_Y1) ? cells[(y + 1) * SIM_W + x].mat : (u8)MAT_WALL;
        if (sproutN < MAX_SPROUTS && (below == MAT_DIRT || below == MAT_GRASS)) {
            sproutTo[sproutN++] = i;
            /* Kept awake until somebody deals with it. Without this a seed that
               lands while the tree table happens to be full settles for ever
               and never gets a second look. */
            dirtyPoint(L, x, y);
        }
    }

    /* --- table-driven phase changes ----------------------------------- */
    const int t = temp[i];
    if (m.boilTemp && t >= (int)m.boilTemp) {
        /* Ore is the one material whose phase change has TWO products: it
           smelts into a mixture of molten metal and molten slag, and which one
           this particular cell yields is a coin weighted by g_matSmeltYield.
           Everything else about it -- the threshold, the latent heat below, the
           freezing of both products afterwards -- is the ordinary table-driven
           path, which is why this is three lines rather than a rule of its own.

           Per CELL, not per pile, and that is the design: one ore cell tells you
           nothing, two hundred give a dependable ratio, so yield becomes a
           property of how much rock you shifted. The separation that follows is
           not implemented anywhere -- molten slag is lighter than either molten
           metal, so the existing density rule sinks the metal through it. */
        u8 into = m.boilsTo;
        if (g_matSmeltYield[c.mat] && !lchance(L, g_matSmeltYield[c.mat]))
            into = MAT_SLAG_MELT;
        phaseChange(L, x, y, into);
        /* Boiling absorbs latent heat. Without this a single hot cell flashes
           an entire pool to steam in one frame instead of simmering. */
        temp[i] = latentDrain(t);
        return;
    }
    /* Slaking: coal touching STEAM becomes fuel, and the steam is spent.

       BEFORE the ignition check below, and that ordering is the whole reason the
       steam route works at all. Steam spawns at 115 C and coal ignites at 90, so a
       coal pile held in steam is by definition above its own ignition point --
       tested the other way round it simply caught fire and made no fuel ever.
       Slaking winning is also the right physical story: wet coal does not light. Sits
       beside quenchedBy because the shape is identical -- look at four neighbours
       for a particular material -- but the two are deliberately separate columns.
       A quench DESTROYS the cell and dumps its heat into whatever put it out; this
       TRANSFORMS it and is a cold process on cold coal. See g_matWetInto. */
    if (g_matWetInto[c.mat]) {
        for (int k = 0; k < 4; ++k) {
            const int nx = x + NB_DX[k], ny = y + NB_DY[k];
            const int j = ny * SIM_W + nx;
            if (cells[j].mat != g_matWetBy[c.mat]) continue;
            convert(L, x, y, g_matWetInto[c.mat]);
            /* One reagent VOLUME is consumed. For an ordinary Steam cell that
               means removing the cell, as before. A compressed cell also owns
               hidden expansion volumes, though, and deleting it wholesale
               would let one Coal contact erase an entire boiler charge. Spend
               one excess unit first and leave the visible owner/provenance
               volume in place; only the last unit becomes Empty. */
            Cell& reagent = cells[j];
            const u8 reagentExcess = MATS[reagent.mat].kind == KIND_GAS
                                   ? (u8)(reagent.moisture & GAS_EXCESS_MASK) : 0;
            if (reagentExcess) {
                reagent.moisture = (u8)((reagent.moisture & GAS_VOLUME_ONLY) |
                                        (reagentExcess - 1));
                reagent.flags = (u8)((reagent.flags & F_DIR) |
                                     (stamp() << STAMP_SHIFT));
                dirtyPoint(L, nx, ny);
            } else {
                spawnCell(L, nx, ny, MAT_EMPTY);
                dirtyPoint(L, nx, ny);
            }
            return;
        }
    }

    /* --- alloying -------------------------------------------------------
       Same shape as the slaking check just above -- a cell converts on
       contact with a specific neighbour -- but the neighbour SURVIVES. See
       the note on g_matAlloyWith in materials.h for why this could not just
       be another g_matWetInto row: bronze's whole point is that neither
       ingredient is spent, and wetInto always destroys the thing it
       touched. Both directions are registered (copper melt reacts to tin
       melt and tin melt reacts to copper melt), so this fires and converts
       the cell being updated regardless of which of the pair it is. */
    if (g_matAlloyWith[c.mat]) {
        for (int k = 0; k < 4; ++k) {
            const int nx = x + NB_DX[k], ny = y + NB_DY[k];
            if (cells[ny * SIM_W + nx].mat != g_matAlloyWith[c.mat]) continue;
            convert(L, x, y, g_matAlloysTo[c.mat]);
            return;
        }
    }

    if (m.igniteTemp) {
        /* A flammable cell catches two ways: heated past its ignition point by
           anything (lava, the heat tool, a nearby blaze), or simply by
           touching fire. The contact path is what makes a burn front reliably
           travel through a plank -- pure heat diffusion through one random
           neighbour per frame is too marginal to sustain it, and contact also
           lights wood that fire is merely sitting on before the flame rises
           away. */
        bool ignite = (t >= (int)m.igniteTemp);
        if (!ignite) {
            for (int k = 0; k < 4; ++k) {
                const u8 nm = cells[(y + NB_DY[k]) * SIM_W + (x + NB_DX[k])].mat;
                /* Lava lights things the same way an open flame does. Its heat
                   alone would get there anyway, but making it a contact rule
                   means wood catches the instant lava touches it rather than
                   after a conduction ramp, which is what you expect to see.
                   Any lava qualifies: it freezes back to stone below 100, well
                   above wood's ignition point. */
                if ((nm == MAT_FIRE || nm == MAT_LAVA || nm == MAT_PLASMA)
                    && lchance(L, FIRE_SPREAD)) { ignite = true; break; }
            }
        }
        if (ignite) {
            /* Combustion, unlike boiling, RELEASES heat: light the flame at
               least as hot as fresh fire so it keeps the front going. */
            const u8 prod = m.burnsTo;
            convert(L, x, y, prod);
            temp[i] = (u8)imax(t, (int)MATS[prod].spawnTemp);
            return;
        }
    }
    if (m.coolTemp && t < (int)m.coolTemp
        && (g_matCondenseChance[c.mat] >= 65536u
            || (lrand(L) & 0xFFFFu) < g_matCondenseChance[c.mat])) {
        /* Expansion-only gas volumes carry pressure but no condensation mass
           token. They collapse to empty; exactly one owner parcel from the
           original liquid returns to liquid, so 1 Water -> 3 Steam -> 1 Water
           rather than multiplying matter on the round trip. */
        if (m.kind == KIND_GAS && MATS[m.coolsTo].kind == KIND_LIQUID &&
            (c.moisture & GAS_VOLUME_ONLY))
            convert(L, x, y, MAT_EMPTY);
        else
            phaseChange(L, x, y, m.coolsTo); /* fire dies, steam condenses, lava sets */
        return;
    }
    /* Expiry on a timer rather than by temperature. Only cold fire uses this,
       because it is the only material whose gradient to open air is too small
       to conduct at all -- (58 * 6) >> 9 truncates to zero -- so it has no way
       to cool itself to death the way fire does. See g_matDecay in materials.h.
       The check is a single load against a MAT_COUNT-byte table that is 0 for
       everything else, so it costs nothing for materials that do not opt in. */
    if (g_matDecay[c.mat] && lchance(L, g_matDecay[c.mat])) {
        convert(L, x, y, MAT_EMPTY);
        return;
    }
    /* --- a leaf that has been condemned --------------------------------
       Only the countdown lives here; the DECISION lives in treeAudit, because
       "is this still joined to a tree" is a question about a whole canopy and
       this function only ever sees one cell. See LEAF_FALL_MAX.

       BELOW the phase rules on purpose. A dying canopy is exactly the sort of
       thing a player sets fire to, and a leaf that had stopped being flammable
       the moment it was condemned would refuse to burn for the second and a
       half it takes to fall. Leaves have nothing below this to do anyway --
       they are static, so the movement rules never applied to them. */
    if (g_matIsLeaf[c.mat] && c.moisture) { updateLeafFall(L, x, y); return; }

    if (m.quenchedBy) {
        for (int k = 0; k < 4; ++k) {
            const int nx = x + NB_DX[k], ny = y + NB_DY[k];
            const int j = ny * SIM_W + nx;
            if (cells[j].mat != m.quenchedBy) continue;
            /* Dump the heat into whatever put it out rather than losing it --
               that is what lets a fire dropped in water raise steam. */
            const int give = imin(80, t - AMBIENT_TEMP);
            if (give > 0) temp[j] = (u8)imin(255, (int)temp[j] + give);
            convert(L, x, y, MAT_EMPTY);
            dirtyPoint(L, nx, ny);
            return;
        }
    }

    /* --- acid ---------------------------------------------------------
       Checked from the ACID side (c.mat == MAT_ACID, looking outward at a
       neighbour), not from the victim's -- a cell touching a specific
       neighbour is destroyed, GATED by ACID_DISSOLVE_CHANCE rather than
       unconditional the way quenchedBy is, because corrosion eating an
       entire wall in the one frame it first touched a pool would read as a
       bug, not a hazard.

       That side-swap is the second correction this rule needed, and it is
       the more important one. The first version checked from the VICTIM's
       side (stone asking "am I touching acid"), and it was wrong for a
       reason that has nothing to do with the reaction's direction: a
       settled stone cell with nothing left to dissolve has no other reason
       to be examined at all, and this engine's entire performance model is
       that a chunk nothing is happening in goes to sleep and costs zero.
       Stone next to acid dissolved for a frame or two after being placed
       and then simply STOPPED, because the stone's own chunk went quiet
       and updateCell() was never called on it again to re-roll the chance
       -- measured, stone sitting beside acid for 600 frames lost exactly
       nothing after the first couple.

       Checking from acid's side fixes it because acid can keep ITSELF
       awake: `moreToDo`, exactly the pattern updateGrass uses for the same
       reason ("only stay awake while there is still somewhere to go") --
       while a dissolvable neighbour remains, this cell re-dirties its own
       position every frame, which keeps the whole chunk (acid AND the
       stone touching it) awake for another attempt. Once nothing
       dissolvable is left touching it, it stops dirtying itself and the
       pool settles down and sleeps like any other liquid -- an idle acid
       pool costs the same as an idle pool of anything else. */
    /* --- the spring ------------------------------------------------------
       Makes water into any empty cell beside it, forever.

       Self-limiting by construction, which is the only reason an infinite
       source is safe: it fills EMPTY cells only, so it floods its chamber up
       to its own level, runs out of empty neighbours, and stops. Drain the
       pool and it starts again. There is no budget to keep correct and no
       counter to get wrong.

       The two subtleties here are both lessons the acid rule paid for.

       It scans for an empty neighbour BEFORE rolling the chance, because
       rngChance draws from the shared global stream -- rolling once per
       spring per frame worldwide would shift every other consumer of that
       stream and make unrelated tests non-deterministic.

       And it only re-dirties itself while it still has somewhere to put
       water. A spring that dirtied unconditionally would hold its chunk awake
       forever, which is precisely the "settled chunks cost nothing" property
       the whole engine is built on. A drowned spring sleeps. */
    if (c.mat == MAT_SPRING) {
        bool room = false;
        for (int k = 0; k < 4; ++k) {
            const int nx = x + NB_DX[k], ny = y + NB_DY[k];
            if (cells[ny * SIM_W + nx].mat != MAT_EMPTY) continue;
            room = true;
            if (!lchance(L, SPRING_FLOW_CHANCE)) continue;
            spawnCell(L, nx, ny, MAT_WATER);
            dirtyPoint(L, nx, ny);
            return;
        }
        if (room) dirtyPoint(L, x, y);
    }

    /* Both phases of acid corrode, on identical terms -- see g_matCorrodes. The
       vapour spending itself the same way the liquid does is what stops a cloud
       from being an unbounded eraser: it can only ever eat as many cells as
       there were cells of acid to begin with. */
    if (g_matCorrodes[c.mat]) {
        bool moreToDo = false;
        for (int k = 0; k < 4; ++k) {
            const int nx = x + NB_DX[k], ny = y + NB_DY[k];
            const u8 nm = cells[ny * SIM_W + nx].mat;
            if (g_matDissolvedBy[nm] != MAT_ACID) continue;
            moreToDo = true;
            if (!lchance(L, ACID_DISSOLVE_CHANCE)) continue;
            /* BOTH cells go: the wall is eaten and the acid that ate it is
               SPENT. That second half is what bounds the whole mechanism,
               and it is not optional.

               An earlier version converted the victim into MORE ACID, on the
               reasoning that a reaction should conserve mass and that fresh
               acid in the cavity keeps the front propagating without relying
               on liquid flow. Both of those are true and it is still wrong,
               because acid is generated INSIDE STONE (see
               generateAcidPockets) -- so every pocket starts with its entire
               rim in contact with something dissolvable, and "eat a cell,
               become two cells of acid" is a chain reaction with nothing on
               the other side of it. Measured on a real generated world:
               16,823 acid cells became 551,506 in 3,000 frames, still
               accelerating, with the chunks holding it permanently awake
               because acid re-dirties itself while it has anything left to
               eat. Left alone it converts the map to acid and takes the
               engine's whole sleep-when-settled performance model with it.

               Spending the acid makes a pocket a FINITE resource: N cells of
               acid dissolve at most N cells of wall, which is both the
               honest chemistry (acid is neutralised by what it dissolves)
               and the thing that makes it worth carrying rather than worth
               fleeing. The pair of empty cells left behind lets the acid
               above flow down into the void on the next tick by ordinary
               liquid movement -- no special case needed, because convert()
               dirties both cells and that is what wakes the pool. */
            /* Some reactions give heat back -- water's is the big one. Taken
               from the material being EATEN rather than from the acid, because
               it is a property of the pair and the acid is the same acid
               whatever it lands in. Read before the cell is destroyed, for the
               obvious reason. */
            const u8 give = g_matDissolveHeat[nm];
            convert(L, nx, ny, MAT_EMPTY);
            convert(L, x, y, MAT_EMPTY);
            if (give) heat(nx, ny, 1, (int)give);
            return;
        }
        if (moreToDo) dirtyPoint(L, x, y);
    }

    /* Machines act on their neighbours and never move; nothing below applies. */
    if (c.mat == MAT_HEATER || c.mat == MAT_COOLER) {
        /* The whole machine, and it deliberately does no work on its
           neighbours: it just restores its own setpoint. updateHeat ran at the
           top of this function and already pushed some of that temperature out
           into the four neighbours (and let a little drift toward ambient);
           putting the setpoint back is what makes the supply inexhaustible.
           Everything that follows -- conduction along an iron bar, boiling a
           pool, melting stone, lighting wood -- is then the ordinary heat model
           doing its ordinary job, with no special cases anywhere for these two.

           Writing the setpoint rather than adding a delta is what bounds it.
           An "add N degrees to my neighbours" version has no fixed point: the
           heat has nowhere to settle, so the region around it climbs until it
           saturates at 255 and every chunk in reach stays awake forever. Pinned
           to a value, the neighbourhood converges on a gradient and quietly
           stops changing.

           This is the one place the "do not dirty unless something really
           happened" rule is deliberately broken, because a permanent source is
           by definition never finished. The cost is bounded and visible: each
           machine holds its own chunk awake and no more. */
        const int set = (c.mat == MAT_HEATER) ? HEATER_TEMP : COOLER_TEMP;
        temp[i] = (u8)set;

        /* Drive the four orthogonal neighbours toward the setpoint, on top of
           whatever ordinary conduction already moved. Orthogonal only, matching
           clone's emission: a machine should not reach through the diagonal gap
           between two blocks.

           Clamping at the setpoint is what keeps this stable. The step can only
           ever close the gap, so a neighbour converges on the setpoint and then
           stops changing; it can never overshoot, and heat cannot accumulate
           anywhere beyond it. That is also why this cannot be written as a
           plain "+= N" -- that has no fixed point, and the region around the
           machine would climb until it saturated and never settle. */
        for (int k = 0; k < 4; ++k) {
            const int nx = x + NB_DX[k], ny = y + NB_DY[k];
            const int j = ny * SIM_W + nx;
            const int tj = temp[j];
            if (tj == set) continue;
            temp[j] = (u8)(tj < set ? imin(set, tj + MACHINE_DRIVE)
                                    : imax(set, tj - MACHINE_DRIVE));
            dirtyPoint(L, nx, ny);
        }
        dirtyPoint(L, x, y);
        return;
    }
    if (c.mat == MAT_CLONE) { updateClone(L, x, y); return; }
    if (c.mat == MAT_VOID)  { updateVoid(L, x, y);  return; }

    /* Burning fuel forces heat into its neighbours, the same way a heater does and
       for the same reason -- see g_matDrive in materials.h. Placed here, after the
       phase changes above, so a cell that has just burnt out does not get one last
       free push on its way to being nothing. */
    if (g_matDrive[c.mat]) {
        const int drive = g_matDrive[c.mat];
        const int set = temp[i];
        int spent = 0;
        for (int k = 0; k < 4; ++k) {
            const int nx = x + NB_DX[k], ny = y + NB_DY[k];
            const int j = ny * SIM_W + nx;
            const int tj = temp[j];
            /* Only ever pushes UP toward the fire's own temperature. A flame
               should not actively chill something hotter than itself, which an
               unsigned "drive toward" would do to molten metal sitting on it. */
            if (tj >= set) continue;
            const int give = imin(drive, set - tj);
            temp[j] = (u8)(tj + give);
            spent += give;
            dirtyPoint(L, nx, ny);
        }
        /* And it COSTS the fire what it gave, shifted by its own thermal mass --
           the same accounting conduction uses. Without this the drive creates heat
           from nothing, and the consequence is not a slow drift, it is a fire that
           can never go out: a burning cell held its neighbours at its own
           temperature, which drove the conduction gradient to zero so ordinary
           conduction could not carry heat out of it either, and two adjacent
           burning cells propped each other up for ever. Measured, a firebox of
           either fuel was still 403 cells alight at exactly its spawn temperature
           after 40000 frames, where ordinary fire goes out in 576.

           A packed firebox therefore burns from the OUTSIDE IN, which is both
           correct and the reason a big one lasts: an interior cell whose
           neighbours are all equally hot gives nothing away and spends nothing. */
        if (spent) {
            /* The fractional part is paid as a PROBABILITY, not truncated away.
               That is not a nicety -- it was the whole reason the first fix did
               nothing. The loss is `spent` shifted down by the fuel's thermal
               mass, and at shift 5 anything under 32 shifts to zero; in steady
               state the gaps a fire is closing are only a degree or two, so the
               integer loss was always exactly 0 and the fire still never went out.
               Sub-unit rates are handled the same way everywhere else here -- see
               AIR_COOL and the slide chance -- for the same reason. */
            const int shift = MATS[c.mat].heatMassShift;
            int loss = spent >> shift;
            const int rem = spent - (loss << shift);
            if (rem && lchance(L, (u32)((rem << 8) >> shift))) ++loss;
            if (loss) {
                const int now = temp[i];
                temp[i] = (u8)(now > loss ? now - loss : 0);
            }
        }
    }

    if (m.capacity) updateMoisture(L, x, y);   /* before moving, so the moisture
                                               travels with the cell */
    if (m.kind == KIND_LIQUID && m.boilsTo) {
        const u8 was = c.mat;
        updateEvaporation(L, x, y);
        if (c.mat != was) return;           /* it turned to vapour; `m` is now
                                               the wrong material to act on */
    }

    /* Grass first, and it may turn this cell into dirt -- after which the
       powder rule below still runs on it, which is right: a buried clod of turf
       should keep falling in the same frame it stops being turf. */
    if (c.mat == MAT_GRASS) updateGrass(L, x, y);

    if      (m.kind == KIND_POWDER) updatePowder(L, x, y);
    else if (m.kind == KIND_LIQUID) updateLiquid(L, x, y);
    else if (m.kind == KIND_GAS)    updateGas(L, x, y);
}


/* Everything a stripe needs to know about the frame it is in, and nothing
   that changes while it runs. Written by step() before any lane starts and
   read-only for the rest of the frame. */
struct StripeFrame {
    int  coreCX0, coreCX1, coreCY0, coreCY1;
    int  liveCX0, liveCX1, liveCY0, liveCY1;
    bool leftFirst;
    u32  seedBase;
};
static StripeFrame g_sf;

/* ======================================================================
   The lane pool
   ----------------------------------------------------------------------
   Raw Win32 threads rather than std::thread: this toolchain's C++11
   threading is not available, and everything else in the project already
   talks to Win32 directly.

   Work is STOLEN, not dealt. Stripes are wildly uneven -- a boiling pool
   sits in two of them and the rest of the screen is settled rock costing
   nothing -- so handing each thread a fixed share would leave most of them
   finished and waiting. A shared counter costs one interlocked increment
   per stripe, which against a stripe's worth of work is nothing.

   Stealing makes the ORDER of execution non-deterministic, which is why
   nothing a lane accumulates is merged in completion order. See the lane
   block at the top of this file.
   ====================================================================== */
static const int MAX_WORKERS = MAX_LANE_THREADS - 1;
static HANDLE  g_laneThread[MAX_WORKERS];
static HANDLE  g_laneGo[MAX_WORKERS];
static HANDLE  g_laneDone[MAX_WORKERS];
static int     g_workerCount = 0;      /* threads BESIDES the caller */
static volatile LONG g_laneQuit = 0;

static World*  g_laneWorld = 0;
static int     g_laneJob[STRIPE_COUNT];
static int     g_laneJobs = 0;
static volatile LONG g_laneNext = 0;

/* Stripes are stolen, so a stripe does not know which THREAD will run it --
   but it does have to know which LANE, because that is where its scratch and
   its dirty plane live. Resolved by asking the pool, once, at the top of
   runStripe: the drain loop parks its own id here first. */
static __thread int t_laneId = 0;
static int laneOf(int stripe) { (void)stripe; return t_laneId; }

static void laneDrain(void) {
    for (;;) {
        const LONG i = InterlockedIncrement(&g_laneNext) - 1;
        if (i >= g_laneJobs) return;
        g_laneWorld->runStripe(g_laneJob[i]);
    }
}

static DWORD WINAPI laneMain(LPVOID param) {
    const int id = (int)(size_t)param;
    /* Set once for the life of the thread. Lane 0's plane belongs to whoever
       calls step(), which works as one of the lanes too. */
    t_laneId = id + 1;
    g_lane[id + 1].dirty = &g_dirtyLog[id + 1];
    for (;;) {
        WaitForSingleObject(g_laneGo[id], INFINITE);
        if (g_laneQuit) return 0;
        laneDrain();
        SetEvent(g_laneDone[id]);
    }
}

int simWorkers(void) { return g_workerCount; }

void simSetWorkers(int threads) {
    if (threads < 1) threads = 1;
    if (threads > MAX_WORKERS + 1) threads = MAX_WORKERS + 1;
    const int want = threads - 1;          /* the caller is one of them */
    if (want == g_workerCount) return;

    /* Tear the pool down whichever way the count is moving. Growing it in
       place would mean two shapes of this function to get right instead of
       one, and the call happens at most a handful of times in a session. */
    if (g_workerCount) {
        InterlockedExchange(&g_laneQuit, 1);
        for (int i = 0; i < g_workerCount; ++i) SetEvent(g_laneGo[i]);
        for (int i = 0; i < g_workerCount; ++i) {
            WaitForSingleObject(g_laneThread[i], INFINITE);
            CloseHandle(g_laneThread[i]);
            CloseHandle(g_laneGo[i]);
            CloseHandle(g_laneDone[i]);
        }
        InterlockedExchange(&g_laneQuit, 0);
        g_workerCount = 0;
    }
    for (int i = 0; i < want; ++i) {
        g_laneGo[i]   = CreateEvent(0, FALSE, FALSE, 0);
        g_laneDone[i] = CreateEvent(0, FALSE, FALSE, 0);
        g_laneThread[i] = CreateThread(0, 0, laneMain, (LPVOID)(size_t)i, 0, 0);
        if (!g_laneThread[i] || !g_laneGo[i] || !g_laneDone[i]) {
            /* Out of threads or handles: run with what was made rather than
               refusing to simulate. */
            if (g_laneThread[i]) CloseHandle(g_laneThread[i]);
            if (g_laneGo[i])     CloseHandle(g_laneGo[i]);
            if (g_laneDone[i])   CloseHandle(g_laneDone[i]);
            break;
        }
        ++g_workerCount;
    }
}

/* Run one phase's stripes across the pool, and return only once every one of
   them has finished. The caller works too -- an idle main thread while eleven
   others simulate would be one twelfth of the machine thrown away. */
static void laneRunPhase(World* w, const int* stripes, int count) {
    if (count <= 0) return;
    g_laneWorld = w;
    g_laneJobs = count;
    for (int i = 0; i < count; ++i) g_laneJob[i] = stripes[i];
    InterlockedExchange(&g_laneNext, 0);

    for (int i = 0; i < g_workerCount; ++i) SetEvent(g_laneGo[i]);
    t_laneId = 0;
    g_lane[0].dirty = &g_dirtyLog[0];
    laneDrain();
    g_lane[0].dirty = 0;
    g_lane[0].rng = &g_rng;
    for (int i = 0; i < g_workerCount; ++i)
        WaitForSingleObject(g_laneDone[i], INFINITE);
}

void World::step() {
    dirtyLogInit();
    /* Pocket sharing is the only pressure operation that searches farther than
       its immediate material path. Bound it across the whole world, not per
       chunk, so a pathological mass-boil drains over several frames instead of
       turning one frame into an unbounded collection of 2,048-node walks. */
    /* Last frame's accumulated rects become this frame's work list. */
    memcpy(cur, next, sizeof(cur));
    clearDirty(next);

    /* Bottom-to-top so a falling cell lands in a row already dealt with and
       cannot fall twice in one frame. Left/right alternates each frame,
       otherwise piles visibly lean the way the scan runs. */
    const bool leftFirst = (frame & 1) != 0;
    activeChunks = 0;

    /* The live window in chunk coordinates, rounded outward so a partly
       visible chunk is fully simulated -- material must not behave differently
       depending on which half of it you can see. */
    const int coreCX0 = imax(0, liveX0 >> CHUNK_SHIFT);
    const int coreCY0 = imax(0, liveY0 >> CHUNK_SHIFT);
    const int coreCX1 = imin(CHUNKS_X - 1, liveX1 >> CHUNK_SHIFT);
    const int coreCY1 = imin(CHUNKS_Y - 1, liveY1 >> CHUNK_SHIFT);
    const int coreW = coreCX1 - coreCX0 + 1, coreH = coreCY1 - coreCY0 + 1;
    const int reserve = coreW * coreH;   /* exactly one extra core of budget */
    const int oldTopRow = imax(0, coreCY0 - fingerTop);
    const int oldBottomRow = imin(CHUNKS_Y - 1, coreCY1 + fingerBottom);
    bool needsTop = false, needsBottom = false;
    for (int cx = coreCX0; cx <= coreCX1; ++cx) {
        if (cur[oldTopRow * CHUNKS_X + cx].minX <= cur[oldTopRow * CHUNKS_X + cx].maxX) needsTop = true;
        if (cur[oldBottomRow * CHUNKS_X + cx].minX <= cur[oldBottomRow * CHUNKS_X + cx].maxX) needsBottom = true;
    }
    const int oldLeftCol = imax(0, coreCX0 - fingerLeft);
    const int oldRightCol = imin(CHUNKS_X - 1, coreCX1 + fingerRight);
    bool needsLeft = false, needsRight = false;
    for (int cy = coreCY0; cy <= coreCY1; ++cy) {
        if (cur[cy * CHUNKS_X + oldLeftCol].minX <= cur[cy * CHUNKS_X + oldLeftCol].maxX) needsLeft = true;
        if (cur[cy * CHUNKS_X + oldRightCol].minX <= cur[cy * CHUNKS_X + oldRightCol].maxX) needsRight = true;
    }
    /* The four fingers share one core-sized reserve.  Grow the shortest edge
       requesting room; at capacity take rows/columns from a longer finger. */
    i32* fingers[4] = { &fingerTop, &fingerBottom, &fingerLeft, &fingerRight };
    const bool need[4] = { needsTop, needsBottom, needsLeft, needsRight };
    const int cost[4] = { coreW, coreW, coreH, coreH };
    int want = -1;
    for (int d = 0; d < 4; ++d) if (need[d] && (want < 0 || *fingers[d] < *fingers[want])) want = d;
    int used = coreW * (fingerTop + fingerBottom) + coreH * (fingerLeft + fingerRight);
    if (want >= 0) {
        bool edge = (want == 0) ? coreCY0 - fingerTop > 0 : (want == 1) ? coreCY1 + fingerBottom < CHUNKS_Y - 1 : (want == 2) ? coreCX0 - fingerLeft > 0 : coreCX1 + fingerRight < CHUNKS_X - 1;
        while (edge && used + cost[want] > reserve) {
            int donor = -1;
            for (int d = 0; d < 4; ++d)
                if (d != want && *fingers[d] > *fingers[want] + 1 && (donor < 0 || *fingers[d] > *fingers[donor])) donor = d;
            if (donor < 0) break;
            --*fingers[donor]; used -= cost[donor];
        }
        if (edge && used + cost[want] <= reserve) ++*fingers[want];
    }
    const int liveCX0 = imax(0, coreCX0 - fingerLeft);
    const int liveCX1 = imin(CHUNKS_X - 1, coreCX1 + fingerRight);
    const int liveCY0 = imax(0, coreCY0 - fingerTop);
    const int liveCY1 = imin(CHUNKS_Y - 1, coreCY1 + fingerBottom);

    /* --- the parallel scan ---------------------------------------------
       Two phases: even stripes, then odd. Adjacent stripes never run at the
       same time, and two stripes that DO run together have a whole stripe
       between them -- which is the separation RULE_WRITE_REACH_X says is
       enough. Everything else about the scan is as it was: bottom to top so
       a falling cell lands in a row already dealt with, and the left/right
       alternation per frame so piles do not lean.

       The frame constants the stripes share are handed over in g_sf rather
       than captured, because a Win32 thread entry point takes one pointer
       and this is one struct instead of a closure. */
    g_sf.leftFirst = leftFirst;
    g_sf.coreCX0 = coreCX0; g_sf.coreCX1 = coreCX1;
    g_sf.coreCY0 = coreCY0; g_sf.coreCY1 = coreCY1;
    g_sf.liveCX0 = liveCX0; g_sf.liveCX1 = liveCX1;
    g_sf.liveCY0 = liveCY0; g_sf.liveCY1 = liveCY1;
    /* One draw off the master stream per frame, and every stripe's stream
       hangs off it. The master keeps advancing whether or not the sim did
       anything, so a save still carries a stream that has moved on. */
    g_sf.seedBase = rngNext();

    memset(g_stripeSproutN, 0, sizeof(g_stripeSproutN));
    memset(g_stripeFelledN, 0, sizeof(g_stripeFelledN));
    memset(g_stripeActive,  0, sizeof(g_stripeActive));

    for (int phase = 0; phase < STRIPE_COLOURS; ++phase) {
        int list[STRIPE_COUNT], n = 0;
        for (int st = phase; st < STRIPE_COUNT; st += STRIPE_COLOURS) list[n++] = st;
        laneRunPhase(this, list, n);
    }

    /* Union every lane's dirty plane into next[]. Only the chunks a lane
       actually touched are visited, so this costs what happened rather than
       what the world is -- the whole-grid pass world.h forbids would undo
       the point of the chunk system entirely. */
    for (int lane = 0; lane < MAX_LANE_THREADS; ++lane) {
        DirtyLog& d = g_dirtyLog[lane];
        for (int i = 0; i < d.n; ++i) {
            const int idx = d.touched[i];
            const Chunk& c = d.rect[idx];
            Chunk& n = next[idx];
            if (c.minX < n.minX) n.minX = c.minX;
            if (c.minY < n.minY) n.minY = c.minY;
            if (c.maxX > n.maxX) n.maxX = c.maxX;
            if (c.maxY > n.maxY) n.maxY = c.maxY;
        }
        dirtyLogClear(d);
    }

    /* Merge, in stripe order, so the world is the same whichever thread got
       to which stripe first. */
    for (int st = 0; st < STRIPE_COUNT; ++st) {
        activeChunks += g_stripeActive[st];
        for (int i = 0; i < g_stripeSproutN[st] && sproutCount < MAX_SPROUTS; ++i)
            sprout[sproutCount++] = g_stripeSprout[st][i];
        for (int i = 0; i < g_stripeFelledN[st]; ++i) {
            const int ch = g_stripeFelled[st][i];
            if (felledMark[ch] || felledCount >= MAX_FELLED) continue;
            felledMark[ch] = 1;
            felled[felledCount++] = ch;
        }
    }
    ++frame;
}

/* One stripe: every chunk in a band STRIPE_CHUNKS wide, whole world tall.

   This is verbatim the loop step() used to run over the entire chunk grid,
   with the x range narrowed and the things that used to be world-wide
   counters made lane-local. */
void World::runStripe(int stripe) {
    const bool leftFirst = g_sf.leftFirst;
    const int coreCX0 = g_sf.coreCX0, coreCX1 = g_sf.coreCX1;
    const int coreCY0 = g_sf.coreCY0, coreCY1 = g_sf.coreCY1;
    const int liveCX0 = g_sf.liveCX0, liveCX1 = g_sf.liveCX1;
    const int liveCY0 = g_sf.liveCY0, liveCY1 = g_sf.liveCY1;
    const int cxLo = stripe * STRIPE_CHUNKS;
    const int cxHi = imin(CHUNKS_X - 1, cxLo + STRIPE_CHUNKS - 1);
    if (cxLo > cxHi) return;

    Lane& L = g_lane[laneOf(stripe)];
    L.stripe = stripe;
    /* Stream per stripe, not per thread. Mixed rather than added so that
       neighbouring stripes on the same frame are not neighbouring streams --
       an xorshift seeded with n and n+1 correlates visibly for a few draws,
       and a few draws is exactly how many a single cell makes. */
    u32 seed = g_sf.seedBase ^ (0x9E3779B9u * (u32)(stripe + 1));
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    L.rngOwn = seed ? seed : 0x9E3779B9u;
    L.rng = &L.rngOwn;
    /* Per stripe rather than per world, and at the FULL budget rather than a
       share of it. Sharing it out was tried and it is a trap: the budget is
       not a cost cap that happens to work, it is what lets trapped pressure
       find its outlet, and pressure that does not find an outlet searches
       again next frame and every frame after. Measured, quartering it to
       keep the world-wide total near 128 left five hundred units of hidden
       volume stuck in the basin against the usual fifty, and cost 8 ms a
       frame -- the exact failure mode a short pocket ray produces.

       The honest reading is that the old 128 was already generous enough
       that a busy region never hit it, so making it per stripe changes the
       worst case on paper and nothing at all in practice. */
    L.scratch.pressureRoutes = GAS_PRESSURE_POCKET_BUDGET;
    int active = 0;

    for (int cy = CHUNKS_Y - 1; cy >= 0; --cy) {
        for (int ci = cxLo; ci <= cxHi; ++ci) {
            int cx = leftFirst ? ci : (cxHi - (ci - cxLo));
            const int idx = cy * CHUNKS_X + cx;
            const Chunk& ch = cur[idx];
            if (ch.minX > ch.maxX) continue;   /* settled: skipped entirely */

            /* A plus-shaped live set: the core, plus four independent fingers.
               Corners are intentionally not paid for, preserving the 2x cap. */
            const bool inLiveWindow = liveCoreMask[idx]
                                   || (cx >= coreCX0 && cx <= coreCX1 && cy >= liveCY0 && cy <= liveCY1)
                                   || (cy >= coreCY0 && cy <= coreCY1 && cx >= liveCX0 && cx <= liveCX1);
            if (inLiveWindow) liveGrace[idx] = LIVE_GRACE_STEPS;
            const bool lingering = !inLiveWindow && liveGrace[idx] > 0;
            if (lingering) --liveGrace[idx];

            if (!inLiveWindow && !lingering && !keepAlive[idx]) {
                /* Outside the window: FROZEN, not forgotten.

                   The pending work has to be carried into next[], because cur
                   is overwritten by next at the top of every step. Simply
                   skipping would silently drop the rect, and the chunk would
                   come back settled -- sand caught mid-fall would hang in the
                   air forever, and the only clue would be that it happened to
                   be off-screen at the time. Merged rather than assigned,
                   since a simulated neighbour may already have dirtied across
                   the boundary this frame. */
                Chunk& n = next[idx];
                if (n.minX > n.maxX) { n = ch; }
                else {
                    if (ch.minX < n.minX) n.minX = ch.minX;
                    if (ch.minY < n.minY) n.minY = ch.minY;
                    if (ch.maxX > n.maxX) n.maxX = ch.maxX;
                    if (ch.maxY > n.maxY) n.maxY = ch.maxY;
                }
                continue;
            }
            ++active;

            for (int y = ch.maxY; y >= ch.minY; --y) {
                if (leftFirst) {
                    for (int x = ch.minX; x <= ch.maxX; ++x) updateCell(L, x, y);
                } else {
                    for (int x = ch.maxX; x >= ch.minX; --x) updateCell(L, x, y);
                }
            }
        }
    }
    g_stripeActive[stripe] = active;
    L.stripe = -1;
}
