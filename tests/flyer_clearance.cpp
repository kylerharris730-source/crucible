/* --- flyers route bodies, not centrelines ---------------------------------

   A six-cell tube points straight at the player. Bats and moths are nine by
   seven cells, so the centreline is open and the body cannot pass. A generous
   opening above is the real route. Both flyers must use it rather than grind
   forever at the attractive false opening.

   Compile with every source file except main.cpp. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "entity.h"
#include "multiplayer.h"
#include <stdio.h>

static World g_testWorld;

static void fill(World& w, int x0, int y0, int x1, int y1, u8 mat) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) w.setCell(x, y, mat);
}

static bool crosses(EntityType type) {
    World& w = g_testWorld;
    w.reset();
    const int wallL = 1000, wallR = 1040;
    fill(w, wallL, 400, wallR, 800, MAT_STONE);
    fill(w, wallL, 480, wallR, 520, MAT_EMPTY); /* body-sized route */
    fill(w, wallL, 590, wallR, 595, MAT_EMPTY); /* false six-cell tube */
    w.setLiveWindow(820, 360, 1220, 840);

    Player& p = g_player;
    p.reset(1120.0f, 575.0f);
    p.alive = true; p.hp = PLAYER_HP_MAX;

    entReset();
    const int slot = entSpawn(w, type, 900.0f, 589.0f);
    if (slot < 0) return false;

    for (int frame = 0; frame < 3600; ++frame) {
        p.x = 1120.0f; p.y = 575.0f;
        p.alive = true; p.hp = PLAYER_HP_MAX;
        entTick(w, p, g_inv);
        const Entity& e = g_entities[slot];
        if (e.type == ENT_NONE) return false;
        if (e.centreX() > wallR + 12) return true;
    }
    return false;
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();

    const bool bat = crosses(ENT_BAT);
    const bool moth = crosses(ENT_MOTH);
    printf("body-sized detour: bat %s, moth %s\n",
           bat ? "passed" : "STUCK", moth ? "passed" : "STUCK");
    if (!bat || !moth) return 1;
    puts("PASS");
    return 0;
}
