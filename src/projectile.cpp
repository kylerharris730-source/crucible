#include "projectile.h"
#include "item.h"     /* the nearest-first disc table, for explosions */
#include "render.h"   /* VIEW_CELLS_W/H */
#include <math.h>

Projectile g_proj[MAX_PROJ];

/* --- explosions ------------------------------------------------------------
   A disc of destruction with a falloff, then fire in the core and heat over the
   whole radius. The heat matters as much as the hole does: an explosion that
   only removed cells would read as a very fast shovel, and this world already
   has a rich reaction to being made hot -- things melt, boil, ignite and catch
   from each other. Handing the explosion a temperature makes it interact with
   every one of those rules for free. */
void explodeAt(World& w, int cx, int cy, int radius, int power) {
    if (radius < 1) radius = 1;
    if (radius > DISC_MAX_R) radius = DISC_MAX_R;

    const int n = g_discEnd[radius];
    const int r2 = radius * radius;
    for (int i = 0; i < n; ++i) {
        const int dx = g_disc[i].dx, dy = g_disc[i].dy;
        const int x = cx + dx, y = cy + dy;
        if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) continue;

        /* Falloff: full power at the centre, HALF at the rim. Without it every
           explosion leaves an identically clean circle, which reads as a cookie
           cutter -- the ragged edge where a material is only just too tough is
           what makes the shape look violent rather than stamped.

           Half rather than the third it started as, because falloff and a
           threshold model interact more sharply than they look. A module whose
           power exactly equals a material's strength destroys only the cells
           where falloff has taken nothing yet -- measured, a radius-14 blast at
           power 90 against strength-90 stone removed SIX cells. For a blast to
           carve a hole of radius R in a material, its power has to exceed that
           material's strength by the falloff across R, so a module's power
           number has to sit above the tier it is meant to break, not on it. */
        const int d2 = dx * dx + dy * dy;
        const int here = power - (power * d2) / (2 * (r2 ? r2 : 1));

        const u8 m = w.at(x, y).mat;
        if (m == MAT_EMPTY) continue;
        if (g_matStrength[m] == STR_NOTHING) continue;
        if ((int)g_matStrength[m] > here) continue;
        /* breakCell rather than clearing it: a pod caught in a blast leaves
           its seed to fall, which is the only thing in the game a destroyed
           cell leaves behind. See World::breakCell. */
        w.breakCell(x, y);
    }

    /* Fire in the core, scattered rather than solid so it looks like a
       fireball and not a painted disc. */
    const int fireR = radius / 2;
    const int fn = g_discEnd[fireR > 0 ? fireR : 1];
    for (int i = 0; i < fn; ++i) {
        const int x = cx + g_disc[i].dx, y = cy + g_disc[i].dy;
        if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) continue;
        if (w.at(x, y).mat != MAT_EMPTY) continue;
        if (!rngChance(90)) continue;
        w.setCell(x, y, MAT_FIRE);
    }

    w.heat(cx, cy, radius, 120);
}

int projExplosionsThisFrame = 0;

void projClear() {
    for (int i = 0; i < MAX_PROJ; ++i) g_proj[i].alive = false;
}

void projSpawn(float x, float y, float vx, float vy,
               int power, int pierce, int life, u32 colour, int blast,
               int payload) {
    for (int i = 0; i < MAX_PROJ; ++i) {
        if (g_proj[i].alive) continue;
        Projectile& p = g_proj[i];
        p.x = x; p.y = y; p.vx = vx; p.vy = vy;
        p.power = power; p.pierce = pierce; p.life = life; p.blast = blast;
        p.colour = colour; p.payload = (u8)payload; p.alive = true;
        return;
    }
    /* Full: drop it. Silently, because the alternative -- replacing the oldest
       -- makes shots vanish mid-flight, which looks like a bug from inside the
       game while dropping one at the muzzle just looks like it did not fire. */
}

int projUpdate(World& w) {
    int destroyed = 0;
    projExplosionsThisFrame = 0;

    for (int i = 0; i < MAX_PROJ; ++i) {
        Projectile& p = g_proj[i];
        if (!p.alive) continue;

        /* Running out of life FIZZLES rather than detonating. A blast shot
           fired at the sky going off on a timer, a hundred cells away from
           anything and several seconds after the click, is an explosion nobody
           can connect to an action they took. Blasts happen where you hit
           something. */
        if (--p.life <= 0) { p.alive = false; continue; }

        /* --- grid traversal, not point sampling --------------------------
           This walks EVERY cell the frame's path passes through, using the
           standard voxel-traversal step: keep the distance to the next vertical
           and horizontal grid line, and advance whichever comes first.

           Sub-stepping by a fraction of a cell -- which is what this was --
           looks correct and is not. Two problems, both of which read in-game as
           "shots phase through things". The obvious one is that a step longer
           than a cell can jump over a thin wall. The subtle one is that even
           with short steps, a diagonal path samples cells at its sample points
           only: moving from (0,0) to (1,1) in one step lands in a cell that
           shares only a CORNER with where it started, silently skipping both
           (0,1) and (1,0). A diagonal wall one cell thick is full of those
           corners, so a shot crossing it at an angle slipped through gaps that
           are not there. Traversal has no sample points and therefore no gaps.

           It also costs less: exactly one iteration per cell actually entered,
           where sub-stepping paid for a fixed number of samples whether they
           landed in new cells or not. */
        const float tx = p.x + p.vx, ty = p.y + p.vy;

        int cx = (int)p.x, cy = (int)p.y;
        const int endX = (int)tx, endY = (int)ty;

        const int stepX = (p.vx > 0) ? 1 : (p.vx < 0 ? -1 : 0);
        const int stepY = (p.vy > 0) ? 1 : (p.vy < 0 ? -1 : 0);

        /* Distance along the path, in units of the path's own length, to the
           next grid line in each axis; and how much one whole cell costs. */
        const float invX = (p.vx != 0.0f) ? 1.0f / (p.vx > 0 ? p.vx : -p.vx) : 1e30f;
        const float invY = (p.vy != 0.0f) ? 1.0f / (p.vy > 0 ? p.vy : -p.vy) : 1e30f;
        float nextX = (stepX > 0) ? ((float)(cx + 1) - p.x) * invX
                    : (stepX < 0) ? (p.x - (float)cx) * invX : 1e30f;
        float nextY = (stepY > 0) ? ((float)(cy + 1) - p.y) * invY
                    : (stepY < 0) ? (p.y - (float)cy) * invY : 1e30f;

        /* Bounded so a degenerate velocity can never spin here forever. The
           bound is generous: a shot cannot cross more cells in a frame than the
           width and height of the world put together. */
        int guard = SIM_W + SIM_H;
        bool blocked = false;
        /* Where a payload gets DEPOSITED, which is not always (cx,cy) -- see
           the note below on the two shapes a stop can take. -1 means "no
           valid impact point", for a shot that left the play area. */
        int dropX = -1, dropY = -1;

        while (guard-- > 0) {
            if (cx == endX && cy == endY) break;
            const int px_ = cx, py_ = cy;   /* the cell about to be left behind */
            if (nextX < nextY) { cx += stepX; nextX += invX; }
            else               { cy += stepY; nextY += invY; }

            if (cx < PLAY_X0 || cx > PLAY_X1 || cy < PLAY_Y0 || cy > PLAY_Y1) {
                p.alive = false; blocked = true; break;
            }

            const u8 m = w.at(cx, cy).mat;
            if (m == MAT_EMPTY) continue;

            const int strength = g_matStrength[m];
            if (strength == STR_NOTHING) continue;   /* gases: fly straight through */

            if (strength > p.power) {
                /* Stopped BY something it could not break -- that cell is
                   still occupied, so a payload cannot land there. It lands
                   one step back instead, in the last cell the shot actually
                   passed through (already open, or gas), which is where it
                   is visually sitting anyway once position is clamped below. */
                p.alive = false; blocked = true;
                dropX = px_; dropY = py_;
                break;
            }

            w.breakCell(cx, cy);
            ++destroyed;
            /* Broke THIS cell, so it is empty now -- the payload belongs
               here, at the point of impact, not one short of it. */
            dropX = cx; dropY = cy;
            if (--p.pierce <= 0) { p.alive = false; blocked = true; break; }
        }

        /* Stop where it stopped, not where it was aimed -- otherwise the blast
           of an exploding shot goes off a few cells past the wall it hit. */
        if (blocked) { p.x = (float)cx + 0.5f; p.y = (float)cy + 0.5f; }
        else         { p.x = tx; p.y = ty; }

        /* The blast FIRST, then the payload -- ordering that matters for the
           one combination that has both. explodeAt clears every cell weaker
           than its power, and every payload worth firing is a liquid or a
           powder, so placing the payload before the explosion means the
           shot's own blast wipes out the thing it just delivered. Detonate,
           then drop into the crater. */
        if (!p.alive && p.blast > 0) {
            explodeAt(w, (int)p.x, (int)p.y, p.blast, p.power);
            ++projExplosionsThisFrame;
        }

        /* A payload only ever delivers on an actual impact -- running out of
           LIFE mid-air (a shot fired at open sky) has no landing cell and
           should not conjure one out of the aim direction. See the note on
           dropX/dropY above for which cell that honestly is. */
        if (blocked && p.payload != MAT_EMPTY && dropX >= 0) {
            w.setCell(dropX, dropY, p.payload);
        }
    }
    return destroyed;
}

/* Takes VIEW coordinates. Callers convert from world by subtracting the
   camera, once, rather than every plot doing it again. */
static inline void plot(u32* px, int x, int y, u32 c) {
    if (x < 0 || x >= VIEW_CELLS_W || y < 0 || y >= VIEW_CELLS_H) return;
    px[y * VIEW_CELLS_W + x] = c;
}

/* Dim toward black by num/den, for the trail's falloff. */
static inline u32 fade(u32 c, int num, int den) {
    const int r = (((c >> 16) & 0xFF) * num) / den;
    const int g = (((c >> 8)  & 0xFF) * num) / den;
    const int b = (( c        & 0xFF) * num) / den;
    return ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

void projDraw(u32* px, int camX, int camY) {
    for (int i = 0; i < MAX_PROJ; ++i) {
        const Projectile& p = g_proj[i];
        if (!p.alive) continue;

        /* The trail is derived from the velocity rather than recorded from past
           positions. Shots travel in straight lines and never turn, so a stored
           history would hold exactly the points this computes -- at the cost of
           a ring buffer per projectile and a decision about what to do with a
           half-filled one on the frame it spawns. Recreating it is both cheaper
           and has no start-up case. */
        const float sp = (p.vx * p.vx + p.vy * p.vy);
        if (sp > 0.0001f) {
            const int TRAIL = 7;
            /* Normalised backwards step, one cell at a time. */
            float inv = 1.0f / (float)sqrt((double)sp);
            const float bx = -p.vx * inv, by = -p.vy * inv;
            for (int t = 1; t <= TRAIL; ++t) {
                const int x = (int)(p.x + bx * t) - camX, y = (int)(p.y + by * t) - camY;
                plot(px, x, y, fade(p.colour, TRAIL + 1 - t, TRAIL + 2));
            }
        }

        /* The head: a 3x3 with the corners knocked off, so it reads as a round
           bolt rather than a square. A single pixel -- and even the 2x2 this
           replaces -- is genuinely hard to follow across speckled terrain, and
           a shot you cannot see is a shot you cannot aim. */
        const int cx = (int)p.x - camX, cy = (int)p.y - camY;
        const u32 c = p.colour, e = fade(p.colour, 3, 5);
        plot(px, cx,     cy,     0xFFFFFF);
        plot(px, cx - 1, cy,     c);
        plot(px, cx + 1, cy,     c);
        plot(px, cx,     cy - 1, c);
        plot(px, cx,     cy + 1, c);
        plot(px, cx - 1, cy - 1, e);
        plot(px, cx + 1, cy - 1, e);
        plot(px, cx - 1, cy + 1, e);
        plot(px, cx + 1, cy + 1, e);
    }
}

int projCount() {
    int n = 0;
    for (int i = 0; i < MAX_PROJ; ++i) if (g_proj[i].alive) ++n;
    return n;
}
