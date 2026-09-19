#pragma once
#include "materials.h"

/* ============================================================================
   brushes.h -- the material picker's contents.

   This lived in main.cpp until there were TWO front-ends over the same
   simulation. Powderlike (src/powder/main.cpp) is the second one, and a second
   copy of this table is exactly the kind of hand-maintained duplicate this
   project has already been bitten by twice -- once when build.bat's source list
   drifted from the Makefile's, and once when the test runner's did. A material
   added here reaches every front-end at once, or it reaches one of them and
   quietly does not exist in the other.

   Header-only, and deliberately: N_BRUSH has to be a compile-time constant
   because the swatch-brush arrays are sized from it, and a table defined in a
   .cpp cannot give one. Each translation unit gets its own copy of about
   eighty const rows, which is nothing.
   ========================================================================== */

/* Brush selections past MAT_COUNT are tools that act on temperature rather than
   placing a material. */
static const int TOOL_HEAT = MAT_COUNT;
static const int TOOL_COOL = MAT_COUNT + 1;
static const int TOOL_SPARK = MAT_COUNT + 2;
static const int HEAT_STEP = 12;   /* degrees a frame under the brush */


/* --- the palette ---------------------------------------------------------
   One row here is one button. Adding a material to the picker is a single line
   (plus its row in materials.cpp); the swatch colour is taken straight from
   the material's own palette, so it just works. */
struct BrushDef { int brush; const char* label; };
static const BrushDef BRUSHES[] = {
    { MAT_SAND,  "Sand"  },
    { MAT_WATER, "Water" },
    { MAT_ICE,   "Ice"   },
    { MAT_DIRT,  "Dirt"  },
    { MAT_GRASS, "Grass" },
    { MAT_STONE, "Stone" },
    { MAT_WOOD,  "Wood"  },
    { MAT_BIRCH_WOOD, "Birch Wood" },
    /* Rubber only -- molten rubber, molten iron and molten copper are all
       simulated but unplaceable, for the same reason mercury's vapour and
       frozen forms are: they are states you put a material INTO, not things
       you build with. */
    { MAT_RUBBER,"Rubber"},
    { MAT_IRON,  "Iron"  },
    { MAT_COPPER,"Copper"},
    /* The ores are placeable because placing them is a STEP: you shovel a heap
       into a furnace and heat it. Their products are not -- molten metal, molten
       slag and slag are states you put material into, the same line already drawn
       around molten rubber and mercury vapour above. */
    { MAT_COPPER_ORE,"Cu Ore"},
    { MAT_IRON_ORE,  "Fe Ore"},
    /* Raw and finished forms remain drawable; molten intermediates do not.
       This keeps the sandbox useful for laying out deeper-layer processing
       without filling the picker with states normally produced by heat. */
    { MAT_TIN_ORE,     "Tin Ore" },
    { MAT_TIN,         "Tin"     },
    { MAT_BRONZE,      "Bronze"  },
    { MAT_STEEL,       "Steel"   },
    { MAT_GOLD_ORE,    "Gold Ore"},
    { MAT_GOLD,        "Gold"    },
    { MAT_TITANIUM_ORE,"Ti Ore"  },
    { MAT_TITANIUM,    "Titanium"},
    { MAT_TUNGSTEN_ORE,"W Ore"   },
    { MAT_TUNGSTEN,    "Tungsten"},
    /* The heat ladder. Clay and ceramic sit together because one becomes the
       other; coal and fuel likewise. The burning forms (ember, fuelfire) are NOT
       placeable, on the same line already drawn around molten metal and slag --
       they are states you put a material into, not things you build with. */
    { MAT_CLAY,    "Clay"   },
    { MAT_CERAMIC, "Ceramic"},
    { MAT_GLASS,   "Glass"   },
    { MAT_ALUMINUM_NITRIDE, "AlN" },
    { MAT_REFRACTORY, "Refractory" },
    { MAT_COAL,    "Coal"   },
    { MAT_FUEL,    "Fuel"   },
    { MAT_COKE,    "Coke"   },
    { MAT_COKE_GAS,"Coke Gas" },
    { MAT_GRAPHENE,"Graphene"},
    { MAT_LAVA,  "Lava"  },
    { MAT_FIRE,  "Fire"  },
    { MAT_PLASMA,"Plasma"},
    { MAT_COLDFIRE,"Cold Fire"},
    { MAT_NITROGEN,"Liquid N2"},
    { MAT_ACID,    "Acid"     },
    { MAT_GLOWFLUID,"Glowfluid"},
    { MAT_WAX,     "Wax"      },
    { MAT_WEB,     "Web"      },
    { MAT_INERT_FLUID, "Inert Fluid" },
    /* Mercury only. Its vapour and frozen forms are still fully simulated -- a
       mercury pool boiled past 150 C still gives off vapour, and chilled past
       -30 C still freezes solid -- they are just not PLACEABLE. They are
       "unusual forms" nobody pictures when they picture mercury, the way nobody
       pictures water vapour when they picture water, so they were clutter in a
       palette where every row costs height. (Steam and Ice are placeable
       because they are the everyday forms of hot and cold water, and the tell
       is that both earned their own names rather than being "water gas" and
       "frozen water". "Hg Vapour" and "Frozen Hg" did not.)

       Removing a row here does NOT remove the material: the two are independent,
       which is exactly why this is only a BRUSHES edit. */
    { MAT_MERCURY,"Mercury"},
    { MAT_STEAM, "Steam" },
    { MAT_WALL,  "Wall"  },
    { MAT_CLONE, "Clone" },
    { MAT_VOID,  "Void"  },
    /* Heater/Cooler are placed blocks that hold a temperature forever; the
       Heat/Cool rows below them are brushes that nudge temperature while you
       drag. Similar names, but one is scenery and the other is a tool, so they
       sit next to each other where the difference is easy to see. */
    /* Only the closed door. Open Door is a state you put a door INTO -- the same
       line already drawn around molten metal and burning coal -- and painting a
       permanently-open doorway would be a hole that seals rooms and stops sand,
       which is a strange thing to be able to build by accident. */
    { MAT_DOOR,  "Door"  },
    { MAT_ROPE,  "Rope"  },
    { MAT_PLATFORM,"Platform"},
    { MAT_OAK_SEED,  "Oak Seed" },
    { MAT_BIRCH_SEED,"Birch Seed"},
    /* Seeds and harvested heads are inventory materials in their own right.
       Saplings, stalks, leaves and pods remain growth states, so drawing a
       field starts from the same inputs as planting one in survival. */
    { MAT_WHEAT_SEED, "Wheat Seed" },
    { MAT_WHEAT,      "Wheat"      },
    { MAT_FLAX_SEED,  "Flax Seed"  },
    { MAT_FLAX,       "Flax"       },
    { MAT_COTTON_SEED,"Cotton Seed"},
    { MAT_COTTON,     "Cotton"     },
    /* World/debug materials and the new plumbing meshes. All are real cells,
       so the player-off sandbox should be able to paint them directly. */
    { MAT_STRATUM,  "Stratum"  },
    { MAT_SPRING,   "Spring"   },
    { MAT_CHITIN,   "Chitin"   },
    { MAT_SIEVE,    "Sieve"    },
    { MAT_GAS_SIEVE,"Gas Sieve"},
    { MAT_LAMP,  "Lamp"  },
    { MAT_HEATER,"Heater"},
    { MAT_COOLER,"Cooler"},
    { TOOL_HEAT, "Heat"  },
    { TOOL_COOL, "Cool"  },
    { TOOL_SPARK,"Spark" },
    { MAT_EMPTY, "Erase" },
};
static const int N_BRUSH = (int)(sizeof(BRUSHES) / sizeof(BRUSHES[0]));
