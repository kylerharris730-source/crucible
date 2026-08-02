#include "armature.h"
#include <string.h>
#include <math.h>

/* --- angles ----------------------------------------------------------------
   A 360-entry table in 1/1024ths. Built once, and built from double precision
   so the table itself is exact even though everything reading it is integer.

   Integer trigonometry rather than calling sinf() per bone is not a
   micro-optimisation here, it is what keeps baking deterministic: a rig baked
   on two machines must produce the same pixels, or a screenshot test becomes a
   test of the floating-point library. */
static i16 g_sin[360], g_cos[360];
static bool g_trigReady = false;

static void initTrig() {
    if (g_trigReady) return;
    for (int d = 0; d < 360; ++d) {
        const double r = d * 3.14159265358979323846 / 180.0;
        g_sin[d] = (i16)lrint(sin(r) * 1024.0);
        g_cos[d] = (i16)lrint(cos(r) * 1024.0);
    }
    g_trigReady = true;
}

static inline int wrapDeg(int d) { d %= 360; return d < 0 ? d + 360 : d; }

/* --- the canvas ------------------------------------------------------------
   One shared subsample buffer, sized for the largest rig anybody bakes. Static
   rather than allocated because baking happens at startup on one thread and a
   96x256 byte scratch is not worth an allocator. */
static const int MAX_SS_W = 32 * ARM_SS;
static const int MAX_SS_H = 64 * ARM_SS;
static u8 g_ss[MAX_SS_W * MAX_SS_H];   /* 0 = empty, else shade + 1 */

/* A filled disc, which is how a capsule is drawn: stamp one every half-radius
   along the bone. Cruder than a proper capsule rasteriser and indistinguishable
   from one at these sizes, and it gets rounded joints for free -- which matters
   more than it sounds, because a limb drawn as a bare quad shows a notch at
   every bend. */
static void stamp(int cx, int cy, int r, u8 v, int w, int h) {
    const int r2 = r * r;
    int y0 = cy - r, y1 = cy + r, x0 = cx - r, x1 = cx + r;
    if (y0 < 0) y0 = 0;
    if (y1 >= h) y1 = h - 1;
    if (x0 < 0) x0 = 0;
    if (x1 >= w) x1 = w - 1;
    for (int y = y0; y <= y1; ++y) {
        const int dy = y - cy;
        for (int x = x0; x <= x1; ++x) {
            const int dx = x - cx;
            if (dx * dx + dy * dy <= r2) g_ss[y * w + x] = v;
        }
    }
}

/* --- reduction -------------------------------------------------------------
   A cell is lit if enough of its subsamples are, and takes the shade the most
   of them agree on.

   The threshold is the one number here that is a judgement rather than a
   derivation. At half coverage a limb one cell wide disappears whenever it is
   not axis-aligned, because a diagonal one-cell line covers about 40% of every
   cell it crosses -- so the legs would flicker in and out as they swung, which
   is the worst possible failure for a walk cycle. A third keeps thin diagonals
   solid and still refuses the stray corner where two limbs nearly touch. */
static const int ARM_COVER = (ARM_SS * ARM_SS) / 3;

static void reduce(const RigDef* rig, u32* out) {
    const int sw = rig->w * ARM_SS;
    for (int cy = 0; cy < rig->h; ++cy)
        for (int cx = 0; cx < rig->w; ++cx) {
            int count[64]; memset(count, 0, sizeof(count));
            int lit = 0;
            for (int sy = 0; sy < ARM_SS; ++sy) {
                const u8* row = g_ss + (cy * ARM_SS + sy) * sw + cx * ARM_SS;
                for (int sx = 0; sx < ARM_SS; ++sx)
                    if (row[sx]) { ++count[row[sx] & 63]; ++lit; }
            }
            if (lit < ARM_COVER) { out[cy * rig->w + cx] = 0; continue; }
            int best = 0, bestN = 0;
            for (int i = 1; i < 64; ++i) if (count[i] > bestN) { bestN = count[i]; best = i; }
            out[cy * rig->w + cx] = rig->shade[best - 1];
        }
}

void armPose(const RigDef* rig, const PoseKey* pose, u32* out, bool ground) {
    initTrig();
    const int sw = rig->w * ARM_SS, sh = rig->h * ARM_SS;
    memset(g_ss, 0, (size_t)sw * sh);

    /* Forward kinematics. Each bone's direction is its parent's plus its own,
       so a pose is a list of RELATIVE angles -- which is what makes a clip
       reusable across rigs with different proportions: "the knee is bent 30
       degrees" means the same thing on a long leg and a short one, where an
       absolute angle would not. */
    int tipX[ARM_MAX_BONES], tipY[ARM_MAX_BONES], dir[ARM_MAX_BONES];
    int baseX[ARM_MAX_BONES], baseY[ARM_MAX_BONES];

    for (int b = 0; b < rig->bones; ++b) {
        const Bone& bo = rig->bone[b];
        int bx, by, pd;
        if (bo.parent < 0) {
            bx = rig->rootX + pose->rootDX;
            by = rig->rootY + pose->rootDY;
            pd = 0;
        } else {
            const Bone& pa = rig->bone[bo.parent];
            bx = baseX[bo.parent] + (tipX[bo.parent] - baseX[bo.parent]) * bo.at / 255;
            by = baseY[bo.parent] + (tipY[bo.parent] - baseY[bo.parent]) * bo.at / 255;
            pd = dir[bo.parent];
            (void)pa;
        }
        /* 0 degrees points straight DOWN the screen, which is the rest pose of
           a limb hanging from a joint. Positive turns the tip forward, toward
           the side the rig faces. */
        const int d = wrapDeg(pd + bo.rest + pose->angle[b]);
        dir[b]  = d;
        baseX[b] = bx; baseY[b] = by;
        tipX[b] = bx + (bo.len * g_sin[d]) / 1024;
        tipY[b] = by + (bo.len * g_cos[d]) / 1024;
    }

    /* Painted in layer order so the far arm and leg go down before the torso
       and the near ones after it. Depth at this size is carried entirely by
       overlap and by shade -- there is no room for anything else, and without
       it a walk reads as one leg with a twitch. */
    for (int pass = 0; pass < 256; ++pass) {
        bool any = false;
        for (int b = 0; b < rig->bones; ++b) {
            const Bone& bo = rig->bone[b];
            if (bo.layer != pass) continue;
            any = true;
            if (bo.len <= 0 || (bo.wBase == 0 && bo.wTip == 0)) continue;
            const int steps = bo.len > 1 ? bo.len : 1;
            for (int s = 0; s <= steps; ++s) {
                const int x = baseX[b] + (tipX[b] - baseX[b]) * s / steps;
                const int y = baseY[b] + (tipY[b] - baseY[b]) * s / steps;
                const int r = (bo.wBase * (steps - s) + bo.wTip * s) / steps;
                stamp(x, y, r, (u8)(bo.shade + 1), sw, sh);
            }
        }
        if (!any && pass > 8) break;
    }

    if (ground) {
        /* Slid at SUBSAMPLE resolution, so the snap is a quarter of a cell
           rather than a whole one -- a foot forced up to the nearest cell would
           make the bob step instead of glide. */
        int lowest = -1;
        for (int y = sh - 1; y >= 0 && lowest < 0; --y)
            for (int x = 0; x < sw; ++x) if (g_ss[y * sw + x]) { lowest = y; break; }
        const int shift = (lowest >= 0) ? (sh - 1 - lowest) : 0;
        if (shift > 0) {
            memmove(g_ss + (size_t)shift * sw, g_ss, (size_t)(sh - shift) * sw);
            memset(g_ss, 0, (size_t)shift * sw);
        } else if (shift < 0) {
            memmove(g_ss, g_ss - (size_t)shift * sw, (size_t)(sh + shift) * sw);
            memset(g_ss + (size_t)(sh + shift) * sw, 0, (size_t)(-shift) * sw);
        }
    }

    reduce(rig, out);
}

void armBake(const RigDef* rig, const Clip* clip, u32* out) {
    const int cells = rig->w * rig->h;
    for (int f = 0; f < clip->frames; ++f) {
        /* Where this frame sits between keys. A looping clip wraps from the
           last key back to the first; a one-shot holds the last. */
        const int span = clip->loop ? clip->keys : clip->keys - 1;
        const int num  = f * span;
        const int k0   = num / clip->frames;
        const int t    = num % clip->frames;               /* 0..frames-1 */
        const int k1   = clip->loop ? (k0 + 1) % clip->keys
                                    : (k0 + 1 < clip->keys ? k0 + 1 : k0);

        PoseKey p;
        p.rootDX = (i16)((clip->key[k0].rootDX * (clip->frames - t)
                        + clip->key[k1].rootDX * t) / clip->frames);
        p.rootDY = (i16)((clip->key[k0].rootDY * (clip->frames - t)
                        + clip->key[k1].rootDY * t) / clip->frames);
        for (int b = 0; b < rig->bones; ++b) {
            /* Interpolated the SHORT way round, so a bone that swings from
               +170 to -170 takes the 20-degree path rather than sweeping 340
               degrees backwards through the body. Nothing in a walk needs it;
               a limb that windmills will. */
            int a0 = clip->key[k0].angle[b], a1 = clip->key[k1].angle[b];
            int d = a1 - a0;
            while (d >  180) d -= 360;
            while (d < -180) d += 360;
            p.angle[b] = (i16)(a0 + d * t / clip->frames);
        }
        armPose(rig, &p, out + (size_t)f * cells, clip->ground);
    }
}
