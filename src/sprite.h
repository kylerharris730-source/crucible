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
    SPR_COUNT
};

/* 0 = transparent, anything else is a packed 0xRRGGBB. */
extern u32 g_sprite[SPR_COUNT][SPR_W * SPR_H];

void initSprites();
