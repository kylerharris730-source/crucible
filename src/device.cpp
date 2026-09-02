#include "device.h"
#include "door.h"
#include "sprite.h"
#include "item.h"      /* ITEMS[], for what a pedestal is holding */
#include "entity.h"    /* g_entities and entSpawn, for the hive's bees */
#include "light.h"     /* isNight(), so the hive keeps daylight hours */
#include "projectile.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>   /* fabsf, for the shed spark's fall */
#include <vector>

Device g_devices[MAX_DEVICES];
static std::vector<TorchFixture> g_torches;

Spark g_sparks[MAX_SPARKS];

CircuitWire g_circuitWires[MAX_CIRCUIT_WIRES];
CircuitDeviceConfig g_circuitConfig[MAX_DEVICES];
/* Values are rebuilt once a frame from the previous frame's emitters. That
   one-tick boundary is intentional: it makes a feedback loop a clockable
   circuit rather than an order-dependent accident, and is still far faster
   than a full graph solve for every combinator. */
static i32 g_circuitInput[MAX_DEVICES][CIRCUIT_PORTS][CIRCUIT_SIGNAL_COUNT];
static i32 g_circuitOutput[MAX_DEVICES][CIRCUIT_SIGNAL_COUNT];
static i32 g_circuitBus[MAX_DEVICES * CIRCUIT_PORTS][CIRCUIT_SIGNAL_COUNT];
static int g_circuitParent[MAX_DEVICES * CIRCUIT_PORTS];
static bool g_circuitConnected[MAX_DEVICES * CIRCUIT_PORTS];

static int circuitRoot(int a) {
    while (g_circuitParent[a] != a) {
        g_circuitParent[a] = g_circuitParent[g_circuitParent[a]];
        a = g_circuitParent[a];
    }
    return a;
}

bool circuitIsCombinator(u8 type) {
    return type == DEV_CONSTANT_COMBINATOR || type == DEV_ARITHMETIC_COMBINATOR ||
           type == DEV_DECIDER_COMBINATOR;
}

bool circuitHasSeparatePorts(u8 type) {
    return type == DEV_ARITHMETIC_COMBINATOR || type == DEV_DECIDER_COMBINATOR;
}

static int circuitNode(int device, int port) {
    return device * CIRCUIT_PORTS + (circuitHasSeparatePorts(g_devices[device].type) ? port : 0);
}

static void circuitJoin(int a, int b) {
    a = circuitRoot(a); b = circuitRoot(b);
    if (a != b) g_circuitParent[b] = a;
}

static void circuitInitDevice(int index, u8 type) {
    CircuitDeviceConfig& c = g_circuitConfig[index];
    c.signal = CIR_SIG_1;
    c.signalA = CIR_SIG_1;
    c.signalB = CIR_SIG_2;
    c.signalOut = CIR_SIG_3;
    c.op = type == DEV_DECIDER_COMBINATOR ? CIR_OP_GREATER : CIR_OP_ADD;
    memset(g_circuitInput[index], 0, sizeof(g_circuitInput[index]));
    memset(g_circuitOutput[index], 0, sizeof(g_circuitOutput[index]));
}

void circuitClear() {
    memset(g_circuitWires, 0, sizeof(g_circuitWires));
    memset(g_circuitConfig, 0, sizeof(g_circuitConfig));
    memset(g_circuitInput, 0, sizeof(g_circuitInput));
    memset(g_circuitOutput, 0, sizeof(g_circuitOutput));
}

void circuitInitMissingConfigs() {
    for (int i = 0; i < MAX_DEVICES; ++i)
        if (g_devices[i].used && g_circuitConfig[i].signal == MAT_EMPTY)
            circuitInitDevice(i, g_devices[i].type);
}

void circuitRemoveDevice(int index) {
    if (index < 0 || index >= MAX_DEVICES) return;
    for (int i = 0; i < MAX_CIRCUIT_WIRES; ++i)
        if (g_circuitWires[i].used && (g_circuitWires[i].a == index || g_circuitWires[i].b == index))
            g_circuitWires[i].used = false;
    memset(&g_circuitConfig[index], 0, sizeof(g_circuitConfig[index]));
    memset(g_circuitInput[index], 0, sizeof(g_circuitInput[index]));
    memset(g_circuitOutput[index], 0, sizeof(g_circuitOutput[index]));
}

bool circuitHasWire(int a, int b) {
    for (int i = 0; i < MAX_CIRCUIT_WIRES; ++i) {
        const CircuitWire& w = g_circuitWires[i];
        if (!w.used) continue;
        if ((w.a == a && w.b == b) || (w.a == b && w.b == a)) return true;
    }
    return false;
}

bool circuitToggleWirePorts(int a, int portA, int b, int portB) {
    if (a < 0 || a >= MAX_DEVICES || b < 0 || b >= MAX_DEVICES ||
        portA < 0 || portA >= CIRCUIT_PORTS || portB < 0 || portB >= CIRCUIT_PORTS ||
        (!circuitHasSeparatePorts(g_devices[a].type) && portA != 0) ||
        (!circuitHasSeparatePorts(g_devices[b].type) && portB != 0) ||
        (a == b && portA == portB) || !g_devices[a].used || !g_devices[b].used) return false;
    for (int i = 0; i < MAX_CIRCUIT_WIRES; ++i) {
        CircuitWire& w = g_circuitWires[i];
        if (!w.used) continue;
        if ((w.a == a && w.portA == portA && w.b == b && w.portB == portB) ||
            (w.a == b && w.portA == portB && w.b == a && w.portB == portA)) { w.used = false; return true; }
    }
    for (int i = 0; i < MAX_CIRCUIT_WIRES; ++i) if (!g_circuitWires[i].used) {
        g_circuitWires[i].a = (u8)a; g_circuitWires[i].b = (u8)b;
        g_circuitWires[i].portA = (u8)portA; g_circuitWires[i].portB = (u8)portB;
        g_circuitWires[i].used = true;
        return true;
    }
    return false;
}

bool circuitToggleWire(int a, int b) { return circuitToggleWirePorts(a, 0, b, 0); }

int circuitWireCount() {
    int n = 0;
    for (int i = 0; i < MAX_CIRCUIT_WIRES; ++i) if (g_circuitWires[i].used) ++n;
    return n;
}

const char* circuitSignalName(int signal) {
    static const char* const NUMBERS[9] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };
    if (signal > MAT_EMPTY && signal < MAT_COUNT) return MATS[signal].name;
    if (signal >= CIR_SIG_1 && signal <= CIR_SIG_9) return NUMBERS[signal - CIR_SIG_1];
    return "none";
}

const char* circuitOpName(int op) {
    static const char* const OPS[CIR_OP_COUNT] = { "+", "-", "*", "/", "%", ">", "<", "=", ">=", "<=", "!=" };
    return op >= 0 && op < CIR_OP_COUNT ? OPS[op] : "?";
}

int circuitInput(int deviceIndex, int signal) {
    return circuitInputPort(deviceIndex, 0, signal);
}

int circuitInputPort(int deviceIndex, int port, int signal) {
    if (deviceIndex < 0 || deviceIndex >= MAX_DEVICES || signal < 0 || signal >= CIRCUIT_SIGNAL_COUNT) return 0;
    if (port < 0 || port >= CIRCUIT_PORTS || (!circuitHasSeparatePorts(g_devices[deviceIndex].type) && port != 0)) return 0;
    return g_circuitInput[deviceIndex][port][signal];
}

void circuitSetOutput(int deviceIndex, int signal, int value) {
    if (deviceIndex < 0 || deviceIndex >= MAX_DEVICES || signal < 0 || signal >= CIRCUIT_SIGNAL_COUNT) return;
    g_circuitOutput[deviceIndex][signal] = value;
}

void circuitPrepareInputs() {
    for (int i = 0; i < MAX_DEVICES * CIRCUIT_PORTS; ++i) {
        g_circuitParent[i] = i;
        g_circuitConnected[i] = false;
        memset(g_circuitBus[i], 0, sizeof(g_circuitBus[i]));
    }
    for (int i = 0; i < MAX_DEVICES; ++i) {
        memset(g_circuitInput[i], 0, sizeof(g_circuitInput[i]));
        if (g_devices[i].used && !circuitHasSeparatePorts(g_devices[i].type))
            circuitJoin(i * CIRCUIT_PORTS, i * CIRCUIT_PORTS + 1);
    }
    for (int i = 0; i < MAX_CIRCUIT_WIRES; ++i) {
        CircuitWire& w = g_circuitWires[i];
        if (!w.used) continue;
        if (w.a >= MAX_DEVICES || w.b >= MAX_DEVICES || w.portA >= CIRCUIT_PORTS || w.portB >= CIRCUIT_PORTS ||
            !g_devices[w.a].used || !g_devices[w.b].used ||
            (!circuitHasSeparatePorts(g_devices[w.a].type) && w.portA != 0) ||
            (!circuitHasSeparatePorts(g_devices[w.b].type) && w.portB != 0)) {
            w.used = false; continue;
        }
        circuitJoin(circuitNode(w.a, w.portA), circuitNode(w.b, w.portB));
    }
    for (int i = 0; i < MAX_CIRCUIT_WIRES; ++i) if (g_circuitWires[i].used) {
        g_circuitConnected[circuitRoot(circuitNode(g_circuitWires[i].a, g_circuitWires[i].portA))] = true;
    }
    for (int i = 0; i < MAX_DEVICES; ++i) if (g_devices[i].used) {
        const int root = circuitRoot(circuitNode(i, circuitHasSeparatePorts(g_devices[i].type) ? 1 : 0));
        for (int s = 0; s < CIRCUIT_SIGNAL_COUNT; ++s) g_circuitBus[root][s] += g_circuitOutput[i][s];
    }
    for (int i = 0; i < MAX_DEVICES; ++i) if (g_devices[i].used) {
        for (int p = 0; p < CIRCUIT_PORTS; ++p) {
            if (!circuitHasSeparatePorts(g_devices[i].type) && p != 0) continue;
            const int root = circuitRoot(circuitNode(i, p));
            if (g_circuitConnected[root])
                memcpy(g_circuitInput[i][p], g_circuitBus[root], sizeof(g_circuitInput[i][p]));
        }
    }
    memset(g_circuitOutput, 0, sizeof(g_circuitOutput));
}

void circuitRemapMaterials(const u8* remap, int savedMatCount) {
    if (!remap || savedMatCount <= 0) return;
    const int shift = MAT_COUNT - savedMatCount;
    for (int i = 0; i < MAX_DEVICES; ++i) {
        CircuitDeviceConfig& c = g_circuitConfig[i];
        u8* signals[4] = { &c.signal, &c.signalA, &c.signalB, &c.signalOut };
        for (int s = 0; s < 4; ++s) {
            const u8 old = *signals[s];
            if (old < savedMatCount) *signals[s] = remap[old];
            /* Numbered circuit channels begin immediately after materials.
               When a material is appended, their raw IDs move with that
               boundary; treating old Sig 1 as the new material was what made
               loaded circuits emit nonsense. */
            else if (old < savedMatCount + 9) *signals[s] = (u8)(old + shift);
        }
    }
}

/* --- the pulse stamp, allocated per block ----------------------------------

   A direct pulse stamp is deliberately separate from Cell: it makes a branch
   test one read/write instead of an allocation-heavy set lookup on every wire
   step. That reasoning is unchanged. What changed is the price.

   It was one flat u16 per world cell, and the comment here said that cost
   24 MiB "for this 4096 x 3072 world". The world is 9216 deep now -- it grew
   twice, for pacing -- so the plane had quietly become 72 MB, three times the
   figure it was justified by, and with g_pulseRecent beside it accounted for
   81 MB of a 347 MB process. Measured against what it holds: a world can be
   played for hours with a few hundred wire cells in it, and the plane reserves
   a slot for every one of thirty-seven million.

   So it is allocated in 32x32 blocks, on demand, and only where a pulse has
   actually been. The block table is 36,864 pointers -- 144 KB -- and a block
   is 2 KB, so a circuit-heavy world costs a few hundred KB where it used to
   cost 72 MB flat.

   Reads do NOT allocate: an absent block reads as zero, which is exactly the
   unvisited stamp it would have held anyway. Writes allocate, except writes of
   zero to an absent block, which have nothing to record.

   Access is still O(1) and still branch-light. SIM_W is a power of two, so a
   linear cell index splits into block and offset with shifts and masks and no
   division. If anything the locality improves: pulses cluster on the few
   blocks a circuit occupies, where the flat plane scattered them across 72 MB
   of address space that was almost entirely zero. */
static const int SIM_W_SHIFT = 12;
#if defined(__cplusplus) && __cplusplus >= 201103L
static_assert((1 << SIM_W_SHIFT) == SIM_W,
              "SIM_W must stay a power of two, or pulse indexing needs a divide");
#endif

static const int PULSE_BLOCK_SHIFT = CHUNK_SHIFT;                  /* 32x32   */
static const int PULSE_BLOCK       = 1 << PULSE_BLOCK_SHIFT;
static const int PULSE_BLOCK_MASK  = PULSE_BLOCK - 1;
static const int PULSE_BLOCK_CELLS = PULSE_BLOCK * PULSE_BLOCK;    /* 1024    */
static const int PULSE_BLOCKS_X    = SIM_W >> PULSE_BLOCK_SHIFT;
static const int PULSE_BLOCKS_Y    = SIM_H >> PULSE_BLOCK_SHIFT;
static const int PULSE_BLOCK_COUNT = PULSE_BLOCKS_X * PULSE_BLOCKS_Y;

static u16* g_pulseMarkBlock[PULSE_BLOCK_COUNT];
static u8*  g_pulseRecentBlock[PULSE_BLOCK_COUNT];

static inline int pulseBlockOf(int cell) {
    const int x = cell & (SIM_W - 1);
    const int y = cell >> SIM_W_SHIFT;
    return (y >> PULSE_BLOCK_SHIFT) * PULSE_BLOCKS_X + (x >> PULSE_BLOCK_SHIFT);
}

static inline int pulseOffsetOf(int cell) {
    const int x = cell & (SIM_W - 1);
    const int y = cell >> SIM_W_SHIFT;
    return ((y & PULSE_BLOCK_MASK) << PULSE_BLOCK_SHIFT) | (x & PULSE_BLOCK_MASK);
}

static inline u16 pulseMarkAt(int cell) {
    const u16* b = g_pulseMarkBlock[pulseBlockOf(cell)];
    return b ? b[pulseOffsetOf(cell)] : (u16)0;
}

static inline void pulseMarkSet(int cell, u16 value) {
    const int bi = pulseBlockOf(cell);
    u16* b = g_pulseMarkBlock[bi];
    if (!b) {
        if (!value) return;         /* zero into an absent block is already true */
        b = (u16*)calloc(PULSE_BLOCK_CELLS, sizeof(u16));
        if (!b) return;             /* out of memory: the wire simply does not latch */
        g_pulseMarkBlock[bi] = b;
    }
    b[pulseOffsetOf(cell)] = value;
}

/* Freed rather than zeroed. A clear happens on world reset and on pulse-id
   wrap, both rare, and handing the memory back is the point of the exercise --
   a world that once had a big circuit in it should not keep paying for it. */
static void pulseBlocksRelease() {
    for (int i = 0; i < PULSE_BLOCK_COUNT; ++i) {
        free(g_pulseMarkBlock[i]);
        free(g_pulseRecentBlock[i]);
        g_pulseMarkBlock[i] = 0;
        g_pulseRecentBlock[i] = 0;
    }
}

static u16 g_nextPulse = 1;

/* A live pulse can be hundreds of cells away from the trail it left behind.
   Treating every one of those old cells as occupied is what made a fresh pulse
   disappear on thick wire: the sideways steps which fill the wire met stale,
   differently-directed claims and were mistaken for a crossing wave.

   Keep a short, exact collision wake instead. Three rotating slots avoid a
   frame-number wrap alias, and two packed bits per cell hold slot 1, 2, 3 or
   "not recent". A claim is recent for the frame it was made and the next two;
   after that it remains a same-pulse loop barrier but no longer blocks a
   separate pulse which is visibly behind it. */
/* Blocked for the same reason and on the same grid as g_pulseMark above, so a
   cell's two structures live in the same block index. Two bits per cell means
   a quarter the bytes: 256 per block against the mark plane's 2 KB. */
static const int SPARK_RECENT_SLOTS = 3;
static const int SPARK_RECENT_MAX = MAX_SPARKS * 5;
static int g_pulseRecentCells[SPARK_RECENT_SLOTS][SPARK_RECENT_MAX];
static int g_pulseRecentCount[SPARK_RECENT_SLOTS];
static int g_pulseRecentSlot = 0;

static inline u8 sparkRecentStamp(int cell) {
    const u8* b = g_pulseRecentBlock[pulseBlockOf(cell)];
    if (!b) return 0;
    const int o = pulseOffsetOf(cell);
    return (u8)((b[o >> 2] >> ((o & 3) * 2)) & 3u);
}

static inline void sparkSetRecentStamp(int cell, u8 stamp) {
    const int bi = pulseBlockOf(cell);
    u8* b = g_pulseRecentBlock[bi];
    if (!b) {
        if (!stamp) return;         /* clearing an absent block: already clear */
        b = (u8*)calloc(PULSE_BLOCK_CELLS / 4, 1);
        if (!b) return;
        g_pulseRecentBlock[bi] = b;
    }
    const int o = pulseOffsetOf(cell);
    u8& packed = b[o >> 2];
    const int shift = (o & 3) * 2;
    packed = (u8)((packed & ~(3u << shift)) | ((u32)stamp << shift));
}

static void sparkRecentBeginFrame() {
    g_pulseRecentSlot = (g_pulseRecentSlot + 1) % SPARK_RECENT_SLOTS;
    const u8 stamp = (u8)(g_pulseRecentSlot + 1);
    for (int i = 0; i < g_pulseRecentCount[g_pulseRecentSlot]; ++i) {
        const int cell = g_pulseRecentCells[g_pulseRecentSlot][i];
        if (sparkRecentStamp(cell) == stamp) sparkSetRecentStamp(cell, 0);
    }
    g_pulseRecentCount[g_pulseRecentSlot] = 0;
}

static void sparkMarkRecent(int cell) {
    const u8 stamp = (u8)(g_pulseRecentSlot + 1);
    if (sparkRecentStamp(cell) == stamp) return;
    int& count = g_pulseRecentCount[g_pulseRecentSlot];
    if (count >= SPARK_RECENT_MAX) return;
    g_pulseRecentCells[g_pulseRecentSlot][count++] = cell;
    sparkSetRecentStamp(cell, stamp);
}

/* --- how many fronts each pulse still has alive ----------------------------
   The mark above records WHICH wave last claimed a cell; this records whether
   that wave still exists. The recent-wake table above separately says whether
   it is still locally near that claim. All three are needed before a different
   wave can be refused.

   32 KB of counters indexed by pulse id, maintained at the one place a front is
   born and the one place it dies. Nothing ever scans it. */
static u16 g_pulseFronts[0x4000];

/* --- what a mark records ----------------------------------------------------
   A claim is a pulse id and the DIRECTION the front was travelling when it made
   the claim, packed into the one u16 the mark table already costs. Fourteen bits
   of id and two of direction; sparks only ever step along an axis, so four
   directions is all there is to record.

   The direction is what tells a wave meeting another wave HEAD-ON apart from a
   wave CATCHING one up, and those two want opposite answers. Head-on the two
   must stop -- that is the whole fix. But a second pulse chasing the first down
   the same wire is ordinary use, and a clock faster than the wire is long would
   otherwise have every pulse after the first swallowed at the terminal, which
   looks exactly like a clock that has stopped working. */
static const u16 PULSE_ID_MASK = 0x3FFF;
static const int PULSE_DIR_SHIFT = 14;

static inline int dirIndex(int dx, int dy) {
    if (dx > 0) return 0;
    if (dx < 0) return 1;
    return dy > 0 ? 2 : 3;
}

static u32 g_sparkFrame = 0;
static int g_nextSparkSlot = 0;
/* Kept sparse each tick through g_sparkTouched below: a runaway blob only
   touches the handful of chunks it actually occupies, never the whole world. */
static u16 g_sparkLoad[CHUNK_COUNT];
static int g_sparkHot[CHUNK_COUNT];
static int g_sparkTouched[MAX_SPARKS];

const DeviceInfo DEVS[DEV_COUNT] = {
    /* The thermocouple. Its setpoint is a temperature in DEGREES CELSIUS, not in
       the stored units the simulation compares -- the panel is the one place in
       the program a human reads a temperature, so it is the one place that should
       not be showing an offset byte. degC() converts on the way in.

       The range stops short of both ends of the scale on purpose. A setpoint at
       -40 C is unreachable-by-construction (see the sentinel note in
       materials.h) and one at 215 C could only ever be crossed by a heater
       running flat out, so neither is a useful thing to be able to dial in.
       Default 150 C sits above boiling water and below every melting point in the
       table, which makes it a sensible "the furnace is working" mark. */
    { "Thermocouple", "trips at", "C", -20, 210, 5, 150, SPR_THERMO, MAT_DEVICE, false },
    /* The clock. Its number is a PERIOD IN FRAMES, shown as frames rather than
       converted to seconds because everything else in this program is measured in
       frames at a fixed 60 Hz step -- a player timing a contraption is counting
       ticks, and a panel reading "0.75 s" would have to be converted back before
       it was useful.

       The floor of 6 frames is not arbitrary: a spark advances one cell a frame,
       so a period shorter than the wire is long puts several sparks on the same
       run at once. That is allowed and occasionally useful, but below about 6 it
       becomes a solid stream and the array cap starts doing the tuning instead of
       the player. */
    { "Clock", "every", "frames", 6, 600, 6, 60, SPR_CLOCK, MAT_DEVICE, false },
    /* Placer and miner. Their one number is CELLS PER PULSE, which is the right
       axis for both: it is the difference between a machine that trickles and one
       that empties itself in a few ticks, and it is the number you tune when a
       contraption is running at the wrong rate. 14 is the width of the footprint,
       so a pulse of 14 lays or lifts exactly one full row underneath. */
    { "Placer", "places", "cells", 1, 14, 1, 4, SPR_PLACER, MAT_DEVICE, true },
    { "Miner",  "mines",  "cells", 1, 14, 1, 4, SPR_MINER,  MAT_DEVICE, true },
    /* The torch. vMin == vMax, so it has nothing to adjust and the panel says so
       rather than offering two dead buttons. Its cells are MAT_TORCH, which is what
       makes it the one device you can walk through. */
    { "Torch", "", "", 0, 0, 0, 0, SPR_TORCH, MAT_TORCH, false },
    { "Item Pipe", "", "", 0, 0, 0, 0, SPR_PIPE, MAT_DEVICE, false },
    { "Pipe Crossover", "", "", 0, 0, 0, 0, SPR_CROSSOVER, MAT_DEVICE, false },
    { "Chest", "", "", 0, 0, 0, 0, SPR_CHEST, MAT_DEVICE, false },
    /* Rate defaults to the full width now: a spout should use the face it
       occupies unless it is deliberately turned down. See devSpout. */
    { "Spout", "rate", "cells", 1, 14, 1, 14, SPR_SPOUT, MAT_DEVICE, true },
    /* NOT aimable. A drain takes from all four edges, so a facing control on it
       was a button that changed nothing anybody could see. */
    { "Drain", "filter", "id", 0, MAT_COUNT - 1, 1, 0, SPR_DRAIN, MAT_DEVICE, false },
    { "Block Watcher", "filter", "id", 0, MAT_COUNT - 1, 1, 0, SPR_THERMO, MAT_DEVICE, true },
    { "Pulse Button", "", "", 0, 0, 0, 0, SPR_BUTTON, MAT_DEVICE, false },
    { "Constant Combinator", "value", "", -9999, 9999, 1, 1, SPR_CIRCUIT_CONSTANT, MAT_DEVICE, false },
    { "Arithmetic Combinator", "", "", 0, 0, 0, 0, SPR_CIRCUIT_ARITH, MAT_DEVICE, false },
    { "Decider Combinator", "", "", 0, 0, 0, 0, SPR_CIRCUIT_DECIDER, MAT_DEVICE, false },
    /* The workbench. The only row here whose cellMat is neither MAT_DEVICE nor
       a material invented for it: MAT_STATION_BENCH already existed and already
       meant "a bench is here" to craftScanStations, so writing the footprint in
       it is what lets a station become furniture without the crafting code
       hearing about devices at all. No setpoint, no facing, and devTick never
       names it -- it is a shape in the world that other code recognises. */
    { "Workbench", "", "", 0, 0, 0, 0, SPR_BENCH, MAT_STATION_BENCH, false },
    { "Anvil", "", "", 0, 0, 0, 0, SPR_ANVIL, MAT_STATION_ANVIL, false },
    { "Chemistry Bench", "", "", 0, 0, 0, 0, SPR_CHEMSTN, MAT_STATION_CHEM, false },
    { "Assembly Table", "", "", 0, 0, 0, 0, SPR_ASSEMBLY, MAT_STATION_ASSEMBLY, false },
    { "Blast Furnace", "", "", 0, 0, 0, 0, SPR_FORGESTN, MAT_STATION_FORGE, false },
    /* Platform cells make the bed furniture you can stand on or pass across,
       rather than a 14-cell wall. The art supplies its body and mattress. */
    { "Bed", "", "", 0, 0, 0, 0, SPR_BED, MAT_PLATFORM, false },
    /* Platform cells, like the bed, so a pedestal in a corridor is scenery you
       walk past rather than a 14-cell plug in the only way through. A reward
       that blocks the route it is rewarding you for taking would be a strange
       object. */
    { "Pedestal", "", "", 0, 0, 0, 0, SPR_PEDESTAL, MAT_PLATFORM, false },
    /* The setpoint is how many bees it keeps. Five is the default and the
       ceiling: past that they stop reading as individuals and the hive is
       just a cloud. Not aimable -- wax leaves through the top because wax
       is the lighter product, not because anyone pointed it there. */
    { "Hive", "bees", "", 1, 5, 1, 5, SPR_HIVE, MAT_DEVICE, false },
};

u16 pedestalItem(const Device& d) {
    if (d.type != DEV_PEDESTAL || d.count <= 0) return ITEM_NONE;
    /* Range-checked on the way OUT rather than trusted. `value` is a setpoint
       field on every other device, so a save from before pedestals existed, or
       a machine whose type was reused, can put any number in here -- and the
       consequence of believing it would be an index off the end of ITEMS[]. */
    if (d.value <= 0 || d.value >= ITEM_COUNT) return ITEM_NONE;
    return (u16)d.value;
}
int pedestalCount(const Device& d) {
    return d.type == DEV_PEDESTAL ? d.count : 0;
}
void pedestalSet(Device& d, u16 item, int count) {
    if (d.type != DEV_PEDESTAL) return;
    if (item == ITEM_NONE || count <= 0) { d.value = 0; d.count = 0; return; }
    d.value = (i32)item;
    d.count = count;
}

int sparkCount() {
    int n = 0;
    for (int i = 0; i < MAX_SPARKS; ++i) if (g_sparks[i].used) ++n;
    return n;
}

/* Defined further down with the rest of the mote code; declared here because
   sparkStep nominates one and sparkClear disposes of them, and both come
   first. */
static bool shedAdd(int x, int y, int dx, int dy);

void sparkClear() {
    for (int i = 0; i < MAX_SPARKS; ++i) g_sparks[i].used = false;
    /* The motes go too. They are shed BY this system and feed back into it, so
       leaving them alive across a clear would let a new world be energised by
       the last one's loose ends. */
    shedClear();
    pulseBlocksRelease();
    memset(g_pulseRecentCount, 0, sizeof(g_pulseRecentCount));
    memset(g_pulseFronts, 0, sizeof(g_pulseFronts));
    memset(g_sparkLoad, 0, sizeof(g_sparkLoad));
    g_nextPulse = 1;
    g_sparkFrame = 0;
    g_nextSparkSlot = 0;
    g_pulseRecentSlot = 0;
}

static u16 nextPulse() {
    /* Zero is the unvisited stamp. Before we would reuse an id, stop the tiny
       set of active fronts and begin a fresh generation; otherwise an old front
       could mistake a new mark for its own and re-enter a loop. */
    if (g_nextPulse >= PULSE_ID_MASK) sparkClear();
    return g_nextPulse++;
}

static bool sparkAddFront(int x, int y, int dx, int dy, u16 pulse) {
    /* Start from where the last allocation left off. Splitting through a dense
       conductor then stays O(number of new fronts), rather than repeatedly
       scanning the same packed prefix of a 2048-slot pool. */
    for (int n = 0, i = g_nextSparkSlot; n < MAX_SPARKS; ++n, ++i) {
        if (i == MAX_SPARKS) i = 0;
        Spark& s = g_sparks[i];
        if (s.used) continue;
        s.x = x; s.y = y;
        s.dx = (i16)dx; s.dy = (i16)dy;
        s.pulse = pulse;
        if (g_pulseFronts[pulse] != 0xFFFFu) ++g_pulseFronts[pulse];
        s.stepped = g_sparkFrame;
        s.cycleX = (i16)x; s.cycleY = (i16)y;
        s.cycleSteps = 0;
        s.used = true;
        g_nextSparkSlot = i + 1;
        if (g_nextSparkSlot == MAX_SPARKS) g_nextSparkSlot = 0;
        return true;
    }
    return false;
}

static inline bool conducts(const World& w, int x, int y) {
    if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) return false;
    return g_matConducts[w.at(x, y).mat] != 0;
}

/* Heat left behind where a spark is spat out. One pulse remains a poor furnace,
   but a jammed circuit deposits enough energy to melt or rupture its endpoint.
   That makes overloads a self-clearing consequence rather than a permanent
   exhaustion of the spark pool. */
static const int SPARK_HEAT = 4;
/* A normal lone spark merely warms its endpoint. A pileup that drives an arc
   close to the simulation's 215 C ceiling ruptures instead: the blast is strong
   enough to tear graphene at its centre, so a permanently overloaded graphene
   blob cleans itself up rather than monopolising the front pool forever. */
static const u8 SPARK_ARC_TEMP  = degC(212);
static const int SPARK_ARC_R    = 5;
static const int SPARK_ARC_POWER = 230;
/* A normal wire has only a few visible fronts. Forty-eight in one 32x32 chunk is
   the telltale "electric soup" made by a misshapen conductive blob. Above that
   point each extra front adds a small local arc contribution. */
static const int SPARK_CROWD_AT = 48;
static const int SPARK_CROWD_HEAT = 2;
static const int SPARK_CYCLE_RADIUS = 16;

static void sparkArcIfHot(World& w, int x, int y) {
    if (w.temp[y * SIM_W + x] >= SPARK_ARC_TEMP)
        explodeAt(w, x, y, SPARK_ARC_R, SPARK_ARC_POWER);
}

/* Move one wavefront one step. Returns false when it has finished, either by
   being spent or by running out of new conductor.

   Straight on first, then the two turns, and NEVER back the way it came.
   Preferring the current heading is what makes a wire behave as drawn; without it
   a spark at a bend picks by array order and can crawl backwards along a straight
   run.

   Reversal was allowed at first, as a "last resort" for backing out of a dead-end
   stub, and it made a straight wire into a resonator: the spark reached the end,
   found the cell behind it conductive, turned round, ran back, turned round again,
   and bounced forever. A wire END is not a dead end to escape from: it is where
   the spark is SPAT OUT. The special loop mode below is the only deliberate way
   a front may circulate. */
static bool sparkStep(World& w, Spark& s) {
    /* An arc explosion or a melting event may have removed the conductor below
       a front earlier this frame. It cannot continue through a cell that is no
       longer wire; ending here lets overload damage immediately shed its sparks. */
    if (!conducts(w, s.x, s.y)) return false;
    const int cdx = s.x - (int)s.cycleX, cdy = s.y - (int)s.cycleY;
    if (cdx * cdx + cdy * cdy <= SPARK_CYCLE_RADIUS * SPARK_CYCLE_RADIUS) {
        if (++s.cycleSteps >= SPARK_CYCLE_STEPS) return false;
    } else {
        s.cycleX = (i16)s.x; s.cycleY = (i16)s.y;
        s.cycleSteps = 0;
    }
    /* Candidate steps, in order of preference. */
    int cand[3][2];
    int n = 0;
    cand[n][0] = s.dx;  cand[n][1] = s.dy;  ++n;          /* straight on */
    /* The two perpendiculars. For an axis-aligned heading one of dx/dy is zero,
       so swapping them gives the turns. */
    cand[n][0] = -s.dy; cand[n][1] = -s.dx; ++n;
    cand[n][0] =  s.dy; cand[n][1] =  s.dx; ++n;

    const int fromX = s.x, fromY = s.y;
    bool travelled = false;
    bool delivered = false;
    bool sleepingBoundary = false;
    /* Set when an onward cell was conductive but already carried this pulse --
       i.e. the wave goes on through it, just not carried by this front. */
    bool handedOn = false;
    for (int k = 0; k < n; ++k) {
        const int nx = fromX + cand[k][0], ny = fromY + cand[k][1];
        if (nx == fromX && ny == fromY) continue;
        if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1) continue;

        /* The wire beyond the live simulation boundary is frozen. Let the pulse
           vanish there instead of treating that boundary as a physical open end:
           clocks can then never pour endpoint heat into copper that is not getting
           its matching cooling step. */
        if (!w.electricalLive(nx, ny)) {
            if (conducts(w, nx, ny) || devAt(nx, ny)) sleepingBoundary = true;
            continue;
        }

        /* A machine is a SINK, not wire. Reaching one delivers the spark and ends
           its journey -- which is what makes "wire the clock to the placer" mean
           something. Devices deliberately do not conduct: a bank of machines
           bolted together would otherwise act as one big lump of cable and a spark
           would wander through all of them. */
        Device* d = devAt(nx, ny);
        if (d) { d->poked = true; ++d->received; delivered = true; continue; }

        /* A door is a sink too, on exactly the same terms as a machine: the
           spark is delivered and its journey ends. That is what makes "wire the
           clock to the door" mean something, and it is why doors are not
           conductive -- a door in a metal wall would otherwise be a piece of the
           wall as far as electricity is concerned, and every spark crossing the
           wall would flap it. */
        if (doorToggle(w, nx, ny)) { delivered = true; continue; }

        if (!conducts(w, nx, ny)) continue;
        const int mark = ny * SIM_W + nx;
        const u16 claim = pulseMarkAt(mark);
        /* --- occupied wire, by this wave or by another one ------------------
           Two cases that have to be refused for two different reasons, and the
           second one used to be allowed.

           It is MY wave: a sibling front got here first. Refusing is what fills
           a thick wire's cross-section exactly once, and what makes a ring
           safe -- fronts going opposite ways round it meet and stop.

           It is the RECENT LOCAL WAKE of another wave that is still travelling.
           This was once permitted, because a mark only meant "my pulse has been
           here", and two head-on waves passed straight THROUGH each other. In
           wire two or more cells thick, each then flooded back down the other's
           trail and refilled the cross-section every frame. Four fronts became
           four hundred on a 200x2 wire until the copper blew apart.

           Testing only whether the other wave still exists went too far the
           other way. A long wave kept its entire trail occupied, so the sideways
           steps of a following pulse could hit an old, differently-directed
           claim and be swallowed despite a visible gap. The short recent wake
           is the actual collision region: beyond it another pulse may reuse the
           trail, while this pulse's own marks remain permanent for loop safety. */
        const u16 claimId = (u16)(claim & PULSE_ID_MASK);
        const bool blocked = claimId
            && (claimId == s.pulse                     /* my own wave's trail */
                || (g_pulseFronts[claimId]             /* another, still going */
                    && sparkRecentStamp(mark)           /* locally still here */
                    && (int)(claim >> PULSE_DIR_SHIFT)
                       != dirIndex(cand[k][0], cand[k][1])));
        if (blocked) {
            /* Either way the wave carries on through that cell without this
               front, so this is not an open end and must not shed. See the note
               at the bottom of this function. */
            handedOn = true;
            continue;
        }
        pulseMarkSet(mark, (u16)(s.pulse
                              | ((u16)dirIndex(cand[k][0], cand[k][1]) << PULSE_DIR_SHIFT)));
        sparkMarkRecent(mark);

        if (!travelled) {
            /* Keep this front for the preferred direction. Other exits become
               sibling fronts with the same pulse id. */
            s.x = nx; s.y = ny;
            s.dx = (i16)cand[k][0]; s.dy = (i16)cand[k][1];
            travelled = true;
        } else {
            sparkAddFront(nx, ny, cand[k][0], cand[k][1], s.pulse);
        }
    }

    if (travelled) return true;
    if (delivered) return false;
    if (sleepingBoundary) return false;

    /* Nowhere to go: spat out. The heat goes into the cell it was heading into if
       that is open, and otherwise into the conductor it died in. */
    const int ox = s.x + s.dx, oy = s.y + s.dy;
    const bool openAhead = ox >= PLAY_X0 && ox <= PLAY_X1 && oy >= PLAY_Y0 && oy <= PLAY_Y1
                           && w.at(ox, oy).mat == MAT_EMPTY;
    const int hx = openAhead ? ox : s.x, hy = openAhead ? oy : s.y;
    w.heat(hx, hy, 1, SPARK_HEAT);
    sparkArcIfHot(w, hx, hy);
    /* --- and it SPITS, if this is a real end of the wave --------------------

       Not merely if this FRONT stopped. A pulse travels as many fronts abreast
       -- one per cell of the wire's cross-section -- so every place a conductor
       narrows, the outer fronts run out of cells while the wave itself carries
       straight on through the middle. Shedding there sheds at every taper, every
       bevel and every rounded corner, which is what "the wire spits sparks any
       time it gets thinner" looks like from the outside.

       `handedOn` is the difference, and it is exact rather than a heuristic: it
       says an onward cell WAS conductive and already carried this pulse, so the
       wave is continuing through it and this front is simply the one that lost
       the race for it. A genuine end has no such cell -- nothing conductive
       ahead at all, taken or otherwise.

       Which leaves a wave that FORKS behaving properly, and that is the point
       of doing it this way rather than by counting fronts per pulse: two
       branches that separate and end in two different places are two real ends,
       and they shed two motes. Only the ends are counted, not the narrowings. */
    if (openAhead && !handedOn) shedAdd(ox, oy, s.dx, s.dy);
    return false;
}

/* A branchy metal blob can leave hundreds of one-cell fronts drifting through
   it. Their endpoints are too spread out to heat any one pixel, which makes a
   bad circuit occupy the front cap without ever resolving. Count live fronts
   per chunk and turn a genuinely crowded chunk into a local arc hotspot.

   The scratch arrays are reset only for chunks which contained a spark this
   frame. With 2048 fronts the work is strictly bounded and proportional to
   electricity activity, not to SIM_W*SIM_H. */
static void sparkCrowdHeat(World& w) {
    int touched = 0;
    for (int i = 0; i < MAX_SPARKS; ++i) {
        const Spark& s = g_sparks[i];
        if (!s.used || !w.electricalLive(s.x, s.y)) continue;
        const int ci = (s.y >> CHUNK_SHIFT) * CHUNKS_X + (s.x >> CHUNK_SHIFT);
        if (g_sparkLoad[ci] == 0) {
            g_sparkTouched[touched++] = ci;
            g_sparkHot[ci] = i;
        }
        if (g_sparkLoad[ci] != 0xFFFFu) ++g_sparkLoad[ci];
    }
    for (int i = 0; i < touched; ++i) {
        const int ci = g_sparkTouched[i];
        const int load = g_sparkLoad[ci];
        const int hot = g_sparkHot[ci];
        g_sparkLoad[ci] = 0;
        if (load < SPARK_CROWD_AT || hot < 0 || !g_sparks[hot].used) continue;
        const Spark& s = g_sparks[hot];
        w.heat(s.x, s.y, 1, (load - SPARK_CROWD_AT + 1) * SPARK_CROWD_HEAT);
        sparkArcIfHot(w, s.x, s.y);
    }
}

/* Emit from a device into every conductor touching its perimeter, heading
   outward. Returns how many sparks got away.

   Scanning the perimeter rather than having a designated output side is what makes
   a machine something you can wire from whichever direction is convenient -- the
   alternative would need a rotation on every device, shown and adjustable, for very
   little gain.

   EVERY run of conductor, not the first one found, and that was a real bug rather
   than a nicety. A thermocouple bolted flush to a graphene crucible -- which is
   what you build a crucible from, since it is the one material with no melting
   point -- had its signal swallowed by the furnace body: devEmit found the housing
   before it found the wire, put the spark into it, and returned. The contraption
   smelted correctly and then simply never triggered its second stage. A firing
   machine energising everything touching it is both the fix and the more honest
   behaviour for a terminal.

   One spark per CONTIGUOUS RUN, not per cell, or a device flush against a slab
   would emit fourteen sparks into the same lump of metal. Bounded by half the
   perimeter in the worst case, which is small and finite. This is not the
   branching that sparkStep deliberately refuses -- that is one spark multiplying
   as it travels, which is unbounded; this is a fixed number of outputs on a fixed
   perimeter. */
static int devEmit(World& w, const Device& d) {
    int sent = 0;
    bool run = false;
    /* Top edge, then bottom, then the two sides. `run` resets between edges so a
       conductor wrapping a corner counts once per side, which is what you want:
       the two sides of a corner are two different directions to leave in. */
    run = false;
    for (int x = d.x; x < d.x + DEV_W; ++x) {
        const bool c = w.electricalLive(x, d.y - 1) && conducts(w, x, d.y - 1);
        if (c && !run && sparkAdd(x, d.y - 1, 0, -1)) ++sent;
        run = c;
    }
    run = false;
    for (int x = d.x; x < d.x + DEV_W; ++x) {
        const bool c = w.electricalLive(x, d.y + DEV_H) && conducts(w, x, d.y + DEV_H);
        if (c && !run && sparkAdd(x, d.y + DEV_H, 0, 1)) ++sent;
        run = c;
    }
    run = false;
    for (int y = d.y; y < d.y + DEV_H; ++y) {
        const bool c = w.electricalLive(d.x - 1, y) && conducts(w, d.x - 1, y);
        if (c && !run && sparkAdd(d.x - 1, y, -1, 0)) ++sent;
        run = c;
    }
    run = false;
    for (int y = d.y; y < d.y + DEV_H; ++y) {
        const bool c = w.electricalLive(d.x + DEV_W, y) && conducts(w, d.x + DEV_W, y);
        if (c && !run && sparkAdd(d.x + DEV_W, y, 1, 0)) ++sent;
        run = c;
    }
    return sent;
}

/* --- the mote a cut wire end spits ---------------------------------------- */
ShedSpark g_shed[MAX_SHED];

/* Long enough to fall a useful distance and short enough that a shower of them
   is a moment rather than a state. At PROJ_GRAVITY a mote covers roughly 130
   cells in 90 frames, which is a couple of screens down -- far enough to reach
   the cable run you forgot about on the floor below. */
static const int SHED_LIFE = 90;
/* Sideways kick, so a row of ends does not drop a ruler-straight curtain. */
static const float SHED_SPREAD = 0.35f;

void shedClear() { for (int i = 0; i < MAX_SHED; ++i) g_shed[i].used = false; }

int shedCount() {
    int n = 0;
    for (int i = 0; i < MAX_SHED; ++i) if (g_shed[i].used) ++n;
    return n;
}

/* Dropped out of the open end at (x, y), heading the way the front was going.
   Silently discarded when the pool is full; see MAX_SHED. */
static bool shedAdd(int x, int y, int dx, int dy) {
    for (int i = 0; i < MAX_SHED; ++i) {
        ShedSpark& s = g_shed[i];
        if (s.used) continue;
        s.x = (float)x + 0.5f;
        s.y = (float)y + 0.5f;
        /* Carries a little of the wire's direction, so a spark leaving a
           horizontal run visibly goes OUT of the end before gravity takes it,
           rather than dropping vertically from a point. */
        s.vx = (float)dx * 0.45f
             + ((float)(int)(rngNext() % 200u) / 100.0f - 1.0f) * SHED_SPREAD;
        s.vy = (float)dy * 0.45f;
        s.life = SHED_LIFE;
        s.used = true;
        return true;
    }
    return false;
}

bool shedPlace(int x, int y) {
    if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) return false;
    /* A placed mote has no wire-end momentum. Its existing random sideways
       drift and gravity take over immediately, exactly as for a naturally shed
       one after the initial kick has gone. */
    return shedAdd(x, y, 0, 0);
}

bool shedTakeNear(int x, int y, int radius) {
    int best = -1;
    float bestD2 = (float)(radius * radius) + 1.0f;
    for (int i = 0; i < MAX_SHED; ++i) {
        const ShedSpark& s = g_shed[i];
        if (!s.used) continue;
        const float dx = s.x - ((float)x + 0.5f);
        const float dy = s.y - ((float)y + 0.5f);
        const float d2 = dx * dx + dy * dy;
        if (d2 <= (float)(radius * radius) && d2 < bestD2) { best = i; bestD2 = d2; }
    }
    if (best < 0) return false;
    g_shed[best].used = false;
    return true;
}

/* Fall, and energise whatever conductor we land in.

   Stepped a cell at a time along the frame's path rather than sampled at the
   end of it: a mote under gravity is soon moving faster than one cell a frame,
   and a sampled step would drop straight through a one-cell-thick wire, which
   is exactly the wire somebody strung across a room. Same reasoning as the
   projectile traversal, at a much smaller scale. */
static void shedTick(World& w) {
    for (int i = 0; i < MAX_SHED; ++i) {
        ShedSpark& s = g_shed[i];
        if (!s.used) continue;
        if (!w.electricalLive((int)s.x, (int)s.y)) continue;
        if (--s.life <= 0) { s.used = false; continue; }

        s.vy += PROJ_GRAVITY;

        const float tx = s.x + s.vx, ty = s.y + s.vy;
        const int steps = 1 + (int)(fabsf(tx - s.x) + fabsf(ty - s.y));
        for (int k = 1; k <= steps && s.used; ++k) {
            const float f = (float)k / (float)steps;
            const int cx = (int)(s.x + (tx - s.x) * f);
            const int cy = (int)(s.y + (ty - s.y) * f);
            if (cx < PLAY_X0 || cx > PLAY_X1 || cy < PLAY_Y0 || cy > PLAY_Y1) {
                s.used = false; break;
            }
            if (!w.electricalLive(cx, cy)) { s.used = false; break; }
            /* THE point of the whole feature. Landing in bare conductor starts
               a pulse there, which is how a spark shed by one circuit sets off
               another one nobody wired to it. */
            if (conducts(w, cx, cy)) {
                sparkAdd(cx, cy, 0, 1);
                s.used = false;
                break;
            }
            /* Anything else solid just stops it. A mote is not a projectile and
               does not dig, burn or bounce -- it lands and goes out. */
            const u8 m = w.at(cx, cy).mat;
            if (m != MAT_EMPTY && MATS[m].kind != KIND_GAS) { s.used = false; break; }
        }
        if (s.used) { s.x = tx; s.y = ty; }
    }
}

void shedDraw(u32* px, int camX, int camY) {
    for (int i = 0; i < MAX_SHED; ++i) {
        const ShedSpark& s = g_shed[i];
        if (!s.used) continue;
        const int vx = (int)s.x - camX, vy = (int)s.y - camY;
        /* Fades as it dies, so a mote reads as cooling rather than as blinking
           out. Two cells across for the same reason a spark front is: one cell
           is two screen pixels and disappears into the noise. */
        const int fade = s.life > 30 ? 255 : (s.life * 255) / 30;
        const u32 col = ((u32)(0xFF * fade / 255) << 16)
                      | ((u32)(0xD8 * fade / 255) << 8)
                      |  (u32)(0x70 * fade / 255);
        for (int oy = 0; oy < 2; ++oy)
            for (int ox = 0; ox < 2; ++ox) {
                const int px_ = vx + ox, py_ = vy + oy;
                if (px_ < 0 || px_ >= VIEW_CELLS_W || py_ < 0 || py_ >= VIEW_CELLS_H) continue;
                px[py_ * VIEW_CELLS_W + px_] = col;
            }
    }
}

void sparkDraw(u32* px, int camX, int camY) {
    for (int i = 0; i < MAX_SPARKS; ++i) {
        const Spark& s = g_sparks[i];
        if (!s.used) continue;
        const int vx = s.x - camX, vy = s.y - camY;
        /* Two cells across, so a spark on a one-cell wire is visible at all --
           a single cell is two screen pixels and reads as noise. */
        for (int oy = 0; oy < 2; ++oy)
            for (int ox = 0; ox < 2; ++ox) {
                const int px_ = vx + ox, py_ = vy + oy;
                if (px_ < 0 || px_ >= VIEW_CELLS_W || py_ < 0 || py_ >= VIEW_CELLS_H) continue;
                px[py_ * VIEW_CELLS_W + px_] = 0xCFF4FF;
            }
    }
}


int devCount() {
    int n = 0;
    for (int i = 0; i < MAX_DEVICES; ++i) if (g_devices[i].used) ++n;
    return n + (int)g_torches.size();
}

int torchCount() { return (int)g_torches.size(); }
const TorchFixture* torchData() { return g_torches.empty() ? 0 : &g_torches[0]; }
void torchLoad(const TorchFixture* fixtures, int count) {
    g_torches.clear();
    if (fixtures && count > 0) g_torches.assign(fixtures, fixtures + count);
}
void torchAdd(i32 x, i32 y) { const TorchFixture fixture = { x, y }; g_torches.push_back(fixture); }

int torchAt(int cx, int cy) {
    for (int i = 0; i < (int)g_torches.size(); ++i) {
        const TorchFixture& t = g_torches[i];
        if (cx >= t.x && cx < t.x + DEV_W && cy >= t.y && cy < t.y + DEV_H) return i;
    }
    return -1;
}

bool torchRemoveAt(int index) {
    if (index < 0 || index >= (int)g_torches.size()) return false;
    g_torches[index] = g_torches.back();
    g_torches.pop_back();
    return true;
}

static void logisticsMarkDirty();
void devClear() {
    for (int i = 0; i < MAX_DEVICES; ++i) g_devices[i].used = false;
    g_torches.clear();
    circuitClear();
    logisticsMarkDirty();
}

Device* devAt(int cx, int cy) {
    for (int i = 0; i < MAX_DEVICES; ++i) {
        Device& d = g_devices[i];
        if (!d.used) continue;
        if (cx >= d.x && cx < d.x + DEV_W && cy >= d.y && cy < d.y + DEV_H)
            return &d;
    }
    return 0;
}

static bool isLogistics(u8 type);
static void logisticsMarkDirty();

bool devPlace(World& w, u8 type, int cx, int cy) {
    if (type >= DEV_COUNT) return false;
    int x0 = devOriginX(cx), y0 = devOriginY(cy);
    /* Logistics pieces use a coarse, shared lattice. Their connection rule is
       literal edge contact, so snapping is what makes a run of pipes a thing
       you can lay reliably rather than a pixel-perfect placement exercise.

       Asked by NAME, not by `type >= DEV_PIPE`, which is what this used to say.
       The ordinal test was only ever right by accident of enum order and had
       already drifted: the block watcher, the pulse button and all three
       combinators sort after DEV_PIPE and were being snapped too, none of which
       connects by edge contact. Appending the workbench made it worse in a way
       that was visible -- a piece of furniture jumping to a 14-cell grid -- and
       would have gone on catching every device added after it.

       Free placement is also the documented default here; see the note in
       device.h on why the lattice was removed from everything else. */
    if (isLogistics(type)) {
        x0 = PLAY_X0 + ((cx - PLAY_X0) / DEV_W) * DEV_W;
        y0 = PLAY_Y0 + ((cy - PLAY_Y0) / DEV_H) * DEV_H;
    }
    if (x0 < PLAY_X0 || y0 < PLAY_Y0) return false;
    if (x0 + DEV_W > PLAY_X1 || y0 + DEV_H > PLAY_Y1) return false;

    /* No overlapping another machine. A rectangle test against the list rather
       than a lattice-slot lookup -- see the note in device.h for why the lattice
       had to go. Devices are capped at 128, so this is a few dozen integer
       comparisons on a mouse click. */
    for (int i = 0; i < MAX_DEVICES; ++i) {
        const Device& o = g_devices[i];
        if (!o.used) continue;
        if (x0 < o.x + DEV_W && o.x < x0 + DEV_W &&
            y0 < o.y + DEV_H && o.y < y0 + DEV_H) return false;
    }
    for (int i = 0; i < (int)g_torches.size(); ++i) {
        const TorchFixture& t = g_torches[i];
        if (x0 < t.x + DEV_W && t.x < x0 + DEV_W &&
            y0 < t.y + DEV_H && t.y < y0 + DEV_H) return false;
    }

    /* And nothing solid may be in the way. Powders and liquids ARE allowed to be
       displaced -- you should be able to bolt a machine into a heap of sand or a
       shallow pool without excavating first -- but rock and other machinery are
       not, so a device can never be shoved into a wall. */
    for (int y = y0; y < y0 + DEV_H; ++y)
        for (int x = x0; x < x0 + DEV_W; ++x) {
            const u8 k = MATS[w.at(x, y).mat].kind;
            if (k == KIND_STATIC) return false;
        }

    /* A fixture is all position and no machine state. It intentionally does
       not consume a circuit/device slot, so a network can be lit without an
       unrelated machine limit becoming a torch limit. */
    if (type == DEV_TORCH) {
        torchAdd(x0, y0);
        return true;
    }

    int slot = -1;
    for (int i = 0; i < MAX_DEVICES; ++i) if (!g_devices[i].used) { slot = i; break; }
    if (slot < 0) return false;

    Device& d = g_devices[slot];
    d.type    = type;
    d.x = x0; d.y = y0;
    d.value   = DEVS[type].vDefault;
    d.firing  = false;
    d.poked   = false;
    d.latched = false;
    d.enabled = true;
    d.phase   = 0;
    d.reading = 0;
    d.received = 0;
    d.mat     = MAT_EMPTY;
    d.count   = 0;
    d.mat2    = MAT_EMPTY;
    d.count2  = 0;
    d.pipeFrom = -1;
    d.face    = 0;                  /* down */
    d.used    = true;
    circuitInitDevice(slot, type);

    for (int y = y0; y < y0 + DEV_H; ++y)
        for (int x = x0; x < x0 + DEV_W; ++x) w.setCell(x, y, DEVS[type].cellMat);
    if (isLogistics(type)) logisticsMarkDirty();
    return true;
}

void devRemove(World& w, Device* d) {
    if (!d || !d->used) return;
    const int index = (int)(d - g_devices);
    for (int y = d->y; y < d->y + DEV_H; ++y)
        for (int x = d->x; x < d->x + DEV_W; ++x)
            if (w.at(x, y).mat == DEVS[d->type].cellMat) w.setCell(x, y, MAT_EMPTY);
    const bool wasLogistics = isLogistics(d->type);
    d->used = false;
    circuitRemoveDevice(index);
    if (wasLogistics) logisticsMarkDirty();
}

void devRegisterLights() {
    for (int i = 0; i < (int)g_torches.size(); ++i)
        lightAddDynamic(g_torches[i].x + DEV_W / 2, g_torches[i].y + DEV_H / 2, 118);
    /* A pedestal is a light source only while it is HOLDING something. An empty
       plinth going on glowing would be the object lying to you about the one
       fact it exists to communicate -- and it is the fact you read from across
       a dark chamber, before you are close enough to see the plinth at all. */
    for (int i = 0; i < MAX_DEVICES; ++i) {
        const Device& d = g_devices[i];
        if (!d.used || d.type != DEV_PEDESTAL || pedestalItem(d) == ITEM_NONE) continue;
        lightAddDynamic(d.x + DEV_W / 2, d.y + DEV_H / 2, PEDESTAL_LIGHT);
    }
}

/* Mean temperature over the device's own cells, in stored units. Averaged rather
   than sampled at one corner: a machine bolted to the side of a furnace has a
   real gradient across it, and reading a single cell makes the trip point depend
   on which way round you placed it. */
static int devTemp(const World& w, const Device& d) {
    long sum = 0; int n = 0;
    for (int y = d.y; y < d.y + DEV_H; ++y)
        for (int x = d.x; x < d.x + DEV_W; ++x) {
            sum += w.temp[y * SIM_W + x];
            ++n;
        }
    return n ? (int)(sum / n) : 0;
}

/* Is the device still all there? A cell of it can be mined or shot out, and a
   machine with a hole in it should stop being a machine rather than carry on
   working invisibly. Checked cheaply -- the four corners and the centre, not all
   196 cells -- because this runs for every device every frame and any real breach
   takes out a corner or the middle almost immediately. */
static bool devIntact(const World& w, const Device& d) {
    /* A torch is a fixture with no physical footprint: it deliberately remains
       intact while water or a falling powder occupies the cells behind it. */
    if (d.type == DEV_TORCH) return true;
    const int xs[5] = { d.x, d.x + DEV_W - 1, d.x, d.x + DEV_W - 1, d.x + DEV_W / 2 };
    const int ys[5] = { d.y, d.y, d.y + DEV_H - 1, d.y + DEV_H - 1, d.y + DEV_H / 2 };
    for (int k = 0; k < 5; ++k)
        if (w.at(xs[k], ys[k]).mat != DEVS[d.type].cellMat) return false;
    return true;
}


int devBoxDepth(const Device& d) {
    /* Zero is not a legal depth, it is "never set" -- see the note in device.h.
       Clamped on the way OUT so a save carrying anything odd still behaves. */
    const int depth = d.mat2 ? (int)d.mat2 : 1;
    return depth < 1 ? 1 : (depth > DEV_W ? DEV_W : depth);
}
void devSetBoxDepth(Device& d, int depth) {
    if (depth < 1) depth = 1;
    if (depth > DEV_W) depth = DEV_W;
    d.mat2 = (u8)depth;
}
int devFilterMat(const Device& d) {
    return (d.pipeFrom > 0 && d.pipeFrom < MAT_COUNT) ? (int)d.pipeFrom : MAT_EMPTY;
}
void devSetFilterMat(Device& d, int mat) {
    d.pipeFrom = (mat > 0 && mat < MAT_COUNT) ? (i16)mat : (i16)-1;
}
int devRunMode(const Device& d) {
    return (d.count2 >= 0 && d.count2 < DEVRUN_COUNT) ? (int)d.count2 : DEVRUN_WHILE_ON;
}
void devSetRunMode(Device& d, int mode) {
    d.count2 = (i32)((mode % DEVRUN_COUNT + DEVRUN_COUNT) % DEVRUN_COUNT);
}
const char* devRunModeName(int mode) {
    return mode == DEVRUN_ON_EDGE ? "per pulse" : "while on";
}

void devBoxCell(const Device& d, int i, int layer, int* ox, int* oy) {
    switch (d.face) {
    case 1:  *ox = d.x + i;                  *oy = d.y - 1 - layer;         break; /* up */
    case 2:  *ox = d.x - 1 - layer;          *oy = d.y + i;                 break; /* left */
    case 3:  *ox = d.x + DEV_W + layer;      *oy = d.y + i;                 break; /* right */
    default: *ox = d.x + i;                  *oy = d.y + DEV_H + layer;     break; /* down */
    }
}

void devFaceCell(const Device& d, int i, int* ox, int* oy) {
    switch (d.face) {
    case 1:  *ox = d.x + i;        *oy = d.y - 1;        break;   /* up */
    case 2:  *ox = d.x - 1;        *oy = d.y + i;        break;   /* left */
    case 3:  *ox = d.x + DEV_W;    *oy = d.y + i;        break;   /* right */
    default: *ox = d.x + i;        *oy = d.y + DEV_H;    break;   /* down */
    }
}

/* --- the buffer ------------------------------------------------------------
   A placer draws loose material touching its footprint into store; a miner puts
   what it breaks there. Both share one rule about mixing: a buffer holds a single
   material, and anything else is refused rather than blended. Silently converting
   one material into another inside a machine is the kind of behaviour that makes a
   contraption impossible to trust. */
static bool devTakeInto(Device& d, u8 mat) {
    const int cap = d.type == DEV_CHEST ? CHEST_CAP : DEV_CAP;
    if (d.count >= cap) return false;
    if (d.count > 0 && d.mat != mat) return false;
    d.mat = mat;
    ++d.count;
    return true;
}

static bool isLogistics(u8 type) {
    return type == DEV_PIPE || type == DEV_CROSSOVER || type == DEV_CHEST ||
           type == DEV_SPOUT || type == DEV_DRAIN;
}

static bool pipeSends(u8 type) {
    return type == DEV_CHEST || type == DEV_DRAIN || type == DEV_PIPE || type == DEV_CROSSOVER;
}
static bool pipeReceives(u8 type) {
    /* Conduits are topology only. A logistics item may finish at a spout, but
       it can never be deposited in a pipe on the way there. */
    return type == DEV_SPOUT;
}

/* Two footprints are joined only when they share a complete cell edge.  Corner
   contact deliberately does not count: a pipe network should be drawable and
   debuggable by eye, not leak through a diagonal kiss. */
static bool pipeJoined(const Device& a, const Device& b, bool* vertical) {
    const bool xOverlap = a.x < b.x + DEV_W && b.x < a.x + DEV_W;
    const bool yOverlap = a.y < b.y + DEV_H && b.y < a.y + DEV_H;
    if (xOverlap && (a.y + DEV_H == b.y || b.y + DEV_H == a.y)) { *vertical = true; return true; }
    if (yOverlap && (a.x + DEV_W == b.x || b.x + DEV_W == a.x)) { *vertical = false; return true; }
    return false;
}

/* Defined HERE rather than in main.cpp, where it started. device.cpp reads it
   and main.cpp only writes it, so putting the storage in the UI made the
   SIMULATION unlinkable without the Win32 half of the program -- which broke
   every headless harness in one go: 29 of 34 died on one undefined symbol while
   build.bat, which links main.cpp, stayed perfectly happy. The direction of the
   dependency is the whole point; a flag the sim consults belongs to the sim. */
bool g_logisticsUiOpen = false;

/* Pipe topology is a cache, not per-frame simulation. Rebuilding does the one
   bounded graph walk after a player changes plumbing; a long bridge through
   sleeping chunks is then just a component id on its two loaded endpoints. */
static i16  g_pipeComponent[MAX_DEVICES];
static bool g_pipeTopologyDirty = true;
static void logisticsMarkDirty() { g_pipeTopologyDirty = true; }

static void logisticsRebuild() {
    if (!g_pipeTopologyDirty) return;
    g_pipeTopologyDirty = false;
    for (int i = 0; i < MAX_DEVICES; ++i) g_pipeComponent[i] = -1;
    int component = 0, queue[MAX_DEVICES];
    for (int root = 0; root < MAX_DEVICES; ++root) {
        if (!g_devices[root].used || !isLogistics(g_devices[root].type) || g_pipeComponent[root] >= 0) continue;
        int head = 0, tail = 0;
        queue[tail++] = root; g_pipeComponent[root] = (i16)component;
        while (head < tail) {
            const int i = queue[head++];
            for (int j = 0; j < MAX_DEVICES; ++j) {
                if (g_pipeComponent[j] >= 0 || !g_devices[j].used || !isLogistics(g_devices[j].type)) continue;
                bool vertical = false;
                if (!pipeJoined(g_devices[i], g_devices[j], &vertical)) continue;
                g_pipeComponent[j] = (i16)component;
                queue[tail++] = j;
            }
        }
        ++component;
    }
    /* Pipes no longer own material. Saves from the previous model may still
       have stock sitting in their little pipe buffers, so flush that stock
       straight into compatible spouts in the same component before clearing
       the obsolete state. Nothing already in the player's build vanishes just
       because the transport model got simpler. */
    for (int i = 0; i < MAX_DEVICES; ++i) {
        Device& d = g_devices[i];
        if (!d.used || (d.type != DEV_PIPE && d.type != DEV_CROSSOVER)) continue;
        u8* mats[2] = { &d.mat, &d.mat2 };
        i32* counts[2] = { &d.count, &d.count2 };
        const int lanes = d.type == DEV_CROSSOVER ? 2 : 1;
        for (int lane = 0; lane < lanes; ++lane) {
            for (int j = 0; j < MAX_DEVICES && *counts[lane] > 0; ++j) {
                Device& dst = g_devices[j];
                if (!dst.used || dst.type != DEV_SPOUT || g_pipeComponent[j] != g_pipeComponent[i]) continue;
                const int cap = DEV_CAP;
                if (dst.count > 0 && dst.mat != *mats[lane]) continue;
                const int n = imin((int)*counts[lane], cap - (int)dst.count);
                if (n <= 0) continue;
                dst.mat = *mats[lane]; dst.count += n; *counts[lane] -= n;
            }
            if (*counts[lane] == 0) *mats[lane] = MAT_EMPTY;
        }
        /* If every compatible spout is full, the remaining legacy stock stays
           only until capacity opens; ordinary pipeTick below flushes it. New
           pipes are born empty and are never used as storage. */
        d.pipeFrom = -1;
    }
}

static void pipeTick(const World& w) {
    if (g_logisticsUiOpen) return;
    logisticsRebuild();
    static const int PIPE_RATE = 4; /* throughput belongs to endpoints, not distance */
    for (int i = 0; i < MAX_DEVICES; ++i) {
        Device& src = g_devices[i];
        if (!src.used || !pipeSends(src.type) || !w.electricalLive(src.x + DEV_W / 2, src.y + DEV_H / 2)) continue;
        int left = PIPE_RATE;
        for (int j = 0; j < MAX_DEVICES; ++j) {
            if (i == j || g_pipeComponent[i] < 0 || g_pipeComponent[i] != g_pipeComponent[j]) continue;
            Device& dst = g_devices[j];
            if (!dst.used || !pipeReceives(dst.type) || !w.electricalLive(dst.x + DEV_W / 2, dst.y + DEV_H / 2)) continue;
            const int cap = dst.type == DEV_CHEST ? CHEST_CAP : DEV_CAP;
            if (src.count <= 0 || (dst.count > 0 && dst.mat != src.mat) || dst.count >= cap) continue;
            const int n = imin(left, imin((int)src.count, cap - (int)dst.count));
            dst.mat = src.mat; dst.count += n; src.count -= n;
            if (src.count == 0) src.mat = MAT_EMPTY;
            if ((left -= n) == 0 || src.count == 0) break;
        }
    }
}

static void devDrainSide(World& w, Device& d, int face, int* taken) {
    Device probe = d; probe.face = (u8)face;
    for (int i = 0; i < DEV_W && *taken < 4; ++i) {
        int x, y; devFaceCell(probe, i, &x, &y);
        if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) continue;
        const u8 m = w.at(x, y).mat;
        if (m == MAT_EMPTY || m == MAT_WALL || m == MAT_DEVICE || (d.value && m != d.value)) continue;
        if (!devTakeInto(d, m)) return;
        w.setCell(x, y, MAT_EMPTY); ++*taken;
    }
}

static void devDrain(World& w, Device& d) {
    /* --- all four edges, always -----------------------------------------
       A drain has no facing any more. It used to prefer its aimed edge and
       only fall back to the others when that one came up dry, which produced a
       device whose behaviour depended on a control most people never set and
       whose sprite pointed somewhere it did not really care about.

       The fallback was already doing most of the work in practice -- a drain
       dropped beside a pool drains it whichever way it happens to be turned --
       so this makes the common case the ONLY case. Sweeping every side every tick
       also means a drain sunk into a floor takes from the sides and the bottom
       at once, which is what anybody building a sump expects it to do.

       Still capped at four cells a tick in total, so widening the intake does
       not quietly quadruple the rate. */
    int taken = 0;
    for (int face = 0; face < 4 && taken < 4; ++face)
        devDrainSide(w, d, face, &taken);
}

/* --- the hive --------------------------------------------------------------

   Two jobs. It keeps its bees alive -- topping the colony back up to its
   setpoint, slowly, so a hive that has just been placed is something you
   watch fill rather than something that arrives finished -- and it turns
   deliveries into material.

   Wax leaves through the TOP and honey through the SIDES. That is not
   decoration: wax is a solid that will sit where it lands and can be mined
   off the roof, and honey is a heavy liquid that would simply pour back down
   over the hive if it came out of the same face. Putting them on different
   faces is what makes the output separable without any sorting machinery.

   FIELD ALIASING, stated plainly because it is the sort of thing that rots:
   a hive uses `count` for pending ordinary deliveries, `count2` for pending
   coal ones, and `phase` as its spawn timer. Those fields belong to the
   logistics parts and the clock respectively, and nothing but a hive ever
   reads them on a hive. */

static const int HIVE_MAX_BEES   = 5;
static const int HIVE_SPAWN_EVERY = 300;  /* five seconds a bee */
static const int HIVE_EXTRUDE_EVERY = 24;
/* How far up the hive will shove its own output to make room. The same
   trick the spout uses (DEV_SPOUT_LIFT) and for the same reason: without
   it the first cell of wax to settle on the roof blocks the face and the
   hive stops, so a working colony produced one row and quit. With it the
   wax stacks into a column you can mine off the top. */
static const int HIVE_WAX_LIFT = 20;

/* --- where the bees go in --------------------------------------------

   On TOP of whatever the hive has extruded, not at a fixed point.

   Two wrong answers came first and both are worth keeping written down. A
   fixed cell above the hive is the wax outlet, so two cells of production
   buried the door and the colony milled about underneath its own hive
   forever -- the reported bug. Moving the door UNDERNEATH fixed the burying
   and broke foraging instead: bees steer straight at what they want with no
   pathfinding, so a door below a fourteen-cell solid box meant every bee
   pressed itself into the hive wall trying to leave.

   Riding the pile solves both. The approach is always from open air above,
   which is the direction a bee can actually fly in, and the door cannot be
   buried because it is defined as the first cell that is NOT buried. As the
   wax stacks, the landing pad rises with it.

   Falls back around the sides for a hive with a ceiling on it, and finally
   to just above the hive so a walled-in colony still has somewhere to aim
   rather than a target inside solid rock. */
void hiveTarget(const World& w, const Device& d, float* x, float* y) {
    const int ix = d.x + DEV_W / 2;
    /* A bee is four cells across and has to FIT where it is sent, so the
       landing pad is the first cell with a bee's worth of clear air above it
       -- not merely the first empty cell. Without the clearance the door sat
       flush on the hive, every spawn was refused for want of room, and the
       colony never appeared at all. */
    const int CLEAR = 5;
    for (int up = 1; up <= HIVE_WAX_LIFT + 8; ++up) {
        const int sy = d.y - up;
        if (sy - CLEAR < PLAY_Y0) break;
        bool room = true;
        for (int k = 0; k < CLEAR && room; ++k)
            room = w.at(ix, sy - k).mat == MAT_EMPTY && !w.blocksCell(ix, sy - k);
        if (!room) continue;
        *x = (float)ix + 0.5f; *y = (float)(sy - 2) + 0.5f;
        return;
    }
    const int sides[2] = { d.x - 2, d.x + DEV_W + 1 };
    for (int k = 0; k < 2; ++k) {
        const int sx = sides[k], sy = d.y + DEV_H / 2;
        if (sx < PLAY_X0 || sx > PLAY_X1) continue;
        if (w.at(sx, sy).mat != MAT_EMPTY) continue;
        *x = (float)sx + 0.5f; *y = (float)sy + 0.5f;
        return;
    }
    *x = (float)ix + 0.5f; *y = (float)(d.y - 2);
}

void hiveDeliver(Device& d, bool coal) {
    if (d.type != DEV_HIVE) return;
    /* Capped. An unattended hive with a big flower field should not be able
       to bank an hour of production and then dump it in one go when you
       finally come back and its chunk starts ticking again. */
    if (coal) { if (d.count2 < 64) ++d.count2; }
    else      { if (d.count  < 64) ++d.count;  }
}

static int hiveBeeCount(int index) {
    int n = 0;
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        const Entity& e = g_entities[i];
        if (!e.alive() || e.home != (i16)index) continue;
        if (e.type == ENT_BEE || e.type == ENT_COAL_BEE) ++n;
    }
    return n;
}

/* Put one cell of `mat` somewhere along an edge, preferring a free cell.
   Returns whether it landed. Refusing when the face is blocked is what stops
   a walled-in hive from overwriting the wall it is against. */
static bool hiveExtrude(World& w, const Device& d, int face, u8 mat) {
    /* --- centre outwards, not randomly ---------------------------------
       Wax is meant to come OUT of the hive, and a random column each time
       spreads it into an even slab a cell or two deep across the whole face --
       which is what it did, and reads as a stain rather than as something
       being extruded. Filling from the middle builds a plug that climbs, and
       only widens once the middle has nowhere left to go.

       Sides are unaffected in practice: honey is a liquid and runs off
       wherever it is put. */
    for (int k = 0; k < DEV_W; ++k) {
        const int half = (k + 1) / 2;
        const int i = DEV_W / 2 + ((k & 1) ? -half : half);
        if (i < 0 || i >= DEV_W) continue;
        int x, y;
        if (face == 0)      { x = d.x + i;        y = d.y - 1; }
        else if (face == 1) { x = d.x - 1;        y = d.y + i; }
        else                { x = d.x + DEV_W;    y = d.y + i; }
        if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) continue;
        if (w.at(x, y).mat != MAT_EMPTY) {
            /* --- stack it, do not shove it -----------------------------
               The first attempt used liftColumn, the spout's pump. It caps
               out at three cells and the reason is written on the lift
               itself: `static means static`, and beeswax is KIND_STATIC.
               The pump can shove sand and water; it flatly refuses to move
               a solid, so the hive filled its face row and then failed
               every attempt after that.

               A solid does not need shoving anyway -- it needs stacking. Walk
               up the column and put the new cell on top of whatever is
               already there, which is what extruding a solid upward actually
               looks like. Bounded by HIVE_WAX_LIFT so a hive builds a slab
               rather than a tower to the sky, and it stops at a ceiling
               because the search runs out of empty cells. */
            if (face != 0) continue;
            int stackY = -1;
            for (int up = 1; up <= HIVE_WAX_LIFT; ++up) {
                const int ny = y - up;
                if (ny < PLAY_Y0) break;
                if (w.at(x, ny).mat != MAT_EMPTY) continue;
                if (w.blocksCell(x, ny)) break;   /* somebody is standing there */
                stackY = ny;
                break;
            }
            if (stackY < 0) continue;
            y = stackY;
        }
        w.setCell(x, y, mat);
        return true;
    }
    return false;
}

static void devHive(World& w, Device& d, int index) {
    /* --- keep the colony topped up ----------------------------------- */
    /* Nothing comes out after dark. Without this the hive would keep
       replacing the bees that have just gone in for the night, and the
       colony would work a night shift -- which is exactly the thing the
       going-in is meant to stop. The timer still runs, so dawn is not
       followed by another five seconds of waiting. */
    const int want = isNight() ? 0 : imax(1, imin((int)d.value, HIVE_MAX_BEES));
    if (++d.phase >= HIVE_SPAWN_EVERY) {
        d.phase = 0;
        if (hiveBeeCount(index) < want) {
            /* At the MOUTH, not the middle. A hive's footprint is
               fourteen cells of solid MAT_DEVICE, so a bee born at its
               centre is born inside a wall: it cannot get out, and it
               never reaches anything again. Two cells clear of the top
               edge is outside the box, and is where hiveTarget sends it
               back to. */
            float bx, by; hiveTarget(w, d, &bx, &by);
            const int slot = entSpawn(w, ENT_BEE, bx, by);
            if (slot >= 0) {
                g_entities[slot].home = (i16)index;
                g_entities[slot].phase = 0;
            }
        }
    }

    /* --- turn deliveries into material -------------------------------- */
    d.reading = d.count + d.count2;
    if ((w.frame % HIVE_EXTRUDE_EVERY) != 0) return;
    if (d.count2 > 0) {
        if (hiveExtrude(w, d, 0, MAT_COAL_WAX) |
            hiveExtrude(w, d, 1 + (int)(rngNext() & 1u), MAT_COAL_HONEY))
            --d.count2;
    } else if (d.count > 0) {
        if (hiveExtrude(w, d, 0, MAT_BEESWAX) |
            hiveExtrude(w, d, 1 + (int)(rngNext() & 1u), MAT_HONEY))
            --d.count;
    }
}

static void devSpout(World& w, Device& d) {
    /* --- the whole face, not the first few cells -------------------------
       The rate says how many cells a pulse delivers; it never said WHERE along
       the face they go, and walking i from zero meant every pulse below full
       rate poured out of the left-hand corner. A fourteen-wide spout behaving
       like a four-wide one bolted to one end is not what the footprint
       promises.

       Spread instead: the k-th of `rate` deliveries goes to cell k*DEV_W/rate,
       so four cells land at 0, 3, 7 and 10 rather than 0, 1, 2, 3, and the
       default rate of DEV_W covers every cell. A blocked cell is skipped
       rather than compensated for -- the rate is a budget, not a quota, and
       hunting for a free cell would make a spout against a wall silently
       dump everything through whatever gap it found. */
    const int rate = imax(1, imin((int)d.value, DEV_W));
    int done = 0;
    for (int k = 0; k < rate && d.count > 0; ++k) {
        const int i = k * DEV_W / rate;
        int x, y; devFaceCell(d, i, &x, &y);
        if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) continue;
        if (w.at(x, y).mat != MAT_EMPTY) {
            /* --- the pump ------------------------------------------------
               Blocked, so shove the column above up one and dispense into the
               cell that frees. That turns a spout pointed UP from something
               that stops the instant one cell of its own output settles on its
               face into something that fills a shaft, a tank or a pipe run
               against a real head of liquid.

               UP ONLY, and that is a decision rather than a gap. Up has one
               obvious answer -- everything above you moves up one -- and the
               world's own rules then take over, because falling back down is
               what loose material does anyway. Sideways would need to decide
               how far along the row to push and what happens at the far end,
               and down would be pushing material into the ground; neither has
               an answer that is one line long, and a half-answered piston is
               worse than an honest limit.

               A failed lift is skipped rather than retried, exactly like a
               blocked cell was before: the rate is a budget, not a quota. */
            if (d.face != 1 || !w.liftColumn(x, y, DEV_SPOUT_LIFT)) continue;
        }
        w.setCell(x, y, d.mat); --d.count; ++done;
    }
    (void)done;
    if (d.count == 0) d.mat = MAT_EMPTY;
}

/* Watches the fourteen cells directly in front of its aimed face.  It fires on
   contact, not every frame of contact: a conveyor arriving at the sensor is one
   event, while a block parked there is a condition that must clear before it can
   trigger again. Filter 0 means any non-empty block. */
static u8 devWatch(const World& w, const Device& d) {
    for (int i = 0; i < DEV_W; ++i) {
        int x, y; devFaceCell(d, i, &x, &y);
        if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) continue;
        const u8 m = w.at(x, y).mat;
        if (m != MAT_EMPTY && (!d.value || m == d.value)) return m;
    }
    return MAT_EMPTY;
}

/* A material signal on the circuit is a filter request. More than one positive
   material signal is legal, but a drain can physically hold only one type, so
   choose the strongest request (then stable id order on a tie). */
static u8 circuitMaterialFilter(int deviceIndex) {
    int bestValue = 0;
    u8 best = MAT_EMPTY;
    for (int m = 1; m < MAT_COUNT; ++m) {
        const int v = circuitInput(deviceIndex, m);
        if (v > bestValue) { bestValue = v; best = (u8)m; }
    }
    return best;
}

static int circuitArithmetic(int a, int b, int op) {
    switch (op) {
    case CIR_OP_ADD:      return a + b;
    case CIR_OP_SUBTRACT: return a - b;
    case CIR_OP_MULTIPLY: return a * b;
    case CIR_OP_DIVIDE:   return b ? a / b : 0;
    case CIR_OP_MODULO:   return b ? a % b : 0;
    default: return 0;
    }
}

static bool circuitCompare(int a, int b, int op) {
    switch (op) {
    case CIR_OP_GREATER: return a > b;
    case CIR_OP_LESS:    return a < b;
    case CIR_OP_EQUAL:   return a == b;
    case CIR_OP_GREATER_EQUAL: return a >= b;
    case CIR_OP_LESS_EQUAL:    return a <= b;
    case CIR_OP_NOT_EQUAL:     return a != b;
    default: return false;
    }
}

bool sparkAdd(int x, int y, int dx, int dy) {
    if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) return false;
    const u16 pulse = nextPulse();
    const int mark = y * SIM_W + x;
    pulseMarkSet(mark, (u16)(pulse | ((u16)dirIndex(dx, dy) << PULSE_DIR_SHIFT)));
    sparkMarkRecent(mark);
    return sparkAddFront(x, y, dx, dy, pulse);
}

/* Cells drawn in per frame while something is piled against a placer. A rate
   rather than "all of it at once", so pouring a heap into one looks like the heap
   draining and not like it vanishing. */
static const int INTAKE_RATE = 3;

static void devIntake(World& w, Device& d) {
    int taken = 0;
    for (int y = d.y - 1; y <= d.y + DEV_H && taken < INTAKE_RATE; ++y) {
        for (int x = d.x - 1; x <= d.x + DEV_W && taken < INTAKE_RATE; ++x) {
            /* Perimeter only -- the interior is the machine itself. */
            const bool edge = (x == d.x - 1 || x == d.x + DEV_W ||
                               y == d.y - 1 || y == d.y + DEV_H);
            if (!edge) continue;
            if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) continue;
            const u8 m = w.at(x, y).mat;
            if (m == MAT_EMPTY) continue;
            const u8 k = MATS[m].kind;
            if (k != KIND_POWDER && k != KIND_LIQUID) continue;
            if (!devTakeInto(d, m)) continue;
            w.setCell(x, y, MAT_EMPTY);
            ++taken;
        }
    }
}

/* Lay up to `value` cells of the buffer into the row under the footprint, left to
   right, skipping anything already occupied. Skipping rather than stopping matters:
   a placer over a partly-filled furnace should top it up, not jam because its
   leftmost outlet happens to be blocked. */
/* The placer, filling the same box. Nearest layer first for the mirror of the
   miner's reason: material laid against the face first builds OUTWARD from the
   machine, so a placer walling something off produces a wall that starts where
   it is bolted rather than a floating sheet with a gap behind it. */
static void devPlaceBox(World& w, Device& d) {
    const int depth = devBoxDepth(d);
    int done = 0;
    for (int layer = 0; layer < depth && done < d.value && d.count > 0; ++layer) {
        for (int i = 0; i < DEV_W && done < d.value && d.count > 0; ++i) {
            int x, y;
            devBoxCell(d, i, layer, &x, &y);
            if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) continue;
            if (w.at(x, y).mat != MAT_EMPTY) continue;
            w.setCell(x, y, d.mat);
            --d.count;
            ++done;
        }
    }
}

/* Take up to `value` cells out of the row under the footprint. Refuses anything
   it cannot hold, and -- importantly -- refuses to eat another MACHINE: a miner
   bolted under a device should not quietly dismantle it. Wall is exempt too, since
   it is the indestructible border. */
/* --- the miner ---------------------------------------------------------------
   Clears its working box, nearest layer first, up to `value` cells per action.

   Nearest-first is the whole reason a depth is useful. A miner bolted to the
   side of a furnace is there to take the SOLIDS out of it, and eating the row
   against its own face before the row behind that means the hole opens toward
   the machine and whatever is left keeps falling into reach. Sweeping the far
   layer first would undercut the pile and leave the near row standing.

   The filter is a single material, and skipping a cell costs no budget -- the
   same rule digInto uses for its whitelist, and for the same reason: a filter
   that spent its bite on the first thing it refused would be a filter that
   does nothing in a mixed pile. */
/* --- what makes a miner or a placer act --------------------------------------
   Two ways in, and they are deliberately different things.

   A SPARK still works. Electricity was the original trigger and a contraption
   built around a thermocouple and a clock should keep running untouched.

   A CIRCUIT SIGNAL is the new one, and it is what makes these usable the way
   Factorio's inserters are: wire a constant 1 to a miner and it runs, wire a
   clock to it and it runs on the clock, wire a decider to it and it runs only
   when the condition holds. The signal read is the device's own configured one
   (CIR_SIG_1 by default -- "the 1 signal"), so the choice of which wire drives
   it is already a control the panel has.

   The two modes are the difference between a level and an edge, which is
   exactly the distinction the thermocouple's latch already draws elsewhere in
   this file:

     WHILE_ON  acts every tick the signal is non-zero. Hold a 1 on the wire and
               it works continuously, which is what you want for clearing out a
               furnace that keeps filling up.
     ON_EDGE   acts once each time the signal goes from zero to non-zero. A
               clock ticking 1/0/1/0 then gives exactly one action per tick
               rather than one per frame the wire happens to be high, which is
               how you meter a placer into laying one row at a time.

   `latched` carries the edge state. It is free on these two types -- only the
   thermocouple and the block watcher were using it -- so this needs no new
   field and no save change. */
static bool devTriggered(int index, Device& d) {
    const int signal = circuitInput(index, g_circuitConfig[index].signal);
    const bool high = signal != 0;
    const int mode = devRunMode(d);

    bool act = d.poked;                     /* a spark always fires it */
    if (mode == DEVRUN_ON_EDGE) {
        if (high && !d.latched) act = true;
    } else if (high) {
        act = true;
    }
    d.latched = high;
    return act;
}

static void devMineBox(World& w, Device& d) {
    const int depth = devBoxDepth(d);
    const int want  = devFilterMat(d);
    int done = 0;
    for (int layer = 0; layer < depth && done < d.value; ++layer) {
        for (int i = 0; i < DEV_W && done < d.value; ++i) {
            int x, y;
            devBoxCell(d, i, layer, &x, &y);
            if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) continue;
            const u8 m = w.at(x, y).mat;
            if (m == MAT_EMPTY || m == MAT_WALL || m == MAT_DEVICE) continue;
            if (want != MAT_EMPTY && m != (u8)want) continue;
            /* Never eat another machine, whatever it is made of -- including a
               torch, whose cells are MAT_TORCH rather than MAT_DEVICE. */
            if (devAt(x, y)) continue;
            if (!devTakeInto(d, m)) return;   /* full, or holding something else */
            w.setCell(x, y, MAT_EMPTY);
            ++done;
        }
    }
}

void devTick(World& w) {
    /* Sparks first, machines second, and the order is the contract. A spark
       arriving this frame sets `poked` on the machine it reaches, and the machine's
       own update below is what consumes it -- so a signal is acted on in the frame
       it lands rather than the frame after, and a device can never see the same
       poke twice. Reversed, every machine in a chain would lag one frame behind the
       one feeding it, which for a sequencing system is exactly the wrong bug to
       have to debug. */
    ++g_sparkFrame;
    /* A u32 wrap needs roughly two years at 60 Hz. Resetting branch stamps at
       that point is enough to retain the "not twice this frame" contract. */
    if (g_sparkFrame == 0) g_sparkFrame = 1;
    sparkRecentBeginFrame();
    for (int i = 0; i < MAX_SPARKS; ++i) {
        Spark& s = g_sparks[i];
        if (!s.used || !w.electricalLive(s.x, s.y) || s.stepped == g_sparkFrame) continue;
        for (int step = 0; step < SPARK_SPEED; ++step) {
            if (!sparkStep(w, s)) {
                s.used = false;
                if (g_pulseFronts[s.pulse]) --g_pulseFronts[s.pulse];
                break;
            }
        }
        s.stepped = g_sparkFrame;
    }
    sparkCrowdHeat(w);
    /* After the fronts have moved and before the devices run: a mote that lands
       on wire this frame starts a pulse, and that pulse should be a front the
       NEXT frame like any other rather than something devices see half-formed. */
    shedTick(w);
    circuitPrepareInputs();

    for (int i = 0; i < MAX_DEVICES; ++i) {
        Device& d = g_devices[i];
        if (!d.used) continue;

        /* A machine's centre chooses its chunk. Its entire 14-cell footprint is
           then frozen with that chunk, so an off-screen clock cannot fire into a
           wire that the material simulation has stopped cooling. */
        if (!w.electricalLive(d.x + DEV_W / 2, d.y + DEV_H / 2)) continue;

        if (!devIntact(w, d)) { devRemove(w, &d); continue; }

        d.firing = false;
        if ((d.type == DEV_DRAIN || d.type == DEV_SPOUT) && d.poked)
            d.enabled = !d.enabled;
        switch (d.type) {
        case DEV_CLOCK: {
            /* Counts frames and fires on the wrap. The phase is kept rather than
               derived from the world frame number, which is what makes two clocks
               with the same period able to run out of step -- and being able to
               offset one against another is the whole basis of sequencing. */
            if (++d.phase >= d.value) { d.phase = 0; d.firing = true; }
            d.reading = d.value ? (i32)(d.value - d.phase) : 0;   /* frames to go */
            circuitSetOutput(i, g_circuitConfig[i].signal, d.firing ? 1 : 0);
            break;
        }
        case DEV_PLACER: {
            /* Fed by POURING onto it. Any loose material touching the footprint is
               drawn into the buffer, which makes loading a placer a thing you do
               with a shovel rather than through a menu -- and means a miner or a
               chute can feed one without either knowing the other exists.

               Only powders and liquids: a placer that ate the stone wall it was
               bolted to would dismantle its own housing. */
            devIntake(w, d);
            d.reading = d.count;
            if (devTriggered(i, d) && d.count > 0) devPlaceBox(w, d);
            break;
        }
        case DEV_MINER: {
            d.reading = d.count;
            if (devTriggered(i, d)) devMineBox(w, d);
            break;
        }
        case DEV_THERMOCOUPLE: {
            const int t = devTemp(w, d);
            d.reading = t - TEMP_OFFSET;             /* report in Celsius */
            const int trip = (int)degC((int)d.value);
            /* An EDGE, not a level, and the latch is what makes it one. A
               thermocouple that announced "still hot" every frame while a furnace
               ran would be useless for sequencing anything -- you want to know
               the moment the charge reached temperature, once.

               Re-arming needs hysteresis or the device chatters: a cell sitting
               exactly on the mark would latch and unlatch on alternate frames as
               conduction nudges it either way. HYST is in stored units, which are
               degrees, so this is "it has to fall five degrees back below the
               mark before it will trip again". */
            static const int HYST = 5;
            if (!d.latched && t >= trip)        { d.firing = true; d.latched = true; }
            else if (d.latched && t < trip - HYST) { d.latched = false; }
            circuitSetOutput(i, g_circuitConfig[i].signal, d.reading);
            break;
        }
        case DEV_DRAIN:
            if (d.enabled) {
                const u8 requested = circuitMaterialFilter(i);
                const i32 saved = d.value;
                if (requested) d.value = requested;
                devDrain(w, d);
                d.value = saved;
            }
            d.reading = d.count;
            break;
        case DEV_SPOUT:
            if (d.enabled) devSpout(w, d);
            d.reading = d.count;
            break;
        case DEV_HIVE:
            devHive(w, d, i);
            break;
        case DEV_BLOCK_WATCHER: {
            const u8 watched = devWatch(w, d);
            const bool hit = watched != MAT_EMPTY;
            d.reading = hit ? 1 : 0;
            if (hit && !d.latched) d.firing = true;
            d.latched = hit;
            if (hit) circuitSetOutput(i, watched, 1);
            break;
        }
        case DEV_PULSE_BUTTON:
            /* Right-click sets poked. Treating a wire pulse identically also
               makes the button a useful visible relay in a test circuit. */
            if (d.poked) d.firing = true;
            circuitSetOutput(i, g_circuitConfig[i].signal, d.firing ? 1 : 0);
            break;
        case DEV_CONSTANT_COMBINATOR:
            d.reading = d.value;
            circuitSetOutput(i, g_circuitConfig[i].signal, d.value);
            break;
        case DEV_ARITHMETIC_COMBINATOR: {
            const CircuitDeviceConfig& c = g_circuitConfig[i];
            const int a = circuitInput(i, c.signalA), b = circuitInput(i, c.signalB);
            d.reading = circuitArithmetic(a, b, c.op);
            circuitSetOutput(i, c.signalOut, d.reading);
            break;
        }
        case DEV_DECIDER_COMBINATOR: {
            const CircuitDeviceConfig& c = g_circuitConfig[i];
            const int a = circuitInput(i, c.signalA), b = circuitInput(i, c.signalB);
            d.reading = circuitCompare(a, b, c.op) ? a : 0;
            circuitSetOutput(i, c.signalOut, d.reading);
            break;
        }
        case DEV_CHEST:
        case DEV_PIPE:
        case DEV_CROSSOVER:
            d.reading = d.count + d.count2; break;
        default: break;
        }

        /* Inventory-bearing machines publish exactly what they are holding.
           This is the Factorio-style information-dense readout: a chest with
           400 Copper contributes `Copper = 400` to its whole circuit network. */
        if (d.count > 0) circuitSetOutput(i, d.mat, d.count);
        if (d.count2 > 0) circuitSetOutput(i, d.mat2, d.count2);

        /* Firing means putting a spark on the wire, if there is one to put it on.
           A machine with nothing wired to it still fires -- the lamp still blinks
           -- because a device that went silent when unwired would be impossible to
           test in isolation, and because seeing it tick is how you work out which
           side to run the wire to. */
        if (d.firing) devEmit(w, d);

        /* The poke is consumed here whether or not this device type does anything
           with one, so a signal can never accumulate. */
        d.poked = false;
    }
    pipeTick(w);
}

static void circuitTerminal(const Device& d, int port, int camX, int camY, int* x, int* y) {
    *x = d.x + (circuitHasSeparatePorts(d.type) && port ? DEV_W - 2 : 1) - camX;
    *y = d.y + DEV_H / 2 - camY;
}

void circuitDraw(u32* px, int camX, int camY, bool lit, int selectedDevice, int selectedPort) {
    (void)lit;
    for (int i = 0; i < MAX_CIRCUIT_WIRES; ++i) {
        const CircuitWire& w = g_circuitWires[i];
        if (!w.used || !g_devices[w.a].used || !g_devices[w.b].used) continue;
        int x0, y0, x1, y1;
        circuitTerminal(g_devices[w.a], w.portA, camX, camY, &x0, &y0);
        circuitTerminal(g_devices[w.b], w.portB, camX, camY, &x1, &y1);
        const u32 col = (w.a == selectedDevice && w.portA == selectedPort) ||
                        (w.b == selectedDevice && w.portB == selectedPort) ? 0xFFE87A : 0xA870E8;
        const int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        const int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            if (x0 >= 0 && x0 < VIEW_CELLS_W && y0 >= 0 && y0 < VIEW_CELLS_H)
                px[y0 * VIEW_CELLS_W + x0] = col;
            if (x0 == x1 && y0 == y1) break;
            const int e2 = err * 2;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
    /* A selected first endpoint needs a world-space acknowledgement too; panel
       text alone makes a long cable setup feel as though the first click was
       eaten. */
    if (selectedDevice >= 0 && selectedDevice < MAX_DEVICES && g_devices[selectedDevice].used) {
        int cx, cy;
        circuitTerminal(g_devices[selectedDevice], selectedPort, camX, camY, &cx, &cy);
        for (int d = -3; d <= 3; ++d) {
            const int x = cx + d, y = cy + d;
            if (x >= 0 && x < VIEW_CELLS_W && cy >= 0 && cy < VIEW_CELLS_H) px[cy * VIEW_CELLS_W + x] = 0xFFE87A;
            if (cx >= 0 && cx < VIEW_CELLS_W && y >= 0 && y < VIEW_CELLS_H) px[y * VIEW_CELLS_W + cx] = 0xFFE87A;
        }
    }
}

void devDraw(const World& w, u32* px, int camX, int camY, bool lit) {
    (void)w;
    /* Fixtures are drawn separately from machines because they deliberately
       have no Device record to tick, wire, or occupy a circuit slot. */
    const u32* torchArt = g_sprite[DEVS[DEV_TORCH].sprite];
    for (int i = 0; i < (int)g_torches.size(); ++i) {
        const int bx = g_torches[i].x - camX, by = g_torches[i].y - camY;
        if (bx + DEV_W <= 0 || by + DEV_H <= 0 || bx >= VIEW_CELLS_W || by >= VIEW_CELLS_H) continue;
        for (int yy = 0; yy < DEV_H; ++yy) for (int xx = 0; xx < DEV_W; ++xx) {
            const u32 c = torchArt[yy * SPR_W + xx];
            const int vx = bx + xx, vy = by + yy;
            if (c == 0 || vx < 0 || vx >= VIEW_CELLS_W || vy < 0 || vy >= VIEW_CELLS_H) continue;
            px[vy * VIEW_CELLS_W + vx] = lit ? shadeColor(c, viewShade(vx, vy)) : c;
        }
    }
    for (int i = 0; i < MAX_DEVICES; ++i) {
        const Device& d = g_devices[i];
        if (!d.used) continue;

        /* Cull against the view before touching any pixels. With the whole list
           walked every frame this is what keeps a world full of machines from
           costing anything while you are somewhere else. */
        const int bx = d.x - camX, by = d.y - camY;
        if (bx + DEV_W <= 0 || by + DEV_H <= 0) continue;
        if (bx >= VIEW_CELLS_W || by >= VIEW_CELLS_H) continue;

        /* Pipes are drawn as connections, not as little black boxes.  The
           underlying footprint remains a solid machine for the simulation;
           only the visible conduit changes as neighbours are placed. */
        if (d.type == DEV_PIPE || d.type == DEV_CROSSOVER) {
            bool up = false, down = false, left = false, right = false;
            for (int j = 0; j < MAX_DEVICES; ++j) {
                const Device& o = g_devices[j]; bool vertical = false;
                if (!o.used || !isLogistics(o.type) || !pipeJoined(d, o, &vertical)) continue;
                if (vertical) { if (o.y < d.y) up = true; else down = true; }
                else          { if (o.x < d.x) left = true; else right = true; }
            }
            const u32 col = (d.type == DEV_CROSSOVER) ? 0xB8D8E8 : 0x70C8E8;
            for (int yy = 5; yy <= 8; ++yy) for (int xx = 5; xx <= 8; ++xx) {
                const int vx = bx + xx, vy = by + yy;
                if (vx >= 0 && vx < VIEW_CELLS_W && vy >= 0 && vy < VIEW_CELLS_H) px[vy * VIEW_CELLS_W + vx] = col;
            }
            for (int k = 0; k < 14; ++k) {
                if (up || (d.type == DEV_PIPE && !down && !left && !right)) for (int xx = 6; xx <= 7; ++xx) if (bx + xx >= 0 && bx + xx < VIEW_CELLS_W && by + k >= 0 && by + k < VIEW_CELLS_H && k <= 5) px[(by + k) * VIEW_CELLS_W + bx + xx] = col;
                if (down) for (int xx = 6; xx <= 7; ++xx) if (bx + xx >= 0 && bx + xx < VIEW_CELLS_W && by + k >= 0 && by + k < VIEW_CELLS_H && k >= 8) px[(by + k) * VIEW_CELLS_W + bx + xx] = col;
                if (left) for (int yy = 6; yy <= 7; ++yy) if (bx + k >= 0 && bx + k < VIEW_CELLS_W && by + yy >= 0 && by + yy < VIEW_CELLS_H && k <= 5) px[(by + yy) * VIEW_CELLS_W + bx + k] = col;
                if (right) for (int yy = 6; yy <= 7; ++yy) if (bx + k >= 0 && bx + k < VIEW_CELLS_W && by + yy >= 0 && by + yy < VIEW_CELLS_H && k >= 8) px[(by + yy) * VIEW_CELLS_W + bx + k] = col;
            }
            continue;
        }

        /* --- what is standing on it ---------------------------------------
           Drawn as the item's own icon, floating clear of the plinth, and it is
           drawn INSTEAD of nothing rather than as a colour swatch because the
           whole object exists to answer "what is that" from across a room. A
           generic glow would make every pedestal in the world the same
           discovery, which is the opposite of the point.

           Unlit, deliberately. Everything else in this function shades by the
           light field; the reward does not, so it reads at full strength in an
           unlit cavern -- it is its own light source (see devRegisterLights),
           and shading it by the illumination it is providing would dim it in
           exactly the dark where it most needs to be seen. */
        if (d.type == DEV_PEDESTAL) {
            const ItemId held = pedestalItem(d);
            if (held != ITEM_NONE) {
                const u8 spr = ITEMS[held].sprite;
                const int liftY = by - 10;
                if (spr != SPR_NONE) {
                    const u32* icon = g_sprite[spr];
                    for (int yy = 0; yy < SPR_H; ++yy) for (int xx = 0; xx < SPR_W; ++xx) {
                        const u32 c = icon[yy * SPR_W + xx];
                        const int vx = bx + xx, vy = liftY + yy;
                        if (c == 0 || vx < 0 || vx >= VIEW_CELLS_W || vy < 0 || vy >= VIEW_CELLS_H) continue;
                        px[vy * VIEW_CELLS_W + vx] = c;
                    }
                } else {
                    /* A material has no icon of its own -- see sprite.h -- so it
                       gets its colour as a solid lozenge. Materials are what a
                       pedestal holds least often and the fallback only has to be
                       legible, not characterful. */
                    const u32 c = ITEMS[held].colour;
                    for (int yy = 4; yy < 10; ++yy) for (int xx = 4; xx < 10; ++xx) {
                        const int vx = bx + xx, vy = liftY + yy;
                        if (vx < 0 || vx >= VIEW_CELLS_W || vy < 0 || vy >= VIEW_CELLS_H) continue;
                        px[vy * VIEW_CELLS_W + vx] = c;
                    }
                }
            }
        }

        const u32* art = g_sprite[DEVS[d.type].sprite];
        for (int yy = 0; yy < DEV_H; ++yy) {
            for (int xx = 0; xx < DEV_W; ++xx) {
                u32 c = art[yy * SPR_W + xx];
                if (c == 0) continue;
                int rx = xx, ry = yy;
                /* The source art faces down. Rotate only the two directional
                   logistics machines; other device sprites remain upright. */
                /* The spout alone. A drain is symmetric under a quarter turn
                   and has no facing to rotate to -- see ART_DRAIN. */
                if (d.type == DEV_SPOUT) {
                    switch (d.face) {
                    case 1: rx = DEV_W - 1 - xx; ry = DEV_H - 1 - yy; break;
                    case 2: rx = DEV_W - 1 - yy; ry = xx; break;
                    case 3: rx = yy; ry = DEV_H - 1 - xx; break;
                    default: break;
                    }
                }
                const int vx = bx + rx, vy = by + ry;
                if (vx < 0 || vx >= VIEW_CELLS_W) continue;
                if (vy < 0 || vy >= VIEW_CELLS_H) continue;
                px[vy * VIEW_CELLS_W + vx] = lit ? shadeColor(c, viewShade(vx, vy)) : c;
            }
        }

        /* Combinators have two real circuit terminals. Violet input on the
           left, gold output on the right: the colour and placement make it
           possible to read a feedback loop from the world without opening UI. */
        if (circuitHasSeparatePorts(d.type)) {
            const int ty = by + DEV_H / 2;
            if (bx + 1 >= 0 && bx + 1 < VIEW_CELLS_W && ty >= 0 && ty < VIEW_CELLS_H)
                px[ty * VIEW_CELLS_W + bx + 1] = 0xB070E8;
            if (bx + DEV_W - 2 >= 0 && bx + DEV_W - 2 < VIEW_CELLS_W && ty >= 0 && ty < VIEW_CELLS_H)
                px[ty * VIEW_CELLS_W + bx + DEV_W - 2] = 0xFFE87A;
        }

        /* The indicator lamp. Drawn rather than baked into the art because it is
           STATE, and it is the only feedback that a machine is doing anything --
           without it a contraption is a row of identical boxes and there is no way
           to see which part of it just acted. Deliberately NOT shaded by the light
           field: it is a lamp, so it is the one thing on a device that should read
           in an unlit cave. */
        if (d.firing || d.latched) {
            const u32 lamp = d.firing ? 0xFFFFC0 : 0x8A3A2A;
            for (int yy = 0; yy < 2; ++yy)
                for (int xx = 0; xx < 2; ++xx) {
                    const int vx = bx + DEV_W - 3 + xx, vy = by + 2 + yy;
                    if (vx < 0 || vx >= VIEW_CELLS_W || vy < 0 || vy >= VIEW_CELLS_H) continue;
                    px[vy * VIEW_CELLS_W + vx] = lamp;
                }
        }
    }
}
