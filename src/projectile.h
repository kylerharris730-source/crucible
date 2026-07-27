#pragma once
#include "world.h"

/* --- projectiles -----------------------------------------------------------

   Entities, not cells, for exactly the reasons the player is one (see
   player.h): a projectile that lived in the grid would need a density, a
   movement rule, and an answer for what a falling-sand rule should do when it
   tries to swap two of them. As an overlay it needs none of that, and it can
   move faster than one cell per frame -- which a grid cell fundamentally
   cannot, and which is the whole point of a projectile.

   The interaction with the world is a THRESHOLD test rather than damage
   accumulation: a shot carries `power` and destroys any cell whose strength is
   at or below it, and is stopped dead by anything above. See MatStrength in
   materials.h for why that is the right model here. */

struct Projectile {
    float x, y;      /* cells, with a fractional part */
    float vx, vy;
    i32   power;     /* highest material strength it can break through */
    i32   pierce;    /* cells it can still destroy before it is spent */
    i32   life;      /* frames remaining */
    i32   blast;     /* explosion radius on impact; 0 for an ordinary shot */
    u32   colour;
    bool  alive;
};

/* Small on purpose. This is a tool that mines, not a bullet-hell -- and a cap
   low enough to reason about means the update loop is a fixed, tiny cost that
   never needs its own profiling story. Spawning past the cap drops the shot
   rather than growing the array, which at 64 in flight nobody can perceive. */
static const int MAX_PROJ = 64;
extern Projectile g_proj[MAX_PROJ];

void projClear();
void projSpawn(float x, float y, float vx, float vy,
               int power, int pierce, int life, u32 colour, int blast = 0);

/* Blows a hole, sets fire to the middle of it and heats the lot. Exposed
   because an explosion is a world event rather than a projectile one -- the
   next things that want to cause one are a ruptured boiler and a dropped
   unstable material, neither of which is a projectile. */
void explodeAt(World& w, int cx, int cy, int radius, int power);

/* Counts up as explosions happen, so a test can assert one went off without
   having to infer it from the shape of the hole. */
extern int projExplosionsThisFrame;

/* Steps every live projectile and applies it to the world. Returns how many
   cells were destroyed this frame. */
int  projUpdate(World& w);
void projDraw(u32* px, int camX, int camY);
int  projCount();
