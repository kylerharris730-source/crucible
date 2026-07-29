#include "sprite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

u32 g_sprite[SPR_COUNT][SPR_W * SPR_H];

/* One palette shared by every sprite, so a colour means the same thing
   everywhere: T is always a highlight, S is always steel, and the two handle
   colours are the two multitool tiers. Per-sprite palettes would let two icons
   drift apart in a set that is supposed to look like a set. */
static u32 paletteOf(char c) {
    switch (c) {
    case '.': return 0;            /* transparent */
    case 'T': return 0xF2F5FF;     /* highlight / working tip */
    case 'S': return 0xAEB6C4;     /* steel shaft */
    case 'G': return 0x6E7684;     /* collar, and dark trim */
    case 'H': return 0xC8B070;     /* Mk I handle -- matches its item colour */
    case 'J': return 0xE8D9A0;     /* Mk II handle, lighter and richer */
    case 'R': return 0x8A93A6;     /* module casing rim */
    case 'B': return 0x2B3040;     /* module body */
    case 'K': return 0x9CE0FF;     /* shot symbol: the colour its shot is */
    case 'L': return 0xFFB040;     /* blast symbol */

    /* The suit. Its own letters rather than reusing the tools' -- a shared
       palette is only worth having while the colours mean the same thing, and
       "helmet white" is not "highlight". */
    case 'W': return 0xE4E9F2;     /* helmet shell */
    case 'w': return 0xB4BCCA;     /* helmet shade */
    case 'V': return 0x22405F;     /* visor glass */
    case 'v': return 0x74B4E4;     /* visor glint */
    case 'U': return 0xCBD3E0;     /* suit */
    case 'u': return 0x98A1B2;     /* suit shade */
    case 'P': return 0x545C6E;     /* life-support pack */
    case 'O': return 0xE08442;     /* orange trim */
    /* Gloves and boots. Mid slate rather than the near-black they started as:
       at 0x30363F they were within a few values of the night sky and of dirt,
       so the character's legs appeared to stop at the knee against both. Dark
       enough to read as a different material from the suit, light enough to
       have a silhouette of its own. */
    case 'g': return 0x5C6472;

    /* The mining ladder. One silhouette at four sizes, told apart by the
       colour of the body -- shape carries "this is a digger", colour carries
       "which one". Trying to make four distinguishable digger SHAPES in
       fourteen pixels would produce four things you cannot tell apart at all;
       one shape in four colours is legible at hotbar size instantly. */
    case '1': return 0xB07848;     /* bronze  */
    case '2': return 0x9AA6B4;     /* steel   */
    case '3': return 0xE0B048;     /* gold    */
    case '4': return 0xB070E8;     /* violet  */
    case 'k': return 0x3A3F49;     /* the bit: dark, on every tier */
    case 'p': return 0x8FC85A;     /* the shoot */
    case 'q': return 0x6A4A2A;     /* the husk */
    default:  return 0xFF00FF;     /* unmapped: loud on purpose */
    }
}

/* A screwdriver, pointing up and to the right. Diagonal rather than axis-
   aligned because a 14px canvas has room for a longer tool along its diagonal,
   and because a horizontal rod at this size reads as a dash. */
static const char* ART_TOOL1[SPR_H] = {
    "..............",
    "...........TT.",
    "..........SS..",
    ".........SS...",
    "........SS....",
    ".......SS.....",
    "......GG......",
    ".....HHH......",
    "....HHHH......",
    "...HHHH.......",
    "...HHH........",
    "..HH..........",
    "..............",
    "..............",
};

/* Mk II: the same silhouette, longer and thicker, with a brighter handle. The
   tier has to be legible from the icon alone at hotbar size, and "bigger" is
   the one visual difference that survives being 14 pixels across. */
static const char* ART_TOOL2[SPR_H] = {
    "............TT",
    "...........TTT",
    "..........SSS.",
    ".........SSS..",
    "........SSS...",
    ".......SSS....",
    "......GGG.....",
    ".....JJJJ.....",
    "....JJJJJ.....",
    "...JJJJJ......",
    "..JJJJJ.......",
    "..JJJJ........",
    "..GG..........",
    "..............",
};

/* Modules are chips: a rimmed rectangle with a symbol on it. The shared casing
   is the point -- it says "this is a module" before you have read the symbol,
   which is what makes a slot full of unfamiliar modules parse at a glance. */
static const char* ART_MOD_SHOT[SPR_H] = {
    "..............",
    ".RRRRRRRRRRRR.",
    ".RBBBBBBBBBBR.",
    ".RBBBBBBBBBBR.",
    ".RBBKBBBBBBBR.",
    ".RBBBKBBBBBBR.",
    ".RBKKKKKKKKBR.",
    ".RBBBKBBBBBBR.",
    ".RBBKBBBBBBBR.",
    ".RBBBBBBBBBBR.",
    ".RBBBBBBBBBBR.",
    ".RRRRRRRRRRRR.",
    "..............",
    "..............",
};

static const char* ART_MOD_BLAST[SPR_H] = {
    "..............",
    ".RRRRRRRRRRRR.",
    ".RBBBBBBBBBBR.",
    ".RBBBBLBBBBBR.",
    ".RBBLBLBLBBBR.",
    ".RBBBLLLBBBBR.",
    ".RBLLLLLLLBBR.",
    ".RBBBLLLBBBBR.",
    ".RBBLBLBLBBBR.",
    ".RBBBBLBBBBBR.",
    ".RBBBBBBBBBBR.",
    ".RRRRRRRRRRRR.",
    "..............",
    "..............",
};


/* --- the mining ladder -----------------------------------------------------
   A stubby bore pointing down-right: body at the top-left, a dark bit at the
   business end. Each tier is the same drawing grown outward, so the ladder
   reads as one family getting heavier rather than as four unrelated objects. */
static const char* ART_MINE1[SPR_H] = {
    "..............",
    "..............",
    "..111.........",
    ".11111........",
    ".111111.......",
    "..11111k......",
    "...111kk......",
    "....1kk.......",
    ".....k........",
    "..............",
    "..............",
    "..............",
    "..............",
    "..............",
};
static const char* ART_MINE2[SPR_H] = {
    "..............",
    "..222.........",
    ".22222........",
    "2222222.......",
    "22222222......",
    ".2222222k.....",
    "..222222kk....",
    "...2222kkk....",
    "....22kkk.....",
    ".....2kk......",
    "......k.......",
    "..............",
    "..............",
    "..............",
};
static const char* ART_MINE3[SPR_H] = {
    "..333.........",
    ".33333........",
    "3333333.......",
    "33333333......",
    "333333333.....",
    ".33333333k....",
    "..33333333k...",
    "...3333333kk..",
    "....333333kk..",
    ".....3333kkk..",
    "......333kk...",
    ".......33k....",
    "........k.....",
    "..............",
};
static const char* ART_MINE4[SPR_H] = {
    ".444..........",
    "44444.........",
    "444444........",
    "4444444.......",
    "44444444......",
    "444444444k....",
    ".444444444k...",
    "..4444444444..",
    "...444444kkk..",
    "....44444kkk..",
    ".....4444kkk..",
    "......444kk...",
    ".......44k....",
    "........k.....",
};

/* Grass seed: a husk with a shoot coming out of it. Green on brown reads as
   "growing" faster than any shape would at this size. */
static const char* ART_SEED[SPR_H] = {
    "..............",
    "..............",
    "........pp....",
    ".......pp.....",
    "......pp......",
    "....ppp.......",
    "...pp.........",
    "..qqqq........",
    ".qqqqqq.......",
    ".qqqqqq.......",
    "..qqqq........",
    "..............",
    "..............",
    "..............",
};

static void expand(int id, const char* const* art) {
    for (int y = 0; y < SPR_H; ++y) {
        /* A short row would silently read past the end of the string literal
           and paint whatever followed it in the binary. Art is edited by hand,
           so a miscounted row is not a hypothetical. */
        if ((int)strlen(art[y]) != SPR_W) {
            fprintf(stderr, "sprite %d row %d is %d chars, expected %d\n",
                    id, y, (int)strlen(art[y]), SPR_W);
            abort();
        }
        for (int x = 0; x < SPR_W; ++x)
            g_sprite[id][y * SPR_W + x] = paletteOf(art[y][x]);
    }
}

/* --- the character ---------------------------------------------------------

   Drawn facing RIGHT: the visor is toward the right of the helmet and the
   life-support pack sits on the left, so the silhouette says which way it is
   looking before you can make out a face. Facing left is the same art
   mirrored at draw time, which flips the pack to the correct side for free.

   Rows 0 and 1 are inset by 2 and 1 to match the collision taper. That is not
   decoration -- the helmet HAS to be the pointed part, because the taper is
   what sheds falling sand, and a square-topped helmet drawn over a pointed
   hitbox would show sand rolling off thin air. */
static const int PBODY_ROWS = 14;

static const char* ART_BODY[PBODY_ROWS] = {
    "...WW...",   /* inset 3: the tapered crown, 2 cells wide at the peak */
    "..WWWW..",   /* inset 2 */
    ".WWWWWW.",   /* inset 1 */
    "WWWVVVVw",
    "WWWVVVVw",
    "WWWVVVvw",
    ".wWWWWw.",   /* neck ring */
    "PPUUUUUU",
    "PPUOOUUu",
    "PPUOOUUu",
    ".PUUUUUu",
    ".uUUUUUu",
    ".uUUUUUg",   /* the leading hand */
    ".OUUUUO.",   /* belt */
};

static const int PLEG_ROWS = PSPR_H - PBODY_ROWS;   /* 8 */

/* --- stances ---------------------------------------------------------------

   Every leg here is VERTICAL and stays inside the stance width. That is the
   whole lesson of the first attempt, which drew the stride as both legs
   fanning outward from the hips down to boots at the very edges of the sprite.
   On paper that is "one leg forward, one leg back" in side view. On screen it
   is a pair of brackets, and a bracket-shaped leg reads as a knee bending the
   wrong way -- the figure looked bowlegged and its stride looked reversed.

   At eight pixels across there is no room to draw a leg at an angle: a
   two-cell-wide limb displaced by one cell per row is a 45-degree line, and a
   human leg is nowhere near 45 degrees off vertical even at a full sprint. So
   the gait is carried by which foot is LIFTED rather than by how far the legs
   splay, which is legible at any size and cannot be misread as anatomy. */
static const char* LEG_STAND[PLEG_ROWS] = {
    ".uUUUUu.", ".UUUUUU.", ".UU..UU.", ".UU..UU.",
    ".UU..UU.", ".UU..UU.", ".gg..gg.", "ggg..ggg",
};
/* One leg planted and vertical, the other swung forward with its boot two rows
   clear of the ground. ASYMMETRIC on purpose: a symmetric stride, with both
   legs fanning out to boots at opposite edges, is the shape that reads as bowed
   -- and it is not avoidable by tweaking, it is what a symmetric stride
   geometrically IS at this width. Only one leg ever leaves vertical, and the
   mirror supplies the other half of the cycle. */
static const char* LEG_STEP[PLEG_ROWS] = {
    ".uUUUUu.", ".UUUUUU.", ".UU.UU..", ".UU.UU..",
    ".UU..UU.", ".UU..ggg", ".UU.....", ".ggg....",
};
/* Both feet down and together, the moment the legs pass each other. */
static const char* LEG_PASS[PLEG_ROWS] = {
    ".uUUUUu.", ".UUUUUU.", "..UUUU..", "..UUUU..",
    "..UUUU..", "..UUUU..", "..UUUU..", ".gggggg.",
};
/* Toes pointed: legs together and tapering. Reads as a launch, and is the one
   pose that cannot be confused with any of the walking frames. */
static const char* LEG_JUMP[PLEG_ROWS] = {
    ".uUUUUu.", ".UUUUUU.", "..UUUU..", "..UUUU..",
    "..UUUU..", "...UU...", "...UU...", "..gggg..",
};
/* Straight and vertical, reaching for the ground. */
static const char* LEG_FALL[PLEG_ROWS] = {
    ".uUUUUu.", ".UUUUUU.", ".UU..UU.", ".UU..UU.",
    ".UU..UU.", ".UU..UU.", ".UU..UU.", ".gg..gg.",
};

u32 g_playerSpr[PF_COUNT][PSPR_W * PSPR_H];

static void composePlayer(int frame, const char* const* legs, bool mirrorLegs) {
    u32* out = g_playerSpr[frame];
    for (int y = 0; y < PSPR_H; ++y) {
        const char* row = (y < PBODY_ROWS) ? ART_BODY[y] : legs[y - PBODY_ROWS];
        const bool mirror = mirrorLegs && y >= PBODY_ROWS;
        if ((int)strlen(row) != PSPR_W) {
            fprintf(stderr, "player frame %d row %d is %d chars, expected %d\n",
                    frame, y, (int)strlen(row), PSPR_W);
            abort();
        }
        for (int x = 0; x < PSPR_W; ++x)
            out[y * PSPR_W + x] = paletteOf(row[mirror ? PSPR_W - 1 - x : x]);
    }
}

void initSprites() {
    memset(g_sprite, 0, sizeof(g_sprite));
    expand(SPR_TOOL1,     ART_TOOL1);
    expand(SPR_TOOL2,     ART_TOOL2);
    expand(SPR_MOD_SHOT,  ART_MOD_SHOT);
    expand(SPR_MOD_BLAST, ART_MOD_BLAST);
    expand(SPR_MINE1,     ART_MINE1);
    expand(SPR_MINE2,     ART_MINE2);
    expand(SPR_MINE3,     ART_MINE3);
    expand(SPR_MINE4,     ART_MINE4);
    expand(SPR_SEED,      ART_SEED);

    memset(g_playerSpr, 0, sizeof(g_playerSpr));
    composePlayer(PF_IDLE,  LEG_STAND, false);
    /* lift the leading foot, pass, lift the trailing foot, pass */
    composePlayer(PF_WALK0, LEG_STEP,  false);
    composePlayer(PF_WALK1, LEG_PASS,  false);
    composePlayer(PF_WALK2, LEG_STEP,  true);
    composePlayer(PF_WALK3, LEG_PASS,  false);
    composePlayer(PF_JUMP,  LEG_JUMP,  false);
    composePlayer(PF_FALL,  LEG_FALL,  false);
}
