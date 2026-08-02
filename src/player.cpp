#include "player.h"
#include "device.h"
#include "sprite.h"   /* the posed frames; player.h no longer pulls it in, since
                        sprite.h now needs PLAYER_W to size its canvas */
#include "light.h"    /* VIEW_CELLS_W/H, and shading the figure by the field */
#include <math.h>

Player g_player;

/* The sprite IS the hitbox here -- see the note in sprite.h. If these ever
   drift apart, material rests visibly inside the character and the shed off the
   shoulders stops making sense, so it is worth failing the build over. */
static_assert(PSPR_W == PLAYER_W && PSPR_H == PLAYER_H,
              "the character sprite must be exactly the size of the collision box");

/* Tuning, in cells and frames at the fixed 60Hz step. Written as the numbers
   they are rather than derived, but the jump is worth showing the working for:
   peak height is v^2 / 2g, so 2.6 and 0.18 give about 18 cells of clearance and
   an apex a quarter of a second away. That is deliberately snappy -- a floatier
   jump reads badly when the ground can vanish underneath you mid-arc. */
static const float GRAVITY     = 0.18f;
static const float MAX_FALL    = 6.0f;   /* terminal velocity, cells/frame */
static const float MOVE_ACCEL  = 0.35f;
static const float MAX_SPEED   = 1.2f;   /* ~72 cells/second */
static const float GROUND_DRAG = 0.55f;  /* stop briskly when input released */
static const float AIR_DRAG    = 0.92f;  /* keep most momentum in the air */
static const float JUMP_VEL    = 2.6f;
/* Frames each idle pose is held. 40 is two thirds of a second, so the breath is
   about a second and a third end to end -- slow enough to read as breathing
   rather than as a twitch. */
static const int   IDLE_HOLD   = 40;

bool playerSolid(const World& w, int x, int y, int mode) {
    /* Outside the world counts as solid, so the player can never be walked out
       of bounds and no caller has to bounds-check first. */
    if (x < 0 || x >= SIM_W || y < 0 || y >= SIM_H) return true;
    const Cell& c = w.at(x, y);
    /* The platform, before the kind test, because it is an exception to
       g_matPassable rather than to anything about kinds: solid when the
       question is "can I stand here" and thin air otherwise. */
    if (g_matPlatform[c.mat]) return mode == SOLID_FLOOR;
    /* Pipes are equipment you walk through, not waist-high walls. Their shared
       MAT_DEVICE footprint still blocks sand and seals rooms; this is only the
       player's collision exception, kept beside the question it answers. */
    if (c.mat == MAT_DEVICE) {
        const Device* d = devAt(x, y);
        if (d && (d->type == DEV_PIPE || d->type == DEV_CROSSOVER)) return false;
    }
    const u8 k = MATS[c.mat].kind;
    if (k != KIND_STATIC && k != KIND_POWDER) return false;
    /* Materials you walk through -- the torch, so far. Per-material and
       permanent; see g_matPassable in materials.h. */
    if (g_matPassable[c.mat]) return false;
    /* Powder in free fall is not ground. A stream of dirt pouring past you is
       not a wall, and it used to be exactly that: measured, walking through one
       covered 96 cells of a 200-cell crossing and spent 159 frames of 400 making
       no progress at all. See cellFalling() in world.h -- the matching half of
       this rule lives in tryMove(). */
    return !cellFalling(c);
}

/* Is the collision shape blocked with its bounding box's top-left at (bx, by)?
   Uses the same tapered outline the world is told about, so the player fits
   through exactly the gaps their silhouette suggests -- the pointed shoulders
   let them under an overhang that a full-width head would catch on. */
static bool boxBlocked(const World& w, int bx, int by, int mode = SOLID_ANY) {
    for (int yy = 0; yy < PLAYER_H; ++yy) {
        const int inset = playerRowInset(yy);
        for (int xx = inset; xx < PLAYER_W - inset; ++xx)
            if (playerSolid(w, bx + xx, by + yy, mode)) return true;
    }
    return false;
}

bool playerOnClimb(const World& w, const Player& p) {
    const int y0 = imax(0, p.top()),  y1 = imin(SIM_H - 1, p.bottom());
    const int x0 = imax(0, p.left()), x1 = imin(SIM_W - 1, p.right());
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (g_matClimb[w.cells[y * SIM_W + x].mat]) return true;
    return false;
}

void Player::reset(float cx, float cy) {
    x = cx - PLAYER_W * 0.5f;
    y = cy - PLAYER_H * 0.5f;
    vx = vy = 0.0f;
    onGround = false;
    buried   = false;
    alive    = true;
    facing    = 1;
    walkPhase = 0.0f;
    frame     = PF_IDLE;
    /* Equipment is the host's to publish; a fresh player has none, which is
       what every headless harness wants and what respawning should do to a
       half-empty tank. */
    fly.thrust = fly.riseCap = fly.refuel = 0.0f;
    fly.fuel   = 0;
    fuel       = 0.0f;
    thrusting  = false;
    /* Respawning restores health, which is what makes reset() the whole answer
       to dying rather than needing a separate revive path. */
    hp        = PLAYER_HP_MAX;
    hurt      = 0.0f;
    hurtFlash = 0;
    fallFromY = y;
    feltTemp  = (u8)AMBIENT_TEMP;
    resist.heat = resist.cold = 0;
    lastFall  = 0.0f;
    swimming   = false;
    underwater = false;
    climbing   = false;
    breath     = BREATH_MAX;
    speedMul   = 1.0f;
}

void Player::damage(float amount) {
    if (!alive || amount <= 0.0f) return;
    hurt += amount;
    /* Whole points come out of the accumulator and the remainder stays in it,
       so a rate of a tenth of a point a frame is a point every ten frames
       rather than nothing at all. */
    const int whole = (int)hurt;
    if (whole > 0) {
        hurt -= (float)whole;
        hp -= whole;
    }
    /* Flashed on the DAMAGE rather than on the point, so a slow burn still
       shows something every frame it is happening -- but only above a floor.
       Weighting temperature by exposure introduced genuinely negligible rates:
       a single cell of drifting steam against the body is a thousandth of a
       point a frame, which costs a point a minute and would otherwise flash the
       character red continuously. A warning nobody can afford to act on is a
       warning they learn to ignore.

       0.02 is a point a second, which is the slowest thing worth turning round
       for. Anything under it still accumulates and still eventually shows up as
       a number on the HUD; it just does not shout. */
    static const float FLASH_AT = 0.02f;
    if (amount >= FLASH_AT || whole > 0) hurtFlash = 8;
    if (hp <= 0) { hp = 0; alive = false; }
}

/* What the body is standing in, thermally: the extremes for the HUD and the
   SEVERITY past each line for the damage. All of it in one pass, because the
   body is 176 cells and walking it three times to ask three questions about the
   same bytes would be silly.

   The two kinds of answer are here together because they are genuinely
   different questions about the same sample, and the split is the point -- see
   the exposure note in player.h. A warning fires on the worst cell; damage is
   paid on how much of you is in it.

   The lines are passed in rather than read from the constants, because
   equipment moves them.

   Reads the grid directly rather than through playerSolid: temperature sets the
   hazard and thermal conductivity weights hot contact; solidity is irrelevant. */
/* How much a given fraction of the body being in it counts for. SQUARED, and
   that is what makes the ladder in player.h come out: linear in the fraction,
   a stray cell of steam still costs a point a second, because heating four
   cells of a hundred and seventy-six is a fortieth of full contact and a
   fortieth of a furnace is not nothing. Squared it is a sixteen-hundredth,
   which is nothing, while a tenth of the body against a lava pool is still a
   hundredth of full and dies in a comfortable ten seconds.

   It is not merely a curve that fits. A small warm patch on a body is being
   carried away by the rest of the body, and the bigger the patch the less of
   it there is to carry -- so the cost really is faster than linear in contact
   area. */
static float exposure(float frac) {
    const float f = frac / CONTACT_FULL;
    return f >= 1.0f ? 1.0f : f * f;
}

/* Heat needs a stronger early ramp than cold: fire and steam are visually loud,
   and a BURNING warning that costs a fraction of a point per second feels like
   a broken warning.  At full contact this is exactly 1.0, preserving the old
   maximum; at 10% of full contact it is about five times the old square. */
static float heatExposure(float frac) {
    const float f = frac / CONTACT_FULL;
    if (f >= 1.0f) return 1.0f;
    static const float EARLY = 0.45f;
    return f * (EARLY + (1.0f - EARLY) * f);
}

/* Body heat is not only about temperature and area: a boot on hot copper takes
   heat much faster than the same patch of hot air. Keep air at the old weight,
   then let a material's existing thermal-conductivity value contribute up to a
   threefold contact weight for metals. This affects uptake only; it does not
   change material-to-material heat simulation or the damage ceiling. */
static float heatCoupling(u8 mat) {
    if (mat == MAT_EMPTY) return 1.0f;
    return 1.0f + 2.0f * (float)MATS[mat].heatCond / 255.0f;
}

/* The collision box stops immediately ABOVE a floor cell, so the body scan
   cannot see the thing the player is standing on. A sole has a small but direct
   contact area; count that row at double weight, then let copper's material
   coupling make standing on hot metal a serious hazard. */
static const float FOOT_HEAT_CONTACT = 2.0f;

/* Temperatures above 100 C are a different class of hazard from an overheated
   workshop. Add a convex tail only there: lava and fire become decisively
   lethal, while the early heat ramp keeps the tuning established above. */
static float heatSeverity(int degreesOver) {
    static const int SCALDING = 55;  /* 45 C line + 55 = 100 C */
    if (degreesOver <= SCALDING) return (float)degreesOver;
    const float excess = (float)(degreesOver - SCALDING);
    return (float)degreesOver + excess * excess / 72.0f;
}

struct BodyTemp {
    u8    hottest, coldest;
    float hotSeverity, coldSeverity;   /* degrees past each line, after exposure */
};
static void bodyTemp(const World& w, const Player& p, u8 heatLine, u8 coldLine,
                     BodyTemp* out) {
    u8 hi = 0, lo = 255;
    float hotContact = 0.0f;
    int nUnder = 0;
    const int y0 = imax(0, p.top()),  y1 = imin(SIM_H - 1, p.bottom());
    const int x0 = imax(0, p.left()), x1 = imin(SIM_W - 1, p.right());
    for (int y = y0; y <= y1; ++y) {
        const u8* row = w.temp + y * SIM_W;
        for (int x = x0; x <= x1; ++x) {
            const u8 t = row[x];
            if (t > hi) hi = t;
            if (t < lo) lo = t;
            if (t > heatLine) hotContact += heatCoupling(w.at(x, y).mat);
            if (t < coldLine) ++nUnder;
        }
    }
    const int footY = p.bottom() + 1;
    const int footInset = playerRowInset(PLAYER_H - 1);
    const int footX0 = imax(0, p.left() + footInset);
    const int footX1 = imin(SIM_W - 1, p.right() - footInset);
    if (footY >= 0 && footY < SIM_H) {
        const u8* row = w.temp + footY * SIM_W;
        for (int x = footX0; x <= footX1; ++x) {
            if (!playerSolid(w, x, footY)) continue;
            const u8 t = row[x];
            if (t > hi) hi = t;
            if (t > heatLine) hotContact += FOOT_HEAT_CONTACT * heatCoupling(w.at(x, footY).mat);
        }
    }
    const float cells = (float)imax(1, (y1 - y0 + 1) * (x1 - x0 + 1));
    /* Counted against the WHOLE body, not against the cells that were over the
       line. Dividing by the affected cells would give back the maximum rule
       with extra steps: one hot cell would be a body entirely in contact. */
    out->hottest = hi; out->coldest = lo;
    out->hotSeverity  = (hi > heatLine)
        ? heatSeverity((int)hi - (int)heatLine) * heatExposure(hotContact / cells) : 0.0f;
    out->coldSeverity = (lo < coldLine)
        ? (float)((int)coldLine - (int)lo) * exposure((float)nUnder / cells) : 0.0f;
}

/* How much of the body is in liquid, and whether the head is. One pass for both
   -- see the note on WATER_LINE for why they are separate questions.

   By KIND rather than by MAT_WATER, so molten metal and fuel float you and
   drown you exactly as water does. That is the right answer and it is free:
   a liquid is a liquid to a body in it, and singling water out would mean a
   character who can tread lava but not a puddle. What it is NOT is a claim that
   they are equally survivable -- the temperature damage already has plenty to
   say about a lava swim, and it says it far faster than the breath counter. */
static void bodyInLiquid(const World& w, const Player& p, float* frac, bool* headUnder) {
    const int y0 = imax(0, p.top()),  y1 = imin(SIM_H - 1, p.bottom());
    const int x0 = imax(0, p.left()), x1 = imin(SIM_W - 1, p.right());
    if (y1 < y0 || x1 < x0) { *frac = 0.0f; *headUnder = false; return; }

    int wet = 0, total = 0, headWet = 0, headTotal = 0;
    for (int y = y0; y <= y1; ++y) {
        const bool isHead = (y - p.top()) < BREATH_HEAD;
        for (int x = x0; x <= x1; ++x) {
            const bool liq = MATS[w.cells[y * SIM_W + x].mat].kind == KIND_LIQUID;
            ++total; wet += liq ? 1 : 0;
            if (isHead) { ++headTotal; headWet += liq ? 1 : 0; }
        }
    }
    *frac = total ? (float)wet / (float)total : 0.0f;
    /* MOST of the head, not any of it: a single stray cell of spray across the
       helmet is not being underwater, and at the surface of a choppy pool there
       is nearly always one. */
    *headUnder = headTotal && headWet * 2 > headTotal;
}

/* Picks the frame from what the body is actually doing, rather than from what
   was pressed. Falling because the ground was mined out from under you has to
   look the same as falling because you jumped off it. */
void Player::animate() {
    if (vx > 0.05f)       facing = 1;
    else if (vx < -0.05f) facing = -1;

    const float speed = (vx > 0.0f) ? vx : -vx;

    if (!onGround) {
        frame = (vy < 0.0f) ? PF_JUMP : PF_FALL;
        return;
    }
    if (speed <= 0.05f) {
        /* Standing still still breathes -- two frames, a second apart. A figure
           frozen solid while every grain around it simulates reads as a paused
           game. walkPhase carries the timer, since it is idle anyway. */
        walkPhase += 1.0f;
        if (walkPhase >= 2.0f * IDLE_HOLD) walkPhase = 0.0f;
        frame = (walkPhase < IDLE_HOLD) ? PF_IDLE : PF_IDLE2;
        return;
    }

    /* One full cycle -- two steps -- per STRIDE_CELLS * PF_WALK_FRAMES
       travelled, so the feet are tied to the ground by construction rather than
       by a timer: accelerating from a standstill shows a slow gait, not a
       full-speed one played early.
    
       The stride LENGTH scales with the body, which it has to: the same 3.4
       cells that read as a walk on a 22-cell figure is a shuffle on a 30-cell
       one, because the legs are half again as long and cover that distance in
       half the swing. */
    const float STRIDE_CELLS = 3.4f * (float)PLAYER_H / 22.0f;
    const float cycle = STRIDE_CELLS * (float)PF_WALK_FRAMES;
    walkPhase += speed;
    while (walkPhase >= cycle) walkPhase -= cycle;
    const int step = (int)(walkPhase / STRIDE_CELLS);
    frame = PF_WALK0 + (step < PF_WALK_FRAMES ? step : PF_WALK_FRAMES - 1);
}

void Player::update(const World& w, const PlayerInput& in) {
    if (!alive) return;
    if (hurtFlash > 0) --hurtFlash;
    lastFall = 0.0f;

    /* --- heat and cold ------------------------------------------------
       Before movement, so what hurts you is where you were standing when the
       frame began rather than where this frame's step happens to end -- which
       matters at the edge of a lava pool, where a single step can cross the
       line and it should be the step you SEE that costs you.

       Both ends are sampled every frame even though only one can be over the
       line at once, because a body spanning 22 cells can genuinely have its
       feet in something hot and its head in something cold, and taking the
       worse of the two is more honest than picking one to look at. */
    {
        BodyTemp bt;
        const u8 hl = heatLine(), cl = coldLine();
        bodyTemp(w, *this, hl, cl, &bt);
        /* feltTemp reports whichever end is worse, so the HUD has one number
           to show and it is the one that is about to matter. Chosen on the
           EXTREMES, unweighted, because a warning that waited for exposure
           would arrive after the thing it was warning about. */
        const int over  = (int)bt.hottest - (int)hl;
        const int under = (int)cl - (int)bt.coldest;
        feltTemp = (over >= under) ? bt.hottest : bt.coldest;
        if (bt.hotSeverity  > 0.0f) damage(bt.hotSeverity * HEAT_DAMAGE);
        if (bt.coldSeverity > 0.0f) damage(bt.coldSeverity * COLD_DAMAGE);
        if (!alive) return;
    }

    /* --- water, and breathing -----------------------------------------
       Sampled before movement for the same reason temperature is: what happens
       to you this frame should be decided by the water you can see yourself
       standing in, not by wherever the step lands. */
    {
        float wet = 0.0f;
        bodyInLiquid(w, *this, &wet, &underwater);
        swimming = wet >= WATER_LINE;

        if (underwater) {
            if (breath > 0) --breath;
            else            damage(DROWN_DAMAGE);
        } else {
            breath += BREATH_REFILL;
            if (breath > BREATH_MAX) breath = BREATH_MAX;
        }
        if (!alive) return;
    }

    /* --- unstick ------------------------------------------------------
       Terrain closing over the player is not an edge case here, it is Tuesday:
       sand falls, lava flows, a wall is drawn straight onto you. So before
       anything else, if the box is inside solid material, lift it out.

       Upward is the right direction to search because almost everything that
       buries you arrives from above or fills in from the side, and the surface
       is nearly always the nearest free space. Giving up after PLAYER_H + 4
       rather than searching further is deliberate -- past that the player is
       genuinely entombed, and teleporting them somewhere distant would be far
       more confusing than telling them they are stuck. */
    buried = false;
    if (boxBlocked(w, left(), top())) {
        bool freed = false;
        for (int lift = 1; lift <= PLAYER_H + 4; ++lift) {
            if (!boxBlocked(w, left(), top() - lift)) {
                y -= (float)lift;
                vy = 0.0f;
                freed = true;
                break;
            }
        }
        if (!freed) { buried = true; vx = vy = 0.0f; return; }
    }

    /* --- horizontal ----------------------------------------------------
       Acceleration is scaled with the top speed, not just the cap. Raising the
       cap alone means the same shove getting you to a higher number eventually,
       which reads as the boots making the character HEAVIER for the first
       second -- the opposite of what they are for. */
    const float want = (in.right ? 1.0f : 0.0f) - (in.left ? 1.0f : 0.0f);
    const float topSpeed = MAX_SPEED * speedMul;
    if (want != 0.0f) {
        vx += want * MOVE_ACCEL * speedMul;
        if (vx >  topSpeed) vx =  topSpeed;
        if (vx < -topSpeed) vx = -topSpeed;
    } else {
        vx *= onGround ? GROUND_DRAG : AIR_DRAG;
        if (vx > -0.02f && vx < 0.02f) vx = 0.0f;
    }

    /* --- vertical ------------------------------------------------------ */
    climbing = playerOnClimb(w, *this);
    if (climbing && !in.jump) {
        /* Held, not accelerated: a rope has no inertia. See CLIMB_SPEED.
           Gravity is not applied at all rather than being cancelled, so hanging
           still is genuinely still and does not creep.

           `!in.jump` above is what leaves a way OFF: the jump key falls through
           to the ordinary branch below, where the rope counts as ground for
           the purpose of pushing off it. Without that the only exit from a
           rope is to walk sideways with no speed, straight back down the shaft
           you climbed. */
        vy = 0.0f;
        if (in.down) vy =  CLIMB_SPEED;
        fallFromY = y;      /* a rope cancels a fall, like water does */
    } else if (climbing && in.jump) {
        vy = -JUMP_VEL;
        onGround = false;
    } else if (swimming) {
        /* No ground needed, and repeatable every frame -- which is what makes
           this a swim rather than a jump you get one of. The stroke is added
           to velocity and capped, rather than assigned like JUMP_VEL, so
           holding the key climbs steadily instead of pinning you to one speed
           the moment you touch it. */
        if (in.jump) {
            vy -= SWIM_STROKE;
            if (vy < -SWIM_RISE) vy = -SWIM_RISE;
        }
        vy += GRAVITY * WATER_GRAVITY;
        vx *= WATER_DRAG;
        vy *= WATER_DRAG;
        if (vy >  WATER_MAX_FALL) vy =  WATER_MAX_FALL;

        /* Water breaks a fall. The reference has to be dragged along or the
           drop that got you here is still on the books, and hitting the bottom
           of a deep pool would cost you the whole descent -- which is the one
           thing everybody expects diving into water NOT to do. */
        fallFromY = y;
    } else {
        if (in.jump && onGround) { vy = -JUMP_VEL; onGround = false; }
        vy += GRAVITY;
    }

    /* --- thrust --------------------------------------------------------
       Applied AFTER gravity, so `riseCap` is a true rate of climb rather than
       a rate that gravity then eats into -- otherwise the number you tune is
       not the number you get, and it drifts with GRAVITY.

       The key is the jump key, held. That means a jump runs straight into
       thrust with no second press, which is the whole feel of rocket boots:
       you leave the ground and keep going. It also means thrust only ever
       happens off the ground, since jumping consumed the ground contact
       above.

       The guard is what keeps it from braking. Rising out of a jump at 2.6
       cells/frame is far faster than any riseCap here, and clamping to the cap
       would stop the jump dead the instant you kept the key down -- a jetpack
       that makes you jump LOWER. So the cap only applies to a rise it would
       speed up.

       Fuel is spent only when the thrust ACTUALLY DID SOMETHING, which is
       inside the guard rather than outside it. Charging for every frame the key
       is held sounds equivalent and is not: a jump rises faster than any
       riseCap for its first fourteen frames, so more than half of a boot's tank
       was going on frames where the guard had already declined to apply any
       thrust. Measured, that cost the boots almost all their point -- 25 cells
       of height against a plain jump's 17, where spending the same fuel where
       it works gives 31. You should not pay for a boost you did not get. */
    /* Not underwater. Rocket boots that worked submerged would make the stroke
       pointless and would also be the only thing in the game that burns without
       air -- and more practically, thrust and the swim stroke share the jump
       key, so both firing at once is one press doing two jobs. */
    thrusting = false;
    if (!swimming && in.jump && !onGround && fly.any() && fuel > 0.0f && vy > -fly.riseCap) {
        vy -= fly.thrust;
        if (vy < -fly.riseCap) vy = -fly.riseCap;
        fuel -= 1.0f;
        if (fuel < 0.0f) fuel = 0.0f;
        thrusting = true;
    }

    /* The air cap. Water has its own, applied above and far lower, so this must
       not undo it -- taking the smaller of the two is the honest way to say
       "whichever medium you are in decides". */
    if (vy > MAX_FALL) vy = MAX_FALL;
    if (swimming && vy > WATER_MAX_FALL) vy = WATER_MAX_FALL;

    /* --- move ----------------------------------------------------------
       Walk the box a whole cell at a time toward the destination, and only if
       nothing was hit adopt the exact fractional target. Two things have to be
       true at once and they pull in opposite directions:

       Every intervening cell must be tested, or the box tunnels. At terminal
       velocity it covers 6 cells a frame and most of this world is one cell
       thick, so a sweep that only checks the endpoints would drop the player
       through the floor regularly.

       The fraction must survive a clean move, or sub-pixel position is a lie.
       An earlier version stepped in whole cells and discarded the remainder,
       which silently quantised every speed to whole cells per frame: MAX_SPEED
       1.2 actually moved 1.0, acceleration was invisible, and no walk slower
       than 60 cells/second was expressible at all. The trace read x=178.00,
       179.00, 180.00 on consecutive frames, which is what gave it away. */

    /* horizontal, with step-up */
    {
        const float target = x + vx;
        const int   end    = (int)target;
        const int   dir    = (vx > 0.0f) ? 1 : -1;
        int  cur     = (int)x;
        bool blocked = false;
        while (cur != end) {
            const int next = cur + dir;
            if (boxBlocked(w, next, top())) {
                /* Try to walk up over it before giving up -- but only from the
                   ground, or the player can climb a sheer wall in mid-air. */
                bool stepped = false;
                if (onGround) {
                    for (int up = 1; up <= PLAYER_STEP_UP; ++up) {
                        if (!boxBlocked(w, next, top() - up)) {
                            y -= (float)up;
                            stepped = true;
                            break;
                        }
                    }
                }
                if (!stepped) { blocked = true; break; }
            }
            cur = next;
        }
        if (blocked) { x = (float)cur; vx = 0.0f; }
        else         { x = target; }
    }

    /* Captured before the vertical step clears it -- see the impact note below,
       which is the only reader. */
    const bool wasOnGround = onGround;

    /* vertical */
    {
        /* Where this descent began. While grounded or rising the reference
           follows the body, so it freezes at the apex the moment the fall
           starts -- which means it measures the drop rather than the arc, and a
           jump off a cliff costs only the part below the ledge. */
        if (onGround || vy <= 0.0f) fallFromY = y;

        const float target = y + vy;
        const int   end    = (int)target;
        const int   dir    = (vy > 0.0f) ? 1 : -1;

        /* --- when a platform counts ------------------------------------
           Only on the way DOWN, only when down is not held, and only if the
           body is not already inside one.

           That last condition is the one that is easy to miss and impossible to
           live without. Having jumped up through a platform, the box overlaps
           it for a few frames on the way back down; without the check the very
           next downward step is blocked, and the player is shoved back up onto
           a platform they were trying to pass -- so a platform you can jump
           through becomes one you can never come back down through. Asking
           "was I already in it before I moved" separates landing ON one from
           being inside one. */
        const bool insidePlatform = boxBlocked(w, left(), (int)y, SOLID_FLOOR)
                                 && !boxBlocked(w, left(), (int)y, SOLID_ANY);
        const int  mode = (dir > 0 && !in.down && !insidePlatform)
                        ? SOLID_FLOOR : SOLID_ANY;

        int  cur     = (int)y;
        bool blocked = false;
        while (cur != end) {
            const int next = cur + dir;
            if (boxBlocked(w, left(), next, mode)) { blocked = true; break; }
            cur = next;
        }
        onGround = false;
        if (blocked) {
            y  = (float)cur;
            vy = 0.0f;
            if (dir > 0) onGround = true;   /* landed */
        } else {
            y = target;
        }
    }

    /* Standing on something is a separate question from having just landed:
       walking off a ledge has to clear onGround even though nothing was hit.

       SOLID_FLOOR, so a platform is somewhere you can stand -- and NOT while
       down is held, or holding down on a platform would leave you permanently
       "on the ground" and unable to fall through it. */
    if (!onGround && vy >= 0.0f)
        onGround = boxBlocked(w, left(), top() + 1, in.down ? SOLID_ANY : SOLID_FLOOR);

    /* --- the impact ---------------------------------------------------
       Fall damage is applied HERE, on the transition into onGround, and not in
       the blocked branch above where it started. The two are not the same
       event, and the difference was invisible until it was measured: a descent
       that ends without the box crossing a cell boundary never enters the
       blocked branch at all -- `while (cur != end)` does not run when they are
       already equal -- and is caught by the standing check instead.

       Dropped from a range of heights, that lost the impact entirely at 100
       cells and again at 160, while 120, 140, 180 and 200 all worked. A fall
       damage system with holes in it at particular heights is worse than none:
       it teaches you the drop is survivable and then kills you the next time
       from the same ledge.

       Keyed on the transition, both paths are one event and there is nothing
       to keep in step. */
    if (onGround && !wasOnGround) {
        /* Measured from the apex. Anything inside FALL_SAFE costs nothing at
           all rather than a token amount -- a fall you can walk away from
           should not chip at you, or the number on the HUD stops meaning "I am
           in trouble". */
        lastFall = y - fallFromY;
        if (lastFall > FALL_SAFE)
            damage((lastFall - FALL_SAFE) * FALL_DAMAGE);
        if (!alive) return;
    }

    /* Resting on the floor, gravity would otherwise accumulate all the way to
       terminal velocity inside a single cell before the box finally crossed a
       boundary and snapped back. Invisible, since rendering truncates to whole
       cells, but it means a player who steps off a ledge starts already falling
       at speed. Zeroing it keeps standing still genuinely still. */
    if (onGround && vy > 0.0f) vy = 0.0f;

    /* Refuel on the ground only, and clamp to whatever is equipped NOW -- swap
       a Mk III for boots and the tank has to shrink with it, or the boots
       inherit a jetpack's range. */
    if (onGround) {
        fuel += fly.refuel;
        if (fuel > (float)fly.fuel) fuel = (float)fly.fuel;
    }
    if (fuel > (float)fly.fuel) fuel = (float)fly.fuel;

    animate();
}

void Player::occupy(World& w) const {
    if (!alive) { w.clearBlockBox(); return; }

    /* Remember where the body was last frame so the cells it has left can be
       woken. A settled pile does not re-examine itself -- that is the whole
       point of the chunk system -- so material that was resting against the
       player would hang in the air once the player moved out from under it. */
    static int lastX0 = 0, lastY0 = 0, lastX1 = -1, lastY1 = -1;

    const int x0 = left(), y0 = top(), x1 = right(), y1 = bottom();
    w.setBlockBox(x0, y0, x1, y1, PLAYER_TAPER);

    if (lastX1 >= lastX0 && (lastX0 != x0 || lastY0 != y0)) {
        w.dirtyArea(imin(lastX0, x0), imin(lastY0, y0),
                    imax(lastX1, x1), imax(lastY1, y1));
    }
    lastX0 = x0; lastY0 = y0; lastX1 = x1; lastY1 = y1;
}

void Player::draw(u32* px, int camX, int camY, bool lit) const {
    if (!alive) return;

    const u32* spr = g_playerSpr[(frame >= 0 && frame < PF_COUNT) ? frame : PF_IDLE];
    /* World cell -> view cell. Everything below works in view space. */
    const int bx = left() - camX, by = top() - camY;

    for (int yy = 0; yy < PSPR_H; ++yy) {
        const int wy = by + yy;
        if (wy < 0 || wy >= VIEW_CELLS_H) continue;
        for (int xx = 0; xx < PSPR_W; ++xx) {
            /* Mirrored by reading the source row backwards, rather than by
               keeping a second set of frames. Free, and the two directions can
               never disagree. */
            const u32 c = spr[yy * PSPR_W + (facing < 0 ? PSPR_W - 1 - xx : xx)];
            if (c == 0) continue;
            const int wx = bx + xx;
            if (wx < 0 || wx >= VIEW_CELLS_W) continue;
            /* Shaded per pixel rather than by one sample at the figure's
               centre. It costs the same lookup either way, and it is the
               difference between walking out of a cave mouth lighting you from
               the feet up and the whole sprite stepping through a brightness
               threshold at once. */
            u32 out = lit ? shadeColor(c, viewShade(wx, wy)) : c;
            /* The damage flash, applied AFTER shading so it reads in an unlit
               cave -- which is where most of the damage in this game is going
               to happen. Blended rather than a flat overwrite so the silhouette
               stays legible: a solid red figure loses the limbs, and the whole
               point of the flash is that you can see what you were doing. */
            if (hurtFlash > 0) out = lerpColor(out, 0xE03428, 150);
            px[wy * VIEW_CELLS_W + wx] = out;
        }
    }

    /* --- the exhaust ---------------------------------------------------
       Drawn rather than made part of the sprite, because it has to flicker and
       because it hangs BELOW the collision box -- a plume inside the box would
       have to be part of the hitbox, and then you could stand on your own
       flame.

       The flicker comes from the fuel counter, not from the rng. Rendering must
       not draw from the global stream: the simulation shares it, so a plume
       that consumed random numbers would make the whole world evolve
       differently depending on whether a jetpack happened to be firing. That is
       exactly the class of bug that made a plasma test report a regression in a
       material nobody had touched. */
    if (thrusting) {
        const u32 CORE = 0xFFC24A, EDGE = 0xE05A20;
        const int phase = (int)(fuel * 3.0f) & 3;
        /* Two jets, under the heels, where the nozzles are on every sprite. */
        const int jetX[2] = { 1, PLAYER_W - 3 };
        for (int j = 0; j < 2; ++j) {
            const int len = 3 + ((phase + j) & 1) + (fly.riseCap > 1.0f ? 2 : 0);
            for (int t = 0; t < len; ++t) {
                const int wy = by + PLAYER_H + t;
                if (wy < 0 || wy >= VIEW_CELLS_H) continue;
                /* Tapers to one cell: a plume of constant width reads as a
                   rectangle hanging off the boots. */
                const int wide = (t < len / 2) ? 2 : 1;
                for (int k = 0; k < wide; ++k) {
                    const int wx = bx + jetX[j] + k;
                    if (wx < 0 || wx >= VIEW_CELLS_W) continue;
                    const u32 c = (t < len / 2) ? CORE : EDGE;
                    px[wy * VIEW_CELLS_W + wx] = c;   /* a flame lights itself */
                }
            }
        }
    }

    /* A dark cell under the feet where they meet solid ground, so the figure
       does not melt into pale material like sand. Only where there is actually
       something to stand on -- an unconditional line would draw a shadow in
       mid-air while falling. */
    const u32 EDGE = 0x10141C;
    for (int xx = 0; xx < PLAYER_W; ++xx) {
        const int wx = bx + xx, wy = by + PLAYER_H;
        if (wx < 0 || wx >= VIEW_CELLS_W || wy < 0 || wy >= VIEW_CELLS_H) continue;
        /* Solidity is a question about the WORLD, so it is asked in world
           coordinates even though the pixel is written in view coordinates. */
        if (playerSolid(g_world, wx + camX, wy + camY))
            px[wy * VIEW_CELLS_W + wx] = lit ? shadeColor(EDGE, viewShade(wx, wy)) : EDGE;
    }
}
