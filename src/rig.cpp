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

    /* Crouching: a HUNCH. Spine pitched forward over bent knees, not a folded
       squat -- see CROUCH_H for why the box is four fifths of standing rather
       than two thirds.

       The SPINE SIGN IS INVERTED against every other bone here, and it is the
       one thing to get right before touching these numbers. Angles are degrees
       from a bone's rest direction with 0 pointing DOWN, so on a limb hanging
       downward positive swings the tip forward. The spine's rest is 180 -- it
       points UP -- so positive swings its tip BACKWARD. The first version of
       this pose used +25 meaning "lean forward" and produced a figure arched
       over backwards with its helmet outside the collision box on the wrong
       side. Forward is negative. Arms inherit the spine's frame, which is why
       theirs are large and positive: they are cancelling the lean to hang
       somewhere near vertical.

       The head does NOT inherit it. Its encoded angle is (head - spine), so
       the absolute head direction works out to 180 + 3*head/2 regardless of
       how far the torso tips -- the counter-rotation the key() helper exists
       for. head = -10 is therefore an absolute forward tilt of the face, not a
       tilt relative to the chest: hunched over, still looking where they are
       going.

       rootDX pulls the hips BACK, which is the counterweight a real hunch
       uses and here also does a mechanical job: without it the leaning helmet
       hangs past the pointed shoulders of the collision outline, and the
       crouch is exactly the pose that must not do that, since ducking under an
       overhang is the whole reason to be in it.

       The SHIN swings back further than the thigh swings forward, and by a
       derived amount rather than a chosen one. A thigh at 56 degrees throws
       the knee forward by thighL*sin(56); the shin has to undo all of that or
       the ankle ends up ahead of the hip and the figure sits back on its heels
       with its feet out in front, which is what the first several attempts
       did. -124 puts the ankle under the hip, and the foot then levels against
       the shin so the sole stays flat.

       Far and near legs differ slightly, as in the walk keys, so the
       silhouette is a person rather than a mannequin.

       These five numbers were swept rather than eyeballed -- spine, thigh,
       ankle offset and rootDX against fit, taper and lean together -- because
       they are not independent: leaning further forward is free until the
       helmet crosses the collision outline, and stops being free abruptly. */
    key(g_crouchKeys[0], 0, -22, -10,   48, -14,  56, -124,  68,   52, -18,  51, -118,  67);
    /* Not a key() parameter because this is the only pose that needs it.
       Twelve subsamples is three cells, and it is very nearly exactly the
       distance the lean throws the shoulders forward -- so the helmet ends up
       back over the middle of the box while the body still reads as pitched
       over. Without it the whole figure slides off the right of the canvas and
       stamp() quietly clips it. */
    g_crouchKeys[0].rootDX = -12;
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

/* ===========================================================================
   The tentacled rig
   =========================================================================== */

/* Four steps, the same job the suit ladder does: a limb crossing the body has
   to stay a limb. Far tentacles sit below the body, near ones above it. */
const u32 RIG_THRESHER[TENT_SHADES] = {
    0x3E2A46,   /* 0 far tentacle  -- darkest */
    0x6B4570,   /* 1 body          -- the middle of the ladder */
    0x9A5F94,   /* 2 near tentacle -- clearly above the body it crosses */
    0xC98BB8,   /* 3 crown */
    0xE8C24A,   /* 4 the eye */
};

void rigTentacled(Bone* b, RigDef* rig, const char* name,
                  int w, int h, const u32* shade) {
    const int H = h * ARM_SS, W = w * ARM_SS;

    /* The body is a wide short capsule in the UPPER part of the box, because
       everything below it is leg. A bulb any lower and the limbs have no room
       to bend, which is what turns a walk into a shuffle. */
    const int bodyLen = (H * 20) / 100;
    /* 17%, not 30%. A capsule end is a DISC of its half-width, so a fat body
       hangs a cap below its own base -- and the tentacles root at that base.
       At 30% the cap alone was eight cells deep and swallowed the top half of
       every limb, which rendered as one blob with a few dark flecks on it. The
       body has to be narrow enough that the limbs leave it. */
    const int bodyW   = (W * 17) / 100;

    /* Segments shorten and narrow outward, which is what makes a taper read as
       a taper rather than as four rectangles in a row. The four together reach
       60% of the box, so a straightened limb just touches the floor. */
    const int reach = (H * 60) / 100;
    const int segLen[TENT_SEGS] = { (reach * 30) / 100, (reach * 27) / 100,
                                    (reach * 23) / 100, (reach * 20) / 100 };
    /* Thicker at the root than the old values: a limb thinner than the body it
       hangs from disappears against it at this scale, and these are supposed to
       be the readable part of the creature. */
    const int segW[TENT_SEGS + 1] = { (W * 8) / 100, (W * 7) / 100,
                                      (W * 6) / 100, (W * 5) / 100, (W * 3) / 100 };

    /* REST 0 POINTS DOWN, 180 points up -- the humanoid sets its spine to 180
       and its thighs to 0, and this rig has to agree with that or it builds
       itself upside down. Which it did: body at 0 grew downward, tentacles at
       180 stood up out of the top, and the creature came out as a bulb wearing
       antennae. The bulb rises from the waist, so it is 180; the limbs hang, so
       they are 0. */
    b[TENT_BODY]  = mk(-1, 180, bodyLen, bodyW, bodyW * 4 / 5, 1, 2, 0);
    b[TENT_CROWN] = mk(TENT_BODY, 0, (H * 6) / 100,
                       bodyW * 3 / 5, bodyW * 2 / 5, 3, 2, 255);

    /* Splay: the outer pair stand wider than the inner pair, so the silhouette
       is a creature standing on four legs rather than two legs drawn twice.

       OFFSETS FROM 180, because rest is measured against the PARENT and the
       parent here is the body, which points up. A tentacle at rest 0 therefore
       points the same way the body does -- straight up -- and the creature
       renders as a tulip: limbs fanning out of a narrow base with the bulb
       buried underneath. 180 turns them to hang. (The body itself is 180 for
       the opposite reason: it has no parent, so its rest is absolute, and
       absolute 0 is down.) */
    static const int splay[TENT_LEGS] = { -34, -12, 12, 34 };
    for (int t = 0; t < TENT_LEGS; ++t) {
        /* Alternating depth: 0 and 2 behind the body, 1 and 3 in front. */
        const bool nearSide = (t & 1) != 0;
        const int  sh    = nearSide ? 2 : 0;
        const int  layer = nearSide ? 3 : 1;
        for (int s = 0; s < TENT_SEGS; ++s) {
            const int idx = tentBone(t, s);
            /* The first segment hangs off the body BASE (at 0). Rooted at the
               bulb tip instead, a tentacle would sprout from its crown. The
               rest chain from their parent tip like any limb. */
            const int parent = s == 0 ? TENT_BODY : idx - 1;
            const int rest   = s == 0 ? 180 + splay[t] : 0;
            const int at     = s == 0 ? 0 : 255;
            b[idx] = mk(parent, rest, segLen[s],
                        segW[s], segW[s + 1], sh, layer, at);
        }
    }

    rig->name  = name;
    rig->bone  = b;
    rig->bones = TENT_BONES;
    rig->shade = shade;
    rig->w = w; rig->h = h;
    /* The root is where the body BASE goes -- the waist of the creature rather
       than its middle. The bulb grows up from here and the tentacles hang
       down, so this is the one point both halves agree on. */
    rig->rootX = (i16)(W / 2);
    rig->rootY = (i16)(H - reach);
}

/* --- the wave --------------------------------------------------------------
   Integer sine, scaled by 1024, so this file still needs no float and no
   math.h. Built once on first use; 360 entries costs nothing and removes every
   rounding argument from the gait below. */
static int isin1024(int deg) {
    static int table[360];
    static bool built = false;
    if (!built) {
        for (int d = 0; d < 360; ++d) {
            int q = d % 180;
            const int sign = d < 180 ? 1 : -1;
            if (q > 90) q = 180 - q;
            /* q degrees in radians, fixed point at 1/1024. */
            const long long x = ((long long)q * 17870) / 1024;
            const long long x3 = x * x / 1024 * x / 1024;
            const long long x5 = x3 * x / 1024 * x / 1024;
            long long s = x - x3 / 6 + x5 / 120;
            if (s > 1024) s = 1024;
            if (s < 0) s = 0;
            table[d] = (int)(sign * s);
        }
        built = true;
    }
    int d = deg % 360;
    if (d < 0) d += 360;
    return table[d];
}

void rigTentacleWalk(PoseKey* keys, int count, int lift) {
    memset(keys, 0, sizeof(PoseKey) * (size_t)count);
    for (int k = 0; k < count; ++k) {
        PoseKey& p = keys[k];
        int bodyRise = 0;
        for (int t = 0; t < TENT_LEGS; ++t) {
            /* Each limb a quarter cycle behind the last, so the creature is
               always planted on two of them. That phasing is the whole gait:
               with every limb in step it hops, and with them randomly placed
               it paddles. */
            const int phase = (k * 360) / count + (t * 360) / TENT_LEGS;
            const int swing = isin1024(phase);          /* fore and aft */
            const int curl  = isin1024(phase + 90);     /* lift on recovery */

            for (int s = 0; s < TENT_SEGS; ++s) {
                /* Each segment lags the one above it, so the bend travels from
                   body to tip instead of the limb hinging as one rigid stick.
                   That travel is what makes it read as a tentacle at all. */
                const int lag = isin1024(phase + 90 + s * 34);
                int a = (s == 0) ? (swing * 26) / 1024
                                 : (lag * lift * (s + 1)) / (1024 * TENT_SEGS);
                /* A planted limb straightens; only the recovering half curls.
                   Without this every limb bends the same amount whether it is
                   carrying weight or not, which is a creature swimming. */
                if (curl < 0 && s > 0) a /= 3;
                p.angle[tentBone(t, s)] = (i16)a;
            }
            if (curl > 0) bodyRise += curl;
        }
        /* The body leans into the stride at twice the limb rate, since two
           limbs land per cycle. */
        p.angle[TENT_BODY]  = (i16)((isin1024((k * 720) / count) * 4) / 1024);
        p.angle[TENT_CROWN] = 0;
        /* Down is positive Y, so a rise is negative. Deliberately small: the
           bob suggests weight, and a body travelling visibly up and down reads
           as bouncing rather than walking. */
        p.rootDY = (i16)(-(bodyRise * ARM_SS) / (1024 * TENT_LEGS));
    }
}

static PoseKey g_tentWalkKeys[8];
static PoseKey g_tentIdleKeys[2];

static void buildTentClips() {
    rigTentacleWalk(g_tentWalkKeys, 8, 30);
    /* Idle is the same wave, shallow and with the limbs left where they stand:
       a creature made of tentacles that froze completely would read as dead
       rather than as waiting. */
    rigTentacleWalk(g_tentIdleKeys, 2, 8);
    for (int k = 0; k < 2; ++k)
        for (int t = 0; t < TENT_LEGS; ++t)
            g_tentIdleKeys[k].angle[tentBone(t, 0)] = 0;
}

static const Clip TENT_WALK = { "tentwalk", g_tentWalkKeys, 8, 8, true, true };
static const Clip TENT_IDLE = { "tentidle", g_tentIdleKeys, 2, 2, true, true };
const Clip RIG_TENT_WALK = TENT_WALK;
const Clip RIG_TENT_IDLE = TENT_IDLE;

/* --- the spider ------------------------------------------------------------ */

const u32 RIG_SPIDER[SPIDER_SHADES] = {
    0x241C2C,   /* 0 far leg    -- nearly black; it is meant to recede */
    0x4A3A52,   /* 1 body       -- the middle of the ladder */
    0x6E5578,   /* 2 near leg   -- clearly above the body it crosses */
    0x322740,   /* 3 abdomen    -- darker than the body, so the bulk reads */
    0x8A6A90,   /* 4 head */
    0xD4433C,   /* 5 the eyes -- the one warm colour on the whole creature */
};

/* Layer 3's. The legs stay nearly black so the silhouette is legs-and-a-glow,
   and the BODY is the light -- a vessel with a fire in it, carried. That
   inversion is what makes it read as a different creature from the Widow rather
   than as the Widow in orange: on her the body is the pale part and the legs
   recede, and here the legs recede further and the body burns. */
const u32 RIG_CENSER[SPIDER_SHADES] = {
    /* The legs were 0x1C1416 and 0x4A2E28 first, on the reasoning that a
       near-black skeleton around a burning body would read as legs-and-a-glow.
       It read as a glow. Rendered, the creature was a small orange dot in an
       empty 56x44 box: both leg shades were DARKER THAN THE BACKDROP, so eight
       limbs and an abdomen drew nothing a viewer could distinguish from
       background, and the animation harness reported the boss as not animating
       -- which was true, because the only visible pixels were a body that does
       not move.

       A value ladder needs its bottom rung above the thing it is drawn on, and
       the suit's note at the top of this file says so already. These are warm
       and dark rather than black. */
    0x5A3228,   /* 0 far leg    -- dark, and still clear of the backdrop */
    0xC85A1C,   /* 1 body       -- the vessel, lit */
    0x9A5636,   /* 2 near leg   -- a clear step above the far side */
    0x7A2A12,   /* 3 abdomen    -- the coal it carries */
    0xFFD46A,   /* 4 head       -- the brightest thing on it */
    0xFF8A3A,   /* 5 the eyes */
};

void rigSpider(Bone* b, RigDef* rig, const char* name,
               int w, int h, const u32* shade) {
    const int H = h * ARM_SS, W = w * ARM_SS;

    /* The body sits high in the box because everything below it is leg, and it
       is SHORT and wide -- a spider's cephalothorax is a plate, not a trunk. */
    const int bodyLen = (H * 18) / 100;
    /* 9%, and the first version's 15% is why this number has a note. Widths in
       a rig are HALF-widths in subsamples, so 15% of a 48-cell box is a body
       SEVEN CELLS to each side -- fourteen across -- and the legs at the time
       were five cells long. Every limb was inside the body, and the creature
       baked as a featureless blob. A spider is mostly leg; the body is a hub
       the legs meet at, and it has to be narrow enough that they leave it. */
    const int bodyW   = (W * 11) / 100;

    /* THE LEGS ARE THE BIGGEST THING ON IT, which is the other half of the same
       correction. A leg has to arch up over the back and still reach the floor
       out at the edge of a box that is wider than it is tall, so the four
       segments together come to most of the whole height. The arch spends a
       good part of that: a straightened leg would stick out past the box, and a
       bent one lands inside it -- which also means the figure grows fast in
       this number, and 110% put the knees clean off the top of the canvas. */
    const int reach = (H * 78) / 100;
    const int segLen[SPIDER_SEGS] = { (reach * 32) / 100, (reach * 30) / 100,
                                      (reach * 22) / 100, (reach * 16) / 100 };
    /* Thin. Eight of them crossing each other at this size turn into a solid
       mass if they are anywhere near the body's width, and the gaps BETWEEN
       the legs are most of what makes a spider legible. */
    const int segW[SPIDER_SEGS + 1] = { (W * 3) / 100, (W * 2) / 100,
                                        (W * 2) / 100, (W * 1) / 100,
                                        (W * 1) / 100 };

    /* Same convention the tentacled rig documents: rest 0 points DOWN, and a
       child's rest is measured from its parent's direction. The body has no
       parent, so 180 is absolute up. */
    b[SPIDER_BODY] = mk(-1, 180, bodyLen, bodyW, bodyW * 9 / 10, 1, 2, 0);
    /* The abdomen hangs BACKWARD off the body's base and is the biggest single
       shape on the creature. Rooted at the base (at 0) rather than the tip, so
       it sits behind the plate instead of on top of it. */
    /* Sign convention, established by the humanoid's visor: NEGATIVE rotates
       toward the front. So the head is negative and the abdomen positive, and
       the creature faces the way everything else in this file faces. */
    b[SPIDER_ABDOMEN] = mk(SPIDER_BODY, 82, (H * 46) / 100,
                           bodyW * 12 / 10, bodyW * 6 / 10, 3, 0, 0);
    /* And a small head off the front, tilted down: the fangs end of it. */
    b[SPIDER_HEAD] = mk(SPIDER_BODY, -98, (H * 16) / 100,
                        bodyW * 7 / 10, bodyW * 4 / 10, 4, 4, 128);

    /* --- the arch ---------------------------------------------------------
       The whole reason this is not rigTentacled with an 8 in it. A leg goes UP
       and OUT to a knee above the creature's back, then turns and comes back
       DOWN to the floor. That arch is the single feature that says "spider" at
       any size, and it lives in the rest pose rather than in the gait.

       THE FAN IS FRONT-TO-BACK, NOT LEFT-TO-RIGHT, and getting that wrong is
       worth recording because it bakes into something that still looks like a
       creature. This is a side view: near and far are DEPTH, and depth in a
       two-dimensional silhouette is a shade, not a direction. A first version
       signed the splay by side, so all four near legs pointed backward and all
       four far legs forward, and the result was a clump with no stance in it.
       Both sides fan the same way; the sides differ only in shading, in draw
       order, and by a few degrees so they do not sit exactly on top of one
       another.

       Angles accumulate down the chain, so these read as one continuous turn:
       start pointing up and out, then keep rotating the SAME way until the
       foot points at the floor. The front pair sweeps forward and the back pair
       backward, which is what puts the creature in a stance rather than a
       huddle. Negative is toward the front -- see the note on the head. */
    /* Biased FORWARD: three of the four pairs are in front of the hub and only
       one trails. Fanned evenly it read as a spider seen head-on rather than
       from the side, because a symmetric stance under a centred body has no
       front to it. Weighting the legs forward leaves the back of the box for
       the abdomen, which is the shape that says which way it is pointing. */
    static const int base[SPIDER_LEGS / 2] = { -88, -52, -18, 44 };
    /* The front and back pairs reach furthest, the way they do on the animal --
       the middle two are the ones carrying weight. */
    static const int legScale[SPIDER_LEGS / 2] = { 112, 94, 90, 104 };
    /* Nearly all of the turn in ONE joint. Spread evenly down the chain it
       comes out as a smooth spiral -- four segments each bending a bit is a
       curl, and the legs read as hooks. A spider has a KNEE: a long straight
       reach out, one hard angle, and a long straight drop. */
    static const int bend[SPIDER_SEGS]     = {   0,  86, 16,  8 };

    for (int t = 0; t < SPIDER_LEGS; ++t) {
        /* Legs 0..3 far, 4..7 near -- four contiguous limbs each rather than
           the tentacled rig's alternation, which at eight would read as one row
           of legs with a shading fault instead of as two sides of a body. */
        const bool nearSide = t >= SPIDER_LEGS / 2;
        const int  pair  = t % (SPIDER_LEGS / 2);
        const int  sh    = nearSide ? 2 : 0;
        const int  layer = nearSide ? 3 : 1;
        /* Which way this leg curls: the way it already points. */
        const int  turn  = base[pair] < 0 ? -1 : 1;
        /* The near set stands a little wider, so eight legs are eight legs
           rather than four drawn twice. */
        const int  offset = nearSide ? turn * 9 : 0;

        for (int s = 0; s < SPIDER_SEGS; ++s) {
            const int idx = spiderBone(t, s);
            const int parent = s == 0 ? SPIDER_BODY : idx - 1;
            const int rest = s == 0 ? base[pair] + offset : turn * bend[s];
            /* Segment 0 hangs off the middle of the body rather than its tip,
               so the legs come out of the sides of the plate instead of
               sprouting from its nose. */
            const int at = s == 0 ? 128 : 255;
            b[idx] = mk(parent, rest, (segLen[s] * legScale[pair]) / 100,
                        segW[s], segW[s + 1], sh, layer, at);
        }
    }

    rig->name  = name;
    rig->bone  = b;
    rig->bones = SPIDER_BONES;
    rig->shade = shade;
    rig->w = w; rig->h = h;
    rig->rootX = (i16)(W / 2);
    /* Where the legs meet the body, as a fraction of the box rather than as
       `H - reach` the way the tentacled rig computes it. That subtraction works
       when the limbs are shorter than the box and hang straight down; here they
       are LONGER than it and arch, so it goes negative and puts the whole
       creature above the canvas -- which is exactly what it did, baking two
       rows of leg tips along the bottom edge and nothing else. A spider is
       low-slung, so the hub sits high and the arch spends the extra length. */
    rig->rootY = (i16)((H * 38) / 100);
}

/* --- the spider's gait ------------------------------------------------------
   Generated, like the tentacled one, because 8 legs x 4 segments is 32 angles a
   frame. What differs is the PHASING, and it is the whole character of the
   walk: a spider does not run a travelling wave down one side, it moves in two
   alternating sets of four -- legs 1 and 3 on one side with 2 and 4 on the
   other -- so it is always standing on a stable four-point stance. Phase is
   therefore a function of (which side, which pair) rather than of leg index,
   and it takes exactly two values half a cycle apart.

   The knee is where the lift happens. Swinging segment 0 alone slides the foot
   along the ground; curling the knee on the recovery half is what picks it up,
   which is the difference between walking and skating. */
void rigSpiderWalk(PoseKey* keys, int count, int lift) {
    memset(keys, 0, sizeof(PoseKey) * (size_t)count);
    for (int k = 0; k < count; ++k) {
        PoseKey& p = keys[k];
        int bodyRise = 0;
        for (int t = 0; t < SPIDER_LEGS; ++t) {
            const bool nearSide = t >= SPIDER_LEGS / 2;
            const int  pair  = t % (SPIDER_LEGS / 2);
            const int  sign  = nearSide ? 1 : -1;
            /* The alternating tetrapod: a leg is in the first set if its pair
               index and its side disagree. */
            const bool setA = ((pair & 1) != 0) != nearSide;
            const int phase = (k * 360) / count + (setA ? 0 : 180);

            const int swing = isin1024(phase);        /* fore and aft */
            const int curl  = isin1024(phase + 90);   /* lift on recovery */

            for (int s = 0; s < SPIDER_SEGS; ++s) {
                int a;
                if (s == 0) {
                    /* Fore and aft, in the plane of travel. Modest: a leg that
                       swings far is a leg that has visibly left its socket. */
                    a = (swing * 18) / 1024;
                } else {
                    /* Curl the knee and everything past it, and only on the
                       recovery half -- a planted leg holds its shape while the
                       body travels over it. Signed by side so both sides fold
                       the same way relative to the body. */
                    const int c = curl > 0 ? curl : 0;
                    a = -sign * (c * lift) / (1024 * s);
                }
                p.angle[spiderBone(t, s)] = (i16)a;
            }
            if (curl > 0) bodyRise += curl;
        }
        /* Two sets land per cycle, so the body bobs at twice the leg rate. */
        p.angle[SPIDER_BODY] = (i16)((isin1024((k * 720) / count) * 3) / 1024);
        p.rootDY = (i16)(-(bodyRise * ARM_SS) / (2 * 1024 * SPIDER_LEGS));
    }
}

static PoseKey g_spiderWalkKeys[8];
static PoseKey g_spiderIdleKeys[2];

static void buildSpiderClips() {
    rigSpiderWalk(g_spiderWalkKeys, 8, 34);
    /* Idle keeps the legs planted and lets only the body breathe. A spider
       standing still is famously STILL -- that stillness is most of why one on
       a wall is unsettling -- so this is the one creature whose idle should not
       be a shallow version of its walk. */
    rigSpiderWalk(g_spiderIdleKeys, 2, 0);
    for (int k = 0; k < 2; ++k)
        for (int t = 0; t < SPIDER_LEGS; ++t)
            for (int s = 0; s < SPIDER_SEGS; ++s)
                g_spiderIdleKeys[k].angle[spiderBone(t, s)] = 0;
    g_spiderIdleKeys[1].angle[SPIDER_BODY] = 2;
    g_spiderIdleKeys[1].rootDY = -ARM_SS / 2;
}

static const Clip SPIDER_WALK = { "spiderwalk", g_spiderWalkKeys, 8, 8, true, true };
static const Clip SPIDER_IDLE = { "spideridle", g_spiderIdleKeys, 2, 2, true, true };
const Clip RIG_SPIDER_WALK = SPIDER_WALK;
const Clip RIG_SPIDER_IDLE = SPIDER_IDLE;

/* Built on first use, for the same reason the humanoid clips are: static
   initialisation order across translation units is not something to bet a
   creature on. */

/* --- the harvester -----------------------------------------------------------
   See the note in rig.h for why this is a fourth skeleton and not the spider's
   with different numbers. The short version: a spider is carried ON its legs
   and this hangs BENEATH them, and that is not a proportion, it is a different
   arrangement of the same parts. */

void rigHarvester(Bone* b, RigDef* rig, const char* name,
                  int w, int h, const u32* shade) {
    const int H = h * ARM_SS, W = w * ARM_SS;

    /* SMALL. The body is the least of this creature -- it is a knot in the
       middle of a lot of leg, and every cell spent on it is a cell not spent on
       the gap that makes the silhouette. A quarter the relative bulk the spider
       gives itself. */
    const int bodyLen = (H *  9) / 100;
    const int bodyW   = (W *  6) / 100;

    /* And the legs are enormous: the four segments together come to more than
       one and a half times the height of the box, because the arch spends most
       of it going up and then all of it coming back down. This is the number
       that makes a harvestman rather than a long spider. */
    /* 120%, and the number is bounded by the CANVAS rather than chosen for
       drama. The vertical span of one leg is the femur's rise plus the shin's
       drop, and the floor-snap puts the foot on the bottom row -- so a span
       taller than the box does not overflow gracefully, it slides the knees off
       the top. At 175% the span was 75 cells in a 56-cell frame and nineteen
       cells of every leg were simply not drawn. */
    const int reach = (H * 120) / 100;
    /* Front-loaded. The first two segments are the arch -- up to the knee and
       back down -- and the last two are a thin foot that barely tapers. A leg
       whose segments shorten evenly reads as a tentacle. */
    /* The SHIN is longer than the femur, and that is what puts the knee at the
       top of the animal rather than halfway up it. Even segments give a bend in
       the middle of the leg, which is a spider; a short climb and a long drop
       gives an apex near the top of the box with the foot far below it, which
       is what a harvestman stands like.

       Sized against the canvas, not chosen for looks: the femur has to fit the
       gap between the hip and the top of the frame, and the shin has to cover
       the whole way back down to the floor. The first version made them nearly
       equal and the knees went clean off the top while the feet stopped in
       mid-air. */
    const int segLen[HARV_SEGS] = { (reach * 38) / 100, (reach * 50) / 100,
                                    (reach *  8) / 100, (reach *  4) / 100 };
    /* Thread-thin and almost untapered, which is what a leg this long has to be
       to stay a LINE rather than a wedge. The spider's taper is wrong here: it
       is drawn at a third of this length. */
    const int segW[HARV_SEGS + 1] = { (W * 2) / 100, (W * 2) / 100,
                                      (W * 1) / 100, (W * 1) / 100,
                                      (W * 1) / 100 };

    /* Same convention as everything else in this file: rest 0 points DOWN, a
       child's rest is measured from its parent, and the body has no parent so
       its 180 is absolute up. */
    b[HARV_BODY] = mk(-1, 180, bodyLen, bodyW, bodyW * 8 / 10, 1, 2, 0);
    /* SLUNG, and this is the join that says which animal it is. The abdomen
       hangs almost straight DOWN off the body's base -- 172, nearly antiparallel
       to the body itself -- so the mass of the creature is below the point where
       the legs meet, with daylight above it. On the spider the same bone reaches
       backward at 82 and the body sits on top of its legs. */
    b[HARV_ABDOMEN] = mk(HARV_BODY, 172, (H * 24) / 100,
                         bodyW * 14 / 10, bodyW * 5 / 10, 3, 0, 0);
    b[HARV_HEAD] = mk(HARV_BODY, -104, (H * 8) / 100,
                      bodyW * 7 / 10, bodyW * 4 / 10, 4, 4, 200);

    /* --- the arch ---------------------------------------------------------
       Three stations front to back, each with a near and a far leg. The first
       segment leans out from vertical by `base` and climbs; the second folds
       back through `fold` so the whole leg finishes pointing very nearly
       straight down, whatever it leaned to get there.

       The fold is COMPUTED rather than tabled, and that is what keeps the feet
       on the floor: every leg has to arrive at about the same downward heading
       or the creature stands on tiptoe at one end and its knee at the other.
       165 is that heading -- just off vertical, on whichever side the leg
       leaned -- and the fold is however much turning it takes to get there from
       wherever the leg started. */
    /* Steeper than a real harvestman's sprawl, because the box IS the hitbox
       here: legs that lean out as far as the animal's really do would either
       leave the canvas or force a collision box mostly made of air. */
    static const int base[HARV_LEGS / 2] = { -40, -13, 32 };
    static const int LAND = 165;

    for (int t = 0; t < HARV_LEGS; ++t) {
        const bool nearSide = t >= HARV_LEGS / 2;
        const int  pair  = t % (HARV_LEGS / 2);
        const int  sh    = nearSide ? 2 : 0;
        const int  layer = nearSide ? 3 : 1;
        const int  turn  = base[pair] < 0 ? -1 : 1;
        /* The near set stands a little wider, so six legs are six and not three
           drawn twice. */
        const int  lean  = base[pair] + (nearSide ? turn * 11 : 0);
        const int  fold  = turn * LAND - lean;

        for (int s = 0; s < HARV_SEGS; ++s) {
            const int idx = harvBone(t, s);
            const int parent = s == 0 ? HARV_BODY : idx - 1;
            int rest;
            if      (s == 0) rest = lean;
            else if (s == 1) rest = fold;
            else             rest = turn * 5;     /* the foot, barely bent */
            /* Off the body's MIDDLE, so the legs meet at the knot rather than
               sprouting from its crown or its base. */
            const int at = s == 0 ? 140 : 255;
            b[idx] = mk(parent, rest, segLen[s],
                        segW[s], segW[s + 1], sh, layer, at);
        }
    }

    rig->name  = name;
    rig->bone  = b;
    rig->bones = HARV_BONES;
    rig->shade = shade;
    rig->w = w; rig->h = h;
    rig->rootX = (i16)(W / 2);
    /* Low. The legs rise from here into the top of the box and come back down
       past it, so the knot sits well below centre and the arch has somewhere to
       go. The spider's 38% would put the knees off the top of the canvas at
       this leg length -- which is the same mistake the spider's own rootY note
       records from the other direction. */
    rig->rootY = (i16)((H * 70) / 100);
}

/* --- the tripod --------------------------------------------------------------
   Three legs down, three lifting, alternating -- which is what six-legged
   things do and what keeps this from being the spider's four-and-four wearing a
   different skin. A leg is in the first tripod if its station index and its
   side disagree, so each side carries one lifting leg between two planted ones
   and the creature is never balanced on a single edge.

   The LIFT is in the knee and nowhere else. On legs this long a swing at the
   hip moves the foot half the width of the box, which reads as wading; folding
   the knee picks the foot up and puts it down almost in place, which is what a
   harvestman actually looks like -- most of the motion is vertical and the
   creature barely seems to travel. */
void rigHarvesterWalk(PoseKey* keys, int count, int lift) {
    memset(keys, 0, sizeof(PoseKey) * (size_t)count);
    for (int k = 0; k < count; ++k) {
        PoseKey& p = keys[k];
        int bodyRise = 0;
        for (int t = 0; t < HARV_LEGS; ++t) {
            const bool nearSide = t >= HARV_LEGS / 2;
            const int  pair = t % (HARV_LEGS / 2);
            const int  turn = pair < 2 ? -1 : 1;
            const bool tripodA = ((pair & 1) != 0) != nearSide;
            const int phase = (k * 360) / count + (tripodA ? 0 : 180);

            const int swing = isin1024(phase);
            const int curl  = isin1024(phase + 90);

            for (int s = 0; s < HARV_SEGS; ++s) {
                int a;
                if (s == 0) {
                    /* Barely anything at the hip. See above. */
                    a = (swing * 7) / 1024;
                } else if (s == 1) {
                    /* The knee does the work, and only on the recovery half --
                       a planted leg holds its shape while the body passes over
                       it, or the creature is swimming. */
                    const int c = curl > 0 ? curl : 0;
                    a = -turn * (c * lift) / 1024;
                } else {
                    const int c = curl > 0 ? curl : 0;
                    a = turn * (c * lift) / (3 * 1024);
                }
                p.angle[harvBone(t, s)] = (i16)a;
            }
            if (curl > 0) bodyRise += curl;
        }
        /* The body hangs, so it swings rather than bobbing: a slung mass lags
           the thing carrying it. Tiny, and on the abdomen rather than the
           body, because what a viewer sees move is the weight underneath. */
        p.angle[HARV_ABDOMEN] = (i16)((isin1024((k * 360) / count) * 6) / 1024);
        p.rootDY = (i16)(-(bodyRise * ARM_SS) / (3 * 1024 * HARV_LEGS));
    }
}

static PoseKey g_harvWalkKeys[8];
static PoseKey g_harvIdleKeys[2];

static void buildHarvClips() {
    rigHarvesterWalk(g_harvWalkKeys, 8, 46);
    /* Idle keeps the legs where they stand and lets the hanging body drift.
       A creature this leggy that froze completely would read as a diagram. */
    rigHarvesterWalk(g_harvIdleKeys, 2, 0);
    for (int k = 0; k < 2; ++k)
        for (int t = 0; t < HARV_LEGS; ++t)
            for (int s = 0; s < HARV_SEGS; ++s)
                g_harvIdleKeys[k].angle[harvBone(t, s)] = 0;
    g_harvIdleKeys[1].angle[HARV_ABDOMEN] = 4;
    g_harvIdleKeys[1].rootDY = -ARM_SS / 2;
}

static const Clip HARV_WALK = { "harvwalk", g_harvWalkKeys, 8, 8, true, true };
static const Clip HARV_IDLE = { "harvidle", g_harvIdleKeys, 2, 2, true, true };
const Clip RIG_HARV_WALK = HARV_WALK;
const Clip RIG_HARV_IDLE = HARV_IDLE;

/* --- the effigy --------------------------------------------------------------
   See the long note in rig.h for what this is and why it is not the humanoid
   with bigger numbers. What follows is only the arithmetic. */
const u32 RIG_EFFIGY[EFFIGY_SHADES] = {
    /* Charred wood, and the ladder is built the way the Censer's note says it
       has to be: the bottom rung is above the backdrop, not black. A creature
       drawn in shades darker than the cave behind it is a creature nobody can
       see move. */
    0x4A3A2E,   /* 0 far limb   -- weathered timber in shadow */
    0x7A6046,   /* 1 the frame  -- hip, chest, staves */
    0x9A7A56,   /* 2 near limb  -- a clear step above the far side */
    0xE85A14,   /* 3 the heart  -- what is burning in the cage */
    0xFFD07A,   /* 4 the head   -- the brightest thing on it */
    0xFF9A34,   /* 5 the eyes */
};

void rigEffigy(Bone* b, RigDef* rig, const char* name,
               int w, int h, const u32* shade) {
    const int H = h * ARM_SS, W = w * ARM_SS;

    /* The vertical budget, spent from the ground up and adding to 96% of the
       box: leg 50, hip 7, cage 30, head 9. It is written as a budget rather
       than as four independent fractions because the first version was not,
       and the result was a creature whose arms hung off the bottom of the
       canvas while its ribs ran off the top. */
    const int legSpan = (H * 50) / 100;
    const int hipLen  = (H *  7) / 100;
    const int cageLen = (H * 30) / 100;
    const int headLen = (H *  9) / 100;

    /* Widths are HALF-widths in subsamples -- the trap the spider's note
       records -- so a "13%" body is a quarter of the box across. */
    const int hipW   = (W *  9) / 100;
    const int staveW = (W *  2) / 100;

    /* --- the frame --------------------------------------------------------
       Rest 0 points DOWN and a child's rest is measured FROM ITS PARENT, which
       is the convention every rig in this file uses and the one that made the
       first version of this creature stand on its head. The hip's 180 is
       absolute up, so a child of the hip at rest 0 also points UP -- which is
       right for the cage and catastrophic for a leg. Anything meant to hang
       off this skeleton is near 180 from its parent, not near 0. */
    b[EFF_HIP]   = mk(-1, 180, hipLen, hipW, hipW * 11 / 10, 1, 2, 0);
    /* The spine, and it is THIN. It runs the whole height of the cage so the
       shoulders are at the top of the ribs rather than just above the hip, but
       at little more than a stave's width, because anything solid in the
       middle of the cage fills in the gap the cage exists to have. */
    b[EFF_CHEST] = mk(EFF_HIP, 0, cageLen, staveW * 3 / 2, staveW * 2, 1, 2, 250);
    /* Off the spine's TIP, so the head sits above the ribs rather than inside
       them -- and short, so it barely clears the shoulders. The tallest point
       of the creature is the cage, not a face. */
    b[EFF_HEAD]  = mk(EFF_CHEST, 0, headLen, staveW * 4, staveW * 2, 4, 4, 255);
    /* Hangs back DOWN inside the ribs from halfway up the spine. Drawn on the
       near layer so it reads as being behind the front ribs and in front of
       the back ones, which is the whole trick of a light in a lantern. */
    b[EFF_HEART] = mk(EFF_CHEST, 180, cageLen / 2,
                      staveW * 5, staveW * 3, 3, 3, 130);

    /* --- the cage ---------------------------------------------------------
       Six ribs off the top of the hip, fanning OUT as they rise: a brazier
       rather than a barrel. They are meant to be read as separate lines with
       daylight between them, so the fan has to be wide -- the first version
       spread them by four to eighteen degrees and six ribs at that spacing
       overlapped into a solid oval, which is a torso and not a cage.

       Longer than the spine by a tenth, because a rib that stops exactly at
       the shoulder looks cut off rather than open. */
    /* Attached at three HEIGHTS along the spine rather than all at the hip,
       and that is what makes this a ribcage instead of a wedge. Six ribs
       sharing one origin are six lines through the same point, and the first
       twenty cells of every one of them overlap into a solid triangle -- which
       is what the first two versions drew, once as a bare tree and once as a
       hood. Started apart, they enclose a space.

       Each rib goes OUT, nearly square to the spine, and then folds back to
       parallel: a stave of a barrel. The fold is the negative of the lean, so
       every rib finishes pointing the same way whatever it leaned to get
       there -- the computed-fold trick the harvestman's legs use. */
    static const int ribAt[EFF_STAVES / 2] = { 40, 120, 200 };
    static const int ribOut[EFF_STAVES / 2] = { 58, 70, 62 };
    for (int t = 0; t < EFF_STAVES; ++t) {
        const bool nearSide = t >= EFF_STAVES / 2;
        const int  pair  = t % (EFF_STAVES / 2);
        const int  turn  = nearSide ? 1 : -1;
        const int  sh    = nearSide ? 2 : 0;
        const int  layer = nearSide ? 3 : 1;
        const int  out   = ribOut[pair];
        b[effStaveBone(t, 0)] = mk(EFF_CHEST, turn * out,
                                   cageLen * 40 / 100, staveW, staveW,
                                   sh, layer, (u8)ribAt[pair]);
        b[effStaveBone(t, 1)] = mk(effStaveBone(t, 0), -turn * out,
                                   cageLen * 62 / 100, staveW, staveW * 3 / 4,
                                   sh, layer, 255);
    }

    /* --- the legs ---------------------------------------------------------
       DIGITIGRADE: a thigh that drops and leans forward, a shin that folds
       BACK past vertical, and a foot that reaches forward again to land. The
       net of the three is a leg that arrives under the body having bent the
       wrong way twice -- a bird's leg, not a person's, and the difference is
       visible at a glance even in silhouette.

       The thigh is the long one. Even segments give a zigzag; a long thigh
       with a shorter fold under it gives the heel-in-the-air stance that makes
       the shape read. */
    const int legSeg[EFF_SEGS] = { (legSpan * 46) / 100,
                                   (legSpan * 36) / 100,
                                   (legSpan * 18) / 100 };
    const int legW[EFF_SEGS + 1] = { (W * 5) / 100, (W * 4) / 100,
                                     (W * 3) / 100, (W * 3) / 100 };
    for (int t = 0; t < EFF_LEGS; ++t) {
        const bool nearSide = t == 1;
        /* One leg forward and one back -- a standing figure drawn with both
           legs in the same place is a post. The near one takes the forward
           stance, so the leg the eye reads first is the one with the stride
           in it. */
        const int stance = nearSide ? -27 : 21;
        const int rest[EFF_SEGS] = { 180 + stance, 44, -34 };
        for (int seg = 0; seg < EFF_SEGS; ++seg) {
            const int idx = effLegBone(t, seg);
            const int parent = seg == 0 ? EFF_HIP : idx - 1;
            b[idx] = mk(parent, rest[seg], legSeg[seg],
                        legW[seg], legW[seg + 1],
                        nearSide ? 2 : 0, nearSide ? 3 : 1,
                        seg == 0 ? 30 : 255);
        }
    }

    /* --- the arms ---------------------------------------------------------
       Off the SPINE'S TIP, so they hang from the shoulders at the top of the
       cage, and long enough that the hand ends around the knee: the three
       segments come to 44% of the box against the leg's 50%, measured from a
       shoulder that is much higher than the hip. An arm that stopped at the
       waist would read as a person's. */
    const int armSpan = (H * 44) / 100;
    const int armSeg[EFF_SEGS] = { (armSpan * 40) / 100,
                                   (armSpan * 36) / 100,
                                   (armSpan * 24) / 100 };
    const int armW[EFF_SEGS + 1] = { (W * 4) / 100, (W * 3) / 100,
                                     (W * 2) / 100, (W * 2) / 100 };
    for (int t = 0; t < EFF_ARMS; ++t) {
        const bool nearSide = t == 1;
        const int turn = nearSide ? 1 : -1;
        /* Nearly straight down with a small outward set at the shoulder and
           the barest bend at the elbow. These are not posed -- they HANG, and
           the stride is what swings them. */
        /* Set well out at the shoulder -- 34 degrees off vertical -- so the
           arm hangs OUTSIDE the ribs rather than down through them. At 14 the
           arms were inside the cage and the whole middle of the creature was
           one indistinct column. */
        const int rest[EFF_SEGS] = { 180 - turn * 46, turn * 30, turn * 10 };
        for (int seg = 0; seg < EFF_SEGS; ++seg) {
            const int idx = effArmBone(t, seg);
            const int parent = seg == 0 ? EFF_CHEST : idx - 1;
            b[idx] = mk(parent, rest[seg], armSeg[seg],
                        armW[seg], armW[seg + 1],
                        nearSide ? 2 : 0, nearSide ? 3 : 1,
                        seg == 0 ? 240 : 255);
        }
    }

    rig->name  = name;
    rig->bone  = b;
    rig->bones = EFF_BONES;
    rig->shade = shade;
    rig->w = w; rig->h = h;
    rig->rootX = (i16)(W / 2);
    /* Half way down, which is where the legs end: everything above this point
       is cage and everything below it is leg. The harvestman's 70% would drive
       the ribs off the top of the canvas, because that rig builds DOWNWARD
       from an arch and this one builds upward from a pelvis. */
    rig->rootY = (i16)((H * 50) / 100);
}

/* --- the stride --------------------------------------------------------------
   Two legs, so there is nothing to phase apart and no thicket to keep in order.
   What has to be generated instead is weight, and it comes from three things
   moving together:

     the legs a half cycle apart, swinging at the hip and folding at the knee;
     the arms a half cycle behind the leg on their own side, which is what a
     walking body actually does and what stops this reading as a shamble;
     and the root DROPPING as a foot lands and rising as it passes over.

   The root drop is the one that carries the weight. Without it a two-legged
   walk is a pair of scissors opening and closing. */
void rigEffigyWalk(PoseKey* keys, int count) {
    memset(keys, 0, sizeof(PoseKey) * (size_t)count);
    for (int k = 0; k < count; ++k) {
        PoseKey& p = keys[k];
        const int cyc = (k * 360) / count;
        for (int t = 0; t < EFF_LEGS; ++t) {
            const int phase = cyc + (t ? 180 : 0);
            const int swing = isin1024(phase);
            const int fold  = isin1024(phase + 90);
            /* The hip swings, the knee folds only while the leg is BEHIND the
               body -- a knee that folds on the forward swing kicks. */
            p.angle[effLegBone(t, 0)] = (i16)((swing * 26) / 1024);
            p.angle[effLegBone(t, 1)] = (i16)((fold < 0 ? -fold : 0) * 30 / 1024);
            p.angle[effLegBone(t, 2)] = (i16)((swing * -8) / 1024);
        }
        for (int t = 0; t < EFF_ARMS; ++t) {
            /* Opposite the leg on the same side, which is the half cycle that
               makes a walk look like a walk. */
            const int phase = cyc + (t ? 0 : 180);
            const int swing = isin1024(phase);
            const int turn  = t ? 1 : -1;
            p.angle[effArmBone(t, 0)] = (i16)((swing * turn * 15) / 1024);
            p.angle[effArmBone(t, 1)] = (i16)((swing * turn * 9) / 1024);
        }
        /* Twice the leg cycle: the body drops on EVERY footfall, and there are
           two of those per stride. The heart lags it, because a weight on a
           chain arrives late. */
        p.rootDY = (i16)((isin1024(cyc * 2) * ARM_SS * 2) / 1024);
        p.angle[EFF_HEART] = (i16)((isin1024(cyc * 2 - 60) * 7) / 1024);
    }
}

static PoseKey g_effWalkKeys[8];
static PoseKey g_effIdleKeys[2];

static void buildEffClips() {
    rigEffigyWalk(g_effWalkKeys, 8);
    /* Standing still is the legs planted and the heart still swinging. A boss
       that freezes completely between steps reads as a prop. */
    rigEffigyWalk(g_effIdleKeys, 2);
    for (int k = 0; k < 2; ++k) {
        for (int t = 0; t < EFF_LEGS; ++t)
            for (int s = 0; s < EFF_SEGS; ++s)
                g_effIdleKeys[k].angle[effLegBone(t, s)] = 0;
        for (int t = 0; t < EFF_ARMS; ++t)
            for (int s = 0; s < EFF_SEGS; ++s)
                g_effIdleKeys[k].angle[effArmBone(t, s)] = 0;
        g_effIdleKeys[k].rootDY = 0;
    }
    g_effIdleKeys[1].angle[EFF_HEART] = 5;
    g_effIdleKeys[1].rootDY = -ARM_SS / 2;
}

static const Clip EFF_WALK = { "effwalk", g_effWalkKeys, 8, 8, true, true };
static const Clip EFF_IDLE = { "effidle", g_effIdleKeys, 2, 2, true, true };
const Clip RIG_EFF_WALK = EFF_WALK;
const Clip RIG_EFF_IDLE = EFF_IDLE;

struct TentClipInit { TentClipInit() { buildTentClips(); buildSpiderClips(); buildHarvClips(); buildEffClips(); } };
static TentClipInit g_tentClipInit;
