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
/* --- weapons are drones ----------------------------------------------------
   Vampire Survivors splits into weapons that fire on their own and passives
   that modify everything, and this game already has the two sinks to put them
   in: companions and trinkets. So a weapon here is not a thing you hold, it is
   a CHASSIS you equip, and the difference between owning a whip and owning a
   magic wand is which drone is flying beside you.

   That is worth doing rather than adding held weapons for one reason: the
   player already has a held weapon, and it is aimed. An autonomous weapon and an
   aimed weapon are different games stapled together if both live in the hand,
   and they compose beautifully if one of them is a follower -- the drone covers
   what you are not looking at, which is exactly the job an aimed weapon cannot
   do for itself.

   Each of the three new chassis is ONE legible idea rather than a spread of
   stats, on the same rule the creature roster is built to: the lance fires
   where you FACE, the mortar fires over cover, and the orbit does not fire at
   all. Told apart by watching them for two seconds, which a damage number and a
   cooldown number never manage. */
enum DroneType {
    DRONE_NONE = 0, DRONE_LIGHT, DRONE_ATTACK, DRONE_PICKUP, DRONE_SHIELD,
    /* Appended, like every other id in this codebase that crosses a wire. */
    DRONE_LANCE,    /* bursts of flat piercing bolts along the player's facing */
    DRONE_MORTAR,   /* one heavy lobbed shell that bursts on arrival */
    DRONE_ORBIT,    /* no shots at all: a blade circling the player */
    DRONE_COUNT
};
static const int MAX_DRONES = 3;
struct Drone {
    u8 type;
    float x, y, vx, vy;
    int shotCool;
    int effectCool;
    /* Where an orbiting drone is round its circle, in radians. A real field
       rather than a number derived from a global frame counter, because two
       orbit drones in the two general bays have to be on OPPOSITE sides of the
       player -- a derived angle would put them both in the same place and the
       second one would be invisible. Unused by every other chassis.

       Also carries the burst position for the lance: an idle counter and a
       shot counter are the same clock read at different scales, and giving the
       lance its own int would be a third meaning for `shotCool`. */
    float phase;
    /* Shots left in the current burst. Zero on everything that fires singly,
       which is every chassis but the lance. */
    int burst;
};

struct PlayerSession {
    Player body;
    Inventory inventory;
    Drone drones[MAX_DRONES];
    i32 garlicCooldown;
    /* Frames since the Husk Heart last returned a point. Beside the garlic
       clock rather than on the Player, for the reason this whole struct exists:
       a passive is a property of the SLOT, not of the body, and the body is
       replicated across a network where this is nobody else's business. */
    i32 regenTimer;
    ItemStack cursor;
    ItemStack trash;
    /* Host-only input/runtime state. It belongs to the player rather than the
       window: two people can hold use, mine on cooldown, and rest in different
       beds independently. These fields are harmless on clients and are reset
       at every generation boundary. */
    i32 previousAimX, previousAimY;
    i32 digCooldown;
    /* --- the swing ------------------------------------------------------
       A melee stroke is a thing that happens OVER TIME, so it needs somewhere
       to remember that it is happening. Here rather than on Player for the
       reason garlicCooldown is here: Player is written to the save as a raw
       sized block, so a field on it would discard every existing character, and
       a swing is not worth that. It is also nobody else's business -- the host
       resolves the damage, and a client has no decision to make about it.

       The consequence to know: another player's swing is NOT replicated, so in
       a four-player game you see their creatures take damage without seeing the
       blade that did it. The same is already true of every held tool (see
       drawHeldTool, which draws session zero's), so this is the existing
       limitation rather than a new one.

       `swingFrame` counts DOWN through the stroke, so zero means idle and the
       animation reads its progress from how far it has left to go. `swingCool`
       is the separate rhythm gate -- see ItemDef::meleeCooldown for why the two
       are not one number.

       `swingDirX/Y` is committed when the stroke STARTS and never revisited.
       That is the whole feel of a melee weapon: a swing you could steer
       mid-stroke would track the cursor round like a turret, and the moment of
       deciding where to point would stop existing. */
    i32 swingFrame;
    i32 swingCool;
    float swingDirX, swingDirY;
    /* Which creatures this stroke has already struck. One bit each, so a blade
       that sweeps across a body over nine frames takes health off once rather
       than nine times -- which is not a rounding error, it is the difference
       between a copper sword and an instant kill. Cleared when a stroke
       begins. */
    u8 swingHit[(96 + 7) / 8];
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
