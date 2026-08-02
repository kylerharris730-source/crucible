#pragma once
#include "common.h"

/* --- a 2D armature ---------------------------------------------------------

   Bones with lengths and angles, posed by keyframes, rasterised into the same
   flat u32 sprite buffers everything else in this file already uses.

   --- why this exists ---
   The character used to be seven hand-drawn frames sharing one body, and the
   note that arrangement left behind is the reason this is here: with hand art,
   every new pose is a new picture, and every new CREATURE is a whole new set of
   them. Enemies are next, and drawing four walk frames each for a dozen of them
   is not a thing anybody finishes. A rig is a table of bone lengths and a table
   of angles; a new creature is two rows of numbers and it inherits every clip
   already written.

   --- why it is baked, not live ---
   Posing costs a sine per bone and a few hundred pixel writes. That is nothing
   once, and it is nothing sixty times a second too -- but it is not FREE, and
   the draw path it would sit in is the one that runs for every visible entity.
   So clips are evaluated at startup and written into ordinary frame buffers,
   exactly the ones the old hand-drawn frames produced. The renderer is
   unchanged and the per-frame cost of animation is a table index, as it was.

   The cost of baking is worth stating because it decides how many frames a clip
   may have: one 12x32 frame at 4x supersampling is 48x128 subsamples, about
   6k pixel ops. A hundred frames across every creature in the game is under a
   millisecond at load.

   --- why it supersamples ---
   This is the whole difficulty, and it is worth the paragraph. A limb of length
   L rotated by theta moves its tip L*sin(theta). At the character's original
   eight pixels across, a leg is 8 long, so a 7-degree swing moves the foot less
   than one pixel and a 45-degree swing moves it four -- there are about six
   distinguishable angles in the entire usable range, and the file this replaces
   says so in as many words: "there is no room to draw a leg at an angle".

   Rasterising at 4x and reducing solves the half of that which is about SHAPE
   rather than reach. Two angles that put the tip in the same output pixel still
   light different subsamples along the way, so they come out as different
   staircases -- the limb bends visibly before its endpoint has moved at all.
   It does not conjure reach out of nothing, which is why the character also got
   bigger; see PLAYER_W.

   Reduced by COVERAGE with a hard threshold rather than by blending. Blending
   would give a soft-edged figure standing in a world drawn one hard pixel per
   cell, and the character would read as being from a different game. */

static const int ARM_SS = 4;          /* subsamples per cell, each axis */
static const int ARM_MAX_BONES = 24;

/* A bone is a tapered capsule from its parent's tip, at an angle relative to
   the parent's direction. Lengths and widths are in SUBSAMPLES, so the numbers
   in a rig table are four times the cell figures -- which is deliberate: they
   are drawing units, and rounding them to cells is what would flatten the
   distinction between a 12-degree and a 20-degree swing. */
struct Bone {
    i8  parent;        /* -1 for the root */
    /* Which way the bone points when the pose says zero, relative to its
       parent. A pose angle is a DELTA from this, which is what lets the tables
       below read as anatomy: an arm hangs down, so its rest is 180 away from
       the spine that points up, and "swing the arm forward 20" is written as
       20 rather than as 200. Without it every arm angle in every clip carries
       the same 180 and one of them eventually gets it wrong. */
    i16 rest;          /* degrees */
    /* Where along the parent this bone hangs from: 0 is the parent's base, 255
       its tip. Almost everything wants the tip, which is what a skeleton is --
       but not everything, and the exceptions are the two bones that carry the
       character's FACING. A visor socketed at the parent's tip sits on the
       crown of the helmet, which is a hat; the face is halfway down. Without a
       socket the only way to put it there is another invisible bone whose whole
       job is to be an offset, and a rig accumulating spacer bones is a rig
       nobody can read. */
    u8  at;
    i16 len;           /* subsamples */
    u8  wBase, wTip;   /* half-width at each end, subsamples */
    u8  shade;         /* index into RigDef::shade */
    u8  layer;         /* draw order; low first, so far limbs go behind */
};

struct RigDef {
    const char* name;
    const Bone* bone;
    int         bones;
    const u32*  shade;      /* colours, indexed by Bone::shade */
    int         w, h;       /* the sprite canvas, in CELLS */
    i16         rootX, rootY;  /* where bone 0 hangs from, in subsamples */
};

/* One keyframe: an angle per bone plus an offset for the root.

   The root offset is what carries the BODY BOB, and it has to be part of the
   pose rather than added afterwards: a walk that does not rise and fall reads
   as a puppet being slid along the floor, and the rise is not a decoration on
   top of the leg angles, it is the same event seen from the hips. Two lows and
   two highs per full cycle, one of each per step.

   Angles are DEGREES, signed, measured from the bone's rest direction, positive
   turning the tip toward the facing side. Degrees because these tables are
   written and read by a person; the sine table is 360 entries and costs
   nothing. */
struct PoseKey {
    i16 rootDX, rootDY;
    i16 angle[ARM_MAX_BONES];
};

struct Clip {
    const char*    name;
    const PoseKey* key;
    int            keys;
    int            frames;   /* how many to bake; interpolated between keys */
    bool           loop;
    /* --- snap the feet to the floor ---------------------------------------
       Set for any clip whose creature is STANDING on something. After posing,
       the whole figure is slid down until its lowest pixel is the bottom row of
       the canvas.

       This is the cheap stand-in for inverse kinematics, and it is the piece
       that makes a hand-written walk cycle work at all. The root is the hips,
       and moving them moves the planted foot with them -- so a bob written as
       root motion lifts the character off the floor it is colliding with.
       Measured before this existed: ten of twelve frames had an empty bottom
       row, and the two that did not were the crouch. Worse, the poses that
       float are the ones with the legs SPLIT, because a leg at 20 degrees
       reaches cos(20) of its length downward, so the error is different in
       every frame and cannot be dialled out with one offset.

       Snapping leaves the interesting half untouched. The hips still rise and
       fall, because they move relative to the feet as the knees straighten and
       bend; what goes away is the whole figure sliding up and down its own box.
       And it means rootDY reads as "how deep is the crouch" rather than as an
       absolute the author has to keep in their head. */
    bool           ground;
};

/* Bake `clip` into `out`, which must hold clip->frames * rig->w * rig->h u32.
   0 is transparent, as everywhere else. */
void armBake(const RigDef* rig, const Clip* clip, u32* out);

/* One frame, for a harness that wants a single pose without a clip. */
void armPose(const RigDef* rig, const PoseKey* pose, u32* out, bool ground = false);
