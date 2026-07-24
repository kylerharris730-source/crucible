#pragma once
#include "materials.h"
#include "world.h"

/* Everything about the tool panel that is not drawing: the palette, the tool
   ids, the window geometry and the action list.

   This lives in its own header because there are two shells (Win32/GDI and
   SDL2) and both need it. Duplicating the table in each was the obvious way to
   start and the wrong place to stop: adding a material is supposed to be one
   line, and a copy in each backend makes it two, with the failure mode being a
   material that quietly exists on one platform and not the other. The drawing
   code stays per-backend -- that part genuinely differs -- but the data does
   not, so it is shared. */

/* The window is a fixed-size left-hand tool panel plus the sim viewport. The
   viewport keeps a clean integer scale so pixels stay crisp and the
   window->cell mapping is a plain divide; the panel is its own strip of the
   window rather than an overlay, so buttons never hide playfield and a stroke
   can never paint underneath them. */
static const int SCALE   = 2;
static const int PANEL_W = 150;
static const int VIEW_W  = SIM_W * SCALE;
static const int VIEW_H  = SIM_H * SCALE;
static const int WIN_W   = PANEL_W + VIEW_W;
static const int WIN_H   = VIEW_H;
static const double FRAME_SECONDS = 1.0 / 60.0;

/* Brush selections past MAT_COUNT are tools that act on temperature rather than
   placing a material. */
static const int TOOL_HEAT = MAT_COUNT;
static const int TOOL_COOL = MAT_COUNT + 1;
static const int HEAT_STEP = 12;   /* degrees per frame under the brush */

/* --- the palette ---------------------------------------------------------
   One row here is one button. Adding a material to the picker is a single line
   (plus its row in materials.cpp); the swatch colour is taken straight from
   the material's own palette, so it just works -- on every backend at once. */
struct BrushDef { int brush; const char* label; };
static const BrushDef BRUSHES[] = {
    { MAT_SAND,  "Sand"  },
    { MAT_WATER, "Water" },
    { MAT_DIRT,  "Dirt"  },
    { MAT_STONE, "Stone" },
    { MAT_WOOD,  "Wood"  },
    { MAT_IRON,  "Iron"  },
    { MAT_LAVA,  "Lava"  },
    { MAT_FIRE,  "Fire"  },
    { MAT_STEAM, "Steam" },
    { MAT_WALL,  "Wall"  },
    { MAT_CLONE, "Clone" },
    { MAT_VOID,  "Void"  },
    { TOOL_HEAT, "Heat"  },
    { TOOL_COOL, "Cool"  },
    { MAT_EMPTY, "Erase" },
};
static const int N_BRUSH = (int)(sizeof(BRUSHES) / sizeof(BRUSHES[0]));

enum ActionId { ACT_OVERWRITE, ACT_VIEW, ACT_PAUSE, ACT_CLEAR, N_ACT };

/* The swatch colour for a palette entry, as 0x00RRGGBB. Materials sample their
   own LUT at a dry, mid-tint index so the button always matches what lands in
   the world; tools and the eraser get synthetic colours. */
static inline u32 brushSwatch(int brush) {
    if (brush == TOOL_HEAT) return 0xE26028;
    if (brush == TOOL_COOL) return 0x5098E2;
    if (brush == MAT_EMPTY) return 0x262830;
    return g_colorLut[(brush << 8) | 0x08];
}
