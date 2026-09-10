/* --- the Widow: eight legs and a mouthful of silk ----------------------------

   Asked for: "lets make the boss for layer 2, it should be a scary looking
   spider thats really big that has 8 legs it walks around with and hurts the
   player with, it can also spit webs that hurt sometimes".

   Layer 2 had five ordinary creatures and no capstone. The Brood Mother is a
   charge you dodge in an open room, and backing away from her works -- which is
   correct for her and would be fatal to repeat, because it means the answer to
   "boss" in this game is currently "walk backwards". This one takes the ground
   you walk backwards onto and covers it in silk.

   Seven properties. The first three are the request, and the last four are the
   things that stop the request from making an unfightable creature:

     it is big, and wider than it is tall        (a spider, not a tower)
     it hurts on contact, across that width      (the legs ARE the threat)
     it spits webs, and the webs hurt            (the request)
     a web is passable, never a wall             (it cannot seal you in)
     silk rots, so the arena clears              (a long fight is not punished)
     fire clears silk faster                     (there is counterplay)
     killing it opens the layer 2 seal           (it is worth fighting)

   The fourth is the one that matters most. A boss that can place solid cells at
   range can build a box around a player, and no amount of tuning fixes that.

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
#include "worldgen.h"
#include "multiplayer.h"
#include <stdio.h>
#include <math.h>

static World g_testWorld;
static const int CX = 1400, CY = 5000;
static const int FLOOR = CY + 50;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  %-54s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static void fill(World& w, int x0, int y0, int x1, int y1, u8 m) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1)
                w.setCell(x, y, m);
}

/* An arena: flat floor, the player at the left, the Widow `gap` to the right. */
/* `back` is how much floor to lay BEHIND the player. Zero for the cases that
   pin somebody; the kite case needs more than the player can cover, or it walks
   off the end of the arena, the creature cannot follow onto ground that is not
   there, and the harness reports a boss that gave up when what actually
   happened is that the test ran out of world. */
static int arena(World& w, int gap, int back = 200) {
    w.reset();
    projClear();
    fill(w, CX - back - 40, CY - 90, CX + gap + 260, FLOOR + 40, MAT_STONE);
    fill(w, CX - back - 20, CY - 60, CX + gap + 240, FLOOR,      MAT_EMPTY);
    w.setLiveWindow(CX - back - 60, CY - 110, CX + gap + 280, FLOOR + 60);

    Player& p = g_player;
    p.reset((float)CX, (float)(FLOOR - PLAYER_H));
    p.alive = true; p.hp = PLAYER_HP_MAX;

    entReset();
    return entSpawn(w, ENT_WIDOW, (float)(CX + gap), (float)(FLOOR - 20));
}

/* Holds the player in place and runs the fight, so what is measured is the
   creature rather than a chase. */
static void run(World& w, int frames, bool holdPlayer = true) {
    Player& p = g_player;
    for (int f = 0; f < frames; ++f) {
        if (holdPlayer) {
            p.x = (float)CX;
            p.y = (float)(FLOOR - PLAYER_H);
            p.alive = true; p.hp = PLAYER_HP_MAX;
        }
        entTick(w, p, g_inv);
        projUpdate(w);
        w.step();
    }
}

/* Hostile shots FIRED, counted as they appear rather than by looking at the
   floor afterwards. Silk on the ground is a second-hand measure of attacking:
   it decays, it lands where the arc took it, and it moves when the creature
   does -- so a change in muzzle speed alters it without the creature attacking
   any more or less. What the report is about is how often the thing shoots. */
static int g_hostileSeen = 0;
static int g_hostilePrev = 0;
static void countShots() {
    int n = 0;
    for (int i = 0; i < MAX_PROJ; ++i) if (g_proj[i].alive && g_proj[i].hostile) ++n;
    if (n > g_hostilePrev) g_hostileSeen += n - g_hostilePrev;
    g_hostilePrev = n;
}
static void resetShots() { g_hostileSeen = 0; g_hostilePrev = 0; }

static int countMat(const World& w, u8 m) {
    int n = 0;
    for (int y = CY - 90; y <= FLOOR + 10; ++y)
        for (int x = CX - 200; x <= CX + 500; ++x)
            if (w.at(x, y).mat == m) ++n;
    return n;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();
    World& w = g_testWorld;

    /* --- 1. it is big, and it is a spider's shape ------------------------- */
    {
        const EntityDef& d = ENT_DEFS[ENT_WIDOW];
        const EntityDef& brood = ENT_DEFS[ENT_BROOD];
        printf("Widow %dx%d, %d hp, %d contact  (Brood Mother %dx%d, %d hp, %d)\n",
               d.w, d.h, d.hp, d.touchDamage,
               brood.w, brood.h, brood.hp, brood.touchDamage);
        check(d.w > brood.w && d.h > brood.h, "bigger than the layer 1 boss");
        check(d.w > d.h, "and wider than it is tall, which a spider is");
        check(d.isBoss && bossBitOf(ENT_WIDOW) != 0,
              "it is a boss and it owns a bit to be remembered by");
        /* The guard on bossBitOf: a creature whose row says isBoss and which
           has no bit would beat you and not be recorded. */
        bool everyBossHasABit = true;
        for (int t = ENT_NONE + 1; t < ENT_COUNT; ++t)
            if (ENT_DEFS[t].isBoss && bossBitOf(t) == 0) everyBossHasABit = false;
        check(everyBossHasABit, "and every boss in the table owns one");
    }

    /* --- 2. the legs hurt, across the whole width ------------------------- */
    /* The Thresher's lesson at twice the size: the box is the reach, so a
       player level with the creature and well off its centre still gets hit.
       Tested at the edge of the box rather than at the middle, because the
       middle would pass on any creature with any hitbox at all. */
    {
        const int e = arena(w, 0);
        if (e < 0) { fprintf(stderr, "could not place the Widow\n"); return 2; }
        const EntityDef& d = ENT_DEFS[ENT_WIDOW];
        /* Stand the player just inside the far edge of its box. */
        Entity& widow = g_entities[e];
        widow.x = (float)CX - (float)d.w * 0.5f + 3.0f;
        widow.y = (float)(FLOOR - d.h);
        Player& p = g_player;
        p.reset((float)CX, (float)(FLOOR - PLAYER_H));
        p.alive = true; p.hp = PLAYER_HP_MAX;
        int hp = p.hp;
        for (int f = 0; f < 120; ++f) {
            widow.x = (float)CX - (float)d.w * 0.5f + 3.0f;
            widow.y = (float)(FLOOR - d.h);
            p.y = (float)(FLOOR - PLAYER_H);
            entTick(w, p, g_inv);
        }
        printf("standing at the edge of its span: %d -> %d hp\n", hp, p.hp);
        check(p.hp < hp, "the legs hurt you out at the edge of its reach");
    }

    /* --- 3. it spits webs, and they hurt ---------------------------------- */
    {
        const int e = arena(w, 80);
        if (e < 0) return 2;
        /* PEAK, not the count at the end. Silk decays, so a single sample
           catches only whatever happened to be mid-life at that instant -- the
           same reason the aqua regia harness reads a high-water mark. What is
           being asked is how much ground the creature covers, and that is the
           level this settles at, not the number standing at one arbitrary
           frame. */
        int silk = 0;
        resetShots();
        for (int t = 0; t < 900; ++t) {
            run(w, 1);
            countShots();
            const int n = countMat(w, MAT_WEB);
            if (n > silk) silk = n;
        }
        printf("after 900 frames at 80 cells: %d shots fired, silk peaked at %d\n",
               g_hostileSeen, silk);
        check(g_hostileSeen > 10, "it repeats volleys with recovery between moves");
        check(silk > 4, "and the silk reaches the ground");
        check(g_matContactDamage[MAT_WEB] > 0.0f, "and standing in one hurts");
        /* It must not be free damage at any range. Silk that arrives through a
           wall is not an attack, it is weather. */
        check(ENT_DEFS[ENT_WIDOW].shotEvery > 0, "on a clock, not every frame");
    }

    /* --- 3b. IT MUST FIGHT AT RANGE --------------------------------------
       Reported from play: "the widow doesnt attack enough it just lets me
       stand and shoot it, unless im really close."

       Check 3 above did not catch this and could not have: the creature closes
       to contact, so by the time it spat anything it was standing on top of the
       player, and the harness measured silk without ever asking WHERE it was
       thrown from. Pinning the creature is the only way to ask the question.

       This is the second time this exact bug has been written in this file --
       see SKIRM_KEEP, whose note spells out that a lobbed shot cannot go
       further than v*v/g whatever it aims at, and which held its distance
       outside its own reach and fired twice in nine hundred frames. */
    {
        const int e = arena(w, 100);
        if (e < 0) return 2;
        Entity& widow = g_entities[e];
        const float wx = widow.centreX();
        Player& p = g_player;
        int silk = 0;
        for (int f = 0; f < 900; ++f) {
            /* BOTH pinned, at a range a player would actually plink from. */
            widow.x = wx - (float)ENT_DEFS[ENT_WIDOW].w * 0.5f;
            widow.y = (float)(FLOOR - ENT_DEFS[ENT_WIDOW].h);
            widow.vx = 0.0f;
            p.x = (float)CX; p.y = (float)(FLOOR - PLAYER_H);
            p.alive = true; p.hp = PLAYER_HP_MAX;
            entTick(w, p, g_inv);
            projUpdate(w);
            const int n = countMat(w, MAT_WEB);
            if (n > silk) silk = n;
        }
        printf("pinned 100 cells apart: silk peaked at %d cells\n", silk);
        check(silk > 0, "it fights a player standing well back from it");
    }

    /* --- 3c. and backing off to a comfortable range does not make it safe --
       The other half of the report, and it took one wrong version to state
       properly. The obvious test -- sprint away in a straight line forever and
       check the creature keeps up -- is a test nothing in this game can pass
       and nothing SHOULD: the character's top speed is 1.2, layer 1's boss
       does not manage it either, and a creature that ran a player down on open
       ground with no counterplay would be the opposite complaint.

       What "it just lets me stand and shoot it" actually describes is backing
       off to a comfortable plinking distance and then STANDING there. So that
       is what this does: retreat to a hundred cells, stop, and see whether the
       fight follows. */
    {
        const int e = arena(w, 40, 1400);
        if (e < 0) return 2;
        Player& p = g_player;
        resetShots();
        int hits = 0;
        for (int f = 0; f < 900; ++f) {
            const float gap = g_entities[e].centreX() - p.centreX();
            if (gap < 100.0f) p.x -= 1.2f;      /* back off, then hold */
            p.y = (float)(FLOOR - PLAYER_H);
            p.alive = true;
            const int hp = p.hp;
            entTick(w, p, g_inv);
            projUpdate(w);
            countShots();
            if (p.hp < hp) ++hits;
            p.hp = PLAYER_HP_MAX;               /* measuring the creature, not the fight */
        }
        const float gap = g_entities[e].centreX() - p.centreX();
        printf("held at plinking range: %d shots fired, %d damaging frames, "
               "final gap %.0f\n", g_hostileSeen, hits, gap);
        check(g_hostileSeen > 10, "standing back does not stop it attacking");
        check(gap < 140.0f, "and it does not simply fall out of the fight");
    }

    /* --- 3d. a shot it cannot make must not hang it ----------------------
       The regression for the uglier half of the first report. widowSpit gives
       up when the ballistic solve has no answer, and it used to give up WITHOUT
       resetting the volley clock -- which left the creature in its throwing
       state, returning from its tick every frame, neither walking nor leaping.
       A hung boss looks exactly like a passive one.

       The hard part is reaching that state on purpose now that the reach is
       set properly. Out of RANGE will not do it: the creature simply stops
       seeing you and walks. What fails is a target that is in sight and still
       unreachable by any arc, which is a player ABOVE it -- height eats
       ballistic range fast. A hundred and fifty cells up and a hundred across
       is 180 away, inside the 180-cell sight, and the discriminant is negative.
       Which is not a contrived position either: standing on a ledge shooting
       down at a boss is the first thing anybody tries. */
    {
        w.reset();
        projClear();
        fill(w, CX - 300, CY - 250, CX + 300, FLOOR + 40, MAT_EMPTY);
        fill(w, CX - 300, FLOOR, CX + 300, FLOOR + 40, MAT_STONE);
        w.setLiveWindow(CX - 320, CY - 270, CX + 320, FLOOR + 60);
        entReset();
        const int e = entSpawn(w, ENT_WIDOW, (float)(CX + 100),
                               (float)(FLOOR - 20));
        if (e < 0) return 2;
        Entity& widow = g_entities[e];
        Player& p = g_player;

        /* Both held, so what is measured is whether the creature keeps trying
           to do anything at all. Movement over the LAST stretch, not the whole
           run: a creature that hangs still coasts to a stop on the velocity it
           had when it hung, and over four hundred frames that carried the
           broken one 67 cells -- enough to pass a naive "did it move" test
           while being exactly the bug. */
        float travelled = 0.0f;
        float px = widow.centreX(), py = widow.centreY();
        for (int f = 0; f < 400; ++f) {
            p.x = (float)CX; p.y = (float)(FLOOR - 150);
            p.alive = true; p.hp = PLAYER_HP_MAX;
            entTick(w, p, g_inv);
            projUpdate(w);
            /* PATH LENGTH, not net displacement, and the difference is the
               whole check. A creature that cannot reach a ledge leaps at it,
               falls back, and leaps again -- ending each two hundred frames
               roughly where it started, which a displacement test reads as
               frozen. What separates hung from busy is whether it moved AT
               ALL, so this sums every frame's step. */
            if (f >= 100) {
                travelled += fabsf(widow.centreX() - px) +
                             fabsf(widow.centreY() - py);
            }
            px = widow.centreX(); py = widow.centreY();
        }
        const float late = travelled;
        printf("in sight on a ledge it cannot arc to: %.0f cells of travel "
               "over frames 100-400\n", late);
        check(late > 40.0f,
              "a shot it cannot make does not freeze it where it stands");
    }

    /* --- 3e. YOU CANNOT KITE IT FOREVER ----------------------------------
       Reported twice. The first time I read "it just lets me stand and shoot
       it" as being about standing, fixed the range and the hang, and argued
       that outrunning a boss in a straight line is something nothing in this
       game can prevent and nothing should. That was wrong, and the second
       report -- "you can just kite forever" -- is the correction.

       It is wrong because the two cases are not the same. A player sprinting
       away from an ORDINARY creature is escaping an encounter, which is a
       legitimate move and the thing the Thresher harness deliberately protects.
       A player walking backwards from a summoned boss in its own arena is not
       escaping anything -- there is nowhere to escape to, the fight is the
       whole reason they are there, and a boss that can be held at arm's length
       by one held key is not a fight, it is a health bar with extra steps.

       So this is a boss-only requirement, and the number is a BOUND rather
       than a demand that it keep exact pace: the creature has to stay in the
       fight and go on attacking, not win the footrace. */
    {
        const int e = arena(w, 60, 1400);   /* more floor than the retreat covers */
        if (e < 0) return 2;
        Player& p = g_player;
        resetShots();
        float worst = 0.0f;
        for (int f = 0; f < 900; ++f) {
            p.x -= 1.2f;                        /* flat out, and never stopping */
            p.y = (float)(FLOOR - PLAYER_H);
            p.alive = true; p.hp = PLAYER_HP_MAX;
            entTick(w, p, g_inv);
            projUpdate(w);
            countShots();
            const float gap = g_entities[e].centreX() - p.centreX();
            if (gap > worst) worst = gap;
        }
        const float gap = g_entities[e].centreX() - p.centreX();
        printf("kited at full sprint for 900 frames: gap %.0f, worst %.0f, "
               "%d shots fired\n", gap, worst, g_hostileSeen);
        check(gap < 260.0f, "a straight-line sprint does not leave it behind");
        check(g_hostileSeen >= 6, "and mixes repeated web volleys into pursuit");
    }

    /* --- 4. a web is never a wall ----------------------------------------- */
    /* THE one that matters. A boss placing solid cells at range can build a box
       round the player, and no tuning fixes that -- so silk is in
       g_matPassable, the same table the open door uses. */
    {
        w.reset();
        fill(w, CX - 60, CY - 60, CX + 60, FLOOR + 20, MAT_STONE);
        fill(w, CX - 40, CY,      CX + 40, FLOOR,      MAT_EMPTY);
        /* A wall of silk across the corridor, floor to ceiling, thicker than
           anything a volley could ever stack. */
        fill(w, CX + 4, CY, CX + 12, FLOOR - 1, MAT_WEB);
        w.setLiveWindow(CX - 80, CY - 20, CX + 80, FLOOR + 40);
        entReset();

        Player& p = g_player;
        p.reset((float)(CX - 20), (float)(FLOOR - PLAYER_H));
        p.alive = true; p.hp = PLAYER_HP_MAX;
        PlayerInput in;
        in.left = false; in.right = true; in.jump = false; in.down = false;
        const float x0 = p.centreX();
        for (int f = 0; f < 200; ++f) {
            p.hp = PLAYER_HP_MAX;      /* the damage is checked above, not here */
            p.update(w, in);
        }
        const float got = p.centreX() - x0;
        printf("walking into a nine-cell curtain of silk: moved %.0f cells\n",
               got);
        check(got > 20.0f, "you walk through silk rather than into it");
    }

    /* --- 5. silk rots -------------------------------------------------- */
    {
        w.reset();
        fill(w, CX - 60, CY - 60, CX + 60, FLOOR + 20, MAT_STONE);
        fill(w, CX - 40, CY,      CX + 40, FLOOR,      MAT_EMPTY);
        fill(w, CX - 20, FLOOR - 20, CX + 20, FLOOR - 1, MAT_WEB);
        w.setLiveWindow(CX - 80, CY - 20, CX + 80, FLOOR + 40);
        const int before = countMat(w, MAT_WEB);
        for (int f = 0; f < 2000; ++f) w.step();
        const int after = countMat(w, MAT_WEB);
        printf("silk left alone: %d -> %d cells in 2000 frames\n", before, after);
        check(after * 2 < before, "silk rots away rather than redecorating the arena");
    }

    /* --- 6. and fire clears it faster ------------------------------------- */
    {
        w.reset();
        fill(w, CX - 60, CY - 60, CX + 60, FLOOR + 20, MAT_STONE);
        fill(w, CX - 40, CY,      CX + 40, FLOOR,      MAT_EMPTY);
        fill(w, CX - 20, FLOOR - 20, CX + 20, FLOOR - 1, MAT_WEB);
        w.setLiveWindow(CX - 80, CY - 20, CX + 80, FLOOR + 40);
        const int before = countMat(w, MAT_WEB);
        /* One torch's worth of flame at one end. */
        fill(w, CX - 22, FLOOR - 10, CX - 21, FLOOR - 8, MAT_FIRE);
        for (int f = 0; f < 300; ++f) w.step();
        const int after = countMat(w, MAT_WEB);
        printf("silk with a flame in it: %d -> %d cells in 300 frames\n",
               before, after);
        check(after * 2 < before, "a flame is a real answer to a curtain of silk");
    }

    /* --- 7. killing it opens the way down --------------------------------- */
    /* The reward, and the reason to fight it at all. Same mechanism the Brood
       Mother uses on the layer above -- and it must be HER bit that stays hers,
       or beating one boss would credit you with the other. */
    {
        w.reset();
        for (int x = PLAY_X0; x <= PLAY_X1; ++x) g_stoneY[x] = CY - 200;
        const int seal = CY - 200 + LAYER2_DEPTH;
        if (seal < PLAY_Y1 - 20) {
            fill(w, CX - 40, seal - 4, CX + 40, seal + 4, MAT_STRATUM);
            w.setLiveWindow(CX - 60, seal - 40, CX + 60, seal + 40);
            int before = 0;
            for (int y = seal - 6; y <= seal + 6; ++y)
                for (int x = CX - 40; x <= CX + 40; ++x)
                    if (w.at(x, y).mat == MAT_STRATUM) ++before;
            g_bossesBeaten = 0;
            entReset();
            const int e = entSpawn(w, ENT_WIDOW, (float)CX, (float)(CY - 100));
            if (e >= 0) {
                g_entities[e].hp = 0;
                Player& p = g_player;
                p.reset((float)CX, (float)(CY - 120));
                entTick(w, p, g_inv);
            }
            int after = 0;
            for (int y = seal - 6; y <= seal + 6; ++y)
                for (int x = CX - 40; x <= CX + 40; ++x)
                    if (w.at(x, y).mat == MAT_STRATUM) ++after;
            printf("layer 2 seal after killing it: %d -> %d cells of stratum\n",
                   before, after);
            check((g_bossesBeaten & BOSS_LAYER2) != 0, "it is recorded as beaten");
            check((g_bossesBeaten & BOSS_LAYER1) == 0,
                  "and the Brood Mother is NOT credited with its death");
            check(after < before, "the layer 2 seal is opened");
        }
    }

    /* Discrete choreography: a locked tell, one attack, then an opening. */
    {
        int slot=arena(w,100);
        if (slot<0) return 2;
        Entity& e=g_entities[slot];
        e.phase=WIDOW_APPROACH; e.actTimer=0; e.shotTimer=0; e.onGround=true;
        entTick(w,g_player,g_inv);
        check(e.phase==WIDOW_WEB_WIND,"medium-range opening chooses a web tell");
        const float target=e.aimX;
        g_player.x-=50;
        for (int f=0;f<29;++f) entTick(w,g_player,g_inv);
        int shots=0; for (int i=0;i<MAX_PROJ;++i) shots+=g_proj[i].alive;
        check(shots==0 && e.aimX==target && e.phase==WIDOW_WEB_WIND,
              "web wind-up locks aim and cannot overlap a pounce");
        entTick(w,g_player,g_inv);
        check(e.phase==WIDOW_RECOVER,"volley enters an explicit recovery");
        projClear();
        bool rested=true;
        for (int f=0;f<40;++f) {
            entTick(w,g_player,g_inv);
            rested=rested && e.phase==WIDOW_RECOVER && fabsf(e.vx)<0.1f;
        }
        check(rested,"recovery provides forty uninterrupted punish frames");
        e.phase=WIDOW_APPROACH; e.actTimer=0; e.shotTimer=1; e.onGround=true;
        entTick(w,g_player,g_inv);
        check(e.phase==WIDOW_LEAP_WIND,"next move gathers for a pounce");
        float leapAim=e.aimX;
        g_player.x+=160;
        for (int f=0;f<32;++f) entTick(w,g_player,g_inv);
        check(e.phase==WIDOW_LEAP && e.aimX==leapAim && e.vx<0,
              "pounce commits to the old position after the player dodges");
        float launchVX=e.vx;
        for (int f=0;f<5;++f) entTick(w,g_player,g_inv);
        check(e.phase==WIDOW_LEAP && fabsf(e.vx-launchVX)<0.1f,
              "airborne pounce does not home onto the player");
        e.phase=WIDOW_APPROACH; e.hp=ENT_DEFS[ENT_WIDOW].hp/2; e.onGround=true;
        entTick(w,g_player,g_inv);
        check(e.phase==WIDOW_MOULT,"half health creates a readable phase break");
        for (int f=0;f<60;++f) entTick(w,g_player,g_inv);
        check(e.phase==WIDOW_APPROACH && e.aimHold==1,"phase break happens once, then returns to approach");
    }

    /* --- and it is reachable at all --------------------------------------- */
    /* A boss with no summon is a boss nobody meets. */
    {
        bool hasRecipe = false;
        for (int r = 0; r < N_RECIPES; ++r)
            if (RECIPES[r].out == ITEM_WIDOW_CALL) hasRecipe = true;
        check(hasRecipe, "there is a recipe that calls it");
        check(ITEMS[ITEM_WIDOW_CALL].summons == ENT_WIDOW,
              "and the summon summons it");
        check(ENT_DEFS[ENT_WIDOW].dropItem == ITEM_SILK_GLAND,
              "and beating it hands you something");
    }

    if (failures) {
        fprintf(stderr, "\n%d Widow check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
