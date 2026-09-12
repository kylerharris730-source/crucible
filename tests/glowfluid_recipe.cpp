#include "craft.h"
#include <stdio.h>
int main() {
    initMaterials(); initItems();
    int recipe=-1;
    for(int r=0;r<N_RECIPES;++r) if(RECIPES[r].out==MAT_GLOWFLUID) recipe=r;
    if(recipe<0 || RECIPES[recipe].station!=STATION_HAND) return 1;
    Inventory inv; inv.clear();
    Player p; p.reset(1200,1200);
    g_world.reset(); craftScanStations(g_world,p);
    inv.add(MAT_COAL,1); inv.add(MAT_WATER,3);
    if(craftMake(inv,recipe) || inv.countOf(MAT_COAL)!=1 || inv.countOf(MAT_WATER)!=3) return 2;
    inv.add(MAT_WATER,1);
    if(!craftMake(inv,recipe)) return 3;
    if(inv.countOf(MAT_COAL)!=0 || inv.countOf(MAT_WATER)!=0 || inv.countOf(MAT_GLOWFLUID)!=4) return 4;
    if(g_matWetInto[MAT_COAL]!=MAT_FUEL || g_matWetBy[MAT_COAL]!=MAT_STEAM) return 5;
    puts("PASS: 1 coal + 4 water crafts 4 GlowFluid by hand; missing ingredients are not spent; steam slaking is unchanged");
    return 0;
}
