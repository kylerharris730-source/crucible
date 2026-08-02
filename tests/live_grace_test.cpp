#include "world.h"
#include "materials.h"
#include "room.h"
#include "device.h"
#include "item.h"
#include "sprite.h"
#include <stdio.h>

/* device.cpp is linked into this headless test because world-facing helpers are
   shared across the game. The UI normally owns this flag. */
bool g_logisticsUiOpen = false;

static int waterY(const World& w, int x, int y0, int y1) {
    for (int y = y0; y <= y1; ++y) if (w.at(x, y).mat == MAT_WATER) return y;
    return -1;
}

int main() {
    initMaterials();
    initItems();       /* explosion discs used by the electrical overload test */
    /* Virtual circuit channels are proper sprite assets, not a UI-only text
       convention. Check one lit segment on the first and last numbered chips
       so a future sprite-table edit cannot silently turn them transparent. */
    if (!g_sprite[SPR_SIGNAL1][4 * SPR_W + 9] || !g_sprite[SPR_SIGNAL9][7 * SPR_W + 6]) {
        fprintf(stderr, "virtual circuit signal sprites were not generated\n"); return 22;
    }
    if (!g_matConducts[MAT_IRON_MELT] || !g_matConducts[MAT_COPPER_MELT] ||
        !g_matConducts[MAT_MERCURY]) {
        fprintf(stderr, "liquid metals do not conduct\n"); return 15;
    }
    if (g_matConducts[MAT_ALUMINUM_NITRIDE] ||
        MATS[MAT_ALUMINUM_NITRIDE].heatCond != 255 ||
        MATS[MAT_ALUMINUM_NITRIDE].heatSpread <= MATS[MAT_COPPER].heatSpread) {
        fprintf(stderr, "aluminum nitride is not a high-conductivity electrical insulator\n"); return 16;
    }
    static World w;  /* World is deliberately large; keep this off the stack. */
    w.reset();
    const int x = 64, startY = 80;
    w.setLiveWindow(32, 32, 95, 95);
    w.setCell(x, startY, MAT_WATER);
    w.step();                         /* seed the grace timer while visible */
    const int visibleY = waterY(w, x, startY, startY + 4);
    if (visibleY < 0) { fprintf(stderr, "water did not start\n"); return 1; }

    w.setLiveWindow(1000, 1000, 1063, 1063);
    for (int i = 0; i < 10; ++i) w.step();
    const int afterTen = waterY(w, x, visibleY, visibleY + 16);
    if (afterTen <= visibleY) { fprintf(stderr, "water froze outside view\n"); return 2; }

    for (int i = 10; i < LIVE_GRACE_STEPS; ++i) w.step();
    const int beforeFreeze = waterY(w, x, afterTen, afterTen + LIVE_GRACE_STEPS + 8);
    w.step();
    const int afterFreeze = waterY(w, x, afterTen, afterTen + LIVE_GRACE_STEPS + 8);
    if (beforeFreeze < 0 || afterFreeze != beforeFreeze) {
        fprintf(stderr, "water did not freeze after grace\n"); return 3;
    }
    w.reset(); roomsClear(w);
    /* A backed, sealed 4x4 interior is the smallest deliberate room. */
    for (int y = 120; y <= 125; ++y) for (int x2 = 120; x2 <= 125; ++x2) {
        if (x2 == 120 || x2 == 125 || y == 120 || y == 125) w.setCell(x2, y, MAT_STONE);
        else w.setBg(x2, y, MAT_STONE, true);
    }
    roomsNotifyEdit(w, 120, 120);
    if (roomCount() != 1 || w.keptChunks == 0) {
        fprintf(stderr, "backed sealed room did not register\n"); return 4;
    }

    /* One pulse arriving at a T must become two fronts: straight ahead and down
       the branch. New fronts wait one tick, which keeps the wave visibly moving
       at one cell per frame instead of racing through a whole tree at once. */
    sparkClear(); devClear();
    const int sx = 300, sy = 300;
    w.setCell(sx,     sy,     MAT_COPPER);
    w.setCell(sx + 1, sy,     MAT_COPPER);  /* junction */
    w.setCell(sx + 2, sy,     MAT_COPPER);  /* straight */
    w.setCell(sx + 1, sy - 1, MAT_COPPER);  /* branch */
    if (!sparkAdd(sx, sy, 1, 0)) {
        fprintf(stderr, "could not start electric pulse\n"); return 5;
    }
    devTick(w);
    if (sparkCount() != 1) {
        fprintf(stderr, "pulse did not reach junction\n"); return 6;
    }
    devTick(w);
    if (sparkCount() != 2) {
        fprintf(stderr, "pulse did not split at junction\n"); return 7;
    }
    for (int i = 0; i < 4; ++i) devTick(w);
    if (sparkCount() != 0) {
        fprintf(stderr, "split pulse did not finish\n"); return 8;
    }

    /* A closed wire must still finish: when its front reaches a previously
       visited cell, that pulse has already covered the rest of the ring. */
    sparkClear();
    const int lx = 340, ly = 340;
    for (int y = ly - 1; y <= ly + 5; ++y)
        for (int x2 = lx - 1; x2 <= lx + 5; ++x2)
            w.setCell(x2, y, MAT_EMPTY);
    for (int x2 = lx; x2 <= lx + 4; ++x2) {
        w.setCell(x2, ly, MAT_COPPER);
        w.setCell(x2, ly + 4, MAT_COPPER);
    }
    for (int y = ly; y <= ly + 4; ++y) {
        w.setCell(lx, y, MAT_COPPER);
        w.setCell(lx + 4, y, MAT_COPPER);
    }
    if (!sparkAdd(lx, ly, 1, 0)) {
        fprintf(stderr, "could not start loop pulse\n"); return 9;
    }
    for (int i = 0; i < 24; ++i) devTick(w);
    if (sparkCount() != 0) {
        fprintf(stderr, "loop pulse recirculated\n"); return 10;
    }

    /* A swarm of otherwise separate one-cell fronts in one chunk adds arc heat
       beyond their ordinary endpoints. This is the cleanup path for malformed
       conductive blobs, whose individual fronts would otherwise be too spread
       out to ever melt or rupture anything. */
    sparkClear();
    const int cx = 430, cy = 430;
    for (int i = 0; i < 48; ++i) {
        const int x = cx + (i % 3) * 3, y = cy + (i / 3);
        w.setCell(x, y, MAT_COPPER);
        w.setCell(x + 1, y, MAT_COPPER);
        if (!sparkAdd(x, y, 1, 0)) {
            fprintf(stderr, "could not create crowded fronts\n"); return 11;
        }
    }
    devTick(w);
    bool crowdWarm = false;
    for (int y = cy - 1; y <= cy + 17; ++y)
        for (int x2 = cx - 1; x2 <= cx + 8; ++x2)
            if (w.temp[y * SIM_W + x2] > AMBIENT_TEMP) crowdWarm = true;
    if (!crowdWarm) {
        fprintf(stderr, "crowded fronts did not arc heat\n"); return 12;
    }

    /* A front that makes no net progress near an explosion remnant must fizzle,
       but the same one-cell trace carries an ordinary front far past the local
       watchdog radius. */
    sparkClear();
    const int wx = 520, wy = 520;
    for (int x2 = wx; x2 < wx + 40; ++x2) w.setCell(x2, wy, MAT_COPPER);
    if (!sparkAdd(wx, wy, 1, 0)) {
        fprintf(stderr, "could not start circulation guard front\n"); return 13;
    }
    g_sparks[0].cycleSteps = SPARK_CYCLE_STEPS - 1;
    devTick(w);
    if (sparkCount() != 0) {
        fprintf(stderr, "circulating front did not fizzle\n"); return 14;
    }
    sparkClear();
    if (!sparkAdd(wx, wy, 1, 0)) {
        fprintf(stderr, "could not restart normal front\n"); return 15;
    }
    for (int i = 0; i < 20; ++i) devTick(w);
    if (sparkCount() != 1) {
        fprintf(stderr, "normal front was mistaken for circulation\n"); return 16;
    }

    /* A jammed arc has enough energy to rupture even graphene, which otherwise
       cannot melt. The front must be removed with the conductor instead of
       surviving in an empty cell and wasting a permanent spark slot. */
    sparkClear();
    const int ax = 390, ay = 390;
    w.setCell(ax, ay, MAT_GRAPHENE);
    w.setCell(ax + 1, ay, MAT_STONE); /* closed endpoint: heat stays in wire */
    w.heat(ax, ay, 0, 255);
    if (!sparkAdd(ax, ay, 1, 0)) {
        fprintf(stderr, "could not start overload pulse\n"); return 17;
    }
    devTick(w);
    if (g_matConducts[w.at(ax, ay).mat] || sparkCount() != 0) {
        fprintf(stderr, "overload did not rupture and clear\n"); return 18;
    }

    /* Circuit wires are separate from copper: a chest contributes its material
       count to a named signal, a constant contributes a numbered signal, and an
       arithmetic combinator can add the two after the deliberate one-tick
       network boundary. */
    w.reset(); devClear(); sparkClear();
    if (!devPlace(w, DEV_CHEST, 700, 700) ||
        !devPlace(w, DEV_CONSTANT_COMBINATOR, 728, 700) ||
        !devPlace(w, DEV_ARITHMETIC_COMBINATOR, 756, 700) ||
        !devPlace(w, DEV_PULSE_BUTTON, 784, 700)) {
        fprintf(stderr, "could not place circuit test devices\n"); return 19;
    }
    int chest = -1, constant = -1, arith = -1, receiver = -1;
    for (int i = 0; i < MAX_DEVICES; ++i) if (g_devices[i].used) {
        if (g_devices[i].type == DEV_CHEST) chest = i;
        else if (g_devices[i].type == DEV_CONSTANT_COMBINATOR) constant = i;
        else if (g_devices[i].type == DEV_ARITHMETIC_COMBINATOR) arith = i;
        else if (g_devices[i].type == DEV_PULSE_BUTTON) receiver = i;
    }
    if (chest < 0 || constant < 0 || arith < 0 || receiver < 0 ||
        !circuitToggleWire(chest, arith) || !circuitToggleWire(constant, arith) ||
        !circuitToggleWirePorts(arith, 1, receiver, 0)) {
        fprintf(stderr, "could not wire circuit test\n"); return 20;
    }
    g_devices[chest].mat = MAT_COPPER; g_devices[chest].count = 12;
    g_devices[constant].value = 5;
    g_circuitConfig[constant].signal = CIR_SIG_1;
    g_circuitConfig[arith].signalA = MAT_COPPER;
    g_circuitConfig[arith].signalB = CIR_SIG_1;
    g_circuitConfig[arith].signalOut = CIR_SIG_3;
    g_circuitConfig[arith].op = CIR_OP_ADD;
    for (int i = 0; i < 3; ++i) devTick(w);
    if (circuitInput(arith, CIR_SIG_3) != 0 || circuitInput(arith, MAT_COPPER) != 12 ||
        circuitInput(receiver, CIR_SIG_3) != 17 || circuitWireCount() != 3) {
        fprintf(stderr, "circuit signals did not aggregate through wires\n"); return 21;
    }
    puts("simulation regression test passed");
    return 0;
}
