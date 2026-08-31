/* Multiplayer players share one silhouette, so their optional identity tint
   must visibly change it while the zero/default path remains the original art.
   Compile with every src/*.cpp except main.cpp. */

#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "player.h"
#include "render.h"
#include <stdio.h>
#include <string.h>

static u32 plain[VIEW_CELLS_W * VIEW_CELLS_H];
static u32 amber[VIEW_CELLS_W * VIEW_CELLS_H];
static u32 cyan[VIEW_CELLS_W * VIEW_CELLS_H];

int main() {
    initMaterials();
    initItems();
    initSprites();

    Player player;
    player.reset(100.0f, 100.0f);
    player.draw(plain, 0, 0, false, 0);
    player.draw(amber, 0, 0, false, 0xF0B44C);
    player.draw(cyan, 0, 0, false, 0x55BFE6);

    int amberChanged = 0, cyanChanged = 0, identitiesDiffer = 0;
    for (int i = 0; i < VIEW_CELLS_W * VIEW_CELLS_H; ++i) {
        if (plain[i] != amber[i]) ++amberChanged;
        if (plain[i] != cyan[i]) ++cyanChanged;
        if (amber[i] != cyan[i]) ++identitiesDiffer;
    }
    printf("changed pixels: amber %d, cyan %d, between identities %d\n",
           amberChanged, cyanChanged, identitiesDiffer);
    if (amberChanged < 20 || cyanChanged < 20 || identitiesDiffer < 20) {
        fprintf(stderr, "FAIL: multiplayer identity colours are not visually distinct\n");
        return 1;
    }

    memset(amber, 0, sizeof(amber));
    player.draw(amber, 0, 0, false);  /* default must remain the zero path */
    if (memcmp(plain, amber, sizeof(plain)) != 0) {
        fprintf(stderr, "FAIL: single-player draw changed when no identity was requested\n");
        return 2;
    }
    printf("PASS\n");
    return 0;
}
