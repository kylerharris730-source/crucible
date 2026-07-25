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
    memset(cells, 0, sizeof(cells));
    memset(temp, AMBIENT_TEMP, sizeof(temp));
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
    for (int i = 0; i < SIM_W * SIM_H; ++i) cells[i].tint = (u8)rngBits(8);
}

void World::setCell(int x, int y, u8 mat) {
    const int i = y * SIM_W + x;
    Cell& c = cells[i];
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

/* Swap material without disturbing temperature: a phase change carries its
   heat across, and the stamp is left alone so the new material waits until
   next frame to move. */
void World::convert(int x, int y, u8 mat) {
    Cell& c = cells[y * SIM_W + x];
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
bool World::tryMove(int sx, int sy, int tx, int ty) {
    const int si = sy * SIM_W + sx, ti = ty * SIM_W + tx;
    Cell& s = cells[si];
    Cell& t = cells[ti];

    if (t.mat != MAT_EMPTY) {
        const MatInfo& tm = MATS[t.mat];
        if (tm.kind != KIND_LIQUID && tm.kind != KIND_GAS) return false;
        const MatInfo& sm = MATS[s.mat];
        /* Gases invert the density test: they displace anything *heavier*,
           which is how steam bubbles up through water. */
        if (sm.kind == KIND_GAS) { if (tm.density <= sm.density) return false; }
        else                     { if (tm.density >= sm.density) return false; }
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

void World::updatePowder(int x, int y) {
    Cell& c = cells[y * SIM_W + x];
    const MatInfo& m = MATS[c.mat];

    if (tryMove(x, y, x, y + 1)) return;

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

void World::updateLiquid(int x, int y) {
    Cell& c = cells[y * SIM_W + x];
    const MatInfo& m = MATS[c.mat];

    if (tryMove(x, y, x, y + 1)) return;

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

void World::updateGas(int x, int y) {
    Cell& c = cells[y * SIM_W + x];
    const MatInfo& m = MATS[c.mat];

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

    int dx = (c.flags & F_DIR) ? 1 : -1;
    if (tryMove(x, y, x + dx, y - 1)) return;
    if (tryMove(x, y, x - dx, y - 1)) return;

    for (int attempt = 0; attempt < 2; ++attempt) {
        int destX = x;
        for (int s = 1; s <= (int)m.dispersion; ++s) {
            int nx = x + dx * s;
            if (nx < PLAY_X0 || nx > PLAY_X1) break;
            const Cell& n = cells[y * SIM_W + nx];
            if (n.mat != MAT_EMPTY) {
                const MatInfo& nm = MATS[n.mat];
                if (nm.kind != KIND_GAS || nm.density <= m.density) break;
            }
            destX = nx;
            if (cells[(y - 1) * SIM_W + nx].mat == MAT_EMPTY) break;  /* found a way up */
        }
        if (destX != x) {
            tryMove(x, y, destX, y);
            dirtyArea(imin(x, destX), y, imax(x, destX), y);
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
        if (rngChance(rate)) temp[i] = (u8)(t > AMBIENT_TEMP ? t - 1 : t + 1);
    }

    dirtyPoint(x, y);
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

    convert(x, y, m.boilsTo);
    temp[i] = latentDrain((int)temp[i]);
}

/* ======================================================================
   Dispatch and the frame step
   ====================================================================== */

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

    /* --- table-driven phase changes ----------------------------------- */
    const int t = temp[i];
    if (m.boilTemp && t >= (int)m.boilTemp) {
        convert(x, y, m.boilsTo);
        /* Boiling absorbs latent heat. Without this a single hot cell flashes
           an entire pool to steam in one frame instead of simmering. */
        temp[i] = latentDrain(t);
        return;
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
        convert(x, y, m.coolsTo);   /* fire burns out, steam condenses, lava sets */
        return;
    }
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

    if (m.capacity) updateMoisture(x, y);   /* before moving, so the moisture
                                               travels with the cell */
    if (m.kind == KIND_LIQUID && m.boilsTo) {
        const u8 was = c.mat;
        updateEvaporation(x, y);
        if (c.mat != was) return;           /* it turned to vapour; `m` is now
                                               the wrong material to act on */
    }

    if      (m.kind == KIND_POWDER) updatePowder(x, y);
    else if (m.kind == KIND_LIQUID) updateLiquid(x, y);
    else if (m.kind == KIND_GAS)    updateGas(x, y);
}

void World::step() {
    /* Last frame's accumulated rects become this frame's work list. */
    memcpy(cur, next, sizeof(cur));
    clearDirty(next);

    /* Bottom-to-top so a falling cell lands in a row already dealt with and
       cannot fall twice in one frame. Left/right alternates each frame,
       otherwise piles visibly lean the way the scan runs. */
    const bool leftFirst = (frame & 1) != 0;
    activeChunks = 0;

    for (int cy = CHUNKS_Y - 1; cy >= 0; --cy) {
        for (int ci = 0; ci < CHUNKS_X; ++ci) {
            int cx = leftFirst ? ci : (CHUNKS_X - 1 - ci);
            const Chunk& ch = cur[cy * CHUNKS_X + cx];
            if (ch.minX > ch.maxX) continue;   /* settled: skipped entirely */
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
