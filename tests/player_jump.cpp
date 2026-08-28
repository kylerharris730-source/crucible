/* Variable-height jump regression. A tap remains the compact old hop, while a
   held Up/Jump clears more than the player's own height. Compile with all game
   source files except main.cpp. */
#include "world.h"
#include "materials.h"
#include "player.h"
#include "device.h"
#include <stdio.h>

static World g_jumpWorld;

static float jumpHeight(bool held) {
    const int floorY = 500;
    g_jumpWorld.reset();
    for (int y = floorY; y <= floorY + 8; ++y)
        for (int x = 300; x <= 500; ++x)
            g_jumpWorld.setCell(x, y, MAT_STONE);

    Player p;
    p.reset(400.0f, (float)floorY - PLAYER_H * 0.5f);
    PlayerInput input = {};
    for (int frame = 0; frame < 8 && !p.onGround; ++frame) p.update(g_jumpWorld, input);
    if (!p.onGround) return -1.0f;

    const float startY = p.y;
    float highestY = p.y;
    bool airborne = false;
    for (int frame = 0; frame < 240; ++frame) {
        input.jump = held || frame == 0;
        p.update(g_jumpWorld, input);
        if (p.y < highestY) highestY = p.y;
        if (!p.onGround) airborne = true;
        if (airborne && p.onGround) break;
    }
    return startY - highestY;
}

int main() {
    initMaterials();
    devClear();
    const float tapped = jumpHeight(false);
    const float held = jumpHeight(true);
    printf("jump height: tap %.2f, held %.2f, body %d\n", tapped, held, PLAYER_H);
    if (tapped < 16.0f || tapped >= (float)PLAYER_H) {
        fprintf(stderr, "FAIL: tap jump lost its short-hop range\n"); return 1;
    }
    if (held <= (float)PLAYER_H || held < tapped + 10.0f) {
        fprintf(stderr, "FAIL: held jump does not clear one player height\n"); return 2;
    }
    puts("PASS");
    return 0;
}
