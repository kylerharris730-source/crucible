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
    /* Item logistics.  Every part uses the same 14-cell footprint, so pipes
       meet cleanly at a shared edge and can be laid out as a visible grid. */
    DEV_PIPE,
    DEV_CROSSOVER,
    DEV_CHEST,
    DEV_SPOUT,
    DEV_DRAIN,
    DEV_BLOCK_WATCHER,
    DEV_PULSE_BUTTON,
    /* Circuit devices are deliberately separate from spark electricity: they
       transform named values on a wire network, never heat metal or emit a
       physical pulse. Appending preserves numeric ids in existing saves. */
    DEV_CONSTANT_COMBINATOR,
    DEV_ARITHMETIC_COMBINATOR,
    DEV_DECIDER_COMBINATOR,
    /* --- the workbench ----------------------------------------------------
       A crafting STATION as a device, which is new only in that no station was
       one before: they were single cells, and a workbench you could lose track
       of behind a grain of dirt is not a piece of furniture, it is a pixel with
       a special meaning.

       It needs nothing from the device machinery except the footprint. It has
       no setpoint, no tick, no ports and no facing -- devTick's switch simply
       never names it. What it uses is `cellMat`, which already existed for the
       torch: its fourteen-by-fourteen block is written in MAT_STATION_BENCH, so
       craftScanStations goes on finding it by looking for that material and
       does not learn about devices at all. Every other part of the system --
       overlap rejection on placement, devIntact noticing you mined a corner,
       devRemove clearing the footprint -- already reads cellMat rather than
       assuming MAT_DEVICE, so this is a table row rather than a feature.

       Appended, like the combinators above and for the same reason: a Device's
       type is a raw number in the save. */
    DEV_WORKBENCH,
    /* The rest of the station ladder, for the same reason and by the same
       mechanism: each writes its own MAT_STATION_* into its footprint, so
       craftScanStations goes on reading g_matStation off the grid and none of
       the crafting code learns that stations became objects.

       Every one of these is a table row and a sprite. That is the whole cost,
       and it is what DeviceInfo::cellMat bought when the torch needed to be
       made of something other than machinery. */
    DEV_ANVIL,
    DEV_CHEM,
    DEV_ASSEMBLY,
    DEV_FORGE,
    /* A bed is furniture with one state owned by the player, not a machine:
       right-clicking it starts resting, which advances only the day clock. */
    DEV_BED,
    /* --- the pedestal -----------------------------------------------------
       One item, standing on a plinth, lit. It is the only object in the game
       whose job is to be SEEN from a long way off, and that is the whole
       design: a chamber you can see something in is a chamber you decide to
       walk into, which is the thing exploration was missing. Ore in a wall
       rewards you for having gone somewhere; a pedestal is a reason to go.

       What it holds lives in the fields the machines already have -- `value`
       carries the ItemId and `count` the number -- rather than in two new ones,
       and that is not thrift, it is the save format. Device is written as a raw
       sized block (see save.cpp), so widening the struct makes every existing
       world's machines fail their length check and vanish: a feature that
       deletes somebody's factory is not worth two fields. `value` is an i32 and
       an ItemId is a u16, so it fits with room to spare, and a pedestal has no
       setpoint for it to collide with.

       Appended, like every device before it, because a Device's type is a raw
       number in that same save. */
    DEV_PEDESTAL,
    /* Makes bees, and turns what they bring back into wax and honey.
       Appended: a device type is written into every save. */
    DEV_HIVE,
    DEV_COUNT
};

/* Kept just above a torch in the rebalanced producer ladder: the displayed
   object should still call attention to itself without lighting the biome. */
static const int PEDESTAL_LIGHT = 123;

/* Circuit signals use material ids directly, so an item sensor can say
   "Copper: 40" without a second translation table. The nine generic channels
   live immediately after materials and are for temperatures, counters, and
   other numbers that are not items. */
enum CircuitSignal {
    CIR_SIG_1 = MAT_COUNT,
    CIR_SIG_2,
    CIR_SIG_3,
    CIR_SIG_4,
    CIR_SIG_5,
    CIR_SIG_6,
    CIR_SIG_7,
    CIR_SIG_8,
    CIR_SIG_9,
    CIRCUIT_SIGNAL_COUNT
};

enum CircuitOp {
    CIR_OP_ADD = 0,
    CIR_OP_SUBTRACT,
    CIR_OP_MULTIPLY,
    CIR_OP_DIVIDE,
    CIR_OP_MODULO,
    CIR_OP_GREATER,
    CIR_OP_LESS,
    CIR_OP_EQUAL,
    CIR_OP_GREATER_EQUAL,
    CIR_OP_LESS_EQUAL,
    CIR_OP_NOT_EQUAL,
    CIR_OP_COUNT
};

/* A wire is pure topology. It has endpoints but no cell footprint, material
   cost, or simulation work while nothing is reading it. */
static const int MAX_CIRCUIT_WIRES = 256;
static const int CIRCUIT_PORTS = 2;
/* 0 = input/ordinary terminal, 1 = combinator output terminal. */
struct CircuitWire { u8 a, b, portA, portB; bool used; };
struct CircuitDeviceConfig {
    u8 signal;       /* source/constant output; thermocouple defaults to 1 */
    u8 signalA, signalB, signalOut;
    u8 op;
};

/* How much either machine can hold. Generous enough that a placer does not need
   babysitting during a smelt, small enough that it is a buffer rather than a
   warehouse -- a machine that could hold 100000 like a player's stack would make
   the whole logistics question disappear. */
static const int DEV_CAP = 2000;
/* One chest slot is a normal material stack, matching the inventory UI. */
static const int CHEST_CAP = 100000;

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

   --- one pulse, many fronts ---
   A spark is one visible front of a PULSE. At a junction the front carries on and
   also sends a front down every perpendicular branch, so a wire network behaves
   like a short travelling wave rather than a single marble choosing a route.

   Every conductor cell keeps the id of the last pulse that reached it, and the
   direction that pulse was going. A pulse may enter a cell once, never twice.
   That gives forks their expected behaviour while making rings harmless: when
   fronts meet on the far side of a loop, the second one simply stops.

   A recent cell claimed by a DIFFERENT wave is refused too, unless that wave
   has finished or was heading the same way. "Recent" is important: it lets a
   pulse follow another down a wire once there is a visible gap, while still
   stopping pulses which cut across or run into each other. Without that local
   collision rule two waves meeting head-on passed straight through each other,
   each flooding the other's trail; in any wire thicker than a single cell the
   front count grew every frame until the wire cooked itself apart. See the note
   in sparkStep.

   The mark is outside Cell on purpose, so electricity still never dirties the
   material simulation. The fixed front array remains the backstop for a very
   large, wildly branching circuit. */

/* Cells a spark advances per frame. One, so a circuit is something you can watch
   work -- at 60 Hz a 100-cell run of wire takes under two seconds, which is fast
   enough to feel immediate and slow enough to debug by eye. */
static const int SPARK_SPEED = 1;

/* The cap contains pathological, repeatedly-pulsed metal blobs. 2048 leaves
   room for a dramatic overload to resolve itself thermally while still giving
   the per-frame device pass a fixed, small upper bound. */
static const int MAX_SPARKS = 2048;
/* A front which fails to leave a small local area for this many frames is a
   circulating remnant, not useful signal propagation. */
static const int SPARK_CYCLE_STEPS = 120;

struct Spark {
    i32 x, y;
    /* The step just taken. Two jobs: it is the direction to prefer next (a spark
       carries on rather than doubling back), and its negation is the one move
       that is forbidden. Without that a spark oscillates between two adjacent
       conductor cells and never gets anywhere. */
    i16 dx, dy;
    /* Shared by every front created when this pulse splits. */
    u16 pulse;
    /* A newly created branch waits until next frame, so each front always moves
       exactly SPARK_SPEED cells per simulation frame. */
    u32 stepped;
    /* Progress watchdog anchor. Normal wire travel moves away and resets it;
       a tiny post-explosion loop accumulates frames and fizzles. */
    i16 cycleX, cycleY;
    u8  cycleSteps;
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

/* --- what an unterminated wire sheds ---------------------------------------

   A pulse reaching the open end of a wire has nowhere to go, and until now it
   simply stopped (with a little heat). Now it SPITS: a bright mote drops out of
   the cut end and falls.

   In open air that is decoration -- it drops, dims and is gone. What makes it
   worth having is what happens when it lands on another exposed conductor,
   which is that it energises it. So a tidy circuit is unaffected, and a bundle
   of bare wire ends over a bundle of bare wire is a machine that sets itself
   off. The consequence is entirely emergent from where you left your cables.

   --- why this is a PARTICLE and not a material ---

   The obvious reading of "a new spark material" is a MatId that falls like sand
   and reacts with wire on contact, and that is the one thing the architecture
   here will not buy cheaply. world.cpp knows nothing about electricity -- no
   devices, no doors, no sparks, not one reference -- and that purity is load
   bearing: the falling-sand rules are a closed system that can be reasoned
   about and tested without any of this file existing. A material whose rule is
   "energise the wire I touched" would put the first crack in that.

   So it lives here, next to the thing that creates it and the thing it acts on,
   and it is an overlay for the same reason the player and projectiles are: it
   moves in a way cells cannot, and it needs to be somewhere the grid is not.
   See the note at the top of projectile.h, which is the same argument. */
struct ShedSpark {
    float x, y;      /* cells, with a fractional part -- it falls smoothly */
    float vx, vy;
    i32   life;      /* frames remaining */
    bool  used;
};

/* Small, and the cap is the whole safety story. A conductive blob can terminate
   dozens of fronts a pulse and every one of them sheds, so this is what stops a
   badly-built circuit from filling the screen with motes. Dropping the excess is
   right: the interesting event is that a wire end spits, not that every single
   one did on a particular frame. */
static const int MAX_SHED = 96;
extern ShedSpark g_shed[MAX_SHED];

int  shedCount();
void shedClear();
/* Put down or capture the loose, falling mote shed by an open wire end. This is
   deliberately distinct from sparkAdd(), which creates current inside wire. */
bool shedPlace(int x, int y);
bool shedTakeNear(int x, int y, int radius);
void shedDraw(u32* px, int camX, int camY);

/* How far a spout pointed up will shove its own output to make room -- the
   pump's HEAD, in cells. Past this the lift fails and the spout skips that
   cell, so a column taller than this stalls exactly the way a real pump does
   and the answer is another spout further up.

   Sixty-four is chosen against the cost, not against the fiction: the lift
   only runs when the face cell is already blocked, which is the working case
   for a pump, and a fourteen-wide face can therefore move up to 14 * 64 cells
   in a pulse. See tests/spout_lift.cpp, which measures it. */
static const int DEV_SPOUT_LIFT = 64;

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
    /* Spouts and drains retain this state between pulses. A signal toggles it,
       rather than merely running them for a single frame, so a clock or watcher
       can genuinely turn a line on and off. */
    bool enabled;
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
    /* A crossover has two physically isolated lanes: lane 0 is north/south,
       lane 1 east/west.  Ordinary logistics parts only use lane 0. */
    u8   mat2;
    i32  count2;
    /* The neighbour that last fed this logistics part.  A pipe never offers
       that same packet straight back, which gives a simple local network a
       direction without a global graph solve. */
    i16  pipeFrom;

    /* Which way an aimable device acts: 0 down, 1 up, 2 left, 3 right. Down by
       default because that is what a hopper and a drill both want, and because
       gravity gives "down" a meaning no other direction has. */
    u8   face;
    i32  phase;      /* the clock's counter; unused by other types */
    i32  reading;    /* last sensed value, for the panel to show */

    bool used;
};

/* What a pedestal is holding, and putting something on it. Accessors rather
   than reaching into `value` and `count` at the call sites, so the one place
   that knows about the field aliasing is here -- a caller that read `d.value`
   directly would break silently if a pedestal ever grew a setpoint.

   The item is a bare u16 rather than an ItemId, and that is the include
   direction rather than sloppiness: item.h includes THIS file, so nothing here
   can name a type item.h defines. ItemId is a u16 and has to stay one for the
   wire format anyway (see Blob::itemf), so the two are the same declaration
   written from the side of the fence that can see it. */
/* --- the hive -------------------------------------------------------
   A bee that has reached its hive with a load hands it over here. The
   hive does not act on it immediately: it queues, and devTick extrudes
   at its own pace, so five bees arriving together produce five cells of
   output over the next few seconds rather than one frame of five.

   Declared in the header because entity.cpp calls it -- the bee knows it
   has arrived, and the hive knows what arriving means. */
/* The world is needed because the door MOVES when it has to -- see the
   note on hiveTarget in device.cpp. */
void hiveTarget(const World& w, const Device& d, float* x, float* y);
void hiveDeliver(Device& d, bool coal);

u16  pedestalItem(const Device& d);
int  pedestalCount(const Device& d);
void pedestalSet(Device& d, u16 item, int count);

extern Device g_devices[MAX_DEVICES];
extern CircuitWire g_circuitWires[MAX_CIRCUIT_WIRES];
extern CircuitDeviceConfig g_circuitConfig[MAX_DEVICES];
/* While a chest inventory is open its stack is being edited by the UI, so pipe
   transfer pauses rather than racing an invisible second copy of that stack. */
extern bool g_logisticsUiOpen;

/* A torch has no machine state, ports, buffers, or tick. Keep those decorative
   fixtures out of the deliberately small machine table: a long tunnel should
   never run out of automation slots just because it is well lit. */
struct TorchFixture { i32 x, y; };
int  torchCount();
const TorchFixture* torchData();
void torchLoad(const TorchFixture* fixtures, int count);
void torchAdd(i32 x, i32 y);
int  torchAt(int cx, int cy);       /* fixture index, or -1 */
bool torchRemoveAt(int index);

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

/* Torches are visual fixtures rather than physical cells, so fluids can share
   their footprint. Their light is registered with the light pass. */
void devRegisterLights();

/* Take one away, clearing its cells back to empty. */
void devRemove(World& w, Device* d);

/* The cell just outside the footprint, `i` cells along the working edge, in the
   direction the device faces. DEV_W == DEV_H so one index covers every side. */
void devFaceCell(const Device& d, int i, int* ox, int* oy);

/* --- the working box, for miners and placers -------------------------------
   `i` runs along the face as above; `layer` runs AWAY from it, zero being the
   row devFaceCell returns. So a miner facing down with depth 4 covers the
   14x4 block directly beneath itself.

   Depth rather than a free rectangle because the device is 14 wide and always
   works off one edge: a box that could also be narrower than the face would
   need an offset as well as a size, which is two more numbers on a panel whose
   whole design is "read it, nudge it" (see DeviceInfo::valueLabel). Fourteen
   by depth is the shape that clears a furnace, which is what these are for. */
void devBoxCell(const Device& d, int i, int layer, int* ox, int* oy);

/* --- where a miner/placer keeps the rest of its settings -------------------
   All three live in fields those two types do not otherwise use, because
   `Device` is written to the save as one raw sized block: widening the struct
   makes every existing world's machines fail their length check and vanish.
   The same trick the pedestal uses for its item.

     mat2      box depth, 1..DEV_W. ZERO means one row, which is what every
               device placed before this existed has -- so an old save keeps
               exactly the single-row behaviour it had.
     pipeFrom  the material filter, or -1 for "take anything". devPlace already
               initialises it to -1, so the default is right for free.
     count2    the trigger mode; see DevRunMode.

   Accessors rather than raw field access at the call sites, so the one place
   that knows about the aliasing is here. */
enum DevRunMode {
    /* Acts on every tick the signal is non-zero. The Factorio default: hold a
       constant 1 on the wire and it runs continuously. */
    DEVRUN_WHILE_ON = 0,
    /* Acts once per RISING EDGE. A clock ticking 1/0/1/0 then gives exactly one
       action per tick rather than one per frame the wire happens to be high. */
    DEVRUN_ON_EDGE,
    DEVRUN_COUNT
};

int  devBoxDepth(const Device& d);
void devSetBoxDepth(Device& d, int depth);
int  devFilterMat(const Device& d);          /* MAT_EMPTY for "anything" */
void devSetFilterMat(Device& d, int mat);
int  devRunMode(const Device& d);
void devSetRunMode(Device& d, int mode);
const char* devRunModeName(int mode);

/* One frame for every device: sense, decide, and drop any whose cells have been
   dug out from under it. */
void devTick(World& w);

/* Draw every device that falls in the view, over the top of the world. `lit`
   shades them by the light field, on the same contract as Player::draw. */
void devDraw(const World& w, u32* px, int camX, int camY, bool lit);

/* --- circuit network ------------------------------------------------------ */
void circuitClear();
void circuitRemoveDevice(int index);
/* Adds or removes a visible wire. Returns false for invalid/self links. */
bool circuitToggleWire(int a, int b);
bool circuitToggleWirePorts(int a, int portA, int b, int portB);
bool circuitHasWire(int a, int b);
bool circuitIsCombinator(u8 type);
bool circuitHasSeparatePorts(u8 type);
int  circuitWireCount();
const char* circuitSignalName(int signal);
const char* circuitOpName(int op);
int  circuitInput(int deviceIndex, int signal);
int  circuitInputPort(int deviceIndex, int port, int signal);
void circuitSetOutput(int deviceIndex, int signal, int value);
void circuitPrepareInputs();
void circuitDraw(u32* px, int camX, int camY, bool lit, int selectedDevice, int selectedPort);
void circuitRemapMaterials(const u8* remap, int savedMatCount);
/* Applies sensible defaults to machines loaded from a pre-circuit save. */
void circuitInitMissingConfigs();
