#include "world.h"
#include <string.h>

World g_world;

/* ======================================================================
   Dirty rectangle bookkeeping
   ====================================================================== */

static void clearDirty(Chunk* c) {
    for (int i = 0; i < CHUNK_COUNT; ++i) {
        c[i].minX = SIM_W; c[i].minY = SIM_H;
        c[i].maxX = -1;    c[i].maxY = -1;
    }
}

/* Mark a span and the ring of cells around it as needing simulation next
   frame. The margin matters: when a cell moves away, whatever was resting on
   it has to get another look. The box can straddle several chunks, so each
   overlapped chunk absorbs only its own clipped slice of it. */
void World::dirtyArea(int x0, int y0, int x1, int y1) {
    x0 -= 1; y0 -= 1; x1 += 1; y1 += 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SIM_W - 1) x1 = SIM_W - 1;
    if (y1 > SIM_H - 1) y1 = SIM_H - 1;

    int cx0 = x0 >> CHUNK_SHIFT, cx1 = x1 >> CHUNK_SHIFT;
    int cy0 = y0 >> CHUNK_SHIFT, cy1 = y1 >> CHUNK_SHIFT;

    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            Chunk& c = next[cy * CHUNKS_X + cx];
            int bx0 = cx << CHUNK_SHIFT, by0 = cy << CHUNK_SHIFT;
            int ax0 = imax(x0, bx0),               ay0 = imax(y0, by0);
            int ax1 = imin(x1, bx0 + CHUNK - 1),   ay1 = imin(y1, by0 + CHUNK - 1);
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

/* Pressure can lift a reasonably deep column cheaply, while the more
   expensive sideways search stays tightly local. These are deliberately two
   limits: the common case is a straight column of water above a steam pocket,
   and making that pay for a square flood fill would punish boilers for no
   visual benefit. */
static const int GAS_PRESSURE_VERTICAL_REACH = 512;
static const int GAS_PRESSURE_LIQUID_RADIUS  = 16;
static const int GAS_PRESSURE_POWDER_REACH   = 8;
static const int GAS_PRESSURE_POCKET_RAY     = 512;
static const int GAS_PRESSURE_POCKET_RADIUS  = 32;
static const int GAS_PRESSURE_POCKET_NODES   = 2048;
static const int GAS_PRESSURE_POCKET_BUDGET  = 128;
static const int GAS_PRESSURE_EXPANSION_BURST = 5;

/* Divide a heat transfer by 2^shift to get the temperature change a material of
   that thermal mass actually feels, carrying the remainder stochastically so
   small transfers average out correctly instead of truncating to nothing.
   `move` is always non-negative here; the caller applies the sign. */
static inline int scaleByMass(int move, int shift) {
    if (!shift) return move;
    const int q = move >> shift;
    const int r = move & ((1 << shift) - 1);
    return q + ((r && (int)rngBits((u32)shift) < r) ? 1 : 0);
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

void World::reset() {
    clearBlockBox();
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
    pressureRoutesRemaining = 0;
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
    for (int i = 0; i < SIM_W * SIM_H; ++i) cells[i].tint = (u8)rngBits(8);
}

/* A wood cell is about to become something else. See World::felled -- this is
   the only moment a canopy can lose its support, so it is the only moment
   anything needs to look. Cheap by construction: two table lookups on a write
   that already happens, and a push on the rare one that matters. */
void World::reportFelled(int x, int y, u8 was, u8 now) {
    if (!g_matIsWood[was] || g_matIsWood[now]) return;
    const int ch = (y >> CHUNK_SHIFT) * CHUNKS_X + (x >> CHUNK_SHIFT);
    if (felledMark[ch]) return;
    if (felledCount >= MAX_FELLED) return;
    felledMark[ch] = 1;
    felled[felledCount++] = ch;
}

/* Warm a disc toward IGNITE_MAX, never past it. Cells already hotter are left
   alone rather than dragged down -- a striker cannot cool anything, and running
   one over an ember should not put it out. */
void World::ignite(int cx, int cy, int r) {
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
            dirtyPoint(x, y);
        }
}

void World::setCell(int x, int y, u8 mat) {
    const int i = y * SIM_W + x;
    Cell& c = cells[i];
    reportFelled(x, y, c.mat, mat);
    c.mat      = mat;
    c.moisture = 0;
    c.tint     = (u8)rngBits(8);
    /* Stamped with the previous frame so a freshly painted cell is eligible on
       the very next step rather than sitting still for one, which would make
       the brush feel laggy. */
    c.flags    = (u8)(((frame - 1) & STAMP_MASK) << STAMP_SHIFT);
    const MatInfo& m = MATS[mat];
    temp[i] = m.spawnTemp ? m.spawnTemp : (u8)AMBIENT_TEMP;
    dirtyPoint(x, y);
}

void World::breakCell(int x, int y) {
    const u8 m    = cells[y * SIM_W + x].mat;
    const u8 drop = g_matDropsAs[m];
    setCell(x, y, (drop != m && g_matIsSeed[drop]) ? drop : (u8)MAT_EMPTY);
}

void World::swapMat(int x, int y, u8 mat) {
    cells[y * SIM_W + x].mat = mat;
    dirtyPoint(x, y);
}

/* Swap material without disturbing temperature: a phase change carries its
   heat across, and the stamp is left alone so the new material waits until
   next frame to move. */
void World::convert(int x, int y, u8 mat) {
    Cell& c = cells[y * SIM_W + x];
    reportFelled(x, y, c.mat, mat);
    c.mat      = mat;
    c.moisture = 0;
    c.tint     = (u8)rngBits(8);
    dirtyPoint(x, y);
}

/* Clamped to the play area, so the border box can never be painted over. */
void World::paint(int cx, int cy, int r, u8 mat, bool replace) {
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
            setCell(x, y, mat);
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
            dirtyPoint(x, y);
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

bool World::tryMove(int sx, int sy, int tx, int ty) {
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
        if (t.moisture) return false;   /* one occupant parcel per host cell */
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
        dirtyPoint(sx, sy); dirtyPoint(tx, ty);
        return true;
    }

    if (blocksCell(tx, ty) && !cellFalling(s)) return false;

    if (t.mat != MAT_EMPTY) {
        const MatInfo& tm = MATS[t.mat];
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
        if (tm.kind != KIND_LIQUID && tm.kind != KIND_GAS) return false;
        const MatInfo& sm = MATS[s.mat];
        const int sourceDensity = materialDensityQ8(s.mat, temp[si]);
        const int targetDensity = materialDensityQ8(t.mat, temp[ti]);
        /* Gases invert the density test: they displace anything *heavier*,
           which is how steam bubbles up through water. */
        if (sm.kind == KIND_GAS) {
            if (targetDensity <= sourceDensity + DENSITY_SWAP_EPS_Q8) return false;
        }
        else {
            if (sourceDensity <= targetDensity + DENSITY_SWAP_EPS_Q8) return false;
            /* Gas owns the swap with liquid, on the gas cell's turn. Letting
               liquid push a gas target sounds symmetric but is not under an
               in-place scan: neighbouring water cells can relay one stamped
               steam cell sideways/upward many times before it gets a turn.
               Waiting costs at most one frame; then the bubble rises itself. */
            if (tm.kind == KIND_GAS) {
                return false;
            }
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
    dirtyPoint(sx, sy);
    dirtyPoint(tx, ty);
    return true;
}

/* Move a fluid parcel already inside a sieve either into the next compatible
   mesh cell or back out into empty world space. The sieve material itself never
   moves. Temperature follows the parcel through the mesh; while occupied, the
   sieve and fluid share that one temperature, which also lets ordinary heat
   conduction act on the contents without another world-sized array. */
bool World::moveFilterFluid(int sx, int sy, int tx, int ty) {
    if (tx < PLAY_X0 || tx > PLAY_X1 || ty < PLAY_Y0 || ty > PLAY_Y1) return false;
    const int si = sy * SIM_W + sx, ti = ty * SIM_W + tx;
    Cell& s = cells[si];
    Cell& t = cells[ti];
    const u8 packed = s.moisture;
    const u8 moving = occupantMat(packed);
    if (!moving) return false;

    if (filterAllows(t.mat, moving)) {
        if (t.moisture) return false;
        t.moisture = packed;
    } else {
        if (t.mat != MAT_EMPTY || blocksCell(tx, ty)) return false;
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
    dirtyPoint(sx, sy);
    dirtyPoint(tx, ty);
    return true;
}

void World::updateFilterFluid(int x, int y) {
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
        convert(nx, ny, g_matWetInto[neighbour]);
        c.moisture = 0;
        temp[i] = AMBIENT_TEMP;
        dirtyPoint(x, y);
        return;
    }

    const MatInfo& m = MATS[moving];
    int dx = (c.flags & F_DIR) ? 1 : -1;
    if (m.kind == KIND_GAS) {
        if (moveFilterFluid(x, y, x, y - 1)) return;
        if (moveFilterFluid(x, y, x + dx, y - 1)) return;
        if (moveFilterFluid(x, y, x - dx, y - 1)) return;
        if (moveFilterFluid(x, y, x + dx, y)) return;
        if (moveFilterFluid(x, y, x - dx, y)) return;
    } else { /* only liquids can enter the other permitted branch */
        if (moveFilterFluid(x, y, x, y + 1)) return;
        if (moveFilterFluid(x, y, x + dx, y + 1)) return;
        if (moveFilterFluid(x, y, x - dx, y + 1)) return;
        if (moveFilterFluid(x, y, x + dx, y)) return;
        if (moveFilterFluid(x, y, x - dx, y)) return;
    }

    c.flags ^= F_DIR;
    dirtyPoint(x, y);   /* occupied mesh retries until it can leave or react */
}

void World::updatePowder(int x, int y) {
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
        if (rngChance((u32)drift * scale / 8) && tryMove(x, y, x + dir, y + 1)) {
            cells[(y + 1) * SIM_W + x + dir].flags |= F_FALL;
            return;
        }
    }

    if (tryMove(x, y, x, y + 1)) {
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
    if (blocksCell(x, y)) {
        const int out = (x * 2 < blockX0 + blockX1) ? -1 : 1;
        static const int TRY[4][2] = { {1,1}, {-1,1}, {1,0}, {-1,0} };
        for (int t = 0; t < 4; ++t) {
            const int tx = x + TRY[t][0] * out, ty = y + TRY[t][1];
            if (tryMove(x, y, tx, ty)) {
                cells[ty * SIM_W + tx].flags |= F_FALL;
                return;
            }
        }
        dirtyPoint(x, y);
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
    if (!rngChance((u32)slide)) return;

    int dx = (rngNext() & 1) ? 1 : -1;
    if (tryMove(x, y, x + dx, y + 1)) return;
    tryMove(x, y, x - dx, y + 1);
}

/* Buoyant parcel convection. Same-material pairs retain the old four-degree
   threshold, but now exchange the whole parcel (cell identity plus heat) rather
   than teleporting temperature. Different liquids/gases exchange when the
   lower parcel's effective thermal density is meaningfully lighter.

   Alternating non-overlapping row pairs prevents one hot parcel from racing up
   several cells during a bottom-to-top scan. No RNG is consumed, preserving
   the deterministic movement silhouette. */
bool World::updateConvection(int x, int y) {
    const int i = y * SIM_W + x;
    const u8 mat = cells[i].mat;
    const u8 kind = MATS[mat].kind;
    if ((kind != KIND_LIQUID && kind != KIND_GAS) ||
        (((u32)y ^ frame) & 1u) != 0u) return false;

    const int above = i - SIM_W;
    const u8 aboveMat = cells[above].mat;
    if (MATS[aboveMat].kind != kind) return false;
    if (aboveMat == mat) {
        if ((int)temp[i] <= (int)temp[above] + 4) return false;
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
    dirtyPoint(x, y);
    dirtyPoint(x, y - 1);
    return true;
}

void World::updateLiquid(int x, int y) {
    Cell& c = cells[y * SIM_W + x];
    const MatInfo& m = MATS[c.mat];

    if (tryMove(x, y, x, y + 1)) return;
    if (updateConvection(x, y)) return;

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
    if (m.jitter && rngChance(m.jitter)) {
        if (cells[y * SIM_W + x - 1].mat == MAT_EMPTY ||
            cells[y * SIM_W + x + 1].mat == MAT_EMPTY ||
            cells[(y + 1) * SIM_W + x - 1].mat == MAT_EMPTY ||
            cells[(y + 1) * SIM_W + x + 1].mat == MAT_EMPTY) dirtyPoint(x, y);
        return;
    }

    int dx = (c.flags & F_DIR) ? 1 : -1;
    if (tryMove(x, y, x + dx, y + 1)) return;
    if (tryMove(x, y, x - dx, y + 1)) return;

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
            tryMove(x, y, destX, y);
            /* tryMove only dirties the two endpoints, but this hop can cover
               several cells at once. Anything resting along the swept path
               just lost its support and has to be woken too. */
            dirtyArea(imin(x, destX), y, imax(x, destX), y);
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
bool World::updateGasPressure(int x, int y) {
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
       five-volume Water -> Steam expansion can consume. */
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
                n.tint = (u8)rngBits(8);
                n.flags = (u8)(gasDir | pressureStamp);
                temp[ni] = gasTemp;
                dirtyPoint(nx, ny);
                queue[tail++] = (i16)niLocal;
                ++spawned;
            }
        }
        if (spawned) {
            c.moisture = (u8)((c.moisture & GAS_VOLUME_ONLY) |
                              ((int)excess - spawned));
            c.flags = (u8)(gasDir | pressureStamp);
            dirtyPoint(x, y);
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
        d.tint = (u8)rngBits(8);
        d.flags = (u8)(gasDir | pressureStamp);
        temp[di] = gasTemp;
        c.moisture = (u8)((c.moisture & GAS_VOLUME_ONLY) | (excess - 1));
        c.flags = (u8)(gasDir | pressureStamp);
        dirtyPoint(x, y);
        dirtyPoint(di % SIM_W, di / SIM_W);
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
            g.tint = (u8)rngBits(8);
            g.flags = (u8)(gasDir | pressureStamp);
            temp[gi] = gasTemp;
        }
        if (burst) {
            c.moisture = (u8)((c.moisture & GAS_VOLUME_ONLY) |
                              ((int)excess - burst));
            c.flags = (u8)(gasDir | pressureStamp);
            dirtyArea(x, outletY - burst, x, y);
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
            if (MATS[n.mat].kind != KIND_POWDER ||
                reactivePowderAllowsGas(n.mat, gasMat)) break;
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
            dirtyPoint(tx, ty);
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
        return false;
    };

    if (!hasPressureRelief(x, y) && pressureRoutesRemaining > 0) {
        --pressureRoutesRemaining;
        int receiver = -1, receiverDistance = 1000000, receiverExcess = 256;
        for (int k = 0; k < 4; ++k) {
            for (int d = 1; d <= GAS_PRESSURE_POCKET_RAY; ++d) {
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
            static const int CAP = SIDE * SIDE;
            static u16 seen[CAP] = { 0 };
            static i16 queue[GAS_PRESSURE_POCKET_NODES];
            static u16 epoch = 0;
            if (++epoch == 0) {
                memset(seen, 0, sizeof(seen));
                epoch = 1;
            }

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
            dirtyPoint(x, y);
            dirtyPoint(receiver % SIM_W, receiver / SIM_W);
            return true;
        }
    }

    /* A short bent pipe, cavity, or sloping shoreline needs more than a
       vertical ray. Search only a 33x33 box around the gas. parent[] is both
       the visited set and the path back to the first adjacent liquid, so the
       work and stack storage have hard upper bounds independent of world or
       lake size. */
    static const int R = GAS_PRESSURE_LIQUID_RADIUS;
    static const int SIDE = R * 2 + 1;
    static const int CAP = SIDE * SIDE;
    i16 parent[CAP];
    i16 queue[CAP];
    for (int k = 0; k < CAP; ++k) parent[k] = -2;

    const int orderDx[4] = { 0, dir, -dir, 0 };
    const int orderDy[4] = { -1, 0, 0, 1 };
    int head = 0, tail = 0;
    for (int k = 0; k < 4; ++k) {
        const int nx = x + orderDx[k], ny = y + orderDy[k];
        if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1) continue;
        if (MATS[cells[ny * SIM_W + nx].mat].kind != KIND_LIQUID) continue;
        const int li = (ny - (y - R)) * SIDE + (nx - (x - R));
        if (parent[li] != -2) continue;
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
            if (parent[ni] != -2) continue;
            const int nx = x - R + nlx, ny = y - R + nly;
            if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1) continue;
            if (MATS[cells[ny * SIM_W + nx].mat].kind != KIND_LIQUID) continue;
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
            dirtyPoint(dst % SIM_W, dst / SIM_W);
            dst = src;
            path = parent[path];
        }
        finishExpansion(dst);
        return true;
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
    dirtyPoint(x, y);
    dirtyPoint(best % SIM_W, best / SIM_W);
    return true;
}

void World::updateGas(int x, int y) {
    Cell& c = cells[y * SIM_W + x];
    const MatInfo& m = MATS[c.mat];

    if (updateGasPressure(x, y)) return;

    /* A submerged bubble gets one upward step per turn, but that step may be
       diagonal. This widens a plume naturally without restoring the old bug:
       unrestricted sideways swaps let water relay one steam cell across a pool
       during an in-place scan. Here lateral travel is bounded by vertical
       travel -- after rising N cells a bubble can be at most N cells sideways. */
    const u8 aboveKind = MATS[cells[(y - 1) * SIM_W + x].mat].kind;
    if (aboveKind == KIND_LIQUID) {
        if (m.jitter && rngChance(m.jitter)) {
            const int jd = (rngNext() & 1u) ? 1 : -1;
            if (tryMove(x, y, x + jd, y - 1)) return;
            if (tryMove(x, y, x - jd, y - 1)) return;
        }
        if (tryMove(x, y, x, y - 1)) return;
    }

    /* Flit: before trying to rise, a gas has a per-material chance of just
       wandering sideways. This is what stops smoke and steam from rising as a
       rigid vertical column -- they billow and drift the way a real plume
       does. A pure sideways hop, so it costs nothing but one tryMove. */
    if (m.jitter && rngChance(m.jitter)) {
        int jd = (rngNext() & 1) ? 1 : -1;
        if (tryMove(x, y, x + jd, y)) return;
        if (tryMove(x, y, x - jd, y)) return;
    }

    /* A mirror of updateLiquid with the vertical sense flipped. */
    if (tryMove(x, y, x, y - 1)) return;
    if (updateConvection(x, y)) return;

    int dx = (c.flags & F_DIR) ? 1 : -1;
    if (tryMove(x, y, x + dx, y - 1)) return;
    if (tryMove(x, y, x - dx, y - 1)) return;

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
        if (moveToX != x && tryMove(moveFromX, y, moveToX, y)) {
            dirtyArea(imin(x, moveToX), y, imax(x, moveToX), y);
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
void World::heatPair(int i, int jx, int jy) {
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
        const int deltaI = scaleByMass(move, (int)MATS[cells[i].mat].heatMassShift);
        const int deltaJ = scaleByMass(move, (int)MATS[cells[j].mat].heatMassShift);

        if (diff < 0) { temp[i] = (u8)(ti + deltaI); temp[j] = (u8)(tj - deltaJ); }
        else          { temp[i] = (u8)(ti - deltaI); temp[j] = (u8)(tj + deltaJ); }
        dirtyPoint(jx, jy);
    }
}

void World::updateHeat(int x, int y) {
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
        heatPair(i, nx, ny);
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
                if (spread > 1) heatPair(i, fxFull, fyFull);
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
            if (fx >= 0 && (fx != x + dx || fy != y + dy)) heatPair(i, fx, fy);
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
        if (rngChance(rate)) {
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
            if (!hold || !rngChance(hold))
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
            dirtyPoint(x, y);
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
    dirtyPoint(x, y);
}

/* Table-driven phase conversion plus gas expansion charge. `convert` remains
   the right operation for chemistry and combustion; only a LIQUID becoming a
   GAS owns the volume increase that later becomes pressure. */
void World::phaseChange(int x, int y, u8 mat) {
    Cell& c = cells[y * SIM_W + x];
    const u8 from = c.mat;
    const bool expands = MATS[from].kind == KIND_LIQUID &&
                         MATS[mat].kind == KIND_GAS;
    convert(x, y, mat);
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
void World::updateMoisture(int x, int y) {
    Cell& c = cells[y * SIM_W + x];
    const MatInfo& m = MATS[c.mat];

    /* --- absorb a touching water cell ---------------------------------- */
    if ((int)c.moisture + MOISTURE_UNIT <= (int)m.capacity) {
        static const int OFF[4][2] = { {0,-1}, {-1,0}, {1,0}, {0,1} };
        u32 start = rngBits(2);   /* rotate the scan order, else water always
                                     gets eaten from the same side first */
        for (u32 k = 0; k < 4; ++k) {
            const int* o = OFF[(start + k) & 3];
            int nx = x + o[0], ny = y + o[1];
            Cell& n = cells[ny * SIM_W + nx];
            if (n.mat == MAT_WATER) {
                n.mat      = MAT_EMPTY;
                n.moisture = 0;
                c.moisture = (u8)(c.moisture + MOISTURE_UNIT);
                dirtyPoint(nx, ny);
                dirtyPoint(x, y);
                break;
            }
        }
    }

    if (c.moisture == 0) return;

    /* --- exchange with one neighbour ----------------------------------- */
    u32 r = rngNext() & 7;
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
            n.tint     = (u8)rngBits(8);
            n.flags    = (u8)(stamp() << STAMP_SHIFT);   /* waits a frame */
            c.moisture = (u8)(c.moisture - MOISTURE_UNIT);
            dirtyPoint(x, y);
            dirtyPoint(nx, ny);
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
    dirtyPoint(x, y);
    dirtyPoint(nx, ny);
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
void World::spawnCell(int x, int y, u8 mat) {
    const int i = y * SIM_W + x;
    Cell& c = cells[i];
    c.mat      = mat;
    c.moisture = 0;
    c.tint     = (u8)rngBits(8);
    c.flags    = (u8)(stamp() << STAMP_SHIFT);
    const MatInfo& m = MATS[mat];
    temp[i] = m.spawnTemp ? m.spawnTemp : (u8)AMBIENT_TEMP;
    dirtyPoint(x, y);
}

void World::updateClone(int x, int y) {
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
            dirtyPoint(x, y);
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
        if (cells[ny * SIM_W + nx].mat == MAT_EMPTY) spawnCell(nx, ny, c.moisture);
    }
}

void World::updateVoid(int x, int y) {
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
        dirtyPoint(nx, ny);
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
void World::updateEvaporation(int x, int y) {
    const int i = y * SIM_W + x;
    const MatInfo& m = MATS[cells[i].mat];

    bool open = false;
    for (int k = 0; k < 4; ++k) {
        if (cells[(y + NB_DY[k]) * SIM_W + (x + NB_DX[k])].mat == MAT_EMPTY) { open = true; break; }
    }
    if (!open) return;
    dirtyPoint(x, y);

    int over = (int)temp[i] - AMBIENT_TEMP;
    if (over < 0) over = 0;
    const u32 chance = 2u + (u32)(over * over);
    if ((rngNext() & 0xFFFF) >= chance) return;

    phaseChange(x, y, m.boilsTo);
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
void World::updateLeafFall(int x, int y) {
    Cell& c = cells[y * SIM_W + x];
    if (--c.moisture) { dirtyPoint(x, y); return; }
    breakCell(x, y);
    /* The neighbourhood, not the cell: the light that was blocked by this leaf
       now reaches past it, and the cells under a vanishing canopy have to be
       given a look or the hole stays dark until something else wakes them. */
    dirtyArea(x - 1, y - 1, x + 1, y + 1);
}

void World::updateGrass(int x, int y) {
    /* Buried: nothing living survives out of reach of the air. */
    if (!airWithin(x, y, GRASS_DEPTH)) { convert(x, y, MAT_DIRT); return; }

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
        if (rngChance(GRASS_SPREAD)) convert(nx, ny, MAT_GRASS);
    }
    /* Only stay awake while there is still somewhere to go. */
    if (moreToDo) dirtyPoint(x, y);
}

void World::updateCell(int x, int y) {
    const int i = y * SIM_W + x;

    /* Heat first, and for every cell -- air and walls included -- so warmth
       crosses open space. Cells already at ambient cost a single compare. */
    if (temp[i] != AMBIENT_TEMP) updateHeat(x, y);

    Cell& c = cells[i];
    if (c.mat == MAT_EMPTY || c.mat == MAT_WALL) return;

    const u8 st = stamp();
    if (((c.flags >> STAMP_SHIFT) & STAMP_MASK) == st) {
        /* Already had its turn this frame -- it moved here from somewhere the
           scan had not reached yet. Dirty it so it is guaranteed another look
           next frame: that costs nothing here (whatever moved it already
           dirtied these cells) and it is what makes a stale stamp, on the rare
           frame one aliases, heal itself instead of stranding the cell. */
        dirtyPoint(x, y);
        return;
    }
    c.flags = (u8)((c.flags & F_DIR) | (st << STAMP_SHIFT));

    const MatInfo& m = MATS[c.mat];

    /* Sieve moisture is a coexisting fluid material id, not absorbed water.
       Give that parcel its turn before applying phase/reaction tables to the
       mesh material itself. */
    if ((c.mat == MAT_SIEVE || c.mat == MAT_GAS_SIEVE) && c.moisture) {
        updateFilterFluid(x, y);
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
        convert(x, y, into);          /* also clears the consumed gas occupant */
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
    if (g_matIsSeed[c.mat] && sproutCount < MAX_SPROUTS) {
        const u8 below = (y < PLAY_Y1) ? cells[(y + 1) * SIM_W + x].mat : (u8)MAT_WALL;
        if (below == MAT_DIRT || below == MAT_GRASS) {
            sprout[sproutCount++] = i;
            /* Kept awake until somebody deals with it. Without this a seed that
               lands while the tree table happens to be full settles for ever
               and never gets a second look. */
            dirtyPoint(x, y);
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
        if (g_matSmeltYield[c.mat] && !rngChance(g_matSmeltYield[c.mat]))
            into = MAT_SLAG_MELT;
        phaseChange(x, y, into);
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
            convert(x, y, g_matWetInto[c.mat]);
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
                dirtyPoint(nx, ny);
            } else {
                spawnCell(nx, ny, MAT_EMPTY);
                dirtyPoint(nx, ny);
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
            convert(x, y, g_matAlloysTo[c.mat]);
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
                    && rngChance(FIRE_SPREAD)) { ignite = true; break; }
            }
        }
        if (ignite) {
            /* Combustion, unlike boiling, RELEASES heat: light the flame at
               least as hot as fresh fire so it keeps the front going. */
            const u8 prod = m.burnsTo;
            convert(x, y, prod);
            temp[i] = (u8)imax(t, (int)MATS[prod].spawnTemp);
            return;
        }
    }
    if (m.coolTemp && t < (int)m.coolTemp) {
        /* Expansion-only gas volumes carry pressure but no condensation mass
           token. They collapse to empty; exactly one owner parcel from the
           original liquid returns to liquid, so 1 Water -> 6 Steam -> 1 Water
           rather than multiplying matter on the round trip. */
        if (m.kind == KIND_GAS && MATS[m.coolsTo].kind == KIND_LIQUID &&
            (c.moisture & GAS_VOLUME_ONLY))
            convert(x, y, MAT_EMPTY);
        else
            phaseChange(x, y, m.coolsTo); /* fire dies, steam condenses, lava sets */
        return;
    }
    /* Expiry on a timer rather than by temperature. Only cold fire uses this,
       because it is the only material whose gradient to open air is too small
       to conduct at all -- (58 * 6) >> 9 truncates to zero -- so it has no way
       to cool itself to death the way fire does. See g_matDecay in materials.h.
       The check is a single load against a MAT_COUNT-byte table that is 0 for
       everything else, so it costs nothing for materials that do not opt in. */
    if (g_matDecay[c.mat] && rngChance(g_matDecay[c.mat])) {
        convert(x, y, MAT_EMPTY);
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
    if (g_matIsLeaf[c.mat] && c.moisture) { updateLeafFall(x, y); return; }

    if (m.quenchedBy) {
        for (int k = 0; k < 4; ++k) {
            const int nx = x + NB_DX[k], ny = y + NB_DY[k];
            const int j = ny * SIM_W + nx;
            if (cells[j].mat != m.quenchedBy) continue;
            /* Dump the heat into whatever put it out rather than losing it --
               that is what lets a fire dropped in water raise steam. */
            const int give = imin(80, t - AMBIENT_TEMP);
            if (give > 0) temp[j] = (u8)imin(255, (int)temp[j] + give);
            convert(x, y, MAT_EMPTY);
            dirtyPoint(nx, ny);
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
            if (!rngChance(SPRING_FLOW_CHANCE)) continue;
            spawnCell(nx, ny, MAT_WATER);
            dirtyPoint(nx, ny);
            return;
        }
        if (room) dirtyPoint(x, y);
    }

    if (c.mat == MAT_ACID) {
        bool moreToDo = false;
        for (int k = 0; k < 4; ++k) {
            const int nx = x + NB_DX[k], ny = y + NB_DY[k];
            const u8 nm = cells[ny * SIM_W + nx].mat;
            if (g_matDissolvedBy[nm] != MAT_ACID) continue;
            moreToDo = true;
            if (!rngChance(ACID_DISSOLVE_CHANCE)) continue;
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
            convert(nx, ny, MAT_EMPTY);
            convert(x, y, MAT_EMPTY);
            return;
        }
        if (moreToDo) dirtyPoint(x, y);
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
            dirtyPoint(nx, ny);
        }
        dirtyPoint(x, y);
        return;
    }
    if (c.mat == MAT_CLONE) { updateClone(x, y); return; }
    if (c.mat == MAT_VOID)  { updateVoid(x, y);  return; }

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
            dirtyPoint(nx, ny);
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
            if (rem && rngChance((u32)((rem << 8) >> shift))) ++loss;
            if (loss) {
                const int now = temp[i];
                temp[i] = (u8)(now > loss ? now - loss : 0);
            }
        }
    }

    if (m.capacity) updateMoisture(x, y);   /* before moving, so the moisture
                                               travels with the cell */
    if (m.kind == KIND_LIQUID && m.boilsTo) {
        const u8 was = c.mat;
        updateEvaporation(x, y);
        if (c.mat != was) return;           /* it turned to vapour; `m` is now
                                               the wrong material to act on */
    }

    /* Grass first, and it may turn this cell into dirt -- after which the
       powder rule below still runs on it, which is right: a buried clod of turf
       should keep falling in the same frame it stops being turf. */
    if (c.mat == MAT_GRASS) updateGrass(x, y);

    if      (m.kind == KIND_POWDER) updatePowder(x, y);
    else if (m.kind == KIND_LIQUID) updateLiquid(x, y);
    else if (m.kind == KIND_GAS)    updateGas(x, y);
}

void World::step() {
    /* Pocket sharing is the only pressure operation that searches farther than
       its immediate material path. Bound it across the whole world, not per
       chunk, so a pathological mass-boil drains over several frames instead of
       turning one frame into an unbounded collection of 2,048-node walks. */
    pressureRoutesRemaining = GAS_PRESSURE_POCKET_BUDGET;
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

    for (int cy = CHUNKS_Y - 1; cy >= 0; --cy) {
        for (int ci = 0; ci < CHUNKS_X; ++ci) {
            int cx = leftFirst ? ci : (CHUNKS_X - 1 - ci);
            const int idx = cy * CHUNKS_X + cx;
            const Chunk& ch = cur[idx];
            if (ch.minX > ch.maxX) continue;   /* settled: skipped entirely */

            /* A plus-shaped live set: the core, plus four independent fingers.
               Corners are intentionally not paid for, preserving the 2x cap. */
            const bool inLiveWindow = (cx >= coreCX0 && cx <= coreCX1 && cy >= liveCY0 && cy <= liveCY1)
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
            ++activeChunks;

            for (int y = ch.maxY; y >= ch.minY; --y) {
                if (leftFirst) {
                    for (int x = ch.minX; x <= ch.maxX; ++x) updateCell(x, y);
                } else {
                    for (int x = ch.maxX; x >= ch.minX; --x) updateCell(x, y);
                }
            }
        }
    }
    ++frame;
}
