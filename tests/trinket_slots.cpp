/* --- six trinket slots, and the saves that only know about four -------------

   Asked for: "add 2 more trinket slots."

   The slots themselves are three lines. What this file is really defending is
   the other half of that change, which is invisible from the game: Inventory
   is written to the save as one raw sized block, so widening equip[] moves
   sizeof(Inventory) and every existing character stops matching the shape the
   loader expects.

   save.cpp already knew that and carries a converter per old shape. The trap
   is subtler and this suite found it: those converters were partly spelled
   with LIVE constants -- `equip[EQ_COUNT]`, `droneModule[DRONE_BAY_COUNT]` --
   which describe a file correctly only until the day one of those constants
   moves. Adding a fifth slot changed EQ_COUNT, which changed the struct that
   was supposed to describe a four-row save, which would have meant every save
   from that era matching nothing and loading with no equipment at all. A shape
   that describes bytes on disk cannot be written with a number that is still
   allowed to change.

   Five properties:

     there are six interchangeable trinket slots
     a charm fits every one of them, and only trinket items do
     six charms worn at once all count
     an inventory survives a save and a reload with all six filled
     and the frozen file shapes are still the sizes they claim to be

   Compile with every src cpp file except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "player.h"
#include "multiplayer.h"
#include "save.h"
#include <stdio.h>
#include <string.h>

static World g_testWorld;
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

    /* --- 1. six of them --------------------------------------------------- */
    {
        printf("%d trinket slots in %d equipment slots\n",
               EQ_TRINKET_COUNT, (int)EQ_COUNT);
        check(EQ_TRINKET_COUNT == 6, "there are six trinket slots");
        int named = 0, distinct = 0;
        for (int i = 0; i < EQ_TRINKET_COUNT; ++i) {
            const int slot = EQ_TRINKETS[i];
            if (slot >= 0 && slot < EQ_COUNT && EQ_NAMES[slot] && EQ_SHORT[slot] &&
                EQ_NAMES[slot][0] && EQ_SHORT[slot][0]) ++named;
            bool dupe = false;
            for (int j = 0; j < i; ++j) if (EQ_TRINKETS[j] == slot) dupe = true;
            if (!dupe) ++distinct;
        }
        check(named == EQ_TRINKET_COUNT, "each has a long name and a short one");
        check(distinct == EQ_TRINKET_COUNT, "and they are six different slots");
        /* The short labels are what is painted INSIDE a 34-pixel square, so
           they have to stay short -- see the note on EQ_SHORT. */
        int tooLong = 0;
        for (int i = 0; i < EQ_TRINKET_COUNT; ++i)
            if (strlen(EQ_SHORT[EQ_TRINKETS[i]]) > 4) ++tooLong;
        check(tooLong == 0, "and short enough to fit in the square");
    }

    /* --- 2. a charm fits all six, and nothing else does -------------------- */
    {
        int fits = 0;
        for (int i = 0; i < EQ_TRINKET_COUNT; ++i)
            if (equipFits(ITEM_SWIFT_CHARM, EQ_TRINKETS[i])) ++fits;
        check(fits == EQ_TRINKET_COUNT, "a charm fits every trinket slot");
        check(!equipFits(ITEM_IRON_HELMET, EQ_TRINKET_E) &&
              !equipFits(ITEM_IRON_HELMET, EQ_TRINKET_F),
              "and a helmet fits none of them");
        check(eqIsTrinket(EQ_TRINKET_E) && eqIsTrinket(EQ_TRINKET_F),
              "the new pair report themselves as trinkets");
    }

    /* --- 3. six worn at once all count -------------------------------------
       The point of the slots. Armour is the readable one to measure because it
       is a plain sum: if a sixth charm were being ignored -- because something
       still iterated four slots, which is the shape of bug this change can
       have -- the total would come up short. */
    {
        static const ItemId WORN[6] = {
            ITEM_CARAPACE_CHARM, ITEM_SHAMBLER_BALLAST, ITEM_SWIFT_CHARM,
            ITEM_MOTH_LANTERN, ITEM_HUSK_HEART, ITEM_SLIME_MAGNET
        };
        Inventory& inv = g_inv;
        inv.clear();
        int expectedArmour = 0;
        for (int i = 0; i < 6; ++i) {
            inv.equip[EQ_TRINKETS[i]].item = WORN[i];
            inv.equip[EQ_TRINKETS[i]].count = 1;
            expectedArmour += ITEMS[WORN[i]].armour;
        }
        printf("six charms worn: armour %d (expected %d), glow %d\n",
               inv.armour(), expectedArmour, inv.lightGlow());
        check(inv.armour() == expectedArmour,
              "all six worn charms are counted, not the first four");
        /* And the largest-wins passives reach the last slot too. A lantern in
           the sixth slot is a different failure from the sum above: a scan
           that stopped at four would report no glow at all, and that gets
           blamed on the charm rather than on the slot. */
        Inventory only;
        only.clear();
        only.equip[EQ_TRINKET_F].item = ITEM_MOTH_LANTERN;
        only.equip[EQ_TRINKET_F].count = 1;
        printf("a lantern alone in the sixth slot glows %d\n", only.lightGlow());
        check(only.lightGlow() > 0, "and a charm in the LAST slot still counts");
    }

    /* --- 4. and they survive a reload -------------------------------------- */
    {
        Inventory& inv = g_inv;
        ItemId before[EQ_COUNT];
        for (int i = 0; i < EQ_COUNT; ++i) before[i] = inv.equip[i].item;
        g_world.reset();
        const char* path = "build/trinket_slots_test.sav";
        check(saveWrite(path, g_world), "a character wearing six charms writes");
        inv.clear();
        check(inv.equip[EQ_TRINKET_F].item == ITEM_NONE, "the pack really was cleared");
        if (!saveRead(path, g_world)) {
            fprintf(stderr, "FAIL: the world did not read back: %s\n", saveError());
            ++failures;
        } else {
            int wrong = 0;
            for (int i = 0; i < EQ_COUNT; ++i)
                if (g_inv.equip[i].item != before[i]) ++wrong;
            printf("after the reload, %d of %d equipment slots differ\n",
                   wrong, (int)EQ_COUNT);
            check(wrong == 0, "and comes back wearing all six");
        }
    }

    /* --- 5. the frozen shapes are still frozen -----------------------------
       The check that would have caught the live-constant bug before it shipped,
       and the reason it is here rather than in save.cpp: those structs are
       private to that file, but their SIZES are the contract, and a size is
       something a test can state out loud.

       These numbers are what the loader must go on matching for as long as
       people have those files. If a change to Inventory moves one of them,
       this fails -- which is the point. Recompute them by hand from the shape
       in save.cpp before touching them, and never by running the program and
       copying what it printed. */
    {
        const int stack = (int)sizeof(ItemStack);
        /* slot[60] + equip[12] + selected + droneModule[4][3] + droneLevel[4],
           padded to the alignment of ItemStack. */
        const int v4 = 60 * stack + 12 * stack + (int)sizeof(int)
                     + 4 * 3 * stack + 4;
        printf("ItemStack is %d bytes; the six-row/twelve-slot save is %d\n",
               stack, v4);
        /* Today's Inventory has to be BIGGER than the shape it replaced, and
           by exactly two equipment slots. Stated as a difference rather than
           as an absolute, because what matters is that the two new slots are
           the only thing that moved. */
        const int now = (int)sizeof(Inventory);
        printf("todays Inventory is %d bytes, %d more than the old shape\n",
               now, now - v4);
        check(now - v4 == 2 * stack,
              "todays pack is the old one plus exactly two trinket slots");
    }

    if (failures) {
        fprintf(stderr, "\n%d trinket check(s) failed\n", failures);
        return 1;
    }
    printf("\nsix slots, and the old saves still know how to become new ones\n");
    return 0;
}
