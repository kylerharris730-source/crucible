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
    DEV_COUNT
};

struct DeviceInfo {
    const char* name;
    /* What the adjustable number MEANS, for the panel. Every device has exactly
       one, which is a deliberate restriction: a machine with six settings is a
       machine nobody will understand from looking at it, and one number keeps the
       interaction to "read it, nudge it". */
    const char* valueLabel;
    const char* valueUnit;
    i32 vMin, vMax, vStep, vDefault;
    u8  sprite;             /* a SpriteId; see the note on DEV_W */
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
    bool latched;
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

/* One frame for every device: sense, decide, and drop any whose cells have been
   dug out from under it. */
void devTick(World& w);

/* Draw every device that falls in the view, over the top of the world. `lit`
   shades them by the light field, on the same contract as Player::draw. */
void devDraw(const World& w, u32* px, int camX, int camY, bool lit);
