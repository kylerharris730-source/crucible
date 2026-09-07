/* --- a charm for every creature ----------------------------------------------

   Asked for: "i think there are more we can implement that drop from all of the
   new enemies, I think generally each enemy should have some accesory drop."

   The six layer-1 creatures have dropped a charm since charms existed. Every
   creature in layers 2 and 3 dropped currency and nothing else, which is the
   hole this fills: ten more, one each.

   What a harness can usefully check here is not "does 30% mean 30%" -- that is
   the table restating itself -- but the two claims that are easy to get wrong
   and invisible when they are:

     every creature that can be fought has a charm     (the ask, enforced)
     no two creatures drop the same one                (ten charms, not one x10)
     each charm is reachable: it is an ACCESSORY,      (a charm you cannot put
     it fits a trinket slot, and it says what it does   on is a decoration)
     and the ones with real behaviour behave           (measured, below)

   Compile with every src cpp file except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "player.h"
#include "accessory.h"
#include "multiplayer.h"
#include <stdio.h>
#include <string.h>

static World g_testWorld;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

/* Everything that can be met in the world and killed. Bosses are excluded --
   they pay out a core, and their charms are a separate question -- as are the
   parts, which are furniture, and the tame bees. */
static bool huntable(int type) {
    const EntityDef& d = ENT_DEFS[type];
    if (d.isBoss || d.tame) return false;
    if (type == ENT_DUMMY) return false;
    /* A creature nothing can spawn and no egg can make is not in the game. */
    return d.layerMask != 0 || d.surfaceAtNight;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();

    /* --- 1. every creature drops one, and they are all different ---------- */
    {
        int missing = 0, dupes = 0, counted = 0;
        for (int t = ENT_NONE + 1; t < ENT_COUNT; ++t) {
            if (!huntable(t)) continue;
            ++counted;
            const ItemId charm = ENT_DEFS[t].rareDrop;
            if (charm == ITEM_NONE || ENT_DEFS[t].rareOneIn <= 0) {
                printf("  %s drops no charm\n", ENT_DEFS[t].name);
                ++missing;
                continue;
            }
            for (int u = ENT_NONE + 1; u < t; ++u)
                if (huntable(u) && ENT_DEFS[u].rareDrop == charm) {
                    printf("  %s and %s share %s\n", ENT_DEFS[u].name,
                           ENT_DEFS[t].name, ITEMS[charm].name);
                    ++dupes;
                }
        }
        printf("%d huntable creatures: %d without a charm, %d shared\n",
               counted, missing, dupes);
        check(counted >= 16, "there are creatures to check");
        check(missing == 0, "every creature you can hunt drops a charm");
        check(dupes == 0, "and no two of them drop the same one");
    }

    /* --- 2. and every charm can actually be worn -------------------------- */
    {
        int bad = 0;
        for (int t = ENT_NONE + 1; t < ENT_COUNT; ++t) {
            if (!huntable(t)) continue;
            const ItemId charm = ENT_DEFS[t].rareDrop;
            if (charm == ITEM_NONE) continue;
            const ItemDef& d = ITEMS[charm];
            if (d.kind != ITEMK_ACCESSORY || !equipFits(charm, EQ_TRINKET_A) ||
                d.description == 0 || d.sprite == SPR_NONE) {
                printf("  %s is not a wearable, described, drawn charm\n", d.name);
                ++bad;
            }
        }
        check(bad == 0, "every charm fits a trinket slot and says what it does");
    }

    /* --- 3. the four charms with a memory --------------------------------- */
    Inventory& inv = g_playerSessions[0].inventory;
    Player& body = g_playerSessions[0].body;
    g_playerSessions[0].connected = true;
    {
        /* Momentum ramps while moving and collapses when you stop. Measured at
           both ends rather than at one: a counter that only ever goes up would
           pass a check that looked for the bonus and would be a permanent
           damage buff wearing a Thresher's name. */
        accessoryReset();
        inv.clear();
        inv.equip[EQ_TRINKET_A].item = ITEM_THRESHING_SPURS;
        inv.equip[EQ_TRINKET_A].count = 1;
        body.reset(400.0f, 400.0f);
        body.alive = true;
        const int cold = accessoryMomentumPct(0, inv);
        for (int f = 0; f < 200; ++f) { body.vx = 1.2f; accessoryTickFor(0, body, inv); }
        const int hot = accessoryMomentumPct(0, inv);
        for (int f = 0; f < 5; ++f) { body.vx = 0.0f; accessoryTickFor(0, body, inv); }
        const int stopped = accessoryMomentumPct(0, inv);
        printf("Threshing Spurs: %d%% from a standstill, %d%% after 200 frames "
               "running, %d%% once stopped\n", cold, hot, stopped);
        check(cold == 0 && hot >= 40 && stopped == 0,
              "the Spurs ramp while you move and collapse when you stop");
    }
    {
        /* The sprint has to care about DIRECTION, or it is the Swift Charm
           with extra steps: jiggling on the spot would pay the same as
           committing to a run. */
        accessoryReset();
        inv.clear();
        inv.equip[EQ_TRINKET_A].item = ITEM_ASHHOUND_COLLAR;
        inv.equip[EQ_TRINKET_A].count = 1;
        for (int f = 0; f < 240; ++f) { body.vx = 1.2f; accessoryTickFor(0, body, inv); }
        const int oneWay = accessorySprintPct(0, inv);
        accessoryReset();
        for (int f = 0; f < 240; ++f) {
            body.vx = (f & 8) ? 1.2f : -1.2f;
            accessoryTickFor(0, body, inv);
        }
        const int jiggling = accessorySprintPct(0, inv);
        printf("Ashhound Collar: %d%% running one way, %d%% changing direction\n",
               oneWay, jiggling);
        check(oneWay >= 30, "the Collar pays for running one way");
        check(jiggling < oneWay / 2, "and not for jiggling on the spot");
    }
    {
        /* The Loader arms itself by NOT firing, and firing disarms it. */
        accessoryReset();
        inv.clear();
        inv.equip[EQ_TRINKET_A].item = ITEM_CULVERIN_LOADER;
        inv.equip[EQ_TRINKET_A].count = 1;
        for (int f = 0; f < 130; ++f) { body.vx = 0.0f; accessoryTickFor(0, body, inv); }
        const bool armed = accessoryBurstReady(0, inv);
        accessoryNoteShot(0);
        const bool spent = accessoryBurstReady(0, inv);
        printf("Culverin Loader: armed after 130 idle frames %s, still armed "
               "after firing %s\n", armed ? "yes" : "no", spent ? "yes" : "no");
        check(armed && !spent, "the Loader arms by holding fire and spends on a shot");
    }
    {
        /* And the trail goes BEHIND you, onto a floor. Both halves matter: fire
           under your feet would be a charm that kills the wearer, and fire in
           mid-air drifts somewhere nobody chose. */
        accessoryReset();
        inv.clear();
        inv.equip[EQ_TRINKET_A].item = ITEM_CINDERLING_ASH;
        inv.equip[EQ_TRINKET_A].count = 1;
        World& w = g_testWorld;
        w.reset();
        const int FX = 900, FY = 900;
        for (int y = FY; y <= FY + 4; ++y)
            for (int x = FX - 60; x <= FX + 60; ++x) w.setCell(x, y, MAT_STONE);
        w.setLiveWindow(FX - 80, FY - 60, FX + 80, FY + 20);
        body.reset((float)FX, (float)(FY - PLAYER_H / 2));
        body.alive = true; body.onGround = true; body.vx = 1.2f;
        int ahead = 0, behind = 0;
        for (int f = 0; f < 60; ++f) {
            accessoryTickFor(0, body, inv);
            accessoryAshTrail(0, body, inv, w);
        }
        for (int y = FY - PLAYER_H; y < FY; ++y) {
            for (int x = FX - 40; x < body.left(); ++x)
                if (w.at(x, y).mat == MAT_FIRE) ++behind;
            for (int x = body.right() + 1; x <= FX + 40; ++x)
                if (w.at(x, y).mat == MAT_FIRE) ++ahead;
        }
        printf("Cinderling Ash, running right: %d cells of fire behind, %d ahead\n",
               behind, ahead);
        check(behind > 0, "the Ash leaves fire behind a running player");
        check(ahead == 0, "and never in front of them");
    }

    /* --- 4. the flat ones are wired to something -------------------------- */
    {
        inv.clear();
        inv.equip[EQ_TRINKET_A].item = ITEM_SHAMBLER_BALLAST;
        inv.equip[EQ_TRINKET_A].count = 1;
        check(inv.contactResistPct() == 30, "the Ballast reaches contact damage");
        inv.equip[EQ_TRINKET_A].item = ITEM_WISP_PRISM;
        check(inv.piercePlus() == 2, "the Prism reaches pierce");
        inv.equip[EQ_TRINKET_A].item = ITEM_SKIRMISHER_CELL;
        check(inv.energyBonus() == 2, "the Cell reaches tool recharge");
        inv.equip[EQ_TRINKET_A].item = ITEM_STOOPER_TALON;
        check(inv.fallGuardPct() == 100, "the Talon reaches fall damage");
        inv.equip[EQ_TRINKET_A].item = ITEM_EMBERWING_FEATHER;
        check(inv.airJumps() == 1, "the Feather reaches the jump budget");
        /* Largest, never summed -- the rule every column above them follows. */
        inv.equip[EQ_TRINKET_B].item = ITEM_EMBERWING_FEATHER;
        inv.equip[EQ_TRINKET_B].count = 1;
        check(inv.airJumps() == 1, "and two of one charm is still one charm");
    }

    /* --- 5. the extra jump is a fresh press, and landing refills it -------- */
    {
        World& w = g_testWorld;
        w.reset();
        const int FX = 900, FY = 900;
        for (int y = FY; y <= FY + 4; ++y)
            for (int x = FX - 40; x <= FX + 40; ++x) w.setCell(x, y, MAT_STONE);
        w.setLiveWindow(FX - 60, FY - 200, FX + 60, FY + 20);

        PlayerInput in;
        memset(&in, 0, sizeof(in));
        float withCharm = 0.0f, without = 0.0f;
        for (int pass = 0; pass < 2; ++pass) {
            body.reset((float)FX, (float)(FY - PLAYER_H / 2));
            body.alive = true;
            body.airJumps = pass;            /* 0: nothing worn, 1: the Feather */
            body.airJumpsUsed = 0;
            /* SETTLE FIRST. reset() places the box by its centre, so the
               character starts a little above the floor and is airborne on
               frame zero -- and the first press of the jump key was therefore
               spending the air jump rather than leaving the ground. The probe
               that found this reported the budget used on frame 0 of every
               jump, which is exactly what the bug it was looking for would
               have looked like. */
            in.jump = false;
            for (int f = 0; f < 30; ++f) body.update(w, in);
            float peak = 0.0f;
            const float start = body.y;
            for (int f = 0; f < 90; ++f) {
                /* Held for the first stretch, released, then pressed again AT
                   THE APEX. Both halves of that are load-bearing. Releasing
                   matters because a charm that fired on the held key would
                   make the two passes identical; the apex matters because the
                   peak is measured from the start, so a second jump taken
                   halfway back down climbs to nearly the same height as the
                   first and reports as nothing happening -- which is what the
                   version pressing at frame 24 measured, one cell of
                   difference from a jump that had fired perfectly well.

                   2.6 cells a frame against 0.18 gravity puts the apex around
                   frame 14. */
                in.jump = f < 8 || (f >= 14 && f < 20);
                body.update(w, in);
                const float risen = start - body.y;
                if (risen > peak) peak = risen;
            }
            if (pass) withCharm = peak; else without = peak;
        }
        printf("jump peak: %.0f cells bare, %.0f cells with the Feather\n",
               without, withCharm);
        check(withCharm > without + 8.0f, "the Feather buys a second jump");
    }

    if (failures) {
        fprintf(stderr, "\n%d charm check(s) failed\n", failures);
        return 1;
    }
    printf("\nevery creature pays out something you can wear\n");
    return 0;
}
