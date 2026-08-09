#include "multiplayer.h"

PlayerSession g_playerSessions[MAX_PLAYERS];
Player& g_player = g_playerSessions[LOCAL_PLAYER_ID].body;
Inventory& g_inv = g_playerSessions[LOCAL_PLAYER_ID].inventory;

static void clearSession(PlayerSession& s) {
    s.inventory.clear();
    s.body.reset(0.0f, 0.0f);
    s.connected = false;
    s.local = false;
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
        return (PlayerId)i;
    }
    return PLAYER_NONE;
}

void playerSessionClose(PlayerId id) {
    if (id == LOCAL_PLAYER_ID || id >= MAX_PLAYERS) return;
    PlayerSession& s = g_playerSessions[id];
    ++s.generation;
    if (!s.generation) ++s.generation;
    clearSession(s);
}

bool playerSessionConnected(PlayerId id) {
    return id < MAX_PLAYERS && g_playerSessions[id].connected;
}

