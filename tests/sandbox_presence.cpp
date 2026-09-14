/* --- a player with the character off is not in the world --------------------

   Asked for: "i want to be able to turn player off in multiplayer too. its
   useful for setting stuff up like a creative mode sometimes".

   Online, a player with the character off still has a body -- the host parks it
   under their camera, because that is what keeps their part of the world
   streamed to them. That body must be nobody's target. The failure is quiet and
   nasty: every creature on the map walking to the spot a builder happens to be
   looking at, and chewing on a character that is not there.

   Checked with the thing a player would actually notice -- health -- and always
   against a CONTROL: the identical scene with the character on. Without the
   control, "the body took no damage" would also be the result of a creature
   that never attacked anybody, and the test would pass for the wrong reason.

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "device.h"
#include "multiplayer.h"
#include <stdio.h>

static World g_testWorld;
static int failures = 0;

static void check(bool ok, const char* what) {
    printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

/* A flat stone floor, one player standing on it, and a rock mite beside them.
   Returns the health the player is left with after `frames` of creature ticks. */
static float runScene(bool sandbox, int frames) {
    World& w = g_testWorld;
    w.reset();
    devClear();
    entReset();
    playerSessionsReset();

    const int FX = 900, FY = 700;
    for (int x = FX - 120; x < FX + 120; ++x)
        for (int y = FY; y < FY + 6; ++y) w.setCell(x, y, MAT_STONE);
    w.setLiveWindow(FX - 200, FY - 200, FX + 200, FY + 200);

    PlayerSession& me = g_playerSessions[0];
    me.body.reset((float)FX, (float)(FY - PLAYER_H / 2 - 1));
    me.sandbox = sandbox;
    const float start = me.body.hp;

    /* On the player's own row, close enough to reach them within a frame or two. */
    if (entSpawn(w, ENT_MITE, (float)FX + 6.0f, (float)FY - 6.0f) < 0) {
        fprintf(stderr, "could not spawn the mite\n");
        return -1.0f;
    }
    for (int t = 0; t < frames; ++t) {
        entTickPlayers(w);
        w.step();
    }
    return start - me.body.hp;
}

int main() {
    initMaterials();
    initItems();
    initSprites();

    const int FRAMES = 240;
    const float hurtOn  = runScene(false, FRAMES);
    const float hurtOff = runScene(true,  FRAMES);
    printf("over %d frames beside a rock mite: character on lost %.0f health, "
           "character off lost %.0f\n", FRAMES, hurtOn, hurtOff);

    check(hurtOn > 0.0f, "control: the mite does attack a player with the character on");
    check(hurtOff == 0.0f, "a player with the character off is never hurt");

    /* The rule itself, directly. */
    {
        playerSessionsReset();
        PlayerSession& me = g_playerSessions[0];
        me.body.reset(500.0f, 500.0f);
        check(playerPresent(me), "a connected, living player is present");
        me.sandbox = true;
        check(!playerPresent(me), "and is not, with the character off");
        me.sandbox = false;
        me.body.alive = false;
        check(!playerPresent(me), "nor while dead, as before");
    }

    if (failures) { fprintf(stderr, "\n%d sandbox check(s) failed\n", failures); return 1; }
    printf("\nPASS\n");
    return 0;
}
