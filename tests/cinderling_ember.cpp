#include "world.h"
#include "item.h"
#include "accessory.h"
#include "multiplayer.h"
#include <stdio.h>

static int failures;
static void check(bool ok,const char* label) {
    printf("%s: %s\n",ok?"PASS":"FAIL",label); if(!ok)++failures;
}
static int flames(u8 ember) {
    g_world.reset(); g_world.setLiveWindow(1150,1150,1450,1250);
    int produced=0;
    // Independent open emitters, refreshed equally: measure emission rather
    // than different thermal masses changing their lifetimes.
    for(int f=0;f<150;++f) {
        for(int n=0;n<24;++n) {
            const int x=1200+n*8;
            g_world.setCell(x,1200,ember);
            g_world.setCell(x,1199,MAT_EMPTY);
            g_world.setCell(x-1,1200,MAT_EMPTY);
            g_world.setCell(x+1,1200,MAT_EMPTY);
        }
        g_world.step();
        for(int n=0;n<24;++n) {
            const int x=1200+n*8;
            produced+=g_world.at(x,1199).mat==MAT_FIRE;
            produced+=g_world.at(x-1,1200).mat==MAT_FIRE;
            produced+=g_world.at(x+1,1200).mat==MAT_FIRE;
        }
    }
    return produced;
}
int main() {
    initMaterials();initItems();playerSessionsReset();
    check(MATS[MAT_CINDERLING_EMBER].spawnTemp==MATS[MAT_FUELFIRE].spawnTemp,"same starting heat as FuelFire");
    check(g_matDrive[MAT_CINDERLING_EMBER]==g_matDrive[MAT_FUELFIRE],"same heating drive as FuelFire");
    check(g_matDecay[MAT_CINDERLING_EMBER]==2*g_matDecay[MAT_WOOD_EMBER],"half the previous average trail lifetime");
    check(g_matIgnitesOnContact[MAT_CINDERLING_EMBER]!=0,"ignites neighboring fuel");
    const int wood=flames(MAT_WOOD_EMBER),cinder=flames(MAT_CINDERLING_EMBER);
    printf("wood flame observations: %d; cinderling: %d\n",wood,cinder);
    check(cinder>wood*13/10,"produces measurably more flames than wood");
    g_world.reset();
    Player p; p.reset(1250,1200);p.vx=2;p.onGround=true;
    Inventory inv;inv.clear();
    inv.equip[EQ_TRINKET_A].item=ITEM_CINDERLING_ASH;
    inv.equip[EQ_TRINKET_A].count=1;
    for(int x=1200;x<1300;++x)g_world.setCell(x,p.bottom()+1,MAT_STONE);
    accessoryAshTrail(0,p,inv,g_world);
    const int heel=p.left()-1;
    check(g_world.at(heel,p.bottom()).mat==MAT_CINDERLING_EMBER,"ash accessory places its own ember");
    accessoryAshTrail(0,p,inv,g_world);
    check(g_world.at(heel,p.bottom()-2).mat==MAT_EMPTY,"trail does not stack on itself");
    return failures?1:0;
}
