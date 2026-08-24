#include "projectile.h"
#include "entity.h"
#include "item.h"     /* the nearest-first disc table, for explosions */
#include "render.h"   /* VIEW_CELLS_W/H */
#include "light.h"
#include "multiplayer.h"
#include <math.h>

Projectile g_proj[MAX_PROJ];

/* --- projectile wake ------------------------------------------------------
   A tail is history, not a copy of the projectile drawn backwards. These
   visual-only motes are shed into world space, then drift and fade on their own.
   Slight scatter breaks the ruler-straight laser streak without touching the
   simulation RNG or changing projectile collision. */
struct TrailMote {
    float x, y, vx, vy;
    u32 colour;
    i16 life, fullLife;
    bool alive;
};

static const int MAX_TRAIL_MOTES = 512;
static TrailMote g_trailMotes[MAX_TRAIL_MOTES];
static int g_trailMoteCursor = 0;
static u32 g_trailRandom = 0x73a91f25u;

static float trailRandom() {
    /* Separate from material RNG: changing a cosmetic tail must never change
       where sand falls or whether a reaction occurs. */
    g_trailRandom ^= g_trailRandom << 13;
    g_trailRandom ^= g_trailRandom >> 17;
    g_trailRandom ^= g_trailRandom << 5;
    return (float)(g_trailRandom & 0xffffu) / 65535.0f;
}

static void trailMoteEmit(const Projectile& p) {
    const float speed = sqrtf(p.vx * p.vx + p.vy * p.vy);
    if (speed < 0.001f) return;
    const float forwardX = p.vx / speed, forwardY = p.vy / speed;
    const float sideX = -forwardY, sideY = forwardX;
    /* One guaranteed mote makes even a quick close-range shot leave a readable
       wake; the occasional second mote supplies density without becoming a
       continuous line again. */
    const int count = trailRandom() < 0.45f ? 2 : 1;
    for (int n = 0; n < count; ++n) {
        int slot = g_trailMoteCursor;
        for (int scanned = 0; scanned < MAX_TRAIL_MOTES; ++scanned) {
            const int candidate = (g_trailMoteCursor + scanned) % MAX_TRAIL_MOTES;
            if (!g_trailMotes[candidate].alive) { slot = candidate; break; }
        }
        TrailMote& m = g_trailMotes[slot];
        const float behind = 0.5f + 2.4f * trailRandom();
        const float aside = (trailRandom() - 0.5f) * 1.5f;
        m.x = p.x - forwardX * behind + sideX * aside;
        m.y = p.y - forwardY * behind + sideY * aside;
        const float driftBack = 0.015f + 0.045f * trailRandom();
        const float driftSide = (trailRandom() - 0.5f) * 0.12f;
        m.vx = -forwardX * driftBack + sideX * driftSide;
        m.vy = -forwardY * driftBack + sideY * driftSide;
        m.colour = p.colour;
        m.life = m.fullLife = (i16)(11 + (int)(10.0f * trailRandom()));
        m.alive = true;
        g_trailMoteCursor = (slot + 1) % MAX_TRAIL_MOTES;
    }
}

static void trailMotesTick() {
    for (int i = 0; i < MAX_TRAIL_MOTES; ++i) {
        TrailMote& m = g_trailMotes[i];
        if (!m.alive) continue;
        if (--m.life <= 0) { m.alive = false; continue; }
        m.x += m.vx; m.y += m.vy;
        m.vx *= 0.94f; m.vy *= 0.94f;
    }
}

/* --- Glowflare reveal burst -----------------------------------------------

   These are overlays rather than cells or projectiles. They deliberately do
   not even receive a World in their tick, which makes the promise that they
   pass through rock structural rather than a collision exception somebody can
   accidentally remove later. Their only effects are a moving dynamic light
   source and a bright pixel. As they travel they leave bounded, invisible
   afterglow points so the rock stays revealed briefly after the visible mote
   has passed. */
struct GlowMote {
    float x, y, vx, vy;
    float lastGlowX, lastGlowY;
    i16 life;
    bool alive;
};

struct GlowAfterglow {
    float x, y;
    i16 life;
    bool alive;
};

static const int MAX_GLOW_MOTES = 256;
static const int GLOW_BURST_MOTES = 28;
static const int MAX_GLOW_AFTERGLOWS = 2048;
static const int GLOW_AFTERGLOW_HOLD = 60;
static const int GLOW_AFTERGLOW_FADE = 50;
static const int GLOW_AFTERGLOW_LIFE = GLOW_AFTERGLOW_HOLD + GLOW_AFTERGLOW_FADE;
static const float GLOW_TRAIL_SPACING = 6.0f;
static GlowMote g_glowMotes[MAX_GLOW_MOTES];
static GlowAfterglow g_glowAfterglows[MAX_GLOW_AFTERGLOWS];
static int g_glowAfterglowCursor = 0;

static void glowAfterglowSpawn(float x, float y) {
    /* Reuse a dead slot when possible. If several flares somehow fill the
       bounded pool, replace the oldest ring position: fresh reveal is more
       useful than preserving the tail end of an already fading trail. */
    int slot = g_glowAfterglowCursor;
    for (int scanned = 0; scanned < MAX_GLOW_AFTERGLOWS; ++scanned) {
        const int candidate = (g_glowAfterglowCursor + scanned) % MAX_GLOW_AFTERGLOWS;
        if (!g_glowAfterglows[candidate].alive) { slot = candidate; break; }
    }
    GlowAfterglow& glow = g_glowAfterglows[slot];
    glow.x = x;
    glow.y = y;
    glow.life = GLOW_AFTERGLOW_LIFE;
    glow.alive = true;
    g_glowAfterglowCursor = (slot + 1) % MAX_GLOW_AFTERGLOWS;
}

static void glowAfterglowsTick() {
    for (int i = 0; i < MAX_GLOW_AFTERGLOWS; ++i) {
        GlowAfterglow& glow = g_glowAfterglows[i];
        if (glow.alive && --glow.life <= 0) glow.alive = false;
    }
}

static float glowBurstRandom(u32& state) {
    /* A local generator keeps the slight scatter deterministic without
       consuming the world's simulation RNG. Replaying the same impact thus
       reveals the same rock and cannot perturb unrelated material motion. */
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (float)(state & 0xffffu) / 65535.0f;
}

static void glowBurst(float x, float y, float impactVx, float impactVy) {
    static const float HALF_CONE = 0.68f; /* about 78 degrees edge-to-edge */
    static const int PAIRS = GLOW_BURST_MOTES / 2;

    float length = sqrtf(impactVx * impactVx + impactVy * impactVy);
    if (length < 0.001f) { impactVx = 1.0f; impactVy = 0.0f; length = 1.0f; }
    const float forwardX = impactVx / length;
    const float forwardY = impactVy / length;
    const float sideX = -forwardY;
    const float sideY = forwardX;

    u32 random = (u32)((int)(x * 16.0f) * 0x9e3779b9u)
               ^ (u32)((int)(y * 16.0f) * 0x85ebca6bu)
               ^ (u32)((int)(forwardX * 1024.0f) * 0xc2b2ae35u)
               ^ (u32)((int)(forwardY * 1024.0f) * 0x27d4eb2fu)
               ^ 0xa341316cu;
    glowAfterglowSpawn(x, y);
    int made = 0;
    for (int pair = 0; pair < PAIRS && made < GLOW_BURST_MOTES; ++pair) {
        /* Stratify the cone so random variation cannot leave a conspicuous
           empty wedge. Mirroring every randomized ray makes it a balanced
           shotgun fan rather than the old perfect wheel or a lopsided spray. */
        const float band = ((float)pair + 0.30f + 0.40f * glowBurstRandom(random))
                         / (float)PAIRS;
        const float angle = HALF_CONE * band;
        const float along = cosf(angle);
        const float across = sinf(angle);
        /* Slower launch plus gentler drag lets the fan read as glowing dust
           travelling through rock instead of a one-frame flash. The longer
           life more than compensates for that speed: motes ultimately reach
           a little farther than before, then linger along the revealed path. */
        const float speed = 2.1f + 1.8f * glowBurstRandom(random);
        const i16 life = (i16)(78 + (int)(17.0f * glowBurstRandom(random)));

        for (int side = -1; side <= 1; side += 2) {
            int slot = 0;
            while (slot < MAX_GLOW_MOTES && g_glowMotes[slot].alive) ++slot;
            if (slot == MAX_GLOW_MOTES) return;
            GlowMote& m = g_glowMotes[slot];
            m.x = x; m.y = y;
            m.vx = (forwardX * along + sideX * across * (float)side) * speed;
            m.vy = (forwardY * along + sideY * across * (float)side) * speed;
            m.lastGlowX = x; m.lastGlowY = y;
            m.life = life;
            m.alive = true;
            ++made;
        }
    }
}

static void glowMotesTick() {
    for (int i = 0; i < MAX_GLOW_MOTES; ++i) {
        GlowMote& m = g_glowMotes[i];
        if (!m.alive) continue;
        if (--m.life <= 0) {
            glowAfterglowSpawn(m.x, m.y);
            m.alive = false;
            continue;
        }
        /* No cell query here: motes cross liquids, walls, ores and machines.
           Drag makes the burst expand quickly and then hang for only a moment
           around the rock it just revealed. */
        m.x += m.vx; m.y += m.vy;
        m.vx *= 0.975f; m.vy *= 0.975f;
        const float glowDx = m.x - m.lastGlowX;
        const float glowDy = m.y - m.lastGlowY;
        if (glowDx * glowDx + glowDy * glowDy >=
            GLOW_TRAIL_SPACING * GLOW_TRAIL_SPACING) {
            glowAfterglowSpawn(m.x, m.y);
            m.lastGlowX = m.x; m.lastGlowY = m.y;
        }
        if (m.x < PLAY_X0 || m.x > PLAY_X1 || m.y < PLAY_Y0 || m.y > PLAY_Y1)
            m.alive = false;
    }
}

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
    for (int i = 0; i < MAX_TRAIL_MOTES; ++i) g_trailMotes[i].alive = false;
    g_trailMoteCursor = 0;
    g_trailRandom = 0x73a91f25u;
    for (int i = 0; i < MAX_GLOW_MOTES; ++i) g_glowMotes[i].alive = false;
    for (int i = 0; i < MAX_GLOW_AFTERGLOWS; ++i) g_glowAfterglows[i].alive = false;
    g_glowAfterglowCursor = 0;
}

bool projSpawn(float x, float y, float vx, float vy,
               int power, int pierce, int life, u32 colour, int blast,
               int payload, int damage, bool hostile, float gravity, int effect,
               int bounces, float homing, u8 owner) {
    for (int i = 0; i < MAX_PROJ; ++i) {
        if (g_proj[i].alive) continue;
        Projectile& p = g_proj[i];
        p.x = x; p.y = y; p.vx = vx; p.vy = vy;
        p.power = power; p.pierce = pierce; p.life = life; p.blast = blast;
        p.bounces = (i16)bounces; p.homing = homing;
        p.colour = colour; p.payload = (u8)payload; p.damage = damage;
        p.hostile = hostile; p.gravity = gravity; p.effect = (u8)effect;
        p.owner = owner; p.alive = true;
        return true;
    }
    /* Full: drop it. Silently, because the alternative -- replacing the oldest
       -- makes shots vanish mid-flight, which looks like a bug from inside the
       game while dropping one at the muzzle just looks like it did not fire. */
    return false;
}

static bool teleportFits(const World& w, float cx, float cy) {
    const int left = (int)(cx - PLAYER_W * 0.5f);
    const int top  = (int)(cy - PLAYER_H * 0.5f);
    for (int y = top; y < top + PLAYER_H; ++y)
        for (int x = left; x < left + PLAYER_W; ++x)
            if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1 ||
                playerSolid(w, x, y)) return false;
    return true;
}

static void teleportProjectileOwner(const World& w, Projectile& p, int impactX, int impactY) {
    if (p.owner >= MAX_PLAYERS || !g_playerSessions[p.owner].connected) return;
    PlayerSession& session = g_playerSessions[p.owner];
    Player& body = session.body;
    if (!body.alive) return;
    float speed = sqrtf(p.vx * p.vx + p.vy * p.vy);
    float backX = speed > 0.001f ? -p.vx / speed : -1.0f;
    float backY = speed > 0.001f ? -p.vy / speed : 0.0f;
    /* The impact cell is safe for a projectile, not necessarily a whole body.
       Walk backward along the shot until the complete player box fits. */
    for (int step = 0; step <= PLAYER_H * 2; ++step) {
        const float cx = (float)impactX + 0.5f + backX * (float)step;
        const float cy = (float)impactY + 0.5f + backY * (float)step;
        if (!teleportFits(w, cx, cy)) continue;
        body.x = cx - PLAYER_W * 0.5f;
        body.y = cy - PLAYER_H * 0.5f;
        body.vx = body.vy = 0.0f;
        body.fallFromY = body.y;
        body.onGround = false; body.buried = false;
        session.restBed = -1;
        return;
    }
}

static bool projectileObstacle(const World& w, int x, int y, int power) {
    if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) return true;
    const u8 mat = w.at(x, y).mat;
    if (mat == MAT_TORCH) return false;
    const int strength = g_matStrength[mat];
    return strength != STR_NOTHING && strength > power;
}

/* Fit a short line through nearby boundary pixels, then use its perpendicular
   as the collision normal. The old normal was whichever grid axis happened to
   be crossed last, so a diagonal rock face still behaved as a stack of tiny
   floors and walls. A four-cell neighbourhood is large enough to see the line
   behind those steps and small enough not to average across a whole cave. */
static void projectileSurfaceNormal(const World& w, int hitX, int hitY, int power,
                                    float vx, float vy, float* outX, float* outY) {
    static const int R = 4;
    float pointX[(R * 2 + 1) * (R * 2 + 1)];
    float pointY[(R * 2 + 1) * (R * 2 + 1)];
    float weight[(R * 2 + 1) * (R * 2 + 1)];
    int count = 0;
    float meanX = 0.0f, meanY = 0.0f, weightSum = 0.0f;
    for (int y = hitY - R; y <= hitY + R; ++y) {
        for (int x = hitX - R; x <= hitX + R; ++x) {
            if (!projectileObstacle(w, x, y, power)) continue;
            /* Only the exposed outline defines the visible surface. Interior
               mass would bias the fit toward whichever side has more rock. */
            if (projectileObstacle(w, x - 1, y, power) &&
                projectileObstacle(w, x + 1, y, power) &&
                projectileObstacle(w, x, y - 1, power) &&
                projectileObstacle(w, x, y + 1, power)) continue;
            const float px = (float)x + 0.5f, py = (float)y + 0.5f;
            const float dx = px - ((float)hitX + 0.5f);
            const float dy = py - ((float)hitY + 0.5f);
            const float wgt = 1.0f / (1.0f + 0.18f * (dx * dx + dy * dy));
            pointX[count] = px; pointY[count] = py; weight[count] = wgt;
            meanX += px * wgt; meanY += py * wgt; weightSum += wgt;
            ++count;
        }
    }

    float nx = 0.0f, ny = 0.0f;
    if (count >= 2 && weightSum > 0.001f) {
        meanX /= weightSum; meanY /= weightSum;
        float xx = 0.0f, xy = 0.0f, yy = 0.0f;
        for (int i = 0; i < count; ++i) {
            const float dx = pointX[i] - meanX, dy = pointY[i] - meanY;
            xx += weight[i] * dx * dx;
            xy += weight[i] * dx * dy;
            yy += weight[i] * dy * dy;
        }
        /* Principal axis is the local surface tangent; rotate it 90 degrees. */
        const float angle = 0.5f * atan2f(2.0f * xy, xx - yy);
        nx = -sinf(angle); ny = cosf(angle);
    }

    float length = sqrtf(nx * nx + ny * ny);
    if (length < 0.001f) {
        /* One isolated pixel has no line to fit. Its honest normal is directly
           against the incoming motion, producing a clean return rather than an
           arbitrary x/y flip. */
        length = sqrtf(vx * vx + vy * vy);
        nx = length > 0.001f ? -vx / length : 0.0f;
        ny = length > 0.001f ? -vy / length : -1.0f;
    } else {
        nx /= length; ny /= length;
    }
    if (vx * nx + vy * ny > 0.0f) { nx = -nx; ny = -ny; }
    *outX = nx; *outY = ny;
}

int projUpdate(World& w) {
    int destroyed = 0;
    projExplosionsThisFrame = 0;
    trailMotesTick();
    glowAfterglowsTick();
    glowMotesTick();

    for (int i = 0; i < MAX_PROJ; ++i) {
        Projectile& p = g_proj[i];
        if (!p.alive) continue;

        /* Running out of life FIZZLES rather than detonating. A blast shot
           fired at the sky going off on a timer, a hundred cells away from
           anything and several seconds after the click, is an explosion nobody
           can connect to an action they took. Blasts happen where you hit
           something. */
        if (--p.life <= 0) { p.alive = false; continue; }

        if (!p.hostile && p.homing > 0.0f) {
            int target = -1;
            float best = 120.0f * 120.0f;
            for (int e = 0; e < MAX_ENTITIES; ++e) {
                if (!g_entities[e].alive()) continue;
                const float dx = g_entities[e].centreX() - p.x;
                const float dy = g_entities[e].centreY() - p.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < best) { best = d2; target = e; }
            }
            if (target >= 0) {
                const float dx = g_entities[target].centreX() - p.x;
                const float dy = g_entities[target].centreY() - p.y;
                const float dist = sqrtf(dx * dx + dy * dy);
                const float speed = sqrtf(p.vx * p.vx + p.vy * p.vy);
                if (dist > 0.001f && speed > 0.001f) {
                    const float desiredX = dx * speed / dist;
                    const float desiredY = dy * speed / dist;
                    p.vx += (desiredX - p.vx) * p.homing;
                    p.vy += (desiredY - p.vy) * p.homing;
                }
            }
        }

        trailMoteEmit(p);

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

        /* Gravity is applied to the VELOCITY before the path is built, so the
           traversal below still walks a straight segment -- one frame's worth
           of a parabola is a straight line to well under a cell, and the whole
           point of the traversal is that it misses nothing along the segment it
           is given. Integrating inside the walk would make the segment curve
           away from the grid lines it just measured its distances to, which is
           exactly the sampling gap the traversal exists to avoid.

           Before the path, not after, so the first frame of flight already has
           some drop in it. Applying it afterwards would make the muzzle frame
           perfectly flat and put a visible kink at the start of every arc. */
        p.vy += p.gravity;

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
        bool blocked = false, ricochet = false;
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

            /* Creatures are checked BEFORE the material, and before the
               MAT_EMPTY skip, because they live in the air: an enemy standing
               in an open tunnel occupies cells that are all MAT_EMPTY, so a
               test placed after that `continue` would never see one. A shot
               spends itself on a body rather than passing through -- which is
               also what stops a single pierce-10 shot from killing a whole
               line of them at once. */
            if (p.damage > 0) {
                if (!p.hostile) {
                    if (entDamageAt(cx, cy, p.damage)) {
                        p.alive = false; blocked = true;
                        dropX = px_; dropY = py_;
                        break;
                    }
                } else {
                    for (int slot = 0; slot < MAX_PLAYERS; ++slot) {
                        PlayerSession& session = g_playerSessions[slot];
                        Player& player = session.body;
                        if (!session.connected || !player.alive ||
                            cx < player.left() || cx > player.right() ||
                            cy < player.top() || cy > player.bottom()) continue;
                        /* Armour belongs to the player actually struck; remote
                           gear never protects the host by accident. */
                        const int dmg = imax(1, p.damage - session.inventory.armour());
                        player.damage((float)dmg); player.hurtFlash = 10;
                        p.alive = false; blocked = true;
                        dropX = px_; dropY = py_;
                        break;
                    }
                    if (blocked) break;
                }
            }

            const u8 m = w.at(cx, cy).mat;
            if (m == MAT_EMPTY) continue;
            /* A torch is mounted scenery, not cover. It is deliberately
               passable to people already; making a bullet spend itself on its
               one-cell flame turned a carefully lit tunnel into accidental
               target practice. Keep blast damage physical, but ordinary shots
               neither break nor stop on a torch. */
            if (m == MAT_TORCH) continue;

            const int strength = g_matStrength[m];
            if (strength == STR_NOTHING) continue;   /* gases: fly straight through */

            if (strength > p.power) {
                if (p.bounces > 0) {
                    p.x = (float)px_ + 0.5f; p.y = (float)py_ + 0.5f;
                    float nx, ny;
                    projectileSurfaceNormal(w, cx, cy, p.power, p.vx, p.vy, &nx, &ny);
                    const float into = p.vx * nx + p.vy * ny;
                    p.vx -= 2.0f * into * nx;
                    p.vy -= 2.0f * into * ny;
                    --p.bounces;
                    ricochet = true;
                    break;
                }
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

        if (ricochet) continue;

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
            /* Everything in the crater, not only whatever the shot happened to
               touch on its way in. An explosion that damaged one creature
               would be strictly worse than the ordinary shot it costs 24 extra
               frames of delay to fire. */
            if (p.damage > 0 && !p.hostile)
                entDamageDisc((int)p.x, (int)p.y, p.blast, p.damage);
            ++projExplosionsThisFrame;
        }

        /* A payload only ever delivers on an actual impact -- running out of
           LIFE mid-air (a shot fired at open sky) has no landing cell and
           should not conjure one out of the aim direction. See the note on
           dropX/dropY above for which cell that honestly is. */
        if (blocked && p.payload != MAT_EMPTY && dropX >= 0) {
            w.setCell(dropX, dropY, p.payload);
        }
        if (blocked && p.effect == PROJ_EFFECT_GLOWFLARE && dropX >= 0)
            glowBurst(p.x, p.y, p.vx, p.vy);
        if (blocked && p.effect == PROJ_EFFECT_TELEPORT && dropX >= 0)
            teleportProjectileOwner(w, p, dropX, dropY);
    }
    return destroyed;
}

void projRegisterLights() {
    /* The intact ampoule is visible in flight; impact replaces that one moving
       point with a fan of stronger sources travelling through the rock. */
    for (int i = 0; i < MAX_PROJ; ++i)
        if (g_proj[i].alive && g_proj[i].effect == PROJ_EFFECT_GLOWFLARE)
            lightAddDynamic((int)g_proj[i].x, (int)g_proj[i].y, 110);
    for (int i = 0; i < MAX_GLOW_MOTES; ++i) {
        const GlowMote& m = g_glowMotes[i];
        if (!m.alive) continue;
        const int level = imin(128, (int)m.life * 4);
        const int x = (int)m.x, y = (int)m.y;
        lightAddDynamic(x, y, (u8)level);
        /* The rebalanced core tops out at 128. A softer
           source in each neighbouring light sample makes that energy occupy
           an area instead of a single four-cell sample, so thick stone is
           visibly brighter without changing global rock opacity. */
        const u8 halo = (u8)imin(110, (int)m.life * 3);
        lightAddDynamic(x - LIGHT_CELL, y, halo);
        lightAddDynamic(x + LIGHT_CELL, y, halo);
        lightAddDynamic(x, y - LIGHT_CELL, halo);
        lightAddDynamic(x, y + LIGHT_CELL, halo);
    }
    for (int i = 0; i < MAX_GLOW_AFTERGLOWS; ++i) {
        const GlowAfterglow& glow = g_glowAfterglows[i];
        if (!glow.alive) continue;
        /* Hold for one second, then take most of another to disappear. The
           trail is invisible: only the moving dust is drawn, while these
           points preserve the glimpse it opened in the stone. */
        const int level = glow.life > GLOW_AFTERGLOW_FADE
                        ? 110
                        : (110 * (int)glow.life) / GLOW_AFTERGLOW_FADE;
        lightAddDynamic((int)glow.x, (int)glow.y, (u8)level);
    }
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
    /* Persistent particles first, so fresh projectile heads remain crisp over
       their own wake. Each mote fades independently instead of forming the old
       solid velocity ruler behind every shot. */
    for (int i = 0; i < MAX_TRAIL_MOTES; ++i) {
        const TrailMote& m = g_trailMotes[i];
        if (!m.alive || m.fullLife <= 0) continue;
        const int x = (int)m.x - camX, y = (int)m.y - camY;
        const u32 c = fade(m.colour, m.life, m.fullLife);
        plot(px, x, y, c);
        if (m.life * 3 > m.fullLife * 2 && (i & 1) == 0) {
            const u32 halo = fade(c, 1, 2);
            plot(px, x + ((i & 2) ? 1 : -1), y, halo);
        }
    }

    for (int i = 0; i < MAX_PROJ; ++i) {
        const Projectile& p = g_proj[i];
        if (!p.alive) continue;

        /* Homing shots are deliberately a much larger orb. Their slow movement
           and expensive battery draw should look like a substantial guided
           object, not the ordinary bolt drifting lazily. Other heads remain a
           compact 3x3 with their corners knocked off. */
        const int cx = (int)p.x - camX, cy = (int)p.y - camY;
        const u32 c = p.colour, e = fade(p.colour, 3, 5);
        if (p.homing > 0.0f) {
            const int radius = 3;
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int d2 = dx * dx + dy * dy;
                    if (d2 > radius * radius) continue;
                    const u32 orb = d2 <= 1 ? 0xFFFFFF : (d2 <= 4 ? c : e);
                    plot(px, cx + dx, cy + dy, orb);
                }
            }
            continue;
        }
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

    for (int i = 0; i < MAX_GLOW_MOTES; ++i) {
        const GlowMote& m = g_glowMotes[i];
        if (!m.alive) continue;
        const int x = (int)m.x - camX, y = (int)m.y - camY;
        const int n = imin(42, (int)m.life);
        const u32 c = fade(0x9AF4BE, n, 42);
        plot(px, x, y, m.life > 24 ? 0xE8FFF0 : c);
        if (m.life > 30) {
            const u32 edge = fade(c, 2, 3);
            plot(px, x - 1, y, edge); plot(px, x + 1, y, edge);
            plot(px, x, y - 1, edge); plot(px, x, y + 1, edge);
        }
    }
}

int projCount() {
    int n = 0;
    for (int i = 0; i < MAX_PROJ; ++i) if (g_proj[i].alive) ++n;
    return n;
}

int projTrailMoteCount() {
    int n = 0;
    for (int i = 0; i < MAX_TRAIL_MOTES; ++i) if (g_trailMotes[i].alive) ++n;
    return n;
}

int projGlowMoteCount() {
    int n = 0;
    for (int i = 0; i < MAX_GLOW_MOTES; ++i) if (g_glowMotes[i].alive) ++n;
    return n;
}

int projGlowAfterglowCount() {
    int n = 0;
    for (int i = 0; i < MAX_GLOW_AFTERGLOWS; ++i)
        if (g_glowAfterglows[i].alive) ++n;
    return n;
}

int projGlowMoteSnapshot(float* xs, float* ys, int capacity) {
    int n = 0;
    for (int i = 0; i < MAX_GLOW_MOTES && n < capacity; ++i) {
        if (!g_glowMotes[i].alive) continue;
        xs[n] = g_glowMotes[i].x;
        ys[n] = g_glowMotes[i].y;
        ++n;
    }
    return n;
}
