#include "drone.h"
#include "entity.h"
#include "light.h"
#include "projectile.h"
#include "render.h"
#include <math.h>
#include <string.h>

/* Cells per frame. Named because the interception solve below needs the same
   number the bolt is actually given -- two copies of it drifting apart would
   make the drone aim at a point its own shot never reaches. */
static const float DRONE_SHOT_SPEED = 4.8f;
/* How much of the computed lead to apply, and how far ahead it may look.
   Under one on purpose: see the note at the solve. The cap stops a nearly
   stationary relative velocity producing an enormous t and an aim pointing
   off into nothing. */
static const float LEAD_FRACTION   = 0.75f;
static const float LEAD_MAX_FRAMES = 40.0f;

static const int ATTACK_DRONE_DAMAGE = 3;
static const int ATTACK_DRONE_COOLDOWN = 28;
static const int OVERCLOCK_DRONE_COOLDOWN = 20;
static const float ATTACK_ACQUIRE_RADIUS = 220.0f;
static const float PICKUP_GRAB_RADIUS = 7.0f;
static const float LIGHT_WALL_CLEARANCE = 6.0f;
/* The bolt head is visibly 3x3. A two-cell circular sweep includes that width
   plus one cell of grace at the cardinal edges, keeping diagonal terrain from
   looking as though the drone deliberately grazed it. */
static const int ATTACK_SHOT_CLEARANCE = 2;

/* Last player position supplied to droneTick. The two envelopes are steering
   rules only; showing them permanently adds a large amount of visual noise. */
static float g_taskX, g_taskY, g_homeX, g_homeY;

static bool hasChip(const Inventory& inv, int droneBay, ItemId item) {
    for (int i = 0; i < Inventory::DRONE_MODULE_SLOTS_MAX; ++i)
        if (inv.droneModule[droneBay][i].item == item) return true;
    return false;
}

void droneReset() {
    for (int player = 0; player < MAX_PLAYERS; ++player)
        memset(g_playerSessions[player].drones, 0, sizeof(g_playerSessions[player].drones));
}

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
    float best2 = ATTACK_ACQUIRE_RADIUS * ATTACK_ACQUIRE_RADIUS;
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        Entity& e = g_entities[i];
        if (!e.alive()) continue;
        const float dx = e.centreX() - x, dy = e.centreY() - y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < best2) { best2 = d2; best = &e; }
    }
    return best;
}

static Pickup* nearestPickupInEnvelope(float px, float py, float x, float y) {
    Pickup* best = 0;
    float best2 = 1e30f;
    const float task2 = (float)(DRONE_TASK_RADIUS * DRONE_TASK_RADIUS);
    for (int i = 0; i < MAX_PICKUPS; ++i) {
        Pickup& drop = g_pickups[i];
        if (!drop.used) continue;
        const float pdx = drop.x - px, pdy = drop.y - py;
        if (pdx * pdx + pdy * pdy > task2) continue;
        const float dx = drop.x - x, dy = drop.y - y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < best2) { best2 = d2; best = &drop; }
    }
    return best;
}

static void pickupCollect(Drone& d, Inventory& inv) {
    const float grab2 = PICKUP_GRAB_RADIUS * PICKUP_GRAB_RADIUS;
    for (int i = 0; i < MAX_PICKUPS; ++i) {
        Pickup& drop = g_pickups[i];
        if (!drop.used) continue;
        const float dx = drop.x - d.x, dy = drop.y - d.y;
        if (dx * dx + dy * dy > grab2) continue;
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

static bool shotOpen(const World& w, int x, int y) {
    if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) return false;
    const u8 m = w.at(x, y).mat;
    return m == MAT_EMPTY || m == MAT_TORCH || g_matStrength[m] == STR_NOTHING;
}

/* The same promise the actual projectile makes, checked before choosing where
   to hover. Bresenham visits every crossed row/column cheaply; the target cell
   is excluded because the creature lives in its empty cell as an overlay. */
static bool clearShot(const World& w, float fx, float fy, float tx, float ty) {
    int x = (int)fx, y = (int)fy;
    const int ex = (int)tx, ey = (int)ty;
    int dx = ex - x; if (dx < 0) dx = -dx;
    int dy = ey - y; if (dy < 0) dy = -dy;
    const int sx = x < ex ? 1 : -1, sy = y < ey ? 1 : -1;
    int err = dx - dy;
    bool first = true;
    for (;;) {
        if (x == ex && y == ey) return true;
        if (!first && !shotOpen(w, x, y)) return false;
        first = false;
        const int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 <  dx) { err += dx; y += sy; }
    }
}

/* Attack aiming is intentionally stricter than the point collision used by
   projectile simulation. Sample a small disc around the flight centreline so
   the entire visible bolt clears curved and diagonal walls. Stop at the near
   edge of the enemy: terrain beside or behind a body should not make the body
   impossible to shoot. */
static bool clearAttackShot(const World& w, float fx, float fy,
                            const Entity& target) {
    const float tx = target.centreX(), ty = target.centreY();
    const float dx = tx - fx, dy = ty - fy;
    const float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.01f) return true;
    const float bodyRadius = (float)imin(target.width(), target.height()) * 0.5f;
    const float clearLength = fmaxf(0.0f, length - bodyRadius -
                                           (float)ATTACK_SHOT_CLEARANCE);
    /* One sample per travelled cell is enough because each sample checks a
       radius-two disc; neighbouring discs overlap generously even diagonally. */
    const int steps = imax(1, (int)ceilf(clearLength));
    for (int i = 0; i <= steps; ++i) {
        const float travel = clearLength * (float)i / (float)steps;
        const int cx = (int)(fx + dx / length * travel);
        const int cy = (int)(fy + dy / length * travel);
        for (int oy = -ATTACK_SHOT_CLEARANCE; oy <= ATTACK_SHOT_CLEARANCE; ++oy)
            for (int ox = -ATTACK_SHOT_CLEARANCE; ox <= ATTACK_SHOT_CLEARANCE; ++ox) {
                if (ox * ox + oy * oy > ATTACK_SHOT_CLEARANCE * ATTACK_SHOT_CLEARANCE)
                    continue;
                if (!shotOpen(w, cx + ox, cy + oy)) return false;
            }
    }
    return true;
}

/* A light source in rock is worse than no companion at all: it spends its
   output illuminating an opaque cell. Trace FROM the player toward the drone
   and stop at the last open cell before the first obstruction. That detail is
   important. Searching around the drone for the geometrically nearest empty
   cell can put it in a sealed pocket on the wrong side of the wall, where it
   is technically in air and still lights none of the space the player sees. */
static bool playerSideOpenPoint(const World& w, const Player& p, const Drone& d,
                                float* outX, float* outY) {
    const float sx = p.centreX(), sy = p.centreY();
    const bool embedded = !shotOpen(w, (int)d.x, (int)d.y);
    if (!embedded && clearShot(w, d.x, d.y, sx, sy)) return false;

    const float dx = d.x - sx, dy = d.y - sy;
    const float length = sqrtf(dx * dx + dy * dy);
    int steps = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy)) * 2.0f);
    if (steps < 1) steps = 1;

    bool haveOpen = false;
    float lastX = sx, lastY = sy;
    for (int i = 0; i <= steps; ++i) {
        const float t = (float)i / (float)steps;
        const float x = sx + dx * t, y = sy + dy * t;
        const int cx = (int)x, cy = (int)y;
        if (!shotOpen(w, cx, cy)) break;
        lastX = x; lastY = y;
        haveOpen = true;
    }
    if (!haveOpen) return false;
    /* Do not aim at the cell touching the wall: that produced a light drone
       which technically escaped and then skated along the surface. Continue
       several cells into the player's side of the room. Every point on this
       part of the ray was already checked open above. */
    const float openLength = sqrtf((lastX - sx) * (lastX - sx) +
                                   (lastY - sy) * (lastY - sy));
    const float retreat = fminf(LIGHT_WALL_CLEARANCE, openLength);
    if (length > 0.01f) {
        lastX -= dx / length * retreat;
        lastY -= dy / length * retreat;
    }
    *outX = lastX; *outY = lastY;
    return true;
}

/* Once it has a clear line to the player, a light drone gently favours the
   middle of open space. This is deliberately a force rather than a snapped
   destination: it rounds motion away from walls without making the follower
   hop between a grid of candidate cells. During an obstruction recovery this
   is disabled, otherwise it would resist crossing the very wall it must pass. */
static void repelLightFromSurfaces(const World& w, Drone& d) {
    static const int R = 6;
    if (!shotOpen(w, (int)d.x, (int)d.y)) return;
    float pushX = 0.0f, pushY = 0.0f;
    const int cx = (int)d.x, cy = (int)d.y;
    for (int oy = -R; oy <= R; ++oy) for (int ox = -R; ox <= R; ++ox) {
        if (ox == 0 && oy == 0) continue;
        const int d2 = ox * ox + oy * oy;
        if (d2 > R * R || shotOpen(w, cx + ox, cy + oy)) continue;
        const float dist = sqrtf((float)d2);
        const float weight = ((float)R + 0.5f - dist) / ((float)R * dist);
        pushX -= (float)ox / dist * weight;
        pushY -= (float)oy / dist * weight;
    }
    const float mag = sqrtf(pushX * pushX + pushY * pushY);
    if (mag > 0.001f) {
        const float force = fminf(0.24f, mag * 0.055f);
        d.vx += pushX / mag * force;
        d.vy += pushY / mag * force;
    }
}

/* Search a small deterministic set of positions inside the task envelope.
   This is positioning, not terrain pathfinding: drones are flying overlays and
   may cross rock on the way there. The chosen endpoint must be open and must
   give the actual projectile a clear line to its target. */
static bool firingPosition(const World& w, const Player& p, const Drone& d,
                           const Entity& target, int bay, float* outX, float* outY) {
    static const float TAU = 6.28318530718f;
    static const float RADII[3] = { 0.32f, 0.62f, 0.92f };
    static const int ANGLES = 20;
    float bestScore = 1e30f;
    bool found = false;
    for (int ring = 0; ring < 3; ++ring) for (int a = 0; a < ANGLES; ++a) {
        const float angle = TAU * (float)a / (float)ANGLES + (float)bay * 0.19f;
        const float r = (float)DRONE_TASK_RADIUS * RADII[ring];
        const float x = p.centreX() + cosf(angle) * r;
        const float y = p.centreY() + sinf(angle) * r;
        if (!shotOpen(w, (int)x, (int)y) || !clearAttackShot(w, x, y, target)) continue;
        const float dx = x - d.x, dy = y - d.y;
        const float score = dx * dx + dy * dy;
        if (score < bestScore) { bestScore = score; *outX = x; *outY = y; found = true; }
    }
    return found;
}

/* Arrival steering: desired speed falls with the remaining distance, while
   response rises with it. Far-away drones accelerate hard; near the target
   they ask for a small velocity and brake instead of orbiting or overshooting. */
static void steerArrive(Drone& d, float tx, float ty, float maxSpeed, float slowRadius) {
    const float dx = tx - d.x, dy = ty - d.y;
    const float dist2 = dx * dx + dy * dy;
    if (dist2 < 0.01f) { d.vx *= 0.78f; d.vy *= 0.78f; return; }
    const float dist = sqrtf(dist2);
    float speed = maxSpeed;
    if (dist < slowRadius) speed *= dist / slowRadius;
    const float wantX = dx * speed / dist, wantY = dy * speed / dist;
    float response = 0.07f + dist * 0.0022f;
    if (response > 0.30f) response = 0.30f;
    d.vx += (wantX - d.vx) * response;
    d.vy += (wantY - d.vy) * response;
}

static void addSeparation(Drone* drones, Drone& d, int self) {
    for (int i = 0; i < MAX_DRONES; ++i) {
        if (i == self || drones[i].type == DRONE_NONE) continue;
        const float dx = d.x - drones[i].x, dy = d.y - drones[i].y;
        const float d2 = dx * dx + dy * dy;
        if (d2 >= 100.0f || d2 < 0.01f) continue;
        const float dist = sqrtf(d2);
        const float push = (10.0f - dist) * 0.012f;
        d.vx += dx * push / dist; d.vy += dy * push / dist;
    }
}

static void droneTickBank(Drone* drones, const World& w, const Player& p, Inventory& inv) {
    g_taskX = p.centreX(); g_taskY = p.centreY();
    g_homeX = p.centreX(); g_homeY = (float)p.top() - (float)DRONE_HOME_ABOVE;

    for (int i = 0; i < MAX_DRONES; ++i) {
        Drone& d = drones[i];
        const u8 want = equippedDrone(inv, i);
        if (d.type != want) {
            memset(&d, 0, sizeof(d));
            d.type = want;
            /* New companions arrive inside the home pocket rather than
               teleporting on top of each other at the player's centre. */
            d.x = g_homeX + (float)(i - 1) * 7.0f;
            d.y = g_homeY + (float)(i & 1) * 4.0f;
        }
        if (d.type == DRONE_NONE) continue;

        Entity* enemy = d.type == DRONE_ATTACK ? nearestEnemy(g_taskX, g_taskY) : 0;
        bool hasTask = false;
        bool escapingLight = false;
        float tx = g_homeX, ty = g_homeY;

        if (d.type == DRONE_PICKUP) {
            Pickup* drop = nearestPickupInEnvelope(g_taskX, g_taskY, d.x, d.y);
            if (drop) { tx = drop->x; ty = drop->y; hasTask = true; }
        } else if (d.type == DRONE_LIGHT) {
            /* Light drones alone care whether their overlay is embedded. Their
               recovery target is guaranteed to be on the player's side. */
            escapingLight = playerSideOpenPoint(w, p, d, &tx, &ty);
            hasTask = escapingLight;
        } else if (enemy && !clearAttackShot(w, d.x, d.y, *enemy)) {
            hasTask = firingPosition(w, p, d, *enemy, i, &tx, &ty);
        }

        const float pdx = d.x - g_taskX, pdy = d.y - g_taskY;
        const bool outsideTask = pdx * pdx + pdy * pdy >
                                 (float)(DRONE_TASK_RADIUS * DRONE_TASK_RADIUS);
        const float hdx = d.x - g_homeX, hdy = d.y - g_homeY;
        const bool insideHome = hdx * hdx + hdy * hdy <=
                                (float)(DRONE_HOME_RADIUS * DRONE_HOME_RADIUS);

        if (escapingLight) {
            /* Do not use ordinary task braking while buried. It was slowing
               before clearing the material and could be abandoned by a
               moving player. Brake only once it is nearly in safe open air. */
            steerArrive(d, tx, ty, 6.5f, 10.0f);
        } else if (outsideTask) {
            /* Catch-up is the only urgent motion. A task never drags a drone
               farther away from a player who has already left it behind. */
            steerArrive(d, g_homeX, g_homeY, 6.0f, 70.0f);
        } else if (hasTask) {
            steerArrive(d, tx, ty, 3.4f, 34.0f);
        } else if (!insideHome) {
            /* Inside the broad envelope, returning home is intentionally calm. */
            steerArrive(d, g_homeX, g_homeY, 2.25f, 48.0f);
        } else {
            d.vx *= 0.86f; d.vy *= 0.86f;
        }
        if (d.type == DRONE_LIGHT && !escapingLight)
            repelLightFromSurfaces(w, d);
        addSeparation(drones, d, i);
        d.x += d.vx; d.y += d.vy;

        if (d.type == DRONE_ATTACK) {
            const bool overclock = hasChip(inv, i, ITEM_OVERCLOCK_CHIP);
            const bool twin      = hasChip(inv, i, ITEM_TWIN_CONTROLLER);
            if (d.shotCool > 0) --d.shotCool;
            /* Positioning is part of aiming now: a blocked drone waits and
               moves instead of spending bolts on the wall it can see. */
            if (enemy && d.shotCool == 0 &&
                shotOpen(w, (int)d.x, (int)d.y) &&
                clearAttackShot(w, d.x, d.y, *enemy)) {
                float dx = enemy->centreX() - d.x, dy = enemy->centreY() - d.y;
                /* Aim where the target is GOING, not where it is. A bolt takes
                   distance/DRONE_SHOT_SPEED frames to arrive, and firing at the
                   present position misses by exactly that time times the
                   target's speed -- which is why drones whiffed on anything
                   walking. Solving |R + Vt| = s*t for the earliest positive t
                   gives the interception point.

                   Deliberately only LEAD_FRACTION of the way there. A perfect
                   solution makes a drone hit a sprinting bat across the screen
                   every time, and the miss on something fast and perpendicular
                   is the interesting part: that is the case leading cannot
                   fully solve, and it should stay unsolved. */
                {
                    const float vx = enemy->vx, vy = enemy->vy;
                    const float s = DRONE_SHOT_SPEED;
                    const float a = vx * vx + vy * vy - s * s;
                    const float b = 2.0f * (dx * vx + dy * vy);
                    const float c = dx * dx + dy * dy;
                    float t = -1.0f;
                    if (fabsf(a) < 0.0001f) {
                        if (fabsf(b) > 0.0001f) t = -c / b;
                    } else {
                        const float disc = b * b - 4.0f * a * c;
                        if (disc >= 0.0f) {
                            const float root = sqrtf(disc);
                            const float t0 = (-b - root) / (2.0f * a);
                            const float t1 = (-b + root) / (2.0f * a);
                            /* Earliest arrival that is actually in the future.
                               A target faster than the bolt makes both roots
                               negative, and then there is no intercept to find
                               -- the drone simply fires at it and misses. */
                            if (t0 > 0.0f && (t1 <= 0.0f || t0 < t1)) t = t0;
                            else if (t1 > 0.0f) t = t1;
                        }
                    }
                    if (t > 0.0f) {
                        if (t > LEAD_MAX_FRAMES) t = LEAD_MAX_FRAMES;
                        dx += vx * t * LEAD_FRACTION;
                        dy += vy * t * LEAD_FRACTION;
                    }
                }
                const float len = sqrtf(dx * dx + dy * dy);
                if (len > 0.01f) {
                    dx *= DRONE_SHOT_SPEED / len; dy *= DRONE_SHOT_SPEED / len;
                    projSpawn(d.x, d.y, dx, dy, 0, 0, 70, 0xF2C16D, 0,
                              MAT_EMPTY, ATTACK_DRONE_DAMAGE, false, 0.0f);
                    if (twin) {
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
                entDamageKnockbackDisc((int)p.centreX(), (int)p.centreY(), 34, 1, 1.25f);
                if (hasChip(inv, i, ITEM_GARLIC_FIELD_CHIP))
                    entDamageDisc((int)d.x, (int)d.y, 14, 1);
                d.effectCool = 24;
            }
        }
    }
}

void droneTick(const World& w, const Player& p, Inventory& inv) {
    droneTickBank(g_playerSessions[0].drones, w, p, inv);
}

void droneTickFor(int playerSlot, const World& w, const Player& p, Inventory& inv) {
    if (playerSlot < 0 || playerSlot >= 4) return;
    droneTickBank(g_playerSessions[playerSlot].drones, w, p, inv);
}

void droneRegisterLights() {
    for (int player = 0; player < 4; ++player)
        for (int i = 0; i < MAX_DRONES; ++i)
            if (g_playerSessions[player].drones[i].type == DRONE_LIGHT)
                lightAddDynamic((int)g_playerSessions[player].drones[i].x,
                                (int)g_playerSessions[player].drones[i].y, 210);
}

void droneDraw(u32* px, int camX, int camY, bool lit) {
    for (int player = 0; player < 4; ++player) for (int i = 0; i < MAX_DRONES; ++i) {
        const Drone& d = g_playerSessions[player].drones[i];
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
