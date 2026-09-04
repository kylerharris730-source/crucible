/* --- modules that are not shots ----------------------------------------------

   Asked for: "then we're gonna want another multitool. mk3, that has better
   stats, then lets add shot modifiers, they apply to one or two shots at the
   same time, double shot modifier shoots both spells its in front of at the
   same time, the fire trail modifier gives the one spell its in front of a fire
   trail, the lightning arc double applies to two spells and they have a
   lightning arc between them when theyre shot, there should also be fire arc,
   and homing modifier, this one hsould be really energy expensive because its
   good, there can be home to mouse which is less energy intensive, there can be
   recharge down which lowers refresh time but costs a lot of mana".

   Every module up to now IS a shot: it sits in a socket, its turn comes round,
   it fires. A modifier fires nothing and changes what comes after it. That is a
   change to the SEQUENCE, not a new stat, which is why it needs a harness of
   its own -- the failure mode is not "the number is wrong", it is "the wrong
   thing came out of the wand".

   Eight properties. The first five are the request, and the last three are what
   stops the request from quietly breaking the tool it is fitted to:

     a modifier fires nothing on its own          (it is not a spell)
     Double sends the next TWO out together       (the request)
     an arc reaches two and links them            (the request)
     seeking is priced far above point seeking    (the request, explicitly)
     Quicken shortens the delay and costs charge  (the request)
     a doubled volley is not slower for it        (or doubling is a downgrade)
     a wand of only modifiers fires nothing       (rather than crashing)
     every existing module still fires as it did  (modKind 0 changes nothing)

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "player.h"
#include "projectile.h"
#include "craft.h"
#include "multiplayer.h"
#include <stdio.h>
#include <math.h>

static const int CX_T = 1400, CY_T = 5000;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  %-56s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

/* A Mk III with the given modules socketed, in order. */
static ItemStack g_tool;
static void wand(ItemId a, ItemId b = ITEM_NONE, ItemId c = ITEM_NONE,
                 ItemId d = ITEM_NONE) {
    g_tool.item = ITEM_MULTITOOL3;
    g_tool.count = 1;
    g_tool.inst = (u16)toolInstNew(ITEM_MULTITOOL3);
    ToolInst& ti = g_toolInst[g_tool.inst];
    for (int i = 0; i < TOOL_SLOTS_MAX; ++i) ti.slot[i] = ITEM_NONE;
    ti.slot[0] = a; ti.slot[1] = b; ti.slot[2] = c; ti.slot[3] = d;
    ti.shotCursor = 0;
    ti.energy = ti.energyCapacity;
    ti.cooldown = 0;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();

    /* --- 0. the chassis --------------------------------------------------- */
    {
        const ItemDef& m2 = ITEMS[ITEM_MULTITOOL2];
        const ItemDef& m3 = ITEMS[ITEM_MULTITOOL3];
        printf("Mk II: %d slots, %d delay, %d charge, %d/frame\n",
               m2.toolSlots, m2.baseDelay, m2.energyCapacity, m2.energyRecharge);
        printf("Mk III: %d slots, %d delay, %d charge, %d/frame\n",
               m3.toolSlots, m3.baseDelay, m3.energyCapacity, m3.energyRecharge);
        check(m3.toolSlots > m2.toolSlots && m3.baseDelay < m2.baseDelay &&
              m3.energyCapacity > m2.energyCapacity &&
              m3.energyRecharge > m2.energyRecharge,
              "Mk III beats Mk II on every stat");
        check(m3.toolSlots <= TOOL_SLOTS_MAX,
              "and it fits in the socket array it is stored in");
    }

    /* --- 1. a modifier is not a spell ------------------------------------- */
    /* The whole distinction, and the cheapest place to get it wrong: if a
       modifier had any damage or power on it, it would fire on its own turn and
       the sequence would be one shot longer than the player socketed. */
    {
        const ItemId mods[] = {
            ITEM_MOD_DOUBLE, ITEM_MOD_TRAIL_FIRE, ITEM_MOD_ARC_LIGHTNING,
            ITEM_MOD_ARC_FIRE, ITEM_MOD_SEEK, ITEM_MOD_SEEK_MOUSE,
            ITEM_MOD_QUICKEN };
        bool clean = true, spanned = true;
        for (int k = 0; k < 7; ++k) {
            const ItemDef& d = ITEMS[mods[k]];
            if (d.modKind == MODK_NONE) clean = false;
            if (d.damage != 0 || d.power != 0 || d.blast != 0) clean = false;
            if (d.modSpan < 1 || d.modSpan > 2) spanned = false;
        }
        check(clean, "every modifier declares a kind and carries no shot");
        check(spanned, "and reaches one or two spells, as asked");

        /* Alone in a wand it produces nothing at all. */
        wand(ITEM_MOD_DOUBLE);
        const ToolShot s = toolResolve(g_tool);
        check(!s.canFire, "a wand holding only a modifier fires nothing");
    }

    /* --- 2. Double sends the next two out together ------------------------ */
    {
        wand(ITEM_MOD_DOUBLE, ITEM_MOD_SHOT, ITEM_MOD_BLAST);
        const ToolShot s = toolResolve(g_tool);
        printf("Double + Shot + Blast: canFire=%d, companion=%d, "
               "primary dmg %d, companion dmg %d\n",
               (int)s.canFire, (int)s.second.used, s.damage, s.second.damage);
        check(s.canFire, "it fires");
        check(s.second.used, "and a second shot comes with it");
        check(s.damage == ITEMS[ITEM_MOD_SHOT].damage &&
              s.second.damage == ITEMS[ITEM_MOD_BLAST].damage,
              "the two are the two spells in front of it, in order");
        /* The energy is BOTH spells plus the modifier, or doubling would be
           free -- which is the version of this that needs no thought and is
           immediately the only modifier anyone fits. */
        const int expect = ITEMS[ITEM_MOD_SHOT].energyCost +
                           ITEMS[ITEM_MOD_BLAST].energyCost +
                           ITEMS[ITEM_MOD_DOUBLE].energyCost;
        printf("  energy: %d (expected %d)\n", s.energyCost, expect);
        check(s.energyCost == expect, "and it is charged for both, plus itself");
    }

    /* --- 3. a doubled volley is not slower for being doubled --------------- */
    /* Summing the two spells' delays would make Double a downgrade: twice the
       shots at half the rate is the same damage out of a worse weapon. */
    {
        wand(ITEM_MOD_SHOT, ITEM_MOD_BLAST);
        const int plain = toolResolve(g_tool).delay;
        wand(ITEM_MOD_DOUBLE, ITEM_MOD_SHOT, ITEM_MOD_BLAST);
        const int doubled = toolResolve(g_tool).delay;
        printf("delay: %d alone, %d doubled\n", plain, doubled);
        check(doubled <= plain + ITEMS[ITEM_MOD_DOUBLE].addDelay + 1,
              "doubling costs its own delay and not the second spell's");
    }

    /* --- 4. the arcs reach two, and link them ----------------------------- */
    {
        wand(ITEM_MOD_ARC_LIGHTNING, ITEM_MOD_SHOT, ITEM_MOD_SHOT);
        const ToolShot l = toolResolve(g_tool);
        wand(ITEM_MOD_ARC_FIRE, ITEM_MOD_SHOT, ITEM_MOD_SHOT);
        const ToolShot f = toolResolve(g_tool);
        check(l.second.used && l.link == MODK_ARC_LIGHTNING,
              "Lightning Arc fires two and strings lightning between them");
        check(f.second.used && f.link == MODK_ARC_FIRE,
              "Fire Arc fires two and strings fire between them");

        /* With only ONE spell to reach, an arc has one end. It must degrade to
           a plain shot rather than to a link pointing at nothing. */
        wand(ITEM_MOD_ARC_LIGHTNING, ITEM_MOD_SHOT);
        const ToolShot one = toolResolve(g_tool);
        printf("one spell under an arc: canFire=%d companion=%d link=%d\n",
               (int)one.canFire, (int)one.second.used, (int)one.link);
        check(one.canFire && !one.second.used && one.link == MODK_NONE,
              "an arc over a single spell is just that spell");
    }

    /* --- 5. the trail, and the two ways of not missing -------------------- */
    {
        wand(ITEM_MOD_TRAIL_FIRE, ITEM_MOD_SHOT);
        const ToolShot t = toolResolve(g_tool);
        check(t.canFire && t.trail == MAT_FIRE,
              "Fire Trail gives the shot in front of it a fire trail");

        wand(ITEM_MOD_SEEK, ITEM_MOD_SHOT);
        const ToolShot seek = toolResolve(g_tool);
        wand(ITEM_MOD_SEEK_MOUSE, ITEM_MOD_SHOT);
        const ToolShot mouse = toolResolve(g_tool);
        wand(ITEM_MOD_SHOT);
        const ToolShot bare = toolResolve(g_tool);
        printf("seeking %d charge, point seeking %d, bare shot %d\n",
               seek.energyCost, mouse.energyCost, bare.energyCost);
        check(seek.homing > 0.0f && !seek.seekMouse, "Seeking homes on creatures");
        check(mouse.homing > 0.0f && mouse.seekMouse,
              "Point Seeking homes on where you pointed");
        /* Explicitly asked for: the good one is expensive, the other is not. */
        check(seek.energyCost > mouse.energyCost * 3,
              "and seeking costs far more than point seeking");
        check(seek.energyCost > bare.energyCost * 3,
              "and far more than the shot it is fitted to");
    }

    /* --- 6. Quicken ------------------------------------------------------- */
    {
        wand(ITEM_MOD_SHOT);
        const ToolShot plain = toolResolve(g_tool);
        wand(ITEM_MOD_QUICKEN, ITEM_MOD_SHOT);
        const ToolShot quick = toolResolve(g_tool);
        printf("Quicken: delay %d -> %d, energy %d -> %d\n",
               plain.delay, quick.delay, plain.energyCost, quick.energyCost);
        check(quick.delay < plain.delay, "Quicken lowers the refresh time");
        check(quick.energyCost > plain.energyCost * 3,
              "and costs a great deal of charge to do it");
        /* The bound. A delay that can reach zero is a weapon that fires every
           frame, and no energy price survives that. */
        wand(ITEM_MOD_QUICKEN, ITEM_MOD_QUICKEN, ITEM_MOD_QUICKEN,
             ITEM_MOD_SHOT);
        const ToolShot stacked = toolResolve(g_tool);
        printf("three Quickens stacked: delay %d\n", stacked.delay);
        check(stacked.delay >= 3, "and stacking it cannot reach a zero delay");
    }

    /* --- 7. nothing that already worked changed --------------------------- */
    /* modKind is zero on every module that predates this, and zero means "an
       ordinary spell". This is the check that says so out loud, because the
       cost of being wrong is every existing wand quietly firing differently. */
    {
        bool unchanged = true;
        for (int i = 1; i < ITEM_COUNT; ++i) {
            const ItemDef& d = ITEMS[i];
            if (d.kind != ITEMK_MODULE) continue;
            /* A module is either a spell with damage or a modifier with a
               kind. Never neither, and never both. */
            const bool isMod = d.modKind != MODK_NONE;
            const bool isSpell = d.damage > 0 || d.blast > 0 ||
                                 d.shotEffect != PROJ_EFFECT_NONE;
            if (isMod == isSpell) {
                printf("  %s is %s\n", d.name,
                       isMod ? "both a modifier and a spell" : "neither");
                unchanged = false;
            }
        }
        check(unchanged, "every module is a spell or a modifier, never both");

        wand(ITEM_MOD_SHOT);
        const ToolShot s = toolResolve(g_tool);
        check(s.canFire && s.damage == ITEMS[ITEM_MOD_SHOT].damage &&
              s.power == ITEMS[ITEM_MOD_SHOT].power && !s.second.used &&
              s.trail == MAT_EMPTY && s.link == MODK_NONE,
              "a plain Shot Module still resolves to exactly what it was");
    }

    /* --- 8. and the two that live in the world, not in the resolver -------
       Everything above reads a struct. These two fire real projectiles through
       a real world, because "the resolver said trail=MAT_FIRE" and "there is
       fire on the floor" are different claims and only the second one is what
       was asked for. */
    {
        static World w;
        w.reset();
        for (int y = CY_T - 40; y <= CY_T + 40; ++y)
            for (int x = CX_T - 20; x <= CX_T + 220; ++x)
                if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1)
                    w.setCell(x, y, MAT_EMPTY);
        w.setLiveWindow(CX_T - 40, CY_T - 60, CX_T + 240, CY_T + 60);
        entReset();
        projClear();

        /* A flat, gravity-free shot straight down an empty corridor, so what is
           measured is the trail and not where the arc of a falling glob went. */
        projSpawn((float)CX_T, (float)CY_T, 2.0f, 0.0f, STR_NOTHING, 1, 90,
                  0xFFFFFF, 0, MAT_EMPTY, 5, false, 0.0f, PROJ_EFFECT_NONE,
                  0, 0.0f, 0xff, 0, MAT_FIRE);
        int laid = 0;
        for (int f = 0; f < 60; ++f) {
            projUpdate(w);
            int n = 0;
            for (int x = CX_T - 10; x < CX_T + 200; ++x)
                if (w.at(x, CY_T).mat == MAT_FIRE) ++n;
            if (n > laid) laid = n;
        }
        printf("a trailing shot down 200 cells: %d cells of fire behind it\n",
               laid);
        check(laid > 3, "a fire trail actually puts fire on the ground");
    }

    {
        static World w;
        w.reset();
        for (int y = CY_T - 40; y <= CY_T + 40; ++y)
            for (int x = CX_T - 20; x <= CX_T + 220; ++x)
                if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1)
                    w.setCell(x, y, MAT_EMPTY);
        w.setLiveWindow(CX_T - 40, CY_T - 60, CX_T + 240, CY_T + 60);
        entReset();
        projClear();

        /* A creature standing BETWEEN the two shots, level with neither. If the
           arc were merely decoration this takes no damage at all -- which is
           the whole point of the modifier and the thing worth testing. */
        const int e = entSpawn(w, ENT_HUSK, (float)(CX_T + 60), (float)CY_T);
        if (e < 0) { fprintf(stderr, "could not place the target\n"); return 2; }
        g_entities[e].hp = ENT_DEFS[ENT_HUSK].hp;
        const int hp0 = g_entities[e].hp;

        /* Two shots passing above and below it, neither one on its line. */
        projSpawn((float)CX_T, (float)(CY_T - 12), 2.0f, 0.0f, STR_NOTHING, 1,
                  120, 0xFFFFFF, 0, MAT_EMPTY, 12, false, 0.0f,
                  PROJ_EFFECT_NONE, 0, 0.0f, 0xff, 0);
        const int a = projLastSpawnedIndex();
        projSpawn((float)CX_T, (float)(CY_T + 12), 2.0f, 0.0f, STR_NOTHING, 1,
                  120, 0xFFFFFF, 0, MAT_EMPTY, 12, false, 0.0f,
                  PROJ_EFFECT_NONE, 0, 0.0f, 0xff, 0);
        const int b = projLastSpawnedIndex();
        projLink(a, b, MODK_ARC_LIGHTNING);
        for (int f = 0; f < 60; ++f) { projUpdate(w); }
        printf("a creature between two arced shots: %d -> %d hp\n",
               hp0, g_entities[e].hp);
        check(g_entities[e].hp < hp0,
              "an arc hurts what passes between the two shots");

        /* And the same pair with no link must NOT hurt it, or the check above
           is measuring the projectiles rather than the arc. */
        entReset();
        projClear();
        const int e2 = entSpawn(w, ENT_HUSK, (float)(CX_T + 60), (float)CY_T);
        if (e2 >= 0) {
            g_entities[e2].hp = ENT_DEFS[ENT_HUSK].hp;
            const int before = g_entities[e2].hp;
            projSpawn((float)CX_T, (float)(CY_T - 12), 2.0f, 0.0f, STR_NOTHING,
                      1, 120, 0xFFFFFF, 0, MAT_EMPTY, 12, false, 0.0f,
                      PROJ_EFFECT_NONE, 0, 0.0f, 0xff, 0);
            projSpawn((float)CX_T, (float)(CY_T + 12), 2.0f, 0.0f, STR_NOTHING,
                      1, 120, 0xFFFFFF, 0, MAT_EMPTY, 12, false, 0.0f,
                      PROJ_EFFECT_NONE, 0, 0.0f, 0xff, 0);
            for (int f = 0; f < 60; ++f) projUpdate(w);
            printf("the same pair unlinked: %d -> %d hp\n",
                   before, g_entities[e2].hp);
            check(g_entities[e2].hp == before,
                  "and the same two shots WITHOUT an arc leave it alone");
        }
    }

    /* --- and it is craftable ---------------------------------------------- */
    {
        const ItemId want[] = {
            ITEM_MULTITOOL3, ITEM_MOD_DOUBLE, ITEM_MOD_TRAIL_FIRE,
            ITEM_MOD_ARC_LIGHTNING, ITEM_MOD_ARC_FIRE, ITEM_MOD_SEEK,
            ITEM_MOD_SEEK_MOUSE, ITEM_MOD_QUICKEN };
        bool all = true;
        for (int k = 0; k < 8; ++k) {
            bool found = false;
            for (int r = 0; r < N_RECIPES; ++r)
                if (RECIPES[r].out == want[k]) found = true;
            if (!found) { printf("  no recipe for %s\n", ITEMS[want[k]].name); all = false; }
        }
        check(all, "all eight have recipes");
    }

    if (failures) {
        fprintf(stderr, "\n%d shot modifier check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
