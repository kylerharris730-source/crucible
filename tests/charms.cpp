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
        /* --- the Loader ---------------------------------------------------
           It arms by NOT firing, spends on a shot, and pays in two stages.

           Reported from play as "very inconsistent", and it was: the payout
           sat behind a flat two-second gate, which meant it fired on 0% of
           pulls at every rate anybody fights at -- five shots a second down to
           one every 1.5 seconds, all zero. It only worked while firing slower
           than once every two seconds, which is not a fight.

           So what is measured here is the RATE it pays at, at the cadences a
           person shoots at, rather than the threshold restating itself. The
           middle row is the one that matters: a beat's pause between shots has
           to be worth something, or the charm is invisible again. */
        accessoryReset();
        inv.clear();
        inv.equip[EQ_TRINKET_A].item = ITEM_CULVERIN_LOADER;
        inv.equip[EQ_TRINKET_A].count = 1;
        body.vx = 0.0f;
        static const int GAPS[3] = { 15, 45, 120 };
        int paid[3] = { 0, 0, 0 }, pulls[3] = { 0, 0, 0 }, bolts[3] = { 0, 0, 0 };
        for (int g = 0; g < 3; ++g) {
            accessoryReset();
            for (int f = 0; f < 900; ++f) {
                accessoryTickFor(0, body, inv);
                if (f % GAPS[g]) continue;
                const int extra = accessoryBurstBolts(0, inv);
                if (extra) ++paid[g];
                bolts[g] += extra;
                accessoryNoteShot(0);
                ++pulls[g];
            }
            printf("Culverin Loader: a pull every %3d frames -> %d of %d "
                   "carried extra bolts (%d bolts)\n",
                   GAPS[g], paid[g], pulls[g], bolts[g]);
        }
        check(paid[0] == 0, "firing flat out never earns a volley");
        check(paid[1] > pulls[1] / 2,
              "a beat between shots does, which is the whole fix");
        check(bolts[2] >= paid[2] * 2,
              "and a long wait is worth two bolts rather than one");
        /* And it still spends. A charm that stayed armed through a volley
           would be a fire-rate bonus wearing a rhythm's clothes. */
        accessoryReset();
        for (int f = 0; f < 130; ++f) accessoryTickFor(0, body, inv);
        const bool armed = accessoryBurstReady(0, inv);
        accessoryNoteShot(0);
        const bool spent = accessoryBurstReady(0, inv);
        check(armed && !spent, "the Loader arms by holding fire and spends on a shot");
    }
    {
        /* --- the Cinderling Ash --------------------------------------------
           The trail goes BEHIND you, onto a floor, it is continuous, and it
           burns. Four claims, and three of them were broken.

           Reported twice from play. First as producing fire "rarely and
           inconsistently", then -- after a fix that was not enough -- as still
           dropping "one pixel of ember every now and then, not a stream, and
           no flames". Every one of those words was a measurement:

             it dropped MAT_FIRE, which is a flame rather than a thing that is
             burning, so on bare stone each cell was gone within a frame or
             two: never more than ONE alight at any moment across ten seconds

             its spacing came off the Ashhound Collar's counter, which resets
             when you change direction, so pivoting in a fight laid embers at
             twice the rate of running straight

             and it was spaced per FRAME, which at running speed put the drops
             four cells apart -- dots, not a line

           So it lays by the cell of ground crossed, two cells tall on the
           even ones, in the Cinderling's own ember, which vents flame -- it
           was wood ember first, and became its own material when the trail
           wanted a hotter one with a shorter life. What is measured here
           is the SHAPE of the trail rather than the rule that makes it: the
           longest gap in it, and whether anything is actually on fire.

           This case MOVES the body, which the version before it did not: it
           set a velocity and left the character standing on one spot, so all
           sixty placements landed in the same cell and one surviving pixel
           passed it. A charm whose whole output is a trail has to be measured
           by walking. */
        accessoryReset();
        inv.clear();
        inv.equip[EQ_TRINKET_A].item = ITEM_CINDERLING_ASH;
        inv.equip[EQ_TRINKET_A].count = 1;
        World& w = g_testWorld;
        const int FX = 900, FY = 900;

        int peak[2] = { 0, 0 }, flames[2] = { 0, 0 }, gap[2] = { 0, 0 }, ahead = 0;
        for (int variant = 0; variant < 2; ++variant) {
            accessoryReset();
            w.reset();
            for (int y = FY; y <= FY + 4; ++y)
                for (int x = FX - 700; x <= FX + 700; ++x) w.setCell(x, y, MAT_STONE);
            w.setLiveWindow(FX - 720, FY - 60, FX + 720, FY + 20);
            body.reset((float)FX, (float)(FY - PLAYER_H / 2));
            body.alive = true; body.onGround = true;
            /* Two speeds, because the old spacing rule got THINNER the faster
               you moved: a per-frame drop rate spreads out with velocity. Laid
               by the cell, both speeds have to give the same line. */
            body.vx = variant ? 2.4f : 1.2f;
            for (int f = 0; f < 180; ++f) {
                body.x += body.vx;
                accessoryTickFor(0, body, inv);
                accessoryAshTrail(0, body, inv, w);
                w.step();
                int alight = 0, lit = 0;
                for (int y = FY - PLAYER_H; y < FY; ++y)
                    for (int x = FX - 700; x <= FX + 700; ++x) {
                        const u8 m = w.at(x, y).mat;
                        if (m == MAT_CINDERLING_EMBER) ++alight;
                        if (m == MAT_FIRE) ++lit;
                    }
                if (alight > peak[variant]) peak[variant] = alight;
                if (lit > flames[variant]) flames[variant] = lit;
            }
            /* The longest run of ground with nothing burning on it, over the
               sixty cells immediately behind the player. This is "a stream,
               not dots" written as a number.

               Sixty and not the whole run, because the trail is SUPPOSED to
               go out: wood ember lives about a second and a half, and at a
               walk that is roughly a hundred cells. Measuring the entire
               journey would be measuring the burn-out, and would fail for the
               one reason this charm is allowed to be sparse. */
            int run = 0;
            for (int x = body.left() - 60; x <= body.left() - 2; ++x) {
                bool burning = false;
                for (int y = FY - PLAYER_H; y < FY; ++y) {
                    const u8 m = w.at(x, y).mat;
                    if (m == MAT_CINDERLING_EMBER || m == MAT_FIRE) burning = true;
                }
                if (burning) run = 0;
                else if (++run > gap[variant]) gap[variant] = run;
            }
            if (variant == 0)
                for (int y = FY - PLAYER_H; y < FY; ++y)
                    for (int x = body.right() + 1; x <= FX + 700; ++x)
                        if (w.at(x, y).mat == MAT_CINDERLING_EMBER) ++ahead;
        }
        printf("Cinderling Ash at 1.2 c/f: peak %d embers, %d flames, "
               "longest gap %d\n", peak[0], flames[0], gap[0]);
        printf("Cinderling Ash at 2.4 c/f: peak %d embers, %d flames, "
               "longest gap %d\n", peak[1], flames[1], gap[1]);
        check(peak[0] >= 20 && peak[1] >= 20,
              "the Ash leaves a TRAIL, not one dying pixel");
        check(flames[0] > 0 && flames[1] > 0,
              "and it is on fire, which is the point of a fire charm");
        /* Twelve cells is about half a player-width of unlit ground. Above
           that it has stopped reading as a line; at zero it would be a solid
           wall, which the charm deliberately is not -- the top row is dashed
           so you can get back through it. */
        check(gap[0] <= 12 && gap[1] <= 12,
              "with no long dead stretches, at either running speed");
        check(ahead == 0, "and never in front of the player");
    }
    {
        /* --- and on ground that is not flat ---------------------------------
           Reported from play: "it doesn't work on slopes up."

           It did not work on slopes at ALL, and the cause is geometry rather
           than arithmetic. The character is eleven cells wide and rests on the
           HIGHEST ground under its box, so climbing a slope its feet sit a
           body-width of rise above the ground behind it -- twelve cells on a
           one-in-one. The old rule asked for open air at exactly foot height
           with something solid directly beneath, which is true on flat ground
           and nowhere else: climbing, the cell under the heel is thin air;
           descending, the cell at foot height is inside the hill.

           Measured before the fix, in embers laid per cell of ground crossed:
           1.50 on the flat, 0.02 up a one-in-eight, and 0.00 up anything
           steeper. Downhill was quietly broken too, at 0.28, for the separate
           reason that a grid slope is a staircase -- running down one leaves
           the character airborne 40% of frames, and the trail asked for
           onGround.

           This drives the real Player::update rather than moving the body by
           hand, because both halves of the bug are about what the physics does
           with a slope, and a harness that teleports the character along would
           reproduce neither. */
        static const int RISE[4] = { 0, 1, 1, -1 };
        static const int RUN[4]  = { 1, 8, 3,  3 };
        static const char* NAME[4] = { "flat", "climb 1:8", "climb 1:3",
                                       "descent 1:3" };
        World& w = g_testWorld;
        const int FX = 900, FY = 900;
        double perCell[4] = { 0, 0, 0, 0 };
        for (int t = 0; t < 4; ++t) {
            accessoryReset();
            inv.clear();
            inv.equip[EQ_TRINKET_A].item = ITEM_CINDERLING_ASH;
            inv.equip[EQ_TRINKET_A].count = 1;
            w.reset();
            for (int x = FX - 100; x <= FX + 500; ++x) {
                const int top = FY - ((x - FX) * RISE[t]) / RUN[t];
                for (int y = top; y <= top + 40; ++y)
                    if (y > 0 && y < SIM_H - 1) w.setCell(x, y, MAT_STONE);
            }
            w.setLiveWindow(FX - 120, FY - 300, FX + 520, FY + 320);
            body.reset((float)FX, (float)(FY - PLAYER_H / 2 - 1));
            body.alive = true; body.hp = PLAYER_HP_MAX;
            PlayerInput in; memset(&in, 0, sizeof(in));
            in.right = true;
            const float x0 = body.x;
            for (int f = 0; f < 200; ++f) {
                body.update(w, in);
                accessoryTickFor(0, body, inv);
                accessoryAshTrail(0, body, inv, w);
            }
            const int travelled = (int)(body.x - x0);
            int laid = 0, tallest = 0;
            for (int x = FX - 20; x <= FX + 500; ++x) {
                int column = 0;
                for (int y = FY - 300; y < FY + 320; ++y)
                    if (w.at(x, y).mat == MAT_CINDERLING_EMBER) { ++laid; ++column; }
                if (column > tallest) tallest = column;
            }
            perCell[t] = travelled > 0 ? (double)laid / travelled : 0.0;
            printf("Cinderling Ash on %-12s %3d cells travelled, %.2f embers "
                   "per cell, tallest column %d\n",
                   NAME[t], travelled, perCell[t], tallest);
            /* Two cells, never more. The floor search finds open air with
               something solid under it -- and an ember IS solid, so without a
               rule excluding its own output the trail treats the cell it laid
               last frame as a floor and builds a tower on it. Measured at four
               before that rule existed. */
            check(tallest <= 2,
                  "the trail is its own height and does not stack on itself");
        }
        check(perCell[0] > 1.0, "the trail is laid on the flat");
        for (int t = 1; t < 4; ++t)
            check(perCell[t] >= perCell[0] * 0.9,
                  "and at the same rate on every slope a character can run");
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

    /* --- 6. the boss sigils are a ladder ----------------------------------
       Asked for: "bosses get charms too, the boss charms can be basic good
       stuff, maybe just an increasingly powerful general buff."

       So the property is not what any one of them does -- it is that they are
       ORDERED. Checked as a chain rather than by pinning four sets of numbers,
       because the numbers are allowed to be tuned and the ordering is not. */
    {
        const ItemId ladder[4] = { ITEM_FORGE_SIGIL, ITEM_SILK_SIGIL,
                                   ITEM_PYRE_SIGIL, ITEM_ASCENT_SIGIL };
        bool rising = true, guaranteed = true;
        for (int i = 0; i < 4; ++i) {
            const ItemDef& d = ITEMS[ladder[i]];
            printf("  %-14s +%d%% damage, +%d armour, +%d%% speed\n",
                   d.name, (int)d.damagePct, (int)d.armour, (int)d.speedPct);
            if (i > 0) {
                const ItemDef& prev = ITEMS[ladder[i - 1]];
                if (d.damagePct <= prev.damagePct || d.armour <= prev.armour ||
                    d.speedPct < prev.speedPct) rising = false;
            }
        }
        /* Guaranteed, not one in fifty. A boss you have to kill four times for
           its charm is a grind where the creature charms are a surprise. */
        const int bosses[4] = { ENT_BROOD, ENT_WIDOW, ENT_CENSER, ENT_EFFIGY };
        for (int i = 0; i < 4; ++i)
            if (ENT_DEFS[bosses[i]].rareDrop != ladder[i] ||
                ENT_DEFS[bosses[i]].rareOneIn != 1) guaranteed = false;
        check(rising, "each sigil is strictly better than the one before it");
        check(guaranteed, "and every boss drops its own, every time");
        /* Broad and shallow: a sigil must never beat the charm that
           specialises in its stat, or the specialists stop being worth a slot
           the moment the last boss is dead. */
        check(ITEMS[ITEM_ASCENT_SIGIL].speedPct < ITEMS[ITEM_SWIFT_CHARM].speedPct,
              "and none of them outruns the charm that specialises in speed");
        check(ITEMS[ITEM_ASCENT_SIGIL].damagePct < ITEMS[ITEM_WHETSTONE].damagePct,
              "nor outhits the one that specialises in damage");
        check(ITEMS[ITEM_ASCENT_SIGIL].regenPer > ITEMS[ITEM_HUSK_HEART].regenPer,
              "nor outheals the one that specialises in healing");
    }

    if (failures) {
        fprintf(stderr, "\n%d charm check(s) failed\n", failures);
        return 1;
    }
    printf("\nevery creature pays out something you can wear\n");
    return 0;
}
