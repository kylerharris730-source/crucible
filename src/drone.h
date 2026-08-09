#pragma once
#include "world.h"
#include "player.h"
#include "item.h"
#include "multiplayer.h"

/* Friendly drones are overlays, like the player and projectiles. They never
   occupy grid cells, so a follower cannot obstruct sand, seal a room, or carve
   a tunnel merely by moving. The equipped item is the durable state; these
   positions are rebuilt from it whenever a world starts. */
/* Two invisible flight envelopes. Idle drones settle in the small pocket above
   the head; task positions must remain inside the larger player-centred disc. */
static const int DRONE_HOME_RADIUS = 18;
static const int DRONE_TASK_RADIUS = 110;
static const int DRONE_HOME_ABOVE  = 16;

/* Compatibility name for single-player diagnostics; storage is owned by the
   stable session rather than a parallel global bank. */
#define g_drones (g_playerSessions[0].drones)

void droneReset();
void droneTick(const World& w, const Player& p, Inventory& inv);
void droneTickFor(int playerSlot, const World& w, const Player& p, Inventory& inv);
void droneRegisterLights();
void droneDraw(u32* px, int camX, int camY, bool lit);
int  droneCount();
