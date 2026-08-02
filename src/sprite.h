#pragma once
#include "common.h"

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
   The art therefore respects the same taper -- see playerRowInset() in player.h.

   Frames are composed at startup from ONE shared body and a set of leg
   stances, rather than being seven independently drawn figures. Seven hand-
   drawn frames drift: a pixel of helmet moves between two of them and the
   character's head twitches every time you take a step. With a shared body
   only the legs can differ, so only the legs do. */
static const int PSPR_W = 8;
static const int PSPR_H = 22;

enum PlayerFrame {
    PF_IDLE = 0,
    PF_WALK0, PF_WALK1, PF_WALK2, PF_WALK3,   /* stride, pass, stride, pass */
    PF_JUMP,                                  /* rising */
    PF_FALL,                                  /* descending */
    PF_COUNT
};

extern u32 g_playerSpr[PF_COUNT][PSPR_W * PSPR_H];

void initSprites();
