#pragma once
#include "common.h"
#include "materials.h"

/* The WORLD. Much larger than the view, which shows 512x384 of it -- eight
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
static const int SIM_W = 4096;
/* Grown twice, for the same reason both times: PACING, not scale.

   3072 -> 6144. The surface sat at y=1200 with about 1870 cells of rock beneath
   it -- 62 body heights, a jetpack ride of well under a minute -- and the sky
   ran out about as fast going the other way.

   6144 -> 9216, and this time the growth is ENTIRELY underground: SURFACE_Y
   moved from SIM_H/2 to SIM_H/3 in the same change so the sky stays at the 3072
   cells it already had. The sky was not the problem. The layers were: three
   cave layers cut out of a 3180-cell underground come to about 1050 each, which
   is thirty-five body heights, and a "layer" you cross in thirty-five body
   heights reads as a stripe rather than as a place. The underground is now
   about 6250 cells and each layer roughly 2100.

   The measurement is what makes this safe rather than the hope in the paragraph
   above: a settled chunk is free at any size, so the price is address space and
   not frame time. World is 217 MB and the save's scratch plane another 38 MB,
   against a 32-bit process's 2 GB. That is a third of the budget, though, so
   the NEXT increase is the one that has to argue for itself rather than lean on
   this note. The things that would not survive growing are the per-frame
   whole-grid passes, and there are none -- which is what the rule above
   protects. */
static const int SIM_H = 9216;

/* Chunked dirty rectangles. Each chunk remembers the smallest box that had
   anything happen in it, and only that box is simulated next frame. A pile
   that has finished settling costs literally nothing -- which matters, since
   in a typical scene the overwhelming majority of the grid is inert. */
static const int CHUNK_SHIFT = 5;
static const int CHUNK       = 1 << CHUNK_SHIFT;      /* 32 */
static const int CHUNKS_X    = SIM_W >> CHUNK_SHIFT;
static const int CHUNKS_Y    = SIM_H >> CHUNK_SHIFT;
static const int CHUNK_COUNT = CHUNKS_X * CHUNKS_Y;
/* Five seconds at the normal 60 simulation steps/sec.  This is deliberately a
   per-chunk countdown rather than a wider permanent window: a waterfall keeps
   falling after the camera leaves, while settled terrain remains free. */
static const int LIVE_GRACE_STEPS = 60 * 5;

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

   One byte per chunk is 6 KB for the whole world.

   Underground is subdivided by CAVE LAYER, and that subdivision does two jobs
   at once, which is why it is here rather than being recomputed from depth
   wherever it is wanted. It picks the backdrop, so crossing a layer boundary is
   something you can SEE rather than infer from a depth readout; and it picks
   which enemies may spawn, so "what lives here" is a property of the place
   instead of a threshold every spawn site has to re-derive.

   Appended, never inserted, for the same reason MatId is append-only: the zone
   array is written to saves as CHUNK_COUNT raw bytes, so renumbering these
   would silently relabel every chunk of every existing world. ZONE_LAYER1 is
   deliberately the value ZONE_UNDER already had -- an old save's underground
   is shallow-tier underground, which is exactly what layer 1 is. */
enum ZoneId {
    ZONE_SKY = 0,      /* outdoors: the backdrop is the sky */
    ZONE_LAYER1,       /* underground, above the first stratum */
    ZONE_LAYER2,       /* between the two strata: the difficulty step */
    ZONE_LAYER3,       /* below the second stratum: the deep */
    ZONE_COUNT
};
/* The name the renderer and the light pass grew up with. Kept because it still
   reads better than ZONE_LAYER1 at those two call sites, where what is meant is
   genuinely "not sky" rather than "the shallow tier". */
static const int ZONE_UNDER = ZONE_LAYER1;

/* 0, 1 or 2 for the three cave layers -- the index the per-layer tables are
   sized to. Sky answers 0 rather than -1: every caller of this is already
   inside a branch that established the chunk is not sky, and returning a value
   that indexes nothing would mean each of them needing a guard against a case
   they have already ruled out. */
static inline int caveLayerOf(int zone) {
    return zone <= ZONE_LAYER1 ? 0 : (zone == ZONE_LAYER2 ? 1 : 2);
}

static const int PLAY_X0 = 1;
static const int PLAY_Y0 = 1;
static const int PLAY_X1 = SIM_W - 2;
static const int PLAY_Y1 = SIM_H - 2;

/* --- how thick turf is -----------------------------------------------------
   How far from open air grass can still live. At 1 -- a face on the air -- turf
   is exactly one cell deep everywhere, which at two screen pixels a cell is a
   green line drawn on top of the dirt rather than a layer of anything. Grass
   reaches this far in instead, so a hillside has a band of it.

   It is a radius and not a depth on purpose: measuring it as "cells below the
   surface" would need a surface to measure from, and there is not one -- grass
   grows on the roof of an overhang and down the face of a cut as readily as on
   level ground, and all this rule knows is where the air is.

   Raising it costs a disc scan per ACTIVE grass cell per frame, which is
   affordable for exactly the reason the spreading rule is: a finished lawn
   dirties nothing and is never visited again. Only the growing edge pays, and
   the scan returns on its first hit, which for grass anywhere near a surface is
   immediate.

   One simplification worth knowing about, because it is visible if you go
   looking: the scan asks whether air is NEARBY, not whether it is REACHABLE.
   Air on the far side of a wall thinner than this counts, so dirt packed behind
   a two-cell stone wall with open space beyond it will green. Making it
   line-of-sight or a flood fill would cost per grass cell what the whole
   lighting system costs per frame, to correct something you can only see by
   digging up the wall to look. It did cost half an hour once, though: a test
   arena with one-cell walls had grass crawling 80 cells down the edges of the
   world, because five cells past that wall is the genuinely empty space outside
   the arena. Anything standing in for the edge of the world has to be thicker
   than this. */
static const int GRASS_DEPTH = 5;

/* ---- leaves that have lost their tree --------------------------------------
   A condemned leaf does not vanish on the frame it is condemned; it counts
   down and then falls. The countdown is spread at random across this many
   frames so a felled oak sheds its canopy as a shower over a second and a half
   rather than deleting 28,000 cells in one step -- which read as the tree
   being erased rather than as leaves dying.

   The counter lives in the leaf's MOISTURE byte, which is free: leaves have a
   capacity of 0, so updateMoisture never runs on them, and their wet and dry
   colours are identical so the renderer's wetness blend is a no-op. That is
   the only spare byte a Cell has, and the alternative was a side table keyed
   by position for something that can span a quarter of a chunk.

   Zero therefore means "healthy", and nothing else may write it. */
static const int LEAF_FALL_MAX = 90;

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
/* How many points along a long-range conduction run get checked before the hop
   is taken. See the fast path in updateHeat.

   This is a SPEED-FOR-REALISM dial and the trade was measured before it was
   picked. On a 160-wide graphene column standing in lava, with the camera on
   the junction -- the case that provoked all of this at 30fps:

     probes  spacing   sim      total    fps   what a graphene run jumps
       4       7      12.71    17.5 ms   57    gaps up to 6 cells
       7       4      15.84    20.6 ms   48    gaps up to 3 cells
      14       2      21.51    26.3 ms   38    1-cell gaps, about half of them
      28       1      31.24    36.0 ms   28    nothing -- and SLOWER than walking

   That last row is worth keeping: probing every cell is strictly worse than the
   walk it replaces, because it pays for the probe loop and then walks anyway.
   The fast path only earns its place while it is genuinely coarse.

   Four, chosen deliberately. The cost is real and specific: a graphene run
   hops over an insulating gap narrower than seven cells, so a thin ceramic
   liner between two graphene sheets less than 28 cells apart stops holding
   heat back. Thicker walls behave correctly, and so does anything sitting
   directly against the conductor, which the immediate-neighbour pass handles
   exactly and this never touches.

   Note what is NOT affected. The probe spacing is spread/SPREAD_PROBES, so
   copper (spread 5) gets a spacing of 1 and iron (spread 2) a spacing of 0 --
   both exact. Only graphene is coarse enough to leak, which is the one material
   whose entire character is conducting further than seems reasonable. */
static const int SPREAD_PROBES = 4;

/* --- lighting a fire -------------------------------------------------------

   Until this existed there was no way to start one. The torch emits no heat at
   all, and the only sources in the world are lava -- which is in layer 2, past
   a barrier you cannot break -- and electric sparks, which need a Clock, which
   needs iron, which needs a fire. The survival opening ran entirely on the
   debug Heat brush, which main.cpp is honest about calling a diagnostic tool.

   The ceiling is what makes a striker a striker rather than a portable furnace.
   110 C is above wood's 80 and coal's 90, so it lights both, and below clay's
   120 and every smelting point in the game -- so it cannot fire a pot, cannot
   melt tin at 120, and certainly cannot reach copper at 165 or iron at 190. It
   starts a fire and the FIRE does the work, which is the whole shape of the
   heat ladder: what you burn and what you burn it in are the interesting
   decisions, and being unable to strike a spark was never one of them. */
static const int IGNITE_MAX  = degC(110);
static const int IGNITE_STEP = 14;   /* degrees a frame while it is held */
/* Small: a striker lights a spot, not a room. Wide enough to cover a couple of
   cells of tinder and narrow enough that aiming it is a real act. */
static const int IGNITE_RADIUS = 3;

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
/* Chance/255 per frame that a dissolvable cell touching acid is consumed --
   see g_matDissolvedBy. Below FIRE_SPREAD on purpose: fire racing through a
   plank is meant to feel urgent, acid eating a wall is meant to feel like it
   is working on it, and the difference between "a hazard" and "a tool you
   wait on" is exactly this number. At 20, a single contacting face clears
   in a little over two seconds on average -- long enough to watch it happen,
   short enough that acid is worth carrying. */
static const int ACID_DISSOLVE_CHANCE = 20;

/* How readily a spring pushes water into an empty neighbour, out of 255. Slow
   on purpose: a spring that filled at the rate water flows would be a burst
   pipe rather than a spring, and the pleasure of finding one is watching the
   pool come back. 24 is twice the former seep rate: a discovered spring now
   supports a practical drain or small reservoir, while still filling one cell
   at a time rather than behaving like a burst pipe. */
static const int SPRING_FLOW_CHANCE = 24;

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
    /* Kind-specific payload:
       - porous solids: absorbed moisture
       - sieve/reactive powder: sparse fluid occupant id, plus
         GAS_VOLUME_ONLY when applicable
       - gas: low 7 bits are unexpanded pressure units; high bit marks an
         expansion-only volume with no condensation mass token
       - Clone: latched material id
       Keeping these meanings kind-exclusive avoids another 38 MB world plane. */
    u8 moisture;
    u8 tint;      /* fixed per-cell colour jitter */
    u8 flags;
};

/* flags bit 0 is a liquid's remembered flow direction, so water keeps
   streaming one way instead of jittering in place. */
static const u8 F_DIR = 0x01;

/* Gas pressure encoding in Cell::moisture. A normal/painted gas cell has one
   condensation mass token and value 0. Expansion daughters carry the high bit
   and vanish when cooled; this lets one Water cell expand to three Steam cells
   and still condense back to exactly one Water cell. */
static const u8 GAS_VOLUME_ONLY = 0x80;
static const u8 GAS_EXCESS_MASK = 0x7F;

/* --- the same bit, for powders ---------------------------------------------
   Set on a powder cell that FELL STRAIGHT DOWN on the frame just gone. It is
   what makes a stream of dirt something you can walk through instead of a wall
   -- see cellFalling() below.

   One bit, two meanings, split by kind, exactly like MatInfo::jitter: nothing
   reads both, because a cell is either a liquid or a powder and never neither.
   There was no spare bit to take -- the other seven are the frame stamp, and
   narrowing that trades a 128-frame aliasing margin for a feature -- and the
   swap in tryMove already carries this bit along with the material, which is
   precisely the semantics both meanings want.

   The worst a conversion between the two kinds can do is leave the bit stale for
   one frame: a liquid that freezes into a powder might read as falling for a
   frame, or a thawed powder might flow the wrong way once. Both self-correct on
   the cell's next visit, and neither is observable. */
static const u8 F_FALL = 0x01;

/* --- material in free fall -------------------------------------------------
   A powder cell that is on its way down. Two rules read this and they are two
   halves of one idea: the player does not collide with it, and it does not
   collide with the player. Both have to hold or neither works -- a stream that
   the player can walk into but that still piles against their body stops
   falling the moment it touches them, clears this flag, turns back into a wall
   in exactly the place the player is standing, and the unstick lifts them into
   the air. The pair is why this is one predicate used from both sides rather
   than two mechanisms kept in step by hand.

   FALLING, not merely moving, and the distinction is the whole design. The
   obvious test -- "is there air underneath it?" -- describes only the LEADING
   cell of a stream: measured on a 57-cell column of dirt in mid-air, 56 of them
   had another dirt cell directly below, so a support test would have made a
   falling stream 98% as solid as a wall. Sideways slides are excluded for the
   opposite reason: a slumping pile is settling, not falling, and a heap that
   went soft every time its surface shifted would swallow you whenever you dug
   near it. Straight down is the one motion that means "this is not holding
   anything up, including you". */
static inline bool cellFalling(const Cell& c) {
    return (c.flags & F_FALL) != 0 && MATS[c.mat].kind == KIND_POWDER;
}

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
    int   pressureRoutesRemaining;/* per-frame shared-pocket search budget */

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

    /* Exact union of every player's guaranteed core. The first window keeps
       the adaptive fingers below; additional, possibly distant players mark
       independent islands here. A bounding rectangle would simulate all the
       empty country between two players and make distance itself expensive. */
    u8  liveCoreMask[CHUNK_COUNT];
    int liveWindowCount;

    /* The camera window is the guaranteed core.  A second core-sized budget is
       available as two vertical fingers: activity that reaches an edge grows
       that edge, while a newly active opposite edge reclaims rows from the
       longer finger.  This keeps long falls and rising gas alive without
       paying for a permanently larger square. */
    i32 fingerTop, fingerBottom, fingerLeft, fingerRight;
    i32 liveCoreCX0, liveCoreCY0, liveCoreCX1, liveCoreCY1;

    /* One ZoneId per chunk. See ZoneId above for why this is a label rather
       than a depth test. */
    u8 zone[CHUNK_COUNT];

    /* --- chunks that tick wherever the camera is -------------------------
       An exception to the live window, and the only one. A chunk flagged here
       is simulated even when it is on the far side of the world.

       This exists for built rooms (see room.h): a sealed, backed-in space you
       made is somewhere you left things running, and having your furnace go
       out because you walked away would make every contraption in the game a
       thing you have to babysit. The live window is right for the wilderness
       and wrong for your own workshop, so the answer is not to widen it -- it
       is to let a small, player-authored set of chunks opt out.

       Set by room.cpp and nowhere else. World knows only that some chunks are
       exempt; what earns the exemption is not its business, which is the same
       line drawn around blockX0/blockTaper for the player's body.

       Cost is bounded by construction rather than by hope: rooms are capped in
       size and in number (see room.h), and a settled room costs what any
       settled chunk costs, which is nothing. */
    u8 keepAlive[CHUNK_COUNT];
    u16 liveGrace[CHUNK_COUNT];  /* frames of off-screen simulation remaining */
    int keptChunks;               /* stat, for the HUD */

    u8   zoneAt(int x, int y) const {
        return zone[(y >> CHUNK_SHIFT) * CHUNKS_X + (x >> CHUNK_SHIFT)];
    }
    void setZoneRect(int x0, int y0, int x1, int y1, u8 z) {
        const int cx0 = imax(0, x0 >> CHUNK_SHIFT), cx1 = imin(CHUNKS_X - 1, x1 >> CHUNK_SHIFT);
        const int cy0 = imax(0, y0 >> CHUNK_SHIFT), cy1 = imin(CHUNKS_Y - 1, y1 >> CHUNK_SHIFT);
        for (int cy = cy0; cy <= cy1; ++cy)
            for (int cx = cx0; cx <= cx1; ++cx) zone[cy * CHUNKS_X + cx] = z;
    }

    /* set starts a new per-frame union; add contributes another disjoint core.
       Existing single-player callers only use set and retain their behavior. */
    void setLiveWindow(int x0, int y0, int x1, int y1);
    void addLiveWindow(int x0, int y0, int x1, int y1);
    void clearLiveWindow() { setLiveWindow(0, 0, SIM_W - 1, SIM_H - 1); }

    /* Electrical state is time-based, so it must share the world's actual live
       area rather than its off-screen grace period. Grace preserves pending
       material work after the camera moves; letting clocks run through it would
       keep adding heat to copper whose cooling step has already frozen. Built
       rooms are the intentional exception: their chunks are explicitly kept
       alive so a workshop can keep operating while the player is away. */
    bool electricalLive(int x, int y) const {
        if (x < 0 || x >= SIM_W || y < 0 || y >= SIM_H) return false;
        const int cx = x >> CHUNK_SHIFT, cy = y >> CHUNK_SHIFT;
        const int ci = cy * CHUNKS_X + cx;
        if (keepAlive[ci] || liveCoreMask[ci]) return true;
        return (cx >= liveCoreCX0 && cx <= liveCoreCX1 &&
                cy >= liveCoreCY0 - fingerTop && cy <= liveCoreCY1 + fingerBottom) ||
               (cy >= liveCoreCY0 && cy <= liveCoreCY1 &&
                cx >= liveCoreCX0 - fingerLeft && cx <= liveCoreCX1 + fingerRight);
    }

    /* --- solid entity boxes -----------------------------------------------
       A small fixed set of axis-aligned shapes that material may not settle
       inside. Four is the co-op player cap, and a fixed array keeps the hot
       movement test bounded without making World know what a Player is.

       They give players physical presence -- sand piles on their heads instead
       of falling through them, water flows around them rather than over them.

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

       Disabled boxes set x0 past the right edge, so their first comparison
       fails. */
    static const int MAX_OCCUPANTS = 4;
    struct OccupantBox {
        i32 x0, y0, x1, y1;
        /* Rows of 45-degree taper at the top, one cell of inset per side per
           row. A flat top collects falling material; a pointed one sheds it. */
        i32 taper;
    } occupant[MAX_OCCUPANTS];
    u8 occupantMask;

    void setBlockBoxFor(int slot, int x0, int y0, int x1, int y1, int taper = 0) {
        if (slot < 0 || slot >= MAX_OCCUPANTS) return;
        OccupantBox& b = occupant[slot];
        b.x0 = x0; b.y0 = y0; b.x1 = x1; b.y1 = y1; b.taper = taper;
        occupantMask = (u8)(occupantMask | (1u << slot));
    }
    void clearBlockBoxFor(int slot) {
        if (slot < 0 || slot >= MAX_OCCUPANTS) return;
        OccupantBox& b = occupant[slot];
        b.x0 = SIM_W; b.y0 = SIM_H; b.x1 = -1; b.y1 = -1; b.taper = 0;
        occupantMask = (u8)(occupantMask & ~(1u << slot));
    }
    void clearBlockBoxes() {
        occupantMask = 0;
        for (int slot = 0; slot < MAX_OCCUPANTS; ++slot) {
            OccupantBox& b = occupant[slot];
            b.x0 = SIM_W; b.y0 = SIM_H; b.x1 = -1; b.y1 = -1; b.taper = 0;
        }
    }
    /* Legacy slot-zero wrappers keep all existing single-player and harness
       call sites behaving identically while new code names an occupant. */
    void setBlockBox(int x0, int y0, int x1, int y1, int taper = 0) {
        setBlockBoxFor(0, x0, y0, x1, y1, taper);
    }
    void clearBlockBox() { clearBlockBoxFor(0); }
    int blockerAt(int x, int y) const {
        for (int slot = 0; slot < MAX_OCCUPANTS; ++slot) {
            if (!(occupantMask & (1u << slot))) continue;
            const OccupantBox& b = occupant[slot];
            /* Ordered so cells far from every occupant reject before taper
               arithmetic. This loop is four fixed, cheap bounds tests. */
            if (x < b.x0 || x > b.x1 || y < b.y0 || y > b.y1) continue;
            const i32 inset = b.taper - (y - b.y0);
            if (inset <= 0 || (x >= b.x0 + inset && x <= b.x1 - inset)) return slot;
        }
        return -1;
    }
    bool blocksCell(int x, int y) const { return blockerAt(x, y) >= 0; }

    void reset();
    void step();
    /* replace=false leaves whatever is already there alone, so you can pour
       into a scene without carving through it. Erasing ignores the flag. */
    void paint(int cx, int cy, int r, u8 mat, bool replace = true);
    void paintBg(int cx, int cy, int r, u8 mat);
    void heat(int cx, int cy, int r, int delta);
    /* A striker's spark: warms a small disc TOWARD a ceiling and no further.
       See IGNITE_MAX for why the ceiling is the whole design. */
    void ignite(int cx, int cy, int r);
    void setCell(int x, int y, u8 mat);
    /* Change what a cell is MADE OF and nothing else: temperature, moisture and
       speckle all survive. setCell is the wrong verb when the thing in the cell
       is the same object in a different state -- it resets the temperature to
       the material's spawn value and re-rolls the tint, so a door in a hot room
       would come back to ambient every time you opened it and would visibly
       re-grain as it swung.

       Deliberately distinct from the private `convert` the phase-change rules
       use. That one re-rolls the tint on purpose, because a cell of water
       becoming a cell of steam really is new material and should not inherit the
       old speckle. This one is for an object that stayed itself. */
    void swapMat(int x, int y, u8 mat);

    /* Shove a column of loose material up one cell, leaving (x, y) empty.

       This is a PISTON, not part of the simulation: nothing about buoyancy or
       density decides it, and it happens because a machine did it. The spout
       uses it to dispense against a head of its own output -- without it, a
       spout pointed up stops the instant one cell of water is sitting on its
       face, which is the moment you actually wanted a pump.

       Only loose material moves. Anything static -- rock, a device, a door --
       refuses the lift outright rather than being shunted, so this cannot push
       a wall or extrude a machine. `maxLift` is how far up it will look for
       somewhere to put the column, and it is the pump's HEAD: past that the
       lift simply fails and the spout skips that cell.

       Returns false and changes nothing on failure, so a caller can treat it as
       "did the piston fire". */
    bool liftColumn(int x, int y, int maxLift);

    /* Destroy a cell the way a blast or a falling canopy does: it leaves
       nothing behind, UNLESS it was a husk -- something whose whole job is to
       hold something smaller. A seed pod broken in mid-air leaves its seed,
       which is a powder, so it drops out of the tree and lands where you can
       pick it up or water it.

       Not the same thing as digInto(), which banks the drop straight into the
       pack. That is right for a tool you are holding and wrong for everything
       else: a shot has no inventory to put anything in, and a canopy shedding
       into your pack from four hundred cells away would be absurd. So the two
       differ in WHERE the drop goes and agree on what it is.

       "Husk" is spelled as "the thing it drops is a seed" rather than as a
       list, which keeps it right when a species is added and keeps it from
       catching the other user of g_matDropsAs: an open door drops a closed
       one, and a door you blew up should not leave a door standing. */
    void breakCell(int x, int y);

    /* Schedule cells for simulation next frame. dirtyArea covers a span plus a
       one-cell margin; anything that moves further than one cell in a step
       must use it, or cells along the swept path never get woken. */
    void dirtyArea(int x0, int y0, int x1, int y1);
    void dirtyPoint(int x, int y) { dirtyArea(x, y, x, y); }

    /* Grass: spreads across exposed dirt, dies back to dirt when buried.
       Called from updateCell for grass cells only. */
    void updateGrass(int x, int y);
    /* One frame of a condemned leaf's countdown. Called from updateCell for
       leaf cells only, and only for ones somebody has condemned -- a healthy
       leaf carries a zero here and costs one compare. */
    void updateLeafFall(int x, int y);
    /* Is there open air within `r` cells? r = 1 is the four touching
       neighbours; anything larger is a disc. This is what "exposed" means for
       grass, and it is a RADIUS rather than a yes/no because a turf line one
       cell thick reads as a green pencil stroke on top of the dirt rather than
       as ground with something growing on it. See GRASS_DEPTH. */
    bool airWithin(int x, int y, int r) const;
    bool airAdjacent(int x, int y) const { return airWithin(x, y, 1); }

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

    /* --- seeds that have come to rest -----------------------------------
       Cells where a tree seed is sitting on wet ground, reported by the
       simulation and drained by treesTick.

       The simulation is the only thing that knows a powder has SETTLED, and
       "settled" is the whole of the planting rule -- so the alternative is
       tree.cpp scanning the world for seeds every frame, which is 12.6 M cells
       to find at most a handful. This is one push on the rare frame a seed
       lands, and the World learns nothing about trees beyond "somebody may care
       about this cell": it does not convert it, and it does not know what
       happens next.

       A ring rather than a queue, and a small one: if more than 32 seeds land
       in a single frame the extras are dropped, and the cost of that is one
       seed you have to nudge. The alternative is an unbounded buffer to serve a
       case that means somebody emptied a stack of seeds off a cliff. */
    static const int MAX_SPROUTS = 32;
    i32 sprout[MAX_SPROUTS];
    int sproutCount;

    /* --- wood that stopped being wood -----------------------------------
       The other half of the same arrangement, for the other direction. Leaves
       die when nothing joins them to a trunk, and the only moment that can
       become true is the moment a wood cell goes away -- mined, burned,
       melted, voided, it does not matter which. So the world reports the
       position and tree.cpp decides what it means, exactly as with seeds.

       Reporting the EVENT rather than having leaves ask "am I still supported"
       is what makes this affordable. A canopy is 28,000 cells; a rule they
       each re-ask would be 28,000 floods a frame for a tree nobody is touching.
       Wood going away happens when you swing at it.

       Reported per CHUNK rather than per cell, and that is not a size
       optimisation -- it is what makes the answer right. A canopy is not one
       blob: measured, an oak's leaves come in two pieces, a 24,058-cell crown
       and a 1,221-cell branch tuft that touches its own branch and nothing
       else. Each piece needs the audit to start somewhere inside it, so a
       per-cell list that drops its overflow leaves whichever piece got dropped
       hanging in the sky -- which is exactly what happened: felling a tree in
       one frame left the tuft floating.

       Chunks DEDUPLICATE, which is what fixes it. A trunk three hundred cells
       tall passes through about ten of them however many times you swing at it,
       so the list stops growing almost immediately and a whole tree vanishing
       at once still fits. The audit then sweeps each reported chunk for leaves
       and floods from every piece it finds. */
    static const int MAX_FELLED = 256;
    i32 felled[MAX_FELLED];      /* chunk indices */
    int felledCount;
    /* Set for a chunk already on the list, so reporting stays O(1) with three
       thousand removals in a frame rather than rescanning the list each time.
       Cleared by walking the list, never by wiping the array. */
    u8  felledMark[CHUNK_COUNT];

private:
    void updateCell(int x, int y);
    void reportFelled(int x, int y, u8 was, u8 now);
    void updateClone(int x, int y);
    void updateVoid(int x, int y);
    void spawnCell(int x, int y, u8 mat);
    void heatPair(int i, int jx, int jy);
    void updateHeat(int x, int y);
    void updateMoisture(int x, int y);
    void updateEvaporation(int x, int y);
    bool updateConvection(int x, int y);
    void updatePowder(int x, int y);
    void updateLiquid(int x, int y);
    void updateGas(int x, int y);
    bool updateGasPressure(int x, int y);
    bool displaceGasForLiquid(int sx, int sy, int tx, int ty);
    void updateFilterFluid(int x, int y);
    bool moveFilterFluid(int sx, int sy, int tx, int ty);
    void convert(int x, int y, u8 mat);
    void phaseChange(int x, int y, u8 mat);
    /* Whether an unlike-liquid exchange is permitted, and WHICH WAY.

       A bool was enough while only one direction existed. The general gravity
       path never exchanges unlike liquids; the submerged-leveling calls opt in
       once they have proved the move is right. Both of those were the denser
       parcel moving toward a lower level, so tryMove could simply require the
       source to be the denser one.

       The lighter-pocket leveling needs the opposite exchange -- a light parcel
       trading upward into a denser one -- and it cannot share the old flag,
       because the callers that pass DENSER_WINS rely on tryMove's density test
       to reject the inverse for them. Making the test symmetric would let a
       light parcel sink into a dense one, which is the bug the test exists to
       prevent. So the direction is stated instead of guessed.

       NONE is 0 and DENSER_WINS is 1, so the historical `true` still means what
       it always meant. */
    enum LiquidSwap { LIQ_SWAP_NONE = 0, LIQ_SWAP_DENSER_WINS, LIQ_SWAP_LIGHTER_WINS };

    bool tryMove(int sx, int sy, int tx, int ty,
                 int liquidSwap = LIQ_SWAP_NONE);
};

extern World g_world;
