#include "material_icon.h"
#include "materials.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
    initMaterials();
    initSprites();
    u32 pixels[INV_SPR_W * INV_SPR_H];
    FILE* out = argc > 1 ? fopen(argv[1], "w") : 0;
    if (argc > 1 && !out) return 2;
    for (int m=1; m<MAT_COUNT; ++m) {
        renderMaterialIcon(m,pixels);
        int count=0;
        if (out) fprintf(out,"%s\t",MATS[m].name);
        for (int k=0; k<INV_SPR_W*INV_SPR_H; ++k) {
            if (pixels[k]) ++count;
            if ((k%21==0 || k%21==20 || k<21 || k>=420) && pixels[k]) return 3;
            if (out) fprintf(out," %06x",pixels[k]);
        }
        if (count<8) { fprintf(stderr,"Empty icon: %s\n",MATS[m].name); return 4; }
        if (out) fprintf(out,"\n");
        u32 again[INV_SPR_W*INV_SPR_H];
        renderMaterialIcon(m,again);
        if (memcmp(pixels,again,sizeof(pixels))) return 5;
    }
    if (out) fclose(out);
    u32 copper[INV_SPR_W*INV_SPR_H];
    renderMaterialIcon(MAT_COPPER,copper);
    renderMaterialIcon(MAT_COPPER_ORE,pixels);
    if (!memcmp(pixels,copper,sizeof(pixels))) return 6;
    printf("All %d material icons have deterministic art and transparent margins.\n",MAT_COUNT-1);
    return 0;
}
