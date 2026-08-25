#include "world.h"
#include "materials.h"
#include "room.h"
#include "device.h"
#include "door.h"
#include "projectile.h"
#include "item.h"
#include "sprite.h"
#include "drone.h"
#include "accessory.h"
#include "entity.h"
#include "light.h"
#include "multiplayer.h"
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
    playerSessionsReset();
    /* Session zero is the exact storage all legacy g_player/g_inv call sites
       see. A second player receives independent durable state and a stable id,
       then closing that slot cannot disturb the host. */
    if (&g_player != &g_playerSessions[LOCAL_PLAYER_ID].body ||
        &g_inv != &g_playerSessions[LOCAL_PLAYER_ID].inventory ||
        !playerSessionConnected(LOCAL_PLAYER_ID)) {
        fprintf(stderr, "local compatibility aliases do not name player session zero\n"); return 123;
    }
    const PlayerId remotePlayer = playerSessionOpen(false, 300.0f, 300.0f);
    if (remotePlayer != 1 || !playerSessionConnected(remotePlayer) ||
        g_playerSessions[remotePlayer].inventory.add(MAT_SAND, 7) != 0 ||
        g_playerSessions[remotePlayer].inventory.countOf(MAT_SAND) != 7 ||
        g_inv.countOf(MAT_SAND) != 0) {
        fprintf(stderr, "remote player session did not keep independent state\n"); return 124;
    }
    playerSessionClose(remotePlayer);
    if (playerSessionConnected(remotePlayer) ||
        g_playerSessions[remotePlayer].inventory.countOf(MAT_SAND) != 0) {
        fprintf(stderr, "closed player session retained live state\n"); return 125;
    }
    g_inv.add((ItemId)MAT_WOOD, 3);
    g_playerSessions[0].networkId = 2;
    const u16 joinedGeneration = g_playerSessions[0].generation;
    const PlayerId replicatedHost = playerSessionOpen(false, 260.0f, 260.0f);
    playerSessionsReturnToOffline();
    if (g_playerSessions[0].networkId != LOCAL_PLAYER_ID ||
        !g_playerSessions[0].local || !g_playerSessions[0].connected ||
        g_playerSessions[0].generation == joinedGeneration ||
        g_inv.countOf((ItemId)MAT_WOOD) != 3 ||
        playerSessionConnected(replicatedHost)) {
        fprintf(stderr, "client slot did not return cleanly to offline identity\n"); return 126;
    }
    g_inv.clear();
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
    /* Material occupancy has one independently clearable shape per co-op
       player. This is the physical prerequisite for two characters sharing a
       falling-sand world: clearing or moving one must not make the other
       permeable. */
    Player firstOccupant, secondOccupant;
    firstOccupant.reset(220.0f, 320.0f);
    secondOccupant.reset(280.0f, 320.0f);
    firstOccupant.occupy(w, 0);
    secondOccupant.occupy(w, 1);
    const int firstBodyX = (int)firstOccupant.centreX();
    const int secondBodyX = (int)secondOccupant.centreX();
    const int bodyY = firstOccupant.bottom();
    if (!w.blocksCell(firstBodyX, bodyY) ||
        !w.blocksCell(secondBodyX, bodyY) ||
        w.blocksCell((firstBodyX + secondBodyX) / 2, bodyY)) {
        fprintf(stderr, "multiple player occupancy boxes were not independent\n"); return 126;
    }
    firstOccupant.alive = false;
    firstOccupant.occupy(w, 0);
    if (w.blocksCell(firstBodyX, bodyY) || !w.blocksCell(secondBodyX, bodyY)) {
        fprintf(stderr, "clearing one player occupancy box cleared another\n"); return 127;
    }
    w.clearBlockBoxes();
    /* Distant players activate disjoint simulation islands. The midpoint must
       remain frozen; treating the two cores as one bounding rectangle would
       pass the endpoint checks but make player separation catastrophically
       expensive. */
    w.reset();
    const int liveAX = 180, liveBX = 3700, liveY = 420;
    const int betweenX = (liveAX + liveBX) / 2;
    w.setLiveWindow(liveAX - 16, liveY - 16, liveAX + 16, liveY + 16);
    w.addLiveWindow(liveBX - 16, liveY - 16, liveBX + 16, liveY + 16);
    w.setCell(liveAX, liveY, MAT_SAND);
    w.setCell(liveBX, liveY, MAT_SAND);
    w.setCell(betweenX, liveY, MAT_SAND);
    w.step();
    if (w.at(liveAX, liveY + 1).mat != MAT_SAND ||
        w.at(liveBX, liveY + 1).mat != MAT_SAND ||
        w.at(betweenX, liveY).mat != MAT_SAND ||
        w.at(betweenX, liveY + 1).mat == MAT_SAND) {
        fprintf(stderr, "disjoint player live windows collapsed into one region\n"); return 128;
    }
    /* Hostile overlays choose the body actually crossed, and companion state
       is banked by player rather than remote equipment replacing the host's
       drones. These are the two easiest singleton regressions to reintroduce. */
    w.reset(); playerSessionsReset(); projClear(); droneReset();
    g_player.reset(220.0f, 320.0f);
    const PlayerId combatRemote = playerSessionOpen(false, 420.0f, 320.0f);
    PlayerSession& remoteSession = g_playerSessions[combatRemote];
    const int hostHp = g_player.hp, remoteHp = remoteSession.body.hp;
    projSpawn(remoteSession.body.left() - 3.0f, remoteSession.body.centreY(),
              6.0f, 0.0f, STR_NOTHING, 1, 10, 0xFFFFFF, 0,
              MAT_EMPTY, 7, true, 0.0f);
    projUpdate(w);
    if (g_player.hp != hostHp || remoteSession.body.hp >= remoteHp) {
        fprintf(stderr, "hostile projectile did not damage the remote body independently\n"); return 129;
    }
    g_inv.equip[EQ_LIGHT_DRONE].item = ITEM_LIGHT_DRONE;
    g_inv.equip[EQ_LIGHT_DRONE].count = 1;
    remoteSession.inventory.equip[EQ_DRONE_A].item = ITEM_ATTACK_DRONE;
    remoteSession.inventory.equip[EQ_DRONE_A].count = 1;
    droneTickFor(0, w, g_player, g_inv);
    droneTickFor(combatRemote, w, remoteSession.body, remoteSession.inventory);
    if (g_playerSessions[0].drones[0].type != DRONE_LIGHT ||
        g_playerSessions[combatRemote].drones[1].type != DRONE_ATTACK ||
        g_playerSessions[0].drones[1].type != DRONE_NONE) {
        fprintf(stderr, "per-player drone banks overwrote one another\n"); return 130;
    }
    playerSessionClose(combatRemote);
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
    droneReset();
    for (int tick = 0; tick < 120 && g_pickups[0].used; ++tick)
        droneTick(w, dronePlayer, droneInv);
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
    /* Player accessories occupy trinket slots and deliberately do not fit the
       drone-chip sockets. Their three initial effects parallel the drone
       chips, but resolve through a separate player-side path. */
    Inventory accessoryInv = {}; accessoryInv.clear();
    if (!equipFits(ITEM_GARLIC_ACCESSORY, EQ_TRINKET_A) ||
        !equipFits(ITEM_GARLIC_ACCESSORY, EQ_TRINKET_B) ||
        equipFits(ITEM_GARLIC_ACCESSORY, EQ_DRONE_A) ||
        ITEMS[ITEM_GARLIC_ACCESSORY].kind != ITEMK_ACCESSORY) {
        fprintf(stderr, "player accessory class does not fit trinket slots\n"); return 77;
    }
    accessoryInv.equip[EQ_TRINKET_A].item = ITEM_OVERLOAD_ACCESSORY;
    accessoryInv.equip[EQ_TRINKET_A].count = 1;
    accessoryInv.equip[EQ_TRINKET_B].item = ITEM_TWIN_ACCESSORY;
    accessoryInv.equip[EQ_TRINKET_B].count = 1;
    if (accessoryShotDelay(accessoryInv, 28) != 21 ||
        !accessoryTwinShot(accessoryInv) ||
        accessoryInv.hasEquipped(ITEM_GARLIC_ACCESSORY)) {
        fprintf(stderr, "overload or twin accessory effect is not isolated\n"); return 78;
    }
    accessoryInv.equip[EQ_TRINKET_A].item = ITEM_GARLIC_ACCESSORY;
    entReset();
    const int garlicTarget = entSpawn(w, ENT_MITE, 520.0f, 500.0f);
    if (garlicTarget < 0) { fprintf(stderr, "could not spawn garlic test target\n"); return 79; }
    const int garlicHp = g_entities[garlicTarget].hp;
    accessoryReset(); accessoryTick(dronePlayer, accessoryInv);
    if (g_entities[garlicTarget].hp >= garlicHp) {
        fprintf(stderr, "garlic accessory did not damage a nearby enemy\n"); return 80;
    }
    /* Followers accelerate back instead of teleporting and settle into the
       invisible small pocket above the player's head after ordinary movement. */
    droneInv.clear();
    droneInv.equip[EQ_LIGHT_DRONE].item = ITEM_LIGHT_DRONE;
    droneInv.equip[EQ_LIGHT_DRONE].count = 1;
    droneReset();
    for (int tick = 0; tick < 120 && g_pickups[0].used; ++tick)
        droneTick(w, dronePlayer, droneInv);
    const float oldDroneX = g_drones[0].x;
    Player movedPlayer; movedPlayer.reset(650.0f, 500.0f);
    droneTick(w, movedPlayer, droneInv);
    if (g_drones[0].x <= oldDroneX || g_drones[0].x >= movedPlayer.centreX()) {
        fprintf(stderr, "drone catch-up did not accelerate smoothly\n"); return 81;
    }
    for (int tick = 0; tick < 180; ++tick) droneTick(w, movedPlayer, droneInv);
    const float homeDx = g_drones[0].x - movedPlayer.centreX();
    const float homeDy = g_drones[0].y - ((float)movedPlayer.top() - DRONE_HOME_ABOVE);
    if (homeDx * homeDx + homeDy * homeDy >
        (float)(DRONE_HOME_RADIUS * DRONE_HOME_RADIUS)) {
        fprintf(stderr, "idle drone did not settle in its home pocket\n"); return 82;
    }
    /* A light drone embedded in a wall must emerge on the player's side of
       that wall. The empty pocket immediately beyond it is closer to the
       drone, but lighting that pocket would still leave the player in shadow. */
    w.reset(); droneReset(); droneTick(w, dronePlayer, droneInv);
    for (int y = 470; y <= 500; ++y)
        for (int x = 514; x <= 522; ++x) w.setCell(x, y, MAT_STONE);
    g_drones[0].x = 520.5f; g_drones[0].y = 484.5f;
    g_drones[0].vx = g_drones[0].vy = 0.0f;
    droneTick(w, dronePlayer, droneInv);
    if (g_drones[0].vx > -0.35f) {
        fprintf(stderr, "embedded light drone recovery still brakes too early\n"); return 86;
    }
    for (int tick = 0; tick < 44; ++tick) droneTick(w, dronePlayer, droneInv);
    if (g_matStrength[w.at((int)g_drones[0].x, (int)g_drones[0].y).mat] != STR_NOTHING ||
        g_drones[0].x >= 511.0f) {
        fprintf(stderr, "light drone did not clear the player side of a surface\n"); return 87;
    }
    /* In clear air beside the same wall, surface avoidance should push away
       rather than allowing a zero-speed drone to skate against it. */
    g_drones[0].x = 513.2f; g_drones[0].y = 484.5f;
    g_drones[0].vx = g_drones[0].vy = 0.0f;
    droneTick(w, dronePlayer, droneInv);
    if (g_drones[0].vx >= 0.0f) {
        fprintf(stderr, "light drone did not keep clearance from a wall\n"); return 88;
    }
    /* An attack drone with a wall across its home firing line must hold its
       shot, fly to an open point inside the broad task envelope, then fire. */
    w.reset(); entReset(); projClear(); droneInv.clear();
    droneInv.equip[EQ_DRONE_A].item = ITEM_ATTACK_DRONE;
    droneInv.equip[EQ_DRONE_A].count = 1;
    for (int y = 450; y <= 550; ++y) w.setCell(540, y, MAT_STONE);
    const int obscuredTarget = entSpawn(w, ENT_MITE, 580.0f, 500.0f);
    if (obscuredTarget < 0) { fprintf(stderr, "could not spawn LOS test target\n"); return 83; }
    droneReset(); droneTick(w, dronePlayer, droneInv);
    if (projCount() != 0) {
        fprintf(stderr, "attack drone fired through an obstructed line\n"); return 84;
    }
    for (int tick = 0; tick < 180 && projCount() == 0; ++tick)
        droneTick(w, dronePlayer, droneInv);
    const float taskDx = g_drones[1].x - dronePlayer.centreX();
    const float taskDy = g_drones[1].y - dronePlayer.centreY();
    if (projCount() == 0 || taskDx * taskDx + taskDy * taskDy >
        (float)(DRONE_TASK_RADIUS * DRONE_TASK_RADIUS)) {
        fprintf(stderr, "attack drone did not reposition inside its task envelope\n"); return 85;
    }
    /* The projectile is drawn three cells wide, so a centreline which misses
       a curved wall by one cell is still a visible clip. The drone must reject
       this grazing line even though the zero-width ray itself is empty. */
    w.reset(); entReset(); projClear(); droneReset(); droneInv.clear();
    droneInv.equip[EQ_DRONE_A].item = ITEM_ATTACK_DRONE;
    droneInv.equip[EQ_DRONE_A].count = 1;
    droneTick(w, dronePlayer, droneInv);  /* instantiate before positioning */
    g_drones[1].x = 500.5f; g_drones[1].y = 500.5f;
    g_drones[1].vx = g_drones[1].vy = 0.0f;
    const int grazeTarget = entSpawn(w, ENT_MITE, 570.0f, 501.0f);
    if (grazeTarget < 0) { fprintf(stderr, "could not spawn grazing-shot target\n"); return 89; }
    w.setCell(540, 502, MAT_STONE); /* below the y=500 zero-width centreline */
    droneTick(w, dronePlayer, droneInv);
    if (projCount() != 0 || (g_drones[1].vx == 0.0f && g_drones[1].vy == 0.0f)) {
        fprintf(stderr, "attack drone accepted a wall-grazing firing line p=%d v=%.3f,%.3f\n",
                projCount(), g_drones[1].vx, g_drones[1].vy); return 90;
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
    {
        const ItemId miners[] = { ITEM_DRILL, ITEM_AUGER, ITEM_LANCE, ITEM_DISRUPTOR };
        const int radius[] = { 12, 19, 29, 48 };
        const int bite[] = { 20, 36, 72, 168 };
        const int cool[] = { 5, 5, 4, 3 };
        if (HAND.maxRadius != 7 || HAND.cellsPerBite != 12 || HAND.cooldown != 6) {
            fprintf(stderr, "base mining stats drifted (%d/%d/%d)\n",
                    HAND.maxRadius, HAND.cellsPerBite, HAND.cooldown); return 116;
        }
        for (int k = 0; k < 4; ++k) {
            const ItemDef& miner = ITEMS[miners[k]];
            if (miner.mineRadius != radius[k] || miner.mineBite != bite[k] ||
                miner.mineCooldown != cool[k]) {
                fprintf(stderr, "mining tier %d drifted (%u/%u/%u)\n", k,
                        miner.mineRadius, miner.mineBite, miner.mineCooldown);
                return 116;
            }
        }
        if (ITEMS[ITEM_SICKLE].mineRadius != 22 ||
            ITEMS[ITEM_SICKLE].mineBite != 48) {
            fprintf(stderr, "sickle mining stats drifted\n"); return 116;
        }
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
    /* Automatic closure is evaluated after the whole multiplayer group. A
       far-away peer must not close the door being approached by slot zero. */
    w.setCell(doorX, doorY, MAT_DOOR);
    Player farPlayer; farPlayer.reset(500.0f, 500.0f);
    doorPlayer.reset(406.0f, 400.0f);
    w.clearBlockBoxes(); doorPlayer.occupy(w, 0); farPlayer.occupy(w, 1);
    doorAutoOpen(w, doorPlayer); doorAutoOpen(w, farPlayer); doorAutoClose(w);
    if (w.at(doorX, doorY).mat != MAT_DOOR_OPEN) {
        fprintf(stderr, "distant peer closed nearby player's door\n"); return 72;
    }
    doorPlayer.reset(460.0f, 460.0f);
    w.clearBlockBoxes(); doorPlayer.occupy(w, 0); farPlayer.occupy(w, 1);
    doorAutoOpen(w, doorPlayer); doorAutoOpen(w, farPlayer); doorAutoClose(w);
    if (w.at(doorX, doorY).mat != MAT_DOOR) {
        fprintf(stderr, "multiplayer door did not close after everyone cleared it\n"); return 73;
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

    /* Glowflares are counts, not unique tools: they merge into one ordinary
       stack, carry no instance handle, and can be consumed one at a time. */
    Inventory flareInv = {};
    flareInv.clear();
    if (flareInv.add(ITEM_GLOW_FLARE, 40) != 0 ||
        flareInv.countOf(ITEM_GLOW_FLARE) != 40 ||
        flareInv.slot[0].count != 40 || flareInv.slot[0].inst != 0 ||
        ITEMS[ITEM_GLOW_FLARE].kind != ITEMK_THROWABLE) {
        fprintf(stderr, "glowflares did not form an ordinary consumable stack\n"); return 55;
    }
    if (flareInv.take(ITEM_GLOW_FLARE, 1) != 1 || flareInv.slot[0].count != 39) {
        fprintf(stderr, "glowflare stack did not consume one use\n"); return 56;
    }

    /* A flare stops at rock, leaves its glowfluid charge on the near side and
       bursts into overlays. The next two updates occur inside a solid 80x80
       block; retaining every mote proves their motion does not collide with
       material. They then all expire promptly instead of becoming permanent
       dynamic lights. */
    projClear(); w.reset();
    for (int yy = 660; yy <= 740; ++yy)
        for (int xx = 660; xx <= 740; ++xx) w.setCell(xx, yy, MAT_STONE);
    if (!projSpawn(700.5f, 700.5f, 12.0f, 0.0f, 0, 1, 20, 0x9AF4BE, 0,
                   MAT_GLOWFLUID, 1, false, 0.0f, PROJ_EFFECT_GLOWFLARE)) {
        fprintf(stderr, "could not spawn glowflare test shot\n"); return 57;
    }
    projUpdate(w);
    if (projCount() != 0 || projGlowMoteCount() != 28 ||
        w.at(701, 700).mat != MAT_STONE || w.at(700, 700).mat != MAT_GLOWFLUID) {
        fprintf(stderr, "glowflare did not burst against an intact wall\n"); return 58;
    }
    lightClearDynamic();
    projRegisterLights();
    lightCompute(w, 640, 640);
    if (lightAt(61, 60) < 90) {
        fprintf(stderr, "glowflare burst did not register strong dynamic light\n"); return 59;
    }
    projUpdate(w); projUpdate(w);
    if (projGlowMoteCount() != 28) {
        fprintf(stderr, "glowflare motes collided with solid blocks\n"); return 60;
    }
    /* A rightward impact must now fire a balanced shotgun cone INTO the wall,
       not the old 360-degree wheel. Every mote advances right; mirrored pairs
       keep the lateral centroid on the impact line while still filling both
       halves of a broad cone. */
    float moteX[28], moteY[28];
    const int moteCount = projGlowMoteSnapshot(moteX, moteY, 28);
    float lateralSum = 0.0f;
    bool above = false, below = false;
    for (int i = 0; i < moteCount; ++i) {
        if (moteX[i] <= 701.5f) {
            fprintf(stderr, "glowflare shotgun sent a mote away from its wall\n"); return 62;
        }
        lateralSum += moteY[i] - 700.5f;
        above = above || moteY[i] < 699.5f;
        below = below || moteY[i] > 701.5f;
    }
    if (moteCount != 28 || !above || !below ||
        lateralSum < -0.01f || lateralSum > 0.01f) {
        fprintf(stderr, "glowflare shotgun was not a balanced randomized cone\n"); return 63;
    }
    /* The slower dust remains fully populated after fifty motion frames, and
       its gentler drag still carries the leading light substantially deeper
       into the wall than the first two-frame fan above. */
    for (int frame = 0; frame < 50; ++frame) projUpdate(w);
    float lateX[28], lateY[28];
    const int lateCount = projGlowMoteSnapshot(lateX, lateY, 28);
    float farthestX = 0.0f;
    for (int i = 0; i < lateCount; ++i)
        if (lateX[i] > farthestX) farthestX = lateX[i];
    if (lateCount != 28 || farthestX < 770.0f) {
        fprintf(stderr, "glowflare motes did not persist deeper into rock\n"); return 64;
    }
    for (int frame = 0; frame < 50; ++frame) projUpdate(w);
    if (projGlowMoteCount() != 0) {
        fprintf(stderr, "glowflare motes did not decay\n"); return 61;
    }
    if (projGlowAfterglowCount() == 0) {
        fprintf(stderr, "glowflare path did not retain an afterglow\n"); return 65;
    }
    lightClearDynamic();
    projRegisterLights();
    lightCompute(w, 640, 640);
    if (lightAt(61, 60) < 17) {
        fprintf(stderr, "glowflare impact faded before its afterglow elapsed\n"); return 66;
    }
    for (int frame = 0; frame < 130; ++frame) projUpdate(w);
    if (projGlowAfterglowCount() != 0) {
        fprintf(stderr, "glowflare afterglow did not finish fading\n"); return 67;
    }

    /* Any light now reads forty world cells into solid material. At thirty
       cells a thick stone block must retain some illumination; the previous
       twenty-cell soak was already zero here. */
    w.reset();
    for (int yy = 450; yy <= 550; ++yy)
        for (int xx = 400; xx <= 459; ++xx) w.setCell(xx, yy, MAT_STONE);
    lightClearDynamic();
    lightAddDynamic(399, 500, 255);
    lightCompute(w, 350, 450);
    if (lightAt(79, 50) == 0) {
        fprintf(stderr, "light did not penetrate thirty cells into solid material\n"); return 62;
    }
    /* Water now has one quarter of an ordinary liquid's attenuation: half its
       previous cost and therefore twice its previous reach. Verify the solve
       at 100 cells, beyond the old 63-cell limit but inside the new 127. */
    if (g_matOpacity[MAT_WATER] * 4 != g_matOpacity[MAT_WAX]) {
        fprintf(stderr, "water light attenuation was not quarter strength\n"); return 68;
    }
    w.reset();
    for (int y = 430; y <= 570; ++y)
        for (int x2 = 380; x2 <= 620; ++x2)
            w.setCell(x2, y, (x2 < 388 || x2 > 612 || y < 438 || y > 562)
                               ? MAT_STONE : MAT_WATER);
    lightClearDynamic();
    lightAddDynamic(420, 500, 255);
    lightCompute(w, 350, 450);
    const int throughWater = lightAt(170, 50);
    w.reset();
    for (int y = 430; y <= 570; ++y)
        for (int x2 = 380; x2 <= 620; ++x2)
            w.setCell(x2, y, (x2 < 388 || x2 > 612 || y < 438 || y > 562)
                               ? MAT_STONE : MAT_WAX);
    lightClearDynamic();
    lightAddDynamic(420, 500, 255);
    lightCompute(w, 350, 450);
    const int throughWax = lightAt(170, 50);
    if (throughWater < 40 || throughWax != 0) {
        fprintf(stderr, "water light reach did not double in the solve (%d/%d)\n",
                throughWater, throughWax); return 69;
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

    /* A Sieve is a fixed mesh with one fluid occupant per cell. A parcel walks
       through a brush-thick mesh one cell per frame while powder is caught. A
       Gas Sieve is the stricter version. */
    w.reset();
    w.setLiveWindow(480, 480, 550, 550);
    for (int y = 499; y <= 505; ++y) {
        w.setCell(499, y, MAT_STONE); w.setCell(501, y, MAT_STONE);
    }
    w.setCell(500, 500, MAT_WATER);
    for (int y = 501; y <= 503; ++y) w.setCell(500, y, MAT_SIEVE);
    w.step();
    if (w.at(500, 500).mat != MAT_EMPTY || w.at(500, 501).moisture != MAT_WATER) {
        fprintf(stderr, "water did not enter sieve occupant slot\n"); return 45;
    }
    for (int frame = 0; frame < 3; ++frame) w.step();
    if (w.at(500, 501).mat != MAT_SIEVE || w.at(500, 502).mat != MAT_SIEVE ||
        w.at(500, 503).mat != MAT_SIEVE || w.at(500, 504).mat != MAT_WATER) {
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
    if (w.at(520, 500).mat != MAT_WATER || w.at(520, 501).moisture != 0) {
        fprintf(stderr, "gas sieve passed liquid\n"); return 47;
    }
    w.reset();
    w.setLiveWindow(480, 480, 550, 550);
    for (int y = 497; y <= 505; ++y) {
        w.setCell(529, y, MAT_STONE); w.setCell(531, y, MAT_STONE);
    }
    w.setCell(530, 504, MAT_STEAM);
    for (int y = 501; y <= 503; ++y) w.setCell(530, y, MAT_GAS_SIEVE);
    w.step();
    if (w.at(530, 504).mat != MAT_EMPTY || w.at(530, 503).moisture != MAT_STEAM) {
        fprintf(stderr, "steam did not enter gas sieve occupant slot\n"); return 48;
    }
    for (int frame = 0; frame < 3; ++frame) w.step();
    if (w.at(530, 503).mat != MAT_GAS_SIEVE || w.at(530, 502).mat != MAT_GAS_SIEVE ||
        w.at(530, 501).mat != MAT_GAS_SIEVE || w.at(530, 500).mat != MAT_STEAM) {
        fprintf(stderr, "gas sieve did not pass gas\n"); return 48;
    }

    /* Occupancy is real contact, not just transport. Steam inside the mesh
       slakes coal resting on top; the resulting liquid fuel then enters the
       vacated occupant slot and flows out beneath it. */
    w.reset();
    w.setLiveWindow(530, 480, 570, 530);
    for (int y = 498; y <= 503; ++y) {
        w.setCell(549, y, MAT_STONE); w.setCell(551, y, MAT_STONE);
    }
    w.setCell(550, 500, MAT_COAL);
    w.setCell(550, 501, MAT_SIEVE);
    w.setCell(550, 502, MAT_STEAM);
    w.step();
    if (w.at(550, 501).moisture != MAT_STEAM) {
        fprintf(stderr, "steam did not occupy sieve beneath coal\n"); return 52;
    }
    w.step();
    if (w.at(550, 501).moisture != MAT_FUEL || w.at(550, 500).mat != MAT_EMPTY) {
        fprintf(stderr, "sieve-contained steam did not slake coal into flowing fuel\n"); return 53;
    }
    w.step();
    if (w.at(550, 501).moisture != 0 || w.at(550, 502).mat != MAT_FUEL) {
        fprintf(stderr, "fuel did not flow out of sieve after slaking\n"); return 54;
    }

    /* A sieve saturated with steam must not trap a denser liquid occupant.
       The gas below owns the exchange, rising while the fuel falls into its
       previous slot. This is the packed-sieve counterpart of ordinary gas /
       liquid buoyancy exchange. */
    w.reset();
    w.setLiveWindow(530, 480, 570, 530);
    for (int y = 499; y <= 502; ++y) {
        w.setCell(549, y, MAT_STONE); w.setCell(551, y, MAT_STONE);
    }
    w.setCell(550, 500, MAT_SIEVE);
    w.setCell(550, 501, MAT_SIEVE);
    w.cells[500 * SIM_W + 550].moisture = MAT_FUEL;
    w.cells[501 * SIM_W + 550].moisture = MAT_STEAM;
    w.step();
    if (w.at(550, 500).moisture != MAT_STEAM ||
        w.at(550, 501).moisture != MAT_FUEL) {
        fprintf(stderr, "steam and denser fuel did not exchange inside saturated sieve\n");
        return 116;
    }

    /* The upper boundary in the actual Coal / Fuel / Sieve / Steam stack:
       Steam is already inside the sieve and must bubble into the ordinary
       Fuel layer above while Fuel falls into the vacated mesh slot. */
    w.reset();
    w.setLiveWindow(530, 480, 570, 530);
    for (int y = 499; y <= 502; ++y) {
        w.setCell(549, y, MAT_STONE); w.setCell(551, y, MAT_STONE);
    }
    w.setCell(550, 500, MAT_FUEL);
    w.setCell(550, 501, MAT_SIEVE);
    w.cells[501 * SIM_W + 550].moisture = MAT_STEAM;
    w.step();
    if (w.at(550, 500).mat != MAT_STEAM ||
        w.at(550, 501).moisture != MAT_FUEL) {
        fprintf(stderr, "sieve-contained steam did not bubble through fuel layer\n");
        return 120;
    }

    /* Exercise the complete production stack, not just its boundary. Steam
       enters from below, crosses the sieve, bubbles through the existing Fuel
       layer, and reaches Coal so the reaction can continue. */
    w.reset();
    w.setLiveWindow(530, 480, 570, 530);
    for (int y = 498; y <= 504; ++y) {
        w.setCell(549, y, MAT_STONE); w.setCell(551, y, MAT_STONE);
    }
    w.setCell(550, 499, MAT_COAL);
    w.setCell(550, 500, MAT_FUEL);
    w.setCell(550, 501, MAT_SIEVE);
    w.setCell(550, 502, MAT_STEAM);
    for (int frame = 0; frame < 4; ++frame) w.step();
    if (w.at(550, 499).mat == MAT_COAL) {
        fprintf(stderr, "Coal/Fuel/Sieve/Steam stack stopped before steam reached coal\n");
        return 121;
    }

    /* This is the chamber-roof boundary from the original failure: the fuel
       is inside a one-cell sieve layer while the steam immediately below is
       still an ordinary world cell. It must make the same density exchange. */
    w.reset();
    w.setLiveWindow(530, 480, 570, 530);
    for (int y = 499; y <= 502; ++y) {
        w.setCell(549, y, MAT_STONE); w.setCell(551, y, MAT_STONE);
    }
    w.setCell(550, 500, MAT_SIEVE);
    w.cells[500 * SIM_W + 550].moisture = MAT_FUEL;
    w.setCell(550, 501, MAT_STEAM);
    w.step();
    if (w.at(550, 500).moisture != MAT_STEAM ||
        w.at(550, 501).mat != MAT_FUEL) {
        fprintf(stderr, "steam chamber did not displace fuel from sieve roof\n");
        return 118;
    }

    /* A sealed chamber equalizes above zero pressure. The boundary parcel
       still has to exchange without deleting its stored volumes: those move
       into the connected steam parcel as fuel drops out of the sieve. */
    w.reset();
    w.setLiveWindow(530, 480, 570, 530);
    for (int y = 499; y <= 503; ++y) {
        w.setCell(549, y, MAT_STONE); w.setCell(551, y, MAT_STONE);
    }
    w.setCell(550, 500, MAT_SIEVE);
    w.cells[500 * SIM_W + 550].moisture = MAT_FUEL;
    w.setCell(550, 501, MAT_STEAM);
    w.setCell(550, 502, MAT_STEAM);
    w.setCell(550, 503, MAT_STONE);
    w.cells[501 * SIM_W + 550].moisture = 1;
    w.cells[502 * SIM_W + 550].moisture = 1;
    w.step();
    int compressedExcess = 0;
    for (int y = 500; y <= 502; ++y) {
        const Cell& pressureCell = w.at(550, y);
        if (MATS[pressureCell.mat].kind == KIND_GAS)
            compressedExcess += pressureCell.moisture & GAS_EXCESS_MASK;
    }
    if (w.at(550, 500).moisture != MAT_STEAM ||
        w.at(550, 501).mat != MAT_FUEL || compressedExcess != 2) {
        fprintf(stderr, "pressurized steam did not conserve volume while clearing sieve (%u/%u, %u/%u, excess %d)\n",
                w.at(550, 500).mat, w.at(550, 500).moisture,
                w.at(550, 501).mat, w.at(550, 501).moisture,
                compressedExcess);
        return 119;
    }

    /* A Gas Sieve cannot receive the displaced liquid, so its selectivity
       still wins over the general density-exchange rule. */
    w.reset();
    w.setLiveWindow(530, 480, 570, 530);
    for (int y = 499; y <= 502; ++y) {
        w.setCell(549, y, MAT_STONE); w.setCell(551, y, MAT_STONE);
    }
    w.setCell(550, 500, MAT_SIEVE);
    w.setCell(550, 501, MAT_GAS_SIEVE);
    w.cells[500 * SIM_W + 550].moisture = MAT_FUEL;
    w.cells[501 * SIM_W + 550].moisture = MAT_STEAM;
    w.step();
    if (w.at(550, 500).moisture != MAT_FUEL ||
        w.at(550, 501).moisture != MAT_STEAM) {
        fprintf(stderr, "gas sieve accepted liquid during occupant exchange\n");
        return 117;
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
        fprintf(stderr, "non-Water convection did not beat conduction-only control (conv %d/%d, control above %d)\n",
                convBelowT, convAboveT, ctrlAboveT); return 49;
    }

    /* Fixed-point thermal density gives lamp wax a real buoyancy inversion:
       it is heavier than water when cool and lighter at working temperature.
       The sealed one-cell column removes lateral flow from the measurement, so
       rising and falling can only come from parcel density. */
    const int coolWaterDensity = materialDensityQ8(MAT_WATER, degC(20));
    const int warmWaterDensity = materialDensityQ8(MAT_WATER, degC(45));
    if (MATS[MAT_WAX].dryA != 0xF1E9D8 ||
        MATS[MAT_WAX].dryB != 0xE9DFCC ||
        MATS[MAT_WAX].wetA != MATS[MAT_WAX].dryA ||
        MATS[MAT_WAX].wetB != MATS[MAT_WAX].dryB) {
        fprintf(stderr, "wax did not keep its subtle ivory palette\n"); return 118;
    }
    if (materialDensityQ8(MAT_WAX, degC(20)) <= coolWaterDensity ||
        materialDensityQ8(MAT_WAX, degC(45)) >= warmWaterDensity) {
        fprintf(stderr, "wax density does not cross water below boiling\n"); return 91;
    }
    /* Inert Fluid is intentionally Water with the phase-change columns
       removed. Matching the fixed-point density curve matters more than just
       matching the table's ambient integer: otherwise a hot bath would change
       the wax buoyancy experiment even though both said density 100. */
    if (MATS[MAT_INERT_FLUID].kind != KIND_LIQUID ||
        MATS[MAT_INERT_FLUID].density != MATS[MAT_WATER].density ||
        MATS[MAT_INERT_FLUID].dispersion != MATS[MAT_WATER].dispersion ||
        MATS[MAT_INERT_FLUID].heatCond != MATS[MAT_WATER].heatCond ||
        MATS[MAT_INERT_FLUID].heatSpread != MATS[MAT_WATER].heatSpread ||
        MATS[MAT_INERT_FLUID].coolTemp != 0 ||
        MATS[MAT_INERT_FLUID].boilTemp != 0 ||
        MATS[MAT_INERT_FLUID].igniteTemp != 0 ||
        g_matThermalExpansionQ8[MAT_INERT_FLUID] !=
            g_matThermalExpansionQ8[MAT_WATER] ||
        materialDensityQ8(MAT_INERT_FLUID, degC(20)) !=
            materialDensityQ8(MAT_WATER, degC(20)) ||
        materialDensityQ8(MAT_INERT_FLUID, degC(90)) !=
            materialDensityQ8(MAT_WATER, degC(90)) ||
        materialDensityQ8(MAT_INERT_FLUID, degC(215)) !=
            materialDensityQ8(MAT_WATER, degC(215))) {
        fprintf(stderr, "Inert Fluid does not match Water's non-phase physical properties\n"); return 121;
    }
    /* Exercise the actual phase-change update at the hottest representable
       temperature, rather than trusting the zero table entries alone. */
    w.reset();
    const int inertX = 620, inertY = 550;
    w.setLiveWindow(inertX - 3, inertY - 3, inertX + 3, inertY + 3);
    for (int y = inertY - 1; y <= inertY + 1; ++y)
        for (int x = inertX - 1; x <= inertX + 1; ++x)
            w.setCell(x, y, (x == inertX && y == inertY) ?
                      MAT_INERT_FLUID : MAT_STONE);
    w.temp[inertY * SIM_W + inertX] = degC(215);
    w.dirtyPoint(inertX, inertY);
    for (int tick = 0; tick < 30; ++tick) w.step();
    if (w.at(inertX, inertY).mat != MAT_INERT_FLUID) {
        fprintf(stderr, "Inert Fluid changed phase at maximum temperature\n"); return 122;
    }
    /* A marked hot parcel in a wall-hugging wax column used to swap directly
       with another wax cell three rows above. Track that exact parcel through
       several turns: density exchange with Water and internal wax convection
       must both remain local, never moving it more than one cell per frame. */
    w.reset();
    const int wallWaxX = 640, wallWaxTop = 570, wallWaxBottom = 587;
    w.setLiveWindow(wallWaxX - 4, wallWaxTop - 4,
                    wallWaxX + 8, wallWaxBottom + 4);
    for (int y = wallWaxTop - 1; y <= wallWaxBottom; ++y) {
        w.setCell(wallWaxX - 1, y, MAT_STONE);
        w.setCell(wallWaxX + 4, y, MAT_STONE);
    }
    for (int x = wallWaxX - 1; x <= wallWaxX + 4; ++x)
        w.setCell(x, wallWaxBottom, MAT_STONE);
    for (int y = wallWaxTop; y < wallWaxBottom; ++y)
        for (int x = wallWaxX; x < wallWaxX + 4; ++x)
            w.setCell(x, y, MAT_WATER);
    for (int y = wallWaxBottom - 5; y < wallWaxBottom; ++y)
        w.setCell(wallWaxX, y, MAT_WAX);
    int markedWaxX = wallWaxX, markedWaxY = wallWaxBottom - 1;
    w.cells[markedWaxY * SIM_W + markedWaxX].moisture = 123;
    for (int tick = 0; tick < 8; ++tick) {
        w.temp[markedWaxY * SIM_W + markedWaxX] = degC(55);
        w.dirtyPoint(markedWaxX, markedWaxY);
        const int oldX = markedWaxX, oldY = markedWaxY;
        w.step();
        bool foundMarkedWax = false;
        for (int y = wallWaxTop; y < wallWaxBottom; ++y)
            for (int x = wallWaxX; x < wallWaxX + 4; ++x)
                if (w.at(x, y).mat == MAT_WAX && w.at(x, y).moisture == 123) {
                    markedWaxX = x; markedWaxY = y; foundMarkedWax = true;
                }
        const int move = (markedWaxX > oldX ? markedWaxX - oldX : oldX - markedWaxX) +
                         (markedWaxY > oldY ? markedWaxY - oldY : oldY - markedWaxY);
        if (!foundMarkedWax || move > 1) {
            fprintf(stderr, "wall-adjacent wax parcel teleported (%d,%d -> %d,%d)\n",
                    oldX, oldY, markedWaxX, markedWaxY); return 116;
        }
    }
    w.reset();
    const int lampX = 660, lampTop = 600, lampBottom = 621;
    w.setLiveWindow(lampX - 4, lampTop - 4, lampX + 4, lampBottom + 4);
    for (int y = lampTop - 1; y <= lampBottom; ++y) {
        w.setCell(lampX - 1, y, MAT_STONE);
        w.setCell(lampX + 1, y, MAT_STONE);
    }
    w.setCell(lampX, lampBottom, MAT_STONE);
    for (int y = lampTop; y < lampBottom; ++y) w.setCell(lampX, y, MAT_WATER);
    w.setCell(lampX, lampBottom - 1, MAT_WAX);
    const int waxStartY = lampBottom - 1;
    int waxY = waxStartY;
    for (int tick = 0; tick < 12; ++tick) {
        for (int y = lampTop; y < lampBottom; ++y)
            if (w.at(lampX, y).mat == MAT_WAX) { waxY = y; break; }
        /* 55 C is enough for the doubled expansion to produce the same strong
           rise that previously required holding wax near 90 C. */
        w.temp[waxY * SIM_W + lampX] = degC(55);
        w.dirtyPoint(lampX, waxY);
        w.step();
    }
    for (int y = lampTop; y < lampBottom; ++y)
        if (w.at(lampX, y).mat == MAT_WAX) { waxY = y; break; }
    if (waxY >= waxStartY - 7) {
        fprintf(stderr, "heated wax did not convect upward through water (%d -> %d)\n",
                waxStartY, waxY); return 92;
    }
    const int waxHighY = waxY;
    for (int tick = 0; tick < 18; ++tick) {
        for (int y = lampTop; y < lampBottom; ++y)
            if (w.at(lampX, y).mat == MAT_WAX) { waxY = y; break; }
        w.temp[waxY * SIM_W + lampX] = degC(20);
        w.dirtyPoint(lampX, waxY);
        w.step();
    }
    int waxCount = 0;
    for (int y = lampTop; y < lampBottom; ++y) {
        if (w.at(lampX, y).mat != MAT_WAX) continue;
        waxY = y; ++waxCount;
    }
    if (waxCount != 1 || waxY <= waxHighY + 7) {
        fprintf(stderr, "cooled wax did not sink back through water (%d -> %d, count %d)\n",
                waxHighY, waxY, waxCount); return 93;
    }

    /* A dense immiscible liquid must finish leveling promptly after it reaches
       the bottom of a water vessel. Start Fuel as a substantial 36-cell,
       six-row pyramid. It must become one flat bottom row in well under a
       second rather than eroding one blocky edge cell at a time. */
    w.reset();
    const int fuelLeft = 700, fuelRight = 763;
    const int fuelTop = 600, fuelBottom = 620;
    w.setLiveWindow(fuelLeft - 4, fuelTop - 4, fuelRight + 4, fuelBottom + 4);
    for (int y = fuelTop - 1; y <= fuelBottom; ++y) {
        w.setCell(fuelLeft - 1, y, MAT_STONE);
        w.setCell(fuelRight + 1, y, MAT_STONE);
    }
    for (int x = fuelLeft - 1; x <= fuelRight + 1; ++x)
        w.setCell(x, fuelBottom, MAT_STONE);
    for (int y = fuelTop; y < fuelBottom; ++y)
        for (int x = fuelLeft; x <= fuelRight; ++x)
            w.setCell(x, y, MAT_WATER);
    const int fuelCenter = 731;
    int expectedFuel = 0;
    for (int layer = 0; layer < 6; ++layer) {
        const int half = 5 - layer;
        for (int x = fuelCenter - half; x <= fuelCenter + half; ++x) {
            w.setCell(x, fuelBottom - 1 - layer, MAT_FUEL);
            ++expectedFuel;
        }
    }
    int fuelFlatTick = -1;
    for (int tick = 1; tick <= 12; ++tick) {
        w.step();
        int bottom = 0;
        for (int x = fuelLeft; x <= fuelRight; ++x)
            if (w.at(x, fuelBottom - 1).mat == MAT_FUEL) ++bottom;
        if (bottom == expectedFuel) { fuelFlatTick = tick; break; }
    }
    int fuelCount = 0, bottomFuel = 0;
    for (int y = fuelTop; y < fuelBottom; ++y)
        for (int x = fuelLeft; x <= fuelRight; ++x)
            if (w.at(x, y).mat == MAT_FUEL) {
                ++fuelCount;
                if (y == fuelBottom - 1) ++bottomFuel;
            }
    if (fuelCount != expectedFuel || bottomFuel != expectedFuel || fuelFlatTick < 0) {
        fprintf(stderr, "submerged Fuel did not flatten quickly (%d total, %d bottom, tick %d)\n",
                fuelCount, bottomFuel, fuelFlatTick);
        for (int y = fuelTop; y < fuelBottom; ++y)
            for (int x = fuelLeft; x <= fuelRight; ++x)
                if (w.at(x, y).mat == MAT_FUEL)
                    fprintf(stderr, "  Fuel at %d,%d\n", x, y);
        return 117;
    }

    /* This is density behavior, not a Fuel exception. Cool Wax is only a
       little denser than Water, but a five-row wax mound must use the same
       submerged leveling path and settle into a flat bottom layer. */
    w.reset();
    w.setLiveWindow(fuelLeft - 4, fuelTop - 4, fuelRight + 4, fuelBottom + 4);
    for (int y = fuelTop - 1; y <= fuelBottom; ++y) {
        w.setCell(fuelLeft - 1, y, MAT_STONE);
        w.setCell(fuelRight + 1, y, MAT_STONE);
    }
    for (int x = fuelLeft - 1; x <= fuelRight + 1; ++x)
        w.setCell(x, fuelBottom, MAT_STONE);
    for (int y = fuelTop; y < fuelBottom; ++y)
        for (int x = fuelLeft; x <= fuelRight; ++x)
            w.setCell(x, y, MAT_WATER);
    int expectedWax = 0;
    for (int layer = 0; layer < 5; ++layer) {
        const int half = 4 - layer;
        for (int x = fuelCenter - half; x <= fuelCenter + half; ++x) {
            w.setCell(x, fuelBottom - 1 - layer, MAT_WAX);
            ++expectedWax;
        }
    }
    int waxFlatTick = -1;
    for (int tick = 1; tick <= 12; ++tick) {
        w.step();
        int bottom = 0;
        for (int x = fuelLeft; x <= fuelRight; ++x)
            if (w.at(x, fuelBottom - 1).mat == MAT_WAX) ++bottom;
        if (bottom == expectedWax) { waxFlatTick = tick; break; }
    }
    int flatWaxCount = 0, bottomWax = 0;
    for (int y = fuelTop; y < fuelBottom; ++y)
        for (int x = fuelLeft; x <= fuelRight; ++x)
            if (w.at(x, y).mat == MAT_WAX) {
                ++flatWaxCount;
                if (y == fuelBottom - 1) ++bottomWax;
            }
    if (flatWaxCount != expectedWax || bottomWax != expectedWax || waxFlatTick < 0) {
        fprintf(stderr, "submerged Wax did not flatten quickly (%d total, %d bottom, tick %d)\n",
                flatWaxCount, bottomWax, waxFlatTick); return 119;
    }

    /* The real failure was not a flat tank: Fuel and Wax stood as separate
       sheer-sided blocks in a curved lake bed. Reproduce that geometry with
       two tall deposits on opposite slopes. At equilibrium density must sort
       vertically (Water, then Wax, then Fuel), and neither dense material may
       retain anything resembling its original thirty-cell wall. */
    w.reset();
    const int bowlCX = 820, bowlLeft = 756, bowlRight = 884;
    const int bowlWaterTop = 560, bowlBase = 680;
    w.setLiveWindow(bowlLeft - 4, bowlWaterTop - 4,
                    bowlRight + 4, bowlBase + 4);
    for (int x = bowlLeft; x <= bowlRight; ++x) {
        const int adx = x > bowlCX ? x - bowlCX : bowlCX - x;
        const int floorY = bowlBase - adx / 2;
        for (int y = bowlWaterTop; y < floorY; ++y) w.setCell(x, y, MAT_WATER);
        for (int y = floorY; y <= bowlBase + 2; ++y) w.setCell(x, y, MAT_STONE);
    }
    for (int y = bowlWaterTop - 1; y <= bowlBase + 2; ++y) {
        w.setCell(bowlLeft - 1, y, MAT_STONE);
        w.setCell(bowlRight + 1, y, MAT_STONE);
    }
    int bowlFuelExpected = 0, bowlWaxExpected = 0;
    for (int x = 780; x < 792; ++x) {
        const int floorY = bowlBase - (bowlCX - x) / 2;
        for (int y = floorY - 30; y < floorY; ++y) {
            w.setCell(x, y, MAT_FUEL); ++bowlFuelExpected;
        }
    }
    for (int x = 849; x < 861; ++x) {
        const int floorY = bowlBase - (x - bowlCX) / 2;
        for (int y = floorY - 30; y < floorY; ++y) {
            w.setCell(x, y, MAT_WAX); ++bowlWaxExpected;
        }
    }
    for (int tick = 0; tick < 120; ++tick) w.step();
    int bowlFuel = 0, bowlWax = 0, maxFuelColumn = 0, maxWaxColumn = 0;
    int densityInversions = 0;
    for (int x = bowlLeft; x <= bowlRight; ++x) {
        int fuelColumn = 0, waxColumn = 0;
        int previousLiquidDensity = 0;
        for (int y = bowlWaterTop; y < bowlBase; ++y) {
            const u8 bm = w.at(x, y).mat;
            if (bm == MAT_FUEL) { ++bowlFuel; ++fuelColumn; }
            if (bm == MAT_WAX)  { ++bowlWax;  ++waxColumn; }
            if (MATS[bm].kind != KIND_LIQUID) continue;
            const int density = materialDensityQ8(bm, w.temp[y * SIM_W + x]);
            if (previousLiquidDensity > density + 64) ++densityInversions;
            previousLiquidDensity = density;
        }
        if (fuelColumn > maxFuelColumn) maxFuelColumn = fuelColumn;
        if (waxColumn > maxWaxColumn) maxWaxColumn = waxColumn;
    }
    if (bowlFuel != bowlFuelExpected || bowlWax != bowlWaxExpected ||
        maxFuelColumn > 18 || maxWaxColumn > 12 || densityInversions != 0) {
        fprintf(stderr, "curved-basin liquids kept walls (%d/%d Fuel max %d, %d/%d Wax max %d, inversions %d)\n",
                bowlFuel, bowlFuelExpected, maxFuelColumn,
                bowlWax, bowlWaxExpected, maxWaxColumn, densityInversions);
        return 120;
    }

    /* Boiling stores expansion locally, then releases the connected gas volumes
       in an open-space burst. Every daughter is volume-only; the single owner
       token is what lets all of them condense back to exactly one Water cell.

       --- read off the table, not written down -------------------------------
       The counts below come from g_matGasExpansion[MAT_STEAM] rather than being
       the literals 2 and 3. That value is a FEEL dial -- it has already gone
       6 -> 3 -> 6 -- and a test that hardcodes it fails every time somebody
       tunes it, reporting a conservation error for what is only a different
       number of volumes. What is actually under test here is the invariant:
       one owner plus (expansion - 1) daughters, and all of them condensing back
       to exactly one Water. That holds at any expansion. */
    const int steamExpansion = (int)g_matGasExpansion[MAT_STEAM];
    const int steamDaughters = steamExpansion - 1;
    w.reset();
    const int pressureX = 760, pressureY = 640;
    w.setLiveWindow(pressureX - 40, pressureY - 40,
                    pressureX + 40, pressureY + 20);
    w.setCell(pressureX, pressureY, MAT_WATER);
    w.temp[pressureY * SIM_W + pressureX] = degC(215);
    w.dirtyPoint(pressureX, pressureY);
    w.step();
    if (w.at(pressureX, pressureY).mat != MAT_STEAM ||
        (w.at(pressureX, pressureY).moisture & GAS_EXCESS_MASK) != steamDaughters ||
        (w.at(pressureX, pressureY).moisture & GAS_VOLUME_ONLY)) {
        fprintf(stderr, "boiling water did not store %d expansion volumes\n",
                steamDaughters); return 94;
    }
    /* Open Steam releases the charge in bursts of GAS_PRESSURE_EXPANSION_BURST.
       Stepped until it is spent rather than exactly once, so an expansion larger
       than one burst can carry is still measured for CONSERVATION rather than
       failing merely for arriving a frame later. */
    for (int spend = 0; spend < 8; ++spend) {
        w.step();
        if (!(w.at(pressureX, pressureY).moisture & GAS_EXCESS_MASK)) break;
    }
    int pressureSteamCount = 0, ownerCount = 0, excessSum = 0;
    for (int y = pressureY - 40; y <= pressureY + 10; ++y)
        for (int x = pressureX - 40; x <= pressureX + 40; ++x) {
            const Cell& pc = w.at(x, y);
            if (pc.mat != MAT_STEAM) continue;
            ++pressureSteamCount;
            excessSum += pc.moisture & GAS_EXCESS_MASK;
            if (!(pc.moisture & GAS_VOLUME_ONLY)) ++ownerCount;
        }
    if (pressureSteamCount != steamExpansion || ownerCount != 1 || excessSum != 0) {
        fprintf(stderr, "steam did not expand to %d conserved volumes (%d steam, %d owner, %d excess)\n",
                steamExpansion, pressureSteamCount, ownerCount, excessSum); return 95;
    }
    for (int y = pressureY - 40; y <= pressureY + 10; ++y)
        for (int x = pressureX - 40; x <= pressureX + 40; ++x)
            if (w.at(x, y).mat == MAT_STEAM) {
                w.temp[y * SIM_W + x] = degC(0);
                w.dirtyPoint(x, y);
            }
    w.step();
    int condensedWater = 0, remainingSteam = 0;
    for (int y = pressureY - 40; y <= pressureY + 10; ++y)
        for (int x = pressureX - 40; x <= pressureX + 40; ++x) {
            if (w.at(x, y).mat == MAT_WATER) ++condensedWater;
            if (w.at(x, y).mat == MAT_STEAM) ++remainingSteam;
        }
    if (condensedWater != 1 || remainingSteam != 0) {
        fprintf(stderr, "expanded steam did not condense back to one water (%d/%d)\n",
                condensedWater, remainingSteam); return 96;
    }

    /* With no free volume, pressure remains stored and the chunk may sleep;
       opening the chamber later will dirty and wake the compressed parcel. */
    w.reset();
    w.setLiveWindow(pressureX - 4, pressureY - 4, pressureX + 4, pressureY + 4);
    for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox)
        if (ox || oy) w.setCell(pressureX + ox, pressureY + oy, MAT_STONE);
    w.setCell(pressureX, pressureY, MAT_WATER);
    w.temp[pressureY * SIM_W + pressureX] = degC(215);
    w.dirtyPoint(pressureX, pressureY);
    w.step();
    for (int tick = 0; tick < 7; ++tick) {
        w.temp[pressureY * SIM_W + pressureX] = degC(120);
        w.dirtyPoint(pressureX, pressureY);
        w.step();
    }
    if (w.at(pressureX, pressureY).mat != MAT_STEAM ||
        (w.at(pressureX, pressureY).moisture & GAS_EXCESS_MASK) != steamDaughters) {
        fprintf(stderr, "sealed steam did not retain its pressure (mat %u moisture %u temp %u)\n",
                w.at(pressureX, pressureY).mat, w.at(pressureX, pressureY).moisture,
                w.temp[pressureY * SIM_W + pressureX]); return 97;
    }
    w.setCell(pressureX, pressureY - 1, MAT_EMPTY);
    for (int tick = 0; tick < 8; ++tick) {
        for (int y = pressureY - 12; y <= pressureY + 2; ++y)
            for (int x = pressureX - 12; x <= pressureX + 12; ++x)
                if (w.at(x, y).mat == MAT_STEAM) {
                    w.temp[y * SIM_W + x] = degC(120);
                    w.dirtyPoint(x, y);
                }
        w.step();
    }
    pressureSteamCount = excessSum = 0;
    for (int y = pressureY - 12; y <= pressureY + 2; ++y)
        for (int x = pressureX - 12; x <= pressureX + 12; ++x)
            if (w.at(x, y).mat == MAT_STEAM) {
                ++pressureSteamCount;
                excessSum += w.at(x, y).moisture & GAS_EXCESS_MASK;
            }
    if (pressureSteamCount != steamExpansion || excessSum != 0) {
        fprintf(stderr, "opened chamber did not release stored steam (%d/%d)\n",
                pressureSteamCount, excessSum); return 98;
    }

    /* Connected sealed gas shares pressure without creating or deleting it. */
    w.reset();
    for (int y = pressureY - 1; y <= pressureY + 1; ++y)
        for (int x = pressureX - 1; x <= pressureX + 2; ++x)
            w.setCell(x, y, MAT_STONE);
    w.setCell(pressureX, pressureY, MAT_STEAM);
    w.setCell(pressureX + 1, pressureY, MAT_STEAM);
    w.cells[pressureY * SIM_W + pressureX].moisture = 6;
    w.temp[pressureY * SIM_W + pressureX] = degC(120);
    w.temp[pressureY * SIM_W + pressureX + 1] = degC(120);
    w.setLiveWindow(pressureX - 4, pressureY - 4, pressureX + 5, pressureY + 4);
    for (int tick = 0; tick < 3; ++tick) w.step();
    const int leftPressure = w.at(pressureX, pressureY).moisture & GAS_EXCESS_MASK;
    const int rightPressure = w.at(pressureX + 1, pressureY).moisture & GAS_EXCESS_MASK;
    if (leftPressure + rightPressure != 6 ||
        (leftPressure > rightPressure ? leftPressure - rightPressure
                                      : rightPressure - leftPressure) > 1) {
        fprintf(stderr, "sealed gas did not equalize pressure (%d/%d)\n",
                leftPressure, rightPressure); return 99;
    }

    /* Sieve transit preserves whether a gas cell is an expansion-only volume;
       otherwise passing through a mesh would manufacture condensation mass.

       --- the pocket is SEALED, and the wait is bounded ------------------
       Both of those changed when gas diffusion became near-isotropic (see
       updateGas), and both are changes to the SCAFFOLDING rather than to the
       property under test.

       The pocket used to be open at the diagonals and underneath, which was
       harmless while a gas rose deterministically every frame: the steam had
       exactly one move available and took it immediately. A gas that now spends
       most frames doing a random walk wandered out of the side of the apparatus
       instead, and the test reported a provenance failure for what was really
       an escape. Sealing it leaves the mesh as the only way out, which is what
       the comment above always claimed the geometry was.

       Entry is waited for rather than demanded on frame one, because with
       diffusion the exact frame is a coin toss. Bounded, so a mesh that never
       admits the parcel still fails rather than hanging. */
    w.reset();
    w.setLiveWindow(pressureX - 4, pressureY - 6, pressureX + 4, pressureY + 4);
    w.setCell(pressureX, pressureY - 1, MAT_GAS_SIEVE);
    w.setCell(pressureX - 1, pressureY, MAT_STONE);
    w.setCell(pressureX + 1, pressureY, MAT_STONE);
    w.setCell(pressureX - 1, pressureY - 1, MAT_STONE);
    w.setCell(pressureX + 1, pressureY - 1, MAT_STONE);
    w.setCell(pressureX - 1, pressureY + 1, MAT_STONE);
    w.setCell(pressureX,     pressureY + 1, MAT_STONE);
    w.setCell(pressureX + 1, pressureY + 1, MAT_STONE);
    w.setCell(pressureX, pressureY, MAT_STEAM);
    w.cells[pressureY * SIM_W + pressureX].moisture = GAS_VOLUME_ONLY;
    w.temp[pressureY * SIM_W + pressureX] = degC(215);
    int sieveEntryFrames = 0;
    while (!(w.at(pressureX, pressureY - 1).moisture & GAS_VOLUME_ONLY) &&
           sieveEntryFrames < 200) { w.step(); ++sieveEntryFrames; }
    if (!(w.at(pressureX, pressureY - 1).moisture & GAS_VOLUME_ONLY)) {
        fprintf(stderr, "gas sieve did not pack expansion provenance on entry (%u/%u)\n",
                w.at(pressureX, pressureY - 1).mat,
                w.at(pressureX, pressureY - 1).moisture); return 100;
    }
    w.step();
    bool volumeExited = false;
    for (int y = pressureY - 5; y < pressureY; ++y)
        for (int x = pressureX - 4; x <= pressureX + 4; ++x)
            if (w.at(x, y).mat == MAT_STEAM &&
                (w.at(x, y).moisture & GAS_VOLUME_ONLY)) volumeExited = true;
    if (!volumeExited) {
        fprintf(stderr, "gas sieve lost expansion-volume provenance (below %u/%u sieve %u/%u above %u/%u)\n",
                w.at(pressureX, pressureY).mat, w.at(pressureX, pressureY).moisture,
                w.at(pressureX, pressureY - 1).mat, w.at(pressureX, pressureY - 1).moisture,
                w.at(pressureX, pressureY - 2).mat, w.at(pressureX, pressureY - 2).moisture);
        return 101;
    }

    /* Steam can occupy the Coal cell itself. Four stored volumes percolate up
       a supported column one by one, and each is consumed into exactly one
       Fuel cell; the Coal pile is never pushed or swapped as a shortcut. */
    w.reset();
    const int poreX = 900, poreBottom = 720, poreCount = 4;
    w.setLiveWindow(poreX - 5, poreBottom - poreCount - 5,
                    poreX + 5, poreBottom + 5);
    for (int y = poreBottom - poreCount - 2; y <= poreBottom + 1; ++y) {
        w.setCell(poreX - 1, y, MAT_STONE);
        w.setCell(poreX + 1, y, MAT_STONE);
    }
    w.setCell(poreX, poreBottom + 1, MAT_STONE);
    for (int y = poreBottom - poreCount; y < poreBottom; ++y)
        w.setCell(poreX, y, MAT_COAL);
    w.setCell(poreX, poreBottom, MAT_STEAM);
    w.cells[poreBottom * SIM_W + poreX].moisture = poreCount - 1;
    w.temp[poreBottom * SIM_W + poreX] = degC(120);
    w.dirtyPoint(poreX, poreBottom);

    bool sawOccupiedCoal = false;
    for (int tick = 0; tick < 40; ++tick) {
        for (int y = poreBottom - poreCount - 2; y <= poreBottom; ++y) {
            Cell& pc = w.cells[y * SIM_W + poreX];
            if (pc.mat == MAT_STEAM) {
                w.temp[y * SIM_W + poreX] = degC(120);
                w.dirtyPoint(poreX, y);
            }
            if (pc.mat == MAT_COAL && (pc.moisture & GAS_EXCESS_MASK) == MAT_STEAM)
                sawOccupiedCoal = true;
        }
        w.step();
    }
    int poreCoal = 0, poreFuel = 0, poreSteam = 0;
    for (int y = poreBottom - poreCount - 2; y <= poreBottom + 1; ++y) {
        const Cell& pc = w.at(poreX, y);
        if (pc.mat == MAT_COAL) ++poreCoal;
        if (pc.mat == MAT_FUEL) ++poreFuel;
        if (pc.mat == MAT_STEAM) ++poreSteam;
    }
    if (!sawOccupiedCoal || poreCoal != 0 || poreFuel != poreCount || poreSteam != 0) {
        fprintf(stderr, "steam did not percolate through coal (%d occupied, %d coal, %d fuel, %d steam)\n",
                sawOccupiedCoal ? 1 : 0, poreCoal, poreFuel, poreSteam); return 104;
    }

    /* Stored pressure can shift a short powder plug into a real empty outlet.
       Three Sand cells require pressure four (base two plus two more cells),
       then exactly one expansion volume occupies the vacated face. */
    w.reset();
    const int pushX = 940, pushY = 720;
    w.setLiveWindow(pushX - 5, pushY - 10, pushX + 5, pushY + 5);
    for (int y = pushY - 8; y <= pushY + 1; ++y) {
        w.setCell(pushX - 1, y, MAT_STONE);
        w.setCell(pushX + 1, y, MAT_STONE);
    }
    w.setCell(pushX, pushY + 1, MAT_STONE);
    for (int y = pushY - 3; y < pushY; ++y) w.setCell(pushX, y, MAT_SAND);
    w.setCell(pushX, pushY, MAT_STEAM);
    w.cells[pushY * SIM_W + pushX].moisture = 4;
    w.temp[pushY * SIM_W + pushX] = degC(120);
    w.dirtyPoint(pushX, pushY);
    w.step();
    int pushedSand = 0, pushedSteam = 0, pushedExcess = 0;
    for (int y = pushY - 5; y <= pushY; ++y) {
        const Cell& pc = w.at(pushX, y);
        if (pc.mat == MAT_SAND) ++pushedSand;
        if (pc.mat == MAT_STEAM) {
            ++pushedSteam;
            pushedExcess += pc.moisture & GAS_EXCESS_MASK;
        }
    }
    if (pushedSand != 3 || pushedSteam != 2 || pushedExcess != 3 ||
        w.at(pushX, pushY - 4).mat != MAT_SAND ||
        w.at(pushX, pushY - 1).mat != MAT_STEAM) {
        fprintf(stderr, "pressure did not conserve a shifted powder plug (%d sand, %d steam, %d excess, top %u, face %u)\n",
                pushedSand, pushedSteam, pushedExcess,
                w.at(pushX, pushY - 4).mat, w.at(pushX, pushY - 1).mat); return 105;
    }

    /* Supported horizontal plugs isolate pressure from gravity: one stored
       unit cannot shift two Sand cells, and five cells exceed pressure four. */
    w.reset();
    w.setLiveWindow(pushX - 5, pushY - 5, pushX + 10, pushY + 5);
    w.setCell(pushX - 1, pushY, MAT_STONE);
    w.setCell(pushX - 1, pushY - 1, MAT_STONE);
    w.setCell(pushX, pushY + 1, MAT_STONE);
    /* The down-left diagonal, which used to be the one open cell in this
       pocket. It did not matter while gas diffusion was sideways-only -- the
       steam had no move that could reach it. Now that a gas can wander
       downward, an unsealed diagonal is a hole in the apparatus: the parcel
       left through it and the test reported "low pressure moved Sand" for a
       frame in which the sand never moved and the steam was simply gone. */
    w.setCell(pushX - 1, pushY + 1, MAT_STONE);
    for (int x = pushX; x <= pushX + 3; ++x)
        w.setCell(x, pushY - 1, MAT_STONE);
    for (int x = pushX + 1; x <= pushX + 2; ++x) {
        w.setCell(x, pushY, MAT_SAND);
    }
    for (int x = pushX + 1; x <= pushX + 3; ++x)
        w.setCell(x, pushY + 1, MAT_STONE);
    w.setCell(pushX, pushY, MAT_STEAM);
    w.cells[pushY * SIM_W + pushX].moisture = 1;
    w.temp[pushY * SIM_W + pushX] = degC(120);
    w.dirtyPoint(pushX, pushY);
    w.step();
    if (w.at(pushX + 1, pushY).mat != MAT_SAND ||
        w.at(pushX + 2, pushY).mat != MAT_SAND ||
        w.at(pushX + 3, pushY).mat != MAT_EMPTY ||
        w.at(pushX, pushY).mat != MAT_STEAM ||
        (w.at(pushX, pushY).moisture & GAS_EXCESS_MASK) != 1) {
        fprintf(stderr, "low pressure moved Sand (%u %u %u, gas %u/%u)\n",
                w.at(pushX + 1, pushY).mat,
                w.at(pushX + 2, pushY).mat,
                w.at(pushX + 3, pushY).mat,
                w.at(pushX, pushY).mat,
                w.at(pushX, pushY).moisture); return 106;
    }

    w.reset();
    w.setLiveWindow(pushX - 5, pushY - 5, pushX + 12, pushY + 5);
    w.setCell(pushX - 1, pushY, MAT_STONE);
    w.setCell(pushX - 1, pushY - 1, MAT_STONE);
    w.setCell(pushX, pushY + 1, MAT_STONE);
    for (int x = pushX; x <= pushX + 6; ++x)
        w.setCell(x, pushY - 1, MAT_STONE);
    for (int x = pushX + 1; x <= pushX + 5; ++x) {
        w.setCell(x, pushY, MAT_SAND);
    }
    for (int x = pushX + 1; x <= pushX + 6; ++x)
        w.setCell(x, pushY + 1, MAT_STONE);
    w.setCell(pushX, pushY, MAT_STEAM);
    w.cells[pushY * SIM_W + pushX].moisture = 4;
    w.temp[pushY * SIM_W + pushX] = degC(120);
    w.dirtyPoint(pushX, pushY);
    w.step();
    if (w.at(pushX + 6, pushY).mat != MAT_EMPTY ||
        w.at(pushX + 1, pushY).mat != MAT_SAND ||
        (w.at(pushX, pushY).moisture & GAS_EXCESS_MASK) != 4 ||
        g_matPressureResistance[MAT_STONE] != 255 ||
        g_matPressureResistance[MAT_COPPER_ORE] <= 5) {
        fprintf(stderr, "packed or immovable material yielded to insufficient pressure\n"); return 107;
    }

    /* Pressure in the interior of a Steam pocket routes to a remote Coal face,
       then lifts the loose grain before falling back to Coal's permeable Steam
       reaction when a packed pile cannot move. Fresh Steam's two stored units
       are enough for this single-grain shove. */
    w.reset();
    const int coalPushX = 960, coalGasTop = 700, coalGasBottom = 710;
    w.setLiveWindow(coalPushX - 4, coalGasTop - 5,
                    coalPushX + 4, coalGasBottom + 4);
    for (int y = coalGasTop - 2; y <= coalGasBottom + 1; ++y) {
        w.setCell(coalPushX - 1, y, MAT_STONE);
        w.setCell(coalPushX + 1, y, MAT_STONE);
    }
    w.setCell(coalPushX, coalGasBottom + 1, MAT_STONE);
    for (int y = coalGasTop; y <= coalGasBottom; ++y) {
        w.setCell(coalPushX, y, MAT_STEAM);
        w.temp[y * SIM_W + coalPushX] = degC(120);
    }
    w.setCell(coalPushX, coalGasTop - 1, MAT_COAL);
    w.cells[coalGasBottom * SIM_W + coalPushX].moisture = 2;
    w.dirtyPoint(coalPushX, coalGasBottom);
    w.step();
    int coalPushSteam = 0, coalPushExcess = 0;
    for (int y = coalGasTop - 2; y <= coalGasBottom; ++y) {
        if (w.at(coalPushX, y).mat == MAT_STEAM) {
            ++coalPushSteam;
            coalPushExcess += w.at(coalPushX, y).moisture & GAS_EXCESS_MASK;
        }
    }
    if (w.at(coalPushX, coalGasTop - 2).mat != MAT_COAL ||
        w.at(coalPushX, coalGasTop - 1).mat != MAT_STEAM ||
        coalPushSteam != 12 || coalPushExcess != 1) {
        fprintf(stderr, "shared Steam pressure did not lift Coal (%u/%u, %d steam, %d excess)\n",
                w.at(coalPushX, coalGasTop - 2).mat,
                w.at(coalPushX, coalGasTop - 1).mat,
                coalPushSteam, coalPushExcess); return 111;
    }

    /* Unpressurized Steam percolates through a Sand pile one cell per frame.
       With a bottom-to-top in-place scan, allowing the powder to own this swap
       relays the same Steam parcel through the entire pile in one turn. */
    w.reset();
    const int sandSinkX = 970, pileTop = 680, pileBottom = 689;
    const int initialSteamY = pileBottom + 1;
    w.setLiveWindow(sandSinkX - 4, pileTop - 4,
                    sandSinkX + 4, initialSteamY + 4);
    for (int y = pileTop - 1; y <= initialSteamY + 1; ++y) {
        w.setCell(sandSinkX - 1, y, MAT_STONE);
        w.setCell(sandSinkX + 1, y, MAT_STONE);
    }
    w.setCell(sandSinkX, initialSteamY + 1, MAT_STONE);
    for (int y = pileTop; y <= pileBottom; ++y)
        w.setCell(sandSinkX, y, MAT_SAND);
    w.setCell(sandSinkX, initialSteamY, MAT_STEAM);
    w.temp[initialSteamY * SIM_W + sandSinkX] = degC(120);
    w.step();
    if (w.at(sandSinkX, pileBottom).mat != MAT_STEAM ||
        w.at(sandSinkX, initialSteamY).mat != MAT_SAND) {
        fprintf(stderr, "Steam did not percolate exactly one Sand cell (%u at %d, %u below)\n",
                w.at(sandSinkX, pileBottom).mat, pileBottom,
                w.at(sandSinkX, initialSteamY).mat); return 113;
    }

    for (int tick = 0; tick < 4; ++tick) {
        for (int y = pileTop; y <= initialSteamY; ++y)
            if (w.at(sandSinkX, y).mat == MAT_STEAM) {
                w.temp[y * SIM_W + sandSinkX] = degC(120);
                w.dirtyPoint(sandSinkX, y);
            }
        w.step();
    }
    int sunkSand = 0, sinkSteam = 0, sinkSteamY = -1, sinkExcess = 0;
    for (int y = pileTop; y <= initialSteamY; ++y) {
        if (w.at(sandSinkX, y).mat == MAT_SAND) ++sunkSand;
        if (w.at(sandSinkX, y).mat == MAT_STEAM) {
            ++sinkSteam;
            sinkSteamY = y;
            sinkExcess += w.at(sandSinkX, y).moisture & GAS_EXCESS_MASK;
        }
    }
    if (sunkSand != 10 || sinkSteam != 1 ||
        sinkSteamY != initialSteamY - 5 || sinkExcess != 0) {
        fprintf(stderr, "Steam percolation did not remain one cell per frame (%d Sand, %d Steam at y %d, %d excess)\n",
                sunkSand, sinkSteam, sinkSteamY, sinkExcess); return 113;
    }

    /* Pressure stored throughout the center of a broad Steam pocket reaches
       its Water boundary in one turn. Adjacent-only equalization needs twenty
       turns just to reach this 41-cell blob's skin, so no new visible gas
       volume would exist after this step under the old rule. */
    w.reset();
    const int blobX = 980, blobTop = 700, blobSize = 41;
    const int blobBottom = blobTop + blobSize - 1;
    w.setLiveWindow(blobX - 6, blobTop - 10,
                    blobX + blobSize + 5, blobBottom + 5);
    for (int y = blobTop - 10; y <= blobBottom + 1; ++y) {
        w.setCell(blobX - 1, y, MAT_STONE);
        w.setCell(blobX + blobSize, y, MAT_STONE);
    }
    for (int x = blobX; x < blobX + blobSize; ++x) {
        w.setCell(x, blobBottom + 1, MAT_STONE);
        for (int y = blobTop; y <= blobBottom; ++y)
            w.setCell(x, y, MAT_STEAM);
        for (int y = blobTop - 4; y < blobTop; ++y)
            w.setCell(x, y, MAT_WATER);
    }
    const int blobCenterX = blobX + blobSize / 2;
    const int blobCenterY = blobTop + blobSize / 2;
    const int chargedRadius = 4;
    const int chargedCells = (chargedRadius * 2 + 1) * (chargedRadius * 2 + 1);
    for (int y = blobCenterY - chargedRadius; y <= blobCenterY + chargedRadius; ++y)
        for (int x = blobCenterX - chargedRadius; x <= blobCenterX + chargedRadius; ++x) {
            w.cells[y * SIM_W + x].moisture = 5;
            w.temp[y * SIM_W + x] = degC(120);
            w.dirtyPoint(x, y);
        }
    w.step();
    int blobSteam = 0, blobWater = 0, blobExcess = 0;
    for (int y = blobTop - 10; y <= blobBottom; ++y)
        for (int x = blobX; x < blobX + blobSize; ++x) {
            const Cell& bc = w.at(x, y);
            if (bc.mat == MAT_STEAM) {
                ++blobSteam;
                blobExcess += bc.moisture & GAS_EXCESS_MASK;
            }
            if (bc.mat == MAT_WATER) ++blobWater;
        }
    const int initialBlobSteam = blobSize * blobSize;
    const int initialBlobVolume = initialBlobSteam + chargedCells * 5;
    if (blobSteam != initialBlobSteam + blobSize * 5 || blobWater != blobSize * 4 ||
        blobSteam + blobExcess != initialBlobVolume) {
        fprintf(stderr, "shared pocket pressure did not decompress broad Steam blob (%d steam, %d water, %d excess)\n",
                blobSteam, blobWater, blobExcess); return 108;
    }

    /* A crooked connected pocket cannot use any straight pressure ray. The
       bounded flood must carry the charge around the corner to the Water face
       without changing the number of pre-existing gas or liquid cells. */
    w.reset();
    const int curveX = 1100, curveY = 720;
    w.setLiveWindow(curveX - 6, curveY - 16, curveX + 12, curveY + 5);
    for (int y = curveY - 12; y <= curveY + 2; ++y)
        for (int x = curveX - 2; x <= curveX + 7; ++x)
            w.setCell(x, y, MAT_STONE);
    for (int x = curveX; x <= curveX + 5; ++x)
        w.setCell(x, curveY, MAT_STEAM);
    for (int y = curveY - 6; y < curveY; ++y)
        w.setCell(curveX + 5, y, MAT_STEAM);
    for (int y = curveY - 10; y < curveY - 6; ++y)
        w.setCell(curveX + 5, y, MAT_WATER);
    w.setCell(curveX + 5, curveY - 11, MAT_EMPTY);
    w.cells[curveY * SIM_W + curveX].moisture = 5;
    w.temp[curveY * SIM_W + curveX] = degC(120);
    w.dirtyPoint(curveX, curveY);
    w.step();
    int curvedSteam = 0, curvedWater = 0, curvedExcess = 0;
    for (int y = curveY - 11; y <= curveY; ++y)
        for (int x = curveX; x <= curveX + 5; ++x) {
            const Cell& cc = w.at(x, y);
            if (cc.mat == MAT_STEAM) {
                ++curvedSteam;
                curvedExcess += cc.moisture & GAS_EXCESS_MASK;
            }
            if (cc.mat == MAT_WATER) ++curvedWater;
        }
    if (curvedSteam != 13 || curvedWater != 4 || curvedExcess != 4 ||
        curvedSteam + curvedExcess != 17) {
        fprintf(stderr, "shared pressure did not follow curved Steam pocket (%d steam, %d water, %d excess)\n",
                curvedSteam, curvedWater, curvedExcess); return 109;
    }

    /* A broad liquid face may advance into a sealed gas pocket by displacing
       it. Before this rule, liquid/gas exchange belonged only to
       the rising gas turn, so Water flowing sideways treated a large Steam
       body as a solid wall. Count physical cells PLUS stored excess: the Water
       must enter, every Steam volume must remain, and its sole condensation
       owner must survive the compression. */
    w.reset();
    const int shoveX = 900, shoveY = 700;
    w.setLiveWindow(shoveX - 30, shoveY - 20, shoveX + 20, shoveY + 10);
    for (int y = shoveY - 11; y <= shoveY + 1; ++y)
        for (int x = shoveX - 21; x <= shoveX + 10; ++x)
            w.setCell(x, y, MAT_STONE);
    int initialShoveVolumes = 0;
    for (int y = shoveY - 10; y <= shoveY; ++y)
        for (int x = shoveX - 20; x < shoveX; ++x) {
            w.setCell(x, y, MAT_STEAM);
            w.cells[y * SIM_W + x].moisture = GAS_VOLUME_ONLY;
            ++initialShoveVolumes;
        }
    /* Make every Steam cell a real condensation owner. This is deliberately
       the case pressure cannot merge: Water still has to advance by swapping
       the complete Steam parcel into its old space, without losing an owner. */
    for (int y = shoveY - 10; y <= shoveY; ++y)
        for (int x = shoveX - 20; x < shoveX; ++x)
            w.cells[y * SIM_W + x].moisture = 0;
    for (int x = shoveX; x < shoveX + 8; ++x)
        w.setCell(x, shoveY, MAT_WATER);
    w.dirtyArea(shoveX - 20, shoveY - 10, shoveX + 7, shoveY);
    w.step();

    int shoveSteamCells = 0, shoveExcess = 0, shoveOwners = 0;
    int waterEnteredSteam = 0;
    for (int y = shoveY - 10; y <= shoveY; ++y)
        for (int x = shoveX - 20; x <= shoveX + 7; ++x) {
            const Cell& sc = w.at(x, y);
            if (sc.mat == MAT_WATER && x < shoveX) ++waterEnteredSteam;
            if (sc.mat != MAT_STEAM) continue;
            ++shoveSteamCells;
            shoveExcess += sc.moisture & GAS_EXCESS_MASK;
            if (!(sc.moisture & GAS_VOLUME_ONLY)) ++shoveOwners;
        }
    if (waterEnteredSteam < 2 || shoveSteamCells + shoveExcess != initialShoveVolumes ||
        shoveOwners != initialShoveVolumes) {
        fprintf(stderr, "Water did not displace sealed Steam pocket (entered %d, volumes %d/%d, owners %d)\n",
                waterEnteredSteam, shoveSteamCells + shoveExcess,
                initialShoveVolumes, shoveOwners); return 110;
    }

    /* Pressure lifts a connected water column immediately instead of waiting
       for the original steam cell to bubble through it one pixel per frame. */
    w.reset();
    const int liftX = 820, liftY = 700, liftDepth = 24;
    w.setLiveWindow(liftX - 4, liftY - liftDepth - 4, liftX + 4, liftY + 4);
    for (int y = liftY - liftDepth; y <= liftY; ++y) {
        w.setCell(liftX - 1, y, MAT_STONE);
        w.setCell(liftX + 1, y, MAT_STONE);
    }
    w.setCell(liftX, liftY + 1, MAT_STONE);
    for (int y = liftY - liftDepth + 1; y < liftY; ++y)
        w.setCell(liftX, y, MAT_WATER);
    w.setCell(liftX, liftY, MAT_STEAM);
    w.cells[liftY * SIM_W + liftX].moisture = 5;
    w.temp[liftY * SIM_W + liftX] = degC(120);
    w.dirtyPoint(liftX, liftY);
    w.step();
    int liftedWater = 0, liftedSteam = 0, liftedExcess = 0;
    for (int y = liftY - liftDepth - 4; y <= liftY; ++y) {
        if (w.at(liftX, y).mat == MAT_WATER) ++liftedWater;
        if (w.at(liftX, y).mat == MAT_STEAM) {
            ++liftedSteam;
            liftedExcess += w.at(liftX, y).moisture & GAS_EXCESS_MASK;
        }
    }
    if (liftedWater != liftDepth - 1 || liftedSteam != 6 || liftedExcess != 0 ||
        w.at(liftX, liftY - liftDepth - 4).mat != MAT_WATER) {
        fprintf(stderr, "steam pressure did not lift deep water (%d water, %d steam, %d excess, top %u)\n",
                liftedWater, liftedSteam, liftedExcess,
                w.at(liftX, liftY - liftDepth - 4).mat); return 102;
    }

    /* A genuinely deep lake must route pressure to its distant surface. The
       pressure source also touches a sealed side pocket of Water: merely
       touching liquid is not an outlet and must not trap the pocket's shared
       pressure at this underwater dead end. */
    w.reset();
    const int lakeX = 1260, lakeGasTop = 830, lakeGasBottom = 930;
    const int lakeDepth = 160, lakeSurface = lakeGasTop - lakeDepth;
    w.setLiveWindow(lakeX - 5, lakeSurface - 8, lakeX + 5, lakeGasBottom + 4);
    for (int y = lakeSurface - 1; y <= lakeGasBottom + 1; ++y) {
        w.setCell(lakeX - 1, y, MAT_STONE);
        w.setCell(lakeX + 1, y, MAT_STONE);
    }
    w.setCell(lakeX, lakeGasBottom + 1, MAT_STONE);
    for (int y = lakeSurface; y < lakeGasTop; ++y)
        w.setCell(lakeX, y, MAT_WATER);
    for (int y = lakeGasTop; y <= lakeGasBottom; ++y) {
        w.setCell(lakeX, y, MAT_STEAM);
        w.temp[y * SIM_W + lakeX] = degC(120);
    }
    const int lakeSourceY = lakeGasTop + 75;
    w.setCell(lakeX - 1, lakeSourceY, MAT_WATER);
    w.setCell(lakeX - 2, lakeSourceY, MAT_STONE);
    w.cells[lakeSourceY * SIM_W + lakeX].moisture = 5;
    w.dirtyPoint(lakeX, lakeSourceY);
    w.step();
    int lakeWater = 0, lakeSteam = 0, lakeExcess = 0;
    for (int y = lakeSurface - 8; y <= lakeGasBottom; ++y)
        for (int x = lakeX - 1; x <= lakeX; ++x) {
            if (w.at(x, y).mat == MAT_WATER) ++lakeWater;
            if (w.at(x, y).mat == MAT_STEAM) {
                ++lakeSteam;
                lakeExcess += w.at(x, y).moisture & GAS_EXCESS_MASK;
            }
        }
    const int initialLakeSteam = lakeGasBottom - lakeGasTop + 1;
    if (lakeWater != lakeDepth + 1 || lakeSteam != initialLakeSteam + 5 ||
        lakeExcess != 0) {
        fprintf(stderr, "deep-lake pressure chose a false underwater outlet (%d water, %d steam, %d excess)\n",
                lakeWater, lakeSteam, lakeExcess); return 110;
    }

    /* The bounded liquid search follows a bent connected path to its outlet;
       this catches implementations that only special-case a vertical column. */
    w.reset();
    const int bendX = 860, bendY = 700;
    w.setLiveWindow(bendX - 6, bendY - 7, bendX + 9, bendY + 4);
    for (int y = bendY - 4; y <= bendY + 1; ++y)
        for (int x = bendX - 2; x <= bendX + 6; ++x)
            w.setCell(x, y, MAT_STONE);
    const int pathX[5] = { bendX, bendX, bendX + 1, bendX + 2, bendX + 3 };
    const int pathY[5] = { bendY - 1, bendY - 2, bendY - 2, bendY - 2, bendY - 2 };
    for (int k = 0; k < 5; ++k) w.setCell(pathX[k], pathY[k], MAT_WATER);
    w.setCell(bendX + 4, bendY - 2, MAT_EMPTY);
    w.setCell(bendX, bendY, MAT_STEAM);
    w.cells[bendY * SIM_W + bendX].moisture = 5;
    w.temp[bendY * SIM_W + bendX] = degC(120);
    w.dirtyPoint(bendX, bendY);
    w.step();
    int bentWater = 0, bentSteam = 0;
    for (int y = bendY - 4; y <= bendY; ++y)
        for (int x = bendX - 1; x <= bendX + 4; ++x) {
            if (w.at(x, y).mat == MAT_WATER) ++bentWater;
            if (w.at(x, y).mat == MAT_STEAM) ++bentSteam;
        }
    if (bentWater != 5 || bentSteam != 2 ||
        w.at(bendX + 4, bendY - 2).mat != MAT_WATER ||
        w.at(bendX, bendY - 1).mat != MAT_STEAM) {
        fprintf(stderr, "steam pressure did not follow bent liquid outlet (%d water, %d steam, outlet %u, inlet %u)\n",
                bentWater, bentSteam, w.at(bendX + 4, bendY - 2).mat,
                w.at(bendX, bendY - 1).mat); return 103;
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

    /* Water convection carries the hot parcel upward three cells per frame;
       this is directional heat transport, separate from symmetric conduction. */
    w.reset();
    const int convectX = 730, convectBottom = 680, convectHeight = 16;
    w.setLiveWindow(convectX - 4, convectBottom - convectHeight - 4,
                    convectX + 4, convectBottom + 4);
    for (int y = convectBottom - convectHeight; y <= convectBottom + 1; ++y) {
        w.setCell(convectX - 1, y, MAT_RUBBER);
        w.setCell(convectX + 1, y, MAT_RUBBER);
    }
    w.setCell(convectX, convectBottom + 1, MAT_RUBBER);
    for (int y = convectBottom - convectHeight + 1; y <= convectBottom; ++y) {
        w.setCell(convectX, y, MAT_WATER);
        w.temp[y * SIM_W + convectX] = AMBIENT_TEMP;
    }
    w.temp[convectBottom * SIM_W + convectX] = degC(90);
    w.cells[convectBottom * SIM_W + convectX].moisture = 123;
    w.dirtyPoint(convectX, convectBottom);
    w.step();
    int hottestWaterY = convectBottom, hottestWaterTemp = -1;
    int markedWaterY = convectBottom;
    for (int y = convectBottom; y > convectBottom - convectHeight; --y) {
        if (w.at(convectX, y).mat == MAT_WATER &&
            (int)w.temp[y * SIM_W + convectX] > hottestWaterTemp) {
            hottestWaterTemp = w.temp[y * SIM_W + convectX];
            hottestWaterY = y;
        }
        if (w.at(convectX, y).mat == MAT_WATER &&
            w.at(convectX, y).moisture == 123) markedWaterY = y;
    }
    if (markedWaterY > convectBottom - 2) {
        fprintf(stderr, "Water convection did not carry its hot parcel upward quickly (marker y %d, hottest %d at y %d; %u %u %u %u)\n",
                markedWaterY, hottestWaterTemp, hottestWaterY,
                (unsigned)w.temp[convectBottom * SIM_W + convectX],
                (unsigned)w.temp[(convectBottom - 1) * SIM_W + convectX],
                (unsigned)w.temp[(convectBottom - 2) * SIM_W + convectX],
                (unsigned)w.temp[(convectBottom - 3) * SIM_W + convectX]); return 112;
    }

    /* The stronger reach is a fluid rule, not a Water special case. Repeat
       the marked-parcel check with Glowfluid, which has no phase transition
       that could satisfy the test by replacing the parcel. */
    w.reset();
    const int glowConvectX = convectX + 20;
    w.setLiveWindow(glowConvectX - 4, convectBottom - convectHeight - 4,
                    glowConvectX + 4, convectBottom + 4);
    for (int y = convectBottom - convectHeight; y <= convectBottom + 1; ++y) {
        w.setCell(glowConvectX - 1, y, MAT_RUBBER);
        w.setCell(glowConvectX + 1, y, MAT_RUBBER);
    }
    w.setCell(glowConvectX, convectBottom + 1, MAT_RUBBER);
    for (int y = convectBottom - convectHeight + 1; y <= convectBottom; ++y) {
        w.setCell(glowConvectX, y, MAT_GLOWFLUID);
        w.temp[y * SIM_W + glowConvectX] = AMBIENT_TEMP;
    }
    w.temp[convectBottom * SIM_W + glowConvectX] = degC(90);
    w.cells[convectBottom * SIM_W + glowConvectX].moisture = 123;
    w.dirtyPoint(glowConvectX, convectBottom);
    w.step();
    int markedGlowY = convectBottom;
    for (int y = convectBottom; y > convectBottom - convectHeight; --y)
        if (w.at(glowConvectX, y).mat == MAT_GLOWFLUID &&
            w.at(glowConvectX, y).moisture == 123) markedGlowY = y;
    if (markedGlowY > convectBottom - 2) {
        fprintf(stderr, "Glowfluid did not use full-strength convection (marker y %d)\n",
                markedGlowY); return 114;
    }

    /* The packed-pool fast path must retain convection. This marked hot parcel
       has all eight neighbours occupied by Fuel, so only that path can move
       it during this frame. */
    w.reset();
    const int packedX0 = 760, packedX1 = 766;
    const int packedTop = 710, packedBottom = 720, packedMarkerX = 763;
    const int packedMarkerY = 718;
    w.setLiveWindow(packedX0 - 4, packedTop - 4,
                    packedX1 + 4, packedBottom + 4);
    for (int x = packedX0 - 1; x <= packedX1 + 1; ++x) {
        w.setCell(x, packedTop - 1, MAT_RUBBER);
        w.setCell(x, packedBottom + 1, MAT_RUBBER);
    }
    for (int y = packedTop; y <= packedBottom; ++y) {
        w.setCell(packedX0 - 1, y, MAT_RUBBER);
        w.setCell(packedX1 + 1, y, MAT_RUBBER);
        for (int x = packedX0; x <= packedX1; ++x) {
            w.setCell(x, y, MAT_FUEL);
            w.temp[y * SIM_W + x] = AMBIENT_TEMP;
        }
    }
    w.temp[packedMarkerY * SIM_W + packedMarkerX] = degC(90);
    w.cells[packedMarkerY * SIM_W + packedMarkerX].moisture = 124;
    w.dirtyPoint(packedMarkerX, packedMarkerY);
    w.step();
    int packedMarkerAfterY = packedMarkerY;
    for (int y = packedTop; y <= packedBottom; ++y)
        if (w.at(packedMarkerX, y).mat == MAT_FUEL &&
            w.at(packedMarkerX, y).moisture == 124) packedMarkerAfterY = y;
    if (packedMarkerAfterY > packedMarkerY - 2) {
        fprintf(stderr, "packed-fluid fast path suppressed convection (%d -> %d)\n",
                packedMarkerY, packedMarkerAfterY); return 115;
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
