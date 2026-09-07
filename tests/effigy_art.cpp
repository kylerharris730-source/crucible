#include "sprite.h"
#include "materials.h"
#include "item.h"
#include <stdio.h>
int main(int argc,char** argv) {
    initMaterials(); initItems(); initSprites();
    FILE* out=argc>1 ? fopen(argv[1],"w") : 0;
    if (argc>1 && !out) return 2;
    for (int pose=0;pose<2;++pose) for (int f=0;f<(pose ? EFFIGY_RITUAL_FRAMES : EFFIGY_WALK_FRAMES);++f) {
        const u32* p=pose ? g_effigyRitual[f] : g_effigyWalk[f];
        int count=0;
        if (out) fprintf(out,"%s %d %d %d",pose ? "Ritual" : "Walk",f,EFFIGY_SPR_W,EFFIGY_SPR_H);
        for (int k=0;k<EFFIGY_SPR_W*EFFIGY_SPR_H;++k) {
            count+=p[k]!=0;
            if (out) fprintf(out," %06x",p[k]);
        }
        if (count<1000) return 3;
        if (out) fprintf(out,"\n");
    }
    if (out) fclose(out);
    puts("PASS: enlarged walk and ritual frames are populated.");
}
