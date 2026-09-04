#pragma once
#include "armature.h"

/* --- the creature rigs -----------------------------------------------------

   One humanoid skeleton, built from PROPORTIONS rather than written out per
   creature, and a library of clips posed against it. The character uses it
   today; anything with two arms and two legs uses it the day it exists, at
   whatever size it happens to be, with no new art.

   That is the whole reason the armature is here. A hand-drawn character costs
   one set of frames per pose; a hand-drawn BESTIARY costs that many times the
   number of creatures, which is the point where nobody finishes. Here a new
   creature is a call to rigHumanoid with different numbers, and it inherits
   every clip already written.

   Proportions are fractions of the body box, so a short wide creature and a
   tall thin one are the same skeleton and not two skeletons that have to be
   kept in step. */

/* Bones, in the order the tables below expect them. The near side is the one
   toward the viewer and is drawn last; the far side is drawn first and darker,
   which is the only depth cue available at this size and is what stops a walk
   reading as one leg with a twitch. */
enum RigBone {
    RB_PELVIS, RB_SPINE, RB_BELT, RB_NECK, RB_HEAD, RB_VISOR, RB_PACK,
    RB_FAR_UPPER, RB_FAR_FORE, RB_FAR_THIGH, RB_FAR_SHIN, RB_FAR_FOOT,
    RB_NEAR_UPPER, RB_NEAR_FORE, RB_NEAR_THIGH, RB_NEAR_SHIN, RB_NEAR_FOOT,
    RB_COUNT
};

/* Fill `bone` (RB_COUNT of them) and `rig` for a humanoid `w` x `h` CELLS.
   `shade` must have at least RIG_SHADES entries; see rigSuitShades. */
static const int RIG_SHADES = 9;
void rigHumanoid(Bone* bone, RigDef* rig, const char* name,
                 int w, int h, const u32* shade);

/* The spacesuit's colours, in the order rigHumanoid indexes them. Exposed so a
   creature can pass its own set and get the same figure in another skin --
   which is what most of a bestiary actually is. */
extern const u32 RIG_SUIT[RIG_SHADES];

/* --- clips -----------------------------------------------------------------
   Poses for the skeleton above. Angles are degrees from each bone's rest
   direction, so these are reusable across every size rigHumanoid can build. */
extern const Clip RIG_WALK;   /* 8 keys, the classic contact/down/passing/up */
extern const Clip RIG_IDLE;
extern const Clip RIG_JUMP;
extern const Clip RIG_FALL;
/* One static pose: a forward HUNCH -- spine pitched over bent knees, hips
   counterweighted back, head still looking level along the ground. Posed on
   the ordinary full-height rig and floor-snapped like any grounded clip; the
   caller crops the bottom CROUCH_H rows out of the result. Deliberately not a
   rig of its own, since a crouch shortens nobody's bones -- see the note on
   g_playerCrouchSpr. */
extern const Clip RIG_CROUCH;

/* --- the tentacled rig -----------------------------------------------------

   Not a humanoid, and the reason it is a second builder rather than another
   set of numbers for rigHumanoid is that it is a different SHAPE of skeleton:
   a body with four chains hanging off it, where a humanoid is a spine with
   paired limbs of fixed count. Everything below the body is a chain, and the
   chain is the whole creature.

   Bones are laid out tentacle-major -- all of tentacle 0, then all of
   tentacle 1 -- so a walk can address one limb by index arithmetic instead of
   a lookup table. TENT_SEGS deep, TENT_LEGS wide.

   Two of the four are drawn behind the body and two in front, which is the
   same depth trick the humanoid uses for its far and near sides: without it
   four identical limbs on one plane read as a fan rather than as a creature
   standing in a place. */
static const int TENT_LEGS = 4;
static const int TENT_SEGS = 4;
static const int TENT_BODY = 0;      /* the root: the bulb */
static const int TENT_CROWN = 1;     /* a small rise on top, so it has a top */
static const int TENT_FIRST_LEG = 2;
static const int TENT_BONES = TENT_FIRST_LEG + TENT_LEGS * TENT_SEGS;   /* 18 */

/* Segment `s` of tentacle `t`. */
static inline int tentBone(int t, int s) { return TENT_FIRST_LEG + t * TENT_SEGS + s; }

/* Shades, in the order rigTentacled indexes them. */
static const int TENT_SHADES = 5;
extern const u32 RIG_THRESHER[TENT_SHADES];

void rigTentacled(Bone* bone, RigDef* rig, const char* name,
                  int w, int h, const u32* shade);

/* --- the spider ------------------------------------------------------------

   The layer 2 boss, and the third skeleton shape. It is not the tentacled rig
   with a bigger number: a tentacle HANGS and a spider leg ARCHES -- up and out
   from the body to a knee above the back, then down to the ground. That arch is
   the single feature that says "spider" at any size, and it is a property of
   the rest pose rather than of the gait, so it belongs in the builder.

   Three body bones rather than the tentacled rig's two, and the abdomen is the
   one that earns its place: a spider read from a distance is a big rear bulb
   with legs in front of it, and without one this is a crab.

   Bones are leg-major after the body, exactly as the tentacled rig lays its
   own out, so a gait can address one limb by index arithmetic. */
static const int SPIDER_LEGS = 8;
static const int SPIDER_SEGS = 4;
static const int SPIDER_BODY    = 0;   /* the cephalothorax: the root */
static const int SPIDER_ABDOMEN = 1;   /* the bulb behind it */
static const int SPIDER_HEAD    = 2;   /* the small front, where the fangs are */
static const int SPIDER_FIRST_LEG = 3;
static const int SPIDER_BONES = SPIDER_FIRST_LEG + SPIDER_LEGS * SPIDER_SEGS;

/* Segment `s` of leg `t`. */
static inline int spiderBone(int t, int s) {
    return SPIDER_FIRST_LEG + t * SPIDER_SEGS + s;
}

static const int SPIDER_SHADES = 6;
extern const u32 RIG_SPIDER[SPIDER_SHADES];

void rigSpider(Bone* bone, RigDef* rig, const char* name,
               int w, int h, const u32* shade);

/* The spider's gait, generated for the same reason the tentacled one is: eight
   limbs of four segments is 32 angles a frame, and nobody maintains that by
   hand. The phasing is the part that differs -- see the note in rig.cpp. */
void rigSpiderWalk(PoseKey* keys, int count, int lift);
extern const Clip RIG_SPIDER_WALK;
extern const Clip RIG_SPIDER_IDLE;

/* --- the gait, GENERATED rather than authored -------------------------------

   The humanoid clips above are hand-written keyframe tables, which is right for
   a figure whose poses are a small set of readable positions. It is the wrong
   tool for four limbs that have to stay a quarter-cycle apart from each other:
   as a table that is TENT_LEGS * TENT_SEGS angles per key, thirty-two numbers a
   frame, every one of them a sine somebody evaluated by hand and none of them
   checkable by eye.

   So the wave is computed. Each tentacle is the same motion at its own phase,
   each segment lags the one above it so the curl travels outward, and the body
   rides the result. Changing the gait means changing an amplitude, not
   re-typing thirty-two numbers and hoping they still add up to a walk.

   Fills `keys` (which must hold `count` of them) for a loop of `count` frames.
   `lift` is how far a limb curls on its recovery half, in degrees. */
void rigTentacleWalk(PoseKey* keys, int count, int lift);
extern const Clip RIG_TENT_WALK;
extern const Clip RIG_TENT_IDLE;
