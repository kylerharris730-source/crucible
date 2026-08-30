/* ============================================================================
   rigshot.cpp -- lay a creature's baked frames out in a strip and look at them.

   A rig can compile, pass every test that asks whether pixels changed, and
   still be a bag of sticks. The tests can tell you the silhouette moved and
   that it is wider than the old sprite sheet; they cannot tell you it reads as
   a creature walking, and that is the only question worth asking of a gait.

   So this bakes the frames and writes them side by side, in order, magnified.
   A walk cycle laid out in a row is readable as a walk cycle by eye -- the
   limbs should travel, the body should ride, and frame 7 should lead back into
   frame 0 without a jump.

   Build like the other harnesses, then:
     ./artifacts/rigshot.exe && python scripts/ppm_to_png.py artifacts/visual/rig-thresher-walk.ppm out.png
   ========================================================================== */
#include "world.h"
#include "materials.h"
#include "sprite.h"
#include "rig.h"
#include "item.h"
#include "render.h"
#include "light.h"
#include "multiplayer.h"
#include "entity.h"

#include <stdio.h>
#include <string.h>
#include <direct.h>

static const int ZOOM = 6;
/* A flat mid grey behind everything: these sprites are transparent where they
   are empty, and on black the dark far-side limbs vanish into the background --
   which would hide exactly the depth the shade ladder exists to create. */
static const u32 BG = 0x2A2E36;

static bool writeStrip(const char* path, const u32* frames, int count,
                       int w, int h) {
    const int OW = w * count * ZOOM, OH = h * ZOOM;
    FILE* f = fopen(path, "wb");
    if (!f) { printf("cannot open %s\n", path); return false; }
    fprintf(f, "P6\n%d %d\n255\n", OW, OH);
    for (int y = 0; y < OH; ++y) {
        for (int x = 0; x < OW; ++x) {
            const int fi = (x / ZOOM) / w;
            const int sx = (x / ZOOM) % w, sy = y / ZOOM;
            u32 c = frames[(size_t)fi * w * h + (size_t)sy * w + sx];
            if (!c) c = BG;
            /* A one-pixel rule between frames, so it is obvious which pose is
               which rather than one long smear of limbs. */
            if ((x % (w * ZOOM)) == 0) c = 0x50566A;
            const unsigned char rgb[3] = {
                (unsigned char)((c >> 16) & 0xFF),
                (unsigned char)((c >> 8) & 0xFF),
                (unsigned char)(c & 0xFF) };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    printf("  %s  (%d frames of %dx%d at %dx)\n", path, count, w, h, ZOOM);
    return true;
}

int main(void) {
    _mkdir("artifacts");
    _mkdir("artifacts\\visual");
    initMaterials();
    initItems();
    initSprites();

    writeStrip("artifacts/visual/rig-thresher-walk.ppm", g_thresherWalk[0],
               THRESHER_WALK_FRAMES, THRESHER_SPR_W, THRESHER_SPR_H);
    writeStrip("artifacts/visual/rig-thresher-idle.ppm", g_thresherIdle[0],
               THRESHER_IDLE_FRAMES, THRESHER_SPR_W, THRESHER_SPR_H);
    writeStrip("artifacts/visual/rig-shambler-walk.ppm", g_shamblerWalk[0],
               SHAMBLER_WALK_FRAMES, SHAMBLER_SPR_W, SHAMBLER_SPR_H);

    /* The hand-drawn creatures too. Same reason: character art is typed in as
       a grid of letters, and a letter in the wrong column is invisible in the
       source and obvious the moment it is drawn. */
    static u32 sheet[4][SPR_W * SPR_H];
    const int ids[4] = { SPR_CULVERIN, SPR_WISP, SPR_STOOPER, SPR_SPITTER };
    for (int k = 0; k < 4; ++k)
        memcpy(sheet[k], g_sprite[ids[k]], sizeof(sheet[k]));
    writeStrip("artifacts/visual/spr-layer2.ppm", sheet[0], 4, SPR_W, SPR_H);
    return 0;
}
