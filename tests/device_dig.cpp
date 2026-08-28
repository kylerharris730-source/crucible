/* --- what happens when you dig a machine? -----------------------------------

   Reported from play: "you can delete the device pixels and get them in your
   inventory, weird."

   Exactly that. digInto had no idea machines existed. A device's footprint is
   written into the grid as MAT_DEVICE cells, every material drops as itself by
   default, and MAT_DEVICE is only STR_METAL hard -- so a dig took one cell of a
   spout, banked a "Device" item with no recipe, no use and no meaning, and left
   the Device struct registered over a footprint with a hole in it. That machine
   then either limped along or was silently destroyed by devIntact's five-cell
   check, losing it with nothing handed back.

   A machine is an OBJECT, not material, so the only sensible answer to "mine
   it" is to hand back the object. Four properties:

     digging a machine removes the whole machine   (not one cell of it)
     and returns the item that PLACES that machine (not a "Device")
     and never banks MAT_DEVICE                    (the bogus item is gone)
     a full pack leaves the machine standing       (never destroyed for nothing)

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "device.h"
#include "player.h"
#include "multiplayer.h"
#include <stdio.h>
#include <string.h>

static World g_testWorld;

static const int CX = 1400, CY = 5000;

static Device* setup(World& w, Inventory& inv) {
    w.reset();
    devClear();
    for (int y = CY - 40; y <= CY + 40; ++y)
        for (int x = CX - 40; x <= CX + 40; ++x)
            w.setCell(x, y, MAT_EMPTY);
    w.setLiveWindow(CX - 60, CY - 60, CX + 60, CY + 60);
    memset(&inv, 0, sizeof(inv));
    if (!devPlace(w, DEV_SPOUT, CX + DEV_W / 2, CY + DEV_H / 2)) return 0;
    return devAt(CX + DEV_W / 2, CY + DEV_H / 2);
}

static int held(const Inventory& inv, ItemId item) {
    int n = 0;
    for (int i = 0; i < INV_SLOTS; ++i)
        if (inv.slot[i].item == item) n += inv.slot[i].count;
    return n;
}

int main() {
    initMaterials();
    initItems();
    playerSessionsReset();
    World& w = g_testWorld;
    static Inventory inv;
    int failures = 0;

    const ItemId spoutItem = itemForDeviceType(DEV_SPOUT);
    printf("a spout is placed by item %d (%s)\n", (int)spoutItem,
           spoutItem == ITEM_NONE ? "NONE" : ITEMS[spoutItem].name);
    if (spoutItem == ITEM_NONE) {
        fprintf(stderr, "FAIL: no item places a spout, so nothing can hand one "
                        "back\n");
        return 1;
    }

    /* --- 1. dig one cell of it ------------------------------------------- */
    {
        Device* d = setup(w, inv);
        if (!d) { fprintf(stderr, "could not place the spout\n"); return 2; }
        /* Deliberately NOT the centre and NOT a corner: the five cells
           devIntact samples are the four corners and the middle, so a cell
           between them is exactly the one that used to leave a live machine
           with a hole in it. */
        const int hx = d->x + 3, hy = d->y + 3;
        digInto(w, inv, hx, hy, 1, 8, false, 255, 0);

        const bool gone = devAt(CX + DEV_W / 2, CY + DEV_H / 2) == 0;
        const int back = held(inv, spoutItem);
        const int bogus = held(inv, (ItemId)MAT_DEVICE);
        printf("dug a footprint cell: machine removed %s, spouts held %d, "
               "\"Device\" items held %d\n", gone ? "yes" : "NO", back, bogus);
        if (!gone) {
            fprintf(stderr, "FAIL: the machine survived with a hole in it\n");
            ++failures;
        }
        if (back != 1) {
            fprintf(stderr, "FAIL: got %d spouts back, expected 1\n", back);
            ++failures;
        }
        if (bogus != 0) {
            fprintf(stderr, "FAIL: banked %d raw MAT_DEVICE cells -- that is the "
                            "meaningless item this was reported for\n", bogus);
            ++failures;
        }
    }

    /* --- 2. no footprint left behind ------------------------------------- */
    {
        Device* d = setup(w, inv);
        const int x0 = d->x, y0 = d->y;
        digInto(w, inv, x0 + 3, y0 + 3, 1, 8, false, 255, 0);
        int leftover = 0;
        for (int y = y0; y < y0 + DEV_H; ++y)
            for (int x = x0; x < x0 + DEV_W; ++x)
                if (w.at(x, y).mat == MAT_DEVICE) ++leftover;
        printf("footprint cells still in the grid: %d\n", leftover);
        if (leftover) {
            fprintf(stderr, "FAIL: %d orphan MAT_DEVICE cells remain -- the "
                            "machine is gone but its pixels are not\n", leftover);
            ++failures;
        }
    }

    /* --- 3. a full pack leaves it standing -------------------------------- */
    {
        Device* d = setup(w, inv);
        /* Fill every slot with something that cannot merge with a spout. */
        for (int i = 0; i < INV_SLOTS; ++i) {
            inv.slot[i].item = (ItemId)MAT_STONE;
            inv.slot[i].count = (u16)ITEMS[MAT_STONE].maxStack;
        }
        digInto(w, inv, d->x + 3, d->y + 3, 1, 8, false, 255, 0);
        const bool still = devAt(CX + DEV_W / 2, CY + DEV_H / 2) != 0;
        printf("full pack: machine still there %s\n", still ? "yes" : "NO");
        if (!still) {
            fprintf(stderr, "FAIL: the machine was destroyed with nowhere to put "
                            "it -- digging must never be a way to lose one\n");
            ++failures;
        }
    }

    if (failures) {
        fprintf(stderr, "\n%d device dig check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
