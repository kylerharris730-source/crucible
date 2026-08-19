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
