#include "drone.h"
#include "entity.h"
#include "light.h"
#include "projectile.h"
#include "render.h"
#include <math.h>
#include <string.h>

Drone g_drones[MAX_DRONES];
/* A pair of basic iron drones should assist a fight, not replace the starter
   weapon. Their reliable aim remains useful; lower damage is the cleanest
   lever because it preserves that feel and their readable firing cadence. */
static const int ATTACK_DRONE_DAMAGE = 3;
static const int ATTACK_DRONE_COOLDOWN = 28;
static const int OVERCLOCK_DRONE_COOLDOWN = 20;

static bool hasChip(const Inventory& inv, int droneBay, ItemId item) {
    for (int i = 0; i < Inventory::DRONE_MODULE_SLOTS_MAX; ++i)
        if (inv.droneModule[droneBay][i].item == item) return true;
    return false;
}

void droneReset() { memset(g_drones, 0, sizeof(g_drones)); }

static u8 equippedDrone(const Inventory& inv, int i) {
    const int slot = i == 0 ? EQ_LIGHT_DRONE : (i == 1 ? EQ_DRONE_A : EQ_DRONE_B);
    if (inv.equip[slot].empty()) return DRONE_NONE;
    return inv.equip[slot].item == ITEM_LIGHT_DRONE ? DRONE_LIGHT
         : inv.equip[slot].item == ITEM_ATTACK_DRONE ? DRONE_ATTACK
         : inv.equip[slot].item == ITEM_PICKUP_DRONE ? DRONE_PICKUP
         : inv.equip[slot].item == ITEM_SHIELD_DRONE ? DRONE_SHIELD : DRONE_NONE;
}

static Entity* nearestEnemy(float x, float y) {
    Entity* best = 0;
    float best2 = 220.0f * 220.0f;
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        Entity& e = g_entities[i];
        if (!e.alive()) continue;
        const float dx = e.centreX() - x, dy = e.centreY() - y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < best2) { best2 = d2; best = &e; }
    }
    return best;
}

static void pickupCollect(Drone& d, Inventory& inv) {
    static const float RANGE = 110.0f;
    for (int i = 0; i < MAX_PICKUPS; ++i) {
        Pickup& drop = g_pickups[i];
        if (!drop.used) continue;
        const float dx = drop.x - d.x, dy = drop.y - d.y;
        if (dx * dx + dy * dy > RANGE * RANGE) continue;
        const int left = inv.add(drop.item, drop.count);
        drop.count = (i16)left;
        if (drop.count == 0) drop.used = false;
    }
}

static void shieldIntercept(const Drone& d) {
    static const float RADIUS = 13.0f;
    for (int i = 0; i < MAX_PROJ; ++i) {
        Projectile& shot = g_proj[i];
        if (!shot.alive || !shot.hostile) continue;
        const float dx = shot.x - d.x, dy = shot.y - d.y;
        if (dx * dx + dy * dy <= RADIUS * RADIUS) shot.alive = false;
    }
}

void droneTick(const World&, const Player& p, Inventory& inv) {
    for (int i = 0; i < MAX_DRONES; ++i) {
        Drone& d = g_drones[i];
        const u8 want = equippedDrone(inv, i);
        if (d.type != want) {
            memset(&d, 0, sizeof(d));
            d.type = want;
            d.x = p.centreX() + (float)(i * 12 - 12);
            d.y = p.centreY() - 26.0f;
        }
        if (d.type == DRONE_NONE) continue;

        float tx, ty;
        if (d.type == DRONE_SHIELD) {
            d.shotCool = (d.shotCool + 4) % 360;
            const float a = (float)d.shotCool * 3.14159265f / 180.0f;
            tx = p.centreX() + cosf(a) * 28.0f;
            ty = p.centreY() + sinf(a) * 20.0f;
        } else {
            const float orbit = (float)(i * 2 - 2);
            tx = p.centreX() + orbit + (float)p.facing * 14.0f;
            ty = p.centreY() - 24.0f - (float)(i & 1) * 8.0f;
        }
        d.vx = (d.vx + (tx - d.x) * 0.10f) * 0.78f;
        d.vy = (d.vy + (ty - d.y) * 0.10f) * 0.78f;
        d.x += d.vx; d.y += d.vy;

        if (d.type == DRONE_ATTACK) {
            const bool overclock = hasChip(inv, i, ITEM_OVERCLOCK_CHIP);
            const bool twin      = hasChip(inv, i, ITEM_TWIN_CONTROLLER);
            if (d.shotCool > 0) --d.shotCool;
            Entity* target = nearestEnemy(d.x, d.y);
            if (target && d.shotCool == 0) {
                float dx = target->centreX() - d.x, dy = target->centreY() - d.y;
                const float len = sqrtf(dx * dx + dy * dy);
                if (len > 0.01f) {
                    dx *= 4.8f / len; dy *= 4.8f / len;
                    projSpawn(d.x, d.y, dx, dy, 0, 0, 70, 0xF2C16D, 0,
                              MAT_EMPTY, ATTACK_DRONE_DAMAGE, false, 0.0f);
                    if (twin) {
                        /* The controller duplicates a firing command, not the
                           drone itself: both bolts originate from the same
                           companion but fan out enough to read as two shots. */
                        const float px = -dy * 0.10f, py = dx * 0.10f;
                        projSpawn(d.x, d.y, dx + px, dy + py, 0, 0, 70, 0xD8A4FF, 0,
                                  MAT_EMPTY, ATTACK_DRONE_DAMAGE, false, 0.0f);
                    }
                    d.shotCool = overclock ? OVERCLOCK_DRONE_COOLDOWN : ATTACK_DRONE_COOLDOWN;
                }
            }
        }
        if (d.type == DRONE_PICKUP) pickupCollect(d, inv);
        if (d.type == DRONE_SHIELD) {
            shieldIntercept(d);
            if (d.effectCool > 0) --d.effectCool;
            if (d.effectCool == 0) {
                /* The shield's base pulse is deliberately a nudge, not a
                   weapon: it buys breathing room around the player. Garlic
                   adds a second, smaller disc around the drone itself. */
                entDamageKnockbackDisc((int)p.centreX(), (int)p.centreY(), 34, 1, 1.25f);
                if (hasChip(inv, i, ITEM_GARLIC_FIELD_CHIP))
                    entDamageDisc((int)d.x, (int)d.y, 14, 1);
                d.effectCool = 24;
            }
        }
    }
}

void droneRegisterLights() {
    for (int i = 0; i < MAX_DRONES; ++i)
        if (g_drones[i].type == DRONE_LIGHT)
            lightAddDynamic((int)g_drones[i].x, (int)g_drones[i].y, 210);
}

void droneDraw(u32* px, int camX, int camY, bool lit) {
    for (int i = 0; i < MAX_DRONES; ++i) {
        const Drone& d = g_drones[i];
        if (d.type == DRONE_NONE) continue;
        const int x = (int)d.x - camX, y = (int)d.y - camY;
        const u32 c = d.type == DRONE_LIGHT  ? 0xA8EEFF
                    : d.type == DRONE_ATTACK ? 0xE6A060
                    : d.type == DRONE_PICKUP ? 0x8CE8B0 : 0x86B8FF;
        for (int oy = -2; oy <= 2; ++oy) for (int ox = -2; ox <= 2; ++ox) {
            if (ox * ox + oy * oy > 5) continue;
            const int vx = x + ox, vy = y + oy;
            if (vx < 0 || vx >= VIEW_CELLS_W || vy < 0 || vy >= VIEW_CELLS_H) continue;
            px[vy * VIEW_CELLS_W + vx] = lit ? shadeColor(c, viewShade(vx, vy)) : c;
        }
    }
}

int droneCount() {
    int n = 0;
    for (int i = 0; i < MAX_DRONES; ++i) if (g_drones[i].type != DRONE_NONE) ++n;
    return n;
}
