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

/* Inventory presentation has more room than a world object. Keep the 14x14
   simulation-facing master above (devices and creatures depend on it), then
   resample it onto this 21x21 canvas for UI use. 21 is exactly 150% of 14. */
static const int INV_SPR_W = 21;
static const int INV_SPR_H = 21;

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
    /* Layer 2. Appended after the layer-1 creatures so every established
       sprite id keeps its meaning. */
    SPR_CULVERIN,
    SPR_WISP,
    SPR_STOOPER,
    /* The boss. Drawn from the same 14x14 canvas as everything else and then
       SCALED UP by its collision box -- see entDraw -- rather than given a
       larger grid of its own. Fourteen pixels of shape blown up to 34 cells
       reads as a big creature, and a second canvas size would mean a second set
       of art conventions for one sprite. */
    SPR_BROOD,
    SPR_DUMMY,
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
    /* --- the melee ladder -------------------------------------------------
       Two silhouettes at seven colours, and NOT fourteen drawings. That is the
       same trade the mining tiers already make and it is right for the same
       reason: shape carries "which weapon is this", colour carries "which
       tier", and fourteen bespoke shapes in fourteen pixels would produce
       fourteen things you cannot tell apart at all. It also means the metal a
       blade is made of looks like that metal everywhere it appears, because the
       colour comes from the material table rather than from a guess.

       Laid out sword-then-spear per tier so the ids run in acquisition order,
       matching the ItemId block. */
    SPR_SWORD_COPPER,   SPR_SPEAR_COPPER,
    SPR_SWORD_BRONZE,   SPR_SPEAR_BRONZE,
    SPR_SWORD_IRON,     SPR_SPEAR_IRON,
    SPR_SWORD_GOLD,     SPR_SPEAR_GOLD,
    SPR_SWORD_STEEL,    SPR_SPEAR_STEEL,
    SPR_SWORD_TITANIUM, SPR_SPEAR_TITANIUM,
    SPR_SWORD_TUNGSTEN, SPR_SPEAR_TUNGSTEN,

    /* --- the drones -------------------------------------------------------
       Four silhouettes, not one recoloured four times, and that is the CHARM
       trade rather than the sword trade. The seven swords do the same job at
       seven strengths, so shape carries the weapon and colour carries the
       tier. These four do four different jobs -- light, damage, collection,
       protection -- so shape has to carry the job or the row is unreadable.

       They still share a chassis, because they are all drones and a player who
       cannot tell at a glance that these four belong together has lost a real
       piece of information. The chassis says "drone"; what hangs underneath
       says which. */
    SPR_DRONE_LIGHT,
    SPR_DRONE_ATTACK,
    SPR_DRONE_PICKUP,
    SPR_DRONE_SHIELD,

    /* --- the armour ladder ------------------------------------------------
       Two silhouettes at two tiers, the same trade the swords make and for the
       same reason: a helmet and a suit are different things you wear at once,
       and steel and titanium are the same thing twice. Tinted from the item
       colours rather than from fresh greys, so the metal looks like that metal
       everywhere it appears. */
    SPR_ARMOUR_HELM_STEEL,    SPR_ARMOUR_SUIT_STEEL,
    SPR_ARMOUR_HELM_TITANIUM, SPR_ARMOUR_SUIT_TITANIUM,

    /* --- the eggs ---------------------------------------------------------
       One shell at every creature's colour, which is the SWORD trade again and
       right for the same reason inverted: the eggs all do exactly one job, and
       the only thing that varies is what comes out. So shape carries the job
       and colour carries the answer.

       A RESERVED BLOCK rather than one id per creature, because the egg ITEMS
       are already generated straight off ENT_DEFS -- "a creature added tomorrow
       gets an egg with no edit here at all" -- and a hand-written list of seven
       sprite ids would quietly break that the first time an eighth creature
       turned up. The colour is read from ENT_DEFS too, for the reason stated
       there: one table describes a creature, never two that can disagree.

       Sixteen is slack, not a plan. initSprites() checks it against ENT_COUNT
       rather than trusting it. */
    SPR_EGG_FIRST,
    SPR_EGG_LAST = SPR_EGG_FIRST + 15,

    /* The one-offs: two boss items, two components, and lunch. */
    SPR_FORGE_CORE,
    SPR_BROOD_CALL,
    SPR_LENS,
    SPR_RELAY,
    SPR_BREAD,

    /* Appended with their ItemIds so established sprite numbers remain save-
       and UI-stable. All share the rimmed module-chip silhouette. */
    SPR_MOD_BOUNCE,
    SPR_MOD_HOMING,
    SPR_MOD_TELEPORT,
    SPR_ARMOUR_DRONE_VISOR,
    SPR_ARMOUR_DRONE_HARNESS,
    SPR_ARMOUR_DRONE_GREAVES,
    SPR_ACC_DRONE_BEACON,
    /* Save-stable item art appended with the items that use it. The Shambler
       itself is larger than the shared 14x14 creature canvas and therefore
       has dedicated rig buffers below rather than a SpriteId. */
    SPR_ICHOR,
    SPR_ARMOUR_IRON_HELM,
    SPR_ARMOUR_IRON_CUIRASS,
    SPR_ARMOUR_IRON_GREAVES,
    SPR_ARMOUR_RANGER_VISOR,
    SPR_ARMOUR_RANGER_COAT,
    SPR_ARMOUR_RANGER_GREAVES,
    SPR_ARMOUR_VANGUARD_HELM,
    SPR_ARMOUR_VANGUARD_PLATE,
    SPR_ARMOUR_VANGUARD_GREAVES,
    SPR_WARP_WAND,
    SPR_SPARK,
    /* The hive and its occupants. */
    SPR_BEE,
    SPR_COAL_BEE,
    SPR_HIVE,
    SPR_HONEY_POTION,
    SPR_FLOWER_ITEM,
    SPR_HEAT_LAMP,

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

/* --- the Shambler ---------------------------------------------------------
   A genuinely rigged enemy, baked at its collision size rather than painted
   into the 14x14 creature sheet and enlarged. These are ordinary pixel buffers
   after startup; the entity renderer pays only a frame lookup. */
static const int SHAMBLER_SPR_W = 22;
static const int SHAMBLER_SPR_H = 36;
static const int SHAMBLER_IDLE_FRAMES = 2;
static const int SHAMBLER_WALK_FRAMES = 8;
extern u32 g_shamblerIdle[SHAMBLER_IDLE_FRAMES][SHAMBLER_SPR_W * SHAMBLER_SPR_H];
extern u32 g_shamblerWalk[SHAMBLER_WALK_FRAMES][SHAMBLER_SPR_W * SHAMBLER_SPR_H];
extern u32 g_shamblerJump[SHAMBLER_SPR_W * SHAMBLER_SPR_H];
extern u32 g_shamblerFall[SHAMBLER_SPR_W * SHAMBLER_SPR_H];

/* --- the Thresher, layer 2 -------------------------------------------------
   Wider than it is tall, because four splayed tentacles are the silhouette and
   the body is a bulb sitting on top of them. The box is the sprite, as with the
   Shambler, so the width is not decoration: those limbs are what touches you.

   Eight walk frames rather than the two a hand-drawn creature can support,
   because the gait is a travelling wave and two samples of a wave is a
   flicker -- see rigTentacleWalk. */
static const int THRESHER_SPR_W = 28;
static const int THRESHER_SPR_H = 26;
static const int THRESHER_IDLE_FRAMES = 2;
static const int THRESHER_WALK_FRAMES = 8;
extern u32 g_thresherIdle[THRESHER_IDLE_FRAMES][THRESHER_SPR_W * THRESHER_SPR_H];
extern u32 g_thresherWalk[THRESHER_WALK_FRAMES][THRESHER_SPR_W * THRESHER_SPR_H];

void initSprites();
