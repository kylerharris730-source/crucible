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

   Material inventory art is generated separately by material_icon.cpp at
   21x21. It describes material form without changing world LUT rendering. */

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
    SPR_SKIRMISHER,
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
    /* Widened from 16 to 24, and then to 32. Twice now the check in
       initSprites has been the thing that noticed -- the Skirmisher took the
       roster to exactly sixteen, and the Effigy's three took it to twenty-six
       past a ceiling of twenty-four. It aborts with the count and the name of
       this constant rather than overflowing, which is why widening it is a
       one-line chore instead of a debugging session. */
    SPR_EGG_LAST = SPR_EGG_FIRST + 31,

    /* The one-offs: two boss items, two components, and lunch. */
    SPR_FORGE_CORE,
    SPR_BROOD_CALL,
    /* The Widow's pair, appended for the same reason everything is. */
    SPR_WIDOW_CALL,
    SPR_SILK_GLAND,
    /* Mk III and its modifiers, appended for the same reason as everything
       else here. The seven chips share the module silhouette with a DARK rim
       -- see the note on their art -- so the socket panel says "changes a
       shot" before you have read a symbol. */
    SPR_TOOL3,
    SPR_MOD_DOUBLE,
    SPR_MOD_TRAIL,
    SPR_MOD_ARC_L,
    SPR_MOD_ARC_F,
    SPR_MOD_SEEK,
    SPR_MOD_SEEKM,
    SPR_MOD_QUICKEN,

    /* Layer 3's roster and its drop. Hand-drawn at 14x14 like every layer 2
       creature that is not built from a rig -- four creatures is not enough
       art to justify a fifth skeleton, and the rig is what the BOSS is for. */
    SPR_ASHHOUND,
    SPR_EMBERWING,
    SPR_SLAGMAW,
    SPR_CINDERLING,
    SPR_CINDER_HEART,
    SPR_CENSER_LIMB,
    SPR_CENSER_CALL,
    SPR_PYRE_CORE,
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

    /* --- the post-Censer armour ------------------------------------------
       Three more families in the same armour grammar the ladder above already
       uses: one helm shape, one suit shape, one greave shape, recoloured per
       family. That is the established rule here rather than a shortcut -- see
       the note beside the expandMetal calls in sprite.cpp -- and it is what
       makes a suit readable as armour at a glance and as WHICH armour at a
       second glance. */
    SPR_ARMOUR_THURIBLE_CROWN,
    SPR_ARMOUR_THURIBLE_HARNESS,
    SPR_ARMOUR_THURIBLE_GREAVES,
    SPR_ARMOUR_ASHEN_HOOD,
    SPR_ARMOUR_ASHEN_COAT,
    SPR_ARMOUR_ASHEN_GREAVES,
    SPR_ARMOUR_BRIMSTEEL_HELM,
    SPR_ARMOUR_BRIMSTEEL_PLATE,
    SPR_ARMOUR_BRIMSTEEL_GREAVES,
    /* One icon per layer-2 and layer-3 charm. Hand-drawn rather than tinted
       from a shared shape, unlike the armour ladder above: an armour piece is
       recognisable by being armour and only needs its family colour, while a
       charm has nothing to read but its own picture. */
    SPR_ACC_BALLAST,
    SPR_ACC_SPURS,
    SPR_ACC_LOADER,
    SPR_ACC_PRISM,
    SPR_ACC_TALON,
    SPR_ACC_CELL,
    SPR_ACC_COLLAR,
    SPR_ACC_FEATHER,
    SPR_ACC_GULLET,
    SPR_ACC_ASH,
    /* The four boss sigils. One shape, four tints -- the same arrangement the
       armour ladder uses and for the same reason its note gives: these ARE a
       ladder, and four unrelated pictures would hide the one fact about them
       that matters. */
    SPR_SIGIL_FORGE,
    SPR_SIGIL_SILK,
    SPR_SIGIL_PYRE,
    SPR_SIGIL_ASCENT,
    SPR_EFFIGY_CALL,
    SPR_ASCENT_CORE,

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

/* --- the Widow, layer 2's boss ----------------------------------------------
   The biggest thing drawn from a rig, and deliberately WIDER than it is tall:
   a spider is a low shape with a lot of horizontal reach, and a boss whose
   silhouette is a tall rectangle is another Brood Mother. See rigSpider. */
static const int WIDOW_SPR_W = 40;
static const int WIDOW_SPR_H = 32;
static const int WIDOW_IDLE_FRAMES = 2;
static const int WIDOW_WALK_FRAMES = 8;
extern u32 g_widowIdle[WIDOW_IDLE_FRAMES][WIDOW_SPR_W * WIDOW_SPR_H];
extern u32 g_widowWalk[WIDOW_WALK_FRAMES][WIDOW_SPR_W * WIDOW_SPR_H];

/* --- the Censer, layer 3's boss ---------------------------------------------
   Its own skeleton -- see rigHarvester. It was the spider's at a bigger size
   first, and that was the wrong call: the layer's capstone came out looking
   like the Widow in orange, because that is what it was.

   TALLER than it is wide, which is the shape the new rig needs and the opposite
   of both bosses above it: the Brood Mother and the Widow are low and broad,
   and this one is a small body hanging in the middle of a lot of vertical
   space. */
static const int CENSER_SPR_W = 64;
static const int CENSER_SPR_H = 56;
static const int CENSER_IDLE_FRAMES = 2;
static const int CENSER_WALK_FRAMES = 8;
extern u32 g_censerIdle[CENSER_IDLE_FRAMES][CENSER_SPR_W * CENSER_SPR_H];
extern u32 g_censerWalk[CENSER_WALK_FRAMES][CENSER_SPR_W * CENSER_SPR_H];

/* The Effigy: the last boss, and the biggest thing in the game by a distance.
   72 x 88 against the Censer's 64 x 56 -- over half again the area, taller
   than anything else in the game by a wide margin, and past what the
   armature's scratch buffer held until it was raised for this.

   72 rather than the 96 it was first drawn at, and the reason is that this one
   STANDS. A spider fills a wide box; an upright figure in a 96-cell one is
   mostly air, and the box is the hitbox -- so the extra width was a boss you
   could be hit by while standing well clear of anything drawn. */
static const int EFFIGY_SPR_W = 96;
static const int EFFIGY_SPR_H = 112;
static const int EFFIGY_IDLE_FRAMES = 4;
static const int EFFIGY_WALK_FRAMES = 16;
static const int EFFIGY_RITUAL_FRAMES = 8;
extern u32 g_effigyRitual[EFFIGY_RITUAL_FRAMES][EFFIGY_SPR_W * EFFIGY_SPR_H];
extern u32 g_effigyIdle[EFFIGY_IDLE_FRAMES][EFFIGY_SPR_W * EFFIGY_SPR_H];
extern u32 g_effigyWalk[EFFIGY_WALK_FRAMES][EFFIGY_SPR_W * EFFIGY_SPR_H];

void initSprites();
