#pragma once
#include "common.h"

struct World;

/* --- bee routes: how a bee gets round a base ----------------------------------

   Reported from play: "bee pathfinding is still too dumb". Measured in
   tests/bee_routes.cpp before this existed, a five-bee colony over a hundred
   seconds made ZERO round trips past a wall it could have flown over, out of a
   room through its door, up to a ledge, or through two baffles -- and only ten
   in open air, for the separate reason given at beeFindFlower.

   A bee used to steer straight at its target with a sixteen-cell look-ahead.
   That dodges a pillar. It cannot find a way round anything longer than the
   look-ahead, and a player's base is made of exactly those things.

   The answer is one breadth-first FIELD per hive, rooted at the hive's door,
   rather than a path per bee. It is the same choice the enemy flow field in
   navigate.h makes, for the same reasons -- one search serves the whole colony,
   nothing is stored per bee, and a terrain change is absorbed by the next
   rebuild -- but it is a separate field because it answers a separate question:
   navigate.cpp's is seeded from the PLAYERS.

   One field gives a colony everything it needs:

     - HOME is downhill. Wherever a bee is, following the distances down leads
       to the door.
     - WORK is the nearest flowers the search REACHED, in the order it reached
       them. A flower the colony cannot get to is never chosen, so a bee does
       not fixate on one behind glass.
     - OUT to a flower is the same downhill chain read from the flower's end.
       The bee flies at the furthest point of it that it can already see.

   Nodes are two cells, half a bee, so any gap five cells wide is always
   flyable and a four-cell one is flyable half the time. */

/* Which flower should a bee from this root go to? `key` names the field: a
   hive's device index, or MAX_DEVICES + an entity slot for a bee with no hive.
   Picks at random among the nearest few reachable flowers so a colony spreads
   over a bed rather than queueing at one blossom. False if none is reachable. */
bool beeRouteFlower(const World& w, int key, float rootX, float rootY,
                    int* flowerX, int* flowerY);

/* Where should a bee at (bx, by) fly next, on the way to the field's root
   (toFlower false) or to the flower at (fx, fy) (toFlower true)? Writes a point
   in straight, clear flight of the bee's whole body. False when the field
   cannot help -- the bee is outside it or somewhere it never reached -- and the
   caller keeps steering straight at its target. */
bool beeRouteWaypoint(const World& w, int key, float rootX, float rootY,
                      float bx, float by, bool toFlower, int fx, int fy,
                      float* wayX, float* wayY);

/* Advance the clock fields age by. Once per creature tick. */
void beeRouteTick();

/* Forget every field. For world loads and the tests. */
void beeRouteReset();

/* Diagnostics, for the harness: field rebuilds since the last reset. */
int beeRouteBuilds();
