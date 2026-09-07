#pragma once
#include "common.h"
struct Inventory;

/* Cached posed equipment art. Null inventory preserves the original suit. */
const u32* wornArmourFrame(const Inventory* inventory, bool crouch, int frame);
