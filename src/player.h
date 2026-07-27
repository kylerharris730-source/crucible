#pragma once
#include "world.h"

/* The player is an ENTITY, not a cell. It has no id in MatId, never occupies a
   slot in the grid, and the simulation neither knows nor cares that it exists --
   it only ever reads the grid to ask "is this cell solid?".

   That is deliberate and it sidesteps an entire class of problem. If the player
   lived in the grid it would need a density, a movement rule, an answer for what
   happens when a liquid tries to displace it, and a story for the frame stamp.
   Worse, the falling-sand rules would be free to move it -- sand landing on your
   head would swap you downward. As an overlay none of that can arise, and the
   cost is a handful of grid reads per frame. */

static const int PLAYER_W = 6;   /* cells; 12 screen pixels at SCALE 2 */
static const int PLAYER_H = 16;  /* 32 screen pixels -- roughly human proportions,
                                    since a person is about four times as tall as
                                    they are wide across the shoulders */

/* How high a ledge you walk up without jumping. This number matters more for
   feel than any other here: terrain in a falling-sand world is never flat, and
   without it you catch on single-pixel bumps constantly and the character reads
   as broken rather than as heavy.

   It is a quarter of body height, which is about what a person manages without
   using their hands, and it wants to stay proportional -- when the character
   doubled in height this went 3 -> 4 for exactly that reason. Absolute step
   height held constant against a taller body reads as tripping over things. */
static const int PLAYER_STEP_UP = 4;

/* --- the pointed head -------------------------------------------------
   The collision shape is not a rectangle. The top PLAYER_TAPER rows are inset
   by one cell per side per row, giving 45-degree shoulders:

       row 0    ..XX..     <- 2 wide
       row 1    .XXXX.
       row 2+   XXXXXX     <- full width

   A flat top collects things. Sand landing on a horizontal surface has nowhere
   to slide to, so it sits there and keeps stacking -- measured, a flat-topped
   head accumulated 24 cells of sand directly on it. On a 45-degree slope the
   diagonal below-and-outward is free, and powders take diagonals readily
   (sand's slideDry is 235 of 255), so it sheds instead of piling.

   One cell of inset per row is exactly the slope a falling-sand powder rule can
   see. A shallower taper would be geometry the simulation cannot act on, since
   a powder only ever considers the three cells directly beneath it. */
static const int PLAYER_TAPER = 2;

/* Cells to inset each side on a given row measured from the top of the box. */
static inline int playerRowInset(int rowFromTop) {
    const int inset = PLAYER_TAPER - rowFromTop;
    return inset > 0 ? inset : 0;
}

struct PlayerInput {
    bool left, right, jump;
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

    void reset(float cx, float cy);
    void update(const World& w, const PlayerInput& in);
    void draw(u32* px) const;

    /* Publish the collision box to the world so material cannot move into it.
       Called once a frame from the host; the world knows nothing about players,
       only that some box is occupied. Also dirties the cells the body just
       vacated -- without that, sand resting on the player's head stays floating
       in mid-air after they walk away, because nothing woke its chunk. */
    void occupy(World& w) const;

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
