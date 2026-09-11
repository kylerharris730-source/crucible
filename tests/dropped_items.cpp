/* --- what a dropped stack looks like ------------------------------------------

   Reported from play: "items are currently a pixel when dropped, they should be
   their sprites, with no background, i dont want them to all be big squares i
   want the sprite."

   They were literally that: five cells of ITEMS[].colour in a plus shape, so a
   titanium bar, a loaf of bread and a boss core were the same smudge in three
   hues. A drop now draws from the item's own art -- see dropArt.

   The risk that swap introduces is the thing this file is for. The old drawing
   could not fail: every item has a colour, so every drop was visible even when
   it was useless. Art can be MISSING, and a drop with no art is not a worse
   picture, it is an invisible item lying on the floor -- which is the one
   failure a player cannot work around, because they cannot see that there is
   anything to work around.

   Four properties:

     every droppable item has art                  (nothing is invisible)
     with enough drawn in it to be seen            (not one stray pixel)
     and some transparency around it               (a shape, not a square)
     and its stated bottom row is really its lowest (it stands on the ground)

   Compile with every src cpp file except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "multiplayer.h"
#include <stdio.h>

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();

    int droppable = 0, missing = 0, faint = 0, solid = 0, wrongBottom = 0;
    int worstFaint = ITEM_NONE, worstSolid = ITEM_NONE;
    for (int i = ITEM_NONE + 1; i < ITEM_COUNT; ++i) {
        /* What can actually end up on the floor: anything that stacks. An item
           with no stack size is not a thing you can hold, drop or throw. */
        if (!ITEMS[i].maxStack) continue;
        ++droppable;
        const u32* art = dropArt((u16)i);
        if (!art) {
            printf("  %s has no art at all\n", ITEMS[i].name);
            ++missing;
            continue;
        }
        int drawn = 0;
        for (int k = 0; k < SPR_W * SPR_H; ++k) drawn += art[k] != 0;
        /* Twelve pixels of a 196-cell canvas. Low on purpose: some of this art
           is a single small object and should be allowed to be small. What it
           catches is art that is effectively blank. */
        if (drawn < 12) {
            if (worstFaint == ITEM_NONE) worstFaint = i;
            printf("  %s draws only %d pixels\n", ITEMS[i].name, drawn);
            ++faint;
        }
        /* And it is a SHAPE. A canvas with no transparent pixel anywhere is a
           filled square, which is the thing the report asked not to see --
           every drop the same rectangle in a different colour. */
        if (drawn == SPR_W * SPR_H) {
            if (worstSolid == ITEM_NONE) worstSolid = i;
            printf("  %s is a solid square\n", ITEMS[i].name);
            ++solid;
        }
        /* The bottom row it claims has to be the lowest row with anything on
           it, because that is what stands the drop on the ground rather than
           burying half of it. */
        const int bottom = dropArtBottom((u16)i);
        if (bottom < 0 || bottom >= SPR_H) { ++wrongBottom; continue; }
        bool onIt = false, below = false;
        for (int x = 0; x < SPR_W; ++x) {
            if (art[bottom * SPR_W + x]) onIt = true;
            for (int y = bottom + 1; y < SPR_H; ++y)
                if (art[y * SPR_W + x]) below = true;
        }
        if (!onIt || below) {
            printf("  %s reports bottom row %d, which is wrong\n",
                   ITEMS[i].name, bottom);
            ++wrongBottom;
        }
    }

    printf("%d droppable items: %d with no art, %d nearly blank, %d solid "
           "squares, %d with a wrong bottom row\n",
           droppable, missing, faint, solid, wrongBottom);
    check(droppable > 200, "there are items to check");
    check(missing == 0, "every droppable item has art to draw on the ground");
    check(faint == 0, "and enough of it to see");
    check(solid == 0, "and none of them is a filled square");
    check(wrongBottom == 0, "and each one knows which row it stands on");

    /* And the art is the SAME art the inventory uses, for anything with a
       sprite of its own. Two sources for one object is how a dropped sword
       ends up looking like something you never picked up. */
    {
        int mismatched = 0;
        for (int i = ITEM_NONE + 1; i < ITEM_COUNT; ++i) {
            if (!ITEMS[i].maxStack) continue;
            const int spr = ITEMS[i].sprite;
            if (spr <= SPR_NONE || spr >= SPR_COUNT) continue;
            if (dropArt((u16)i) != g_sprite[spr]) ++mismatched;
        }
        check(mismatched == 0, "a drop is drawn from the icon's own canvas");
    }

    if (failures) {
        fprintf(stderr, "\n%d dropped-item check(s) failed\n", failures);
        return 1;
    }
    printf("\nwhat is on the floor looks like what it is\n");
    return 0;
}
