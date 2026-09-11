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
   accessoryBurstBolts  -- Culverin Loader: extra bolts the next pull adds */
int  accessoryMomentumPct(int playerSlot, const Inventory& inv);
int  accessorySprintPct(int playerSlot, const Inventory& inv);

/* --- the Loader, which is a ramp now ----------------------------------------
   It was a switch: two full seconds of held fire bought three bolts instead of
   one, and nothing at all below that. Reported as "very inconsistent", and
   measured, which is the useful half -- at every rate a person actually fights
   at, from five shots a second down to one every 1.5 seconds, it fired the
   volley on 0% of pulls. It only ever worked if you were firing slower than
   once every two seconds, which is not a fight, it is target practice.

   So it pays out in stages, and the first stage is inside the rhythm of a real
   exchange: a beat's pause is worth one extra bolt, a longer one is worth two.
   That turns it from a mode you are almost never in into a thing you can play
   toward -- fire, step, fire -- which is what the tooltip claimed all along.

   Returns 0, 1 or 2. The firing site loops that many times, so there is one
   number here and no second copy of the thresholds anywhere. */
int  accessoryBurstBolts(int playerSlot, const Inventory& inv);
/* Kept for the reticle and for anything that only wants the yes/no. */
bool accessoryBurstReady(int playerSlot, const Inventory& inv);
void accessoryNoteShot(int playerSlot);

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

/* Cells a loose drop is collected from, and the radius it starts being drawn in
   from. The bare figure plus whatever is worn -- see ITEM_SLIME_MAGNET. */
float accessoryPickupRadius(const Inventory& inv);
static const float PICKUP_BASE_RADIUS = 20.0f;

/* Registers the wearer's own light with the light pass, for everyone connected.
   Called beside droneRegisterLights and for the same reason: a light that is
   not in the grid has to announce itself every frame. */
void accessoryRegisterLights();
