#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "render.h"
#include <stdio.h>
#include <string.h>

/* Every creature must change silhouette across its restrained procedural
   cycle. This catches the easy regression where a new species is added to the
   table but not to entityPixelMotion and goes back to being a decal dragged
   through the world. Compile with all source files except main.cpp.

   THERE ARE TWO CLOCKS and the sample has to advance both. Wings and breathing
   run on g_world.frame, because a flier beats its wings while hovering and
   lungs work while a creature stands still. Legs run on Entity::walkPhase,
   which accumulates GROUND COVERED -- see gaitStep.

   This test used to move only the frame counter, which was correct when
   everything was timer-driven and became a false negative the moment legs
   started measuring distance: every walker reported "changed only 0 pixels"
   while animating perfectly well in the game, because the sample never moved
   it anywhere. The assertion below is unchanged; only the way the cycle is
   advanced is. */
int main() {
    initMaterials(); initItems(); initSprites(); entReset(); g_world.reset();
    static u32 first[VIEW_CELLS_W * VIEW_CELLS_H];
    static u32 second[VIEW_CELLS_W * VIEW_CELLS_H];
    /* A frame far enough into each creature's own cycle to have moved. Adding
       a species and forgetting a row here zero-fills it, so both samples come
       from frame 0 and the creature is reported as not animating when the real
       fault is in this table -- checked below rather than left as a puzzle. */
    const int later[ENT_COUNT] = { 0, 5, 6, 9, 7, 4, 6, 6, 6, 8 };

    for (int type = ENT_NONE + 1; type < ENT_COUNT; ++type) {
        if (later[type] == 0) {
            fprintf(stderr, "%s has no sample frame -- add a row to later[]\n",
                    ENT_DEFS[type].name);
            return 9;
        }
        memset(g_entities, 0, sizeof(g_entities));
        Entity& e = g_entities[0];
        e.type = (u8)type; e.hp = ENT_DEFS[type].hp; e.facing = 1;
        e.x = 100.0f; e.y = 100.0f; e.vx = 0.5f; e.onGround = true;

        memset(first, 0, sizeof(first)); g_world.frame = 0;
        e.walkPhase = 0.0f;
        entDraw(first, 0, 0, false);

        memset(second, 0, sizeof(second));
        g_world.frame = (u32)later[type];
        /* Exactly ONE stride further, not "a lot further".
           gaitStep alternates every max(2, h/3) cells, so a large advance
           lands on an arbitrary half of the cycle -- one body height happens
           to be an EVEN number of strides for some sizes, which put the
           creature back in the pose it started in and reported it as static.
           One stride past zero is the other half by construction, whatever
           the creature's size. Mirrors the stride rule in gaitStep. */
        const int stride = ENT_DEFS[type].h / 3 > 2 ? ENT_DEFS[type].h / 3 : 2;
        e.walkPhase = (float)stride + 0.5f;
        entDraw(second, 0, 0, false);

        int visible = 0, changed = 0;
        for (int p = 0; p < VIEW_CELLS_W * VIEW_CELLS_H; ++p) {
            if (first[p] || second[p]) ++visible;
            if (first[p] != second[p]) ++changed;
        }
        if (!visible || changed < 4) {
            fprintf(stderr, "%s animation changed only %d pixels\n",
                    ENT_DEFS[type].name, changed);
            return 10 + type;
        }
    }

    puts("all enemy and boss silhouettes animate");
    return 0;
}
