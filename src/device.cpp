#include "device.h"
#include "sprite.h"
#include "light.h"

Device g_devices[MAX_DEVICES];

Spark g_sparks[MAX_SPARKS];

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
    { "Thermocouple", "trips at", "C", -20, 210, 5, 150, SPR_THERMO },
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
    { "Clock", "every", "frames", 6, 600, 6, 60, SPR_CLOCK },
    /* Placer and miner. Their one number is CELLS PER PULSE, which is the right
       axis for both: it is the difference between a machine that trickles and one
       that empties itself in a few ticks, and it is the number you tune when a
       contraption is running at the wrong rate. 14 is the width of the footprint,
       so a pulse of 14 lays or lifts exactly one full row underneath. */
    { "Placer", "places", "cells", 1, 14, 1, 4, SPR_PLACER },
    { "Miner",  "mines",  "cells", 1, 14, 1, 4, SPR_MINER  },
};

int sparkCount() {
    int n = 0;
    for (int i = 0; i < MAX_SPARKS; ++i) if (g_sparks[i].used) ++n;
    return n;
}

void sparkClear() {
    for (int i = 0; i < MAX_SPARKS; ++i) g_sparks[i].used = false;
}

bool sparkAdd(int x, int y, int dx, int dy) {
    for (int i = 0; i < MAX_SPARKS; ++i) {
        Spark& s = g_sparks[i];
        if (s.used) continue;
        s.x = x; s.y = y;
        s.dx = (i16)dx; s.dy = (i16)dy;
        s.life = SPARK_LIFE;
        s.used = true;
        return true;
    }
    return false;
}

static inline bool conducts(const World& w, int x, int y) {
    if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) return false;
    return g_matConducts[w.at(x, y).mat] != 0;
}

/* Heat left behind where a spark is spat out. Small on purpose: a spark is
   energy, and having it warm the end of a wire is the detail that makes the
   system feel physical rather than symbolic. It is NOT meant to be a heat source
   -- one spark a second lands nowhere near what a heater delivers, and if
   electricity became the cheap way to run a furnace the whole ore process would
   collapse into "wire it up". Flavour, with a deliberate ceiling. */
static const int SPARK_HEAT = 4;

/* Move one spark one step. Returns false when it has finished, either by being
   spent, by triggering a machine, or by running out of conductor.

   Straight on first, then the two turns, and NEVER back the way it came.
   Preferring the current heading is what makes a wire behave as drawn; without it
   a spark at a bend picks by array order and can crawl backwards along a straight
   run.

   Reversal was allowed at first, as a "last resort" for backing out of a dead-end
   stub, and it made a straight wire into a resonator: the spark reached the end,
   found the cell behind it conductive, turned round, ran back, turned round again,
   and bounced until its lifetime expired. Measured, a 101-cell wire held one spark
   for the full 400-frame test rather than the ~100 frames it should take, and a
   two-cell stub held one for all 600 frames of its life. The mistake was thinking
   a wire END is a dead end to escape from. It is not -- it is where the spark is
   SPAT OUT, which is the behaviour being implemented. */
static bool sparkStep(World& w, Spark& s) {
    if (--s.life <= 0) return false;

    /* Candidate steps, in order of preference. */
    int cand[3][2];
    int n = 0;
    cand[n][0] = s.dx;  cand[n][1] = s.dy;  ++n;          /* straight on */
    /* The two perpendiculars. For an axis-aligned heading one of dx/dy is zero,
       so swapping them gives the turns. */
    cand[n][0] = -s.dy; cand[n][1] = -s.dx; ++n;
    cand[n][0] =  s.dy; cand[n][1] =  s.dx; ++n;

    for (int k = 0; k < n; ++k) {
        const int nx = s.x + cand[k][0], ny = s.y + cand[k][1];
        if (nx == s.x && ny == s.y) continue;
        if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1) continue;

        /* A machine is a SINK, not wire. Reaching one delivers the spark and ends
           its journey -- which is what makes "wire the clock to the placer" mean
           something. Devices deliberately do not conduct: a bank of machines
           bolted together would otherwise act as one big lump of cable and a spark
           would wander through all of them. */
        Device* d = devAt(nx, ny);
        if (d) { d->poked = true; ++d->received; return false; }

        if (!conducts(w, nx, ny)) continue;
        s.x = nx; s.y = ny;
        s.dx = (i16)cand[k][0]; s.dy = (i16)cand[k][1];
        return true;
    }

    /* Nowhere to go: spat out. The heat goes into the cell it was heading into if
       that is open, and otherwise into the conductor it died in. */
    const int ox = s.x + s.dx, oy = s.y + s.dy;
    const bool openAhead = ox >= PLAY_X0 && ox <= PLAY_X1 && oy >= PLAY_Y0 && oy <= PLAY_Y1
                           && w.at(ox, oy).mat == MAT_EMPTY;
    w.heat(openAhead ? ox : s.x, openAhead ? oy : s.y, 1, SPARK_HEAT);
    return false;
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

bool devPlace(World& w, u8 type, int cx, int cy) {
    if (type >= DEV_COUNT) return false;
    const int x0 = devOriginX(cx), y0 = devOriginY(cy);
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
    d.phase   = 0;
    d.reading = 0;
    d.received = 0;
    d.mat     = MAT_EMPTY;
    d.count   = 0;
    d.used    = true;

    for (int y = y0; y < y0 + DEV_H; ++y)
        for (int x = x0; x < x0 + DEV_W; ++x) w.setCell(x, y, MAT_DEVICE);
    return true;
}

void devRemove(World& w, Device* d) {
    if (!d || !d->used) return;
    for (int y = d->y; y < d->y + DEV_H; ++y)
        for (int x = d->x; x < d->x + DEV_W; ++x)
            if (w.at(x, y).mat == MAT_DEVICE) w.setCell(x, y, MAT_EMPTY);
    d->used = false;
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
        if (w.at(xs[k], ys[k]).mat != MAT_DEVICE) return false;
    return true;
}


/* --- the buffer ------------------------------------------------------------
   A placer draws loose material touching its footprint into store; a miner puts
   what it breaks there. Both share one rule about mixing: a buffer holds a single
   material, and anything else is refused rather than blended. Silently converting
   one material into another inside a machine is the kind of behaviour that makes a
   contraption impossible to trust. */
static bool devTakeInto(Device& d, u8 mat) {
    if (d.count >= DEV_CAP) return false;
    if (d.count > 0 && d.mat != mat) return false;
    d.mat = mat;
    ++d.count;
    return true;
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
    const int y = d.y + DEV_H;
    if (y > PLAY_Y1) return;
    int done = 0;
    for (int x = d.x; x < d.x + DEV_W && done < d.value && d.count > 0; ++x) {
        if (x < PLAY_X0 || x > PLAY_X1) continue;
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
    const int y = d.y + DEV_H;
    if (y > PLAY_Y1) return;
    int done = 0;
    for (int x = d.x; x < d.x + DEV_W && done < d.value; ++x) {
        if (x < PLAY_X0 || x > PLAY_X1) continue;
        const u8 m = w.at(x, y).mat;
        if (m == MAT_EMPTY || m == MAT_WALL || m == MAT_DEVICE) continue;
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
    for (int i = 0; i < MAX_SPARKS; ++i) {
        Spark& s = g_sparks[i];
        if (!s.used) continue;
        for (int step = 0; step < SPARK_SPEED; ++step) {
            if (!sparkStep(w, s)) { s.used = false; break; }
        }
    }

    for (int i = 0; i < MAX_DEVICES; ++i) {
        Device& d = g_devices[i];
        if (!d.used) continue;

        if (!devIntact(w, d)) { devRemove(w, &d); continue; }

        d.firing = false;
        switch (d.type) {
        case DEV_CLOCK: {
            /* Counts frames and fires on the wrap. The phase is kept rather than
               derived from the world frame number, which is what makes two clocks
               with the same period able to run out of step -- and being able to
               offset one against another is the whole basis of sequencing. */
            if (++d.phase >= d.value) { d.phase = 0; d.firing = true; }
            d.reading = d.value ? (i32)(d.value - d.phase) : 0;   /* frames to go */
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
            break;
        }
        default: break;
        }

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

        const u32* art = g_sprite[DEVS[d.type].sprite];
        for (int yy = 0; yy < DEV_H; ++yy) {
            const int vy = by + yy;
            if (vy < 0 || vy >= VIEW_CELLS_H) continue;
            for (int xx = 0; xx < DEV_W; ++xx) {
                u32 c = art[yy * SPR_W + xx];
                if (c == 0) continue;
                const int vx = bx + xx;
                if (vx < 0 || vx >= VIEW_CELLS_W) continue;
                px[vy * VIEW_CELLS_W + vx] = lit ? shadeColor(c, viewShade(vx, vy)) : c;
            }
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
