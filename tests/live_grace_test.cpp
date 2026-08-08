#include "world.h"
#include "materials.h"
#include "room.h"
#include "device.h"
#include "door.h"
#include "projectile.h"
#include "item.h"
#include "sprite.h"
#include "drone.h"
#include "entity.h"
#include <stdio.h>

/* This used to define g_logisticsUiOpen itself, because device.cpp read a flag
   that main.cpp owned and the simulation would not otherwise link without the
   Win32 half of the program. Defining it here fixed this one test and left
   every other headless harness broken -- 29 of them. The storage now lives in
   device.cpp beside the code that reads it, so nothing has to declare it. */

static int waterY(const World& w, int x, int y0, int y1) {
    for (int y = y0; y <= y1; ++y) if (w.at(x, y).mat == MAT_WATER) return y;
    return -1;
}

int main() {
    initMaterials();
    initItems();       /* explosion discs used by the electrical overload test */
    /* A lava backdrop is useful as a free copper furnace, but ore falling through
       its molten cells must not instantly skip iron's fuel-gated smelting step. */
    if (g_bgHeat[MAT_LAVA] >= MATS[MAT_IRON_ORE].boilTemp ||
        g_bgHeat[MAT_LAVA] <= MATS[MAT_STONE].boilTemp) {
        fprintf(stderr, "lava hotspot crosses the iron or stone heat threshold\n"); return 23;
    }
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
    Inventory droneInv; droneInv.clear();
    droneInv.equip[EQ_LIGHT_DRONE].item = ITEM_LIGHT_DRONE;
    droneInv.equip[EQ_LIGHT_DRONE].count = 1;
    droneInv.equip[EQ_DRONE_A].item = ITEM_ATTACK_DRONE;
    droneInv.equip[EQ_DRONE_A].count = 1;
    Player dronePlayer; dronePlayer.reset(500.0f, 500.0f);
    droneReset(); droneTick(w, dronePlayer, droneInv);
    if (droneCount() != 2 || !equipFits(ITEM_ATTACK_DRONE, EQ_DRONE_B)) {
        fprintf(stderr, "drone equipment did not create its companions\n"); return 27;
    }
    /* A pickup drone is a real utility companion: it collects a distant
       collectible through its own range, not by pretending the player walked
       there or by turning the drop back into a world cell. */
    entReset(); droneInv.clear();
    droneInv.equip[EQ_DRONE_A].item = ITEM_PICKUP_DRONE;
    droneInv.equip[EQ_DRONE_A].count = 1;
    g_pickups[0].used = true; g_pickups[0].item = (ItemId)MAT_CHITIN;
    g_pickups[0].count = 1; g_pickups[0].x = 550.0f; g_pickups[0].y = 500.0f;
    droneReset(); droneTick(w, dronePlayer, droneInv);
    if (g_pickups[0].used || droneInv.countOf((ItemId)MAT_CHITIN) != 1 ||
        !equipFits(ITEM_SHIELD_DRONE, EQ_DRONE_B)) {
        fprintf(stderr, "pickup or shield drone equipment failed\n"); return 36;
    }
    /* Chips belong to individual drone bays. A shield pulse both makes close
       enemies give ground and lets its installed garlic field add local AOE. */
    droneInv.clear(); entReset();
    droneInv.equip[EQ_DRONE_A].item = ITEM_SHIELD_DRONE;
    droneInv.equip[EQ_DRONE_A].count = 1;
    droneInv.droneModule[1][0].item = ITEM_GARLIC_FIELD_CHIP;
    droneInv.droneModule[1][0].count = 1;
    const int shieldTarget = entSpawn(w, ENT_MITE, 520.0f, 500.0f);
    if (shieldTarget < 0) { fprintf(stderr, "could not spawn shield test target\n"); return 37; }
    const int shieldHp = g_entities[shieldTarget].hp;
    droneReset(); droneTick(w, dronePlayer, droneInv);
    if (g_entities[shieldTarget].hp >= shieldHp || g_entities[shieldTarget].vx <= 0.0f) {
        fprintf(stderr, "shield drone did not pulse damage and knockback\n"); return 38;
    }
    /* Mining capability belongs to the best miner carried, not the selected
       hotbar slot; holding building material must not make an auger disappear. */
    Inventory mineInv; mineInv.clear();
    mineInv.add(ITEM_DRILL, 1); mineInv.add(ITEM_LANCE, 1);
    const ToolSpec bestMine = miningSpec(mineInv);
    if (bestMine.cellsPerBite != ITEMS[ITEM_LANCE].mineBite ||
        bestMine.cooldown != ITEMS[ITEM_LANCE].mineCooldown) {
        fprintf(stderr, "best carried miner was not selected\n"); return 31;
    }
    /* Overwrite gives the displaced material back before consuming the held
       replacement, so sealing a cell cannot silently delete what was there. */
    Inventory replaceInv; replaceInv.clear(); replaceInv.add((ItemId)MAT_WOOD, 1);
    w.setCell(200, 200, MAT_STONE);
    if (overwriteFrom(w, replaceInv, 200, 200, 0, 1, STR_HARD) != 1 ||
        w.at(200, 200).mat != MAT_WOOD || replaceInv.countOf((ItemId)MAT_STONE) != 1) {
        fprintf(stderr, "overwrite did not replace and collect material\n"); return 32;
    }
    w.reset();
    if (!devPlace(w, DEV_PIPE, 140, 140) || playerSolid(w, 140, 140) ||
        playerSolid(w, 140, 140, SOLID_FLOOR)) {
        fprintf(stderr, "item pipe blocks player movement\n"); return 24;
    }
    /* A logistics network is topology, not a procession of tiny inventories.
       Keep only the two endpoints alive; the forty pipe segments between them
       deliberately sit in sleeping chunks and must still transfer at once. */
    devClear(); w.reset();
    const int netY = 500, sourceX = 210, pipeN = 40, sinkX = sourceX + (pipeN + 1) * DEV_W;
    if (!devPlace(w, DEV_CHEST, sourceX, netY)) { fprintf(stderr, "could not place logistics source\n"); return 42; }
    for (int n = 1; n <= pipeN; ++n)
        if (!devPlace(w, DEV_PIPE, sourceX + n * DEV_W, netY)) { fprintf(stderr, "could not place logistics bridge\n"); return 43; }
    if (!devPlace(w, DEV_SPOUT, sinkX, netY)) { fprintf(stderr, "could not place logistics sink\n"); return 44; }
    Device* netSource = devAt(sourceX, netY);
    Device* netSink = devAt(sinkX, netY);
    if (!netSource || !netSink) { fprintf(stderr, "could not find logistics endpoints\n"); return 45; }
    netSource->mat = MAT_FUEL; netSource->count = 12;
    w.keepAlive[((netSource->y + DEV_H / 2) >> CHUNK_SHIFT) * CHUNKS_X + ((netSource->x + DEV_W / 2) >> CHUNK_SHIFT)] = 1;
    w.keepAlive[((netSink->y + DEV_H / 2) >> CHUNK_SHIFT) * CHUNKS_X + ((netSink->x + DEV_W / 2) >> CHUNK_SHIFT)] = 1;
    devTick(w);
    if (netSink->count != 4 || netSink->mat != MAT_FUEL || netSource->count != 8) {
        fprintf(stderr, "sleeping pipe bridge did not transfer directly\n"); return 46;
    }
    /* Drains prefer their aim, but a dry saved/default aim must not leave a
       basin-side water drain inert when the matching liquid touches another
       edge. This is the common "machine below a reservoir" build. */
    devClear(); w.reset();
    if (!devPlace(w, DEV_DRAIN, 600, 600)) { fprintf(stderr, "could not place fallback drain\n"); return 47; }
    Device* fallbackDrain = devAt(600, 600);
    if (!fallbackDrain) { fprintf(stderr, "could not find fallback drain\n"); return 48; }
    fallbackDrain->value = MAT_WATER;
    for (int i = 0; i < DEV_W; ++i) {
        int x2, y2; Device up = *fallbackDrain; up.face = 1; devFaceCell(up, i, &x2, &y2); w.setCell(x2, y2, MAT_WATER);
        Device down = *fallbackDrain; down.face = 0; devFaceCell(down, i, &x2, &y2); w.setCell(x2, y2, MAT_STONE);
    }
    w.setLiveWindow(570, 570, 630, 630); devTick(w);
    if (fallbackDrain->mat != MAT_WATER || fallbackDrain->count != 4) {
        fprintf(stderr, "dry-faced drain did not fall back to adjacent water\n"); return 49;
    }
    devClear(); w.reset();
    /* Doors are player convenience, not an enemy ability: approaching opens
       one and leaving lets it become solid again. */
    const int doorX = 410, doorY = 400;
    w.setCell(doorX, doorY, MAT_DOOR);
    Player doorPlayer; doorPlayer.reset(406.0f, 400.0f);
    doorPlayer.occupy(w);
    doorAuto(w, doorPlayer);
    if (w.at(doorX, doorY).mat != MAT_DOOR_OPEN) {
        fprintf(stderr, "player approach did not open door\n"); return 28;
    }
    doorPlayer.reset(450.0f, 450.0f);
    doorPlayer.occupy(w);
    doorAuto(w, doorPlayer);
    if (w.at(doorX, doorY).mat != MAT_DOOR) {
        fprintf(stderr, "door did not close after player cleared it\n"); return 29;
    }
    /* Torches are light fixtures, not bullet cover: a basic shot passes
       through without destroying either the torch or itself. */
    projClear(); w.reset();
    w.setCell(110, 100, MAT_TORCH);
    projSpawn(100.5f, 100.5f, 12.0f, 0.0f, 0, 1, 10, 0xFFFFFF, 0,
              MAT_EMPTY, 0, false, 0.0f);
    projUpdate(w);
    if (w.at(110, 100).mat != MAT_TORCH || projCount() != 1) {
        fprintf(stderr, "shot did not pass through torch\n"); return 30;
    }
    /* A torch is now a fixture: it can be installed under water without
       consuming the pool, and its footprint cannot catch falling loot. */
    devClear(); w.reset();
    const int torchX = 350, torchY = 350;
    for (int y = torchY - 7; y < torchY + 7; ++y)
        for (int x2 = torchX - 7; x2 < torchX + 7; ++x2) w.setCell(x2, y, MAT_WATER);
    if (!devPlace(w, DEV_TORCH, torchX, torchY) || w.at(torchX, torchY).mat != MAT_WATER) {
        fprintf(stderr, "underwater torch displaced its water\n"); return 39;
    }
    w.setLiveWindow(320, 320, 380, 380); devTick(w);
    if (devCount() != 1) { fprintf(stderr, "underwater torch did not remain intact\n"); return 40; }
    /* Decorative fixtures must not consume the 128-machine automation budget.
       A fully lit base can have hundreds of torches without the 129th silently
       failing to place or causing the existing ones to disappear. */
    for (int ty = 0; ty < 10; ++ty) for (int tx = 0; tx < 20; ++tx)
        if (!devPlace(w, DEV_TORCH, 600 + tx * DEV_W, 600 + ty * DEV_H)) {
            fprintf(stderr, "torch fixture unexpectedly hit a machine cap\n"); return 42;
        }
    if (torchCount() != 201 || devCount() != 201) {
        fprintf(stderr, "torch fixture count was not retained\n"); return 43;
    }
    devClear();
    w.reset();
    w.setLiveWindow(380, 380, 450, 450);
    w.setCell(410, 401, MAT_STONE);
    w.setCell(410, 400, MAT_WATER);
    w.setCell(410, 399, MAT_GLOWFLUID);
    w.step();
    if (w.at(410, 400).mat != MAT_GLOWFLUID) {
        fprintf(stderr, "glowfluid did not sink through water\n"); return 41;
    }
    w.reset();
    /* Chitin is a collectible overlay now: killing a mite creates no material
       cell, and moving into range collects the drop without mining it. */
    entReset();
    Player lootPlayer; lootPlayer.reset(600.0f, 600.0f);
    Inventory lootInv; lootInv.clear();
    const int lootMite = entSpawn(w, ENT_MITE, 300.0f, 300.0f);
    if (lootMite < 0) { fprintf(stderr, "could not spawn loot test mite\n"); return 33; }
    g_entities[lootMite].hp = 0;
    entTick(w, lootPlayer, lootInv);
    if (pickupCount() != 1 || w.at(300, 300).mat == MAT_CHITIN) {
        fprintf(stderr, "chitin did not become a pickup\n"); return 34;
    }
    lootPlayer.reset(300.0f, 300.0f);
    entTick(w, lootPlayer, lootInv);
    if (pickupCount() != 0 || lootInv.countOf((ItemId)MAT_CHITIN) < 1) {
        fprintf(stderr, "chitin pickup was not collected\n"); return 35;
    }
    /* Land enemies must not materialise inside water or glowfluid. The public
       spawn gate is tested too, so future callers cannot bypass the spawner's
       site check. */
    entReset(); w.reset();
    const EntityDef& waterMite = ENT_DEFS[ENT_MITE];
    for (int yy = 500; yy < 500 + waterMite.h; ++yy)
        for (int xx = 500; xx < 500 + waterMite.w; ++xx) w.setCell(xx, yy, MAT_WATER);
    if (entSpawn(w, ENT_MITE, 500.0f, 500.0f) >= 0) {
        fprintf(stderr, "enemy spawned underwater\n"); return 44;
    }
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

    /* A pulse dropped behind another one on a thick wire must grow into the
       full-width wave, not be mistaken for a crossing wave by old trail marks. */
    sparkClear();
    const int tx = 600, ty = 300, tw = 100, th = 6;
    for (int y = ty; y < ty + th; ++y)
        for (int x2 = tx; x2 < tx + tw; ++x2)
            w.setCell(x2, y, MAT_COPPER);
    if (!sparkAdd(tx, ty + 1, 1, 0)) {
        fprintf(stderr, "could not start leading thick-wire pulse\n"); return 39;
    }
    for (int i = 0; i < 20; ++i) devTick(w);
    if (!sparkAdd(tx + 10, ty + 1, 1, 0)) {
        fprintf(stderr, "could not start following thick-wire pulse\n"); return 40;
    }
    for (int i = 0; i < 20; ++i) devTick(w);
    int followingRows = 0, followingFronts = 0;
    for (int y = ty; y < ty + th; ++y) {
        bool hasRow = false;
        for (int i = 0; i < MAX_SPARKS; ++i) if (g_sparks[i].used &&
            g_sparks[i].pulse == 2 && g_sparks[i].y == y) {
            hasRow = true; ++followingFronts;
        }
        if (hasRow) ++followingRows;
    }
    if (followingRows != th || followingFronts != th) {
        fprintf(stderr, "following thick-wire pulse was swallowed or narrowed\n"); return 41;
    }

    /* The more local collision rule must retain the reason it exists: opposing
       waves annihilate instead of crossing and recursively refilling trails. */
    sparkClear();
    for (int y = ty; y < ty + th; ++y)
        for (int x2 = tx; x2 < tx + tw; ++x2) {
            w.setCell(x2, y, MAT_COPPER);
            w.temp[y * SIM_W + x2] = AMBIENT_TEMP;
        }
    if (!sparkAdd(tx, ty + 1, 1, 0) ||
        !sparkAdd(tx + tw - 1, ty + th - 2, -1, 0)) {
        fprintf(stderr, "could not start opposing thick-wire pulses\n"); return 42;
    }
    int opposingPeak = 0;
    for (int i = 0; i < tw + 40; ++i) {
        devTick(w);
        if (sparkCount() > opposingPeak) opposingPeak = sparkCount();
    }
    bool opposingWireIntact = true;
    for (int y = ty; y < ty + th; ++y)
        for (int x2 = tx; x2 < tx + tw; ++x2)
            if (w.at(x2, y).mat != MAT_COPPER) opposingWireIntact = false;
    if (sparkCount() != 0 || opposingPeak > th * 3 || !opposingWireIntact) {
        fprintf(stderr, "opposing thick-wire pulses cascaded\n"); return 43;
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

    /* Electricity must freeze with an off-screen chunk. Before this, world.step
       froze copper's cooling while devTick kept clocks firing into it; a simple
       clock-and-wire run consequently heated itself until it exploded. */
    w.reset(); devClear(); sparkClear();
    const int clockX = 800, clockY = 800;
    if (!devPlace(w, DEV_CLOCK, clockX, clockY)) {
        fprintf(stderr, "could not place off-screen clock test\n"); return 25;
    }
    Device* clock = devAt(clockX, clockY);
    for (int x = clock->x + DEV_W; x <= clock->x + DEV_W + 16; ++x)
        w.setCell(x, clock->y + DEV_H / 2, MAT_COPPER);
    clock->value = 6;
    clock->phase = 5;  /* next active tick would emit immediately */
    const int wireEnd = clock->x + DEV_W + 16;
    w.setLiveWindow(1400, 1400, 1463, 1463);
    for (int i = 0; i < 360; ++i) devTick(w);
    if (clock->phase != 5 || sparkCount() != 0 ||
        w.at(wireEnd, clock->y + DEV_H / 2).mat != MAT_COPPER) {
        fprintf(stderr, "off-screen clock advanced or overloaded its wire\n"); return 26;
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

    /* A Sieve is a fixed mesh, not consumed by the flow: liquids and gas hop
       across it, while powder is caught. A Gas Sieve is the stricter version. */
    w.reset();
    w.setLiveWindow(480, 480, 550, 550);
    w.setCell(500, 500, MAT_WATER); w.setCell(500, 501, MAT_SIEVE);
    w.step();
    if (w.at(500, 501).mat != MAT_SIEVE || w.at(500, 502).mat != MAT_WATER) {
        fprintf(stderr, "sieve did not pass water\n"); return 45;
    }
    w.reset();
    w.setLiveWindow(480, 480, 550, 550);
    w.setCell(510, 500, MAT_SAND); w.setCell(510, 501, MAT_SIEVE);
    w.setCell(509, 500, MAT_STONE); w.setCell(511, 500, MAT_STONE);
    w.setCell(509, 501, MAT_STONE); w.setCell(511, 501, MAT_STONE);
    w.step();
    if (w.at(510, 500).mat != MAT_SAND) {
        fprintf(stderr, "sieve passed powder\n"); return 46;
    }
    w.reset();
    w.setLiveWindow(480, 480, 550, 550);
    w.setCell(520, 500, MAT_WATER); w.setCell(520, 501, MAT_GAS_SIEVE);
    w.setCell(519, 500, MAT_STONE); w.setCell(521, 500, MAT_STONE);
    w.setCell(519, 501, MAT_STONE); w.setCell(521, 501, MAT_STONE);
    w.step();
    if (w.at(520, 500).mat != MAT_WATER) {
        fprintf(stderr, "gas sieve passed liquid\n"); return 47;
    }
    w.reset();
    w.setLiveWindow(480, 480, 550, 550);
    w.setCell(530, 500, MAT_STEAM); w.setCell(530, 499, MAT_GAS_SIEVE);
    w.step();
    if (w.at(530, 499).mat != MAT_GAS_SIEVE || w.at(530, 498).mat != MAT_STEAM) {
        fprintf(stderr, "gas sieve did not pass gas\n"); return 48;
    }

    /* Convection moves a hot fluid parcel upward without moving either cell.
       Use glowfluid rather than water so this catches an accidental return to
       a water-only special case. The bottom row is even so reset frame zero
       selects this non-overlapping pair. */
    w.reset();
    const int convX = 600, convBottom = 600;
    w.setLiveWindow(convX - 4, convBottom - 6, convX + 4, convBottom + 4);
    for (int y = convBottom - 2; y <= convBottom + 1; ++y) {
        w.setCell(convX - 3, y, MAT_STONE);
        w.setCell(convX + 3, y, MAT_STONE);
    }
    for (int x = convX - 2; x <= convX + 2; ++x) {
        w.setCell(x, convBottom + 1, MAT_STONE);
        w.setCell(x, convBottom, MAT_GLOWFLUID);
        w.setCell(x, convBottom - 1, MAT_GLOWFLUID);
        w.temp[convBottom * SIM_W + x] = degC(90);
        w.temp[(convBottom - 1) * SIM_W + x] = AMBIENT_TEMP;
    }
    g_rng = 0x31415926u;
    w.step();
    const int convBelowT = w.temp[convBottom * SIM_W + convX];
    const int convAboveT = w.temp[(convBottom - 1) * SIM_W + convX];
    const bool convMatsStayed = w.at(convX, convBottom).mat == MAT_GLOWFLUID &&
                                w.at(convX, convBottom - 1).mat == MAT_GLOWFLUID;

    /* Identical sealed column shifted down one row: frame zero does not select
       its odd bottom row, so this is the neighbor-conduction-only control. */
    w.reset();
    const int ctrlBottom = convBottom + 1;
    w.setLiveWindow(convX - 4, ctrlBottom - 6, convX + 4, ctrlBottom + 4);
    for (int y = ctrlBottom - 2; y <= ctrlBottom + 1; ++y) {
        w.setCell(convX - 3, y, MAT_STONE);
        w.setCell(convX + 3, y, MAT_STONE);
    }
    for (int x = convX - 2; x <= convX + 2; ++x) {
        w.setCell(x, ctrlBottom + 1, MAT_STONE);
        w.setCell(x, ctrlBottom, MAT_GLOWFLUID);
        w.setCell(x, ctrlBottom - 1, MAT_GLOWFLUID);
        w.temp[ctrlBottom * SIM_W + x] = degC(90);
        w.temp[(ctrlBottom - 1) * SIM_W + x] = AMBIENT_TEMP;
    }
    g_rng = 0x31415926u;
    w.step();
    const int ctrlAboveT = w.temp[(ctrlBottom - 1) * SIM_W + convX];
    if (!convMatsStayed || convAboveT <= ctrlAboveT) {
        fprintf(stderr, "water convection did not beat conduction-only control (conv %d/%d, control above %d)\n",
                convBelowT, convAboveT, ctrlAboveT); return 49;
    }

    /* Water's short reach makes a horizontal heat front advance three cells in
       one frame. Frame zero scans right-to-left, so local neighbor conduction
       alone cannot relay heat away from the left endpoint during this step. */
    w.reset();
    const int waterHeatX = 700, waterHeatY = 620;
    w.setLiveWindow(waterHeatX - 4, waterHeatY - 4, waterHeatX + 12, waterHeatY + 4);
    for (int x = waterHeatX; x < waterHeatX + 8; ++x) {
        w.setCell(x, waterHeatY, MAT_WATER);
        w.setCell(x, waterHeatY + 1, MAT_STONE);
    }
    w.setCell(waterHeatX - 1, waterHeatY, MAT_STONE);
    w.setCell(waterHeatX + 8, waterHeatY, MAT_STONE);
    w.setCell(waterHeatX - 1, waterHeatY + 1, MAT_STONE);
    w.setCell(waterHeatX + 8, waterHeatY + 1, MAT_STONE);
    w.temp[waterHeatY * SIM_W + waterHeatX] = degC(90);
    const int waterHeatBefore = w.temp[waterHeatY * SIM_W + waterHeatX];
    w.step();
    if (w.temp[waterHeatY * SIM_W + waterHeatX + 3] <= AMBIENT_TEMP) {
        fprintf(stderr, "water heat front did not use its short conduction reach (before %d; %u %u %u %u; mats %u %u)\n",
                waterHeatBefore,
                (unsigned)w.temp[waterHeatY * SIM_W + waterHeatX],
                (unsigned)w.temp[waterHeatY * SIM_W + waterHeatX + 1],
                (unsigned)w.temp[waterHeatY * SIM_W + waterHeatX + 2],
                (unsigned)w.temp[waterHeatY * SIM_W + waterHeatX + 3],
                (unsigned)w.at(waterHeatX, waterHeatY).mat,
                (unsigned)w.at(waterHeatX + 3, waterHeatY).mat); return 51;
    }

    /* A submerged gas plume may rise diagonally, but never makes a pure
       sideways underwater hop. Track it each frame: it must widen, while its
       lateral distance remains bounded by how far it has risen. */
    w.reset();
    const int bubbleX = 800, bubbleY = 700;
    w.setLiveWindow(bubbleX - 16, bubbleY - 68, bubbleX + 16, bubbleY + 8);
    for (int y = bubbleY - 60; y <= bubbleY + 2; ++y) {
        for (int x = bubbleX - 10; x <= bubbleX + 10; ++x) {
            const bool wall = x == bubbleX - 10 || x == bubbleX + 10 || y == bubbleY + 2;
            w.setCell(x, y, wall ? MAT_STONE : MAT_WATER);
            if (!wall) w.temp[y * SIM_W + x] = AMBIENT_TEMP;
        }
    }
    w.setCell(bubbleX, bubbleY, MAT_STEAM);
    g_rng = 0x27182818u;
    int steamCount = 0, steamX = bubbleX, steamY = bubbleY, maxLateral = 0;
    for (int frame = 0; frame < 6; ++frame) {
        w.step();
        steamCount = 0;
        for (int y = bubbleY - 60; y <= bubbleY + 1; ++y)
            for (int x = bubbleX - 9; x <= bubbleX + 9; ++x)
                if (w.at(x, y).mat == MAT_STEAM) {
                    ++steamCount; steamX = x; steamY = y;
                }
        const int lateral = steamX < bubbleX ? bubbleX - steamX : steamX - bubbleX;
        if (lateral > maxLateral) maxLateral = lateral;
        if (steamCount != 1 || lateral > bubbleY - steamY) break;
    }
    if (steamCount != 1 || steamY >= bubbleY || maxLateral == 0 ||
        (steamX < bubbleX ? bubbleX - steamX : steamX - bubbleX) > bubbleY - steamY) {
        fprintf(stderr, "submerged steam did not widen locally (%d at %d,%d; max lateral %d)\n",
                steamCount, steamX, steamY, maxLateral); return 50;
    }

    puts("simulation regression test passed");
    return 0;
}
