/* --- does everything you can hold have a picture? ----------------------------

   Reported from play: "a lot of the new items don't have proper sprites,
   they've got the placeholder gray rectangle."

   True, and the mechanism is a quiet one. initItems() ends with a line that
   gives any item still lacking art the generic placeholder, so a new item
   never crashes and never renders as nothing -- it renders as a grey box that
   looks deliberate. Twenty items had accumulated behind that fallback,
   including all four drones, which are the WEAPONS.

   A fallback that hides its own use is the thing to test. So this asserts the
   placeholder is unused rather than that any particular icon exists: add an
   item tomorrow and forget its sprite, and this fails by name.

   Three checks, and the second and third are what stop the first from being
   satisfied cheaply:

     no item may resolve to SPR_ITEM_GENERIC
     every sprite an item points at must have actual pixels in it
     the egg shells must be the colour of the creature inside them

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include <stdio.h>

static int pixelsIn(int s) {
    int n = 0;
    for (int k = 0; k < SPR_W * SPR_H; ++k) if (g_sprite[s][k]) ++n;
    return n;
}

int main() {
    if (INV_SPR_W * 2 != SPR_W * 3 || INV_SPR_H * 2 != SPR_H * 3) {
        fprintf(stderr, "inventory sprite canvas is not an exact 3:2 enlargement\n");
        return 10;
    }
    initMaterials();
    initItems();
    initSprites();

    int failures = 0, checked = 0, placeholder = 0, empty = 0;

    for (int i = 1; i < ITEM_COUNT; ++i) {
        const ItemDef& d = ITEMS[i];
        if (!d.name || !d.name[0]) continue;
        /* Materials draw from their own rendered block, not from a sprite --
           see drawItemIcon, which checks `item < MAT_COUNT` first. They are
           not placeholders and must not be counted as such. */
        if (i < MAT_COUNT) continue;
        ++checked;

        if (d.sprite == SPR_ITEM_GENERIC) {
            fprintf(stderr, "PLACEHOLDER: \"%s\" (item %d) is still on the "
                            "generic grey box\n", d.name, i);
            ++placeholder; ++failures;
            continue;
        }
        if (d.sprite <= SPR_NONE || d.sprite >= SPR_COUNT) {
            fprintf(stderr, "BAD SPRITE: \"%s\" (item %d) points at sprite %d\n",
                    d.name, i, d.sprite);
            ++failures;
            continue;
        }
        if (pixelsIn(d.sprite) == 0) {
            /* An id that exists in the enum but was never baked. This is the
               failure that a "does it have an id" test would miss entirely:
               the item looks configured and draws nothing at all. */
            fprintf(stderr, "EMPTY ART: \"%s\" (item %d) points at sprite %d, "
                            "which has no pixels -- was it added to the enum "
                            "and not to initSprites?\n", d.name, i, d.sprite);
            ++empty; ++failures;
        }
    }

    printf("%d holdable items checked; %d on the placeholder, %d with empty art\n",
           checked, placeholder, empty);

    /* The eggs are tinted from ENT_DEFS at bake time and their swatch is read
       from the same field, so the two cannot drift -- but only while both keep
       reading it. This checks the shell actually carries the creature's colour
       rather than trusting that it does. */
    int eggsChecked = 0;
    for (int t = ENT_NONE + 1; t < ENT_COUNT; ++t) {
        const int s = SPR_EGG_FIRST + (t - 1);
        if (s > SPR_EGG_LAST) {
            fprintf(stderr, "EGG SLOTS: creature %d (%s) has no sprite slot\n",
                    t, ENT_DEFS[t].name);
            ++failures;
            continue;
        }
        const u32 want = ENT_DEFS[t].eggColour;
        bool found = false;
        for (int k = 0; k < SPR_W * SPR_H && !found; ++k)
            if (g_sprite[s][k] == want) found = true;
        ++eggsChecked;
        if (!found) {
            fprintf(stderr, "EGG COLOUR: the %s shell has no pixel of that "
                            "creature's own colour %06X\n",
                    ENT_DEFS[t].name, (unsigned)want);
            ++failures;
        }
    }
    printf("%d egg shells carry their creature's colour\n", eggsChecked - 0);

    if (failures) {
        fprintf(stderr, "\n%d sprite coverage check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
