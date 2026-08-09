#pragma once
#include "world.h"
#include "player.h"
#include "item.h"

/* Friendly drones are overlays, like the player and projectiles. They never
   occupy grid cells, so a follower cannot obstruct sand, seal a room, or carve
   a tunnel merely by moving. The equipped item is the durable state; these
   positions are rebuilt from it whenever a world starts. */
enum DroneType { DRONE_NONE = 0, DRONE_LIGHT, DRONE_ATTACK, DRONE_PICKUP, DRONE_SHIELD, DRONE_COUNT };
static const int MAX_DRONES = 3;  /* one dedicated light bay plus two versatile drone bays */
/* Two invisible flight envelopes. Idle drones settle in the small pocket above
   the head; task positions must remain inside the larger player-centred disc. */
static const int DRONE_HOME_RADIUS = 18;
static const int DRONE_TASK_RADIUS = 110;
static const int DRONE_HOME_ABOVE  = 16;

struct Drone {
    u8 type;
    float x, y, vx, vy;
    int shotCool;
    int effectCool;
};

/* One overlay bank per stable player slot. The compatibility macro keeps the
   original single-player diagnostics aimed at local slot zero. */
extern Drone g_dronesByPlayer[4][MAX_DRONES];
#define g_drones (g_dronesByPlayer[0])

void droneReset();
void droneTick(const World& w, const Player& p, Inventory& inv);
void droneTickFor(int playerSlot, const World& w, const Player& p, Inventory& inv);
void droneRegisterLights();
void droneDraw(u32* px, int camX, int camY, bool lit);
int  droneCount();
