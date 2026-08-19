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
    /* Rough wood, for the starter weapon's stock. A SYMBOL rather than a
       letter because every letter and digit in this palette is now spoken for
       -- which is a warning worth leaving here: the next few additions will
       have to either reuse a colour that already means something else or start
       on punctuation, and the first of those is how a shared palette quietly
       stops being shared. */
    case '#': return 0x6A4A2A;
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

    /* --- creatures --------------------------------------------------------
       Their own letters rather than borrowed ones, and the reason is the same
       one the suit gives above: a shared palette is only worth having while a
       colour means the same thing everywhere, and "carapace" is not "steel
       shaft". These are also the only entries read at world scale rather than
       at hotbar size, so they are picked for contrast against TERRAIN -- stone
       is 0x6E747C and dirt is browner still, which is why the mite is darker
       and warmer than either rather than the mid-grey a bug wants to be. */
    case 'a': return 0x463A2E;     /* mite: underside, deepest shade */
    case 'b': return 0x6A5643;     /* mite: carapace */
    case 'h': return 0x8E7758;     /* mite: lit plates along the back */
    case 'j': return 0x241E18;     /* mite: legs and mandible, near-black */
    case 'x': return 0xFF8A3A;     /* an ember eye. Shared by all three: it is
                                      the one thing that says "alive" at the
                                      edge of a torch's reach, and it should
                                      mean that regardless of species. */
    case 'M': return 0x8A6E52;     /* moth: wing */
    case 'N': return 0xC0A47C;     /* moth: wing edge, catching the light */
    case 'X': return 0xE0561C;     /* moth: body, banked heat */
    case 'Y': return 0xFFC24A;     /* moth: the core of it */
    case 'Q': return 0x6FA23C;     /* slime: body */
    case 'C': return 0x466A26;     /* slime: shade, and the drips */
    case 'Z': return 0xA8D866;     /* slime: highlight along the top */

    /* --- the Terraria half ------------------------------------------------
       Four more creatures. The letters are picked from what was actually LEFT
       rather than from what would read nicely, which is why there are digits in
       here: this palette is one switch over a char, so a duplicate case is a
       compile error, and every obvious letter was already spoken for. Used
       elsewhere and unavailable: T S G H J R B K L W w V v U u P O g 1-4 k p q
       n m f e c y z A D d E F i l a b h j x M N X Y Q C Z. */
    /* Husk: drained greyish-green, the only humanoid down here. */
    case 'o': return 0x6E7A52;     /* husk: hide */
    case 'r': return 0x4E5838;     /* husk: shade and hollows */
    case 's': return 0x8E9A6E;     /* husk: lit edge */
    case 't': return 0x2A2E20;     /* husk: the gaps between its ribs */
    /* Bat: nearly black with a violet cast, so it reads as a SILHOUETTE first --
       right for the one creature you track by its motion rather than its
       detail. */
    case '5': return 0x3A2C38;     /* bat: membrane */
    case '6': return 0x6A4C68;     /* bat: lit edge of the wing */
    case '7': return 0x241A24;     /* bat: body, darkest */
    /* Spitter: warm clay-brown with a pale sac. The sac is the tell that this
       one shoots, so it is the lightest thing on the creature. */
    case '8': return 0x8A5A3A;     /* spitter: carapace */
    case '9': return 0xA87A52;     /* spitter: lit plates */
    case 'I': return 0xD8E098;     /* spitter: the venom sac */
    /* Brood mother: her young are warm browns and she is RED, because a boss
       should be recognisable as itself from across a cavern before any of the
       detail resolves. */
    case '0': return 0xB04838;     /* brood: carapace */
    case '+': return 0x7A2A20;     /* brood: shade */
    case '=': return 0xE87A4A;     /* brood: lit ridge */

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
/* A second, independently baked skeleton at (PSPR_W, CROUCH_H) -- see the note
   on g_playerCrouchSpr in sprite.h for why this is not the standing rig
   squashed into a shorter canvas. */
u32 g_playerCrouchSpr[PCF_COUNT][CSPR_W * CSPR_H];

static void buildPlayerFrames() {
    static Bone bone[RB_COUNT];
    RigDef rig;
    rigHumanoid(bone, &rig, "player", PSPR_W, PSPR_H, RIG_SUIT);
    armBake(&rig, &RIG_IDLE, g_playerSpr[PF_IDLE]);
    armBake(&rig, &RIG_WALK, g_playerSpr[PF_WALK0]);
    armBake(&rig, &RIG_JUMP, g_playerSpr[PF_JUMP]);
    armBake(&rig, &RIG_FALL, g_playerSpr[PF_FALL]);

    /* The crouch is the same skeleton, posed and then CROPPED -- see the note
       on g_playerCrouchSpr. Floor-snapped by the clip, so the figure is
       already sitting on the bottom row of the full canvas and the bottom
       CSPR_H rows are exactly the crouched body. */
    static u32 full[PSPR_W * PSPR_H];
    armBake(&rig, &RIG_CROUCH, full);
    const int cut = PSPR_H - CSPR_H;
    /* The hunch has to FIT. A pose that stands taller than the crouch box
       would lose its head to the crop and nothing downstream could tell --
       the sprite would simply be a decapitated figure that still passed every
       size assertion. Checked the same way expand() checks hand-edited art,
       and fatal for the same reason: it is a fact about the art, so it either
       always holds or never does. */
    for (int y = 0; y < cut; ++y)
        for (int x = 0; x < PSPR_W; ++x)
            if (full[y * PSPR_W + x]) {
                fprintf(stderr, "crouch pose is taller than CROUCH_H: lit pixel "
                                "at row %d, %d rows above the crop\n", y, cut - y);
                abort();
            }
    memcpy(g_playerCrouchSpr[PCF_CROUCH], full + (size_t)cut * PSPR_W,
           sizeof(u32) * PSPR_W * CSPR_H);
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

/* --- the creatures ---------------------------------------------------------
   All three face RIGHT; entDraw mirrors them by reading the far column when
   facing is negative, so there is one drawing of each rather than two that can
   drift apart.

   Each is built around a silhouette that survives being seen badly: a low wedge
   bristling with legs, a wide pair of wings around a hot core, a sagging blob
   that drips. At one sprite pixel per world cell these are about a third of the
   character's height, which is the size a thing has to be before you can tell
   what it is while it is moving toward you. */

/* The burrower. Low and wide, all back-plate and legs, with the mandible and
   the eye at the leading edge -- so which way it is coming is readable from the
   silhouette alone before any of the detail resolves. */
static const char* ART_MITE[SPR_H] = {
    "..............",
    "..............",
    "..............",
    "....bbbb......",
    "..bbhhhhbb....",
    ".bbhhhhhhbb...",
    ".bhhhhhhhhbbj.",
    ".abhhhhhhhhbxj",
    ".aabbbbbbbbbxj",
    ".aaaaaaaaaaaj.",
    "..j..j..j..j..",
    "..j..j..j..j..",
    "..............",
    "..............",
};

/* The heat-seeker. Nearly all wing, with a narrow column of banked heat down
   the middle -- it is the only creature here that GLOWS, which is the honest
   thing for something that spends its life looking for the hottest cell it can
   find, and it makes one visible in an unlit tunnel before it reaches you.

   The first version left the middle of the canvas empty for the top three
   rows, so the two wing halves did not meet and the thing read as a butterfly
   that had been cut down the middle. Rendered large it was obvious and no
   number would ever have said so -- which is the whole argument for looking at
   the picture. The body is now a continuous column from the antennae to the
   abdomen, and the wings attach to it. */
static const char* ART_MOTH[SPR_H] = {
    "..............",
    "....N....N....",
    ".....N..N.....",
    "..NMMMXXMMMN..",
    ".NMMMMXXMMMMN.",
    ".NMMMMXYMMMMN.",
    ".NMMMMXYMMMMN.",
    "..NMMMXYMMMN..",
    "...NMMXYMMN...",
    "....NMXYMN....",
    "......XY......",
    "......XY......",
    "..............",
    "..............",
};

/* The corroder. Heaviest at the bottom and visibly falling apart at the base,
   because what actually threatens you is not the creature but what comes off
   it -- the drips in the last row are the whole warning.

   The underside went through the same correction the moth's wings did. It was
   a flat row of short stubs, which at world scale read as FEET -- and feet are
   exactly the wrong reading for the one creature here whose threat is that it
   leaks. It is now a ragged sag with two droplets falling clear of the body,
   so what the silhouette says about it is what it actually does. */
static const char* ART_SLIME[SPR_H] = {
    "..............",
    "..............",
    "..............",
    ".....ZZZ......",
    "...ZZQQQZZ....",
    "..ZQQQQQQQZ...",
    ".ZQQQQQQQQQZ..",
    ".QQQQQQQQQxQQ.",
    ".QQQQQQQQQQQQ.",
    ".CQQQQQQQQQQC.",
    ".CCQQQQQQQQCC.",
    "..CCCQQQQCCC..",
    "...CC.CC.CC...",
    "....C...C.....",
};


/* The zombie. Upright and heavy-shouldered, and the only thing in layer 1
   shaped like a PERSON -- so it registers as a different category of threat
   from the vermin before any detail lands. */
static const char* ART_HUSK[SPR_H] = {
    ".....ssss.....",
    "....soooos....",
    "....soxoos....",
    "....soooos....",
    ".....roor.....",
    "..ssooooooss..",
    "..soooooooos..",
    "..roooooooor..",
    "..r.oooooo.r..",
    "..r.oooooo.r..",
    "....oooooo....",
    "....otttto....",
    "....oo..oo....",
    "....rr..rr....",
};

/* The bat. Wings wide and body tiny, so the silhouette is almost entirely
   membrane -- which is what you actually track when one crosses a dark cavern
   at speed. */
static const char* ART_BAT[SPR_H] = {
    "..............",
    "..6........6..",
    ".656......656.",
    ".6555....5556.",
    "65555....55556",
    "6555557755555.",
    ".55557xx755555",
    "..5557777555..",
    "...55.77.55...",
    "......77......",
    "..............",
    "..............",
    "..............",
    "..............",
};

/* The spitter. Squat and front-heavy, with the venom sac riding high on its
   back where it cannot be missed -- the sac is the warning that this one
   shoots. */
static const char* ART_SPITTER[SPR_H] = {
    "..............",
    "..............",
    ".....III......",
    "....IIIII.....",
    "...9IIIII9....",
    "..9998889999..",
    ".988888888899.",
    ".98888888888x.",
    ".88888888888..",
    ".8888888888...",
    "..8.8.8.8.8...",
    "..8.8.8.8.8...",
    "..............",
    "..............",
};

/* The brood mother. The mite silhouette widened and given a ridged back, so she
   reads as the same KIND of thing you have been killing all layer, only wrong
   in scale. That recognition is the design -- she needs no introduction. */
static const char* ART_BROOD[SPR_H] = {
    "..............",
    "...======.....",
    "..=000000==...",
    ".=0000000000=.",
    "=000000000000=",
    "=00++++++000==",
    "=00+++++++000=",
    "=+++++++++0x0=",
    "=++++++++++x0=",
    ".+++++++++++=.",
    ".++++++++++=..",
    "..+.+.+.+.+=..",
    "..+.+.+.+.+...",
    "..+.+.+.+.+...",
};

/* Two stones and the spark between them. The spark is the whole icon -- a pair
   of grey lumps alone would read as "rock", which is exactly what this is made
   of and exactly not what it is for. */
static const char* ART_FLINT[SPR_H] = {
    "..............",
    "...SS.........",
    "..SSSS...f....",
    "..SSSSS.f.....",
    "...SSS..ff....",
    "....S..f..f...",
    ".......ff.....",
    "......f..f....",
    ".....SSS......",
    "....SSSSS.....",
    "...SSSSSSS....",
    "...SSSSSS.....",
    "....SSSS......",
    "..............",
};

/* A glass ampoule with a bright liquid core. The halo reads as a light source
   at hotbar size; the narrow neck keeps it from looking like another potion. */
static const char* ART_FLARE[SPR_H] = {
    "..............",
    "......W.......",
    ".....WWW......",
    "......W.......",
    ".....GGG......",
    "....GLLLG.....",
    "....GLLLG.....",
    "....GLLLG.....",
    "....GLLLG.....",
    ".....GGG......",
    "......G.......",
    ".....GGG......",
    "..............",
    "..............",
};

static const char* ART_ACC_GARLIC[SPR_H] = {
    "..............",
    "......q.......",
    ".....qWq......",
    "....WWWWW.....",
    "...WWwWwWW....",
    "...WwWWWwW....",
    "...WWWWWWW....",
    "....WWWWW.....",
    ".....WWW......",
    "......G.......",
    ".....GGG......",
    "....G...G.....",
    "..............",
    "..............",
};

static const char* ART_ACC_OVERLOAD[SPR_H] = {
    "......K.......",
    "..K...K...K...",
    "...K.....K....",
    "....GOOOG.....",
    "...GO...OG....",
    "..GO..K..OG...",
    "..GO.KTK.OG...",
    "..GO..K..OG...",
    "...GO...OG....",
    "....GOOOG.....",
    "...K.....K....",
    "..K.......K...",
    "......K.......",
    "..............",
};

static const char* ART_ACC_TWIN[SPR_H] = {
    "..............",
    "...444..444...",
    "..4WWW44WWW4..",
    ".4W...44...W4.",
    ".4W...44...W4.",
    ".4W...44...W4.",
    "..4WWW44WWW4..",
    "...444..444...",
    ".....4..4.....",
    "......44......",
    "......GG......",
    ".....G..G.....",
    "..............",
    "..............",
};

/* --- the creature charms ---------------------------------------------------
   Eight icons that have to be told apart at fourteen pixels, in a row, while
   four of them are worn at once. So each one is a SILHOUETTE quoting its
   source, not a repainted amulet: the plate is a shell, the lantern is a lamp,
   the magnet is a horseshoe, the heart is a heart. Colour is the second signal
   and never the first, for the reason the mining ladder makes the opposite
   trade -- there, one shape at four sizes was right because the four tools do
   the same job; here the eight charms do eight different jobs, and the shape is
   what has to say which. */

/* Mite shell. Segmented, and the segments are what stop it reading as a
   pebble. */
static const char* ART_ACC_CARAPACE[SPR_H] = {
    "..............",
    "....qqqqq.....",
    "...q11111q....",
    "..q1111111q...",
    "..q1kkkkk1q...",
    "..q1111111q...",
    "..q1kkkkk1q...",
    "..q1111111q...",
    "...q11111q....",
    "....qqqqq.....",
    "..............",
    "..............",
    "..............",
    "..............",
};

/* Moth lamp. The halo is the whole point: this is the one charm whose effect is
   literally light, so the icon emits some. */
static const char* ART_ACC_LANTERN[SPR_H] = {
    "..............",
    "......G.......",
    ".....GGG......",
    "....GGGGG.....",
    "...GfffffG....",
    "...GfTTTfG....",
    "...GfTTTfG....",
    "...GfffffG....",
    "....GGGGG.....",
    ".....GGG......",
    "....G...G.....",
    "...G.....G....",
    "..............",
    "..............",
};

/* Horseshoe magnet, poles down. The most literal icon in the set and
   deliberately so -- "things come to you" has one universally read shape. */
static const char* ART_ACC_MAGNET[SPR_H] = {
    "..............",
    "....pppp......",
    "...p....p.....",
    "..p......p....",
    "..p......p....",
    "..p......p....",
    "..p......p....",
    "..p......p....",
    "..p......p....",
    "..S......S....",
    "..S......S....",
    "..............",
    "..............",
    "..............",
};

/* Husk heart. Off-centre highlight so it beats rather than sits. */
static const char* ART_ACC_HEART[SPR_H] = {
    "..............",
    "...rr...rr....",
    "..roor.roor...",
    ".rooTorooooor.",
    ".rooTooooooor.",
    ".rooooooooor..",
    "..rooooooor...",
    "...rooooor....",
    "....rooor.....",
    ".....ror......",
    "......r.......",
    "..............",
    "..............",
    "..............",
};

/* Bat wing. Scalloped trailing edge, which is the one detail that separates a
   wing from a leaf at this size. */
static const char* ART_ACC_SWIFT[SPR_H] = {
    "..............",
    ".......y......",
    "......yzy.....",
    ".....yzzzy....",
    "....yzzzzzy...",
    "...yzzzzzzzy..",
    "..yzzzzzzzzzy.",
    "..yzzzzzzzzzy.",
    "..y.zzz.zzz.y.",
    "...y..y..y....",
    "......G.......",
    ".....GGG......",
    "..............",
    "..............",
};

/* Spitter barb, worn on the forearm. A straight spike bound by two straps:
   speed, not damage, and a strap says "worn" where a bare spike would say
   "ammunition". */
static const char* ART_ACC_BRACER[SPR_H] = {
    "..............",
    "......T.......",
    "......S.......",
    "......S.......",
    ".....SSS......",
    "....GGGGG.....",
    "...G.....G....",
    "...G.SSS.G....",
    "...G.....G....",
    "....GGGGG.....",
    ".....SSS......",
    "..............",
    "..............",
    "..............",
};

/* Whetstone. A block with a bright worked edge -- the edge is the stat. */
static const char* ART_ACC_WHETSTONE[SPR_H] = {
    "..............",
    "..............",
    "...SSSSSSSS...",
    "..STTTTTTTTS..",
    "..SGGGGGGGGS..",
    "..SGGGGGGGGS..",
    "..SGGGGGGGGS..",
    "..SSSSSSSSSS..",
    "..............",
    ".....S..S.....",
    "....S....S....",
    "..............",
    "..............",
    "..............",
};

/* Chronometer. A cased dial with one hand -- two hands are unreadable at this
   size and a clock only needs to say "time" once. */
static const char* ART_ACC_CHRONO[SPR_H] = {
    "..............",
    "......G.......",
    ".....GGG......",
    "....RRRRR.....",
    "...RBBBBBR....",
    "..RBBTBBBBR...",
    "..RBBTBBBBR...",
    "..RBBTTTBBR...",
    "..RBBBBBBBR...",
    "...RBBBBBR....",
    "....RRRRR.....",
    "..............",
    "..............",
    "..............",
};

/* The pedestal, as a carried item. A plinth with something bright standing on
   it: the loot is the icon, because a bare plinth is furniture and the thing
   this places is a display case. */
static const char* ART_PEDESTAL[SPR_H] = {
    "..............",
    "......T.......",
    ".....TfT......",
    "......T.......",
    "..............",
    "....GGGGG.....",
    ".....GGG......",
    ".....GGG......",
    ".....GGG......",
    ".....GGG......",
    "....GGGGG.....",
    "...GGGGGGG....",
    "..GGGGGGGGG...",
    "..............",
};

/* The fallback for named objects that do not yet merit a bespoke silhouette.
   It is deliberately a parcel/tag rather than a colour square, so every
   non-material item has an object-shaped icon while dedicated art can arrive
   incrementally without leaving UI regressions behind. */
static const char* ART_ITEM_GENERIC[SPR_H] = {
    "..............", "....BBBBBB....", "...BWWWWWWB...", "...BWGGGGWB...",
    "...BWGGGGWB...", "...BWGGGGWB...", "...BWGGGGWB...", "...BWGGGGWB...",
    "...BWGGGGWB...", "...BWGGGGWB...", "...BWWWWWWB...", "....BBBBBB....",
    "..............", "..............",
};

/* The starter weapon. Keeps the house diagonal every tool icon uses -- handle
   at the bottom left, working end at the top right -- because the hotbar reads
   as a set and one item lying the other way looks like a mistake.

   Everything else is deliberately the OPPOSITE of the multitools, and the FIRST
   attempt at that got it wrong in a way only a picture showed. That version was
   the Mk I silhouette in the Mk I colours, two rows shorter, on the theory that
   size would carry the difference. Rendered at true hotbar scale beside the
   real thing it was simply a slightly smaller Mk I -- at fourteen pixels a two
   row difference is nothing, and shape and colour are all there is.

   So the difference is VALUE. The multitools are light: pale steel on a cream
   handle, which is what a precision instrument looks like. This is dark: a fat
   rough-wood stock under a stubby dark barrel, which is what something you
   made yourself out of a plank looks like. Light versus dark survives being
   three pixels tall, where eleven rows versus thirteen does not. */
static const char* ART_BOLTER[SPR_H] = {
    "..............",
    "..............",
    "..............",
    "..........TT..",
    ".........GG...",
    "........GG....",
    ".......##.....",
    "......###.....",
    ".....####.....",
    "....####......",
    "....##........",
    "..............",
    "..............",
    "..............",
};

/* The workbench. Read as FURNITURE rather than as machinery: a plank top on
   legs with a shelf under it, and no casing, no rim, no glowing face -- the
   visual language every other device shares says "this does something when
   poked", and a bench does not. It is a place.

   Drawn wide and low so the silhouette says "surface you work on" at a glance,
   with a tool standing on the top to say which kind of surface. Its cells are
   MAT_STATION_BENCH, whose own colour this deliberately echoes. */
static const char* ART_BENCH[SPR_H] = {
    "..............",
    "..............",
    ".....SS.......",
    ".....SS.......",
    ".....GG.......",
    "..HHHHHHHHHH..",
    "..##########..",
    "..#........#..",
    "..#........#..",
    "..#..####..#..",
    "..#........#..",
    "..##......##..",
    "..............",
    "..............",
};

/* Low, wide furniture rather than another upright machine: pale flax bedding
   sits inside a rough wood frame, with the raised headboard on the left. */
static const char* ART_BED[SPR_H] = {
    "..............",
    ".##...........",
    ".##AAAAAAAAA..",
    ".##AAAAAAAAA..",
    ".##AAAAAAAAA..",
    ".##AAAAAAAAA..",
    ".##AAAAAAAAA..",
    ".##AAAAAAAAA..",
    ".##AAAAAAAAA..",
    ".############.",
    ".#..........#.",
    ".#..........#.",
    ".#..........#.",
    "..............",
};

/* The rest of the station ladder, drawn as FURNITURE like the bench above --
   no casing, no rim, no lit face, because the device visual language says "this
   does something when poked" and a station is a place you stand.

   What tells them apart is the SILHOUETTE plus one identity colour taken from
   the material each is made of, which is the same rule the mining tiers use.
   Trying to make four distinguishable "workshop furniture" shapes in fourteen
   pixels would produce four things you cannot tell apart; one recognisable
   profile each, in its own colour, is legible at hotbar size. */

/* Anvil: the horn-and-waist profile, which is the one piece of workshop kit
   with a silhouette everybody already knows. Grey, its own material colour. */
static const char* ART_ANVIL[SPR_H] = {
    "..............",
    "..............",
    "..............",
    "...SSSSSSSS...",
    "..SSSSSSSSSS..",
    "..SSSSSSSSS...",
    "....GGGGGG....",
    ".....GGGG.....",
    ".....GGGG.....",
    "....GGGGGG....",
    "...##########.",
    "...##########.",
    "..............",
    "..............",
};

/* Chemistry bench: a bench with glassware on it. The flask is the identity --
   a round-bottomed vessel is unmistakable even at three pixels. */
static const char* ART_CHEMSTN[SPR_H] = {
    "..............",
    "..............",
    ".....cc.......",
    ".....cc...cc..",
    "....cccc..cc..",
    "...cccccc.cc..",
    "...cccccccccc.",
    "..HHHHHHHHHHH.",
    "..###########.",
    "..#.........#.",
    "..#..#####..#.",
    "..##.......##.",
    "..............",
    "..............",
};

/* Assembly table: a bench with a part clamped on it and a gantry over the top.
   Brass, matching its material -- the one station that is precision equipment
   rather than a place to hit things. */
static const char* ART_ASSEMBLY[SPR_H] = {
    "..............",
    "..EEEEEEEEEE..",
    "..E........E..",
    "..E...EE...E..",
    "......EE......",
    "....JJJJJJ....",
    "...JJJJJJJJ...",
    "..HHHHHHHHHH..",
    "..##########..",
    "..#........#..",
    "..#........#..",
    "..##......##..",
    "..............",
    "..............",
};

/* Blast furnace: a squat stack with a fire door. The only station that is HOT,
   so it is the only one allowed the flame colours -- which is what makes it
   read as the end of the ladder at a glance. */
static const char* ART_FORGESTN[SPR_H] = {
    "..............",
    "....######....",
    "...########...",
    "..##########..",
    "..##########..",
    "..##.ffff.##..",
    "..##.feef.##..",
    "..##.ffff.##..",
    "..##########..",
    "..##########..",
    ".############.",
    ".############.",
    "..............",
    "..............",
};

void initSprites() {
    memset(g_sprite, 0, sizeof(g_sprite));
    expand(SPR_MITE,      ART_MITE);
    expand(SPR_HUSK,      ART_HUSK);
    expand(SPR_BAT,       ART_BAT);
    expand(SPR_SPITTER,   ART_SPITTER);
    expand(SPR_BROOD,     ART_BROOD);
    expand(SPR_MOTH,      ART_MOTH);
    expand(SPR_SLIME,     ART_SLIME);
    expand(SPR_TOOL1,     ART_TOOL1);
    expand(SPR_TOOL2,     ART_TOOL2);
    expand(SPR_MOD_SHOT,  ART_MOD_SHOT);
    expand(SPR_MOD_BLAST, ART_MOD_BLAST);
    expand(SPR_MINE1,     ART_MINE1);
    expand(SPR_MINE2,     ART_MINE2);
    expand(SPR_MINE3,     ART_MINE3);
    expand(SPR_MINE4,     ART_MINE4);
    expand(SPR_SEED,      ART_SEED);
    expand(SPR_FLINT,     ART_FLINT);
    expand(SPR_FLARE,     ART_FLARE);
    expand(SPR_ACC_GARLIC,   ART_ACC_GARLIC);
    expand(SPR_ACC_OVERLOAD, ART_ACC_OVERLOAD);
    expand(SPR_ACC_TWIN,     ART_ACC_TWIN);
    expand(SPR_ACC_CARAPACE,  ART_ACC_CARAPACE);
    expand(SPR_ACC_LANTERN,   ART_ACC_LANTERN);
    expand(SPR_ACC_MAGNET,    ART_ACC_MAGNET);
    expand(SPR_ACC_HEART,     ART_ACC_HEART);
    expand(SPR_ACC_SWIFT,     ART_ACC_SWIFT);
    expand(SPR_ACC_BRACER,    ART_ACC_BRACER);
    expand(SPR_ACC_WHETSTONE, ART_ACC_WHETSTONE);
    expand(SPR_ACC_CHRONO,    ART_ACC_CHRONO);
    expand(SPR_PEDESTAL,      ART_PEDESTAL);
    expand(SPR_ITEM_GENERIC, ART_ITEM_GENERIC);
    expand(SPR_BOLTER,    ART_BOLTER);
    expand(SPR_BENCH,     ART_BENCH);
    expand(SPR_BED,       ART_BED);
    expand(SPR_ANVIL,     ART_ANVIL);
    expand(SPR_CHEMSTN,   ART_CHEMSTN);
    expand(SPR_ASSEMBLY,  ART_ASSEMBLY);
    expand(SPR_FORGESTN,  ART_FORGESTN);
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
