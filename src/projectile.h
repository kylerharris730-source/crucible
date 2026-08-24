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

/* --- one gravity, and speed is the lever ------------------------------------
   The same number the player falls at (see GRAVITY in player.cpp), deliberately,
   because a world with two gravities is a world where nobody can predict
   anything by watching it. A shot that fell at its own private rate would look
   like a rendering effect rather than like a thing obeying the same rules as
   everything else on screen.

   Which means the interesting number is not this one, it is SPEED. Drop over a
   distance D at speed v is (g/2)(D/v)^2 -- quadratic in the flight time -- so
   the same gravity produces a nearly flat line for something fast and a
   pronounced lob for something slow, with no per-weapon gravity fudge needed.
   That is also how it works in reality, and it makes shot speed a stat that
   MEANS something instead of a number nobody could feel:

       weapon            speed   drop at 30 cells   at full reach (56)
       Bolt Caster        6.0        2.3 cells          7.8 cells
       Blast Module       3.5        6.6               23.0
       Spitter glob       1.7       28.0               96.4

   So the starter weapon shoots nearly flat at knife range and asks you to lead
   it across a cavern; the blast module lobs like the grenade launcher it always
   was; and the spitter throws a visible arcing glob, which is what a thing that
   SPITS should do. See the note in spitTick on why the spitter had to be taught
   to aim high once this existed. */
static const float PROJ_GRAVITY = 0.18f;

enum ProjectileEffect {
    PROJ_EFFECT_NONE = 0,
    PROJ_EFFECT_GLOWFLARE,
    PROJ_EFFECT_TELEPORT
};

struct Projectile {
    float x, y;      /* cells, with a fractional part */
    float vx, vy;
    /* Added to vy each frame. Per-projectile rather than global so a shot can
       opt OUT -- see ItemDef::shotBeam. Zero is a perfectly straight line. */
    float gravity;
    i32   power;     /* highest material strength it can break through */
    /* Health taken off a creature it strikes. Carried on the shot rather than
       looked up from the module that fired it, because by the time a shot
       lands the tool may have been unloaded, dropped or reconfigured -- a
       projectile has to be self-describing or its damage becomes a question
       about the state of something else, later. See ItemDef::damage for why
       this is not derived from `power`. */
    i32   damage;
    /* Whose shot this is. A player's shot hurts creatures and passes through
       the character; a creature's shot does exactly the opposite.

       One bit rather than two projectile systems, because everything else about
       a shot -- how it flies, what it breaks, what it drops on impact -- is
       identical whoever fired it, and a second system would be a second place
       for that behaviour to drift. */
    bool  hostile;
    i32   pierce;    /* cells it can still destroy before it is spent */
    i32   life;      /* frames remaining */
    i32   blast;     /* explosion radius on impact; 0 for an ordinary shot */
    i16   bounces;   /* solid-surface ricochets remaining */
    float homing;    /* fraction of heading corrected toward a target each frame */
    u32   colour;
    /* A MatId, or MAT_EMPTY for an ordinary shot. See the note on projSpawn.
       This is the whole of "a launcher fires whatever you load it with" --
       DESIGN.md's second open decision -- and it costs one field here plus
       one placement at impact, because every payload material's actual
       BEHAVIOUR (LN2 freezing and boiling to cold fire, acid dissolving,
       lava igniting) is already simulated and asks nothing further of this
       file. */
    u8    payload;
    /* Optional impact-only overlay effect. Kept separate from payload because
       glowfluid is also valid ammunition in an ordinary multitool, while a
       Glowflare is specifically the ampoule that bursts into revealing motes. */
    u8    effect;
    u8    owner;     /* PlayerId for owner-specific effects such as teleport */
    bool  alive;
};

/* Small on purpose. This is a tool that mines, not a bullet-hell -- and a cap
   low enough to reason about means the update loop is a fixed, tiny cost that
   never needs its own profiling story. Spawning past the cap drops the shot
   rather than growing the array, which at 64 in flight nobody can perceive. */
static const int MAX_PROJ = 64;
extern Projectile g_proj[MAX_PROJ];

void projClear();
/* `payload` is a MatId, MAT_EMPTY (0) by default -- an ordinary shot. When
   set, the cell the shot comes to rest in (whichever cell that honestly is;
   see the note in projUpdate()) is converted to it on impact rather than
   simply cleared. Nothing about delivery is special-cased per material:
   dropping LN2 there freezes and boils away on its own schedule, lava
   ignites what is nearby, acid starts dissolving its neighbours -- all of
   that is the ordinary simulation reacting to a cell that changed, exactly
   as it would if a player had placed the material by hand. */
bool projSpawn(float x, float y, float vx, float vy,
               int power, int pierce, int life, u32 colour, int blast = 0,
               int payload = MAT_EMPTY, int damage = 0, bool hostile = false,
               /* Defaulted ON, so a shot arcs unless it says otherwise. That is
                  the right way round: falling is what things do here, and a new
                  weapon that forgot to think about it should look like it obeys
                  the world rather than like it hovers. Pass 0 to opt out, and
                  say why -- there is exactly one reason so far (a mining beam,
                  which has to bore a straight tunnel). */
               float gravity = PROJ_GRAVITY,
               int effect = PROJ_EFFECT_NONE,
               int bounces = 0, float homing = 0.0f,
               u8 owner = 0xff);

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
/* Register the glow carried by live flares and their non-colliding impact
   motes. Called after lightClearDynamic() and before lightUpdate(). */
void projRegisterLights();
void projDraw(u32* px, int camX, int camY);
int  projCount();
int  projTrailMoteCount();
int  projGlowMoteCount();
int  projGlowAfterglowCount();
/* Copies live mote positions for diagnostics and deterministic regression
   tests. Returns the number copied, capped by capacity. */
int  projGlowMoteSnapshot(float* xs, float* ys, int capacity);
