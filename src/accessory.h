#pragma once
#include "item.h"

/* Player-side passive effects. These deliberately read equipment only; the
   matching drone chips are resolved in drone.cpp and cannot reach this path. */
static const int ACCESSORY_GARLIC_RADIUS   = 30;
static const int ACCESSORY_GARLIC_DAMAGE   = 1;
static const int ACCESSORY_GARLIC_COOLDOWN = 24;

void accessoryReset();
void accessoryTick(const Player& player, const Inventory& inv);
void accessoryTickFor(int playerSlot, const Player& player, const Inventory& inv);

/* Overload shortens player weapon delay to three quarters. Twin duplicates a
   firing command; callers still spend ammunition once for the command. */
int  accessoryShotDelay(const Inventory& inv, int baseDelay);
bool accessoryTwinShot(const Inventory& inv);

/* --- the charms with a memory ------------------------------------------------
   Three of the ten new charms are about what you have been DOING rather than
   about what you are wearing, so they need a clock. All three read the same
   per-slot state accessoryTickFor maintains, and all three answer in the unit
   their call site wants rather than exposing the counter.

   accessoryMomentumPct -- Threshing Spurs: extra shot damage, 0 while still
   accessorySprintPct   -- Ashhound Collar: extra move speed, 0 from a standstill

   The Culverin Loader used to be the third of these and is no longer a clock
   at all -- see accessoryBurstBolts. */
int  accessoryMomentumPct(int playerSlot, const Inventory& inv);
int  accessorySprintPct(int playerSlot, const Inventory& inv);

/* --- the Loader, which is a chance now ---------------------------------------
   Two earlier versions were about TIMING. A flat two-second gate fired on 0% of
   pulls at every rate anybody fights at; the ramp that replaced it paid for a
   beat's pause between shots, which worked but asked the player to keep an eye
   on a clock in the middle of a fight.

   Asked for instead: "some set percent chance of a doubled shot, lets say
   30%". So it is a coin, it needs no rhythm and no tooltip about one, and the
   thing it rewards is simply shooting.

   Returns 0 or 1 -- an extra bolt on this pull -- and ROLLS as it answers, so
   it must be called exactly once per trigger pull. accessoryLoaderPct is the
   number, for the tooltip and the tests. */
int  accessoryBurstBolts(const Inventory& inv);
int  accessoryLoaderPct();

/* The Cinderling Ash: embers dropped behind a running player. It takes the World
   because it WRITES to it, which is why it is not folded into accessoryTickFor
   -- everything else in that function reads equipment and moves numbers, and a
   tick that also edits terrain would be a very different thing to reason
   about. Called from the same loop. */
struct World;
void accessoryAshTrail(int playerSlot, const Player& player,
                       const Inventory& inv, World& world);

/* The charms that act on a shot, applied at the firing site because that is the
   only place all three are known at once. Kept as functions taking the base
   value rather than as a struct of percentages, so a call site reads as the
   number it is about to use and nothing has to remember to apply them. */
int   accessoryShotDamage(const Inventory& inv, int baseDamage);
float accessoryShotSpeed(const Inventory& inv, float baseSpeed);

/* --- the Slime Gland ---------------------------------------------------------
   Frames of poison a hit from this wearer leaves on a creature, or 0 with no
   gland worn. Asked for as "slime magnet should be a poison damage apply": the
   charm was a pickup magnet, and the slime it comes off leaves acid where it
   walks, so its charm now hands that over rather than being the one charm that
   is purely a convenience.

   Read by the places that resolve a player's damage and passed down to
   entDamageAt / entHitSegment, which know nothing about equipment. */
int  accessoryPoisonFrames(const Inventory& inv);

/* Cells a loose drop is collected from, and the radius it starts being drawn in
   from. The bare figure plus whatever is worn -- see ITEM_SLIME_MAGNET. */
float accessoryPickupRadius(const Inventory& inv);
static const float PICKUP_BASE_RADIUS = 20.0f;

/* Registers the wearer's own light with the light pass, for everyone connected.
   Called beside droneRegisterLights and for the same reason: a light that is
   not in the grid has to announce itself every frame. */
void accessoryRegisterLights();
