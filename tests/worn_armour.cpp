#include "worn_armour.h"
#include "sprite.h"
#include "item.h"
#include "materials.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(int argc,char** argv) {
    initMaterials(); initItems(); initSprites();
    const ItemId sets[][3]={
        {ITEM_NONE,ITEM_NONE,ITEM_NONE},
        {ITEM_IRON_HELMET,ITEM_IRON_CUIRASS,ITEM_IRON_GREAVES},
        {ITEM_STEEL_HELMET,ITEM_STEEL_SUIT,ITEM_NONE},
        {ITEM_TITANIUM_HELMET,ITEM_TITANIUM_SUIT,ITEM_NONE},
        {ITEM_DRONE_VISOR,ITEM_DRONE_HARNESS,ITEM_DRONE_GREAVES},
        {ITEM_RANGER_VISOR,ITEM_RANGER_COAT,ITEM_RANGER_GREAVES},
        {ITEM_VANGUARD_HELM,ITEM_VANGUARD_PLATE,ITEM_VANGUARD_GREAVES},
        {ITEM_THURIBLE_CROWN,ITEM_THURIBLE_HARNESS,ITEM_THURIBLE_GREAVES},
        {ITEM_ASHEN_HOOD,ITEM_ASHEN_COAT,ITEM_ASHEN_GREAVES},
        {ITEM_BRIMSTEEL_HELM,ITEM_BRIMSTEEL_PLATE,ITEM_BRIMSTEEL_GREAVES},
        {ITEM_DRONE_VISOR,ITEM_VANGUARD_PLATE,ITEM_RANGER_GREAVES}
    };
    const char* names[]={"Bare","Iron","Steel","Titanium","Drone","Ranger","Vanguard","Thurible","Ashen","Brimsteel","Mixed"};
    const int slots[]={EQ_HEAD,EQ_BODY,EQ_FEET};
    FILE* out=argc>1 ? fopen(argv[1],"w") : 0;
    if (argc>1 && !out) return 2;
    u32 saved[PSPR_W*PSPR_H];
    for (int s=0;s<11;++s) {
        Inventory inv={};
        for (int p=0;p<3;++p) inv.equip[slots[p]]={sets[s][p],1,0};
        for (int f=0;f<=PF_COUNT;++f) {
            bool crouch=f==PF_COUNT;
            const u32* pixels=wornArmourFrame(&inv,crouch,f);
            int height=crouch ? CSPR_H : PSPR_H, count=0;
            if (out) fprintf(out,"%s\t%d\t%d\t%d",names[s],f,PSPR_W,height);
            for (int k=0;k<PSPR_W*height;++k) {
                count+=pixels[k]!=0;
                if (out) fprintf(out," %06x",pixels[k]);
            }
            assert(count>20);
            if (out) fprintf(out,"\n");
            if (s && !crouch) assert(memcmp(pixels,g_playerSpr[f],sizeof(saved))!=0);
        }
        // Each equipped piece changes art even without a set bonus.
        for (int p=0;p<3;++p) if (sets[s][p]!=ITEM_NONE) {
            Inventory one={}; one.equip[slots[p]]={sets[s][p],1,0};
            assert(memcmp(wornArmourFrame(&one,false,PF_IDLE),g_playerSpr[PF_IDLE],sizeof(saved))!=0);
        }
        if (s==1) memcpy(saved,wornArmourFrame(&inv,false,PF_IDLE),sizeof(saved));
    }
    Inventory iron={};
    for (int p=0;p<3;++p) iron.equip[slots[p]]={sets[1][p],1,0};
    assert(!memcmp(saved,wornArmourFrame(&iron,false,PF_IDLE),sizeof(saved)));
    assert(wornArmourFrame(0,false,PF_IDLE)==g_playerSpr[PF_IDLE]);
    for (int id=1;id<ITEM_COUNT;++id) {
        const ItemDef& d=ITEMS[id];
        if (d.armour<=0 || (d.equipSlot!=EQ_HEAD && d.equipSlot!=EQ_BODY && d.equipSlot!=EQ_FEET)) continue;
        Inventory single={}; single.equip[d.equipSlot]={(ItemId)id,1,0};
        if (!memcmp(wornArmourFrame(&single,false,PF_IDLE),g_playerSpr[PF_IDLE],sizeof(saved))) {
            fprintf(stderr,"Missing worn design: %s\n",d.name); return 3;
        }
    }
    if (out) fclose(out);
    puts("PASS: nine armour families, individual pieces, mixed sets, all poses, unequip and cache eviction.");
}
