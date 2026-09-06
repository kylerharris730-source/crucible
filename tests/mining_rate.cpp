/* --- how fast does digging actually go? --------------------------------------

   Asked for: "double default mining speed and size, mining is just overall too
   slow now."

   Two numbers move and a third has to be checked because of them. Bare hands
   are a ToolSpec like everything else (see HAND), so "default mining" is one
   row: radius, cells per action, and the frames between actions. Doubling the
   first two is the request.

   The third thing is the LADDER. Every mining tool is priced against bare
   hands -- the table in item.cpp states each tier's multiple of them -- and the
   first rung was 2x at a smaller radius than the doubled baseline would have.
   Doubling the default without moving that rung does not make mining faster,
   it deletes the Hand Drill: same throughput, less reach, strictly worse than
   having nothing. So the tiers move with the baseline and this asserts the
   ladder is still a ladder afterwards.

   Five properties:

     the default digs twice as much a second     (the request)
     over twice the area                         (the request)
     every tier still beats bare hands           (or it is dead content)
     and every tier beats the one below it       (in BOTH speed and reach)
     the top of the ladder still fits its disc   (DISC_MAX_R is a real limit)

   Rates are MEASURED by digging real stone, not read off the table. The table
   says what was intended; a rate test that reads the same fields it is checking
   would pass on a build where digInto ignored them entirely.

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "player.h"
#include "multiplayer.h"
#include <stdio.h>

static World g_testWorld;
static const int CX = 1400, CY = 5000;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  %-56s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

/* Cells removed in one second of holding the button, measured against solid
   stone with a pack big enough to hold all of it.

   The pack matters and is easy to miss: digInto banks what it removes, and a
   full inventory makes it stop. A rate test that quietly filled up would report
   the size of a backpack as the speed of a drill. */
static int cellsPerSecond(const ToolSpec& t, int brush) {
    World& w = g_testWorld;
    w.reset();
    for (int y = CY - 200; y <= CY + 200; ++y)
        for (int x = CX - 200; x <= CX + 200; ++x)
            if (x > PLAY_X0 && x < PLAY_X1 && y > PLAY_Y0 && y < PLAY_Y1)
                w.setCell(x, y, MAT_STONE);
    w.setLiveWindow(CX - 220, CY - 220, CX + 220, CY + 220);

    static Inventory inv;
    inv.clear();
    const int r = brush < t.maxRadius ? brush : t.maxRadius;

    int dug = 0, cooldown = 0;
    for (int f = 0; f < 60; ++f) {
        if (cooldown > 0) { --cooldown; continue; }
        dug += digInto(w, inv, CX, CY, r, t.cellsPerBite, t.plantsOnly, t.power, 0);
        cooldown = t.cooldown;
        /* Emptied every action. See the note above -- this measures the tool,
           and a pack is a different subject. */
        inv.clear();
    }
    return dug;
}

/* The ToolSpec a tier resolves to, so the ladder is read through the same
   function the game reads it through rather than from the item table twice. */
static ToolSpec specFor(ItemId tool) {
    static Inventory inv;
    inv.clear();
    if (tool != ITEM_NONE) inv.add(tool, 1);
    return miningSpec(inv);
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();

    /* The brush a player starts with, from main.cpp. Copied rather than
       exported because main.cpp is not linked here; if it moves, this is where
       the mismatch shows up. */
    static const int DEFAULT_BRUSH = 12;

    /* --- 1 & 2. the default, doubled -------------------------------------- */
    {
        printf("HAND: radius %d, %d cells per %d frames\n",
               HAND.maxRadius, HAND.cellsPerBite, HAND.cooldown);
        const int rate = cellsPerSecond(HAND, DEFAULT_BRUSH);
        printf("bare hands at the default brush: %d cells/second\n", rate);
        /* 216, which is twice the 108 THIS HARNESS measured before the change
           -- not twice the 120 item.h quotes. The two differ for a reason worth
           stating rather than splitting: 24 cells every 6 frames is 240 a
           second as a rate, but a sixty-frame window starting on a dig gets
           nine actions and not ten, so a measured second always reads a bite
           light. The rate in the table is right; a test that asserted it would
           be measuring the arithmetic of its own loop. */
        check(rate >= 216, "bare hands shift twice what they used to");
        check(HAND.maxRadius >= 14, "and reach at least twice as far");
        /* The default brush must be able to USE the new reach, or doubling the
           cap is a number nobody sees: digRadius() clamps the brush to the
           tool, so a default of 6 against a cap of 14 would leave the size
           change invisible until the player scrolled the wheel. */
        check(DEFAULT_BRUSH <= HAND.maxRadius,
              "and the default brush is not clamped below that reach");
    }

    /* --- 3, 4 & 5. the ladder is still a ladder --------------------------- */
    {
        struct Rung { const char* name; ItemId id; };
        const Rung rungs[] = {
            { "Hands",         ITEM_NONE      },
            { "Hand Drill",    ITEM_DRILL     },
            { "Rock Auger",    ITEM_AUGER     },
            { "Thermal Lance", ITEM_LANCE     },
            { "Disruptor",     ITEM_DISRUPTOR },
        };
        const int N = 5;
        int  rate[5];
        int  reach[5];
        for (int k = 0; k < N; ++k) {
            const ToolSpec t = specFor(rungs[k].id);
            /* At each tool's OWN full radius, which is what the tier is for. */
            reach[k] = t.maxRadius;
            rate[k]  = cellsPerSecond(t, t.maxRadius);
            printf("  %-14s radius %2d, %6d cells/second  (%.1fx hands)\n",
                   rungs[k].name, reach[k], rate[k],
                   rate[0] ? (double)rate[k] / (double)rate[0] : 0.0);
        }

        bool faster = true, wider = true;
        for (int k = 1; k < N; ++k) {
            if (rate[k]  <= rate[k - 1])  faster = false;
            if (reach[k] <= reach[k - 1]) wider  = false;
        }
        check(rate[1] > rate[0] && reach[1] > reach[0],
              "the first rung still beats bare hands");
        check(faster, "every tier digs faster than the one below it");
        check(wider,  "and reaches further than the one below it");

        /* The disc table is built once to DISC_MAX_R; a tool whose radius
           exceeded it would index past the end of g_discEnd. */
        bool fits = true;
        for (int k = 0; k < N; ++k) if (reach[k] > DISC_MAX_R) fits = false;
        check(fits, "and no tool reaches past the disc table it indexes");
    }

    if (failures) {
        fprintf(stderr, "\n%d mining check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
