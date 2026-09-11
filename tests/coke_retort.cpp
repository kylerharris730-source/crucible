#include "world.h"
#include "materials.h"
#include "item.h"
#include <stdio.h>

static int failures;
static const int X=1200, Y=1600;
static void check(bool ok, const char* label) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static int count(u8 mat, int radius=25) {
    int n=0;
    for(int y=Y-radius;y<=Y+radius;++y)
        for(int x=X-radius;x<=X+radius;++x) n+=g_world.at(x,y).mat==mat;
    return n;
}
static void reset() {
    g_world.reset(); g_world.setLiveWindow(X-40,Y-40,X+40,Y+40);
}
static void retort(int heat, bool vent) {
    reset();
    for(int y=Y-5;y<=Y+5;++y) for(int x=X-5;x<=X+5;++x) {
        const bool wall=x==X-5||x==X+5||y==Y-5||y==Y+5;
        g_world.setCell(x,y,wall ? MAT_CERAMIC : MAT_FUEL);
        g_world.temp[y*SIM_W+x]=degC(heat);
    }
    if(vent) for(int x=X-3;x<=X+3;++x) g_world.setCell(x,Y-5,MAT_GAS_SIEVE);
}
static void heatWalls(int heat) {
    for(int d=-5;d<=5;++d) {
        g_world.temp[(Y-5)*SIM_W+X+d]=degC(heat);
        g_world.temp[(Y+5)*SIM_W+X+d]=degC(heat);
        g_world.temp[(Y+d)*SIM_W+X-5]=degC(heat);
        g_world.temp[(Y+d)*SIM_W+X+5]=degC(heat);
    }
}
static int furnace(u8 ore, u8 fire) {
    reset();
    for(int y=Y-7;y<=Y+7;++y) for(int x=X-7;x<=X+7;++x)
        g_world.setCell(x,y,MAT_REFRACTORY);
    for(int y=Y-2;y<=Y+2;++y) for(int x=X-2;x<=X+2;++x) g_world.setCell(x,y,ore);
    for(int y=Y-5;y<=Y+5;++y) for(int x=X-5;x<=X+5;++x)
        if(x<X-2||x>X+2||y<Y-2||y>Y+2) g_world.setCell(x,y,fire);
    for(int frame=0;frame<800;++frame) {
        // Constant supply at this fuel's OWN temperature, never heat the ore.
        for(int y=Y-5;y<=Y+5;++y) for(int x=X-5;x<=X+5;++x)
            if((x<X-2||x>X+2||y<Y-2||y>Y+2) &&
               (g_world.at(x,y).mat==fire || g_world.at(x,y).mat==MAT_EMPTY))
                g_world.setCell(x,y,fire);
        g_world.step();
    }
    const int remaining=count(ore);
    printf("  ore %s, source %s: %d/25 converted\n",MATS[ore].name,MATS[fire].name,25-remaining);
    return 25-remaining;
}
int main() {
    initMaterials(); initItems();
    check(MATS[MAT_FUELFIRE].spawnTemp<MATS[MAT_TITANIUM_ORE].boilTemp,"fuel is below titanium threshold");
    check(MATS[MAT_FUELFIRE].spawnTemp<MATS[MAT_TUNGSTEN_ORE].boilTemp,"fuel is below tungsten threshold");
    check(MATS[MAT_COKE_EMBER].spawnTemp>MATS[MAT_TUNGSTEN_ORE].boilTemp,"coke can reach both top ore thresholds");
    check(MATS[MAT_FIRE].spawnTemp<MATS[MAT_TITANIUM_ORE].boilTemp,"recovered gas fire stays below titanium smelting heat");
    retort(160,false);
    for(int f=0;f<1000;++f) { heatWalls(160); g_world.step(); }
    const int coke=count(MAT_COKE), gas=count(MAT_COKE_GAS), fuel=count(MAT_FUEL);
    printf("  sealed retort: %d coke, %d gas, %d fuel\n",coke,gas,fuel);
    check(coke>10 && gas==coke,"sealed hot fuel makes coke and gas in equal amounts");
    check(coke+gas+fuel==81,"retort conversion conserves cells");
    check(count(MAT_FUELFIRE)+count(MAT_COKE_EMBER)+count(MAT_FIRE)==0,"sealed hot charge does not ignite");
    retort(100,false);
    for(int f=0;f<400;++f) { heatWalls(100); g_world.step(); }
    check(count(MAT_COKE)==0 && count(MAT_FUEL)==81,"cold retort does nothing");
    retort(20,false);
    for(int d=-5;d<=5;++d) {
        g_world.setCell(X+d,Y-5,MAT_IRON); g_world.setCell(X+d,Y+5,MAT_IRON);
        g_world.setCell(X-5,Y+d,MAT_IRON); g_world.setCell(X+5,Y+d,MAT_IRON);
    }
    for(int f=0;f<1600;++f) {
        // Feed an external coal fire. The charge and wall temperatures are
        // entirely simulated, so this checks that the recipe is reachable.
        for(int d=-5;d<=5;++d) {
            g_world.setCell(X+d,Y+6,MAT_EMBER);
            g_world.setCell(X-6,Y+d,MAT_EMBER);
            g_world.setCell(X+6,Y+d,MAT_EMBER);
        }
        g_world.step();
    }
    printf("  external retort: %d coke, %d gas, %d fuel, %d fire; center %d C, wall %d C\n",
        count(MAT_COKE),count(MAT_COKE_GAS),count(MAT_FUEL),count(MAT_FUELFIRE),
        g_world.temp[Y*SIM_W+X]-TEMP_OFFSET,g_world.temp[Y*SIM_W+X-5]-TEMP_OFFSET);
    check(count(MAT_COKE)>0,"external coal heat through iron walls cooks a cold batch");
    reset();
    g_world.setCell(X,Y+1,MAT_CERAMIC); g_world.setCell(X,Y,MAT_FUEL);
    g_world.temp[Y*SIM_W+X]=degC(160); g_world.step();
    check(g_world.at(X,Y).mat==MAT_FUELFIRE,"exposed hot fuel burns rather than coking");
    reset();
    g_world.setCell(X,Y,MAT_COKE_GAS); g_world.temp[Y*SIM_W+X]=degC(160); g_world.step();
    check(count(MAT_FIRE)>0,"vented hot gas provides ordinary fire heat");
    reset();
    g_world.setCell(X,Y,MAT_COKE); g_world.setCell(X,Y+1,MAT_CERAMIC);
    g_world.temp[Y*SIM_W+X]=degC(160); g_world.step();
    check(g_world.at(X,Y).mat==MAT_COKE_EMBER,"hot coke exposed to air ignites");
    check(g_matDecay[MAT_COKE_EMBER]>0,"burning coke is finite, not a permanent heater");
    retort(130,true);
    bool escaped=false;
    for(int f=0;f<1200;++f) {
        heatWalls(130); g_world.step();
        for(int y=Y-24;y<Y-5;++y) for(int x=X-20;x<=X+20;++x)
            escaped=escaped||g_world.at(x,y).mat==MAT_COKE_GAS;
    }
    check(escaped,"coke gas escapes through a gas-sieve outlet");
    check(furnace(MAT_IRON_ORE,MAT_FUELFIRE)>0,"ordinary fuel still smelts iron");
    check(furnace(MAT_TITANIUM_ORE,MAT_FUELFIRE)==0,"ordinary fuel cannot smelt titanium");
    check(furnace(MAT_TUNGSTEN_ORE,MAT_FUELFIRE)==0,"ordinary fuel cannot smelt tungsten");
    check(furnace(MAT_TITANIUM_ORE,MAT_COKE_EMBER)>0,"coke furnace smelts titanium");
    check(furnace(MAT_TUNGSTEN_ORE,MAT_COKE_EMBER)>0,"coke furnace smelts tungsten");
    return failures ? 1 : 0;
}
