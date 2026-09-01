#include "sprite.h"
#include "rig.h"
/* For ENT_DEFS: the egg shells are tinted from the creature table so the two
   cannot disagree. See the egg loop in initSprites(). */
#include "entity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

u32 g_sprite[SPR_COUNT][SPR_W * SPR_H];
u32 g_shamblerIdle[SHAMBLER_IDLE_FRAMES][SHAMBLER_SPR_W * SHAMBLER_SPR_H];
u32 g_shamblerWalk[SHAMBLER_WALK_FRAMES][SHAMBLER_SPR_W * SHAMBLER_SPR_H];
u32 g_shamblerJump[SHAMBLER_SPR_W * SHAMBLER_SPR_H];
u32 g_shamblerFall[SHAMBLER_SPR_W * SHAMBLER_SPR_H];
u32 g_thresherIdle[THRESHER_IDLE_FRAMES][THRESHER_SPR_W * THRESHER_SPR_H];
u32 g_thresherWalk[THRESHER_WALK_FRAMES][THRESHER_SPR_W * THRESHER_SPR_H];

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
    case '?': return 0x72E09A;     /* bounce-module ricochet path */
    case '@': return 0xD59CFF;     /* homing-module orbit/target symbol */
    case '^': return 0xFF72D8;     /* teleport-module spatial split */

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
    /* --- wax ------------------------------------------------------------
       These two were the LAST unused characters in this table. Everything
       else the hive and its bees are drawn with reuses a colour that was
       already here -- gold for the body, the husk's near-black for banding,
       the feather white for a wing. Anything added after this has to reuse
       as well, or the key table has to become per-sprite. */
    case '>': return 0xE8C25C;     /* wax and honey, lit */
    case '~': return 0xC79A38;     /* wax, shaded */
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

    /* --- layer 2 -------------------------------------------------------
       Built from the characters that were actually FREE. The first attempt
       reached for the obvious symbols -- '+', '<', '*' and friends -- and
       every one of them was already a colour: '<' is the dummy's joints, and
       the compiler caught it as a duplicate case. Only six characters were
       unused in the whole palette, so two of these deliberately REUSE an
       existing colour rather than inventing a near-duplicate of it: the
       Culverin's shade is the husk's dark green and the Stooper's body is the
       bat's near-black, both of which are already exactly the value wanted. */
    case '/': return 0x5E7A4E;     /* culverin: plate */
    case '_': return 0x86A86A;     /* culverin: lit plate */
    case '`': return 0xD8F0A0;     /* culverin: the muzzle, and its charge */
    case '|': return 0xC98BB8;     /* wisp: core */
    case '}': return 0x6B4570;     /* wisp: halo */
    case ',': return 0x2E3A2A;     /* stooper: membrane */
    /* Brood mother: her young are warm browns and she is RED, because a boss
       should be recognisable as itself from across a cavern before any of the
       detail resolves. */
    case '0': return 0xB04838;     /* brood: carapace */
    case '+': return 0x7A2A20;     /* brood: shade */
    case '=': return 0xE87A4A;     /* brood: lit ridge */

    /* --- the drones ------------------------------------------------------
       Every letter was already spoken for by the time these were drawn, which
       is why this last group reaches for punctuation. The names below are the
       contract, exactly as above: '$' is a shield field wherever it appears and
       nothing else, and the day it means two things is the day the set stops
       looking like a set.

       The four drones share the casing colours D/d/E with the instruments,
       deliberately -- they are the same kind of made object, and what tells
       them apart is what hangs underneath. */
    case '!': return 0xFFE9A8;     /* drone lamp, the glow it throws */
    case '$': return 0x7ACFFF;     /* shield field */
    case '%': return 0x3E7EA8;     /* shield field, its edge */
    /* Forge core: the only item in the game that is a piece of a boss, and it
       is lit from inside. Hotter than the flame colours the furnace uses,
       because it is the thing that makes a furnace possible. */
    case '&': return 0xC85A2A;     /* forge core, hot shell */
    case '*': return 0xFFD46A;     /* forge core, the light inside it */
    /* Brood call: bleached chitin, so it reads as a piece of the creature it
       summons rather than as a manufactured horn. */
    case '(': return 0xD8C89A;     /* chitin horn */
    case ')': return 0x8A7450;     /* chitin horn, its bore and shade */
    case '-': return 0xB8E8FF;     /* lens glass */
    case '[': return 0xC89A5A;     /* bread crust */
    case ']': return 0xE8D2A2;     /* bread crumb */
    case '{': return 0x2A2620;     /* egg speckle, dark on every shell */
    /* The crash dummy. ONE colour, in three values -- it was hazard yellow on
       black and read as a warning sign rather than as a body: the stripes were
       the loudest thing on screen and the silhouette was the thing you actually
       needed to see. Beige carries better against cave rock than either. */
    case ':': return 0xD6BE9A;     /* dummy: body */
    case ';': return 0xA8916C;     /* dummy: shade, where a limb needs an edge */
    case '<': return 0x6E5E46;     /* dummy: joints and eye slots */

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

static const char* ART_MOD_BOUNCE[SPR_H] = {
    "..............", ".RRRRRRRRRRRR.", ".RBBBBBBBBBBR.", ".RBB?BBBBBBBR.",
    ".RBBB??BBBBBR.", ".RBBBB?BBBBBR.", ".RBBBBB??BBBR.", ".RBBBBBB?BBBR.",
    ".RBBBBB?BBBBR.", ".RBBBB??BBBBR.", ".RBBBBBBBBBBR.", ".RRRRRRRRRRRR.",
    "..............", "..............",
};

static const char* ART_MOD_HOMING[SPR_H] = {
    "..............", ".RRRRRRRRRRRR.", ".RBBBBBBBBBBR.", ".RBBB@@@@BBBR.",
    ".RBB@BBBB@BBR.", ".RB@BB@@BB@BR.", ".RB@BB@@BB@BR.", ".RBB@BBBB@BBR.",
    ".RBBB@@@@BBBR.", ".RBBBB@BBBBBR.", ".RBBBBBBBBBBR.", ".RRRRRRRRRRRR.",
    "..............", "..............",
};

/* --- the warp wand ---------------------------------------------------------
   A rod held on the diagonal with the split-space colour burning at the tip.

   Deliberately built from the TELEPORT MODULE's palette rather than new
   entries: '^' is the module's spatial-split magenta and reusing it is what
   makes a player who has seen one recognise the other as the same effect at
   a different price. The shaft borrows the steel and the dark bit from the
   mining ladder for the same reason -- it should read as forged, because it
   is what iron buys. */
static const char* ART_WARP_WAND[SPR_H] = {
    "..............",
    "..........^...",
    ".........^^^..",
    "........^^^^^.",
    ".........^^^..",
    "..........^...",
    ".........2....",
    "........2.....",
    ".......2......",
    "......2.......",
    ".....k........",
    "....k.........",
    "...k..........",
    "..............",
};

static const char* ART_MOD_TELEPORT[SPR_H] = {
    "..............", ".RRRRRRRRRRRR.", ".RBBBBBBBBBBR.", ".RB^^BBBB^^BR.",
    ".RB^^^BB^^^BR.", ".RBB^^BB^^BBR.", ".RBBB^BB^BBBR.", ".RBBB^BB^BBBR.",
    ".RBB^^BB^^BBR.", ".RB^^^BB^^^BR.", ".RBBBBBBBBBBR.", ".RRRRRRRRRRRR.",
    "..............", "..............",
};

/* A bright, asymmetric electrical fork rather than a generic star. The bent
   main stroke reads as motion at inventory scale while the short side branch
   identifies it with the branching pulse seen travelling through wire. */
static const char* ART_SPARK[SPR_H] = {
    "..............",
    ".........ff...",
    "........fff...",
    ".......fe.....",
    "......fe......",
    "....ffffff....",
    ".....ffff.....",
    "......fe......",
    ".....fe.......",
    "....fee.......",
    "...ee.........",
    "..............",
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

/* expand(), with two palette letters overridden. 'M' becomes `metal` and 'm'
   becomes `shade`, so one piece of art serves a whole tier ladder.

   A separate function rather than a parameter on expand() with nulls at every
   other call site: forty existing sprites have nothing to say about a metal,
   and making all of them mention it to say "none" is how a table stops being
   readable. Shares expand()'s row-length abort by doing the same check -- art
   is edited by hand and a miscounted row is not hypothetical. */
static void expandMetal(int id, const char* const* art, u32 metal, u32 shade) {
    for (int y = 0; y < SPR_H; ++y) {
        if ((int)strlen(art[y]) != SPR_W) {
            fprintf(stderr, "sprite %d row %d is %d chars, expected %d\n",
                    id, y, (int)strlen(art[y]), SPR_W);
            abort();
        }
        for (int x = 0; x < SPR_W; ++x) {
            const char c = art[y][x];
            g_sprite[id][y * SPR_W + x] = c == 'M' ? metal
                                        : c == 'm' ? shade
                                        : paletteOf(c);
        }
    }
}

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

/* --- the Shambler: the first enemy on the armature -----------------------
   It uses the same humanoid builder and clips as the character, then changes
   ANATOMY rather than repainting a player: the torso pitches forward, arms
   lengthen, legs shorten, and the pack bone becomes the hump that owns its
   silhouette. Because these are rest proportions, every inherited pose keeps
   the hunch -- walk, idle and airborne -- without a second animation system.

   The palette is an explicit value ladder just like RIG_SUIT. Layer two is
   cold green; the eye is the only warm/high-value mark, so facing remains
   readable at the edge of the light rather than relying on a mirrored outline. */
static void buildShamblerFrames() {
    static const u32 SHADE[RIG_SHADES] = {
        0x3B463A,  /* far limb */
        0x596553,  /* torso */
        0x7A876B,  /* near limb */
        0xA0AA83,  /* head */
        0x293229,  /* far hand/foot */
        0x435043,  /* near hand/foot */
        0xE4D65D,  /* eye ridge */
        0x354137,  /* hump */
        0x6B3D47,  /* ichor-dark waist */
    };
    static Bone bone[RB_COUNT];
    RigDef rig;
    rigHumanoid(bone, &rig, "shambler", SHAMBLER_SPR_W, SHAMBLER_SPR_H, SHADE);

    /* Facing-right is positive X. Taking twenty degrees out of the spine's
       upward rest pitches its shoulders forward; the neck gives most of that
       angle back so the head still watches the corridor instead of the floor. */
    bone[RB_SPINE].rest -= 20;
    bone[RB_NECK].rest  += 15;
    bone[RB_HEAD].rest  += 5;

    /* Gorilla-like reach and a low centre of mass. Long arms are both the
       silhouette and the reason its inherited counter-swing looks heavy. */
    bone[RB_FAR_UPPER].len  = (i16)(bone[RB_FAR_UPPER].len  * 5 / 4);
    bone[RB_NEAR_UPPER].len = (i16)(bone[RB_NEAR_UPPER].len * 5 / 4);
    bone[RB_FAR_FORE].len   = (i16)(bone[RB_FAR_FORE].len   * 5 / 4);
    bone[RB_NEAR_FORE].len  = (i16)(bone[RB_NEAR_FORE].len  * 5 / 4);
    bone[RB_FAR_THIGH].len  = (i16)(bone[RB_FAR_THIGH].len  * 9 / 10);
    bone[RB_NEAR_THIGH].len = (i16)(bone[RB_NEAR_THIGH].len * 9 / 10);
    bone[RB_PACK].len       = (i16)(bone[RB_PACK].len * 6 / 5);
    bone[RB_PACK].wBase     = (u8)(bone[RB_PACK].wBase * 3 / 2);
    bone[RB_PACK].wTip      = (u8)(bone[RB_PACK].wTip  * 3 / 2);

    armBake(&rig, &RIG_IDLE, g_shamblerIdle[0]);
    armBake(&rig, &RIG_WALK, g_shamblerWalk[0]);
    armBake(&rig, &RIG_JUMP, g_shamblerJump);
    armBake(&rig, &RIG_FALL, g_shamblerFall);
}

/* The Thresher is the first creature whose skeleton is NOT the humanoid one --
   see rigTentacled. Nothing else here changes: the same armature, the same
   bake, the same flat u32 frames the hand-drawn sprites produce, so the
   renderer never learns that a second kind of creature exists.

   Its walk is generated rather than authored (rigTentacleWalk), which is the
   only way four limbs stay exactly a quarter cycle apart without somebody
   maintaining thirty-two angles a frame by hand. */
static void buildThresherFrames() {
    static Bone bone[TENT_BONES];
    RigDef rig;
    rigTentacled(bone, &rig, "thresher",
                 THRESHER_SPR_W, THRESHER_SPR_H, RIG_THRESHER);
    armBake(&rig, &RIG_TENT_WALK, g_thresherWalk[0]);
    armBake(&rig, &RIG_TENT_IDLE, g_thresherIdle[0]);
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
/* --- the drain, four ways ---------------------------------------------------
   Symmetric under a quarter turn: the same stub on every side, and nothing
   anywhere that says which way is forward. That is not decoration, it is the
   object telling the truth -- a drain takes from all four edges (see devDrain),
   so a sprite with an intake at the top and a taper at the bottom was
   advertising a facing it does not have and never really used. It also means
   devDraw no longer rotates this one; there is nothing to rotate. */
static const char* ART_DRAIN[SPR_H] = {
    ".....llll.....", ".....llll.....", "...DDDDDDDD...", "..DDEEEEEEDD..", "..DEFFFFFFED..",
    "llDEFFFFFFEDll", "llDEFFFFFFEDll", "llDEFFFFFFEDll", "llDEFFFFFFEDll", "..DEFFFFFFED..",
    "..DDEEEEEEDD..", "...DDDDDDDD...", ".....llll.....", ".....llll.....",
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

/* --- the Culverin, layer 2 -------------------------------------------------
   Squat and heavily plated, with the barrel riding high and forward so the
   thing that shoots you is the thing you see first. The spitter carries its
   sac on its back as a warning; this carries a muzzle, and the muzzle rises
   as the reload completes -- see entityPixelMotion, where that is the tell for
   an incoming volley. */
static const char* ART_CULVERIN[SPR_H] = {
    "..............",
    "..............",
    ".....___/.....",
    "....__///`....",
    "...__////``...",
    "..////////....",
    ".._///////_...",
    ".//x//////__..",
    ".//////////_..",
    ".r////////r...",
    "..r.r..r.r....",
    "..r.r..r.r....",
    "..............",
    "..............",
};

/* --- the Wisp, layer 2 -----------------------------------------------------
   A core inside a halo, and almost nothing else. It has no wings on purpose:
   the bat and the moth both beat, so a third flier that also beat would read
   as a third bat. This one DRIFTS, and a shape with no visible means of
   propulsion is what sells that. */
static const char* ART_WISP[SPR_H] = {
    "..............",
    ".....}}}......",
    "...}}|||}}....",
    "..}}|||||}}...",
    ".}}|||||||}}..",
    ".}||||x||||}..",
    ".}|||||||||}..",
    ".}}|||||||}}..",
    "..}}|||||}}...",
    "...}}|||}}....",
    ".....}}}......",
    "......}.......",
    "..............",
    "..............",
};

/* --- the Stooper, layer 2 --------------------------------------------------
   Wings held BACK rather than spread, because this one is not a flapper: it
   climbs, holds, and then falls on you. A swept silhouette reads as speed even
   while it is hovering, which is the warning that it is about to stop
   hovering. */
static const char* ART_STOOPER[SPR_H] = {
    "..............",
    ".,............",
    ".,,...........",
    ".,,,......,...",
    "..,,,....,,...",
    "..,,,,..,,,...",
    "...,,,,7,,,...",
    "....,,7x7,....",
    ".....7777.....",
    "......77......",
    "......7.......",
    "..............",
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
static const char* ART_BEE[SPR_H] = {
    "..............",
    "..............",
    ".....A..A.....",
    "....AAAAAA....",
    "....AAAAAA....",
    ".....j33j.....",
    "....j3333j....",
    "....3jjjj3....",
    "....j3333j....",
    ".....j33j.....",
    "......jj......",
    ".....j..j.....",
    "..............",
    "..............",
};

static const char* ART_COAL_BEE[SPR_H] = {
    "..............",
    "..............",
    ".....S..S.....",
    "....SSSSSS....",
    "....SSSSSS....",
    ".....jggj.....",
    "....jggggj....",
    "....gjjjjg....",
    "....jggggj....",
    ".....jggj.....",
    "......jj......",
    ".....j..j.....",
    "..............",
    "..............",
};

static const char* ART_HIVE[SPR_H] = {
    "..............",
    ".....>>>>.....",
    "...>>~~~~>>...",
    "..>>>>>>>>>>..",
    "..>~~~~~~~~>..",
    "..>>>>>>>>>>..",
    "..>~~~~~~~~>..",
    "..>>>>>>>>>>..",
    "..>~~~~~~~~>..",
    "..>>>>>>>>>>..",
    "..>~~~jj~~~>..",
    "..>>>>jj>>>>..",
    "...>>>>>>>>...",
    "..............",
};

static const char* ART_HONEY_POTION[SPR_H] = {
    "..............",
    "......GG......",
    "......GG......",
    ".....GGGG.....",
    "....G>>>>G....",
    "...G>>>>>>G...",
    "..G>>>>>>>>G..",
    "..G>>>>>>>>G..",
    "..G>>>>>>>>G..",
    "..G>>>>>>>>G..",
    "..G>>>>>>>>G..",
    "...G>>>>>>G...",
    "....GGGGGG....",
    "..............",
};

static const char* ART_FLOWER_ITEM[SPR_H] = {
    "..............",
    "..............",
    "..............",
    "......@@......",
    ".....@@@@.....",
    "....@@>>@@....",
    "....@@>>@@....",
    ".....@@@@.....",
    "......pp......",
    "......pp......",
    "....pppppp....",
    "......pp......",
    "......pp......",
    "..............",
};

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

/* --- the melee ladder -------------------------------------------------------
   Two pieces of art for fourteen items. 'M' is the tier metal and 'm' its
   shade; expandMetal() substitutes both, so the palette table below is not
   asked to hold seven near-identical pairs of greys and yellows that mean
   nothing on their own.

   Both point up and to the right at 45 degrees, which is the angle that gets
   the most length out of a square canvas -- a vertical blade in fourteen pixels
   is fourteen pixels long, a diagonal one is nearly twenty. */

/* Sword. A broad tapering blade with a crossguard, and the crossguard is the
   whole silhouette: without it this is a spear, and with it nobody has to be
   told which is which. */
static const char* ART_SWORD[SPR_H] = {
    "..........MM..",
    ".........MMM..",
    "........MMMm..",
    ".......MMMm...",
    "......MMMm....",
    ".....MMMm.....",
    "....MMMm......",
    "...MMMm.......",
    "..GGMm.GG.....",
    "...GGGGG......",
    "..GGGHm.......",
    ".GGHH.........",
    "..HH..........",
    "..............",
};

/* Spear. A long shaft with a leaf head and no guard at all, so the two read
   apart at a glance even in a dark cave: one is wide at the bottom, the other
   is a line. */
static const char* ART_SPEAR[SPR_H] = {
    "...........M..",
    "..........MMM.",
    "..........MMM.",
    ".........MMm..",
    "........Mmm...",
    ".......Gm.....",
    "......GH......",
    ".....HH.......",
    "....HH........",
    "...HH.........",
    "..HH..........",
    ".HH...........",
    "HH............",
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

/* --- the drones ------------------------------------------------------------

   A shared chassis and four payloads. The chassis is rows 1-7 in every one of
   them, identical to the character: two rotors on stalks over a boxed hull.
   That repetition is the point -- it is what makes four different icons read
   as one family before any of the detail resolves -- and it is also why the
   payload gets the whole bottom half rather than a corner.

   Casing colours are D/d/E, the same three the thermometer and the circuit
   parts use. These are manufactured objects and they should look it, next to
   charms that are pieces of animal. */

/* Light: a lamp under the hull, throwing a halo. The one drone whose effect is
   literally light, so the icon emits some -- the same decision the moth
   lantern charm makes, and they should agree. */
static const char* ART_DRONE_LIGHT[SPR_H] = {
    "..............",
    "..EE......EE..",
    "...E......E...",
    "..DDDDDDDDDD..",
    "..DdEEEEEEdD..",
    "..DdEEEEEEdD..",
    "..dddddddddd..",
    "...DDDDDDDD...",
    "....ffffff....",
    ".....ffff.....",
    "....!....!....",
    "...!......!...",
    "..............",
    "..............",
};

/* Attack: twin barrels with the heat still in them. Two rather than one,
   because a single centred barrel at this size is a dot and reads as another
   lamp. */
static const char* ART_DRONE_ATTACK[SPR_H] = {
    "..............",
    "..EE......EE..",
    "...E......E...",
    "..DDDDDDDDDD..",
    "..DdEEEEEEdD..",
    "..DdEEEEEEdD..",
    "..dddddddddd..",
    "...DDDDDDDD...",
    "...SS..SS.....",
    "...SS..SS.....",
    "...ee..ee.....",
    "....f..f......",
    "..............",
    "..............",
};

/* Pickup: an open claw. Open rather than closed, so it reads as reaching for
   something instead of holding one -- the drone's whole job is the reaching. */
static const char* ART_DRONE_PICKUP[SPR_H] = {
    "..............",
    "..EE......EE..",
    "...E......E...",
    "..DDDDDDDDDD..",
    "..DdEEEEEEdD..",
    "..DdEEEEEEdD..",
    "..dddddddddd..",
    "...DDDDDDDD...",
    "...SS....SS...",
    "..SS......SS..",
    "..SS......SS..",
    "...SS....SS...",
    "....SSSSSS....",
    "..............",
};

/* Shield: a field domed BELOW the hull rather than ringed around it. A ring
   would collide with the chassis and turn the whole icon into a blue blob;
   hung underneath, the chassis stays legible and the dome still says the drone
   is projecting something. */
static const char* ART_DRONE_SHIELD[SPR_H] = {
    "..............",
    "..EE......EE..",
    "...E......E...",
    "..DDDDDDDDDD..",
    "..DdEEEEEEdD..",
    "..DdEEEEEEdD..",
    "..dddddddddd..",
    "...DDDDDDDD...",
    "..%%%%%%%%%%..",
    ".%$$$$$$$$$$%.",
    ".%..........%.",
    "..%........%..",
    "...%%....%%...",
    "..............",
};

/* --- the armour ladder -----------------------------------------------------
   M and m are substituted per tier by expandMetal; everything else is fixed.
   The visor uses the same V/v the character's own helmet does, so a suit in
   the pack and a suit on the body are recognisably the same object. */

static const char* ART_ARMOUR_HELM[SPR_H] = {
    "..............",
    "....MMMMMM....",
    "...MMMMMMMM...",
    "..MMMMMMMMMM..",
    "..MMVVVVVVMM..",
    "..MmVvVVVVmM..",
    "..MmVVVVVVmM..",
    "..MmmmmmmmmM..",
    "...MmmmmmmM...",
    "....MMMMMM....",
    "..............",
    "..............",
    "..............",
    "..............",
};

/* The orange band is the life-support trim the character's own suit carries.
   It is the only warm thing on either piece, which is what stops two grey
   rectangles at two greys from being indistinguishable. */
static const char* ART_ARMOUR_SUIT[SPR_H] = {
    "..............",
    "...MM....MM...",
    "..MMMM..MMMM..",
    "..MMMMMMMMMM..",
    "..MmMMMMMMmM..",
    "..MmMOOOOMmM..",
    "..MmMMMMMMmM..",
    "...MMMMMMMM...",
    "...MmMMMMmM...",
    "....MMMMMM....",
    "..............",
    "..............",
    "..............",
    "..............",
};

/* Drone-set legs keep the split silhouette readable at inventory scale. The
   cyan control strips repeat the visor colour and distinguish them from two
   ordinary metal boots. */
static const char* ART_ARMOUR_GREAVES[SPR_H] = {
    "..............",
    "...MMM.MMM....",
    "...MVM.MVM....",
    "...MVM.MVM....",
    "...MmM.MmM....",
    "...MmM.MmM....",
    "...MmM.MmM....",
    "..MMMM.MMMM...",
    "..Mmmm.Mmmm...",
    ".MMMMM..MMMMM.",
    ".Mmmmm..Mmmmm.",
    "..............",
    "..............",
    "..............",
};

/* A small transmitting puck: antenna and cyan signal arcs above a dark metal
   housing. Negative space between the arcs keeps it distinct from a lamp. */
static const char* ART_DRONE_BEACON[SPR_H] = {
    "......l.......",
    "...l..l..l....",
    "....l.l.l.....",
    ".....lll......",
    "......l.......",
    "...DDDDDDDD...",
    "..DEEEEEEEEd..",
    "..DE..ll..Ed..",
    "..DE..ll..Ed..",
    "..DEEEEEEEEd..",
    "...dddddddd...",
    "....D....D....",
    "..............",
    "..............",
};

/* --- the egg ---------------------------------------------------------------
   Narrow at the top, heavy at the bottom, with dark mottling that is the same
   on every shell. The speckle is what stops a tinted oval reading as a gem:
   eggs are the one item here that hatch into something, and the icon should
   look organic rather than cut. */
static const char* ART_EGG[SPR_H] = {
    "..............",
    ".....MMMM.....",
    "....MMMMMM....",
    "...MMMMMMMM...",
    "..MMMM{MMMMM..",
    "..MMMMMMMMMM..",
    "..MM{MMMM{MM..",
    "..MMMMMMMMMM..",
    "..MmMMMMMMmM..",
    "...mMMMMMMm...",
    "....mmmmmm....",
    "..............",
    "..............",
    "..............",
};

/* --- the one-offs ----------------------------------------------------------*/

/* Forge core. A faceted stone lit from inside, and the brightest thing in the
   item set on purpose: it drops off the layer's boss and it is what a forge is
   built around. */
static const char* ART_FORGE_CORE[SPR_H] = {
    "..............",
    "......&&......",
    "....&&&&&&....",
    "...&&****&&...",
    "..&&******&&..",
    "..&***TT***&..",
    "..&***TT***&..",
    "..&&******&&..",
    "...&&****&&...",
    "....&&&&&&....",
    "......&&......",
    "..............",
    "..............",
    "..............",
};

/* Brood call. Two prongs on a chitin band, cut from her own mandibles.

   This one took three attempts and the failures are worth recording, because
   they were all the same mistake. A tapered horn came out as a wooden spoon; a
   bell with straight sides came out as a cone; a bell with concave sides and a
   crown came out as a fir tree. At fourteen pixels a solid mass shaded light
   on one side and dark on the other reads as a LIT CONE whatever outline you
   give it -- the silhouette loses to the shading every time.

   Two prongs with a gap between them cannot do that: the negative space is the
   shape, and negative space survives being small. It is also the only icon in
   the set with a hole in the middle of it, which is worth something in a row
   of amulets. */
static const char* ART_BROOD_CALL[SPR_H] = {
    "..............",
    "..((((((((((..",
    "..))))))))))..",
    "..((......((..",
    "..((......((..",
    "..((......((..",
    "..((......((..",
    "..))......))..",
    "..))......))..",
    "...)......)...",
    "...)......)...",
    "....)....)....",
    "..............",
    "..............",
};

/* Focusing lens. A rimmed disc with a bright centre, which is the one shape
   that says "optics" at fourteen pixels without any glint trickery. */
static const char* ART_LENS[SPR_H] = {
    "..............",
    ".....GGGG.....",
    "...GG----GG...",
    "..G--------G..",
    ".G---TTTT---G.",
    "G---TTTTTT---G",
    "G---TTTTTT---G",
    ".G---TTTT---G.",
    "..G--------G..",
    "...GG----GG...",
    ".....GGGG.....",
    "..............",
    "..............",
    "..............",
};

/* Field relay. A cased box with an aerial and a pair of terminals, using the
   same 'l' the circuit parts use for "where a spark goes in or out" -- which
   is exactly what a relay is for. */
static const char* ART_RELAY[SPR_H] = {
    "......S.......",
    "....S.S.S.....",
    ".....S.S......",
    "......S.......",
    "..DDDDDDDDDD..",
    "..DEEEEEEEEd..",
    "..DE.llll.Ed..",
    "..DE.llll.Ed..",
    "..DEEEEEEEEd..",
    "..dddddddddd..",
    "...D......D...",
    "..............",
    "..............",
    "..............",
};

/* Bread. Crust outside, crumb inside, and slashed across the top -- the one
   food item in the game, and it should be obvious it is food rather than
   another brown component. */
static const char* ART_BREAD[SPR_H] = {
    "..............",
    "....[[[[[[....",
    "..[[]][]]][[..",
    ".[]]][]]]][]].",
    ".[]][]]]][]][.",
    ".[]]]][]]]][].",
    ".[[]]]]]]]][[.",
    "..[[[[[[[[[[..",
    "..............",
    "..............",
    "..............",
    "..............",
    "..............",
    "..............",
};

/* --- the crash dummy -------------------------------------------------------
   One beige, in three values, with ARMS -- and the arms are the reason this
   file's usual "just draw it" does not apply.

   entDraw squeezes fourteen columns into eleven for a body this shape, and the
   squeeze is nearest-neighbour: it samples source columns 0,1,2,3,5,6,7,8,10,
   11,12 and NEVER SAMPLES 4, 9 or 13. A gap drawn in one of those three
   vanishes completely, which is exactly how the first version ended up a
   solid slab with no arms in it -- the arms were there, the gap beside them
   was not.

   So the layout is placed on columns that survive, and the budget is spent on
   the TORSO rather than split evenly: arms on 0-1 and 11-12, gaps on 2 and 10,
   torso on 3-8. On screen that lands as two pixels of arm, one of gap, five of
   torso, one of gap, two of arm. An earlier version gave the arms three
   columns each and the torso four, and the result was a totem pole -- limbs
   as thick as the body read as a stack of blocks rather than as a figure.

   The legs split at columns 6-7, both of which are sampled, so the gap between
   them is two pixels wide and actually visible.

   Row 5 is deliberately SOLID all the way across. With the gap running the
   full height of the arm the limbs read as two rectangles floating beside the
   body rather than as arms; one connected shoulder line is all it takes to
   attach them, and it doubles as the widest part of the silhouette.

   Drawn short and wide for the same reason as before: fourteen rows are
   stretched over thirty, so a figure at natural proportions arrives twice as
   tall as it should. */
static const char* ART_DUMMY[SPR_H] = {
    "...::::::.....",
    "...::::::.....",
    "...::<:<:.....",
    "...::::::.....",
    ".....::.......",
    "::::::::::::..",
    "::.::::::..::.",
    ";;.::::::..;;.",
    "::.::::::..::.",
    "...::::::.....",
    "...::::::.....",
    "...::::::::...",
    "...:::..:::...",
    "...;;;..;;;...",
};

/* Ichor is carried tissue, not a placeable world material, so it needs an icon
   rather than a material swatch. A broad torn mass tapering into one heavy drip
   reads differently from the round egg shell beside it in the creative list. */
static const char* ART_ICHOR[SPR_H] = {
    "......MM......",
    ".....MMMM.....",
    "....MMMMMM....",
    "...MMMMMMMM...",
    "..MMMMMMMMMM..",
    "..MMMMMMMMMM..",
    "...MMMMMMMM...",
    "...MMmMMmMM...",
    "....MmmmmM....",
    "....MMMMMM....",
    ".....MMMM.....",
    ".....MMMM.....",
    "......MM......",
    "..............",
};

void initSprites() {
    memset(g_sprite, 0, sizeof(g_sprite));
    expand(SPR_MITE,      ART_MITE);
    expand(SPR_HUSK,      ART_HUSK);
    expand(SPR_BAT,       ART_BAT);
    expand(SPR_CULVERIN,  ART_CULVERIN);
    expand(SPR_WISP,      ART_WISP);
    expand(SPR_STOOPER,   ART_STOOPER);
    expand(SPR_SPITTER,   ART_SPITTER);
    expand(SPR_BROOD,     ART_BROOD);
    expand(SPR_DUMMY,     ART_DUMMY);
    expandMetal(SPR_ICHOR, ART_ICHOR, 0x8F4358, 0x572A3A);
    expand(SPR_MOTH,      ART_MOTH);
    expand(SPR_SLIME,     ART_SLIME);
    expand(SPR_TOOL1,     ART_TOOL1);
    expand(SPR_TOOL2,     ART_TOOL2);
    expand(SPR_MOD_SHOT,  ART_MOD_SHOT);
    expand(SPR_MOD_BLAST, ART_MOD_BLAST);
    expand(SPR_MOD_BOUNCE, ART_MOD_BOUNCE);
    expand(SPR_MOD_HOMING, ART_MOD_HOMING);
    expand(SPR_MOD_TELEPORT, ART_MOD_TELEPORT);
    expand(SPR_WARP_WAND, ART_WARP_WAND);
    expand(SPR_SPARK, ART_SPARK);
    expandMetal(SPR_ARMOUR_DRONE_VISOR,   ART_ARMOUR_HELM,    0x6FAFBE, 0x3D6C78);
    expandMetal(SPR_ARMOUR_DRONE_HARNESS, ART_ARMOUR_SUIT,    0x6FAFBE, 0x3D6C78);
    expandMetal(SPR_ARMOUR_DRONE_GREAVES, ART_ARMOUR_GREAVES, 0x6FAFBE, 0x3D6C78);
    /* The three progression families use one readable armour grammar, with a
       full-palette change rather than a tiny trim pixel: iron is neutral,
       Ranger bronze-green, and Vanguard ichor-red. */
    expandMetal(SPR_ARMOUR_IRON_HELM,       ART_ARMOUR_HELM,    0xA8ADB6, 0x555B64);
    expandMetal(SPR_ARMOUR_IRON_CUIRASS,    ART_ARMOUR_SUIT,    0xA8ADB6, 0x555B64);
    expandMetal(SPR_ARMOUR_IRON_GREAVES,    ART_ARMOUR_GREAVES, 0xA8ADB6, 0x555B64);
    expandMetal(SPR_ARMOUR_RANGER_VISOR,    ART_ARMOUR_HELM,    0x9DA76A, 0x4D633F);
    expandMetal(SPR_ARMOUR_RANGER_COAT,     ART_ARMOUR_SUIT,    0x9DA76A, 0x4D633F);
    expandMetal(SPR_ARMOUR_RANGER_GREAVES,  ART_ARMOUR_GREAVES, 0x9DA76A, 0x4D633F);
    expandMetal(SPR_ARMOUR_VANGUARD_HELM,   ART_ARMOUR_HELM,    0xA85A65, 0x59313B);
    expandMetal(SPR_ARMOUR_VANGUARD_PLATE,  ART_ARMOUR_SUIT,    0xA85A65, 0x59313B);
    expandMetal(SPR_ARMOUR_VANGUARD_GREAVES,ART_ARMOUR_GREAVES, 0xA85A65, 0x59313B);
    expand(SPR_ACC_DRONE_BEACON, ART_DRONE_BEACON);
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

    /* --- the melee ladder ------------------------------------------------
       Colours taken from the metals themselves rather than invented here. A
       bronze sword that is not bronze-coloured is a small lie the player has to
       learn, and there is no reason to tell it: these are the same values the
       world draws those materials with.

       The shade is a darkened copy rather than a second hand-picked colour, so
       adding an eighth tier is one row here and not a fresh pair of greys. */
    {
        struct MetalSpr { int sword, spear; u32 col; };
        static const MetalSpr METAL[] = {
            { SPR_SWORD_COPPER,   SPR_SPEAR_COPPER,   0xC87A32 },
            { SPR_SWORD_BRONZE,   SPR_SPEAR_BRONZE,   0xCE9B4E },
            { SPR_SWORD_IRON,     SPR_SPEAR_IRON,     0xA8ADB6 },
            { SPR_SWORD_GOLD,     SPR_SPEAR_GOLD,     0xE8C233 },
            { SPR_SWORD_STEEL,    SPR_SPEAR_STEEL,    0x8E97A6 },
            { SPR_SWORD_TITANIUM, SPR_SPEAR_TITANIUM, 0xD2DAE4 },
            { SPR_SWORD_TUNGSTEN, SPR_SPEAR_TUNGSTEN, 0x6F7A86 },
        };
        for (int i = 0; i < (int)(sizeof(METAL) / sizeof(METAL[0])); ++i) {
            const u32 c = METAL[i].col;
            const u32 shade = ((c >> 1) & 0x7F7F7Fu);
            expandMetal(METAL[i].sword, ART_SWORD, c, shade);
            expandMetal(METAL[i].spear, ART_SPEAR, c, shade);
        }
    }
    expand(SPR_DRONE_LIGHT,  ART_DRONE_LIGHT);
    expand(SPR_DRONE_ATTACK, ART_DRONE_ATTACK);
    expand(SPR_DRONE_PICKUP, ART_DRONE_PICKUP);
    expand(SPR_DRONE_SHIELD, ART_DRONE_SHIELD);
    expand(SPR_FORGE_CORE,   ART_FORGE_CORE);
    expand(SPR_BROOD_CALL,   ART_BROOD_CALL);
    expand(SPR_LENS,         ART_LENS);
    expand(SPR_RELAY,        ART_RELAY);
    expand(SPR_BREAD,        ART_BREAD);

    /* --- the armour ladder ------------------------------------------------
       The same two-shapes-many-tints arrangement as the swords above, and the
       colours are the ones item.cpp already gives these pieces rather than a
       second opinion about what steel looks like. */
    {
        struct ArmourSpr { int helm, suit; u32 col; };
        static const ArmourSpr ARMOUR[] = {
            { SPR_ARMOUR_HELM_STEEL,    SPR_ARMOUR_SUIT_STEEL,    0x9CA0A6 },
            { SPR_ARMOUR_HELM_TITANIUM, SPR_ARMOUR_SUIT_TITANIUM, 0xC8CCD2 },
        };
        for (int i = 0; i < (int)(sizeof(ARMOUR) / sizeof(ARMOUR[0])); ++i) {
            const u32 c = ARMOUR[i].col;
            const u32 shade = ((c >> 1) & 0x7F7F7Fu);
            expandMetal(ARMOUR[i].helm, ART_ARMOUR_HELM, c, shade);
            expandMetal(ARMOUR[i].suit, ART_ARMOUR_SUIT, c, shade);
        }
    }

    /* --- the eggs ---------------------------------------------------------
       One shell, tinted per creature, and the tint is read from ENT_DEFS
       rather than listed here. entity.h is explicit about why the colour lives
       on the creature -- "one table describing a creature and not two that can
       disagree about what colour it is" -- and an egg that is not the colour
       of the thing inside it is exactly that disagreement.

       So this loop mirrors the one in item.cpp that builds the egg items, and
       a creature added tomorrow gets a shell in its own colour with no edit
       here either. */
    if (ENT_COUNT - 1 > SPR_EGG_LAST - SPR_EGG_FIRST + 1) {
        fprintf(stderr, "%d creatures but only %d egg sprite slots -- widen "
                        "SPR_EGG_LAST\n", ENT_COUNT - 1,
                SPR_EGG_LAST - SPR_EGG_FIRST + 1);
        abort();
    }
    for (int t = 1; t < ENT_COUNT; ++t) {
        const u32 c = ENT_DEFS[t].eggColour;
        const u32 shade = ((c >> 1) & 0x7F7F7Fu);
        expandMetal(SPR_EGG_FIRST + (t - 1), ART_EGG, c, shade);
    }

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
    expand(SPR_BEE,           ART_BEE);
    expand(SPR_COAL_BEE,      ART_COAL_BEE);
    expand(SPR_HIVE,          ART_HIVE);
    expand(SPR_HONEY_POTION,  ART_HONEY_POTION);
    expand(SPR_FLOWER_ITEM,   ART_FLOWER_ITEM);
    for (int digit = 1; digit <= 9; ++digit) makeSignalSprite(SPR_SIGNAL1 + digit - 1, digit);

    buildPlayerFrames();
    buildShamblerFrames();
    buildThresherFrames();
}
