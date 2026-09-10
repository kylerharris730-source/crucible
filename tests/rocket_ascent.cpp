/* --- the ascent, and what it leaves behind -----------------------------------

   Stage three of ENDGAME.md: ignition spends the cargo, the rocket climbs out
   of the world carrying its crew, and what is left is a pad, a victory flag
   and a crew standing safely on the ground.

   The win screen itself is in main.cpp and no harness links it, so what is
   checked here is everything the screen is a picture OF -- which is the right
   split anyway: a screen that draws the wrong thing is a bad afternoon, and a
   crew left two hundred cells in the air by a quit is a lost save.

   Seven properties:

     ignition spends the core and the fuel, and only at ignition
     the hull leaves the grid rather than standing there as solid air
     it climbs, and the crew climb with it
     the ascent ends by putting the crew back on the ground, alive
     the victory flag is set, and survives a save and a reload
     a world that was never won does not load as won
     and the pad that is left cannot be pocketed as a free rocket

   Compile with every src cpp file except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "device.h"
#include "player.h"
#include "multiplayer.h"
#include "save.h"
#include <stdio.h>
#include <string.h>

static World g_testWorld;
static const int CX = 1400, GROUND = 5000;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static Device* pad(World& w) {
    w.reset();
    devClear();
    playerSessionsReset();
    rocketSetVictory(false);
    for (int y = GROUND - 400; y <= GROUND + 20; ++y)
        for (int x = CX - 240; x <= CX + 240; ++x)
            w.setCell(x, y, y >= GROUND ? MAT_STONE : MAT_EMPTY);
    w.setLiveWindow(CX - 260, GROUND - 420, CX + 260, GROUND + 40);
    if (!devPlace(w, DEV_ROCKET, CX, GROUND)) return 0;
    Device* d = devAt(CX, GROUND - 1);
    if (!d) return 0;
    rocketSetCore(*d, true);
    d->mat = MAT_FUEL;
    d->count = ROCKET_FUEL_NEED;
    PlayerSession& host = g_playerSessions[0];
    host.connected = true;
    host.body.reset((float)CX, (float)(GROUND - PLAYER_H / 2));
    host.body.alive = true;
    host.body.hp = PLAYER_HP_MAX;
    rocketToggleReady(*d, 0);
    return d;
}

static void run(World& w, int frames) {
    for (int f = 0; f < frames; ++f) devTick(w);
}

static int held(const Inventory& inv, ItemId item) {
    int n = 0;
    for (int i = 0; i < INV_SLOTS; ++i)
        if (inv.slot[i].item == item) n += (int)inv.slot[i].count;
    return n;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    World& w = g_testWorld;

    /* --- 1. ignition, and not one frame before it ------------------------- */
    {
        Device* d = pad(w);
        if (!d) { fprintf(stderr, "could not stand a rocket up\n"); return 2; }
        rocketBeginLaunch(w, *d, 0);
        run(w, ROCKET_COUNTDOWN_FRAMES - 1);
        check(rocketCore(*d) && rocketFuel(*d) == ROCKET_FUEL_NEED,
              "one frame short of ignition, the cargo is still aboard");
        /* Which is the whole reason a cancel is free -- ENDGAME.md asks for
           the spend to happen at ignition and nowhere earlier. */
        run(w, 1);
        check(rocketStage(*d) == ROCKET_LIT, "then it lights");
        check(!rocketCore(*d) && rocketFuel(*d) == 0,
              "and that is when the core and the fuel are spent");
    }

    /* --- 2. the hull leaves the grid -------------------------------------- */
    {
        Device* d = pad(w);
        rocketBeginLaunch(w, *d, 0);
        run(w, ROCKET_COUNTDOWN_FRAMES);
        int solid = 0;
        for (int y = d->y; y < d->y + ROCKET_H; ++y)
            for (int x = d->x; x < d->x + ROCKET_W; ++x)
                if (w.at(x, y).mat == MAT_DEVICE) ++solid;
        printf("cells of hull left in the grid at ignition: %d\n", solid);
        check(solid == 0, "a lit rocket is out of the simulation");
        /* And the device survives that, which is not free: devIntact deletes
           any machine whose cells have gone, and it deleting this one would
           take the ascent, the crew aboard it and the record of the launch
           with it. */
        run(w, 30);
        check(devAt(CX, GROUND - 1) != 0,
              "but the machine is still there, running its ascent");
    }

    /* --- 3. it climbs, and the crew climb with it -------------------------- */
    {
        Device* d = pad(w);
        const float groundY = g_playerSessions[0].body.centreY();
        rocketBeginLaunch(w, *d, 0);
        run(w, ROCKET_COUNTDOWN_FRAMES);
        run(w, ROCKET_ASCENT_FRAMES / 2);
        const float half = rocketAscent(*d);
        const float crewY = g_playerSessions[0].body.centreY();
        printf("half way: ascent %.2f, crew at y %.0f (pad was %.0f)\n",
               half, crewY, groundY);
        check(half > 0.2f && half < 0.9f, "half way up, it is half way up");
        check(crewY < groundY - 20.0f,
              "and the crew are riding it rather than standing on the pad");
    }

    /* --- 4. and it puts them down again ------------------------------------
       The case that matters most in this file. A crew pinned to a hull that
       has left the world would be stranded in the sky, and the win screen is
       exactly the moment somebody might close the game -- so the world is put
       right when the ascent ENDS, not when the screen is dismissed. */
    {
        Device* d = pad(w);
        rocketBeginLaunch(w, *d, 0);
        run(w, ROCKET_COUNTDOWN_FRAMES + ROCKET_ASCENT_FRAMES + 2);
        const Player& body = g_playerSessions[0].body;
        printf("after the ascent: stage %d, crew at (%.0f,%.0f), pad rows %d..%d\n",
               rocketStage(*d), body.centreX(), body.centreY(),
               d->y, d->y + ROCKET_H - 1);
        check(rocketStage(*d) == ROCKET_GONE, "the ascent finishes");
        check(body.alive, "the crew are alive");
        check(body.centreY() > (float)(GROUND - PLAYER_H - 4),
              "and standing on the ground at the pad, not in the sky");
        check(rocketVictory(), "and the world is marked won");
    }

    /* --- 5. the pad is a landmark, not a free rocket -----------------------
       Its cells are gone but devAt still answers for its whole rectangle, so
       without the guard in digInto every dig anywhere in that column of air
       would hand back a Launch Assembly. */
    {
        static Inventory inv;
        memset(&inv, 0, sizeof(inv));
        Device* d = devAt(CX, GROUND - 1);
        check(d != 0, "the pad is still registered");
        if (d) {
            digInto(w, inv, d->x + 14, d->y + 40, 1, 8, false, 255, 0);
            printf("digging the empty pad banked %d assemblies, %d cores\n",
                   held(inv, ITEM_LAUNCH_ASSEMBLY), held(inv, ITEM_ASCENT_CORE));
            check(held(inv, ITEM_LAUNCH_ASSEMBLY) == 0,
                  "digging where the rocket was does not hand one back");
            check(devAt(CX, GROUND - 1) != 0, "and the landmark stays");
        }
    }

    /* --- 6. a victory keeps ------------------------------------------------ */
    {
        const char* path = "build/rocket_ascent_test.sav";
        check(rocketVictory(), "the flag is set before saving");
        check(saveWrite(path, w), "a won world writes");
        rocketSetVictory(false);
        if (!saveRead(path, w)) {
            fprintf(stderr, "FAIL: the world did not read back: %s\n", saveError());
            ++failures;
        } else {
            check(rocketVictory(), "and reads back won");
            Device* back = devAt(CX, GROUND - 1);
            check(back && rocketStage(*back) == ROCKET_GONE,
                  "with its pad still standing, still launched");
        }
    }

    /* --- 7. and an unfinished world does not inherit one -------------------
       The flag is a global, so a session that has won something has to be able
       to load a world that has not. This is the check that says the default is
       applied on the way IN rather than trusted to be there already. */
    {
        const char* path = "build/rocket_unwon_test.sav";
        Device* d = pad(w);          /* clears the flag and stands a fresh one */
        check(!rocketVictory(), "a fresh world is unwon");
        check(d != 0 && saveWrite(path, w), "and writes");
        rocketSetVictory(true);      /* as if the session had won elsewhere */
        check(saveRead(path, w), "the unwon world reads back");
        check(!rocketVictory(),
              "and it is still unwon, rather than inheriting the session flag");
    }

    if (failures) {
        fprintf(stderr, "\n%d ascent check(s) failed\n", failures);
        return 1;
    }
    printf("\nit leaves, they come home, and the world remembers\n");
    return 0;
}
