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
