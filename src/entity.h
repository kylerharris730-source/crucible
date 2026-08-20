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
    /* --- the Terraria half ----------------------------------------------
       The three above all interact with the SIMULATION -- they eat walls, chase
       heat, leave acid. These three are pure combat, and that is deliberate
       rather than a lapse: a layer made only of gimmicks is a layer where every
       fight is a puzzle, and the fun of Terraria's roster is that most of it is
       simple enough to read at a glance and still worth fighting.

       Simple AI, distinct MOVEMENT. That is the whole design -- a zombie you
       walk away from, a bat you cannot, and a shooter that makes standing still
       the wrong answer. */
    ENT_HUSK,            /* zombie: slow, tanky, hits hard, will not stop */
    ENT_BAT,             /* fast flier with bad steering -- it OVERSHOOTS */
    ENT_SPITTER,         /* keeps its distance and shoots */
    /* --- the layer 1 boss -------------------------------------------------
       Summoned, never spawned. See BOSS_LAYER1 and the summon item. */
    ENT_BROOD,
    ENT_COUNT
};

/* Which bosses have been beaten. A bitmask rather than a list, and the ONLY
   creature state that survives a session -- see the note above about entities
   being transient. One bit per boss, saved as a single u32. */
extern u32 g_bossesBeaten;
static const u32 BOSS_LAYER1 = 1u << 0;

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
    /* --- ranged attack ---------------------------------------------------
       Frames between shots, 0 for everything that does not shoot. A creature
       with this fires at the player when it has line of sight and is roughly
       facing them; see spitterTick. */
    int   shotEvery;
    int   shotDamage;
    float shotSpeed;
    /* How far it tries to stay from the player. Zero means "close the
       distance", which is what everything melee wants. A shooter that walked
       into contact range would be a bad melee creature rather than a shooter. */
    float standOff;
    /* Bosses do not spawn from the dark, are not capped by ENT_MAX_ALIVE, and
       announce themselves. Summoned by an item instead. */
    bool  isBoss;
    ItemId dropItem;
    int   dropMin, dropMax;
    /* --- the rare drop ---------------------------------------------------
       One charm per creature, at one chance in `rareOneIn`. ITEM_NONE and 0 on
       anything that has none, so a creature declares its rare drop the same way
       it declares its ordinary one.

       This is the answer to "every layer-1 creature drops chitin, so killing
       things feels the same regardless of what you killed". The ordinary drop
       stays exactly as it was -- chitin is the layer's material and the boss
       summon needs it -- and the rare one is what makes the creatures DIFFERENT
       to hunt. A bat and a husk now lead somewhere different.

       One in fifty rather than one in ten, and the difference matters more than
       it looks. These are permanent changes to how the character plays, so they
       have to be events: at one in ten a charm is a chore you complete, at one
       in fifty it is a thing that happens to you on the way somewhere else. It
       also has to survive the spawn cap -- seven alive at a time and a despawn
       rule means nobody is farming a hundred husks in one place -- so the rate
       is set against a session of playing rather than against a grind. */
    ItemId rareDrop;
    int   rareOneIn;
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
    int   shotTimer;   /* frames until it can shoot again */
    /* Where a bat is currently committed to flying. Held for a stretch of
       frames rather than recomputed every one, which is the entire reason a bat
       overshoots -- see batTick. */
    float aimX, aimY;
    int   aimHold;
    int   phase;       /* bosses: which half of the fight this is */
    float animPhase;
    /* --- the dash ---------------------------------------------------------
       Three fields serving one behaviour, and they are on Entity rather than
       tucked into the spare bits of an existing one because entities are not
       saved (see the note at the top of this file) -- the only cost of a field
       here is a line in codecEntity, and the alternative was overloading
       `phase` with a second meaning that nothing in its name admits to.

       `telegraph` counts down the wind-up. It is what makes a dash a MOVE
       rather than damage that arrives: the creature stops, is visibly about to
       do something, and only then commits. `weightless` suspends gravity for
       the frames it is airborne, which is what lets the dash leave the floor
       plane at all -- and leaving the floor plane is the whole point, because a
       ground chase can never reach somebody standing on a rope. */
    int   telegraph;
    bool  weightless;
    /* Where it was, and for how long it has been failing to leave. A creature
       34 cells wide gets wedged on geometry that nothing else in the game
       notices, so it needs to be able to tell that it is pushing against
       something rather than walking. Compared against its own last position
       rather than against its velocity: the mover zeroes vx on contact and the
       chase puts it straight back, so velocity says "moving" the whole time it
       is stuck. */
    float prevX, prevY;
    int   stuck;

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

/* Collectible drops are overlays too. Unlike a material cell they cannot flow
   into a torch or become buried by sand, and an inventory can collect them
   without the player having to aim a mining action at the exact resting cell. */
static const int MAX_PICKUPS = 96;
struct Pickup {
    ItemId item;
    i16    count;
    float  x, y, vx, vy;
    bool   used;
};
extern Pickup g_pickups[MAX_PICKUPS];

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
void entTick(World& w, Player& p, Inventory& inv);
void entTickPlayers(World& w);

/* Hurt whatever creature covers this cell, if any. Returns true if something
   was hit, so a projectile can spend itself on a body rather than sailing
   through it. */
bool entDamageAt(int x, int y, int damage);

/* Area damage, for explosions. Returns how many creatures were hit. */
int  entDamageDisc(int cx, int cy, int radius, int damage);

/* Disc damage with a gentle radial push. Used by close-range companions: the
   hit is intentionally small, but creating space is tangible protection. */
int  entDamageKnockbackDisc(int cx, int cy, int radius, int damage, float knockback);

/* --- a blade passing through the world -------------------------------------
   Damage every creature whose box the segment (x0,y0)-(x1,y1) crosses, pushing
   each away from (fromX, fromY), and remember in `hitMask` which ones have
   already been struck so that a stroke lasting a dozen frames costs one hit
   each rather than a dozen.

   A SEGMENT rather than a disc, because that is the shape a blade actually is:
   a sword sweeping a 130-degree arc has to miss the creature standing behind
   the swinger, and a disc centred on the player cannot express that. It is also
   what makes a spear a different weapon rather than a shorter-ranged sword --
   its whole reach is in one direction.

   `hitMask` is MAX_ENTITIES bits and is the caller's, not this function's,
   because "one hit per stroke" is a fact about the STROKE and this is called
   once a frame. Returns how many were newly struck, so the caller can tell a
   connecting swing from a whiff. */
int  entHitSegment(float x0, float y0, float x1, float y1,
                   float fromX, float fromY,
                   int damage, float knockback, u8* hitMask);

/* How many are alive right now, for the spawner's cap and for the HUD. */
int  entAliveCount();

void entDraw(u32* px, int camX, int camY, bool lit);
int  pickupCount();

/* --- the spawner -----------------------------------------------------------
   Called once a frame with the camera rectangle. Everything about WHERE a
   creature may appear lives here; see entity.cpp. */
void entSpawnTick(World& w, const Player& p, int camX, int camY, bool lightFieldValid = true);

/* Creatures alive at once. Small on purpose: these are meant to be a hazard you
   meet in a tunnel, not a horde. Enough that a dark cavern feels occupied and
   few enough that the contact-damage rules never turn into an unavoidable
   grind.

   Ten -> seven, which is the "how many" half of a complaint that also had a
   "how fast" half. The two are separate levers and it is worth keeping them
   that way: this one sets the DENSITY a cave settles at, and SPAWN_COOL in
   entity.cpp sets how long it takes to get there. Seven still fills a chamber;
   ten was starting to read as a horde in the corridors, which are the tight
   spaces where touch damage has nowhere for you to go.

   A CAP WITH NO DESPAWN IS A BUDGET YOU SPEND ONCE. See ENT_DESPAWN_DIST --
   without it this number does not mean "how many are around you", it means
   "how many will ever exist", and the whole rest of the world is empty. */
static const int ENT_MAX_ALIVE = 7;

/* --- how far away a creature stops existing --------------------------------
   Nothing despawned. Creatures were created and then lived forever, which
   sounds harmless next to a cap of seven and is precisely what makes the cap
   catastrophic: stand in one dark spot until seven spawn, walk away, and those
   seven hold the entire world's budget for the rest of the session. Reported
   from play as exploring the caves and meeting nothing at all -- which is
   exactly right, because every creature the world was allowed to have was
   standing in a chamber somewhere behind the player.

   The number has to clear the SPAWN rectangle or the two rules fight. Spawning
   happens inside the light field -- the view plus LIGHT_MARGIN on every side --
   so the farthest a creature can legally appear is about half of
   LIGHT_W = 512 + 170, some 341 cells horizontally, plus the slack of standing
   at the edge of the view rather than its centre. Despawning nearer than that
   would delete things the spawner had just placed, which is a busy loop that
   also never puts a creature in front of you.

   700 is comfortably past it, and is a little over one screen width beyond the
   far edge of what is drawn: far enough that nothing ever vanishes where you
   could see it happen, close enough that walking away from a fight for a few
   seconds genuinely frees the budget. */
static const int ENT_DESPAWN_DIST = 700;

/* How long a boss stays committed to a charge. Long enough to see it start,
   decide, and be somewhere else -- a lunge you cannot read is just damage that
   arrives. */
static const int CHARGE_FRAMES = 46;

/* --- reading the charge ----------------------------------------------------
   Frames of wind-up before she commits. The charge already lasted long enough
   to dodge; what it had no room for was DECIDING to dodge, because it began on
   the same frame it became visible. She now stops dead for this long first,
   glows, and only then picks a heading -- so the information arrives before the
   creature does, which is the difference between a boss and a hazard.

   Long enough to react to, short enough that it is not a free window: at 26
   frames it is a little under half a second. */
static const int BOSS_WINDUP = 26;

/* Cells per frame while dashing, and the arc is a straight line at this speed
   in whatever direction she committed to -- not a ground chase at a multiplier.
   46 frames at 3.6 crosses about 165 cells, which is most of a screen, so the
   answer to a dash is to be somewhere else rather than to out-walk it. */
static const float BOSS_DASH_SPEED = 3.6f;

/* Frames of failing to move before she stops trying to walk and dashes out of
   it. She is 34x24, by far the largest box in the game, and geometry that every
   other creature steps over will wedge her -- so the recovery has to be a move
   she already has rather than a special case in the mover. A dash is
   weightless and eats rock, so it frees her from anything short of a layer
   barrier.

   Measured in cells rather than in "did the mover refuse": the mover zeroes vx
   on contact and the chase restores it immediately, so from the outside a
   wedged creature looks like a walking one. */
static const int   BOSS_STUCK_FRAMES = 40;
static const float BOSS_STUCK_CELLS  = 0.35f;
