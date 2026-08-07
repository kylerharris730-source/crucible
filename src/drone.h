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

struct Drone {
    u8 type;
    float x, y, vx, vy;
    int shotCool;
    int effectCool;
};

extern Drone g_drones[MAX_DRONES];

void droneReset();
void droneTick(const World& w, const Player& p, Inventory& inv);
void droneRegisterLights();
void droneDraw(u32* px, int camX, int camY, bool lit);
int  droneCount();
