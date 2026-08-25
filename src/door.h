#pragma once
#include "world.h"

struct Player;

/* --- doors -----------------------------------------------------------------

   A door is not an object. It is any connected patch of door material, and
   opening it converts the whole patch at once.

   That is the entire design, and it is chosen over a placed multi-cell fixture
   for one reason: SIZE. A device is 14x14, and the character is 8 wide and 22
   tall -- a 14-cell opening is a hole they cannot fit through. Doors are the one
   piece of built equipment whose dimensions are dictated by the player's body
   rather than by what looks like a machine, so the player has to be the one who
   decides them. Paint a door two cells wide and twenty-four tall, or eight by
   twenty-four, or a hatch in the floor; whatever you painted is what opens.

   --- why two materials ---
   See MAT_DOOR in materials.h. There is nowhere in a 4-byte Cell to keep an
   open/closed bit, so open and closed are separate ids and the toggle is a
   conversion. The valuable consequence is that everything else in the game
   already knows what to do with them: they are ordinary static solids, so they
   seal rooms, hold back sand, take heat, and break under a tool with no special
   case anywhere.

   --- what opens one ---
   Right-click, and a spark. The second is the point of building it now rather
   than later: a spark already means "a machine did something", so a door on a
   wire is a door a clock or a thermocouple can open, and the parts to do that
   already exist. A door is a spark SINK, exactly like a device -- see the note
   in sparkStep. */

/* How far the fill may travel from the cell you poked, in cells, in each
   direction. A reach rather than a box for the reason ROOM_REACH is one: you
   poke a door wherever you happen to be standing, not in the middle of it.

   97x97 of bitmap is 1.2 KB of scratch, which is what buying O(1) "have I seen
   this cell" costs here. */
static const int DOOR_REACH = 48;

/* And a hard cap on the patch itself, which is the number that actually bounds
   the work. A door larger than this is not refused as a judgement about what a
   door should be -- it simply fails, the same way an over-large room does, and
   for the same reason: the cost of the scan has to have a ceiling that no
   amount of building can raise. */
static const int DOOR_MAX_CELLS = 1024;

static inline bool isDoor(u8 m) { return m == MAT_DOOR || m == MAT_DOOR_OPEN; }

/* Flip the door containing (x, y). Returns how many cells changed, or 0 if
   there is no door there, if it is too big, or if closing it would shut the
   door on somebody.

   The whole patch takes the state OPPOSITE to the cell you poked, rather than
   each cell flipping individually. Two doors painted against each other are one
   door -- and a patch left half-open by some earlier edit normalises the first
   time anyone uses it, instead of staying striped for ever. */
int doorToggle(World& w, int x, int y);

/* Opens a closed door as the player approaches and closes doors it opened once
   the player has cleared the doorway. Enemies never call this: their contact
   with a door remains ordinary solid collision. */
void doorAuto(World& w, const Player& p);

/* Multiplayer runs every player before deciding which automatic doors may
   close.  Splitting the two halves prevents a far-away player from closing a
   doorway that another player is still approaching.  World occupancy is the
   shared source of truth, so doorAutoClose sees local and remote bodies alike. */
void doorAutoOpen(World& w, const Player& p);
void doorAutoClose(World& w);
