#pragma once
#include "common.h"
#include "player.h"   /* PLAYER_W/H: the sprite canvas IS the collision box */

/* --- item sprites ----------------------------------------------------------

   Pixel art, written as character rows and expanded once at startup into flat
   u32 buffers with 0 meaning transparent.

   Character art rather than hex arrays because these get edited by eye. A row
   of ".RBBKBBBBBBR." is a picture you can read and change in a text editor; the
   same row as twelve 0xRRGGBB literals is not, and the whole point of art is
   that adjusting it should be cheap.

   Materials do NOT get sprites and should not. Every material already has a
   colour that means something in this game -- a stack of stone is stone-
   coloured, automatically, from the same LUT the world uses -- and a swatch
   reads faster than a glyph. Sprites are for the things that have no natural
   colour: tools and modules. */

static const int SPR_W = 14;
static const int SPR_H = 14;

enum SpriteId {
    SPR_NONE = 0,
    SPR_TOOL1,       /* screwdriver */
    SPR_TOOL2,       /* the same idea, bigger and heavier */
    SPR_MOD_SHOT,
    SPR_MOD_BLAST,
    SPR_MINE1,       /* the mining ladder: one silhouette, four sizes */
    SPR_MINE2,
    SPR_MINE3,
    SPR_MINE4,
    SPR_SEED,
    SPR_FLINT,
    SPR_FLARE,
    SPR_ITEM_GENERIC,
    SPR_BOLTER,
    SPR_BENCH,
    SPR_BED,
    SPR_ANVIL,
    SPR_CHEMSTN,
    SPR_ASSEMBLY,
    SPR_FORGESTN,
    SPR_BOOTS,       /* worn on the feet */
    SPR_HERMES,      /* also worn on the feet, and the reason that slot is a choice */
    SPR_PACK1,       /* the jetpack ladder: one silhouette, three sizes */
    SPR_PACK2,
    SPR_PACK3,
    /* Devices. These are the one place a sprite is BOTH the inventory icon and
       the object in the world -- see DEV_W in device.h -- so the art is read at
       two very different scales and has to survive both. */
    SPR_THERMO,
    SPR_CLOCK,
    SPR_PLACER,
    SPR_MINER,
    SPR_TORCH,
    SPR_PIPE,
    SPR_CROSSOVER,
    SPR_CHEST,
    SPR_SPOUT,
    SPR_DRAIN,
    SPR_BUTTON,
    SPR_CIRCUIT_CONSTANT,
    SPR_CIRCUIT_ARITH,
    SPR_CIRCUIT_DECIDER,
    /* Virtual circuit signals. They are sprites rather than bare text so a
       numbered channel has the same visual weight as a material signal. */
    SPR_SIGNAL1,
    SPR_SIGNAL2,
    SPR_SIGNAL3,
    SPR_SIGNAL4,
    SPR_SIGNAL5,
    SPR_SIGNAL6,
    SPR_SIGNAL7,
    SPR_SIGNAL8,
    SPR_SIGNAL9,
    /* --- creatures --------------------------------------------------------
       The one category here that is neither an inventory icon nor a machine:
       these are drawn in the WORLD, at one sprite pixel per cell, so they are
       read at the same scale as the terrain rather than at hotbar size.

       That changes what the art has to do. An icon needs to be identifiable in
       a grid of other icons; a creature needs a SILHOUETTE that survives being
       seen at the edge of a torch's reach, in motion, against rock. So each of
       these is built around one unmistakable outline -- a low wedge, a pair of
       wings, a sagging blob -- and the interior detail is secondary. */
    SPR_MITE,
    SPR_MOTH,
    SPR_SLIME,
    SPR_HUSK,
    SPR_BAT,
    SPR_SPITTER,
    /* The boss. Drawn from the same 14x14 canvas as everything else and then
       SCALED UP by its collision box -- see entDraw -- rather than given a
       larger grid of its own. Fourteen pixels of shape blown up to 34 cells
       reads as a big creature, and a second canvas size would mean a second set
       of art conventions for one sprite. */
    SPR_BROOD,
    /* Player accessories: pendant, charged coil, and paired lens. Appended so
       the established sprite ids above keep their meaning. */
    SPR_ACC_GARLIC,
    SPR_ACC_OVERLOAD,
    SPR_ACC_TWIN,
    /* The creature charms. Each one quotes the thing it came off -- a plate, a
       lamp, a lens, a heart, a wing, a barb -- because "which creature drops
       this" is the single most useful fact about a rare drop and a row of
       generic amulets would hide it. */
    SPR_ACC_CARAPACE,
    SPR_ACC_LANTERN,
    SPR_ACC_MAGNET,
    SPR_ACC_HEART,
    SPR_ACC_SWIFT,
    SPR_ACC_BRACER,
    SPR_ACC_WHETSTONE,
    SPR_ACC_CHRONO,
    SPR_PEDESTAL,
    SPR_COUNT
};

/* 0 = transparent, anything else is a packed 0xRRGGBB. */
extern u32 g_sprite[SPR_COUNT][SPR_W * SPR_H];

/* --- the character ---------------------------------------------------------

   Its own canvas, exactly the size of the collision box, so what you see is
   what you collide with. That is not a stylistic choice: this world buries you
   in sand, and if the sprite were bigger than the hitbox (as it is in most
   platformers) material would visibly rest partway inside the character and the
   whole "stuff rolls off the pointed shoulders" behaviour would read as broken.

   The frames are POSED, not drawn: see rig.h. What used to be seven pictures
   sharing one hand-drawn body is now one skeleton and a list of joint angles,
   baked into these buffers at startup. The renderer cannot tell the difference
   and does not need to -- it still indexes a frame -- but adding a pose is now
   a row of numbers rather than a picture, and adding a CREATURE is a rig.

   Eight walk frames rather than four, because interpolating between key poses
   costs nothing at bake time and four frames of a stride at this size reads as
   a stutter. */
static const int PSPR_W = PLAYER_W;
static const int PSPR_H = PLAYER_H;

enum PlayerFrame {
    PF_IDLE = 0,
    PF_IDLE2,
    PF_WALK0, PF_WALK1, PF_WALK2, PF_WALK3,
    PF_WALK4, PF_WALK5, PF_WALK6, PF_WALK7,
    PF_JUMP,                                  /* rising */
    PF_FALL,                                  /* descending */
    PF_COUNT
};
static const int PF_WALK_FRAMES = 8;

extern u32 g_playerSpr[PF_COUNT][PSPR_W * PSPR_H];

/* --- crouching ---------------------------------------------------------
   A shorter canvas holding the SAME skeleton in a different pose, cropped out
   of a full-height bake -- not a second, smaller rig.

   Building a fresh rig at (PLAYER_W, CROUCH_H) was the first attempt and it
   was wrong in a way that is obvious the moment it is rendered beside the
   standing frame: rigHumanoid takes its proportions from the canvas, so every
   bone came out four fifths as long and the result reads as a CHILD STANDING
   UP rather than an adult crouching. Nothing about a crouch shortens a femur.

   So RIG_CROUCH is posed on the ordinary full-height rig, floor-snapped like
   any other grounded clip, and the bottom CROUCH_H rows are copied out. The
   hunch has to actually fit in that many rows, which buildPlayerFrames()
   checks rather than assumes -- a pose that stood too tall would otherwise be
   silently decapitated by the crop.

   One pose, moving or not, the same choice already made for jump and fall:
   both of those are a single unanimated frame too, not a cycle. A crouch-walk
   shuffle is a reasonable thing to want later; nothing has asked for it yet. */
static const int CSPR_W = PLAYER_W;
static const int CSPR_H = CROUCH_H;
enum PlayerCrouchFrame { PCF_CROUCH = 0, PCF_COUNT };
extern u32 g_playerCrouchSpr[PCF_COUNT][CSPR_W * CSPR_H];

void initSprites();
