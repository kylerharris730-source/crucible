#include "rig.h"
#include <string.h>

/* --- the suit --------------------------------------------------------------
   A VALUE LADDER, four steps from the far limbs up to the helmet, and it is the
   single thing that decides whether this reads as a figure or as a bowling pin.

   The first version gave the torso and the near limbs nearly the same value.
   In side view an arm swings INSIDE the body's silhouette, so with no contrast
   between them the arms simply vanished and the whole upper body reduced to one
   pale blob with a dark bar on it. Far limb, torso, near limb, helmet, each a
   clear step lighter than the last, so an arm crossing the chest is still an
   arm. */
const u32 RIG_SUIT[RIG_SHADES] = {
    0x767F91,   /* 0 far limb   -- darkest of the suit */
    0xA3ADBF,   /* 1 torso      -- the middle of the ladder */
    0xCFD7E4,   /* 2 near limb  -- clearly above the torso it crosses */
    0xEDF1F8,   /* 3 helmet     -- the lightest thing on the figure */
    0x3F4653,   /* 4 boot and glove, far side */
    0x565E6C,   /* 5 boot and glove, near side */
    0x22405F,   /* 6 visor glass */
    0x4A5264,   /* 7 life-support pack */
    0xE08442,   /* 8 belt */
};

static Bone mk(int par, int rest, int len, int wb, int wt, int sh, int ly, int at) {
    Bone o;
    o.parent = (i8)par; o.rest = (i16)rest; o.at = (u8)at; o.len = (i16)len;
    o.wBase = (u8)wb; o.wTip = (u8)wt; o.shade = (u8)sh; o.layer = (u8)ly;
    return o;
}

void rigHumanoid(Bone* b, RigDef* rig, const char* name,
                 int w, int h, const u32* shade) {
    const int H = h * ARM_SS, W = w * ARM_SS;

    /* Lengths as a fraction of height. head + neck + spine + thigh + shin comes
       to 95%, leaving the last 5% for the thickness of the boot, so the figure
       fills its box exactly -- which matters more here than in most games,
       because the sprite IS the collision box and a gap would show as material
       resting inside the character. */
    /* The head grows in WIDTH, not in length, and the distinction is not
       cosmetic -- it is what a capsule does at the ends. A bone of length L
       with half-width w draws L + w of material, because the cap is a disc
       centred on the tip. The head's caps are the biggest on the figure, so
       length and width both push the crown up and the two compound: raising
       headL to 22% at the same time as the width made an eleven-row head on a
       thirty-row body, which is a beehive rather than a helmet.

       Width alone is also the right lever for the stated problem. At four cells
       across the head is eight screen pixels at SCALE 2 -- enough for a shape,
       not enough for a face -- and the visor, which is the only feature on the
       figure and the only thing that says which way it is looking, has two
       cells to live in. Wider fixes that. Taller does not.

       The NECK moves with it, and has to. The head's base cap is a disc of
       radius headW, so widening the head extends it DOWNWARD as well, and at
       25% it reached past a 4% neck and sat straight on the shoulders again --
       the merged head-and-torso this rig had to fix once already. 7% keeps the
       pinch visible under a wider helmet. */
    const int headL = 17 * H / 100, neckL =  7 * H / 100, spineL = 26 * H / 100;
    const int thighL = 24 * H / 100, shinL = 21 * H / 100;
    const int footL  =  9 * H / 100;
    const int upperL = 19 * H / 100, foreL = 17 * H / 100;
    const int visorL = 10 * H / 100, packL = 15 * H / 100;

    /* Widths as a fraction of width, and these are HALF-widths. */
    const int hipW  = 15 * W / 100, shdW  = 22 * W / 100;
    const int neckW =  7 * W / 100, headW = 25 * W / 100;
    const int thighW = 11 * W / 100, shinW = 9 * W / 100;
    const int armW   =  9 * W / 100, foreW = 7 * W / 100, footW = 7 * W / 100;
    const int visorW = 12 * W / 100, packW = 11 * W / 100, beltL = 8 * W / 100;

    /*                parent, rest, len, wBase, wTip, shade, layer, socket */
    b[RB_PELVIS] = mk(-1, 0, 0, 0, 0, 1, 1, 255);
    b[RB_SPINE]  = mk(RB_PELVIS, 180, spineL, hipW, shdW, 1, 4, 255);
    /* A belt on the hips: a few cells of the only warm colour on the figure.
       A capsule cannot narrow in the middle, so without this the torso and the
       legs are one continuous taper and the character has no waist. */
    b[RB_BELT]   = mk(RB_PELVIS, 90, beltL, hipW, hipW, 8, 5, 0);
    /* A NECK, narrow and short. Without it the helmet capsule begins inside the
       shoulders and the entire upper body reduces to one rectangle with a notch
       in it, which is exactly what the first render produced. The pinch is two
       cells wide and it is the whole difference between a character and a
       domino. */
    b[RB_NECK]   = mk(RB_SPINE, 0, neckL, neckW, neckW, 1, 4, 255);
    /* 8/10 rather than 7/10: a sharper taper on a head this wide comes to a
       point and reads as a hood. This is a helmet, so the crown stays broad.

       That crown is now wider than the collision outline's apex, which is
       PLAYER_W - 2*PLAYER_TAPER = 3 cells, and in the two LEANING poses -- jump
       and fall -- one or two helmet pixels fall outside it. Measured, and
       accepted rather than missed: swept from 22% up, every width that reads as
       a face at this scale overflows, because a 3-cell apex cannot hold a
       readable helmet at all. What the taper is for is ducking under an
       overhang while walking, and every grounded frame is clean; the cost is a
       pixel of helmet drawn over a block for the few frames of a jump. Narrowing
       the head to fit would undo the readability this change exists for. */
    b[RB_HEAD]   = mk(RB_NECK,  0, headL, headW, headW * 8 / 10, 3, 5, 255);
    /* The visor is socketed part way UP the head rather than at its tip, and
       that is what Bone::at exists for: at the tip it sits on the crown, which
       is a hat. The face is a third of the way down. It is also the only thing
       on the figure that says which way it is looking -- a bare capsule head is
       the same picture facing either way. */
    b[RB_VISOR]  = mk(RB_HEAD, -80, visorL, visorW, visorW * 3 / 5, 6, 8, 110);
    b[RB_PACK]   = mk(RB_SPINE, 168, packL, packW, packW * 4 / 5, 7, 3, 205);

    b[RB_FAR_UPPER]  = mk(RB_SPINE, 180, upperL, armW, armW, 0, 2, 255);
    b[RB_FAR_FORE]   = mk(RB_FAR_UPPER, 0, foreL, foreW, foreW, 0, 2, 255);
    b[RB_FAR_THIGH]  = mk(RB_PELVIS, 0, thighL, thighW, shinW, 0, 2, 255);
    b[RB_FAR_SHIN]   = mk(RB_FAR_THIGH, 0, shinL, shinW, shinW * 3 / 4, 0, 2, 255);
    b[RB_FAR_FOOT]   = mk(RB_FAR_SHIN, 90, footL, footW, footW, 4, 2, 255);

    b[RB_NEAR_UPPER] = mk(RB_SPINE, 180, upperL, armW, armW, 2, 6, 255);
    b[RB_NEAR_FORE]  = mk(RB_NEAR_UPPER, 0, foreL, foreW, foreW, 2, 6, 255);
    b[RB_NEAR_THIGH] = mk(RB_PELVIS, 0, thighL, thighW, shinW, 2, 6, 255);
    b[RB_NEAR_SHIN]  = mk(RB_NEAR_THIGH, 0, shinL, shinW, shinW * 3 / 4, 2, 6, 255);
    b[RB_NEAR_FOOT]  = mk(RB_NEAR_SHIN, 90, footL, footW, footW, 5, 7, 255);

    rig->name = name; rig->bone = b; rig->bones = RB_COUNT;
    rig->shade = shade; rig->w = w; rig->h = h;
    rig->rootX = (i16)(W / 2);
    rig->rootY = (i16)(headL + neckL + spineL);
}

/* --- poses -----------------------------------------------------------------
   Written through a helper rather than as brace initialisers, because a
   seventeen-entry array of angles is unreadable and a miscounted one is
   undetectable. Naming the joints costs a function and makes the tables below
   something you can check against a mirror. */
static void key(PoseKey& k, int dy, int spine, int head,
                int fUp, int fFore, int fThigh, int fShin, int fFoot,
                int nUp, int nFore, int nThigh, int nShin, int nFoot) {
    memset(&k, 0, sizeof(k));
    k.rootDY = (i16)dy;
    k.angle[RB_SPINE] = (i16)spine;
    /* The head counter-rotates the lean so the character keeps looking level
       along the ground however far the torso tips. A head welded to the spine
       is the difference between someone walking and someone falling over. */
    k.angle[RB_NECK]  = (i16)(head / 2);
    k.angle[RB_HEAD]  = (i16)(head - spine);
    k.angle[RB_FAR_UPPER] = (i16)fUp;   k.angle[RB_FAR_FORE] = (i16)fFore;
    k.angle[RB_FAR_THIGH] = (i16)fThigh; k.angle[RB_FAR_SHIN] = (i16)fShin;
    k.angle[RB_FAR_FOOT]  = (i16)fFoot;
    k.angle[RB_NEAR_UPPER] = (i16)nUp;   k.angle[RB_NEAR_FORE] = (i16)nFore;
    k.angle[RB_NEAR_THIGH] = (i16)nThigh; k.angle[RB_NEAR_SHIN] = (i16)nShin;
    k.angle[RB_NEAR_FOOT]  = (i16)nFoot;
}

/* --- the walk --------------------------------------------------------------
   The four classic key poses of a step, and the second step is the first with
   near and far exchanged -- which is why only four are described and eight are
   written.

     CONTACT  legs at full split, front heel about to land, hips level
     DOWN     weight taken, front knee bent, hips at their LOWEST
     PASSING  legs together, support leg straight, swing knee lifted, rising
     UP       push-off, hips HIGHEST, back leg extended behind

   The hips falling on contact and rising through passing is what makes this
   read as weight rather than as legs waving: two lows and two highs per cycle,
   one of each per step. It is carried by the root offset, which is why that is
   part of a pose rather than something added afterwards.

   Arms swing opposite the leg on the same side, and the elbow trails -- a
   forearm held straight makes the arm a stick, and a stick swinging from a
   shoulder is a pendulum rather than an arm. */
static PoseKey g_walkKeys[8];
static PoseKey g_idleKeys[2];
static PoseKey g_jumpKeys[1];
static PoseKey g_fallKeys[1];
static PoseKey g_crouchKeys[1];
static bool g_clipsReady = false;

static void buildClips() {
    if (g_clipsReady) return;
    g_clipsReady = true;
    /* rootDY is measured DOWN from the highest the hips ever go, so zero is the
       push-off and everything else sinks from there. Written the other way
       first -- centred on nought, dipping to +2 and rising to -3 -- and it lifts
       the whole figure, PLANTED FOOT AND ALL, off the bottom of its own box:
       twelve of twelve frames had an empty bottom row, which at this size means
       the character walks a cell above the floor it is colliding with.

       The root is the hips and there is no inverse kinematics here to pin a
       foot while they move. What keeps the support foot down instead is the
       LEG: the hips are highest exactly when that leg is straightest, so the
       reach compensates. That only works if zero is the top of the arc.

              dy  spn head | far: up  fore thigh shin foot | near: up fore thigh shin foot */
    key(g_walkKeys[0], 3, 4, -2,   22, -20, -20, -12,  25,   -22, -12,  24,  -6,  -8);
    key(g_walkKeys[1], 5, 5, -2,   14, -22, -14, -28,  10,   -12, -10,  12, -22,   0);
    key(g_walkKeys[2], 2, 4, -2,    0, -14,   8, -46,   6,     0,  -8,  -2,  -4,   0);
    key(g_walkKeys[3], 0, 5, -2,  -12, -14,  18, -26,  -4,    12, -10, -14,  -6,  18);
    key(g_walkKeys[4], 3, 4, -2,  -22, -12,  24,  -6,  -8,    22, -20, -20, -12,  25);
    key(g_walkKeys[5], 5, 5, -2,  -12, -10,  12, -22,   0,    14, -22, -14, -28,  10);
    key(g_walkKeys[6], 2, 4, -2,    0,  -8,  -2,  -4,   0,     0, -14,   8, -46,   6);
    key(g_walkKeys[7], 0, 5, -2,   12, -10, -14,  -6,  18,   -12, -14,  18, -26,  -4);

    /* Standing. Two keys a hair apart rather than one, so the figure breathes:
       a character frozen solid while everything around it simulates reads as
       having been paused. One cell of rise over about a second. */
    key(g_idleKeys[0], 1, 2, -1,    4, -12,  -3,  -2,   2,    -4, -10,   3,  -2,   2);
    key(g_idleKeys[1], 0, 3, -1,    3, -14,  -3,  -2,   2,    -3, -12,   3,  -2,   2);

    /* Rising. Arms up and back, legs tucked and toes down -- the one pose that
       cannot be confused with any frame of the walk, which is what a jump has
       to be at this size. */
    key(g_jumpKeys[0], 0, 8, -6,  -46, -28,  26, -54,  16,   -38, -34, -10, -30,  22);
    /* Descending: legs reaching for the ground, arms out for balance. */
    key(g_fallKeys[0], 2, -4, 4,   38, -24,  18, -14, -10,    30, -20, -16, -10,  -8);

    /* Crouching: knees folded hard, hips low, torso pitched forward over them
       for balance -- the shape a squat actually makes, not a shorter idle.
       Baked onto the CROUCH_H canvas (see buildPlayerFrames), where the bone
       lengths are already proportionally shorter, so this bend is on top of
       that rather than trying to make a standing-height skeleton fit a small
       box by folding harder than a knee does.

       head=0 against spine=25 leaves the encoded head angle at -25 -- the
       same counter-rotation convention as every other key -- so the face
       still looks level rather than down at the floor. Thighs and shins are
       given a slightly different bend far vs near (20/-65 against 22/-68)
       for the same reason the walk keys are never quite mirrored: a
       perfectly symmetric silhouette reads as a mannequin. */
    key(g_crouchKeys[0], 4, 8, 0,   15, -25,  20, -65,  15,   -15, -22,  22, -68,  14);
}

/* The clips. `frames` is how many are baked: eight for the walk because that is
   one per key and the eye cannot resolve more at this size, one for the poses
   that do not move. */
static const Clip WALK = { "walk", g_walkKeys, 8, 8, true,  true  };
static const Clip IDLE = { "idle", g_idleKeys, 2, 2, true,  true  };
/* Airborne, so NOT floor-snapped: a jump that kept its feet welded to the
   bottom of its box would be a character bobbing on the spot. */
static const Clip JUMP = { "jump", g_jumpKeys, 1, 1, false, false };
static const Clip FALL = { "fall", g_fallKeys, 1, 1, false, false };
/* Grounded, unlike jump and fall, so it floor-snaps -- a crouch with its feet
   not on the bottom row would be a character crouching an inch above the
   floor. */
static const Clip CROUCH = { "crouch", g_crouchKeys, 1, 1, false, true };

const Clip RIG_WALK   = WALK;
const Clip RIG_IDLE   = IDLE;
const Clip RIG_JUMP   = JUMP;
const Clip RIG_FALL   = FALL;
const Clip RIG_CROUCH = CROUCH;

/* Built on first use rather than from a constructor: static initialisation
   order across translation units is not something to bet a character on. */
struct ClipInit { ClipInit() { buildClips(); } };
static ClipInit g_clipInit;
