/* --- what the companions actually deliver -------------------------------------

   Reported from play: "shield drones dont really work and orbit drones kinda
   suck. and lance drones basically never hit."

   All three were true, and the first two were worse than the words. Measured
   before touching anything, against a pinned target at four ranges and against
   four shooters at the compass points:

     Lance   0.0 damage a second at 14, 30, 70 and 150 cells. Not weak: zero.
     Orbit   2.2 a second in contact, which is the drone that asks nothing of
             you, and nothing at all past touching distance.
     Shield  115 damage reached the player with it equipped. 115 without.

   The causes were all the same shape -- a weapon aimed at where the drone
   happens to be rather than at anything. The lance fired a flat line along the
   facing from a hover point above the player's head, so the burst sailed over
   everything. The shield intercepted inside a radius of ITSELF while sitting at
   that same point, twenty cells above the line a shot at your body travels.

   This file is the numbers, kept, because "kinda sucks" is not something a
   table of constants can be checked against but a damage rate is. The
   thresholds are deliberately loose: what is being defended is the SHAPE of
   each chassis, not its tuning.

   Compile with every src cpp file except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "player.h"
#include "projectile.h"
#include "drone.h"
#include "multiplayer.h"
#include <stdio.h>
#include <math.h>

static World g_testWorld;
static const int CX = 900, CY = 900;
static const int FLOOR = CY + 40;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static void arena(World& w) {
    w.reset();
    projClear();
    entReset();
    droneReset();
    for (int y = FLOOR; y <= FLOOR + 6; ++y)
        for (int x = CX - 300; x <= CX + 300; ++x) w.setCell(x, y, MAT_STONE);
    w.setLiveWindow(CX - 320, CY - 200, CX + 320, FLOOR + 20);
    Player& p = g_player;
    p.reset((float)CX, (float)(FLOOR - PLAYER_H / 2));
    p.alive = true; p.hp = PLAYER_HP_MAX; p.facing = 1;
}

static void equipDrone(Inventory& inv, ItemId drone) {
    inv.clear();
    inv.equip[EQ_DRONE_A].item = drone;
    inv.equip[EQ_DRONE_A].count = 1;
}

/* Damage a second delivered to targets held at full health and pinned in
   place. Pinned because a chassis that kills its target early would otherwise
   report a rate that is really "how long until the room was empty", and held at
   full health because the question is output, not whether one husk dies. */
static float dpsAgainst(ItemId drone, const float* ox, int count, int frames) {
    World& w = g_testWorld;
    arena(w);
    Inventory& inv = g_inv;
    equipDrone(inv, drone);
    int es[8];
    for (int k = 0; k < count; ++k)
        es[k] = entSpawn(w, ENT_HUSK, (float)CX + ox[k], (float)(FLOOR - 16));
    const int FULL = 100000;
    float total = 0.0f;
    for (int f = 0; f < frames; ++f) {
        for (int k = 0; k < count; ++k) {
            if (es[k] < 0) continue;
            Entity& t = g_entities[es[k]];
            t.hp = FULL;
            t.x = (float)CX + ox[k]; t.y = (float)(FLOOR - 16);
            t.vx = t.vy = 0.0f;
        }
        g_player.facing = 1;
        droneTick(w, g_player, inv);
        projUpdate(w);
        entTick(w, g_player, inv);
        for (int k = 0; k < count; ++k)
            if (es[k] >= 0 && g_entities[es[k]].type != ENT_NONE)
                total += (float)(FULL - g_entities[es[k]].hp);
    }
    return total * 60.0f / (float)frames;
}

static float dpsAt(ItemId drone, float dist, int frames) {
    return dpsAgainst(drone, &dist, 1, frames);
}

/* Damage that reaches the player from shooters at the four compass points, one
   shot every `every` frames. Four rather than one so this measures a bubble
   rather than one lucky line -- a shield that only covered the side it happened
   to be drifting toward would pass a single-shooter test. */
static float barrage(ItemId drone, int every, int frames, int* firedOut) {
    World& w = g_testWorld;
    arena(w);
    Inventory& inv = g_inv;
    if (drone == ITEM_NONE) inv.clear(); else equipDrone(inv, drone);
    int fired = 0;
    float taken = 0.0f;
    for (int f = 0; f < frames; ++f) {
        droneTick(w, g_player, inv);
        if (f % every == 0) {
            const float px = g_player.centreX(), py = g_player.centreY();
            static const float OFF[4][2] = { { 90, -4 }, { -90, -4 },
                                             { 40, -70 }, { -40, 60 } };
            const int which = (f / every) & 3;
            const float sx = px + OFF[which][0], sy = py + OFF[which][1];
            float dx = px - sx, dy = py - sy;
            const float len = sqrtf(dx * dx + dy * dy);
            projSpawn(sx, sy, dx / len * 4.0f, dy / len * 4.0f,
                      0, 0, 200, 0xFF8080, 0, MAT_EMPTY, 5, true, 0.0f);
            ++fired;
        }
        const int hpBefore = g_player.hp;
        projUpdate(w);
        entTick(w, g_player, inv);
        taken += (float)(hpBefore - g_player.hp);
        /* Restored every frame: this counts hits, not how long it takes to
           die, and a dead player stops being shot at. */
        g_player.hp = PLAYER_HP_MAX;
        g_player.alive = true;
    }
    if (firedOut) *firedOut = fired;
    return taken;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();

    /* --- 1. the lance hits things ----------------------------------------- */
    {
        const float near = dpsAt(ITEM_LANCE_DRONE, 14.0f, 900);
        const float mid  = dpsAt(ITEM_LANCE_DRONE, 70.0f, 900);
        const float far  = dpsAt(ITEM_LANCE_DRONE, 150.0f, 900);
        const float base = dpsAt(ITEM_ATTACK_DRONE, 70.0f, 900);
        printf("Lance: %.1f/s at 14 cells, %.1f at 70, %.1f at 150 "
               "(Attack drone %.1f)\n", near, mid, far, base);
        check(near > 0.0f && mid > 0.0f && far > 0.0f,
              "the lance connects at every range, which it did not at any");
        /* It is the chassis you point, so it has to be worth pointing. */
        check(mid > base * 2.0f, "and it is worth more than the drone that aims itself");
    }

    /* --- 2. and only in the half you are facing ---------------------------- */
    {
        /* The facing hemisphere is what keeps this the directional chassis. A
           lance that turned round to shoot behind you would be an attack drone
           with better numbers, and the choice between them would evaporate. */
        const float behind = dpsAt(ITEM_LANCE_DRONE, -70.0f, 900);
        const float ahead  = dpsAt(ITEM_LANCE_DRONE,  70.0f, 900);
        printf("Lance: %.1f/s in front, %.1f/s behind\n", ahead, behind);
        check(behind == 0.0f, "and it will not shoot behind the player");
    }

    /* --- 3. the orbit is a crowd answer ------------------------------------ */
    {
        const float one = dpsAt(ITEM_ORBIT_DRONE, 14.0f, 900);
        static const float SIX[6] = { -24, -17, -10, 10, 17, 24 };
        const float crowd = dpsAgainst(ITEM_ORBIT_DRONE, SIX, 6, 900);
        const float away  = dpsAt(ITEM_ORBIT_DRONE, 70.0f, 900);
        printf("Orbit: %.1f/s against one in contact, %.1f/s against six, "
               "%.1f/s at 70 cells\n", one, crowd, away);
        check(one > 4.0f, "the blade hurts something standing on you");
        check(crowd > one * 2.0f, "and hurts a crowd far more, which is its whole job");
        check(away == 0.0f, "and nothing at all at range, which is its whole cost");
    }

    /* --- 4. the shield stops a shooter ------------------------------------- */
    {
        int fired = 0;
        const float bare  = barrage(ITEM_NONE, 30, 900, &fired);
        const float armed = barrage(ITEM_SHIELD_DRONE, 30, 900, 0);
        printf("Shield vs one shot every 30 frames (%d shots): %.0f damage "
               "through bare, %.0f with the drone\n", fired, bare, armed);
        check(bare > 0.0f, "an unshielded player is hit by all of it");
        check(armed < bare / 4.0f, "and a shielded one is not");
    }

    /* --- 5. but it is not immunity ----------------------------------------- */
    {
        /* The half of this that is easy to get wrong. Interposing alone stopped
           EVERYTHING -- 100% of a shot every ten frames -- and one drone bay
           that cancels every shooter in the game is not a companion, it is a
           difficulty setting. The recharge is what turns it back into an
           answer to being shot at rather than to ranged combat. */
        int fired = 0;
        const float armed = barrage(ITEM_SHIELD_DRONE, 4, 900, &fired);
        const float stopped = 100.0f - armed * 100.0f / (float)(fired * 5);
        printf("Shield under a shot every 4 frames (%d shots): %.0f%% stopped\n",
               fired, stopped);
        check(stopped > 20.0f, "it still helps against a barrage");
        check(stopped < 90.0f, "but a barrage gets through, so it is not immunity");
    }

    /* --- 6. and none of it touches your bees ------------------------------
       Reported from play: "drones shoot bees, stop that."

       Nothing in the damage layer knew what a tame creature was. The drones'
       targeting took the nearest ENTITY -- a bee is an entity -- and the orbit
       blade, the shield pulse and the garlic field damaged every body in a
       radius. A player who keeps bees was running a machine that killed them.

       Every chassis is checked, not just the two that were obviously shooting,
       because the bug was in the shared targeting rather than in any one of
       them. */
    {
        static const ItemId CHASSIS[5] = {
            ITEM_ATTACK_DRONE, ITEM_LANCE_DRONE, ITEM_ORBIT_DRONE,
            ITEM_MORTAR_DRONE, ITEM_SHIELD_DRONE
        };
        static const char* NAMES[5] = { "Attack", "Lance", "Orbit", "Mortar",
                                        "Shield" };
        int killed = 0;
        for (int k = 0; k < 5; ++k) {
            World& w = g_testWorld;
            arena(w);
            Inventory& inv = g_inv;
            equipDrone(inv, CHASSIS[k]);
            /* Bees all round the player: in contact, at the blade's radius,
               and out where a bolt would have to travel. */
            int bees[6];
            static const float AT[6] = { -14, 14, -26, 26, -70, 70 };
            for (int b = 0; b < 6; ++b)
                bees[b] = entSpawn(w, ENT_BEE, (float)CX + AT[b],
                                   (float)(FLOOR - 20));
            for (int f = 0; f < 900; ++f) {
                for (int b = 0; b < 6; ++b) {
                    if (bees[b] < 0) continue;
                    Entity& e = g_entities[bees[b]];
                    if (e.type == ENT_NONE) continue;
                    /* Pinned, so this measures what the drone does to them
                       rather than where they wandered. */
                    e.x = (float)CX + AT[b];
                    e.y = (float)(FLOOR - 20);
                    e.vx = e.vy = 0.0f;
                }
                droneTick(w, g_player, inv);
                projUpdate(w);
                entTick(w, g_player, inv);
            }
            int alive = 0, hurt = 0;
            for (int b = 0; b < 6; ++b) {
                if (bees[b] < 0) continue;
                const Entity& e = g_entities[bees[b]];
                if (e.type == ENT_BEE && e.alive()) {
                    ++alive;
                    if (e.hp < ENT_DEFS[ENT_BEE].hp) ++hurt;
                }
            }
            printf("  %-7s drone: %d of 6 bees alive, %d of them hurt\n",
                   NAMES[k], alive, hurt);
            if (alive < 6 || hurt > 0) ++killed;
        }
        check(killed == 0, "no chassis harms a bee, at any range");
    }

    if (failures) {
        fprintf(stderr, "\n%d drone check(s) failed\n", failures);
        return 1;
    }
    printf("\nthe three chassis do what their notes claim\n");
    return 0;
}
