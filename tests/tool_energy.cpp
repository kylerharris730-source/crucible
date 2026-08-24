#include "world.h"
#include "materials.h"
#include "item.h"
#include "entity.h"
#include "projectile.h"
#include "multiplayer.h"
#include <stdio.h>

int main() {
    initMaterials(); initItems(); playerSessionsReset();
    static World w; w.reset();

    Inventory& inv = g_playerSessions[0].inventory;
    if (inv.add(ITEM_MULTITOOL, 1) != 0) return 1;
    ItemStack& tool = inv.slot[0];
    ToolInst& ti = g_toolInst[tool.inst];
    ti.slot[0] = ITEM_MOD_SHOT;
    ti.slot[1] = ITEM_MOD_BOUNCE;
    ti.slot[2] = ITEM_MOD_BLAST;
    if (ti.energy != 120 || ti.energyCapacity != 120 || ti.energyRecharge != 1) {
        fprintf(stderr, "Mk I battery was not initialized (%u/%u +%u)\n",
                ti.energy, ti.energyCapacity, ti.energyRecharge); return 2;
    }
    if (ITEMS[ITEM_MOD_HOMING].energyCost != 55) {
        fprintf(stderr, "homing module energy balance drifted (%u)\n",
                (unsigned)ITEMS[ITEM_MOD_HOMING].energyCost);
        return 16;
    }

    const ItemId expected[3] = { ITEM_MOD_SHOT, ITEM_MOD_BOUNCE, ITEM_MOD_BLAST };
    for (int i = 0; i < 3; ++i) {
        ti.shotCursor = (u8)i;
        const ToolShot s = toolResolve(tool);
        if (!s.canFire || s.moduleSlot != i || ti.slot[s.moduleSlot] != expected[i]) {
            fprintf(stderr, "module sequence did not resolve slot %d\n", i); return 3;
        }
    }
    ti.shotCursor = 0; ti.energy = ti.energyCapacity;
    const int cost[3] = { 20, 7, 65 };
    int remaining = ti.energy;
    for (int i = 0; i < 3; ++i) {
        const ToolShot s = toolResolve(tool);
        if (s.moduleSlot != i || !toolShotEnergyAvailable(tool, s)) return 12;
        toolCommitShot(tool, s, s.delay);
        remaining -= cost[i];
        if (ti.energy != remaining || ti.shotCursor != (u8)((i + 1) % 3)) {
            fprintf(stderr, "successful shot did not spend energy/advance (%u, %u)\n",
                    ti.energy, ti.shotCursor); return 13;
        }
    }
    ti.energy = 0;
    const ToolShot blocked = toolResolve(tool);
    if (toolShotEnergyAvailable(tool, blocked)) return 14;
    const u8 heldCursor = ti.shotCursor;
    toolCommitShot(tool, blocked, blocked.delay);
    if (ti.shotCursor != heldCursor || ti.energy != 0) {
        fprintf(stderr, "empty battery skipped a module in the sequence\n"); return 15;
    }
    ti.energy = 0;
    for (int i = 0; i < 10; ++i) toolInstTick();
    if (ti.energy != 10) {
        fprintf(stderr, "battery recharge was not ten charge in ten frames (%u)\n", ti.energy);
        return 4;
    }

    ti.shotCursor = 1;
    const ToolShot gravityBounce = toolResolve(tool);
    if (gravityBounce.moduleSlot != 1 || gravityBounce.gravity != PROJ_GRAVITY) {
        fprintf(stderr, "bounce module did not inherit ordinary gravity (%.3f)\n",
                gravityBounce.gravity);
        return 18;
    }

    projClear();
    for (int y = 470; y <= 530; ++y) w.setCell(520, y, MAT_STONE);
    if (!projSpawn(500.5f, 500.5f, 5.5f, 0.0f, 0, 1, 60, 0x72E09A,
                   0, MAT_EMPTY, 4, false, 0.0f, PROJ_EFFECT_NONE, 2)) return 5;
    for (int i = 0; i < 5; ++i) projUpdate(w);
    if (!g_proj[0].alive || g_proj[0].vx >= 0.0f || g_proj[0].bounces != 1) {
        fprintf(stderr, "bounce shot did not ricochet (%d, %.2f)\n",
                g_proj[0].bounces, g_proj[0].vx); return 6;
    }

    /* A thick digital diagonal should reflect as one sloped surface, not as
       whichever horizontal/vertical pixel face traversal happened to enter. */
    w.reset(); projClear();
    for (int x = 505; x <= 535; ++x) {
        const int y = 1040 - x;
        for (int thick = -2; thick <= 2; ++thick) w.setCell(x, y + thick, MAT_STONE);
    }
    if (!projSpawn(500.5f, 500.5f, 4.0f, 4.0f, 0, 1, 60, 0x72E09A,
                   0, MAT_EMPTY, 4, false, PROJ_GRAVITY, PROJ_EFFECT_NONE, 2)) return 19;
    for (int i = 0; i < 8 && g_proj[0].bounces == 2; ++i) projUpdate(w);
    if (!g_proj[0].alive || g_proj[0].bounces != 1 ||
        g_proj[0].vx >= 0.0f || g_proj[0].vy >= 0.0f) {
        fprintf(stderr, "diagonal surface did not produce a fitted normal (%d, %.2f, %.2f)\n",
                g_proj[0].bounces, g_proj[0].vx, g_proj[0].vy);
        return 20;
    }

    w.reset(); entReset(); projClear();
    if (entSpawn(w, ENT_BAT, 515.0f, 530.0f) < 0) return 7;
    if (!projSpawn(500.0f, 500.0f, 1.65f, 0.0f, 0, 1, 120, 0xD59CFF,
                   0, MAT_EMPTY, 9, false, 0.0f, PROJ_EFFECT_NONE, 0, 0.085f)) return 8;
    for (int i = 0; i < 8; ++i) projUpdate(w);
    if (!g_proj[0].alive || g_proj[0].vy <= 0.15f) {
        fprintf(stderr, "homing shot did not curve toward target (vy %.3f)\n", g_proj[0].vy);
        return 9;
    }
    if (projTrailMoteCount() < 4) {
        fprintf(stderr, "projectile did not leave a persistent particle wake (%d)\n",
                projTrailMoteCount());
        return 17;
    }

    w.reset(); entReset(); projClear();
    for (int y = 450; y <= 550; ++y) w.setCell(540, y, MAT_STONE);
    Player& p = g_playerSessions[0].body; p.reset(480.0f, 500.0f); p.hp = 63;
    if (!projSpawn(500.0f, 500.0f, 4.0f, 0.0f, 0, 1, 60, 0xFF72D8,
                   0, MAT_EMPTY, 0, false, 0.0f, PROJ_EFFECT_TELEPORT,
                   0, 0.0f, LOCAL_PLAYER_ID)) return 10;
    for (int i = 0; i < 12; ++i) projUpdate(w);
    if (p.centreX() <= 500.0f || p.centreX() >= 540.0f || p.hp != 63) {
        fprintf(stderr, "teleport shot failed or healed owner (x %.1f hp %d)\n",
                p.centreX(), p.hp); return 11;
    }

    puts("tool energy, sequence, bounce, homing, and teleport passed");
    return 0;
}
