#include "device.h"
#include "sprite.h"
#include "light.h"

Device g_devices[MAX_DEVICES];

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
};

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
    d.latched = false;
    d.reading = 0;
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

void devTick(World& w) {
    for (int i = 0; i < MAX_DEVICES; ++i) {
        Device& d = g_devices[i];
        if (!d.used) continue;

        if (!devIntact(w, d)) { devRemove(w, &d); continue; }

        d.firing = false;
        switch (d.type) {
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
