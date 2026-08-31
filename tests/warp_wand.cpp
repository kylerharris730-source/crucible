#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "player.h"
#include "projectile.h"
#include "multiplayer.h"
#include <stdio.h>
#include <string.h>

/* The wand fires, costs a full battery to do it, and puts you where it landed.

   Most of this is about toolResolve's SLOTLESS branch, which is the path a tool
   with no module sockets takes. It used to answer only for a plain bolt, and it
   quietly dropped every shot field a bolt did not care about -- the effect, the
   energy cost, the lifetime. A wand going down that path would have fired a
   free, full-range shot that did nothing on arrival, which is a difficult
   failure to see: the thing shoots, it just never teleports and never runs out.

   So the Bolt Caster is checked here too. It is the other user of that branch,
   and widening the gate that lets a wand through is exactly the kind of change
   that takes the starter weapon with it.

   Compile with all source files except main.cpp. */

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

static ItemStack makeTool(ItemId item) {
    ItemStack s;
    s.item = item;
    s.count = 1;
    s.inst = toolInstNew(item);
    return s;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    g_world.reset();

    /* --- the wand resolves as a firing tool ------------------------------- */
    ItemStack wand = makeTool(ITEM_WARP_WAND);
    check(wand.inst != 0, "the wand gets a tool instance");
    const ToolShot ws = toolResolve(wand);
    check(ws.canFire, "a wand with no damage still counts as fireable");
    check(ws.effect == PROJ_EFFECT_TELEPORT, "it carries the teleport effect");
    check(ws.energyCost == ITEMS[ITEM_WARP_WAND].energyCost, "and its energy cost");
    check(ws.life == ITEMS[ITEM_WARP_WAND].shotLife, "and its short lifetime");
    check(ws.damage == 0, "it does no damage");

    /* --- the starter weapon is untouched ---------------------------------- */
    ItemStack bolter = makeTool(ITEM_BOLTER);
    const ToolShot bs = toolResolve(bolter);
    check(bs.canFire, "the Bolt Caster still fires");
    check(bs.damage == ITEMS[ITEM_BOLTER].damage, "with its damage intact");
    check(bs.effect == PROJ_EFFECT_NONE, "and no effect attached to it");
    check(bs.energyCost == 0, "and no energy cost, so it never runs dry");

    /* --- one charge, and then a wait -------------------------------------- */
    check(toolShotEnergyAvailable(wand, ws), "a fresh wand has the charge for a shot");
    toolCommitShot(wand, ws, ITEMS[ITEM_WARP_WAND].baseDelay);
    check(!toolShotEnergyAvailable(wand, ws), "and none at all for a second one");

    /* It fills at energyRecharge per frame, so the wait is a real one rather
       than a cooldown that happens to be spelled differently. */
    const int need = ITEMS[ITEM_WARP_WAND].energyCapacity /
                     imax(1, (int)ITEMS[ITEM_WARP_WAND].energyRecharge);
    for (int f = 0; f < need - 2; ++f) toolInstTick();
    check(!toolShotEnergyAvailable(wand, ws), "still empty most of the way through");
    for (int f = 0; f < 8; ++f) toolInstTick();
    check(toolShotEnergyAvailable(wand, ws), "and ready once it has refilled");

    /* --- and it actually moves you ---------------------------------------- */
    /* The effect lives on the projectile, so this fires one the way the tool
       would and checks the body arrived. A session has to be connected because
       the shot relocates its OWNER, which is a player slot rather than a
       Player* -- see teleportProjectileOwner. */
    const int PX = 900, PY = 900;
    for (int y = PY - 60; y <= PY + 60; ++y)
        for (int x = PX - 20; x <= PX + 400; ++x)
            g_world.setCell(x, y, MAT_EMPTY);
    for (int y = PY - 60; y <= PY + 60; ++y)
        for (int x = PX + 50; x <= PX + 70; ++x)
            g_world.setCell(x, y, MAT_STONE);          /* a wall to arrive at */
    /* PLAYER_H is 30, so a body centred on the shot's line reaches 15 cells
       below it. A floor any higher means the backward search for somewhere
       the whole player FITS fails at every step and nobody moves. */
    for (int y = PY + 16; y <= PY + 40; ++y)
        for (int x = PX - 20; x <= PX + 400; ++x)
            g_world.setCell(x, y, MAT_STONE);          /* a floor to stand on */
    g_world.setLiveWindow(PX - 200, PY - 200, PX + 400, PY + 200);

    playerSessionsReset();
    PlayerSession& me = g_playerSessions[0];
    me.connected = true;
    me.body.reset((float)PX, (float)PY);
    me.body.alive = true;
    const float startX = me.body.centreX();

    projClear();
    check(projSpawn((float)(PX + 4), (float)PY, ws.speed, 0.0f,
                    ws.power, ws.pierce, ws.life, ws.colour, 0, MAT_EMPTY,
                    ws.damage, false, 0.0f, ws.effect, 0, 0.0f, 0),
          "the wand's shot spawns");
    for (int f = 0; f < 240; ++f) projUpdate(g_world);

    const float movedX = me.body.centreX();
    printf("player went from x=%.0f to x=%.0f (wall at %d, wand reaches %d)\n",
           startX, movedX, PX + 50, (int)(ws.speed * (float)ws.life));
    check(movedX > startX + 20.0f, "the shot took the player most of the way to the wall");
    check(movedX < (float)(PX + 70), "and stopped short of the rock rather than inside it");

    /* Overshooting is not a misfire. A wall beyond speed*life is never reached
       and the bolt expires in mid air -- and it delivers there, because that
       spot is open by definition and a body fits in it more easily than beside
       rock. The range is a ceiling on how far one hop takes you, not a
       condition on whether the hop happens; a shot dying a cell short of the
       wall used to cost the charge and leave you standing still, which reads
       as the wand breaking rather than as a miss. */
    for (int y = PY - 60; y <= PY + 60; ++y)
        for (int x = PX + 50; x <= PX + 70; ++x)
            g_world.setCell(x, y, MAT_EMPTY);
    const int FAR = PX + 300;   /* past speed*life, whatever that currently is */
    check((float)(FAR - PX) > ws.speed * (float)ws.life,
          "the far wall really is out of range (else this proves nothing)");
    for (int y = PY - 60; y <= PY + 15; ++y)
        for (int x = FAR; x <= FAR + 20; ++x)
            g_world.setCell(x, y, MAT_STONE);
    me.body.reset((float)PX, (float)PY);
    const float farStart = me.body.centreX();
    projClear();
    projSpawn((float)(PX + 4), (float)PY, ws.speed, 0.0f,
              ws.power, ws.pierce, ws.life, ws.colour, 0, MAT_EMPTY,
              ws.damage, false, 0.0f, ws.effect, 0, 0.0f, 0);
    for (int f = 0; f < 400; ++f) projUpdate(g_world);
    const float overshot = me.body.centreX();
    const float reach = ws.speed * (float)ws.life;
    printf("overshoot: from x=%.0f to x=%.0f (reach %.0f, wall at %d)\n",
           farStart, overshot, reach, FAR);
    check(overshot > farStart + reach * 0.5f,
          "a shot that reaches nothing still carries you most of its range");
    check(overshot < (float)FAR,
          "but no further than the bolt actually got");

    /* --- dying just short of a wall still gets you there ------------------- */
    /* The case that prompted this: a bolt that expires a cell or two before the
       rock. It is the same expiry rule as above, but the landing has to cope
       with arriving right against a surface rather than in open space, which is
       where a body is hardest to fit. */
    for (int y = PY - 60; y <= PY + 15; ++y)
        for (int x = FAR; x <= FAR + 20; ++x)
            g_world.setCell(x, y, MAT_EMPTY);
    const int SHORT_WALL = PX + 4 + (int)reach + 2;   /* two cells past its last */
    for (int y = PY - 60; y <= PY + 15; ++y)
        for (int x = SHORT_WALL; x <= SHORT_WALL + 20; ++x)
            g_world.setCell(x, y, MAT_STONE);
    me.body.reset((float)PX, (float)PY);
    projClear();
    projSpawn((float)(PX + 4), (float)PY, ws.speed, 0.0f,
              ws.power, ws.pierce, ws.life, ws.colour, 0, MAT_EMPTY,
              ws.damage, false, 0.0f, ws.effect, 0, 0.0f, 0);
    for (int f = 0; f < 400; ++f) projUpdate(g_world);
    const float nearWall = me.body.centreX();
    printf("expired two cells short of rock at %d: landed at %.0f\n", SHORT_WALL, nearWall);
    check(nearWall > (float)(SHORT_WALL - 20),
          "expiring just short of a wall still puts you up against it");
    check(nearWall < (float)SHORT_WALL, "and not inside it");

    /* --- a wall puts you level with the mark, not back down the corridor --- */
    /* The case the backward-only search got wrong. A corridor too short for a
       30-cell player, a wall at the end of it, and headroom only near that
       wall: backing away from a vertical face never finds a fit at any
       distance, while sliding UP the face finds one immediately. The check is
       that the player ends up in the impact's column rather than somewhere
       behind it. */
    g_world.reset();
    for (int y = PY - 60; y <= PY + 60; ++y)
        for (int x = PX - 20; x <= PX + 200; ++x)
            g_world.setCell(x, y, MAT_EMPTY);
    for (int y = PY - 60; y <= PY - 10; ++y)          /* low ceiling over the run */
        for (int x = PX - 20; x <= PX + 45; ++x)
            g_world.setCell(x, y, MAT_STONE);
    for (int y = PY + 6; y <= PY + 40; ++y)           /* floor, close under the shot */
        for (int x = PX - 20; x <= PX + 200; ++x)
            g_world.setCell(x, y, MAT_STONE);
    for (int y = PY - 60; y <= PY + 60; ++y)          /* the wall it stops against */
        for (int x = PX + 60; x <= PX + 80; ++x)
            g_world.setCell(x, y, MAT_STONE);
    g_world.setLiveWindow(PX - 200, PY - 200, PX + 400, PY + 200);

    me.body.reset((float)(PX + 20), (float)(PY - 8));
    me.body.alive = true;
    projClear();
    projSpawn((float)(PX + 4), (float)PY, ws.speed, 0.0f,
              ws.power, ws.pierce, ws.life, ws.colour, 0, MAT_EMPTY,
              ws.damage, false, 0.0f, ws.effect, 0, 0.0f, 0);
    for (int f = 0; f < 240; ++f) projUpdate(g_world);

    const float wallX = me.body.centreX(), wallY = me.body.centreY();
    printf("wall hit at x=%d: player landed at %.0f,%.0f\n", PX + 59, wallX, wallY);
    check(wallX > (float)(PX + 50),
          "the player arrives level with the wall it shot, not back down the corridor");
    check(wallY < (float)PY, "having been lifted to where a body actually fits");

    if (failures) {
        fprintf(stderr, "%d warp wand check(s) failed\n", failures);
        return 1;
    }
    puts("the wand fires once, spends its battery, and lands you at the wall");
    return 0;
}
