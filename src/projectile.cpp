#include "projectile.h"

Projectile g_proj[MAX_PROJ];

void projClear() {
    for (int i = 0; i < MAX_PROJ; ++i) g_proj[i].alive = false;
}

void projSpawn(float x, float y, float vx, float vy,
               int power, int pierce, int life, u32 colour) {
    for (int i = 0; i < MAX_PROJ; ++i) {
        if (g_proj[i].alive) continue;
        Projectile& p = g_proj[i];
        p.x = x; p.y = y; p.vx = vx; p.vy = vy;
        p.power = power; p.pierce = pierce; p.life = life;
        p.colour = colour; p.alive = true;
        return;
    }
    /* Full: drop it. Silently, because the alternative -- replacing the oldest
       -- makes shots vanish mid-flight, which looks like a bug from inside the
       game while dropping one at the muzzle just looks like it did not fire. */
}

int projUpdate(World& w) {
    int destroyed = 0;

    for (int i = 0; i < MAX_PROJ; ++i) {
        Projectile& p = g_proj[i];
        if (!p.alive) continue;

        if (--p.life <= 0) { p.alive = false; continue; }

        /* Walk the frame's movement in sub-cell steps rather than jumping to
           the destination. A shot moves several cells a frame, and testing only
           where it LANDS would let it tunnel straight through a one-cell wall
           whenever the wall happened to fall between two samples -- which for a
           mining tool means it sometimes ignores the very thing you aimed it
           at, at random, depending on sub-pixel alignment. */
        const float speed = (p.vx > 0 ? p.vx : -p.vx) + (p.vy > 0 ? p.vy : -p.vy);
        int steps = (int)(speed + 1.0f);
        if (steps < 1) steps = 1;
        const float sx = p.vx / steps, sy = p.vy / steps;

        int lastCx = (int)p.x, lastCy = (int)p.y;
        for (int s = 0; s < steps && p.alive; ++s) {
            p.x += sx; p.y += sy;
            const int cx = (int)p.x, cy = (int)p.y;
            if (cx == lastCx && cy == lastCy) continue;   /* same cell, nothing new */
            lastCx = cx; lastCy = cy;

            if (cx < PLAY_X0 || cx > PLAY_X1 || cy < PLAY_Y0 || cy > PLAY_Y1) {
                p.alive = false;
                break;
            }

            const u8 m = w.at(cx, cy).mat;
            if (m == MAT_EMPTY) continue;

            const int strength = g_matStrength[m];
            if (strength == STR_NOTHING) continue;   /* gases, liquids: fly through */

            if (strength > p.power) { p.alive = false; break; }   /* too hard */

            w.setCell(cx, cy, MAT_EMPTY);
            ++destroyed;
            if (--p.pierce <= 0) { p.alive = false; break; }
        }
    }
    return destroyed;
}

void projDraw(u32* px) {
    for (int i = 0; i < MAX_PROJ; ++i) {
        const Projectile& p = g_proj[i];
        if (!p.alive) continue;
        const int cx = (int)p.x, cy = (int)p.y;
        /* A 2x2 blob rather than a single pixel. One sim pixel at this scale is
           genuinely hard to follow against speckled terrain, and a shot you
           cannot see is a shot you cannot aim. */
        for (int dy = 0; dy <= 1; ++dy)
            for (int dx = 0; dx <= 1; ++dx) {
                const int x = cx + dx, y = cy + dy;
                if (x < 0 || x >= SIM_W || y < 0 || y >= SIM_H) continue;
                px[y * SIM_W + x] = p.colour;
            }
    }
}

int projCount() {
    int n = 0;
    for (int i = 0; i < MAX_PROJ; ++i) if (g_proj[i].alive) ++n;
    return n;
}
