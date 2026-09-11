#include "spear_art.h"
#include "materials.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static const int W=384,H=100;
static u32 sheet[W*H*7],shortArt[W*H],longArt[W*H];
static u32 tint(u32,int,int) { return 0x123456; }
int main(int argc,char** argv) {
    initMaterials(); initItems();
    const ItemId ids[]={ITEM_SPEAR_COPPER,ITEM_SPEAR_BRONZE,ITEM_SPEAR_IRON,
        ITEM_SPEAR_GOLD,ITEM_SPEAR_STEEL,ITEM_SPEAR_TITANIUM,ITEM_SPEAR_TUNGSTEN};
    for (int i=0;i<7;++i) {
        memset(shortArt,0,sizeof(shortArt)); memset(longArt,0,sizeof(longArt));
        drawHeldSpear(shortArt,W,H,ids[i],0,20,40,20);
        drawHeldSpear(longArt,W,H,ids[i],0,20,80,20);
        // The spearhead and socket never stretch with the thrust.
        for (int y=16;y<=24;++y) for (int x=20;x<=40;++x)
            assert(shortArt[y*W+x]==longArt[y*W+x+40]);
        assert(longArt[20*W+80]!=0);
        int shaft=0,head=0;
        for (int y=15;y<=25;++y) {
            shaft+=longArt[y*W+20]!=0;
            head+=longArt[y*W+70]!=0;
        }
        assert(head>shaft);
        u32* row=sheet+i*W*H;
        drawHeldSpear(row,W,H,ids[i],12,40,52,40);
        drawHeldSpear(row,W,H,ids[i],85,40,165,40);
        drawHeldSpear(row,W,H,ids[i],205,78,262,21);
        drawHeldSpear(row,W,H,ids[i],360,24,295,70);
        if (i) assert(memcmp(row,row-W*H,W*H*sizeof(u32))!=0);
    }
    u32 clipped[18]; memset(clipped,0,sizeof(clipped));
    clipped[0]=clipped[17]=0xABCDEF;
    drawHeldSpear(clipped+1,4,4,ids[0],-20,-20,20,20,tint);
    assert(clipped[0]==0xABCDEF && clipped[17]==0xABCDEF);
    for (int i=1;i<=16;++i) assert(!clipped[i] || clipped[i]==0x123456);
    if (argc>1) {
        FILE* f=fopen(argv[1],"wb"); if (!f) return 2;
        fprintf(f,"P6\n%d %d\n255\n",W,H*7);
        for (int k=0;k<W*H*7;++k) {
            u32 c=sheet[k] ? sheet[k] : 0x20242D;
            unsigned char rgb[]={(unsigned char)(c>>16),(unsigned char)(c>>8),(unsigned char)c};
            fwrite(rgb,1,3,f);
        }
        fclose(f);
    }
    puts("PASS: seven spear designs, stable heads, distinct shafts, clipping and lighting.");
}
