#include "worn_armour.h"
#include "item.h"
#include "sprite.h"
#include "rig.h"
#include <string.h>

namespace {
enum Style { BARE, IRON, STEEL, TITANIUM, DRONE, RANGER, VANGUARD, THURIBLE, ASHEN, BRIMSTEEL,
             CINDERWEAVE, FUMAROLE, PELT, HOARFROST };
/* `quilt` is the fourth silhouette, and the two resistance lines needed one:
   they are neither plate nor coat nor harness but LAGGING -- thick, soft and
   wrapped, with the bulk on the torso and a rolled collar rather than
   pauldrons. Without it the Cinderweave would have been the Ranger in a
   different colour, and the whole point of a suit worn for a place rather than
   for a fight is that you can tell at a glance which one somebody has on. */
struct Design { u32 plate, shadow, trim; bool heavy, coat, harness, quilt; };
const Design designs[] = {
    {0,0,0,false,false,false,false},
    {0xA8ADB6,0x555B64,0xC8C1AB,true,false,false,false},
    {0x8399B0,0x424F65,0xC8DCE8,true,false,false,false},
    {0xD2CDE3,0x777B98,0x96ECE6,false,false,false,false},
    {0x6FAFBE,0x3D6C78,0x8AF0ED,false,false,true,false},
    {0x9DA76A,0x4D633F,0xDEC293,false,true,false,false},
    {0xA85A65,0x59313B,0xE2AD79,true,false,false,false},
    {0xE8A24A,0x8A5410,0xFFE5A1,false,false,true,false},
    {0xB9B2A6,0x5E5A52,0xEBB56F,false,true,false,false},
    {0xC0492A,0x5E2010,0xFFD06A,true,false,false,false},
    /* The four specialists. Two ember palettes and two frost ones, and the
       later suit of each pair is the brighter and harder of the two -- quilted
       hide first, then plate over it. */
    {0xB4643C,0x5E2E18,0xE8A05A,false,false,false,true},
    {0xE07A28,0x7A3608,0xFFD08A,true, false,false,true},
    {0x8AA0B4,0x46586A,0xD6E4F0,false,false,false,true},
    {0xC8E8F7,0x6A94B4,0xFFFFFF,true, false,false,true}
};
int style(ItemId id) {
    if (id<=ITEM_NONE || id>=ITEM_COUNT || ITEMS[id].armour<=0) return BARE;
    switch (ITEMS[id].armourSet) {
    case ARMOUR_SET_DRONE: return DRONE;
    case ARMOUR_SET_RANGED: return RANGER;
    case ARMOUR_SET_MELEE: return VANGUARD;
    case ARMOUR_SET_DRONE_PYRE: return THURIBLE;
    case ARMOUR_SET_RANGED_PYRE: return ASHEN;
    case ARMOUR_SET_MELEE_PYRE: return BRIMSTEEL;
    default: break;
    }
    switch (id) {
    case ITEM_IRON_HELMET: case ITEM_IRON_CUIRASS: case ITEM_IRON_GREAVES: return IRON;
    case ITEM_STEEL_HELMET: case ITEM_STEEL_SUIT: return STEEL;
    case ITEM_TITANIUM_HELMET: case ITEM_TITANIUM_SUIT: return TITANIUM;
    case ITEM_CINDERWEAVE_HOOD: case ITEM_CINDERWEAVE_COAT: return CINDERWEAVE;
    case ITEM_FUMAROLE_HELM: case ITEM_FUMAROLE_PLATE: return FUMAROLE;
    case ITEM_PELT_HOOD: case ITEM_PELT_COAT: return PELT;
    case ITEM_HOARFROST_HELM: case ITEM_HOARFROST_PLATE: return HOARFROST;
    default: return BARE;
    }
}
struct Frames {
    bool valid;
    int styles[3];
    u32 standing[PF_COUNT][PSPR_W*PSPR_H];
    u32 crouch[CSPR_W*CSPR_H];
};
Frames cache[8];
unsigned nextEntry=0;

void bake(Frames& f, const int* styles) {
    Bone bones[ARM_MAX_BONES];
    u32 shades[32];
    memcpy(shades,RIG_SUIT,sizeof(RIG_SUIT));
    RigDef rig;
    rigHumanoid(bones,&rig,"worn armour",PSPR_W,PSPR_H,shades);
    int count=RB_COUNT;
    // Decoration bones inherit the joint's animation, not fixed screen positions.
    const auto detail = [&](int parent,int at,int angle,int length,int width,int colour) {
        Bone& b=bones[count++];
        b.parent=(i8)parent; b.at=(u8)at; b.rest=(i16)angle;
        b.len=(i16)length; b.wBase=b.wTip=(u8)width;
        b.shade=(u8)colour; b.layer=9;
    };
    for (int part=0; part<3; ++part) {
        const int s=styles[part];
        if (!s) continue;
        const Design& d=designs[s];
        int col=9+part*3;
        shades[col]=d.plate; shades[col+1]=d.shadow; shades[col+2]=d.trim;
        if (part==0) {
            bones[RB_HEAD].shade=col;
            bones[RB_VISOR].shade=col+1;
            bones[RB_HEAD].wTip=d.coat ? 3 : d.heavy ? 10 : 8;
            bones[RB_VISOR].wBase=d.harness ? 4 : 5;
            bones[RB_VISOR].wTip=3;
            if (d.quilt) { // Padded hood, rolled collar, and a filter at the jaw.
                bones[RB_HEAD].wTip=11;
                detail(RB_HEAD,60,90,9,4,col+1);   // the roll, around the neck
                detail(RB_HEAD,60,-90,9,4,col+1);
                detail(RB_HEAD,170,-40,5,3,col+2); // filter, on the facing side
            } else if (d.coat) { // Pointed hood and forward brim.
                detail(RB_HEAD,150,-75,7,2,col);
            } else if (d.harness) { // Earpiece and upright receiver/crown prongs.
                detail(RB_HEAD,100,95,4,3,col+1);
                detail(RB_HEAD,100,15,s==THURIBLE ? 14 : 10,2,col+2);
            } else {
                detail(RB_HEAD,120,0,6,3,col+2); // helmet ridge
                if (s==VANGUARD || s==BRIMSTEEL)
                    detail(RB_HEAD,80,-150,6,2,col); // cheek guard
            }
        } else if (part==1) {
            bones[RB_SPINE].shade=col+1;
            bones[RB_NEAR_UPPER].shade=col;
            bones[RB_FAR_UPPER].shade=col+1;
            bones[RB_NEAR_FORE].shade=col;
            bones[RB_FAR_FORE].shade=col+1;
            bones[RB_SPINE].wTip=d.heavy ? 12 : d.harness ? 7 : 9;
            detail(RB_SPINE,70,0,14,d.heavy ? 6 : 3,col);
            detail(RB_SPINE,170,80,6,3,col+2);
            if (d.quilt) {
                /* Three bands around the trunk rather than shoulders on it.
                   Lagging is thickest at the middle and the bands are what say
                   it is wrapped rather than cast -- and they are child bones of
                   the spine, so they sway with the body instead of sitting
                   over an animated character like a sticker. */
                bones[RB_SPINE].wTip=(u8)(d.heavy ? 14 : 12);
                detail(RB_SPINE,90,90,11,3,col+1);
                detail(RB_SPINE,150,90,10,3,col+1);
                detail(RB_SPINE,210,90,9,3,col+1);
                /* And a heavier sleeve on the near arm, which is what makes
                   the later pair of each line read as plate over the padding
                   rather than as more padding. */
                if (d.heavy) detail(RB_NEAR_UPPER,0,0,7,6,col+2);
            } else if (d.heavy) { // Pauldron follows the upper arm, with separate gauntlet.
                detail(RB_NEAR_UPPER,0,0,6,7,col);
                detail(RB_NEAR_FORE,180,0,4,4,col+2);
            } else if (d.coat) {
                detail(RB_SPINE,0,160,19,5,col); // rear coat skirt
                detail(RB_SPINE,200,160,12,3,col+2); // lapel/sash
            } else if (d.harness) {
                bones[RB_PACK].shade=col+1;
                bones[RB_PACK].wBase=5;
                detail(RB_PACK,60,0,9,3,col+2); // control unit
                detail(RB_SPINE,210,150,14,3,col+2);
            } else {
                detail(RB_NEAR_UPPER,0,0,6,3,col+2);
            }
        } else {
            const int legs[]={RB_NEAR_THIGH,RB_NEAR_SHIN,RB_NEAR_FOOT,RB_FAR_THIGH,RB_FAR_SHIN,RB_FAR_FOOT};
            for (int i=0;i<6;++i) {
                bones[legs[i]].shade=i<3 ? col : col+1;
                if (d.heavy) bones[legs[i]].wBase+=1;
            }
            detail(RB_NEAR_SHIN,0,0,d.harness ? 10 : 5,d.heavy ? 4 : 2,col+2);
            detail(RB_FAR_SHIN,0,0,5,2,col+1);
        }
    }
    rig.bones=count;
    armBake(&rig,&RIG_IDLE,f.standing[PF_IDLE]);
    armBake(&rig,&RIG_WALK,f.standing[PF_WALK0]);
    armBake(&rig,&RIG_JUMP,f.standing[PF_JUMP]);
    armBake(&rig,&RIG_FALL,f.standing[PF_FALL]);
    u32 full[PSPR_W*PSPR_H];
    armBake(&rig,&RIG_CROUCH,full);
    memcpy(f.crouch,full+(PSPR_H-CSPR_H)*PSPR_W,sizeof(f.crouch));
    memcpy(f.styles,styles,sizeof(f.styles));
    f.valid=true;
}
}

const u32* wornArmourFrame(const Inventory* inv,bool crouch,int frame) {
    int styles[3]={0,0,0};
    if (inv) {
        const int slots[]={EQ_HEAD,EQ_BODY,EQ_FEET};
        for (int i=0;i<3;++i) if (!inv->equip[slots[i]].empty()) styles[i]=style(inv->equip[slots[i]].item);
    }
    frame=crouch ? 0 : (frame>=0 && frame<PF_COUNT ? frame : PF_IDLE);
    if (!(styles[0]||styles[1]||styles[2])) return crouch ? g_playerCrouchSpr[0] : g_playerSpr[frame];
    Frames* found=0;
    for (int i=0;i<8;++i) if (cache[i].valid && !memcmp(cache[i].styles,styles,sizeof(styles))) { found=&cache[i]; break; }
    if (!found) { found=&cache[nextEntry++%8]; bake(*found,styles); }
    return crouch ? found->crouch : found->standing[frame];
}
