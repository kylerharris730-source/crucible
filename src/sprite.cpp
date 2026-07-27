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

void initSprites() {
    memset(g_sprite, 0, sizeof(g_sprite));
    expand(SPR_TOOL1,     ART_TOOL1);
    expand(SPR_TOOL2,     ART_TOOL2);
    expand(SPR_MOD_SHOT,  ART_MOD_SHOT);
    expand(SPR_MOD_BLAST, ART_MOD_BLAST);
}
