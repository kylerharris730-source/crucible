#pragma once
#include "world.h"
#include "player.h"
#include "item.h"

/* --- creatures -------------------------------------------------------------

   The second kind of thing in this game that is not a cell. The player was the
   first, and this deliberately copies its arrangement rather than inventing a
   new one: an overlay on the grid, with its own position and its own physics,
   which the simulation neither knows nor cares about. See the note at the top
   of player.h for why that sidesteps an entire class of problem -- a creature
   that lived in the grid would need a density, a movement rule, and a story for
   what happens when sand tries to displace it.

   --- what an enemy is FOR ---
   DESIGN.md's answer, and the one the archetypes below are built on:

     In a game about machines, the threat should threaten machines.

   An enemy that only drains hp is a parallel game with its own rules bolted
   onto this one. An enemy that interacts with the SIMULATION makes every
   thermal decision a defensive decision as well, and costs almost nothing in
   new systems because the simulation is already there. So the three creatures
   here are a burrower that eats through your walls, a heat-seeker that finds
   your furnace, and a corroder that leaves acid where it walks. Each one is a
   gradient follower -- toward the player, toward heat -- and none of them
   pathfinds, because a gradient is both far cheaper and far more in keeping
   with a world that is itself a field of values.

   --- entities are NOT saved ---
   Deliberate, and it is what keeps this subsystem from touching the save format
   at all. Creatures respawn from the dark, so nothing about them is worth
   preserving across a session; writing them out would mean a new versioned
   section, a new way for an old save to be wrong, and a second unsaved-state
   problem beside the one ToolInst already has. What DOES need to persist is
   which bosses have been beaten, and that is a bitmask rather than a list.

   The visible consequence is that quitting inside a cave clears whatever was
   chasing you. That is a fair trade, and arguably the kinder behaviour. */

enum EntityType {
    ENT_NONE = 0,        /* a free slot; never a real creature */
    /* --- layer 1 --------------------------------------------------------
       Treacherous with bare hands, trivial once you have a Shot Module: the
       whole difficulty curve of the first layer is "get a weapon". */
    ENT_MITE,            /* burrower: walks at you, chews through rock */
    ENT_MOTH,            /* heat-seeker: flies to the hottest cell it can find */
    ENT_SLIME,           /* corroder: slow, and leaves acid behind it */
    ENT_COUNT
};

/* 96 rather than a round 128, and the number is a budget rather than a limit
   anyone should be hitting: the spawner caps live creatures far below this (see
   ENT_MAX_ALIVE), so the pool only needs headroom for a cap's worth plus
   whatever a player spawns by hand out of the creative menu. */
static const int MAX_ENTITIES = 96;

struct EntityDef {
    const char* name;
    int   w, h;            /* collision box, in cells */
    int   hp;
    int   touchDamage;     /* health per contact, before armour */
    int   touchCooldown;   /* frames between contacts, so it is not per-frame */
    float speed;           /* cells per frame at a walk */
    float accel;
    bool  flies;           /* ignores gravity and steers in both axes */
    /* Which cave layers it may spawn in, as a bitmask of 1<<caveLayerOf(). All
       three of these are layer 1 for now; the field exists because layer 2 is
       the next thing to be designed and an enemy table with no notion of depth
       would have to be rewritten rather than extended. */
    u8    layerMask;
    /* Whether it may spawn on the surface at night. See the day/night note in
       light.h -- the classic rule is that darkness is what spawns things, and
       the surface is only dark half the time. */
    bool  surfaceAtNight;
    ItemId dropItem;
    int   dropMin, dropMax;
    u8    sprite;
    /* The swatch its spawn egg gets in the creative list. Lives on the creature
       rather than beside the egg item so there is one table describing a
       creature and not two that can disagree about what colour it is. */
    u32   eggColour;
};

extern const EntityDef ENT_DEFS[ENT_COUNT];

struct Entity {
    u8    type;        /* ENT_NONE means this slot is free */
    float x, y;        /* top-left of the collision box, in cells */
    float vx, vy;
    int   hp;
    int   facing;      /* +1 right, -1 left */
    bool  onGround;
    int   touchTimer;  /* frames until it can hurt the player again */
    int   hurtFlash;   /* frames of white; damage you cannot see teaches nothing */
    int   actTimer;    /* per-type: chew progress, acid drip, wingbeat */
    float animPhase;

    bool  alive() const { return type != ENT_NONE && hp > 0; }
    int   width()  const { return ENT_DEFS[type].w; }
    int   height() const { return ENT_DEFS[type].h; }
    float centreX() const { return x + width()  * 0.5f; }
    float centreY() const { return y + height() * 0.5f; }
    int   left()   const { return (int)x; }
    int   top()    const { return (int)y; }
    int   right()  const { return (int)x + width()  - 1; }
    int   bottom() const { return (int)y + height() - 1; }
};

extern Entity g_entities[MAX_ENTITIES];

/* Every creature removed. Called on world generation and on load -- see the
   note above about entities being transient. */
void entReset();

/* Put one at a position given in CELLS, taking the position as the creature's
   CENTRE rather than its corner, because every caller has a point it wants the
   thing to appear at and none of them are thinking about box corners. Returns
   the slot index, or -1 if the pool is full or the box will not fit. */
int  entSpawn(const World& w, int type, float cx, float cy);

/* One step for every live creature: senses, movement, contact damage, and
   whatever the archetype does to the world. */
void entTick(World& w, Player& p);

/* Hurt whatever creature covers this cell, if any. Returns true if something
   was hit, so a projectile can spend itself on a body rather than sailing
   through it. */
bool entDamageAt(int x, int y, int damage);

/* Area damage, for explosions. Returns how many creatures were hit. */
int  entDamageDisc(int cx, int cy, int radius, int damage);

/* How many are alive right now, for the spawner's cap and for the HUD. */
int  entAliveCount();

void entDraw(u32* px, int camX, int camY, bool lit);

/* --- the spawner -----------------------------------------------------------
   Called once a frame with the camera rectangle. Everything about WHERE a
   creature may appear lives here; see entity.cpp. */
void entSpawnTick(World& w, const Player& p, int camX, int camY);

/* Creatures alive at once. Small on purpose: these are meant to be a hazard you
   meet in a tunnel, not a horde. Ten is enough that a dark cavern feels
   occupied and few enough that the contact-damage rules never turn into an
   unavoidable grind. */
static const int ENT_MAX_ALIVE = 10;
