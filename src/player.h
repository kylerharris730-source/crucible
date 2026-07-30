#pragma once
#include "world.h"
#include "sprite.h"

/* The player is an ENTITY, not a cell. It has no id in MatId, never occupies a
   slot in the grid, and the simulation neither knows nor cares that it exists --
   it only ever reads the grid to ask "is this cell solid?".

   That is deliberate and it sidesteps an entire class of problem. If the player
   lived in the grid it would need a density, a movement rule, an answer for what
   happens when a liquid tries to displace it, and a story for the frame stamp.
   Worse, the falling-sand rules would be free to move it -- sand landing on your
   head would swap you downward. As an overlay none of that can arise, and the
   cost is a handful of grid reads per frame. */

static const int PLAYER_W = 8;   /* cells; 16 screen pixels at SCALE 2 */
static const int PLAYER_H = 22;  /* 44 screen pixels. Grown from 6x16 to leave
                                    room for a figure with readable parts -- a
                                    helmet, a pack, a belt, two legs. At 6x16
                                    the legs were two cells each and a walk
                                    cycle had nothing to move.

                                    Stockier than the four-heads-tall real
                                    proportion on purpose: the head has to be
                                    big enough to hold a visor, and a heavier
                                    build reads better against terrain that is
                                    itself one cell per grain. */

/* How high a ledge you walk up without jumping. This number matters more for
   feel than any other here: terrain in a falling-sand world is never flat, and
   without it you catch on single-pixel bumps constantly and the character reads
   as broken rather than as heavy.

   It is a quarter of body height, which is about what a person manages without
   using their hands, and it wants to stay proportional -- when the character
   doubled in height this went 3 -> 4 for exactly that reason. Absolute step
   height held constant against a taller body reads as tripping over things.
   3 -> 4 when the character doubled in height, 4 -> 5 when it grew again. */
static const int PLAYER_STEP_UP = 5;

/* --- the pointed head -------------------------------------------------
   The collision shape is not a rectangle. The top PLAYER_TAPER rows are inset
   by one cell per side per row, giving 45-degree shoulders:

       row 0    ..XX..     <- 2 wide
       row 1    .XXXX.
       row 2+   XXXXXX     <- full width

   What this is FOR has changed, and the history matters because the numbers
   below were tuned for the old job.

   Originally it was about shedding. A flat top collects things: sand landing on
   a horizontal surface has nowhere to slide to, so it sits there and keeps
   stacking -- measured, a flat-topped head accumulated 24 cells of sand directly
   on it, where a 45-degree slope leaves the diagonal below-and-outward free and
   powders take diagonals readily (sand's slideDry is 235 of 255). One cell of
   inset per row is exactly the slope a falling-sand powder rule can see; a
   shallower taper would be geometry the simulation cannot act on, since a powder
   only ever considers the three cells directly beneath it.

   That job is now done by something else. Powder in free fall passes straight
   through the player (see cellFalling() in world.h), so nothing lands on the
   head to be shed -- measured over 8 seeds after that change, a flat top kept
   0.0 cells and a pointed one kept 0.0. The taper was SUPERSEDED here, not
   broken, and it has never been what saves you from a deep drift: at depth the
   sand overhead is supported by the sand beside it rather than by you, and every
   taper from 0 to 3 measured identically at 441 cells.

   What still earns it is the COLLISION OUTLINE. boxBlocked() tests this shape,
   not the bounding box, so the player fits through exactly the gaps their
   silhouette suggests -- the pointed shoulders get them under an overhang a
   full-width head would catch on. Settled material still piles against the shape
   too, so a rising heap meets a wedge rather than a shelf.

   Keep the apex rule in mind if the body is ever resized, since it is what the
   value was chosen by: what matters is the width of the apex, which is
   PLAYER_W - 2*PLAYER_TAPER. At 6 wide with taper 2 the peak was 2 cells. Widening
   the body to 8 and leaving the taper alone made the peak 4 cells -- a flat roof
   again -- and 3 puts the apex back to 2. */
static const int PLAYER_TAPER = 3;

/* Cells to inset each side on a given row measured from the top of the box. */
static inline int playerRowInset(int rowFromTop) {
    const int inset = PLAYER_TAPER - rowFromTop;
    return inset > 0 ? inset : 0;
}

/* How far from the character the tool can reach, in cells. Roughly three and a
   half body heights, which is far enough to dig a tunnel comfortably and short
   enough that you have to walk somewhere to work on it.

   This will become a stat on the multitool rather than a constant -- reach is
   an obvious thing for a module to extend -- so anything that reads it should
   be happy taking it as a parameter later. */
static const int PLAYER_REACH = 56;

struct PlayerInput {
    bool left, right, jump;
};

/* --- thrust ----------------------------------------------------------------

   What a piece of worn flight gear does, resolved from equipment by item.cpp
   and published onto the Player by the host each frame -- the same arrangement
   World uses for the player's collision box, and for the same reason: movement
   should not have to know that an inventory exists.

   Three numbers rather than one, because they are the three things a player can
   feel separately. `thrust` is how quickly it takes hold, `riseCap` is how fast
   you end up going, and `fuel` is how long you get. A single "power" number
   would make rocket boots and a jetpack the same item at different volumes.

   riseCap is what makes this controllable rather than a second jump. Thrust
   alone accelerates without limit, so holding the key would fling you off the
   top of the world; capping the climb turns the key into "hold to go up at a
   known rate", which is what you can actually aim with. It never BRAKES a
   faster rise -- see the note in update() -- so thrusting out of a jump does
   not feel like hitting a ceiling. */
struct FlightSpec {
    float thrust;    /* cells/frame^2 added while the key is held */
    float riseCap;   /* fastest climb it will drive you to, cells/frame */
    int   fuel;      /* frames of thrust from full */
    float refuel;    /* frames of fuel restored per frame stood on the ground */

    bool any() const { return fuel > 0 && riseCap > 0.0f; }
};

struct Player {
    /* Top-left of the collision box, in cells, with a fractional part.
       Sub-pixel position is not a luxury: integer-only movement quantises walk
       speed to whole cells per frame, so the slowest possible walk is already
       60 cells/second and there is no room to tune below it.

       These are floats where the simulation is strictly integer, which is a
       deliberate exception rather than drift. The sim is a hot loop over 196k
       cells and its integer discipline pays for itself; this is one entity
       updated once a frame, where being able to read the tuning constants as
       ordinary numbers is worth more than the cycles. */
    float x, y;
    float vx, vy;
    bool  onGround;
    bool  buried;      /* terrain closed over us and we could not be freed */
    bool  alive;

    /* --- animation ----------------------------------------------------
       facing is +1 right, -1 left, and it LATCHES: it only changes when the
       character is actually moving, so releasing a key leaves them looking
       the way they were going rather than snapping to a default.

       walkPhase accumulates distance travelled, not frames elapsed. Driving
       the cycle off a timer makes the feet skate whenever speed changes --
       accelerating from a standstill would show a full-speed gait at walking
       pace. Off distance, the legs are tied to the ground by construction. */
    int   facing;
    float walkPhase;
    int   frame;       /* a PlayerFrame */

    /* --- flight -------------------------------------------------------
       `fly` is what is equipped and is written by the host every frame, so
       swapping a jetpack takes effect immediately and nothing here has to
       know where equipment lives. `fuel` is the state that persists, and it
       is a float because refuel is a rate and rounding a rate to whole
       frames is how you get gear that recharges either instantly or never.

       thrusting is an output, for the exhaust plume. Kept rather than
       recomputed in draw() because draw() has no input to look at, and a
       plume that guessed from vy would fire while you were falling. */
    FlightSpec fly;
    float      fuel;
    bool       thrusting;

    void reset(float cx, float cy);
    void update(const World& w, const PlayerInput& in);
    void animate();          /* called by update(); picks facing and frame */
    /* Draws into the VIEW buffer, so it takes the camera's top-left cell.
       Everything that draws into that buffer now needs it -- see render.h. */
    /* `lit` shades the figure by the light field, which the caller must have
       computed for this camera position -- same contract as renderView(), and
       for the same reason: the light buffer is one global with a fixed geometry,
       so there is nothing to choose between and nothing to get out of step.
       Off by default so headless harnesses keep drawing a visible character. */
    void draw(u32* px, int camX, int camY, bool lit = false) const;

    /* Publish the collision box to the world so material cannot move into it.
       Called once a frame from the host; the world knows nothing about players,
       only that some box is occupied. Also dirties the cells the body just
       vacated -- without that, sand resting on the player's head stays floating
       in mid-air after they walk away, because nothing woke its chunk. */
    void occupy(World& w) const;

    /* Centre of the body, which is what reach is measured from -- measuring
       from the feet or a corner makes the reachable area lopsided in a way you
       can feel without being able to name. */
    float centreX() const { return x + PLAYER_W * 0.5f; }
    float centreY() const { return y + PLAYER_H * 0.5f; }

    /* Cell bounds of the collision box at the current position. */
    int left()   const { return (int)x; }
    int top()    const { return (int)y; }
    int right()  const { return (int)x + PLAYER_W - 1; }
    int bottom() const { return (int)y + PLAYER_H - 1; }
};

extern Player g_player;

/* True if a cell stops the player. Powders count as solid ground -- you walk on
   sand rather than sinking into it. That is a choice, not a physical truth: the
   alternative reads as more realistic and plays much worse, because a pile of
   sand shifting under you while you dig is a constant low-grade frustration
   with no upside. Liquids and gases never block; wading and swimming come
   later. */
bool playerSolid(const World& w, int x, int y);
