#pragma once
#include "world.h"

/* --- multi-cell devices ----------------------------------------------------

   A machine that occupies a rectangle of cells rather than one, sits on a grid,
   and can be right-clicked to change what it does.

   --- why these are not materials ---
   Everything else you place in this world is a material, and materials are the
   right answer right up until a thing needs STATE. A thermocouple has a
   setpoint, a clock has a period and a phase, a placer has an inventory. A Cell
   is four bytes -- mat, moisture, tint, flags -- and every one of them already
   means something, so there is nowhere to keep any of that. The Clone material
   is the existing high-water mark for this trick, and all it manages to smuggle
   into a cell is a single material id hidden in an unused moisture byte.

   So a device is an ENTITY with a side table, and the grid holds MAT_DEVICE
   wherever one stands. That split is exactly the arrangement the player already
   uses (see the note on blockX0 in world.h) and that rooms use: the simulation
   is told only "these cells are solid and man-made", and what kind of machine it
   is, what it is set to and what it is doing are none of its business.

   --- why they are drawn on top ---
   Rendering a device needs to know which device a cell belongs to, and the
   obvious answer -- a per-cell index array -- costs 25 MB at this world size for
   something at most a few hundred cells care about. A linear scan per cell is
   worse: 196k view cells against the device list, every frame.

   Neither is necessary, because devices draw themselves AFTER renderView, the
   same way the character and the held tool already do. The list is walked once
   per frame, not once per cell, and the cost is proportional to the number of
   devices rather than to the size of the world or the view.

   --- the grid ---
   Cell-aligned, and CENTRED on where you clicked. Not snapped to a coarse
   lattice of whole footprints, which is what this did first and which was a
   mistake worth recording: it made it impossible to bolt a machine flush against
   anything. A thermocouple has to touch the furnace it is sensing, and with a
   14-cell lattice the snapped footprint always swallowed a few cells of the
   furnace wall and the placement was refused -- correctly, and uselessly. The
   lattice bought tidy rows of devices and cost the ability to put one where it
   needed to go.

   So overlap is a rectangle test against the device list instead of a lookup of a
   lattice slot. That is a few dozen comparisons on a click, which is nothing, and
   it is the price of being able to build. */

/* Footprint, in cells, and it is uniform across every type on purpose. Devices
   are meant to tile, and a lattice of mixed sizes either has gaps or needs a
   packing rule; neither is worth it for a difference nobody would read as
   meaningful.

   14 is the size of the existing sprite canvas (SPR_W), which is not a
   coincidence -- it means one piece of 14x14 character art serves as both the
   inventory icon and the object in the world at one art pixel per cell. Against
   a character 8 cells wide and 22 tall, a 14-cell box reads as a chunky piece of
   equipment: wider than the player, about two thirds their height. */
static const int DEV_W = 14;
static const int DEV_H = 14;

/* Capped, and the cap is what makes the per-frame walk over all devices
   defensible. 128 machines is far more than any contraption needs and still only
   128 cheap updates a frame. */
static const int MAX_DEVICES = 128;

enum DeviceType {
    DEV_THERMOCOUPLE = 0,   /* watches temperature, fires when it crosses a mark */
    DEV_CLOCK,              /* fires on a period, so things can happen in order */
    /* --- the automation pair ---------------------------------------------
       A placer puts material into the world, a miner takes it out, and both are
       driven by a spark. Between them and the clock and the thermocouple, the ore
       process becomes something you can build a machine to do: drop a charge, heat
       it, wait for the trip, break the crust.

       Both act on the ROW DIRECTLY BELOW their footprint, and neither has a
       facing. That is a real restriction and it is chosen over the alternative:
       rotation would mean another piece of state, another thing to show on the
       panel, another thing to get wrong when you place one -- and a hopper that
       drops downward and a drill that bites downward are what both of these are
       for nine times out of ten. Gravity already gives "down" a meaning nothing
       else has. */
    DEV_PLACER,
    DEV_MINER,
    /* A light, and the one device whose cells you can WALK THROUGH. It is a device
       rather than a material now for the reason a thermocouple is: at one cell a
       torch was a 2x2 dot on screen -- it lit well and did not look like an object.
       See DeviceInfo::cellMat for how a device gets to be passable. */
    DEV_TORCH,
    DEV_COUNT
};

/* How much either machine can hold. Generous enough that a placer does not need
   babysitting during a smelt, small enough that it is a buffer rather than a
   warehouse -- a machine that could hold 100000 like a player's stack would make
   the whole logistics question disappear. */
static const int DEV_CAP = 2000;

/* ==========================================================================
   Electricity
   ==========================================================================

   A spark is absorbed by a conductor, travels through it, and is spat out at the
   far end. That is the Powder Toy model and it is the right one here: it makes a
   wire a thing you draw with an ordinary material rather than a special object,
   and it makes a circuit something you can see working.

   --- why a spark is an entity and not a cell state ---
   Powder Toy implements this as a STATE applied to the conductor: the cell becomes
   SPRK, remembers what it used to be, and reverts after a few frames. That needs
   somewhere to keep the original material id and a countdown, per cell.

   There is nowhere. A Cell is mat, moisture, tint and flags; moisture and tint
   both mean something for metals, and flags is one direction bit plus a seven-bit
   frame stamp whose width is load-bearing (see the note in world.h). The one
   material that pulls this trick off, Clone, gets away with a single id in a
   moisture byte it happens not to use -- and it still could not fit a timer.

   So the spark is a small travelling entity and the conductor is untouched. That
   turns out to be better rather than merely necessary: the grid is not written to
   at all as a spark passes, so no chunk is dirtied, no cell is converted and
   converted back, and a live circuit costs nothing in the simulation. The whole
   system is a few hundred integers.

   --- no branching ---
   A spark takes ONE path. It prefers to carry straight on, and turns only when it
   must. A junction does not split it.

   That is a deliberate restriction and the reason is containment: splitting inside
   a solid slab of metal doubles the spark count per cell crossed, and since there
   is no cell state to mark a conductor as already-sparked there is nothing to stop
   it. One path per spark means a wire behaves exactly as drawn, and a player who
   wants two things to happen runs two wires -- which is legible, where a slab
   full of exponentially multiplying sparks is not.

   Even so a spark could circle a loop of wire forever, so every one carries a
   lifetime. Between that and the array cap the worst case is bounded by
   construction rather than by hope. */

/* Cells a spark advances per frame. One, so a circuit is something you can watch
   work -- at 60 Hz a 100-cell run of wire takes under two seconds, which is fast
   enough to feel immediate and slow enough to debug by eye. */
static const int SPARK_SPEED = 1;

/* Frames before a spark gives up. The backstop against a loop of wire: with no
   cell state marking where it has been, a ring circuit would carry one round for
   ever. 600 is ten seconds, far longer than any sane run of wire. */
static const int SPARK_LIFE = 600;

static const int MAX_SPARKS = 512;

struct Spark {
    i32 x, y;
    /* The step just taken. Two jobs: it is the direction to prefer next (a spark
       carries on rather than doubling back), and its negation is the one move
       that is forbidden. Without that a spark oscillates between two adjacent
       conductor cells and never gets anywhere. */
    i16 dx, dy;
    i16 life;
    bool used;
};

extern Spark g_sparks[MAX_SPARKS];

int  sparkCount();
void sparkClear();

/* Inject a spark at a cell, heading (dx, dy). Fails quietly if the array is
   full -- a dropped spark is a missed tick, which is far better than a machine
   that stops working because something unrelated filled the array. */
bool sparkAdd(int x, int y, int dx, int dy);

/* Draw live sparks over the world. Never shaded by the light field: a spark is
   its own light, and one crawling along a wire in an unlit cave is the single
   most useful thing to be able to see. */
void sparkDraw(u32* px, int camX, int camY);

struct DeviceInfo {
    const char* name;
    /* What the adjustable number MEANS, for the panel. Every device has exactly
       one, which is a deliberate restriction: a machine with six settings is a
       machine nobody will understand from looking at it, and one number keeps the
       interaction to "read it, nudge it". */
    const char* valueLabel;
    const char* valueUnit;
    /* vMin == vMax means "nothing to adjust", and the panel hides its -/+ for such
       a device rather than showing two buttons that do nothing. */
    i32 vMin, vMax, vStep, vDefault;
    u8  sprite;             /* a SpriteId; see the note on DEV_W */

    /* What the device's cells are made of in the grid. MAT_DEVICE for machinery,
       but the torch uses MAT_TORCH -- which is already passable, already emits
       light, and already exists -- so "a device you can walk through" needs no new
       concept at all, just the freedom to say which material the footprint is
       written in. The alternative was a passability flag on Device plus a matching
       exception in playerSolid, which is two places to keep in agreement about one
       fact the material table already knows. */
    u8  cellMat;

    /* Whether this device can be aimed. Only the placer and miner care: they ACT on
       the cells just outside one edge, and which edge should be the player's
       choice. A thermocouple senses its own cells and a clock counts, so a facing
       on either would be a control that does nothing. */
    bool aimable;
};

extern const DeviceInfo DEVS[DEV_COUNT];

struct Device {
    u8  type;
    i32 x, y;        /* top-left cell */
    i32 value;       /* the setpoint. Meaning is per type; see DeviceInfo */

    /* --- what it is doing ------------------------------------------------
       `firing` is this frame's output, recomputed by devTick. `latched` is the
       memory that makes it an EDGE rather than a level: a thermocouple over its
       mark should announce the crossing once, not every frame for as long as the
       furnace stays hot. Without it the device is a thermostat rather than a
       trigger, and sequencing anything off it becomes impossible. */
    bool firing;
    /* A spark arrived this frame. The INPUT side, kept separate from `firing`
       which is the output -- one device can be both fed and firing, and conflating
       them would make a chain of machines impossible to reason about. Set by
       sparkStep, consumed by devTick in the same frame. */
    bool poked;
    bool latched;
    /* Running total of sparks that have arrived. Shown on the panel, and it is
       the only way to tell "the wire is not connected" from "the machine is
       ignoring me" -- which is the first question anyone asks of a contraption
       that is not working. `poked` cannot answer it: it is set and consumed inside
       one devTick, so from outside it is never observably true. */
    i32  received;

    /* --- the buffer, for the placer and the miner ------------------------
       One material at a time, not a general inventory. A machine that could hold
       a mixture would need a slot list on its panel and a rule for which slot a
       pulse draws from, and neither earns its complexity: a placer places one
       thing, and a miner biting into layered ground wants to tell you it has hit
       something different rather than silently blending it.

       `mat` is meaningless while `count` is zero, which is what lets a placer
       accept whatever it is first fed. */
    u8   mat;
    i32  count;

    /* Which way an aimable device acts: 0 down, 1 up, 2 left, 3 right. Down by
       default because that is what a hopper and a drill both want, and because
       gravity gives "down" a meaning no other direction has. */
    u8   face;
    i32  phase;      /* the clock's counter; unused by other types */
    i32  reading;    /* last sensed value, for the panel to show */

    bool used;
};

extern Device g_devices[MAX_DEVICES];

/* Where a footprint lands for a click at (cx, cy): centred on the click, so the
   machine appears under the cursor rather than down and to the right of it. */
static inline int devOriginX(int cx) { return cx - DEV_W / 2; }
static inline int devOriginY(int cy) { return cy - DEV_H / 2; }

int  devCount();
void devClear();

/* The device covering a world cell, or 0. A linear scan, which is fine because
   every caller is a single user action -- a click, a placement check -- and never
   a per-cell loop. */
Device* devAt(int cx, int cy);

/* Place a device of `type` with its footprint centred on (cx, cy). Fails and
   returns false if it would overlap another device, if anything solid is in the
   way, or if the list is full. On success the footprint becomes MAT_DEVICE. */
bool devPlace(World& w, u8 type, int cx, int cy);

/* Take one away, clearing its cells back to empty. */
void devRemove(World& w, Device* d);

/* The cell just outside the footprint, `i` cells along the working edge, in the
   direction the device faces. DEV_W == DEV_H so one index covers every side. */
void devFaceCell(const Device& d, int i, int* ox, int* oy);

/* One frame for every device: sense, decide, and drop any whose cells have been
   dug out from under it. */
void devTick(World& w);

/* Draw every device that falls in the view, over the top of the world. `lit`
   shades them by the light field, on the same contract as Player::draw. */
void devDraw(const World& w, u32* px, int camX, int camY, bool lit);
