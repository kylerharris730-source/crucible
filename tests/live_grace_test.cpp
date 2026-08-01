#include "world.h"
#include "materials.h"
#include "room.h"
#include <stdio.h>

/* device.cpp is linked into this headless test because world-facing helpers are
   shared across the game. The UI normally owns this flag. */
bool g_logisticsUiOpen = false;

static int waterY(const World& w, int x, int y0, int y1) {
    for (int y = y0; y <= y1; ++y) if (w.at(x, y).mat == MAT_WATER) return y;
    return -1;
}

int main() {
    initMaterials();
    static World w;  /* World is deliberately large; keep this off the stack. */
    w.reset();
    const int x = 64, startY = 80;
    w.setLiveWindow(32, 32, 95, 95);
    w.setCell(x, startY, MAT_WATER);
    w.step();                         /* seed the grace timer while visible */
    const int visibleY = waterY(w, x, startY, startY + 4);
    if (visibleY < 0) { fprintf(stderr, "water did not start\n"); return 1; }

    w.setLiveWindow(1000, 1000, 1063, 1063);
    for (int i = 0; i < 10; ++i) w.step();
    const int afterTen = waterY(w, x, visibleY, visibleY + 16);
    if (afterTen <= visibleY) { fprintf(stderr, "water froze outside view\n"); return 2; }

    for (int i = 10; i < LIVE_GRACE_STEPS; ++i) w.step();
    const int beforeFreeze = waterY(w, x, afterTen, afterTen + LIVE_GRACE_STEPS + 8);
    w.step();
    const int afterFreeze = waterY(w, x, afterTen, afterTen + LIVE_GRACE_STEPS + 8);
    if (beforeFreeze < 0 || afterFreeze != beforeFreeze) {
        fprintf(stderr, "water did not freeze after grace\n"); return 3;
    }
    w.reset(); roomsClear(w);
    /* A backed, sealed 4x4 interior is the smallest deliberate room. */
    for (int y = 120; y <= 125; ++y) for (int x2 = 120; x2 <= 125; ++x2) {
        if (x2 == 120 || x2 == 125 || y == 120 || y == 125) w.setCell(x2, y, MAT_STONE);
        else w.setBg(x2, y, MAT_STONE, true);
    }
    roomsNotifyEdit(w, 120, 120);
    if (roomCount() != 1 || w.keptChunks == 0) {
        fprintf(stderr, "backed sealed room did not register\n"); return 4;
    }
    puts("live grace test passed");
    return 0;
}
