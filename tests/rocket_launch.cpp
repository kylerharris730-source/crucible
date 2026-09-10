/* --- the countdown, and everything that calls it off -------------------------

   Stage two of ENDGAME.md: readiness, the countdown, and the rechecks. The
   rocket exists and can be loaded (tests/rocket.cpp); this is about the five
   seconds between deciding to go and going.

   Nearly all of this file is the CANCEL cases, and that is the right shape for
   it. Starting a countdown is one rule read once; a countdown is a window in
   which the world keeps moving underneath a decision that has already been
   made, and every one of the ways it can go wrong -- somebody builds a roof,
   somebody mines the pad, somebody dies, somebody wanders off -- is a way to
   launch a rocket that should not have launched. ENDGAME.md asks for all of
   them by name, so each one is a case here.

   What is NOT tested here is the button: the panel lives in main.cpp, which no
   harness links. That is why the rules are in device.cpp and the panel only
   sends actions -- everything a player can decide is reachable from here.

   Eight properties:

     a fuelled, crewed, ready rocket starts counting
     an unready one does not, and says which requirement stopped it
     a guest cannot start one, and anybody can call one off
     the countdown reaches ignition on its own
     a roof over the corridor cancels it
     digging the pad out cancels it
     a crewmate dying or walking away cancels it
     and none of it costs the cargo

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

/* A pad, a rocket on it, loaded, with the host standing at its feet. The state
   every case here starts from, because what is being measured is what takes it
   AWAY. */
static Device* pad(World& w, bool crewed = true) {
    w.reset();
    devClear();
    playerSessionsReset();
    for (int y = GROUND - 240; y <= GROUND + 20; ++y)
        for (int x = CX - 240; x <= CX + 240; ++x)
            w.setCell(x, y, y >= GROUND ? MAT_STONE : MAT_EMPTY);
    w.setLiveWindow(CX - 260, GROUND - 260, CX + 260, GROUND + 40);
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
    if (crewed) rocketToggleReady(*d, 0);
    return d;
}

/* Frames of countdown, run through the real devTick. */
static void run(World& w, int frames) {
    for (int f = 0; f < frames; ++f) devTick(w);
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    World& w = g_testWorld;

    /* --- 1. what a ready pad looks like ----------------------------------- */
    {
        Device* d = pad(w);
        if (!d) { fprintf(stderr, "could not stand a rocket up\n"); return 2; }
        printf("fault on a loaded, crewed, ready pad: %s\n",
               rocketFaultText(rocketFault(w, *d)));
        check(rocketFault(w, *d) == ROCKET_READY, "a loaded crewed pad reports ready");
        check(rocketBeginLaunch(w, *d, 0), "and the host can start the countdown");
        check(rocketStage(*d) == ROCKET_COUNTING, "which puts it in the counting stage");
        check(rocketCountdown(*d) == ROCKET_COUNTDOWN_FRAMES,
              "with the whole five seconds on the clock");
    }

    /* --- 2. and what stops it, one requirement at a time ------------------ */
    {
        Device* d = pad(w);
        rocketSetCore(*d, false);
        check(rocketFault(w, *d) == ROCKET_FAULT_NO_CORE, "no core, no launch");
        check(!rocketBeginLaunch(w, *d, 0), "and the countdown refuses to start");
        rocketSetCore(*d, true);

        d->count = ROCKET_FUEL_NEED - 1;
        check(rocketFault(w, *d) == ROCKET_FAULT_NO_FUEL,
              "one unit short of a full load is not fuelled");
        d->count = ROCKET_FUEL_NEED;

        /* Nobody aboard and nobody ready are different faults, and the panel
           shows the first one -- being told "crew not ready" while standing
           alone at the pad would be advice you cannot act on. */
        g_playerSessions[0].body.x = (float)(CX + 400);
        check(rocketFault(w, *d) == ROCKET_FAULT_NO_CREW,
              "a rocket with nobody near it has no crew");
        g_playerSessions[0].body.x = (float)CX;
        rocketToggleReady(*d, 0);
        check(rocketFault(w, *d) == ROCKET_FAULT_NOT_READY,
              "and somebody standing there who has not said go holds it");
        rocketToggleReady(*d, 0);
        check(rocketFault(w, *d) == ROCKET_READY, "saying go clears it");
    }

    /* --- 3. whose decision it is ------------------------------------------
       ENDGAME.md: the host confirms the launch, and anyone can cancel. The
       asymmetry is deliberate and it is the only part of this that is not a
       group decision -- a guest should not be able to end somebody else's
       world on a five-second timer, and a guest who wants no part of a launch
       must be able to stop one. */
    {
        Device* d = pad(w);
        PlayerSession& guest = g_playerSessions[1];
        guest.connected = true;
        guest.body.reset((float)(CX + 20), (float)(GROUND - PLAYER_H / 2));
        guest.body.alive = true;
        guest.body.hp = PLAYER_HP_MAX;
        rocketToggleReady(*d, 1);
        check(rocketFault(w, *d) == ROCKET_READY, "two aboard, both ready");
        check(!rocketBeginLaunch(w, *d, 1), "a guest cannot start the countdown");
        check(rocketStage(*d) == ROCKET_IDLE, "so nothing is counting");
        check(rocketBeginLaunch(w, *d, 0), "the host can");
        rocketCancel(*d);
        check(rocketStage(*d) == ROCKET_IDLE, "and a cancel puts it back to idle");
    }

    /* --- 4. it gets there on its own -------------------------------------- */
    {
        Device* d = pad(w);
        rocketBeginLaunch(w, *d, 0);
        run(w, 60);
        printf("after one second: stage %d, %.1f s left\n",
               rocketStage(*d), rocketCountdown(*d) / 60.0f);
        check(rocketStage(*d) == ROCKET_COUNTING, "a second in, it is still counting");
        check(rocketCountdown(*d) < ROCKET_COUNTDOWN_FRAMES,
              "and the clock has actually moved");
        run(w, ROCKET_COUNTDOWN_FRAMES);
        check(rocketStage(*d) == ROCKET_LIT, "and it reaches ignition on its own");
        /* Which is where this file stops caring. What ignition COSTS, what the
           ascent does and what it leaves behind are tests/rocket_ascent.cpp;
           all that matters here is that the countdown handed over. */
        check(rocketAscent(*d) >= 0.0f, "and hands over to the ascent");
    }

    /* --- 5. a roof over the corridor -------------------------------------- */
    {
        Device* d = pad(w);
        rocketBeginLaunch(w, *d, 0);
        run(w, 60);
        for (int x = d->x; x < d->x + ROCKET_W; ++x)
            w.setCell(x, d->y - 40, MAT_STONE);
        run(w, 1);
        check(rocketStage(*d) == ROCKET_IDLE, "a roof built overhead cancels it");
        check(rocketCore(*d) && rocketFuel(*d) == ROCKET_FUEL_NEED,
              "and the cancel costs nothing");
    }

    /* --- 6. the pad dug out from under it ---------------------------------
       The case ENDGAME.md calls "destroyed support". devIntact covers digging
       into the MACHINE; this is the ground it is standing on, which nothing
       else was watching. */
    {
        Device* d = pad(w);
        rocketBeginLaunch(w, *d, 0);
        run(w, 60);
        for (int y = GROUND; y < GROUND + 6; ++y)
            for (int x = d->x; x < d->x + ROCKET_W; ++x)
                w.setCell(x, y, MAT_EMPTY);
        run(w, 1);
        check(rocketStage(*d) == ROCKET_IDLE, "mining the pad away cancels it");
    }

    /* --- 7. the crew ------------------------------------------------------ */
    {
        Device* d = pad(w);
        rocketBeginLaunch(w, *d, 0);
        run(w, 60);
        g_playerSessions[0].body.alive = false;
        run(w, 1);
        check(rocketStage(*d) == ROCKET_IDLE, "the crew dying cancels it");

        d = pad(w);
        rocketBeginLaunch(w, *d, 0);
        run(w, 60);
        g_playerSessions[0].body.x = (float)(CX + 400);
        run(w, 1);
        check(rocketStage(*d) == ROCKET_IDLE, "and so does walking away from it");
        /* The bit is still set, and that is fine -- rocketReady asks whether
           somebody is crew as well as whether they said yes, so a stale bit
           cannot decide anything on its own. Walking back and being counted
           again is the behaviour you want: leaving to fetch fuel should not
           mean tapping Ready a second time. */
        g_playerSessions[0].body.x = (float)CX;
        check(rocketFault(w, *d) == ROCKET_READY,
              "and coming back makes them crew again without re-arming");
    }

    /* --- 8. it survives a reload ------------------------------------------
       ENDGAME.md: "Persistent completion and launch stages need explicit
       save-version handling." They needed none, and this is the check that
       says why -- the whole launch state lives in Device fields that were
       already written to the save and already replicated, so quitting mid
       countdown and coming back finds the pad exactly as it was. */
    {
        Device* d = pad(w);
        rocketBeginLaunch(w, *d, 0);
        run(w, 90);
        const int left = rocketCountdown(*d);
        const char* path = "build/rocket_launch_test.sav";
        check(saveWrite(path, w), "a world mid-countdown writes");
        devClear();
        check(devAt(CX, GROUND - 1) == 0, "the pad really was cleared");
        if (!saveRead(path, w)) {
            fprintf(stderr, "FAIL: the world did not read back: %s\n", saveError());
            ++failures;
        } else {
            Device* back = devAt(CX, GROUND - 1);
            check(back != 0, "and the rocket is standing there again");
            if (back) {
                printf("reloaded: stage %d, %d frames left (was %d)\n",
                       rocketStage(*back), rocketCountdown(*back), left);
                check(rocketStage(*back) == ROCKET_COUNTING,
                      "still counting, because the stage is saved with the machine");
                check(rocketCountdown(*back) == left, "from exactly where it was");
                check(rocketCore(*back) && rocketFuel(*back) == ROCKET_FUEL_NEED,
                      "with its cargo");
            }
        }
    }

    if (failures) {
        fprintf(stderr, "\n%d launch check(s) failed\n", failures);
        return 1;
    }
    printf("\nthe countdown runs, and everything that should stop it does\n");
    return 0;
}
