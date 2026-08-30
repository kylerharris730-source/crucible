#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "player.h"
#include "multiplayer.h"
#include "render.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* The first layer-two enemy is also the first enemy to use the armature. This
   harness keeps those two promises together: it must be selected by a layer-2
   spawn, occupy a genuinely larger animated silhouette, and leave typed Ichor
   behind rather than an invalid material byte. Compile with all source files
   except main.cpp. */

static void makeLayerTwoRooms(World& w, int x0, int y0, int x1, int y1) {
    w.setZoneRect(x0, y0, x1, y1, ZONE_LAYER2);
    /* A floor every 64 cells gives every random spawn probe a floor within the
       spawner's drop search while leaving 63 cells of clear vertical room --
       enough for the 36-cell Shambler without turning the test into one lucky
       coordinate. */
    for (int y = y0; y <= y1; ++y) {
        if ((y - y0) % 64 != 63) continue;
        for (int x = x0; x <= x1; ++x) w.setCell(x, y, MAT_STONE);
    }
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();
    entReset();
    g_world.reset();

    int failures = 0;
    const EntityDef& d = ENT_DEFS[ENT_SHAMBLER];
    if (d.layerMask != (1u << 1) || d.surfaceAtNight || d.isBoss) {
        fprintf(stderr, "Shambler spawn flags do not describe a normal layer-2 enemy\n");
        ++failures;
    }
    if (d.w != SHAMBLER_SPR_W || d.h != SHAMBLER_SPR_H ||
        d.eggItem != ITEM_EGG_SHAMBLER || d.dropItem != ITEM_ICHOR) {
        fprintf(stderr, "Shambler definition disagrees with its rig, egg, or loot\n");
        ++failures;
    }
    if (ITEMS[ITEM_ICHOR].kind != ITEMK_COMPONENT ||
        ITEMS[ITEM_ICHOR].sprite != SPR_ICHOR || ITEMS[ITEM_ICHOR].maxStack < 2) {
        fprintf(stderr, "Ichor is not configured as a stackable component\n");
        ++failures;
    }
    if (ITEMS[ITEM_EGG_SHAMBLER].kind != ITEMK_EGG ||
        ITEMS[ITEM_EGG_SHAMBLER].summons != ENT_SHAMBLER) {
        fprintf(stderr, "Shambler egg does not summon the Shambler\n");
        ++failures;
    }

    /* Actual spawner selection, not only a table assertion. Layer two has one
       eligible species today, so every natural result must be a Shambler. */
    const int camX = 1200, camY = 3400;
    makeLayerTwoRooms(g_world, camX - 180, camY - 180,
                      camX + VIEW_CELLS_W + 180, camY + VIEW_CELLS_H + 180);
    Player& p = g_player;
    p.reset((float)(camX + VIEW_CELLS_W / 2),
            (float)(camY + VIEW_CELLS_H / 2));
    p.alive = true;
    g_rng = 0x51A8B1E2u;
    for (int frame = 0; frame < 1200 && entAliveCount() < 2; ++frame)
        entSpawnTick(g_world, p, camX, camY, false);
    if (entAliveCount() == 0) {
        fprintf(stderr, "layer-2 spawner produced no creature in prepared rooms\n");
        ++failures;
    }
    /* Everything the layer-2 spawner produces must BE a layer-2 creature.
       This used to assert the Shambler specifically, which was the same claim
       only while it was the only creature down here -- adding the Thresher
       then made it fail or pass depending on the seed. What the check is
       actually worth is that layer 1 does not leak into layer 2, and that
       survives every creature added after this one. */
    for (int i = 0; i < MAX_ENTITIES; ++i)
        if (g_entities[i].alive() &&
            !(ENT_DEFS[g_entities[i].type].layerMask & 2)) {
            fprintf(stderr, "layer-2 spawner selected %s, which is not a layer-2 creature\n",
                    ENT_DEFS[g_entities[i].type].name);
            ++failures;
        }

    /* The rig must cover more than the legacy 14-cell creature canvas and the
       walk must change the actual baked silhouette, not merely its position. */
    entReset();
    g_world.frame = 0;
    const int s = entSpawn(g_world, ENT_SHAMBLER, 120.0f, 120.0f);
    if (s < 0) { fprintf(stderr, "could not spawn Shambler for render check\n"); return 2; }
    Entity& e = g_entities[s];
    e.onGround = true; e.vx = d.speed; e.vy = 0.0f; e.facing = 1;
    static u32 first[VIEW_CELLS_W * VIEW_CELLS_H];
    static u32 second[VIEW_CELLS_W * VIEW_CELLS_H];
    e.walkPhase = 0.0f;
    entDraw(first, 0, 0, false);
    e.walkPhase = (float)(d.h / 3) / 4.0f + 0.5f;
    entDraw(second, 0, 0, false);
    int changed = 0, minX = VIEW_CELLS_W, maxX = -1, lit = 0;
    for (int y = 0; y < VIEW_CELLS_H; ++y)
        for (int x = 0; x < VIEW_CELLS_W; ++x) {
            const int k = y * VIEW_CELLS_W + x;
            if (first[k] != second[k]) ++changed;
            if (first[k] || second[k]) {
                ++lit; if (x < minX) minX = x; if (x > maxX) maxX = x;
            }
        }
    if (changed < 20 || lit < 120 || maxX - minX + 1 <= SPR_W) {
        fprintf(stderr, "Shambler rig is too small or static: %d changed, %d lit, width %d\n",
                changed, lit, maxX - minX + 1);
        ++failures;
    }

    /* Killing it must create one typed pickup stack of 2..4 Ichor. This also
       guards the ItemId/MatId boundary: writing ITEM_ICHOR through setCell as
       a u8 would leave no pickup and corrupt the material lookup instead. */
    e.hp = 0;
    entTick(g_world, p, g_inv);
    int ichorStacks = 0, ichorCount = 0;
    for (int i = 0; i < MAX_PICKUPS; ++i) if (g_pickups[i].used) {
        if (g_pickups[i].item == ITEM_ICHOR) {
            ++ichorStacks;
            ichorCount += g_pickups[i].count;
        }
    }
    if (ichorStacks != 1 || ichorCount < 2 || ichorCount > 4) {
        fprintf(stderr, "Shambler dropped %d Ichor in %d stacks\n", ichorCount, ichorStacks);
        ++failures;
    }

    /* It is not only art and loot: in an ordinary corridor the large body has
       to close the distance and deliver its configured contact hit. This is a
       deliberately simple route; the shared maze/path field has its own much
       harsher enemy_path harness. */
    entReset();
    const int floorY = 900;
    for (int x = 500; x <= 1050; ++x) g_world.setCell(x, floorY, MAT_STONE);
    p.reset(560.0f, (float)(floorY - PLAYER_H));
    p.alive = true; p.hp = PLAYER_HP_MAX;
    const int hunter = entSpawn(g_world, ENT_SHAMBLER, 900.0f,
                                (float)(floorY - d.h / 2));
    if (hunter < 0) {
        fprintf(stderr, "could not spawn Shambler for combat check\n");
        ++failures;
    } else {
        const float startX = g_entities[hunter].x;
        float slowestStep = 1000.0f, fastestStep = 0.0f;
        int groundedSteps = 0;
        bool contacted = false;
        for (int frame = 0; frame < 1800 && g_entities[hunter].alive(); ++frame) {
            const float beforeX = g_entities[hunter].x;
            ++g_world.frame;
            p.alive = true;
            entTick(g_world, p, g_inv);
            const float step = fabsf(g_entities[hunter].x - beforeX);
            if (g_entities[hunter].onGround && step > 0.01f) {
                if (step < slowestStep) slowestStep = step;
                if (step > fastestStep) fastestStep = step;
                ++groundedSteps;
            }
            if (p.hp < PLAYER_HP_MAX) { contacted = true; break; }
        }
        if (!contacted || g_entities[hunter].x > startX - 200.0f) {
            fprintf(stderr, "Shambler did not close and hit: moved %.1f cells, hp %d\n",
                    startX - g_entities[hunter].x, p.hp);
            ++failures;
        }
        if (groundedSteps < 100 || fastestStep - slowestStep < 0.10f) {
            fprintf(stderr, "Shambler still slides evenly: %d steps, %.3f..%.3f cells\n",
                    groundedSteps, slowestStep, fastestStep);
            ++failures;
        }
    }

    /* An unreachable player above a tall wall keeps the route uphill. That
       used to relaunch the Shambler as soon as it landed. It should still make
       a real attempt at the obstruction, then spend most of the interval on
       the ground instead of pogoing against it. */
    entReset();
    const int jumpFloor = 1200, wallX = 1400;
    for (int x = 1200; x <= 1580; ++x)
        g_world.setCell(x, jumpFloor, MAT_STONE);
    for (int y = jumpFloor - 100; y < jumpFloor; ++y)
        g_world.setCell(wallX, y, MAT_STONE);
    p.reset(1480.0f, (float)(jumpFloor - PLAYER_H - 45));
    p.alive = true;
    const int jumper = entSpawn(g_world, ENT_SHAMBLER, 1300.0f,
                                (float)(jumpFloor - d.h / 2));
    int jumps = 0;
    if (jumper < 0) {
        fprintf(stderr, "could not spawn Shambler for jump-cadence check\n");
        ++failures;
    } else {
        for (int frame = 0; frame < 600 && g_entities[jumper].alive(); ++frame) {
            const float beforeVy = g_entities[jumper].vy;
            ++g_world.frame;
            entTick(g_world, p, g_inv);
            if (beforeVy >= 0.0f && g_entities[jumper].vy < 0.0f) ++jumps;
        }
        if (jumps < 1 || jumps > 4) {
            fprintf(stderr, "Shambler jump cadence is wrong: %d jumps in 600 frames\n",
                    jumps);
            ++failures;
        }
    }

    if (failures) {
        fprintf(stderr, "%d Shambler check(s) failed\n", failures);
        return 1;
    }
    printf("layer-2 Shambler spawned, animated, and dropped %d Ichor\n", ichorCount);
    return 0;
}
