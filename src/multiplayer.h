#pragma once
#include "item.h"

/* Multiplayer begins with identity, not sockets. A network packet, projectile,
   pickup claim or inventory command all need to name the same player for the
   lifetime of a connection. Slots are deliberately small and stable: the host
   can address them with one byte, while generation prevents a late packet from
   a disconnected player being mistaken for whoever later reuses that slot. */
typedef u8 PlayerId;
static const int MAX_PLAYERS = 4;
static const PlayerId PLAYER_NONE = 0xFF;
static const PlayerId LOCAL_PLAYER_ID = 0;

struct PlayerSession {
    Player body;
    Inventory inventory;
    bool connected;
    bool local;
    u16 generation;
};

extern PlayerSession g_playerSessions[MAX_PLAYERS];

/* Keeps today's single-player call sites source-compatible while making their
   ownership explicit. These are references to session zero, not second copies
   of its body and inventory. */
extern Player& g_player;
extern Inventory& g_inv;

/* Establish one local host player and clear every unused slot. Called before a
   world hands out its starting kit, so that kit lands in session zero's pack. */
void playerSessionsReset();

/* Reserves the first free non-host slot. Networking will call this after a
   handshake; tests can use it now to prove player state is independent. */
PlayerId playerSessionOpen(bool local, float spawnX, float spawnY);
void playerSessionClose(PlayerId id);
bool playerSessionConnected(PlayerId id);

