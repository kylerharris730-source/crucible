#include "multiplayer.h"

PlayerSession g_playerSessions[MAX_PLAYERS];
Player& g_player = g_playerSessions[LOCAL_PLAYER_ID].body;
Inventory& g_inv = g_playerSessions[LOCAL_PLAYER_ID].inventory;

static void clearSession(PlayerSession& s) {
    s.inventory.clear();
    s.cursor = ItemStack();
    s.trash = ItemStack();
    s.body.reset(0.0f, 0.0f);
    s.previousAimX = s.previousAimY = -1;
    s.digCooldown = 0;
    s.restBed = -1;
    s.openDevice = -1;
    s.wireX = s.wireY = -1;
    s.circuitWireFrom = -1; s.circuitWirePort = 0;
    s.suppressRightUse = false;
    s.lineActive = false; s.lineBits = s.lineSelected = 0; s.lineRadius = 1;
    s.lineBackground = s.lineOverwrite = false;
    s.previousCommandBits = 0;
    s.connected = false;
    s.local = false;
    s.networkId = PLAYER_NONE;
}

void playerSessionsReset() {
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        /* Increment instead of resetting: packets from before a world/session
           reset must not become valid again merely because slot numbers match. */
        ++g_playerSessions[i].generation;
        if (!g_playerSessions[i].generation) ++g_playerSessions[i].generation;
        clearSession(g_playerSessions[i]);
    }
    g_playerSessions[LOCAL_PLAYER_ID].connected = true;
    g_playerSessions[LOCAL_PLAYER_ID].local = true;
    g_playerSessions[LOCAL_PLAYER_ID].networkId = LOCAL_PLAYER_ID;
}

PlayerId playerSessionOpen(bool local, float spawnX, float spawnY) {
    for (int i = 1; i < MAX_PLAYERS; ++i) {
        PlayerSession& s = g_playerSessions[i];
        if (s.connected) continue;
        ++s.generation;
        if (!s.generation) ++s.generation;
        clearSession(s);
        s.body.reset(spawnX, spawnY);
        s.connected = true;
        s.local = local;
        s.networkId = (PlayerId)i; /* authoritative host slots map 1:1 */
        return (PlayerId)i;
    }
    return PLAYER_NONE;
}

void playerSessionClose(PlayerId id) {
    if (id == LOCAL_PLAYER_ID || id >= MAX_PLAYERS) return;
    PlayerSession& s = g_playerSessions[id];
    for (int i = 0; i < INV_SLOTS; ++i) if (s.inventory.slot[i].inst) toolInstFree(s.inventory.slot[i].inst);
    for (int i = 0; i < EQ_COUNT; ++i) if (s.inventory.equip[i].inst) toolInstFree(s.inventory.equip[i].inst);
    for (int d = 0; d < 3; ++d) /* Inventory owns three fixed drone bays. */
        for (int m = 0; m < Inventory::DRONE_MODULE_SLOTS_MAX; ++m)
            if (s.inventory.droneModule[d][m].inst) toolInstFree(s.inventory.droneModule[d][m].inst);
    if (s.cursor.inst) toolInstFree(s.cursor.inst);
    if (s.trash.inst) toolInstFree(s.trash.inst);
    ++s.generation;
    if (!s.generation) ++s.generation;
    clearSession(s);
}

bool playerSessionConnected(PlayerId id) {
    return id < MAX_PLAYERS && g_playerSessions[id].connected;
}

int playerSessionSlotForNetworkId(PlayerId networkId) {
    for (int slot = 0; slot < MAX_PLAYERS; ++slot)
        if (g_playerSessions[slot].connected &&
            g_playerSessions[slot].networkId == networkId) return slot;
    return -1;
}
