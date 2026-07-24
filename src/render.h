#pragma once
#include "world.h"

enum ViewMode {
    VIEW_NORMAL = 0,   /* materials, with hot cells glowing */
    VIEW_MATERIAL,     /* materials only -- no heat shown at all */
    VIEW_HEAT,         /* temperature as false colour, for inspecting heat */
    VIEW_COUNT
};

/* Fills `out` (SIM_W * SIM_H pixels, 0x00RRGGBB) and returns the number of
   non-wall, non-empty cells. */
int renderWorld(const World& w, u32* out, int view);
