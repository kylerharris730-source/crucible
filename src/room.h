#pragma once
#include "world.h"

/* --- rooms -----------------------------------------------------------------

   A room is a space you SEALED and BACKED IN: an enclosed pocket of air whose
   every cell has player-placed background behind it. Rooms are the one thing
   that keeps chunks simulating when the camera has gone somewhere else.

   Both halves of that definition are load bearing, and neither works alone.

     Enclosure alone would qualify every natural air pocket in the world. The
     mountain is full of them and you did not build any of them.

     Placed background alone would qualify a wall you papered the front of a
     cliff with, which is not a room in any sense a player would recognise.

   Together they are almost impossible to satisfy by accident and completely
   obvious to satisfy on purpose -- dig a box, wall the back of it, close the
   door -- which is exactly the shape a mechanic should have. It also means the
   game never has to ask "did you MEAN this to stay loaded"; building the thing
   is the statement of intent.

   --- doors ---
   The seal test asks whether a cell is passable to AIR, not whether it is
   passable to the player. Anything solid seals, so a door built out of any
   static material seals a room while remaining something you can dig through
   and rebuild. When a proper door material arrives -- one the player walks
   through and sand does not -- it will be solid to this test for free, and
   that is the correct answer: a room with the door open is still your room.

   --- the cost, which is the whole question ---
   The player asked whether this could be affordable, and it is, by
   construction rather than by measurement:

     A room is capped at ROOM_MAX_CELLS and at ROOM_REACH cells across. Anything
     larger is not refused as a judgement call, it simply fails the scan -- the
     fill runs out of room and gives up, which costs a bounded amount of work.

     There are at most MAX_ROOMS of them, so the total keep-alive area has a
     hard ceiling no amount of building can pass.

     A room that is not doing anything costs NOTHING, because it is made of
     chunks and a settled chunk is skipped whether or not it is flagged. What
     you pay for is a room with something happening in it, which is the thing
     you built the room to have happen.

   Scans run on demand -- when the world is edited -- and one existing room is
   revalidated per ROOM_RECHECK frames, so nothing here runs per frame per
   room. */

/* How far the fill may travel from its seed, in cells, in each direction. It
   is a reach rather than a fixed box because the seed is wherever the player
   happened to click, not the middle of anything -- a box centred on the seed
   would refuse a perfectly ordinary room simply for being entered near one
   end. */
static const int ROOM_REACH     = 128;
static const int ROOM_MAX_CELLS = 4096;
/* Smaller than this is not a room, it is a gap. Four by four of usable
   interior. Without a floor here, a single air cell left behind a papered wall
   would flag a chunk to tick forever, and you would never find out why. */
static const int ROOM_MIN_CELLS = 16;
static const int MAX_ROOMS      = 64;
/* Frames between revalidations. One room is checked per tick, round-robin, so
   this is the cost of the whole system per frame however many rooms exist. A
   broken room therefore stops being one within MAX_ROOMS * ROOM_RECHECK frames
   at worst -- but breaking a wall is an edit, and edits revalidate on the spot
   (see roomsNotifyEdit), so the slow path only ever catches rooms undone by the
   simulation itself: a flooding, a burn-through, a cave-in. */
static const int ROOM_RECHECK   = 20;

struct Room {
    i32  seedX, seedY;              /* a cell known to be inside it */
    i32  x0, y0, x1, y1;            /* inclusive bounds of the interior */
    i32  cells;
    bool used;
};

extern Room g_rooms[MAX_ROOMS];

int  roomCount();

/* Flood the enclosed space containing (x,y) and report whether it is a room.
   Pure: touches nothing but its own scratch buffers, so it is also the honest
   way to ask the question in a test. Fills `out` when it succeeds. */
bool roomScan(const World& w, int x, int y, Room* out);

/* Index of the room whose interior contains (x,y), or -1. */
int  roomAt(int x, int y);

/* Scan around (x,y) and record a room if there is one. The point does not have
   to be inside it: the four neighbours are tried too, because the cell you just
   placed to SEAL a room is by definition part of its wall and not part of the
   room. Returns the room index, or -1. */
int  roomRegister(World& w, int x, int y);

/* The world was edited at (x,y). Revalidates any room that could have been
   broken by it, then looks for a new one. This is the fast path, and the reason
   sealing a room lights up immediately and breaching one stops mattering
   immediately. */
void roomsNotifyEdit(World& w, int x, int y);

/* Revalidate one room, round-robin. Call once per frame; it does nothing on
   most of them. */
void roomsTick(World& w);

void roomsClear(World& w);
