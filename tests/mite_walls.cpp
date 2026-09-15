/* --- rock mites leave your walls alone -----------------------------------------

   Asked for: "those beetles chewing through walls is too annoying, remove it".

   The rock mite used to eat any rock-strength cell it was pressed against, one
   bite every fourteen frames, and was deliberately NOT routed so that it would
   keep pressing. Taking the chewing away from a creature built that way leaves
   one that walks into a wall and stops, so the other half of the change is that
   it now routes and hops like every other walker.

   Two properties, and the second is the control that stops the first passing
   for the wrong reason -- a mite that simply never moved would also leave the
   wall intact:

     a mite walled off from the player does not dig    (the ask)
     a mite with a way round -- a step up -- gets to the player

   Compile with every src/*.cpp except main.cpp. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "device.h"
#include "player.h"
#include "multiplayer.h"
#include <stdio.h>

static World g_testWorld;
static int failures = 0;

static void check(bool ok, const char* what) {
    printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static void rect(World& w, int x0, int y0, int x1, int y1, u8 m) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) w.setCell(x, y, m);
}

static const int FX = 900, FY = 700;

/* A stone floor, the player standing at the right, a mite at the left. */
static int setup(World& w) {
    w.reset();
    devClear();
    entReset();
    playerSessionsReset();
    rect(w, FX - 200, FY, FX + 200, FY + 5, MAT_STONE);
    w.setLiveWindow(FX - 300, FY - 300, FX + 300, FY + 300);
    PlayerSession& me = g_playerSessions[0];
    me.body.reset((float)(FX + 120), (float)(FY - PLAYER_H));
    return entSpawn(w, ENT_MITE, (float)(FX - 120), (float)FY - 6.0f);
}

static int stoneIn(const World& w, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) n += w.at(x, y).mat == MAT_STONE;
    return n;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    World& w = g_testWorld;

    /* --- walled off ------------------------------------------------------- */
    {
        const int slot = setup(w);
        if (slot < 0) { fprintf(stderr, "could not spawn the mite\n"); return 2; }
        /* A wall floor to roof between them, well over anything a mite can hop. */
        rect(w, FX - 4, FY - 120, FX + 4, FY - 1, MAT_STONE);
        const int before = stoneIn(w, FX - 4, FY - 120, FX + 4, FY - 1);
        float nearest = 1e9f;
        for (int t = 0; t < 3600; ++t) {
            entTickPlayers(w);
            w.step();
            const Entity& e = g_entities[slot];
            if (e.alive()) {
                const float d = (float)(FX - 4) - (float)e.right(); /* can only shrink toward 0 if it stays this side */
                if (d < nearest) nearest = d;
            }
        }
        const int after = stoneIn(w, FX - 4, FY - 120, FX + 4, FY - 1);
        printf("  wall: %d stone cells before, %d after; mite came within %.0f cells of it\n",
               before, after, nearest);
        check(nearest <= 3.0f, "control: the mite did walk up to the wall");
        check(after == before, "and did not take a single cell out of it");
    }

    /* --- a step it can get over ------------------------------------------- */
    {
        const int slot = setup(w);
        if (slot < 0) return 2;
        /* A raised block across the floor, twelve cells high: too tall to walk
           over, low enough to hop. */
        rect(w, FX - 30, FY - 12, FX + 30, FY - 1, MAT_STONE);
        const int before = stoneIn(w, FX - 30, FY - 12, FX + 30, FY - 1);
        bool reached = false;
        for (int t = 0; t < 3600 && !reached; ++t) {
            entTickPlayers(w);
            w.step();
            const Entity& e = g_entities[slot];
            if (e.alive() && e.centreX() > (float)(FX + 60)) reached = true;
        }
        const int after = stoneIn(w, FX - 30, FY - 12, FX + 30, FY - 1);
        printf("  step: mite %s the far side; step %d -> %d stone cells\n",
               reached ? "reached" : "never reached", before, after);
        check(reached, "a mite gets over a step to reach the player");
        check(after == before, "by climbing it, not eating it");
    }

    if (failures) { fprintf(stderr, "\n%d mite wall check(s) failed\n", failures); return 1; }
    printf("PASS\n");
    return 0;
}
