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
