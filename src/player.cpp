#include "player.h"

Player g_player;

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

bool playerSolid(const World& w, int x, int y) {
    /* Outside the world counts as solid, so the player can never be walked out
       of bounds and no caller has to bounds-check first. */
    if (x < 0 || x >= SIM_W || y < 0 || y >= SIM_H) return true;
    const u8 k = MATS[w.at(x, y).mat].kind;
    return k == KIND_STATIC || k == KIND_POWDER;
}

/* Is the collision shape blocked with its bounding box's top-left at (bx, by)?
   Uses the same tapered outline the world is told about, so the player fits
   through exactly the gaps their silhouette suggests -- the pointed shoulders
   let them under an overhang that a full-width head would catch on. */
static bool boxBlocked(const World& w, int bx, int by) {
    for (int yy = 0; yy < PLAYER_H; ++yy) {
        const int inset = playerRowInset(yy);
        for (int xx = inset; xx < PLAYER_W - inset; ++xx)
            if (playerSolid(w, bx + xx, by + yy)) return true;
    }
    return false;
}

void Player::reset(float cx, float cy) {
    x = cx - PLAYER_W * 0.5f;
    y = cy - PLAYER_H * 0.5f;
    vx = vy = 0.0f;
    onGround = false;
    buried   = false;
    alive    = true;
}

void Player::update(const World& w, const PlayerInput& in) {
    if (!alive) return;

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

    /* --- horizontal ---------------------------------------------------- */
    const float want = (in.right ? 1.0f : 0.0f) - (in.left ? 1.0f : 0.0f);
    if (want != 0.0f) {
        vx += want * MOVE_ACCEL;
        if (vx >  MAX_SPEED) vx =  MAX_SPEED;
        if (vx < -MAX_SPEED) vx = -MAX_SPEED;
    } else {
        vx *= onGround ? GROUND_DRAG : AIR_DRAG;
        if (vx > -0.02f && vx < 0.02f) vx = 0.0f;
    }

    /* --- vertical ------------------------------------------------------ */
    if (in.jump && onGround) { vy = -JUMP_VEL; onGround = false; }
    vy += GRAVITY;
    if (vy > MAX_FALL) vy = MAX_FALL;

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

    /* vertical */
    {
        const float target = y + vy;
        const int   end    = (int)target;
        const int   dir    = (vy > 0.0f) ? 1 : -1;
        int  cur     = (int)y;
        bool blocked = false;
        while (cur != end) {
            const int next = cur + dir;
            if (boxBlocked(w, left(), next)) { blocked = true; break; }
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
       walking off a ledge has to clear onGround even though nothing was hit. */
    if (!onGround && vy >= 0.0f)
        onGround = boxBlocked(w, left(), top() + 1);

    /* Resting on the floor, gravity would otherwise accumulate all the way to
       terminal velocity inside a single cell before the box finally crossed a
       boundary and snapped back. Invisible, since rendering truncates to whole
       cells, but it means a player who steps off a ledge starts already falling
       at speed. Zeroing it keeps standing still genuinely still. */
    if (onGround && vy > 0.0f) vy = 0.0f;
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

void Player::draw(u32* px) const {
    if (!alive) return;

    /* Three bands rather than a flat rectangle. At 4x8 there is no room for
       detail, but a lighter head and darker legs are enough to read as a figure
       and, more usefully, to tell at a glance which way up it is. */
    const u32 HEAD = 0xF2F6FF;
    const u32 BODY = 0x7FA8D8;
    const u32 LEGS = 0x3C5A80;
    const u32 EDGE = 0x10141C;

    /* Drawn to the same tapered outline as the collision shape. If the sprite
       were a plain rectangle the head would visibly overlap material that is
       really resting on the shoulders, and sand would appear to roll off thin
       air -- the shed has to be legible or it just looks like a glitch. */
    const int bx = left(), by = top();
    for (int yy = 0; yy < PLAYER_H; ++yy) {
        const int wy = by + yy;
        if (wy < 0 || wy >= SIM_H) continue;
        const u32 band = (yy < PLAYER_H / 4)     ? HEAD
                       : (yy < PLAYER_H * 3 / 4) ? BODY
                                                 : LEGS;
        const int inset = playerRowInset(yy);
        for (int xx = inset; xx < PLAYER_W - inset; ++xx) {
            const int wx = bx + xx;
            if (wx < 0 || wx >= SIM_W) continue;
            px[wy * SIM_W + wx] = band;
        }
    }

    /* A one-cell dark outline under the feet and along the sides, so the figure
       does not disappear when it stands against pale material like sand. */
    for (int xx = 0; xx < PLAYER_W; ++xx) {
        const int wx = bx + xx, wy = by + PLAYER_H;
        if (wx >= 0 && wx < SIM_W && wy >= 0 && wy < SIM_H && playerSolid(g_world, wx, wy))
            px[wy * SIM_W + wx] = EDGE;
    }
}
