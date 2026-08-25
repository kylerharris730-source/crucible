/* --- does a stroke land exactly once, and only where the blade is? ----------

   Two properties, and the first is the one a melee weapon gets wrong by
   default. A stroke lasts ten to sixteen frames and the hit test runs on every
   one of them, so the naive version takes the weapon's damage off a creature
   ten to sixteen times -- a copper sword doing 7 becomes a copper sword doing
   112, which is more than the whole first tier of the roster has health. That
   is not a tuning error that shows up as "melee feels strong"; it is a copper
   sword one-shotting everything in the layer.

   PlayerSession::swingHit is the fix, and this is what proves it is working.

   The second property is that the shape means something. A sword sweeps an arc
   in front of you, so a creature standing BEHIND the swing must survive it --
   otherwise the arc is decoration over what is really a radius, and the whole
   difference between a sword and a spear evaporates.

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "multiplayer.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static World g_testWorld;

/* A stripped copy of what main.cpp's meleeBlade does, so the harness can drive
   a stroke without a window. It is deliberately NOT shared code: this is the
   test's own model of where the blade should be, and if it ever disagrees with
   the game the disagreement is the finding. Keep them in step by hand. */
static void bladeAt(const Player& body, const ItemDef& def,
                    float dirX, float dirY, float phase,
                    float* hx, float* hy, float* tx, float* ty) {
    const float cx = body.centreX(), cy = body.centreY();
    if (def.meleeStyle == MELEE_SWING) {
        const float half = (float)def.meleeArc * 0.5f * 3.14159265f / 180.0f;
        const float base = atan2f(dirY, dirX);
        const float sweep = (dirX < 0.0f) ? -1.0f : 1.0f;
        const float angle = base - sweep * half + sweep * (2.0f * half) * phase;
        const float ca = cosf(angle), sa = sinf(angle);
        *hx = cx + ca * 2.0f; *hy = cy + sa * 2.0f;
        *tx = cx + ca * (float)def.meleeReach;
        *ty = cy + sa * (float)def.meleeReach;
        return;
    }
    const float out = sinf(phase * 3.14159265f);
    const float lead = 2.0f + ((float)def.meleeReach - 2.0f) * out;
    *hx = cx + dirX * (lead - (float)def.meleeReach * 0.55f);
    *hy = cy + dirY * (lead - (float)def.meleeReach * 0.55f);
    *tx = cx + dirX * lead;
    *ty = cy + dirY * lead;
}

/* Run one whole stroke against whatever is alive, and report the damage taken
   off the creature in slot `victim`. */
static int oneStroke(const Player& body, ItemId weapon, float dirX, float dirY, int victim) {
    const ItemDef& def = ITEMS[weapon];
    u8 hitMask[(MAX_ENTITIES + 7) / 8];
    memset(hitMask, 0, sizeof(hitMask));

    const int before = g_entities[victim].hp;
    const int total = def.meleeFrames;
    for (int f = 0; f < total; ++f) {
        const float phase = (float)f / (float)total;
        float hx, hy, tx, ty;
        bladeAt(body, def, dirX, dirY, phase, &hx, &hy, &tx, &ty);
        entHitSegment(hx, hy, tx, ty, body.centreX(), body.centreY(),
                      def.damage, def.meleeKnock, hitMask);
    }
    return before - g_entities[victim].hp;
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();

    World& w = g_testWorld;
    w.reset();
    const int floorY = 700;
    for (int x = PLAY_X0; x <= PLAY_X1; ++x)
        for (int y = floorY; y <= floorY + 20 && y <= PLAY_Y1; ++y)
            w.setCell(x, y, MAT_STONE);

    /* Well clear of the floor rather than standing on it. Nothing here needs
       the ground -- the floor exists only so the world is not wholly empty --
       and a target spawned beside a player standing ON it has its lower rows
       inside the stone, which entSpawn correctly refuses. That refusal cost a
       run: the failure reads as "could not place the target", which sounds
       like a pool problem and is a geometry one. */
    Player& p = g_player;
    p.x = 1000.0f; p.y = (float)(floorY - 60);
    p.alive = true; p.hp = PLAYER_HP_MAX;
    p.facing = 1;

    int failures = 0;

    /* --- every weapon lands its damage exactly once --------------------- */
    struct Row { ItemId id; const char* name; };
    const Row all[] = {
        { ITEM_SWORD_COPPER,   "Copper Sword"   }, { ITEM_SPEAR_COPPER,   "Copper Spear"   },
        { ITEM_SWORD_BRONZE,   "Bronze Sword"   }, { ITEM_SPEAR_BRONZE,   "Bronze Spear"   },
        { ITEM_SWORD_IRON,     "Iron Sword"     }, { ITEM_SPEAR_IRON,     "Iron Spear"     },
        { ITEM_SWORD_GOLD,     "Gold Sword"     }, { ITEM_SPEAR_GOLD,     "Gold Spear"     },
        { ITEM_SWORD_STEEL,    "Steel Sword"    }, { ITEM_SPEAR_STEEL,    "Steel Spear"    },
        { ITEM_SWORD_TITANIUM, "Titanium Sword" }, { ITEM_SPEAR_TITANIUM, "Titanium Spear" },
        { ITEM_SWORD_TUNGSTEN, "Tungsten Sword" }, { ITEM_SPEAR_TUNGSTEN, "Tungsten Spear" },
    };

    printf("%-16s %6s %6s  %s\n", "weapon", "dealt", "stated", "");
    for (int i = 0; i < (int)(sizeof(all) / sizeof(all[0])); ++i) {
        entReset();
        /* Directly in front, well inside even the shortest reach. Health is
           raised far above the roster's so a single stroke can never kill it --
           a dead creature stops taking damage, which would hide exactly the bug
           this is looking for. */
        const int e = entSpawn(w, ENT_HUSK, p.centreX() + 8.0f, p.centreY());
        if (e < 0) { fprintf(stderr, "could not place the target\n"); return 2; }
        g_entities[e].hp = 100000;

        const int dealt = oneStroke(p, all[i].id, 1.0f, 0.0f, e);
        const int want  = ITEMS[all[i].id].damage;
        const bool ok = (dealt == want);
        printf("%-16s %6d %6d  %s\n", all[i].name, dealt, want, ok ? "" : "<-- MULTI-HIT");
        if (!ok) ++failures;
    }

    /* --- the arc has a back to it --------------------------------------- */
    {
        entReset();
        /* Behind the swing, at the same distance the front target was hit at.
           A tungsten sword reaches 38, so 8 cells the other way is comfortably
           inside a RADIUS of that size and comfortably outside a 130-degree arc
           pointed the other way. That gap is the whole test. */
        const int back = entSpawn(w, ENT_HUSK, p.centreX() - 8.0f, p.centreY());
        if (back < 0) { fprintf(stderr, "could not place the rear target\n"); return 2; }
        g_entities[back].hp = 100000;
        const int dealt = oneStroke(p, ITEM_SWORD_TUNGSTEN, 1.0f, 0.0f, back);
        printf("\nswing aimed right, target 8 cells LEFT: %d damage\n", dealt);
        if (dealt != 0) {
            fprintf(stderr, "FAIL: the arc hit something behind the swing -- "
                            "it is behaving as a radius, not an arc\n");
            ++failures;
        }
    }

    /* --- both weapon families use the extended ladders ------------------ */
    {
        static const ItemId sword[] = {
            ITEM_SWORD_COPPER, ITEM_SWORD_BRONZE, ITEM_SWORD_IRON,
            ITEM_SWORD_GOLD, ITEM_SWORD_STEEL, ITEM_SWORD_TITANIUM,
            ITEM_SWORD_TUNGSTEN
        };
        static const ItemId spear[] = {
            ITEM_SPEAR_COPPER, ITEM_SPEAR_BRONZE, ITEM_SPEAR_IRON,
            ITEM_SPEAR_GOLD, ITEM_SPEAR_STEEL, ITEM_SPEAR_TITANIUM,
            ITEM_SPEAR_TUNGSTEN
        };
        static const int swordReach[] = { 32, 35, 35, 32, 38, 40, 44 };
        static const int spearReach[] = { 39, 41, 44, 39, 48, 51, 56 };
        static const int swordCool[] = { 33, 33, 33, 24, 33, 33, 35 };
        for (int i = 0; i < 7; ++i) {
            if (ITEMS[sword[i]].meleeReach != swordReach[i]) {
                fprintf(stderr, "FAIL: sword reach ladder drifted at tier %d (%u/%d)\n",
                        i, ITEMS[sword[i]].meleeReach, swordReach[i]);
                ++failures;
            }
            if (ITEMS[spear[i]].meleeReach != spearReach[i]) {
                fprintf(stderr, "FAIL: spear reach ladder drifted at tier %d (%u/%d)\n",
                        i, ITEMS[spear[i]].meleeReach, spearReach[i]);
                ++failures;
            }
            if (ITEMS[sword[i]].meleeFrames != 16 ||
                ITEMS[sword[i]].meleeCooldown != swordCool[i]) {
                fprintf(stderr, "FAIL: sword timing drifted at tier %d (%u/%u, expected 16/%d)\n",
                        i, ITEMS[sword[i]].meleeFrames,
                        ITEMS[sword[i]].meleeCooldown, swordCool[i]);
                ++failures;
            }
        }

        entReset();
        /* A bat centred 32 cells away lies at the new copper blade tip. */
        const int far = entSpawn(w, ENT_BAT, p.centreX() + 32.0f, p.centreY());
        if (far < 0) { fprintf(stderr, "could not place long-sword target\n"); return 2; }
        g_entities[far].hp = 100000;
        const int dealt = oneStroke(p, ITEM_SWORD_COPPER, 1.0f, 0.0f, far);
        const float shove = sqrtf(g_entities[far].vx * g_entities[far].vx +
                                  g_entities[far].vy * g_entities[far].vy);
        printf("copper sword at 32 cells: %d damage, %.2f cells/frame shove\n",
               dealt, shove);
        if (dealt != ITEMS[ITEM_SWORD_COPPER].damage || shove < 2.0f) {
            fprintf(stderr, "FAIL: extended copper sword did not reach and visibly knock back\n");
            ++failures;
        }
    }

    /* --- a sword catches a crowd, a spear does not ----------------------- */
    {
        entReset();
        int hitSword = 0, hitSpear = 0;
        /* Three targets fanned across the front: level, above, below. A
           130-degree arc covers all three; a straight thrust reaches only the
           one it is pointed at.

           BATS, not husks, and the size is the whole reason. A husk is 22 cells
           tall, so two of them fanned seven cells apart still both straddle the
           horizontal line a thrust travels along -- the spear hit all three and
           the test called that a design failure when it was a measurement one.
           A bat is 9x7, so eight cells of separation genuinely puts it off the
           line. The lesson generalises: a shape test needs targets smaller than
           the distances it is trying to distinguish. */
        const float at[3][2] = { { 9.0f, 0.0f }, { 5.0f, -8.0f }, { 5.0f, 8.0f } };
        for (int pass = 0; pass < 2; ++pass) {
            entReset();
            int slot[3];
            for (int k = 0; k < 3; ++k) {
                slot[k] = entSpawn(w, ENT_BAT, p.centreX() + at[k][0], p.centreY() + at[k][1]);
                if (slot[k] >= 0) g_entities[slot[k]].hp = 100000;
            }
            const ItemId weapon = pass ? ITEM_SPEAR_TUNGSTEN : ITEM_SWORD_TUNGSTEN;
            const ItemDef& def = ITEMS[weapon];
            u8 hitMask[(MAX_ENTITIES + 7) / 8];
            memset(hitMask, 0, sizeof(hitMask));
            for (int f = 0; f < def.meleeFrames; ++f) {
                float hx, hy, tx, ty;
                bladeAt(p, def, 1.0f, 0.0f, (float)f / (float)def.meleeFrames,
                        &hx, &hy, &tx, &ty);
                entHitSegment(hx, hy, tx, ty, p.centreX(), p.centreY(),
                              def.damage, def.meleeKnock, hitMask);
            }
            int n = 0;
            for (int k = 0; k < 3; ++k)
                if (slot[k] >= 0 && g_entities[slot[k]].hp < 100000) ++n;
            if (pass) hitSpear = n; else hitSword = n;
        }
        printf("three targets fanned in front: sword hit %d, spear hit %d\n",
               hitSword, hitSpear);
        if (hitSword < 3) {
            fprintf(stderr, "FAIL: the sword's arc missed part of the fan\n");
            ++failures;
        }
        if (hitSpear >= hitSword) {
            fprintf(stderr, "FAIL: the spear covered as much as the sword -- "
                            "the two styles are not distinct\n");
            ++failures;
        }
    }

    if (failures) { fprintf(stderr, "\n%d melee check(s) failed\n", failures); return 1; }
    printf("\nPASS\n");
    return 0;
}
