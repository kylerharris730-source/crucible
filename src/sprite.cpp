#include "sprite.h"
#include "rig.h"
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
    /* Flight gear. The nozzle colours are shared between the boots and every
       jetpack tier on purpose: what makes a tier legible is how much APPARATUS
       there is, not a repaint, and three differently-coloured packs read as
       three unrelated items rather than as a ladder. */
    case 'n': return 0x3A4050;     /* nozzle housing */
    case 'm': return 0x8A93A6;     /* metal, lit edge */
    case 'f': return 0xFFC24A;     /* flame core */
    case 'e': return 0xE05A20;     /* flame edge */
    case 'c': return 0x6ED0FF;     /* coolant / tank window */
    /* Hermes boots. Their own two colours rather than the flight-gear metal,
       because they are not flight gear and the palette is only worth having
       while a colour means one thing: 'm' and 'n' say "there is a nozzle on
       this", which is the single fact these boots most need not to imply. */
    case 'y': return 0xE8D45A;     /* winged leather */
    case 'z': return 0xB09A2E;     /* its shade */
    case 'A': return 0xF6F0D0;     /* feather */
    /* Devices. A shared casing palette so every machine reads as part of one
       family of equipment, with only the FACE -- the dial, the gauge, whatever the
       thing actually does -- differing between them. That is the same reasoning as
       the jetpack tiers: the silhouette says "machine", the detail says which. */
    case 'D': return 0x4A5262;     /* casing */
    case 'd': return 0x333A48;     /* casing shade */
    case 'E': return 0x6E7888;     /* casing highlight / bezel */
    case 'F': return 0x1A1E26;     /* recessed face */
    case 'i': return 0xE8503A;     /* the needle: red, and the only warm thing */
    case 'l': return 0x9CE0FF;     /* terminal, where a spark goes in or out */

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

/* Boots with the thrusters on the heels. Drawn as a pair seen from the side so
   the nozzles are visible, because the nozzle is the whole point of the item --
   a plain boot silhouette would be indistinguishable from armour. */
static const char* ART_BOOTS[SPR_H] = {
    "..............",
    "..............",
    "...mm....mm...",
    "..mUUm..mUUm..",
    "..mUUm..mUUm..",
    "..mUUm..mUUm..",
    "..mUUmmmUUm...",
    "..mUUUUUUUm...",
    ".mmUUUUUUUmm..",
    ".mnnmmmmmnnm..",
    "..ff......ff..",
    "..ee......ee..",
    "...e.......e..",
    "..............",
};

/* The jetpack ladder. One tank at Mk I, two at Mk II, two plus a bigger nozzle
   block at Mk III -- so the tier is countable at a glance in the hotbar, which
   is where you actually need to tell them apart. */
static const char* ART_PACK1[SPR_H] = {
    "..............",
    "....mmmm......",
    "...mccccm.....",
    "...mccccm.....",
    "...mccccm.....",
    "...mUUUUm.....",
    "...mUUUUm.....",
    "...mnnnnm.....",
    "....nnnn......",
    "....ffff......",
    "....eeee......",
    ".....ee.......",
    "..............",
    "..............",
};

static const char* ART_PACK2[SPR_H] = {
    "..............",
    "..mmmm.mmmm...",
    ".mcccmmcccm...",
    ".mcccmmcccm...",
    ".mcccmmcccm...",
    ".mUUUmmUUUm...",
    ".mUUUmmUUUm...",
    ".mnnnmmnnnm...",
    "..nnn...nnn...",
    "..fff...fff...",
    "..eee...eee...",
    "...e.....e....",
    "..............",
    "..............",
};

static const char* ART_PACK3[SPR_H] = {
    "...mmm.mmm....",
    "..mcccmcccm...",
    "..mcccmcccm...",
    "..mcccmcccm...",
    "..mUUUmUUUm...",
    "..mUUUmUUUm...",
    ".mmUUUmUUUmm..",
    ".mnnnnmnnnnm..",
    ".mnnnnmnnnnm..",
    "..nnnn.nnnn...",
    "..ffff.ffff...",
    "..eeee.eeee...",
    "...ee...ee....",
    "....e....e....",
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

   Posed rather than drawn. The seven hand-drawn frames that used to live here
   -- one body plus a set of leg stances, mirrored -- are gone; what replaces
   them is one skeleton (rig.h) and a list of joint angles per pose, baked into
   the same buffers at startup.

   The comment they left behind is worth keeping, because it is the reason the
   character is now 11 cells wide instead of 8: "at eight pixels across there is
   no room to draw a leg at an angle". That was true, and the old frames dodged
   it by carrying the gait in which foot was lifted rather than in how the legs
   swung. A rig has no such dodge available, so the body grew until the angles
   were real. See PLAYER_W. */
u32 g_playerSpr[PF_COUNT][PSPR_W * PSPR_H];

static void buildPlayerFrames() {
    static Bone bone[RB_COUNT];
    RigDef rig;
    rigHumanoid(bone, &rig, "player", PSPR_W, PSPR_H, RIG_SUIT);
    armBake(&rig, &RIG_IDLE, g_playerSpr[PF_IDLE]);
    armBake(&rig, &RIG_WALK, g_playerSpr[PF_WALK0]);
    armBake(&rig, &RIG_JUMP, g_playerSpr[PF_JUMP]);
    armBake(&rig, &RIG_FALL, g_playerSpr[PF_FALL]);
}

/* The thermocouple: a boxed gauge with a dial face and a needle, and a terminal
   post on each side. The terminals are load-bearing art rather than decoration --
   they are where wiring is meant to meet it, so the picture has to say which
   edges are the electrical ones.

   Columns are strictly: 0 and 13 are the terminal posts (blank except on the
   two rows wiring meets), 1 and 12 casing, 2 and 11 bezel, 3..10 the face. Laid
   out that way rather than drawn freehand because expand() aborts on a
   miscounted row, and because a gauge whose bezel wanders by a pixel looks
   dented rather than machined. */
static const char* ART_THERMO[SPR_H] = {
    "..DDDDDDDDDD..",
    ".DEEEEEEEEEED.",
    ".DEFFFFFFFFED.",
    "lDEFFFFFFFFEDl",
    "lDEFFFiiFFFEDl",
    ".DEFFFiiFFFED.",
    ".DEFFFiiFFFED.",
    ".DEFFiiiiFFED.",
    ".DEFFFFFFFFED.",
    ".DEEEEEEEEEED.",
    ".DddddddddddD.",
    ".DdEEEEEEEEdD.",
    ".DddddddddddD.",
    "..DDDDDDDDDD..",
};

/* The clock: the same casing as the thermocouple with a different face, which is
   the whole point of a shared device palette -- you should know it is a machine
   from the silhouette and which machine from the face alone. Two hands rather than
   a dial, because a clock face is the one symbol nobody has to be taught.

   Same strict columns as the thermocouple: 0 and 13 terminals, 1 and 12 casing,
   2 and 11 bezel, 3..10 the face. */
static const char* ART_CLOCK[SPR_H] = {
    "..DDDDDDDDDD..",
    ".DEEEEEEEEEED.",
    ".DEFFFFFFFFED.",
    "lDEFFFiFFFFEDl",
    "lDEFFFiFFFFEDl",
    ".DEFFFiiiiFED.",
    ".DEFFFFFFFFED.",
    ".DEFFFFFFFFED.",
    ".DEFFFFFFFFED.",
    ".DEEEEEEEEEED.",
    ".DddddddddddD.",
    ".DdEEEEEEEEdD.",
    ".DddddddddddD.",
    "..DDDDDDDDDD..",
};

/* The placer: a funnel, mouth up. The face says which way the machine works,
   which matters more here than on the sensors -- a hopper that took from below
   would be a different machine, and the picture is the only thing that says so.

   The miner is the same casing with a bit pointing down, so the pair read as
   opposites at a glance: one open at the top, one toothed at the bottom. */
static const char* ART_PLACER[SPR_H] = {
    "..DDDDDDDDDD..",
    ".DEEEEEEEEEED.",
    ".DEFFFFFFFFED.",
    "lDEiFFFFFFiEDl",
    "lDEiiFFFFiiEDl",
    ".DEFiiFFiiFED.",
    ".DEFFiiiiFFED.",
    ".DEFFFiiFFFED.",
    ".DEFFFiiFFFED.",
    ".DEEEEEEEEEED.",
    ".DddddddddddD.",
    ".DdEEEEEEEEdD.",
    ".DddddddddddD.",
    "..DDDDDDDDDD..",
};

static const char* ART_MINER[SPR_H] = {
    "..DDDDDDDDDD..",
    ".DEEEEEEEEEED.",
    ".DEFFFFFFFFED.",
    "lDEFFFiiFFFEDl",
    "lDEFFFiiFFFEDl",
    ".DEFFFiiFFFED.",
    ".DEFFiiiiFFED.",
    ".DEFFFiiFFFED.",
    ".DEFFFFiFFFED.",
    ".DEEEEEEEEEED.",
    ".DdkdddddkddD.",
    ".DdkEEEEEkddD.",
    ".DdkddddkdddD.",
    "..DDDDDDDDDD..",
};

/* Hermes boots: the same pair of boots as ART_BOOTS with wings where the
   nozzles are. Deliberately the same silhouette below the ankle -- they are
   boots, and the reader should get that from the shape before reading anything
   else -- with everything that says WHICH boots happening above it. Rocket
   boots point down and burn; these point out and have feathers. */
static const char* ART_HERMES[SPR_H] = {
    "..............",
    "..............",
    "...zz....zz...",
    "..zyyz..zyyz..",
    "AAAyyz..zyyAAA",
    "AAAzyz..zyzAAA",
    ".AAzyyzzyyzAA.",
    "..zyyyyyyyz...",
    ".zzyyyyyyyzz..",
    ".zyyyyyyyyyz..",
    ".zzzzzzzzzzz..",
    "..zz......zz..",
    "..............",
    "..............",
};

/* A wall torch: a bracket, a shaft, and a flame. Deliberately NOT the boxed casing
   the machines share -- it is not machinery, and the whole point of a shared device
   palette is that it should mean something. */
static const char* ART_TORCH[SPR_H] = {
    "......ff......",
    ".....feef.....",
    "....feffef....",
    "....fefffe....",
    ".....feef.....",
    "......ff......",
    ".....EmmE.....",
    ".....DmmD.....",
    ".....DmmD.....",
    "....EDmmDE....",
    "....dDmmDd....",
    "....dDDDDd....",
    ".....dddd.....",
    "..............",
};

/* Logistics parts share a pipe-blue casing.  The crossover's two lanes are
   separated by a dark bridge, making the non-connection visible instead of a
   rule the player has to memorize. */
static const char* ART_PIPE[SPR_H] = {
    "....llllll....", "....llllll....", "....llDDll....", "....llDEll....",
    "...llDFFDll...", "...llDFFDll...", "...llDFFDll...", "...llDFFDll...",
    "...llDFFDll...", "...llDFFDll...", "....llDEll....", "....llDDll....",
    "....llllll....", "....llllll....",
};
static const char* ART_CROSSOVER[SPR_H] = {
    "....llllll....", "....llllll....", "....llDDll....", "....llDDll....",
    "llllDDFFDDllll", "llllDDFFDDllll", "llDDDDDDDDDDll", "llDDDDDDDDDDll",
    "llllDDFFDDllll", "llllDDFFDDllll", "....llDDll....", "....llDDll....",
    "....llllll....", "....llllll....",
};
static const char* ART_CHEST[SPR_H] = {
    ".DDDDDDDDDDDD.", ".DEEEEEEEEEED.", ".DFFFFFFFFFFD.", ".DFFFFFFFFFFD.", ".DFFFFFFFFFFD.",
    ".DddddddddddD.", ".DDEEEEEEEEDD.", ".DDEEEEEEEEDD.", ".DDEEEllEEEDD.", ".DDEEEllEEEDD.",
    ".DDEEEEEEEEDD.", ".DddddddddddD.", ".DDDDDDDDDDDD.", "..............",
};
static const char* ART_SPOUT[SPR_H] = {
    "....DDDDDD....", "...DEEEEEED...", "...DEFFFFED...", "...DEFFFFED...", ".llDEFFFFEDll.",
    ".llDEFFFFEDll.", "...DEFFFFED...", "...DEFFFFED...", "...DEFFFFED...", "...DEFFFFED...",
    "...DDEEEEDD...", "....DDEEDD....", ".....llll.....", ".....llll.....",
};
static const char* ART_DRAIN[SPR_H] = {
    ".....llll.....", ".....llll.....", "....DDEEDD....", "...DDEEEEDD...", "...DEFFFFED...",
    "...DEFFFFED...", "...DEFFFFED...", ".llDEFFFFEDll.", ".llDEFFFFEDll.", "...DEFFFFED...",
    "...DEFFFFED...", "...DEEEEEED...", "....DDDDDD....", "..............",
};
static const char* ART_BUTTON[SPR_H] = {
    "..............", "....EEEEEE....", "...EiiiiiiE...", "..EiiiiiiiiE..",
    "..EiiiiiiiiE..", "..EiiiiiiiiE..", "...EiiiiiiE...", "....EEEEEE....",
    "....DddddD....", "....DddddD....", "....DDDDDD....", "..............",
    "..............", "..............",
};
/* Circuit machines keep the shared casing, but their faces are deliberately
   symbolic: a dot is a constant source, a plus is arithmetic, and a split
   needle is a decision. The violet terminals distinguish information wires
   from the pale-blue terminals used by physical spark machinery. */
static const char* ART_CIRCUIT_CONSTANT[SPR_H] = {
    "..DDDDDDDDDD..", ".DEEEEEEEEEED.", ".DEFFFFFFFFED.", "lDEFFFFFFFFEDl",
    "lDEFFFiFFFFEDl", "lDEFFFiFFFFEDl", ".DEFFFFFFFFED.", ".DEFFFFFFFFED.",
    ".DEFFFFFFFFED.", ".DEEEEEEEEEED.", ".DddddddddddD.", ".DdEEEEEEEEdD.",
    ".DddddddddddD.", "..DDDDDDDDDD..",
};
static const char* ART_CIRCUIT_ARITH[SPR_H] = {
    "..DDDDDDDDDD..", ".DEEEEEEEEEED.", ".DEFFFFFFFFED.", "lDEFFFFiFFFEDl",
    "lDEFFFFiFFFEDl", "lDEFFiiiiiFEDl", ".DEFFFFiFFFED.", ".DEFFFFiFFFED.",
    ".DEFFFFFFFFED.", ".DEEEEEEEEEED.", ".DddddddddddD.", ".DdEEEEEEEEdD.",
    ".DddddddddddD.", "..DDDDDDDDDD..",
};
static const char* ART_CIRCUIT_DECIDER[SPR_H] = {
    "..DDDDDDDDDD..", ".DEEEEEEEEEED.", ".DEFFFFFFFFED.", "lDEFFiFFFFFEDl",
    "lDEFFiiFFFFEDl", "lDEFFFiFFFFEDl", ".DEFFFFiFFFED.", ".DEFFFFFFFFED.",
    ".DEFFFFFFFFED.", ".DEEEEEEEEEED.", ".DddddddddddD.", ".DdEEEEEEEEdD.",
    ".DddddddddddD.", "..DDDDDDDDDD..",
};

/* The nine virtual signals are compact violet seven-segment chips. Generated
   from masks rather than nine copied arrays so their casing, scale and terminal
   language cannot drift apart. */
static void makeSignalSprite(int sprite, int digit) {
    static const unsigned char MASK[10] = {
        0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
    };
    u32* out = g_sprite[sprite];
    const u32 rim = 0x4A5262, face = 0x252838, on = 0xD8A8FF, off = 0x5C506C;
    for (int y = 2; y <= 11; ++y) for (int x = 2; x <= 11; ++x)
        out[y * SPR_W + x] = (x == 2 || x == 11 || y == 2 || y == 11) ? rim : face;
    const unsigned char mask = MASK[digit];
    /* a,b,c,d,e,f,g in seven-segment order. */
    const int seg[7][4] = {
        { 5, 3, 8, 3 }, { 9, 4, 9, 6 }, { 9, 8, 9,10 }, { 5,10, 8,10 },
        { 4, 8, 4,10 }, { 4, 4, 4, 6 }, { 5, 7, 8, 7 }
    };
    for (int s = 0; s < 7; ++s) {
        const u32 c = (mask & (1 << s)) ? on : off;
        const int x0 = seg[s][0], y0 = seg[s][1], x1 = seg[s][2], y1 = seg[s][3];
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) out[y * SPR_W + x] = c;
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
    expand(SPR_BOOTS,     ART_BOOTS);
    expand(SPR_HERMES,    ART_HERMES);
    expand(SPR_PACK1,     ART_PACK1);
    expand(SPR_PACK2,     ART_PACK2);
    expand(SPR_PACK3,     ART_PACK3);
    expand(SPR_THERMO,    ART_THERMO);
    expand(SPR_CLOCK,     ART_CLOCK);
    expand(SPR_PLACER,    ART_PLACER);
    expand(SPR_MINER,     ART_MINER);
    expand(SPR_TORCH,     ART_TORCH);
    expand(SPR_PIPE,      ART_PIPE);
    expand(SPR_CROSSOVER, ART_CROSSOVER);
    expand(SPR_CHEST,     ART_CHEST);
    expand(SPR_SPOUT,     ART_SPOUT);
    expand(SPR_DRAIN,     ART_DRAIN);
    expand(SPR_BUTTON,    ART_BUTTON);
    expand(SPR_CIRCUIT_CONSTANT, ART_CIRCUIT_CONSTANT);
    expand(SPR_CIRCUIT_ARITH,    ART_CIRCUIT_ARITH);
    expand(SPR_CIRCUIT_DECIDER,  ART_CIRCUIT_DECIDER);
    for (int digit = 1; digit <= 9; ++digit) makeSignalSprite(SPR_SIGNAL1 + digit - 1, digit);

    buildPlayerFrames();
}
