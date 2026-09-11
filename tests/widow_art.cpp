/* The Widow's tells, dumped for scripts/preview_widow.py to render.

   Run with a path it writes the dump; run with NO arguments it checks that
   every pose still draws something and says so. That second mode is not
   decoration: the suite runs every harness in tests/ with no arguments, and a
   dumper that returns an error code when it has nowhere to write is a
   permanent red line in a suite whose whole value is that red means broken.
   tests/effigy_art.cpp is the same shape for the same reason. */
#include "entity.h"
#include "materials.h"
#include "sprite.h"
#include "item.h"
#include "render.h"
#include <stdio.h>
#include <string.h>

static u32 pixels[VIEW_CELLS_W*VIEW_CELLS_H];

int main(int argc,char** argv) {
    initMaterials(); initItems(); initSprites(); entReset();
    FILE* out=argc>1 ? fopen(argv[1],"w") : 0;
    if (argc>1 && !out) return 2;
    const int phases[]={WIDOW_APPROACH,WIDOW_WEB_WIND,WIDOW_LEAP_WIND,WIDOW_RECOVER,WIDOW_MOULT};
    const char* names[]={"Approach","Web tell","Pounce tell","Recovery","Half-health"};
    for (int i=0;i<5;++i) {
        Entity& e=g_entities[0]; e.type=ENT_WIDOW; e.hp=1400;
        e.x=8; e.y=8; e.facing=1; e.phase=phases[i]; e.walkPhase=8;
        e.vx=i==0 ? 1.0f : 0.0f; e.telegraph=(i==1 || i==2) ? 15 : 0;
        memset(pixels,0,sizeof(pixels)); entDraw(pixels,0,0,false);
        int drawn=0;
        if (out) fprintf(out,"%s\t",names[i]);
        for (int y=0;y<48;++y) for (int x=0;x<56;++x) {
            const u32 c=pixels[y*VIEW_CELLS_W+x];
            drawn+=c!=0;
            if (out) fprintf(out," %06x",c);
        }
        if (out) fprintf(out,"\n");
        /* A pose that draws nothing is the failure this can actually catch:
           a phase renamed out from under the art, or a tell that stopped
           picking a frame. */
        if (drawn<200) {
            fprintf(stderr,"FAIL: %s draws only %d pixels\n",names[i],drawn);
            if (out) fclose(out);
            return 3;
        }
    }
    if (out) fclose(out);
    puts("PASS: every Widow pose draws.");
    return 0;
}
