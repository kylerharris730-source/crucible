#pragma once
#include "common.h"
#include "materials.h"

static const int SIM_W = 512;
static const int SIM_H = 384;

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
   Temperature is a plain u8 of degrees, carried by every cell including air.
   Ambient 20, water boils at 100, fire burns near 230, so the whole useful
   range fits without scaling. (Nothing below 0 C is representable; if ice
   turns up later this wants an offset.) */
static const int AMBIENT_TEMP   = 20;
static const int HEAT_MIN_DIFF  = 2;   /* below this no conduction happens, so
                                          the field can actually reach rest */
static const int AIR_COOL       = 20;  /* chance/255 per frame that an AIR cell
                                          steps one degree toward ambient -- the
                                          main way heat leaves the scene, as if
                                          into an open room. */
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
static const int LATENT_HEAT    = 30;  /* boiling costs this much temperature,
                                          which stops a heat source flashing a
                                          whole pool to steam in one frame */
static const int FIRE_SPREAD    = 34;  /* chance/255 per frame that a flammable
                                          cell catches from each touching flame;
                                          higher = fire races through wood */

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
    Chunk cur[CHUNK_COUNT];       /* work list being processed this frame */
    Chunk next[CHUNK_COUNT];      /* being accumulated for the next frame */
    u32   frame;
    int   activeChunks;           /* stat, for the HUD */

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

    u8 stamp() const { return (u8)(frame & STAMP_MASK); }

private:
    void updateCell(int x, int y);
    void updateClone(int x, int y);
    void updateVoid(int x, int y);
    void spawnCell(int x, int y, u8 mat);
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
