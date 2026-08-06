#include "device.h"
#include "door.h"
#include "sprite.h"
#include "light.h"
#include "projectile.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>   /* fabsf, for the shed spark's fall */

Device g_devices[MAX_DEVICES];

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

void circuitRemapMaterials(const u8* remap) {
    if (!remap) return;
    for (int i = 0; i < MAX_DEVICES; ++i) {
        CircuitDeviceConfig& c = g_circuitConfig[i];
        if (c.signal < MAT_COUNT) c.signal = remap[c.signal];
        if (c.signalA < MAT_COUNT) c.signalA = remap[c.signalA];
        if (c.signalB < MAT_COUNT) c.signalB = remap[c.signalB];
        if (c.signalOut < MAT_COUNT) c.signalOut = remap[c.signalOut];
    }
}

/* A direct pulse stamp is deliberately separate from Cell. It costs 24 MiB for
   this 4096 x 3072 world, but makes a branch test one cache-friendly read/write
   instead of an allocation-heavy set lookup on every wire step. The table is
   cleared only when the 16-bit pulse id wraps (normally after many minutes of
   continuous clocks), never per frame. At that very rare boundary active fronts
   are cleared too, rather than allowing an old and newly-issued id to collide. */
static u16 g_pulseMark[SIM_W * SIM_H];
static u16 g_nextPulse = 1;
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
    { "Spout", "rate", "cells", 1, 14, 1, 4, SPR_SPOUT, MAT_DEVICE, true },
    { "Drain", "filter", "id", 0, MAT_COUNT - 1, 1, 0, SPR_DRAIN, MAT_DEVICE, true },
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
};

int sparkCount() {
    int n = 0;
    for (int i = 0; i < MAX_SPARKS; ++i) if (g_sparks[i].used) ++n;
    return n;
}

void sparkClear() {
    for (int i = 0; i < MAX_SPARKS; ++i) g_sparks[i].used = false;
    /* The motes go too. They are shed BY this system and feed back into it, so
       leaving them alive across a clear would let a new world be energised by
       the last one's loose ends. */
    shedClear();
    memset(g_pulseMark, 0, sizeof(g_pulseMark));
    memset(g_sparkLoad, 0, sizeof(g_sparkLoad));
    g_nextPulse = 1;
    g_sparkFrame = 0;
    g_nextSparkSlot = 0;
}

static u16 nextPulse() {
    /* Zero is the unvisited stamp. Before we would reuse an id, stop the tiny
       set of active fronts and begin a fresh generation; otherwise an old front
       could mistake a new mark for its own and re-enter a loop. */
    if (g_nextPulse == 0xFFFFu) sparkClear();
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

/* Defined further down with the rest of the mote code; declared here because
   sparkStep sheds one and sparkClear disposes of them, and both come first. */
static void shedAdd(int x, int y, int dx, int dy);

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
    for (int k = 0; k < n; ++k) {
        const int nx = fromX + cand[k][0], ny = fromY + cand[k][1];
        if (nx == fromX && ny == fromY) continue;
        if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1) continue;

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
        if (g_pulseMark[mark] == s.pulse) continue;
        g_pulseMark[mark] = s.pulse;

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

    /* Nowhere to go: spat out. The heat goes into the cell it was heading into if
       that is open, and otherwise into the conductor it died in. */
    const int ox = s.x + s.dx, oy = s.y + s.dy;
    const bool openAhead = ox >= PLAY_X0 && ox <= PLAY_X1 && oy >= PLAY_Y0 && oy <= PLAY_Y1
                           && w.at(ox, oy).mat == MAT_EMPTY;
    const int hx = openAhead ? ox : s.x, hy = openAhead ? oy : s.y;
    w.heat(hx, hy, 1, SPARK_HEAT);
    sparkArcIfHot(w, hx, hy);
    /* And it SPITS, when there is open air to spit into. A wire buried in rock
       or butted against a machine just stops; a cut end hanging in a room drops
       a mote out of it. See ShedSpark -- what makes this more than decoration is
       that the mote energises any bare conductor it lands on, so where you leave
       your cable ends becomes something the world has an opinion about. */
    if (openAhead) shedAdd(ox, oy, s.dx, s.dy);
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
        if (!s.used) continue;
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
        const bool c = conducts(w, x, d.y - 1);
        if (c && !run && sparkAdd(x, d.y - 1, 0, -1)) ++sent;
        run = c;
    }
    run = false;
    for (int x = d.x; x < d.x + DEV_W; ++x) {
        const bool c = conducts(w, x, d.y + DEV_H);
        if (c && !run && sparkAdd(x, d.y + DEV_H, 0, 1)) ++sent;
        run = c;
    }
    run = false;
    for (int y = d.y; y < d.y + DEV_H; ++y) {
        const bool c = conducts(w, d.x - 1, y);
        if (c && !run && sparkAdd(d.x - 1, y, -1, 0)) ++sent;
        run = c;
    }
    run = false;
    for (int y = d.y; y < d.y + DEV_H; ++y) {
        const bool c = conducts(w, d.x + DEV_W, y);
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
static void shedAdd(int x, int y, int dx, int dy) {
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
        return;
    }
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
    return n;
}

void devClear() {
    for (int i = 0; i < MAX_DEVICES; ++i) g_devices[i].used = false;
    circuitClear();
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

    /* And nothing solid may be in the way. Powders and liquids ARE allowed to be
       displaced -- you should be able to bolt a machine into a heap of sand or a
       shallow pool without excavating first -- but rock and other machinery are
       not, so a device can never be shoved into a wall. */
    for (int y = y0; y < y0 + DEV_H; ++y)
        for (int x = x0; x < x0 + DEV_W; ++x) {
            const u8 k = MATS[w.at(x, y).mat].kind;
            if (k == KIND_STATIC) return false;
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
    return true;
}

void devRemove(World& w, Device* d) {
    if (!d || !d->used) return;
    const int index = (int)(d - g_devices);
    for (int y = d->y; y < d->y + DEV_H; ++y)
        for (int x = d->x; x < d->x + DEV_W; ++x)
            if (w.at(x, y).mat == DEVS[d->type].cellMat) w.setCell(x, y, MAT_EMPTY);
    d->used = false;
    circuitRemoveDevice(index);
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
    const int xs[5] = { d.x, d.x + DEV_W - 1, d.x, d.x + DEV_W - 1, d.x + DEV_W / 2 };
    const int ys[5] = { d.y, d.y, d.y + DEV_H - 1, d.y + DEV_H - 1, d.y + DEV_H / 2 };
    for (int k = 0; k < 5; ++k)
        if (w.at(xs[k], ys[k]).mat != DEVS[d.type].cellMat) return false;
    return true;
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
    return type == DEV_PIPE || type == DEV_CROSSOVER || type == DEV_SPOUT;
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

static void lane(Device& d, bool vertical, u8** mat, i32** count) {
    if (d.type == DEV_CROSSOVER && !vertical) { *mat = &d.mat2; *count = &d.count2; }
    else { *mat = &d.mat; *count = &d.count; }
}

/* A pipe moves four items per frame.  It is intentionally a simple push system:
   networks do not need a global flood-fill or per-frame graph allocation, and a
   line of pipes visibly fills from the chest toward the consumer. */
static void pipeTick() {
    if (g_logisticsUiOpen) return;
    static const int PIPE_RATE = 4;
    for (int i = 0; i < MAX_DEVICES; ++i) {
        Device& src = g_devices[i];
        if (!src.used || !pipeSends(src.type)) continue;
        for (int j = 0; j < MAX_DEVICES; ++j) {
            if (i == j || j == src.pipeFrom) continue;
            Device& dst = g_devices[j];
            if (!dst.used || !pipeReceives(dst.type)) continue;
            bool vertical = false;
            if (!pipeJoined(src, dst, &vertical)) continue;
            u8 *sm, *dm; i32 *sc, *dc;
            lane(src, vertical, &sm, &sc); lane(dst, vertical, &dm, &dc);
            const int cap = dst.type == DEV_CHEST ? CHEST_CAP : DEV_CAP;
            if (*sc <= 0 || (*dc > 0 && *dm != *sm) || *dc >= cap) continue;
            const int n = imin(PIPE_RATE, imin((int)*sc, cap - (int)*dc));
            *dm = *sm; *dc += n; *sc -= n;
            if (*sc == 0) *sm = MAT_EMPTY;
            dst.pipeFrom = (i16)i;
            break;
        }
    }
}

static void devDrain(World& w, Device& d) {
    int taken = 0;
    for (int i = 0; i < DEV_W && taken < 4; ++i) {
        int x, y; devFaceCell(d, i, &x, &y);
        if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) continue;
        const u8 m = w.at(x, y).mat;
        if (m == MAT_EMPTY || m == MAT_WALL || m == MAT_DEVICE || (d.value && m != d.value)) continue;
        if (!devTakeInto(d, m)) break;
        w.setCell(x, y, MAT_EMPTY); ++taken;
    }
}

static void devSpout(World& w, Device& d) {
    int done = 0;
    for (int i = 0; i < DEV_W && done < d.value && d.count > 0; ++i) {
        int x, y; devFaceCell(d, i, &x, &y);
        if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1 || w.at(x, y).mat != MAT_EMPTY) continue;
        w.setCell(x, y, d.mat); --d.count; ++done;
    }
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
    g_pulseMark[y * SIM_W + x] = pulse;
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
static void devPlaceRow(World& w, Device& d) {
    int done = 0;
    for (int i = 0; i < DEV_W && done < d.value && d.count > 0; ++i) {
        int x, y;
        devFaceCell(d, i, &x, &y);
        if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) continue;
        if (w.at(x, y).mat != MAT_EMPTY) continue;
        w.setCell(x, y, d.mat);
        --d.count;
        ++done;
    }
}

/* Take up to `value` cells out of the row under the footprint. Refuses anything
   it cannot hold, and -- importantly -- refuses to eat another MACHINE: a miner
   bolted under a device should not quietly dismantle it. Wall is exempt too, since
   it is the indestructible border. */
static void devMineRow(World& w, Device& d) {
    int done = 0;
    for (int i = 0; i < DEV_W && done < d.value; ++i) {
        int x, y;
        devFaceCell(d, i, &x, &y);
        if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) continue;
        const u8 m = w.at(x, y).mat;
        if (m == MAT_EMPTY || m == MAT_WALL || m == MAT_DEVICE) continue;
        /* Never eat another machine, whatever it is made of -- including a torch,
           whose cells are MAT_TORCH rather than MAT_DEVICE. */
        if (devAt(x, y)) continue;
        if (!devTakeInto(d, m)) break;      /* full, or holding something else */
        w.setCell(x, y, MAT_EMPTY);
        ++done;
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
    for (int i = 0; i < MAX_SPARKS; ++i) {
        Spark& s = g_sparks[i];
        if (!s.used || s.stepped == g_sparkFrame) continue;
        for (int step = 0; step < SPARK_SPEED; ++step) {
            if (!sparkStep(w, s)) { s.used = false; break; }
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
            if (d.poked && d.count > 0) devPlaceRow(w, d);
            break;
        }
        case DEV_MINER: {
            d.reading = d.count;
            if (d.poked) devMineRow(w, d);
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
    pipeTick();
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

        const u32* art = g_sprite[DEVS[d.type].sprite];
        for (int yy = 0; yy < DEV_H; ++yy) {
            for (int xx = 0; xx < DEV_W; ++xx) {
                u32 c = art[yy * SPR_W + xx];
                if (c == 0) continue;
                int rx = xx, ry = yy;
                /* The source art faces down. Rotate only the two directional
                   logistics machines; other device sprites remain upright. */
                if (d.type == DEV_SPOUT || d.type == DEV_DRAIN) {
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
