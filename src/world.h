#pragma once
#include "common.h"
#include "materials.h"

/* The WORLD. Much larger than the view, which shows 512x384 of it -- four
   screens across and eight down.

   Making this bigger is nearly free, and the benchmark is the reason to
   believe that rather than a hope: localised activity costs the same at every
   size, because the chunk system only ever visits chunks something is
   happening in. Fire and boiling across a few hundred chunks measured 12.5 ms
   at 6.3M cells and 11.5 ms at 786k. A settled world is 0.005 ms at either.

   What does NOT scale is work proportional to the whole grid. Rendering the
   entire world every frame went from 0.3 ms to 8.8 ms across that range, which
   is why renderView() draws only the window (see render.h), and a world where
   every cell is churning at once costs 120 ms, which is why step() honours a
   live window (see setLiveWindow below). Both of those are hard rules now: any
   new per-frame pass over SIM_W*SIM_H undoes the whole thing. */
static const int SIM_W = 2048;
static const int SIM_H = 3072;

/* Chunked dirty rectangles. Each chunk remembers the smallest box that had
   anything happen in it, and only that box is simulated next frame. A pile
   that has finished settling costs literally nothing -- which matters, since
   in a typical scene the overwhelming majority of the grid is inert. */
static const int CHUNK_SHIFT = 5;
static const int CHUNK       = 1 << CHUNK_SHIFT;      /* 32 */
static const int CHUNKS_X    = SIM_W >> CHUNK_SHIFT;
static const int CHUNKS_Y    = SIM_H >> CHUNK_SHIFT;
static const int CHUNK_COUNT = CHUNKS_X * CHUNKS_Y;

/* The outer ring of cells is permanent wall. It draws the box you pour into,
   and because no rule ever runs on a wall cell, every neighbour lookup from a
   simulated cell is guaranteed in-bounds -- so the movement rules need no
   bounds checks at all. */
/* Background layer packing -- see World::bg below. */
static const u8 BG_MAT_MASK = 0x7F;
static const u8 BG_PLACED   = 0x80;

/* --- zones -----------------------------------------------------------------
   What you see when there is NOTHING behind a cell -- no wall, natural or
   built. Open sky, or the dark of being underground.

   Labelled PER CHUNK, not derived from depth, and that is the whole point.
   A height threshold ties the backdrop to the terrain's shape, so the moment
   the surface stops being flat the sky either cuts into a hillside or stops
   short of a valley floor. Generation knows which chunks it made into sky and
   which into rock; asking it to encode that as a number the renderer can
   rediscover from y is throwing away the answer and guessing it back.

   One byte per chunk is 6 KB for the whole world. */
enum ZoneId {
    ZONE_SKY = 0,      /* outdoors: the backdrop is the sky */
    ZONE_UNDER,        /* underground: the backdrop is cave dark */
    ZONE_COUNT
};

static const int PLAY_X0 = 1;
static const int PLAY_Y0 = 1;
static const int PLAY_X1 = SIM_W - 2;
static const int PLAY_Y1 = SIM_H - 2;

/* ---- moisture model -----------------------------------------------------
   Moisture is measured so that one absorbed water cell is worth
   MOISTURE_UNIT. Absorption and dripping both move exactly that amount, so
   water is never quietly created or destroyed by the wetness system. */
static const int MOISTURE_UNIT = 64;
static const int MAX_TRANSFER  = 24;  /* per cell per frame, so wetting spreads
                                         as a visible front rather than
                                         teleporting */

/* Wicking is anisotropic: gravity helps moisture downward and resists it
   upward. The gradient a transfer must overcome is the material's `wick`
   shifted by these, so reach downward, sideways and upward is in a 4:2:1
   ratio. */
static const int WICK_DOWN_SHIFT = 0;
static const int WICK_SIDE_SHIFT = 1;
static const int WICK_UP_SHIFT   = 2;

/* ---- heat ---------------------------------------------------------------
   Temperature is a plain u8 carried by every cell including air, holding
   degrees Celsius plus TEMP_OFFSET -- see the encoding note in materials.h.
   Ice froze the offset into existence: the old scale started at 0 C, so there
   was nothing below freezing to represent and the heat view had no cold half.
   Ambient is room temperature, water boils at 100 C and freezes at 0 C, and
   the range runs -40 C .. +215 C. */
static const int AMBIENT_TEMP   = degC(20);
static const int HEAT_MIN_DIFF  = 2;   /* below this no conduction happens, so
                                          the field can actually reach rest */
static const int AIR_COOL       = 20;  /* chance/255 per frame that an AIR cell
                                          steps one degree toward ambient -- the
                                          main way heat leaves the scene, as if
                                          into an open room. */
/* Conductivity used ONLY between two air cells, in place of Empty's heatCond.
   It exists because those are two different jobs wearing one number.

   Empty's own heatCond (6, in materials.cpp) is near-insulating so that a hot
   SOLID next to air barely bleeds -- conduction runs at min(condA, condB), so
   that number is what keeps lava molten for ~40s and stops a flame heating the
   whole room. It has to stay small.

   But it also governed air-to-air, and there it was not slow, it was off:
   move = (adiff * cond) >> 9 needs a large gap before it even reaches 1. Warm
   air spread only while its edge was steep and then stopped dead, so a hot
   patch sat where it was, drifting toward ambient in place for half a minute,
   instead of dissipating into the room. Measured: a blob of hot air reached 12
   cells and stalled, and a single air cell 20 degrees above ambient NEVER
   warmed its neighbour at all.

   Splitting the two lets air mix with air properly while air-to-solid keeps
   its own separate, still-tiny rate. Note this makes heat leave the scene
   FASTER, which is the opposite of the usual intuition about raising a
   conductivity: conduction only moves energy, AIR_COOL is what removes it, and
   AIR_COOL is a per-cell chance -- so spreading a hot patch over more cells
   multiplies the number of places it can drain from.

   The two numbers are coupled and were tuned together, so change them
   together. Air that spreads heat is also air that carries heat off a hot
   solid, which is why the solid-facing value had to come down from 12 to 6.
   Measured against the pre-change build:

     hot air blob, spread      15 cells -> 26        (the point of the change)
     hot air blob, gone after  1911f    -> 856f      (32s -> 14s)
     lava, thick blob          2103f    -> 1760f
     lava, thin puddle         1104f    -> 1227f
     steam rise                339      -> 339       (unchanged)

   Lava converges a little: thick pools cool sooner and thin ones last longer,
   so depth matters less than it did (a 1.9x spread became 1.4x). Lava's own
   heatMassShift 3 -> 2 lands every lava figure nearer the old build, but that
   halves its heat capacity to chase a 16% metric, so it was left alone.

   The one behaviour that genuinely changed is a heater buried in rock: it used
   to melt 104 cells of a stone blob, now 20. That was the old blanket at work
   -- heat had nowhere to go, so the whole rock cooked. A heater now melts a
   pocket around itself, which is the more defensible result, but it IS a
   visible difference. It cannot be tuned back via MACHINE_DRIVE: melting is
   limited by the blob's equilibrium against air, not by how hard the machine
   pushes (measured flat at 20 cells for drive 24 through 255).

   A heater under WOOD was hit by the same effect and IS recovered, via
   MACHINE_DRIVE -- see the note there, since the mechanism is worth reading
   before touching either number. */
static const int AIR_MIX        = 160;
static const int SOLID_COOL     = 3;   /* the same for solids/liquids, but slow:
                                          they shed heat mostly by conduction, so
                                          this only ensures they eventually reach
                                          ambient and sleep rather than staying
                                          warm forever. Higher = shorter reach. */
static const int GAS_COOL       = 14;  /* gases sit between: a puff of steam or
                                          smoke is mixing with open air, so it
                                          cools faster than a solid but still
                                          rides a good way up first. This is the
                                          knob for how far steam floats before
                                          it condenses back to water. */
static const int LATENT_HEAT    = 30;  /* a phase change costs this much
                                          temperature, pulling the cell toward
                                          ambient from whichever side it was
                                          on. It is what stops a heat source
                                          flashing a whole pool to steam in one
                                          frame, and equally what stops a warm
                                          room shattering an ice block in one.
                                          Applied via latentDrain(). */
static const int FIRE_SPREAD    = 34;  /* chance/255 per frame that a flammable
                                          cell catches from each touching flame;
                                          higher = fire races through wood */

/* Heater and cooler setpoints. These are the extremes of the u8 scale on
   purpose: the heater sits above stone's melting point (220) and so above
   every other threshold in the table, and the cooler sits below every
   coolTemp, so both are unambiguously "all the way" rather than tuned to
   beat one particular material. Neither is a starting temperature -- the
   machine is pinned back to it every frame (see updateCell), which is what
   makes it a source of unlimited capacity instead of a hot rock that cools. */
static const int HEATER_TEMP    = 255;
static const int COOLER_TEMP    = 0;
/* Degrees per frame each machine forces into (or out of) each of its four
   orthogonal neighbours, on top of ordinary conduction.

   Holding the machine at its setpoint and letting plain conduction spread it
   is not enough, and the reason is worth keeping: conduction runs at
   min(condA, condB), so the rate is set by the POORER conductor -- the
   neighbour. A heater touching stone therefore delivers at stone's rate
   however conductive the heater is, and the stone settles into a gradient
   that levels off around 150: short of its 220 melting point, so a max-heat
   machine could not melt rock. Raising the heater's own heatCond does nothing
   at all about this, which is the trap.

   Driving the neighbour directly is what makes it a source rather than merely
   a hot object. It stays bounded because it is a step toward the setpoint and
   never past it, so the machine can only ever drag its neighbourhood to the
   setpoint -- never beyond, and never without limit.

   Raised 24 -> 64 when air started mixing (AIR_MIX). A heater under a wood pile
   never ignited it directly even before: it heated the AIR POCKET beside
   itself, which pooled at ~250 because that air had nowhere to shed to, and the
   pocket slow-cooked the pile past wood's ignition point. Once air disperses,
   the pocket settles at ~194 instead, the pile asymptotes at 97 -- just under
   the 120 it needs -- and a heater under wood does nothing at all forever. 64
   puts the pocket back over the line. It is a knee, not a slope: 48 is enough
   and 48/96/160/255 all behave identically, so 64 is simply clear of the edge.
   Note this cannot fix the same shortfall for stone (see AIR_MIX) -- melting is
   limited by the blob's own equilibrium, not by the pocket. */
static const int MACHINE_DRIVE  = 64;

/* ---- liquid spreading ---------------------------------------------------
   Liquids fall at a flat one cell per frame, so a poured body stays coherent
   on the way down rather than stretching out and shedding droplets. (An
   acceleration model was tried and removed: it broke streams into visibly
   separated packets, which read as wrong.) All of the naturalness comes from
   how a liquid behaves once it has landed, below.

   How many cells of liquid overhead are counted as pressure. Each one adds a
   cell of sideways reach, so buried liquid spreads further than surface liquid
   and a pile slumps into a dome instead of holding vertical walls. Capped
   because it is a short upward scan per settled liquid cell. */
static const int PRESSURE_MAX   = 10;

struct Cell {
    u8 mat;
    u8 moisture;
    u8 tint;      /* fixed per-cell colour jitter */
    u8 flags;
};

/* flags bit 0 is a liquid's remembered flow direction, so water keeps
   streaming one way instead of jittering in place. */
static const u8 F_DIR = 0x01;

/* flags bits 1..7 hold a 7-bit stamp of the frame this cell was last visited
   on. Comparing it against the current frame gives "already handled this
   frame" without clearing a flag array every frame.
   A single parity bit is NOT enough, and getting this wrong is subtle: it
   assumes every cell is visited every frame, which is precisely what dirty
   rectangles stop doing. A cell that goes unvisited for one frame comes back
   with a parity that aliases to "already handled", updateCell early-returns
   forever, and nothing dirties it again -- which is what stranded sand in
   mid-air. Seven bits pushes aliasing out to 128 frames, and updateCell
   dirties anything it skips, so even that case self-heals in one frame. */
static const u8 STAMP_SHIFT = 1;
static const u8 STAMP_MASK  = 0x7F;

struct Chunk {
    i32 minX, minY, maxX, maxY;   /* inclusive; empty when minX > maxX */
};

struct World {
    Cell  cells[SIM_W * SIM_H];
    /* Temperature lives in its own array rather than inside Cell: it keeps
       Cell at a tidy 4 bytes (16 to a cache line) and means the movement rules
       never pay for heat they do not read. */
    u8    temp[SIM_W * SIM_H];

    /* --- the background layer --------------------------------------------
       What is BEHIND the cell: the wall of a tunnel, the back of a room. It
       is scenery, not material. Nothing falls, flows, burns or conducts here,
       and the simulation never reads this array at all -- which is exactly why
       it is a separate array rather than a field in Cell. Cell stays 4 bytes
       and the movement rules keep paying nothing for it.

       One byte holds two things:
         bits 0-6  the MatId it looks like (0 = nothing, open sky or void)
         bit 7     BG_PLACED -- a player put it there

       That flag is the whole reason this layer exists rather than being a
       cosmetic tint. A room is going to be defined as an enclosed space backed
       by NON-NATURAL background, so "who put this here" has to survive in the
       world, and seven bits is ample for a table of 28 materials. */
    u8    bg[SIM_W * SIM_H];
    Chunk cur[CHUNK_COUNT];       /* work list being processed this frame */
    Chunk next[CHUNK_COUNT];      /* being accumulated for the next frame */
    u32   frame;
    int   activeChunks;           /* stat, for the HUD */

    /* --- the live window -------------------------------------------------
       Chunks outside this rectangle are not simulated at all. Everything in
       them keeps its state and resumes exactly where it left off when the
       window returns; nothing is discarded.

       This is what makes a big world safe. Without it, one world-wide event --
       a cave-in, a runaway heat source -- costs 120 ms a frame at 2048x3072,
       and the player cannot even see most of what they are paying for. With
       it, the worst case is bounded by the window's area no matter how large
       the world becomes.

       Measured in CELLS, and rounded outward to whole chunks when tested, so
       the caller can hand over a pixel rectangle without thinking about chunk
       geometry.

       Defaults to the entire world, which matters more than it looks: every
       headless test and benchmark drives World directly and never sets one, so
       the default has to be "simulate everything" or half the suite would
       quietly stop simulating and still pass. */
    i32 liveX0, liveY0, liveX1, liveY1;

    /* One ZoneId per chunk. See ZoneId above for why this is a label rather
       than a depth test. */
    u8 zone[CHUNK_COUNT];

    u8   zoneAt(int x, int y) const {
        return zone[(y >> CHUNK_SHIFT) * CHUNKS_X + (x >> CHUNK_SHIFT)];
    }
    void setZoneRect(int x0, int y0, int x1, int y1, u8 z) {
        const int cx0 = imax(0, x0 >> CHUNK_SHIFT), cx1 = imin(CHUNKS_X - 1, x1 >> CHUNK_SHIFT);
        const int cy0 = imax(0, y0 >> CHUNK_SHIFT), cy1 = imin(CHUNKS_Y - 1, y1 >> CHUNK_SHIFT);
        for (int cy = cy0; cy <= cy1; ++cy)
            for (int cx = cx0; cx <= cx1; ++cx) zone[cy * CHUNKS_X + cx] = z;
    }

    void setLiveWindow(int x0, int y0, int x1, int y1) {
        liveX0 = x0; liveY0 = y0; liveX1 = x1; liveY1 = y1;
    }
    void clearLiveWindow() { setLiveWindow(0, 0, SIM_W - 1, SIM_H - 1); }

    /* --- solid entity box ------------------------------------------------
       A single axis-aligned box that no material may move into. It exists so
       the player has physical presence -- sand piles on their head instead of
       falling through it, water flows around them rather than over them.

       This lives in World, and is set from outside each frame, so that
       world.cpp needs to know nothing about players or entities. It is a box
       of cells that happens to be occupied; who occupies it is not the
       simulation's business.

       Enforced in exactly one place, tryMove(), which every single material
       movement already funnels through. That is what makes presence almost
       free: one test in one function covers powders, liquids and gases at
       once, with no per-material rules and no cells written into the grid.
       Putting the entity IN the grid was the obvious alternative and is worse
       in every respect -- it needs a density, the falling-sand rules are then
       free to shove it around, and material it moves into has to go somewhere.

       Disabled by setting x0 past the right edge, so the very first comparison
       fails and the rest are never evaluated. */
    i32   blockX0, blockY0, blockX1, blockY1;
    /* Rows of 45-degree taper at the top of the box, one cell of inset per side
       per row. An occupant with a flat top collects whatever falls on it; a
       pointed one sheds it. See the note in player.h for why the slope has to
       be one cell per row and not shallower. */
    i32   blockTaper;

    void setBlockBox(int x0, int y0, int x1, int y1, int taper = 0) {
        blockX0 = x0; blockY0 = y0; blockX1 = x1; blockY1 = y1; blockTaper = taper;
    }
    void clearBlockBox() {
        blockX0 = SIM_W; blockY0 = SIM_H; blockX1 = -1; blockY1 = -1; blockTaper = 0;
    }
    bool blocksCell(int x, int y) const {
        /* Ordered so the overwhelmingly common case -- nowhere near the
           occupant -- fails on the first comparison against a constant, and the
           taper arithmetic only runs for cells actually inside the box. */
        if (x < blockX0 || x > blockX1 || y < blockY0 || y > blockY1) return false;
        const i32 inset = blockTaper - (y - blockY0);
        if (inset <= 0) return true;
        return x >= blockX0 + inset && x <= blockX1 - inset;
    }

    void reset();
    void step();
    /* replace=false leaves whatever is already there alone, so you can pour
       into a scene without carving through it. Erasing ignores the flag. */
    void paint(int cx, int cy, int r, u8 mat, bool replace = true);
    void heat(int cx, int cy, int r, int delta);
    void setCell(int x, int y, u8 mat);

    /* Schedule cells for simulation next frame. dirtyArea covers a span plus a
       one-cell margin; anything that moves further than one cell in a step
       must use it, or cells along the swept path never get woken. */
    void dirtyArea(int x0, int y0, int x1, int y1);
    void dirtyPoint(int x, int y) { dirtyArea(x, y, x, y); }

    const Cell& at(int x, int y) const { return cells[y * SIM_W + x]; }

    /* --- background accessors -------------------------------------------
       No dirtying, no chunk marking: the background is never simulated, so
       nothing needs waking when it changes. Only the renderer reads it. */
    u8   bgAt(int x, int y)       const { return (u8)(bg[y * SIM_W + x] & BG_MAT_MASK); }
    bool bgPlaced(int x, int y)   const { return (bg[y * SIM_W + x] & BG_PLACED) != 0; }
    void setBg(int x, int y, u8 mat, bool placed) {
        bg[y * SIM_W + x] = (u8)((mat & BG_MAT_MASK) | (placed ? BG_PLACED : 0));
    }
    void clearBg(int x, int y) { bg[y * SIM_W + x] = 0; }

    u8 stamp() const { return (u8)(frame & STAMP_MASK); }

private:
    void updateCell(int x, int y);
    void updateClone(int x, int y);
    void updateVoid(int x, int y);
    void spawnCell(int x, int y, u8 mat);
    void heatPair(int i, int jx, int jy);
    void updateHeat(int x, int y);
    void updateMoisture(int x, int y);
    void updateEvaporation(int x, int y);
    void updatePowder(int x, int y);
    void updateLiquid(int x, int y);
    void updateGas(int x, int y);
    void convert(int x, int y, u8 mat);
    bool tryMove(int sx, int sy, int tx, int ty);
};

extern World g_world;
