/* --- does the heat lamp warm what it is pointed at, and nothing else? ------

   The lamp's whole promise is that it is SAFE to leave running: it warms a
   cone toward a setpoint and then stops. So the properties worth pinning are
   as much about what it refuses to do as what it does.

     1. It warms cells in front of it, air included -- warming the air is the
        mechanism, not a side effect, and it is what melts a wax pile sitting
        in the warmed air rather than in the beam.
     2. It stops AT the setpoint. A lamp that kept pushing would be a furnace
        with extra steps.
     3. It cannot be set above 100 C, and cannot reach a smelting temperature
        even if something writes a larger number into its setpoint directly.
     4. It does not heat through walls, and the cone it PAINTS is the cone it
        HEATS -- a red beam through solid rock would be teaching a lie.
     5. It melts beeswax, which is the errand it was built for.

   Compile with every source file except main.cpp. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "device.h"
#include <stdio.h>

static int failures = 0;

static void check(bool ok, const char* what) {
    printf("  %-54s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static const int LX = 1200, LY = 5000;

/* A lamp at (LX, LY) pointing right (face 3). */
static Device* lamp(int setpoint) {
    devClear();
    g_world.reset();
    g_world.setLiveWindow(LX - 120, LY - 120, LX + 120, LY + 120);
    if (!devPlace(g_world, DEV_HEAT_LAMP, LX, LY)) return 0;
    Device* d = devAt(LX, LY);
    if (!d) return 0;
    d->face  = 3;
    d->value = setpoint;
    return d;
}

static void run(int frames) {
    for (int i = 0; i < frames; ++i) { devTick(g_world); g_world.step(); }
}

static int tempAt(int x, int y) { return (int)g_world.temp[y * SIM_W + x]; }

int main() {
    initMaterials();
    initItems();

    /* --- 1 and 2. it warms the air ahead, and stops at the setpoint -------- */
    {
        Device* d = lamp(60);
        if (!d) { fprintf(stderr, "could not place a lamp\n"); return 2; }
        const int probeX = LX + 20, probeY = LY;
        check(g_world.at(probeX, probeY).mat == MAT_EMPTY, "the probe cell is open air");
        check(tempAt(probeX, probeY) == AMBIENT_TEMP, "and starts at ambient");

        run(600);
        const int warmed = tempAt(probeX, probeY);
        check(warmed > AMBIENT_TEMP, "the lamp warms open air in front of it");
        check(warmed <= degC(60), "and never past its setpoint");

        run(2400);
        check(tempAt(probeX, probeY) <= degC(60),
              "still at the setpoint after a long run");
    }

    /* --- 3. the ceiling holds even against a bad setpoint ------------------ */
    {
        Device* d = lamp(60);
        if (!d) return 2;
        /* Straight past the panel's own limit, the way a corrupt save or a
           future edit might. The clamp has to live in the device, not in the
           UI that happens to be in front of it today. */
        d->value = 900;
        run(3000);
        const int hot = tempAt(LX + 20, LY);
        check(hot <= degC(100), "100 C is a hard ceiling, whatever the setpoint says");
        check(hot < (int)MATS[MAT_CLAY].boilTemp || MATS[MAT_CLAY].boilTemp == 0,
              "and stays under anything that fires or smelts");
    }

    /* --- 4. walls cast shadows, and the drawn cone is the heated cone ------ */
    {
        Device* d = lamp(100);
        if (!d) return 2;
        /* A stone wall across the beam. Note the cone starts at the device's
           EDGE, not its centre: a 14-cell footprint placed at LX means the
           beam leaves from LX + 7, so probes have to be beyond that. */
        const int wallX = LX + 30;
        for (int y = LY - 60; y <= LY + 60; ++y) g_world.setCell(wallX, y, MAT_STONE);
        run(1800);

        const int inFront = tempAt(LX + 15,   LY) - AMBIENT_TEMP;
        const int behind  = tempAt(wallX + 6, LY) - AMBIENT_TEMP;
        printf("    in front %d above ambient, behind the wall %d\n", inFront, behind);
        check(inFront > 0, "cells in front of the wall are warmed");
        /* Not ZERO behind, and that is a correction rather than a regression.
           Air used to be very nearly an insulator, so nothing crossed a wall at
           all; now that solids and air exchange properly, stone conducts and a
           wall with a lamp on one side does get warm on the other. That is
           right. What must still hold is that the BEAM stops -- asserted
           directly against the cone below -- and that the far side is a faint
           fraction of the near side rather than lit as brightly. */
        check(behind * 4 < inFront, "and the far side is only faintly warmed");

        /* The overlay and the heating must agree, because they are the same
           list -- assert that rather than trusting the comment. */
        static i32 cone[HEAT_LAMP_MAX_CELLS];
        const int n = heatLampCells(g_world, *d, cone, HEAT_LAMP_MAX_CELLS);
        bool anyBehind = false;
        for (int i = 0; i < n; ++i)
            if ((cone[i] % SIM_W) > LX + 30) anyBehind = true;
        check(n > 0, "the cone has cells in it");
        check(!anyBehind, "and none of them are behind the wall");
    }

    /* --- 5. the errand it was built for ------------------------------------ */
    {
        Device* d = lamp(80);
        if (!d) return 2;
        for (int k = 0; k < 6; ++k) g_world.setCell(LX + 20 + k, LY, MAT_BEESWAX);
        bool melted = false;
        for (int f = 0; f < 4000 && !melted; ++f) {
            devTick(g_world); g_world.step();
            for (int k = 0; k < 6; ++k)
                if (g_world.at(LX + 20 + k, LY).mat == MAT_BEESWAX_MELT) melted = true;
        }
        check(melted, "a lamp melts beeswax");

        /* --- and it STAYS melted ------------------------------------------
           The bug this is really guarding. A phase change costs LATENT_HEAT,
           and latentDrain clamps the result at ambient -- so a solid melting
           anywhere above ambient lands at exactly ambient, whatever its
           melting point was. Give the melt a freezing point above ambient and
           it refreezes on the very next frame, forever: melt, drop to ambient,
           refreeze, reheat. On screen that is wax under a lamp at 100 C that
           simply will not melt, which is exactly how it was reported.

           So: run it long, and require the solid to be GONE rather than merely
           to have flickered once. */
        for (int f = 0; f < 6000; ++f) { devTick(g_world); g_world.step(); }
        int stillSolid = 0;
        for (int k = 0; k < 6; ++k)
            if (g_world.at(LX + 20 + k, LY).mat == MAT_BEESWAX) ++stillSolid;
        check(stillSolid == 0, "and none of it is left sitting there solid");
    }

    /* --- 6. melted wax STAYS melted --------------------------------------
       The bug this is really guarding, and it needs a pocket the melt cannot
       run out of -- in open air it escapes on whichever frame it happens to be
       liquid, so the wax clears even while flickering and the fault hides.

       A phase change costs LATENT_HEAT (30) and latentDrain clamps the result
       at ambient, so a solid melting anywhere above ambient lands at exactly
       ambient whatever its melting point was. Give the melt a freezing point
       ABOVE ambient and it refreezes on the very next frame, forever: melt,
       fall to ambient, refreeze, reheat. On screen that is wax under a lamp at
       100 C that simply will not melt, which is how it was reported. */
    {
        devClear();
        g_world.reset();
        g_world.setLiveWindow(LX - 40, LY - 40, LX + 40, LY + 40);
        /* A one-cell pocket: floor, two walls, open above. */
        g_world.setCell(LX,     LY + 1, MAT_STONE);
        g_world.setCell(LX - 1, LY,     MAT_STONE);
        g_world.setCell(LX + 1, LY,     MAT_STONE);
        g_world.setCell(LX,     LY,     MAT_BEESWAX);
        g_world.temp[LY * SIM_W + LX] = (u8)(MATS[MAT_BEESWAX].boilTemp + 4);
        g_world.dirtyPoint(LX, LY);

        int sawSolidAfterMelting = 0;
        bool everMelted = false;
        for (int f = 0; f < 400; ++f) {
            g_world.step();
            const u8 m = g_world.at(LX, LY).mat;
            if (m == MAT_BEESWAX_MELT) everMelted = true;
            else if (everMelted && m == MAT_BEESWAX) ++sawSolidAfterMelting;
        }
        printf("    melted=%d, went solid again %d times\n",
               (int)everMelted, sawSolidAfterMelting);
        check(everMelted, "wax in a pocket melts");
        check(sawSolidAfterMelting == 0, "and never snaps back to solid");
    }

    if (failures == 0) { puts("PASS"); return 0; }
    fprintf(stderr, "%d heat lamp check(s) failed\n", failures);
    return 1;
}
