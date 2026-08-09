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

/* Per-player overlay state belongs to the stable slot as surely as the body
   and inventory do. Keeping drones and passive cooldowns here prevents a new
   gameplay system from quietly growing another parallel "player 0" global. */
enum DroneType { DRONE_NONE = 0, DRONE_LIGHT, DRONE_ATTACK, DRONE_PICKUP, DRONE_SHIELD, DRONE_COUNT };
static const int MAX_DRONES = 3;
struct Drone {
    u8 type;
    float x, y, vx, vy;
    int shotCool;
    int effectCool;
};

struct PlayerSession {
    Player body;
    Inventory inventory;
    Drone drones[MAX_DRONES];
    i32 garlicCooldown;
    ItemStack cursor;
    ItemStack trash;
    /* Host-only input/runtime state. It belongs to the player rather than the
       window: two people can hold use, mine on cooldown, and rest in different
       beds independently. These fields are harmless on clients and are reset
       at every generation boundary. */
    i32 previousAimX, previousAimY;
    i32 digCooldown;
    i32 restBed;
    i32 openDevice;
    i32 wireX, wireY;
    i32 circuitWireFrom;
    u8 circuitWirePort;
    bool suppressRightUse;
    bool lineActive;
    u8 lineBits, lineSelected, lineRadius;
    i16 lineBrush;
    bool lineBackground, lineOverwrite;
    bool lineFilterOn;
    u8 lineFilter[(MAT_COUNT + 7) / 8];
    u8 previousCommandBits;
    bool connected;
    bool local;
    PlayerId networkId;  /* authority's id; differs from local slot on clients */
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
int playerSessionSlotForNetworkId(PlayerId networkId);
/* Retain the joined player's body/inventory when leaving a host, discard
   replicated peers, and return slot zero to ordinary offline identity. */
void playerSessionsReturnToOffline();
