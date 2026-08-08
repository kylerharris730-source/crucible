#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>   /* timeBeginPeriod; excluded by WIN32_LEAN_AND_MEAN */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "world.h"
#include "render.h"
#include "player.h"
#include "item.h"
#include "projectile.h"
#include "worldgen.h"
#include "light.h"
#include "room.h"
#include "device.h"
#include "door.h"
#include "tree.h"
#include "craft.h"
#include "map.h"
#include "entity.h"
#include "drone.h"
#include "accessory.h"
#include "save.h"

/* The window is a fixed-size left-hand tool panel plus the sim viewport. The
   viewport keeps a clean integer scale so pixels stay crisp and the
   window->cell mapping is a plain divide; the panel is its own strip of the
   window rather than an overlay, so buttons never hide playfield and a stroke
   can never paint underneath them. */
static const int SCALE   = 2;
/* Wide enough for two columns of material buttons. The palette outgrew a single
   column: at 25 entries the derived row pitch had squeezed down to 20px, and
   the fix for "make the buttons taller" is more width, not less content. */
static const int PANEL_W = 264;
/* Window pixels. VIEW_CELLS_* (render.h) is how much WORLD that shows; the two
   were the same thing back when the world was exactly one screen. */
static const int VIEW_W  = VIEW_CELLS_W * SCALE;
static const int VIEW_H  = VIEW_CELLS_H * SCALE;
static const int WIN_W   = PANEL_W + VIEW_W;
static const int WIN_H   = VIEW_H;
static const double FRAME_SECONDS = 1.0 / 60.0;

/* Top of the stats block. layoutPanel() stops the buttons above this and
   drawPanel() writes from it downward, so the two cannot drift apart -- which
   is the failure this panel has already had twice, buttons silently drawn on
   top of the stats text. Five lines now: the hover readout joins fps, sim ms,
   cells and chunks. */
static const int STATS_TOP = VIEW_H - 92;

static u32         g_pixels[VIEW_CELLS_W * VIEW_CELLS_H];
/* GDI honours the DIB's row stride, not the source rectangle's width.  A zoom
   crop therefore needs compact rows of its own; passing a narrower source
   rectangle over g_pixels would make its second row begin halfway through the
   first rendered row. */
static u32         g_zoomPixels[VIEW_CELLS_W * VIEW_CELLS_H];
static BITMAPINFO  g_bmi;
static HFONT       g_font;
static bool        g_running = true;

/* Off-screen back buffer. The sim blit, the panel and the HUD are all composed
   into this and sent to the window as one BitBlt. Drawing them straight to the
   window DC in stages is what used to flicker. */
static HDC     g_backDC;
static HBITMAP g_backBmp;
static HGDIOBJ g_backOldBmp;

/* Brush selections past MAT_COUNT are tools that act on temperature rather than
   placing a material. */
static const int TOOL_HEAT = MAT_COUNT;
static const int TOOL_COOL = MAT_COUNT + 1;
static const int HEAT_STEP = 12;   /* degrees per frame under the brush */

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
    { MAT_GRAPHENE,"Graphene"},
    { MAT_LAVA,  "Lava"  },
    { MAT_FIRE,  "Fire"  },
    { MAT_PLASMA,"Plasma"},
    { MAT_COLDFIRE,"Cold Fire"},
    { MAT_NITROGEN,"Liquid N2"},
    { MAT_ACID,    "Acid"     },
    { MAT_GLOWFLUID,"Glowfluid"},
    { MAT_WAX,     "Wax"      },
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
    { MAT_EMPTY, "Erase" },
};
static const int N_BRUSH = (int)(sizeof(BRUSHES) / sizeof(BRUSHES[0]));

enum ActionId { ACT_OVERWRITE, ACT_LAYER, ACT_WIRE, ACT_CIRCUIT, ACT_VIEW, ACT_LIGHT, ACT_PLAYER, ACT_PAUSE, ACT_FILTER, ACT_CLEAR, N_ACT };

/* --- simulation speed ----------------------------------------------------
   A multiplier on how many sim steps run per displayed frame, NOT a change to
   the frame rate: the window still presents at 60fps and the sim still uses a
   fixed step, so at 2x every rule simply happens twice before you see the
   result. Doing it this way means nothing in world.cpp has to know about it --
   there is no dt anywhere in the sim to scale, and introducing one to support
   this would have changed every rate constant in the model.

   The cost is real and linear: 4x is four times the sim work per frame, so a
   busy scene that just holds 60fps at 1x will drop frames at 4x. That shows up
   honestly in the ms/frame readout, which times all the substeps together. */
static const int SPEEDS[]  = { 1, 2, 4 };
static const int N_SPEED   = (int)(sizeof(SPEEDS) / sizeof(SPEEDS[0]));
/* The framebuffer remains its original size.  Zooming crops a smaller piece of
   it and scales that crop into the same viewport, so lighting and rendering
   retain their fixed, inexpensive buffers. */
static const int ZOOMS[]   = { 1, 2, 4 };
static const int N_ZOOM    = (int)(sizeof(ZOOMS) / sizeof(ZOOMS[0]));

/* Layout rects, filled once by layoutPanel().  The palette combines ordinary
   brushes and machines: sandbox play should not require turning the character
   on just to get an item into a hotbar before it can be placed. */
static const int N_PALETTE = N_BRUSH + DEV_COUNT;
static const int PALETTE_VISIBLE_ROWS = 14;
static RECT g_paletteRect[N_PALETTE];
static RECT g_paletteArea, g_paletteScrollTrack, g_paletteScrollThumb;
static RECT g_actRect[N_ACT];
static RECT g_sizeDec, g_sizeInc, g_sizeBox;
static RECT g_speedRect[N_SPEED];
static RECT g_zoomRect[N_ZOOM];

/* GDI objects, all created once -- object churn per frame is not free. */
static HBRUSH g_panelBg, g_btnBg, g_btnBgHot, g_btnBgSel, g_borderBrush, g_accentBrush, g_warnBrush;
static HBRUSH g_swatchBrush[N_BRUSH];

/* --- item icons -----------------------------------------------------------
   Each sprite is baked into a bitmap once and blitted, rather than drawn as
   pixels every frame. That is not a micro-optimisation, it is the difference
   between working and not: a 14x14 sprite is up to 196 filled rects, and the
   hotbar plus the creative grid want forty-odd icons a frame. The last time
   this codebase drew per-pixel with GDI it cost 511 ms a frame.

   Transparency is a magenta colour key rather than a mask, because the slot
   behind an icon has three different background colours (normal, hovered,
   selected) and a key handles all three with one bitmap. */
static const COLORREF ICON_KEY = RGB(255, 0, 255);
static const int ICON_SCALE = 2;                  /* 14x14 art -> 28x28 icon */
static const int ICON_PX    = SPR_W * ICON_SCALE;
static HDC     g_iconDC;
static HBITMAP g_iconBmp[SPR_COUNT];
static HBITMAP g_matIconBmp[MAT_COUNT];
static ItemId  g_hoverItem = ITEM_NONE;
static RECT    g_hoverRect;
static int     g_mx = 0, g_my = 0;
static bool inRect(const RECT& r, int x, int y);

static u32 iconLight(u32 c, int add) {
    int r = ((c >> 16) & 255) + add, g = ((c >> 8) & 255) + add, b = (c & 255) + add;
    return ((u32)imin(255, imax(0, r)) << 16) | ((u32)imin(255, imax(0, g)) << 8) | (u32)imin(255, imax(0, b));
}

static void buildMaterialIcons(HDC screen) {
    for (int m = 1; m < MAT_COUNT; ++m) {
        g_matIconBmp[m] = CreateCompatibleBitmap(screen, ICON_PX, ICON_PX);
        HGDIOBJ old = SelectObject(g_iconDC, g_matIconBmp[m]);
        RECT all = { 0, 0, ICON_PX, ICON_PX }; HBRUSH key = CreateSolidBrush(ICON_KEY);
        FillRect(g_iconDC, &all, key); DeleteObject(key);
        const u32 base = MATS[m].dryA, hi = iconLight(base, 42), lo = iconLight(base, -38);
        const bool molten = m == MAT_LAVA || m == MAT_IRON_MELT || m == MAT_COPPER_MELT || m == MAT_RUBBER_MELT || m == MAT_SLAG_MELT;
        for (int y = 0; y < SPR_H; ++y) for (int x = 0; x < SPR_W; ++x) {
            bool on = false;
            if (MATS[m].kind == KIND_POWDER) on = y >= 6 + abs(x - 6) / 2;
            else if (MATS[m].kind == KIND_LIQUID) on = y >= 7 + ((x + m) % 3 == 0 ? 1 : 0);
            else if (MATS[m].kind == KIND_GAS) { const int dx = x - 6, dy = y - 7; on = dx*dx + dy*dy < 30 || ((x-3)*(x-3)+(y-5)*(y-5)<12); }
            else on = x >= 2 && x <= 11 && y >= 2 && y <= 11;
            if (!on) continue;
            u32 c = ((x * 13 + y * 7 + m * 11) % 9 == 0) ? hi : (((x + y + m) % 11 == 0) ? lo : base);
            if (molten && ((x + y * 3 + m) % 5 == 0)) c = 0xFFD45A;
            RECT p = { x * ICON_SCALE, y * ICON_SCALE, (x + 1) * ICON_SCALE, (y + 1) * ICON_SCALE };
            HBRUSH b = CreateSolidBrush(RGB((c>>16)&255, (c>>8)&255, c&255)); FillRect(g_iconDC, &p, b); DeleteObject(b);
        }
        SelectObject(g_iconDC, old);
    }
}

static void buildIcons() {
    HDC screen = GetDC(NULL);
    g_iconDC = CreateCompatibleDC(screen);
    for (int s = 1; s < SPR_COUNT; ++s) {
        g_iconBmp[s] = CreateCompatibleBitmap(screen, ICON_PX, ICON_PX);
        HGDIOBJ old = SelectObject(g_iconDC, g_iconBmp[s]);
        RECT all = { 0, 0, ICON_PX, ICON_PX };
        HBRUSH key = CreateSolidBrush(ICON_KEY);
        FillRect(g_iconDC, &all, key);
        DeleteObject(key);
        for (int y = 0; y < SPR_H; ++y)
            for (int x = 0; x < SPR_W; ++x) {
                const u32 c = g_sprite[s][y * SPR_W + x];
                if (c == 0) continue;             /* transparent */
                RECT r = { x * ICON_SCALE, y * ICON_SCALE,
                           x * ICON_SCALE + ICON_SCALE, y * ICON_SCALE + ICON_SCALE };
                HBRUSH b = CreateSolidBrush(RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));
                FillRect(g_iconDC, &r, b);
                DeleteObject(b);
            }
        SelectObject(g_iconDC, old);
    }
    buildMaterialIcons(screen);
    ReleaseDC(NULL, screen);
}

/* Centres an item's icon in a rect, or falls back to a colour swatch for
   everything without one -- which is every material, deliberately. */
static void drawItemIcon(HDC hdc, const RECT& r, ItemId item) {
    if (inRect(r, g_mx, g_my)) { g_hoverItem = item; g_hoverRect = r; }
    if (item < MAT_COUNT && g_matIconBmp[item]) {
        const int side = imin(imin(r.right-r.left-2, r.bottom-r.top-2), ICON_PX);
        if (side > 0) { const int x = r.left + (r.right-r.left-side)/2, y = r.top + (r.bottom-r.top-side)/2;
            HGDIOBJ old = SelectObject(g_iconDC, g_matIconBmp[item]); TransparentBlt(hdc,x,y,side,side,g_iconDC,0,0,ICON_PX,ICON_PX,ICON_KEY); SelectObject(g_iconDC,old); }
        return;
    }
    const int s = ITEMS[item].sprite;
    if (s > SPR_NONE && s < SPR_COUNT && g_iconBmp[s]) {
        /* Square, and centred. Stretching to fill the rect would squash every
           icon by whatever the slot's aspect happened to be -- which turned the
           module chips into unreadable letterboxed smears in the hotbar, where
           the swatch area is wider than it is tall to leave room for the count.
           A colour swatch can be any shape; a picture cannot. */
        int side = imin(r.right - r.left - 2, r.bottom - r.top - 2);
        if (side > ICON_PX) side = ICON_PX;
        if (side < 1) return;
        const int w = side, h = side;
        const int x = r.left + (r.right - r.left - w) / 2;
        const int y = r.top  + (r.bottom - r.top - h) / 2;
        HGDIOBJ old = SelectObject(g_iconDC, g_iconBmp[s]);
        TransparentBlt(hdc, x, y, w, h, g_iconDC, 0, 0, ICON_PX, ICON_PX, ICON_KEY);
        SelectObject(g_iconDC, old);
        return;
    }
    HBRUSH b = CreateSolidBrush(RGB((ITEMS[item].colour >> 16) & 0xFF,
                                    (ITEMS[item].colour >> 8) & 0xFF,
                                     ITEMS[item].colour & 0xFF));
    RECT rr = r;
    FillRect(hdc, &rr, b);
    DeleteObject(b);
}

/* Circuit signals use the same icon slot as items. Materials reuse their item
   art; virtual 1-9 channels have dedicated sprite chips, so a wire readout is
   not visually demoted to plain text just because it carries a number. */
static void drawCircuitSignalIcon(HDC hdc, const RECT& r, int signal) {
    if (signal > MAT_EMPTY && signal < MAT_COUNT) { drawItemIcon(hdc, r, (ItemId)signal); return; }
    if (signal < CIR_SIG_1 || signal > CIR_SIG_9) return;
    const int sprite = SPR_SIGNAL1 + signal - CIR_SIG_1;
    const int side = imin(imin(r.right - r.left - 2, r.bottom - r.top - 2), ICON_PX);
    if (side <= 0 || !g_iconBmp[sprite]) return;
    const int x = r.left + (r.right - r.left - side) / 2;
    const int y = r.top + (r.bottom - r.top - side) / 2;
    HGDIOBJ old = SelectObject(g_iconDC, g_iconBmp[sprite]);
    TransparentBlt(hdc, x, y, side, side, g_iconDC, 0, 0, ICON_PX, ICON_PX, ICON_KEY);
    SelectObject(g_iconDC, old);
}

static void drawItemTooltip(HDC hdc) {
    if (g_hoverItem == ITEM_NONE || g_hoverItem >= ITEM_COUNT) return;
    const char* name = ITEMS[g_hoverItem].name;
    if (!name || !name[0]) return;
    RECT r = { g_hoverRect.left, g_hoverRect.top - 22, g_hoverRect.left + 150, g_hoverRect.top - 4 };
    if (r.top < 2) { r.top = g_hoverRect.bottom + 4; r.bottom = r.top + 18; }
    FillRect(hdc, &r, g_panelBg); FrameRect(hdc, &r, g_accentBrush);
    SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, RGB(245,224,150));
    DrawTextA(hdc, name, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/* UI/input state */
static bool g_uiCapture = false;   /* the click landed on the panel, not the sim */
static bool g_overwrite = false;   /* false = placement only fills empty space */
static int  g_view      = VIEW_NORMAL;
static bool g_lmb = false, g_rmb = false;
/* One consumable use per click rather than one per frame. Most left-click actions
   in the game wants to repeat while held -- digging, building, sowing -- so the
   rate limiting they share is a cooldown rather than a latch. Eggs, food and
   throwable flares are the opposite: each consumes one item, and holding the
   button should not empty the whole stack. Cleared on
   button release, in the same WM_LBUTTONUP that clears g_lmb. */
static bool g_useLatch = false;

/* --- the line tool ---------------------------------------------------------
   Hold R, press a mouse button, drag, release: the brush is laid down along the
   straight line between where you pressed and where you let go, once, rather
   than following the mouse.

   It is a MODIFIER on whatever you were already doing rather than a mode of its
   own -- the same brush, the same radius, the same material, the same
   left-builds / right-erases split. A mode would mean a button on the panel, a
   state to notice you are in, and a second way for every one of those choices
   to be wrong.

   Implemented by holding the stroke's start point instead of advancing it: the
   brush already interpolates from the previous point to the current one, so a
   straight line is what you get by simply not moving the previous point until
   the button comes up. Nothing new draws anything.

   --- sharing R with respawn ---
   R was already "respawn at the cursor", which is the escape hatch when you
   bury yourself and is worth keeping. The two coexist because one is a TAP and
   the other is a HOLD: respawn moved to key-UP and is suppressed if the hold
   was used to draw. That is subtle enough to be worth stating, and it is still
   better than moving a binding somebody relies on. */
/* What the last save or load said, and for how long to keep saying it. A save
   that reports nothing is a save you do not trust; four seconds is long enough
   to read and short enough not to become furniture. */
static char g_saveMsg[256] = "";
static int  g_saveMsgFrames = 0;
static const char* const SAVE_PATH = "crucible.sav";

static bool g_lineKey  = false;   /* R is down */
static bool g_lineOn   = false;   /* ...and a drag is in progress */
static bool g_lineDrew = false;   /* this hold of R drew something */
static int  g_lineX = 0, g_lineY = 0;
/* Wire mode is deliberately separate from R's general-purpose line modifier:
   it always places one-cell copper, regardless of the selected brush or held
   hotbar item. It is the quick way to make a circuit after placing machines. */
static bool g_wireMode = false;
static bool g_wireOn   = false;
static int  g_wireX = 0, g_wireY = 0;
/* Circuit wire is an information link, not copper. Click one device then a
   second: the visible cable joins their signal networks (or removes that same
   cable if it already exists). */
static bool g_circuitWireMode = false;
static int  g_circuitWireFrom = -1;
static int  g_circuitWireFromPort = 0;
/* Which device's panel is open, or -1. An index rather than a pointer so that a
   device being dug out from under an open panel cannot leave a dangling one --
   devTick can remove a device at any time, and the panel revalidates by index
   every frame it draws. */
static int  g_devPanel = -1;
static bool handleDevPanelClick(int mx, int my);
static bool handleCraftClick(int mx, int my);
static void layoutCraft();
extern bool g_craftOpen;
extern int  g_craftScroll;
static void drawDevPanel(HDC hdc);
static int  g_pmx = -1, g_pmy = -1;  /* previous aim point, in cells */
static int  g_brushMat = MAT_SAND;
/* -1 means a material/tool brush is selected.  Device buttons only become a
   free placement source while the character is off; survival inventory
   placement remains exactly as before. */
static int  g_paletteDevice = -1;
static int  g_paletteScroll = 0;
static int  g_paletteMaxScroll = 0;
static int  g_brushRadius = 6;
static bool g_paused = false;

/* --- the map ------------------------------------------------------------
   Two views of one dataset (see map.h). The MINIMAP is a small always-on
   window on the corner of the screen, and the FULL MAP takes the viewport and
   scrolls. They share the reveal, so walking with the minimap up fills in the
   full map too -- there is one record of where you have been, not two. */
static bool g_mapOpen  = false;      /* the full-screen map */
static bool g_miniOn   = true;       /* the corner minimap */
/* Where the full map is looking, in MAP pixels. Follows the player until you
   drag it, and re-centres when reopened -- a map you have to re-find yourself
   on every time you open it is a map you stop opening. */
static int  g_mapPanX = 0, g_mapPanY = 0;
static bool g_mapPanned = false;
static bool g_stepOnce = false;
static int  g_speedIdx = 0;      /* index into SPEEDS */
static int  g_zoomIdx = 0;       /* index into ZOOMS; zoom-in only */
/* The character can be switched off, because the sandbox this grew out of is
   still worth having on its own -- and because a figure standing in the middle
   of a scene you are trying to draw is a nuisance. */
static bool g_playerOn = true;
/* Index rather than a Device pointer: removing or rebuilding a bed cannot
   leave rest state pointing at recycled device storage. -1 is awake. */
static int  g_restBed = -1;
/* The pause menu. Escape opens it rather than quitting outright -- an unprompted
   Escape-to-quit is fine in a toy you are drawing in, and hostile in a game you
   have built something in. Quitting now takes a deliberate second action. */
static bool g_menuOpen = false;
/* Survival mode: the brush draws from the inventory and digging fills it.
   Off, the palette behaves as the unlimited sandbox tool it has always been,
   which is still how you build a scene to test something in. */
static bool g_survival = true;
/* --- which layer the brush acts on ---------------------------------------
   A modal toggle rather than a modifier key, because building a room means
   laying a lot of background in a row and holding a key through all of it is
   miserable. It is stateful and clearly labelled for the same reason a paint
   program has a layer selector rather than a shift-to-draw-behind.

   Both verbs move together: in background mode left-click backs a wall and
   right-click scrapes it off, exactly mirroring the foreground. */
static bool g_bgLayer = false;
/* Grown from 34 to make room for a square icon. At 34 the swatch area was 21
   wide by 13 tall once the count row was reserved, so a 14x14 sprite had to be
   letterboxed into a strip and the module chips were unreadable. Icons want
   square space, and the row still spans well under half the viewport. */
static const int HOTBAR_SLOT = 42;   /* screen pixels per hotbar cell */
static RECT g_hotRect[HOTBAR_SLOTS];
static RECT g_menuResume, g_menuQuit, g_menuPanel;

/* --- the creative inventory ----------------------------------------------

   Tab opens a grid of every item that exists, and clicking one puts it in the
   pack. It is a debug tool that is honest about being one, rather than a
   crafting screen with the costs switched off.

   That is the right shape for this specifically because item ids and material
   ids share a space and ITEMS[] is filled programmatically from MATS[]: the
   grid is a loop over the table, so a material added tomorrow appears here with
   no edit at all. A hand-maintained spawn list would be wrong the first time
   somebody forgot to add a row, and quietly -- you would think the material was
   broken when it was only missing from a menu.

   Right-click removes rather than adds, because the tedious half of testing an
   item is getting rid of it again. */
static bool g_creativeOpen = false;
static const int CRE_COLS = 4;
/* How many rows of the palette are on screen at once, and where the window
   starts. The list is 83 entries and growing; at four columns that is 21 rows,
   and a panel tall enough for all of them ran off the top and bottom of the
   window once the pack was added under it.

   Scrolling rather than shrinking the rows, because the rows are the point.
   They were tried as bare icon squares -- twenty across, name on hover -- which
   fit beautifully and was unusable: two thirds of this table is materials whose
   swatch is a flat colour, so Stone, Wall, Slag and Ceramic were four grey
   squares and the only way to tell them apart was to point at each one. A list
   you can read is worth more than a list that fits. */
static const int CRE_VIS_ROWS = 10;
static const int CRE_MAX_ENTRIES = ITEM_COUNT + 9;
static int g_creScroll = 0;      /* first visible row */
static int g_creRowCount = 0;    /* total rows, for clamping and the bar */
static RECT g_creTrack, g_creThumb;
static RECT g_creRect[CRE_MAX_ENTRIES];
static RECT g_crePanel, g_creClear;
static RECT g_creSearchBox;
static int  g_creCount = 0;              /* entries actually laid out */
static int g_creItem[CRE_MAX_ENTRIES];    /* item id or CircuitSignal in picker mode */
static char g_creSearch[32] = "";
static bool g_creSearchFocus = false;
static int  g_filterDevice = -1;         /* drain/watcher material picker */

/* --- the dig filter --------------------------------------------------------
   A whitelist of materials the tool is allowed to break, and a mode switch for
   it. It exists for one situation that has no other answer: a vessel part-way
   through a smelt holds the thing you are making and the things you are making
   it FROM, all interleaved -- ceramic with copper and slag through it -- and
   the ordinary brush is indiscriminate. Draining the copper out while it is
   still liquid is the elegant way and it needs planning; this is the way that
   works after the fact, without dismantling the setup.

   Two pieces of state rather than one, because "which materials" should
   OUTLIVE "is it on". You tick copper and slag once and then toggle the mode
   as often as you like; losing the selection every time you switched it off
   would make the feature annoying in exactly the situation it is for. */
static bool g_digFilterOn = false;
static bool g_digFilterMat[MAT_COUNT];
/* True while the whitelist picker is up. Distinct from g_filterDevice above,
   which picks ONE material for a drain; this one toggles many and stays open. */
static bool g_digFilterPicking = false;

static int digFilterCount() {
    int n = 0;
    for (int m = 1; m < MAT_COUNT; ++m) if (g_digFilterMat[m]) ++n;
    return n;
}

/* The picker's title, which doubles as the readout: it is the only place the
   current selection is written out in words, and a whitelist you cannot read
   back is a whitelist you will not trust. Names the materials while there are
   few enough to fit and counts them after that. */
static const char* digFilterTitle() {
    static char t[256];
    const int n = digFilterCount();
    if (n == 0) {
        return "DIG FILTER  --  click materials to whitelist. NOTHING TICKED: nothing can be broken";
    }
    int written = sprintf(t, "DIG FILTER (%d)  --  breaking only:", n);
    int listed = 0;
    for (int m = 1; m < MAT_COUNT && written < 200; ++m) {
        if (!g_digFilterMat[m]) continue;
        if (listed == 6) { written += sprintf(t + written, ", ..."); break; }
        written += sprintf(t + written, "%s %s", listed ? "," : "", MATS[m].name);
        ++listed;
    }
    return t;
}
enum CircuitPickField { CIR_PICK_NONE = -1, CIR_PICK_SIGNAL, CIR_PICK_A, CIR_PICK_B, CIR_PICK_OUT };
static int  g_signalPickerDevice = -1;
static int  g_signalPickerField = CIR_PICK_NONE;

/* --- the pack, and the thing on the cursor ---------------------------------
   The player's own forty slots, drawn under the creative palette, with the
   hotbar as the bottom row -- because it IS the bottom row: the hotbar is the
   first ten entries of the same array, so the grid has to line up under it or
   the two read as separate containers when they are one.

   g_drag is what the cursor is carrying, and it is the whole of the
   click-and-carry model. One stack, held between slots, exactly as every game
   with an inventory screen does it: click a slot to lift its contents, click
   another to put them down, click a matching one to merge. Everything the
   screen can do -- move, split, merge, swap, equip, install a module -- is that
   one gesture against different slots, which is why it replaces four separate
   click rules rather than adding a fifth.

   An ItemStack rather than an item id and a count, so a tool keeps its `inst`
   while it is on the cursor. A multitool that lost its modules by being dragged
   two slots to the left would be the worst possible bug to ship in a feature
   whose entire purpose is rearranging things. */
static RECT g_packRect[INV_SLOTS];
static ItemStack g_drag = { ITEM_NONE, 0, 0 };
static int g_chestOpen = -1;
static ItemStack g_chestStack = { ITEM_NONE, 0, 0 };
static RECT g_chestPanel, g_chestSlot, g_chestPack[INV_SLOTS], g_chestClose;
static bool handleChestClick(int mx, int my, bool right);
static void drawChest(HDC hdc);
static void closeChest();

/* The tool bench: the carried multitool's own slots, drawn inside the inventory
   screen. It belongs here rather than in a screen of its own because installing
   a module is a transfer between two containers, and putting both on screen at
   once is what makes that legible without a drag-and-drop system. */
static RECT g_toolSlotRect[TOOL_SLOTS_MAX];
static int  g_toolSlotCount = 0;
/* The payload slot: one more square past the last module, holding an actual
   ItemStack (ammunition) rather than a bare module id -- see ToolInst::
   payload in item.h. Drawn and clicked with the exact same slotClick()
   gesture the pack already uses, since it is a genuine ItemStack. */
static RECT g_toolPayloadRect;
static int  g_toolPackSlot  = -1;   /* which inventory slot the bench is showing */

/* Equipment slots, on the same screen and for the same reason as the tool
   bench: putting a jetpack on is a transfer between two containers, and both
   have to be visible for click-to-move to say what a drag would. Always drawn,
   unlike the bench -- an empty equipment row tells you the slots exist and what
   goes in them, whereas an absent tool bench tells you nothing is missing. */
static RECT g_eqRect[EQ_COUNT];
static RECT g_droneModuleRect[MAX_DRONES][Inventory::DRONE_MODULE_SLOTS_MAX];

/* --- the trash ------------------------------------------------------------
   Somewhere to put things you do not want. Sitting on the end of the equipment
   row because that is where the slots that are not the pack already live.

   It HOLDS what you last put in rather than swallowing it, which is the whole
   design: dropping the wrong stack into a void is unrecoverable and happens to
   everyone, and a slot that keeps one thing costs nothing and makes the mistake
   undoable. Putting something else in is what finally destroys the last thing --
   so it is a bin you can reach back into until the next time you use it.

   Deliberately NOT saved. The contents are already discarded as far as the
   player is concerned; persisting them would make the bin a fortieth inventory
   slot that survives sessions, which is the opposite of throwing something
   away. */
static RECT      g_trashRect;
static ItemStack g_trash;

/* --- the camera ----------------------------------------------------------
   Top-left cell of the visible window. Kept as floats so the follow can be
   smoothed, and truncated to whole cells for every use -- a camera at a
   fractional position would shimmer the whole world by a pixel as it drifts,
   which is far more distracting than the character being a pixel off centre.

   The margin is what the simulation runs beyond the edges of the view. It
   exists so that things do not visibly start moving the moment they scroll
   into frame: sand mid-fall just off-screen keeps falling, and arrives already
   in motion. Half a screen was picked as the smallest margin where you cannot
   catch the boundary by running at it -- the character crosses a screen in
   about seven seconds, and the margin refills far faster than that. */
static const int SIM_MARGIN = 192;   /* cells beyond the view that still tick */
static float g_camXf = 0.0f, g_camYf = 0.0f;
static int   g_camX  = 0,    g_camY  = 0;

static int zoomFactor() { return ZOOMS[g_zoomIdx]; }
static int viewCellsW() { return VIEW_CELLS_W / zoomFactor(); }
static int viewCellsH() { return VIEW_CELLS_H / zoomFactor(); }
static int cellPixels() { return SCALE * zoomFactor(); }

/* stats */
static double g_fps = 0.0, g_simMs = 0.0;
static int    g_cellCount = 0;

/* Where the tool acts versus where the mouse is; see currentAim() below for
   why they can differ. Declared up here because both the hover readout and the
   cursor drawing need it before it is defined. */
struct Aim {
    int  x, y;           /* where the tool acts, in cells */
    int  ghostX, ghostY; /* where the mouse actually is, in cells */
    bool clamped;
};
static Aim currentAim();
static Aim currentWireAim();
static void applyBrush();
static void circuitWireClick();

/* Lay down the line and end the drag. g_pmx/g_pmy still hold the press point,
   which is the whole trick -- applyBrush interpolates from there to the current
   aim, so one ordinary call draws the straight line.

   Called from the button-up handlers BEFORE they clear g_lmb/g_rmb, since
   applyBrush does nothing with no button held. */
/* Begin a line drag, if R is held. The anchor goes straight into the stroke's
   "previous point", which is where a freehand stroke would have put it anyway --
   the only difference is that applyBrush will not advance it. */
static void startLine() {
    if (!g_lineKey || g_uiCapture || g_mx < PANEL_W) return;
    const Aim a = currentAim();
    g_pmx = a.x; g_pmy = a.y;
    g_lineX = a.x; g_lineY = a.y;     /* kept separately, for the preview */
    g_lineOn = true;
}

static void commitLine() {
    if (!g_lineOn) return;
    g_lineOn   = false;
    g_lineDrew = true;      /* so releasing R does not also respawn */
    applyBrush();
    g_pmx = -1;
}

static void startWire() {
    if (!g_wireMode || g_uiCapture || g_mx < PANEL_W) return;
    const Aim a = currentWireAim();
    g_wireX = a.x; g_wireY = a.y;
    g_wireOn = true;
}

/* Place an intentionally narrow, 4-connected copper route. A normal pixel
   line can advance diagonally from (x,y) to (x+1,y+1); pixels then touch only
   at a corner, which electrical conduction correctly treats as disconnected.
   The bridge cell on every diagonal advance makes the raster route continuous
   without ever painting a brush-sized blob or overwriting existing work. */
static void wireCell(int x, int y) {
    if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) return;
    const u8 old = g_world.at(x, y).mat;
    if (old == MAT_COPPER) return;
    if (old != MAT_EMPTY) return;
    if (g_survival && g_playerOn && !g_inv.take(MAT_COPPER, 1)) return;
    g_world.setCell(x, y, MAT_COPPER);
}

static void commitWire() {
    if (!g_wireOn) return;
    g_wireOn = false;
    const Aim a = currentWireAim();
    int x = g_wireX, y = g_wireY;
    const int dx = abs(a.x - x), sx = x < a.x ? 1 : -1;
    const int dy = abs(a.y - y), sy = y < a.y ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        wireCell(x, y);
        if (x == a.x && y == a.y) break;
        const int twice = err * 2;
        const bool moveX = twice > -dy;
        const bool moveY = twice < dx;
        int nx = x, ny = y;
        if (moveX) { err -= dy; nx += sx; }
        if (moveY) { err += dx; ny += sy; }
        if (moveX && moveY) wireCell(nx, y);  /* cardinal bridge at a diagonal */
        x = nx; y = ny;
    }
    roomsNotifyEdit(g_world, a.x, a.y);
}

/* Base reach plus whatever the pack is carrying. Reading it through a function
   rather than a constant is what lets an item change it -- and later a tool. */
static int currentReach() { return PLAYER_REACH + g_inv.reachBonus(); }

/* --- what you dig with, and how big ----------------------------------------
   Mining is clamped to whatever is in your hand; BUILDING IS NOT.

   That asymmetry was a deliberate change from capping both. Capping building
   means the player wants to make something and the game says "not yet", which
   buys pacing at the price of the thing people actually play a sandbox for.
   Capping mining costs nothing expressive -- the hole still gets dug, just
   slower -- and it is what gives every tier of the ladder something to sell.

   The size control is left free to run past the current cap rather than being
   clamped at the source, so the number you set survives picking up a better
   tool and immediately means more. */
static ToolSpec digSpec() { return miningSpec(g_inv); }
static int digRadius()    { return imin(g_brushRadius, digSpec().maxRadius); }
static int buildRadius()  { return g_brushRadius; }

/* What is under the cursor, for the panel readout. Returns false when the
   pointer is off the playfield (over the panel, or outside the window), so the
   caller can show a placeholder instead of a stale name.

   Air is reported as "Air" with its temperature rather than skipped: pointing
   at apparently empty space to see how hot it is turns out to be one of the
   more useful things the readout does, since heat is invisible in Material
   view and only roughly shaded in Glow view. */
static bool hoverCell(char* out, int cap, u32* colOut) {
    /* Reports the cell the tool would act on, not the one under the mouse.
       Past the reach limit those differ, and naming the unreachable cell would
       be describing something you cannot touch. */
    const Aim a = currentAim();
    const int cx = a.x, cy = a.y;
    if (g_mx < PANEL_W || cx < 0 || cx >= SIM_W || cy < 0 || cy >= SIM_H) return false;
    const Cell& c = g_world.at(cx, cy);
    const int t = (int)g_world.temp[cy * SIM_W + cx] - TEMP_OFFSET;
    const char* name = (c.mat == MAT_EMPTY) ? "Air" : MATS[c.mat].name;
    /* Names what is BEHIND as well as in front, because in background mode the
       thing you are about to act on is the one you cannot otherwise identify --
       a backdrop is deliberately too dark to tell apart by colour alone. */
    const u8 b = g_world.bgAt(cx, cy);
    if (b != MAT_EMPTY)
        _snprintf(out, cap, "%s  %+d C  / %s%s", name, t, MATS[b].name,
                  g_world.bgPlaced(cx, cy) ? " (built)" : "");
    else
        _snprintf(out, cap, "%s  %+d C", name, t);
    out[cap - 1] = 0;
    if (colOut) {
        /* the material's own dry colour, so the label is tinted like the thing
           it names -- and a mid-tone floor so dark materials stay readable */
        u32 col = (c.mat == MAT_EMPTY) ? 0x9AA0AA : MATS[c.mat].dryA;
        int r = (col >> 16) & 0xFF, g = (col >> 8) & 0xFF, b = col & 0xFF;
        if (r + g + b < 240) { r += 70; g += 70; b += 70; }
        *colOut = ((u32)imin(r,255) << 16) | ((u32)imin(g,255) << 8) | (u32)imin(b,255);
    }
    return true;
}

static bool inRect(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

/* ======================================================================
   Layout
   ====================================================================== */
static void layoutPanel() {
    /* The pitch is DERIVED from the space available rather than hard-coded, and
       that is a fix for a real trap rather than tidiness. Twice now the palette
       has grown and silently overflowed the stats block at the foot of the
       panel: nothing warns, the buttons just draw on top of the text. The fixed
       22+4 pitch had room for one more row and this change adds three (Copper,
       Graphene, and the speed selector), so it would have overflowed again.

       Now the rows divide up whatever is between the title and the stats, so
       adding a button can never overlap anything -- it just makes every row a
       little shorter. Capped at 26 so a short palette does not stretch into
       comically tall buttons. */
    const int pad = 10, w = PANEL_W - pad * 2;
    const int top = 34;              /* leave room for the title */
    const int statsTop = STATS_TOP;  /* keep clear of the stats block */
    const int sepTotal = 12;         /* the two 6px group separators below */

    /* The scrollable two-column catalog deliberately has a fixed visible
       height.  Materials and devices therefore compete for neither panel
       height nor readability as the catalog grows. */
    const int rowCount  = PALETTE_VISIBLE_ROWS + 3 + N_ACT; /* + size, speed, zoom */

    int pitch = (statsTop - top - sepTotal) / rowCount;
    if (pitch > 30) pitch = 30;
    if (pitch < 14) pitch = 14;   /* past this the labels are unreadable anyway;
                                     better to overflow visibly than to compute
                                     a zero or negative row height */
    const int gap = 4;
    const int h = pitch - gap;
    int y = top;

    /* The catalog is row-major: scrolling through it reads in the same order
       as its definitions, first all materials then all devices. */
    {
        const int colGap = 6;
        const int railW = 10;
        const int catalogW = w - railW - 4;
        const int colW   = (catalogW - colGap) / 2;
        const int totalRows = (N_PALETTE + 1) / 2;
        g_paletteMaxScroll = imax(0, totalRows - PALETTE_VISIBLE_ROWS);
        g_paletteScroll = imax(0, imin(g_paletteScroll, g_paletteMaxScroll));
        SetRect(&g_paletteArea, pad, top, pad + catalogW, top + PALETTE_VISIBLE_ROWS * pitch - gap);
        SetRect(&g_paletteScrollTrack, pad + catalogW + 4, top,
                pad + catalogW + 4 + railW, top + PALETTE_VISIBLE_ROWS * pitch - gap);
        if (g_paletteMaxScroll > 0) {
            const int trackH = g_paletteScrollTrack.bottom - g_paletteScrollTrack.top;
            const int thumbH = imax(20, trackH * PALETTE_VISIBLE_ROWS / totalRows);
            const int travel = trackH - thumbH;
            const int thumbY = g_paletteScrollTrack.top + travel * g_paletteScroll / g_paletteMaxScroll;
            SetRect(&g_paletteScrollThumb, g_paletteScrollTrack.left, thumbY,
                    g_paletteScrollTrack.right, thumbY + thumbH);
        } else {
            SetRectEmpty(&g_paletteScrollThumb);
        }
        for (int i = 0; i < N_PALETTE; ++i) {
            const int col = i & 1;
            const int row = i / 2 - g_paletteScroll;
            if (row < 0 || row >= PALETTE_VISIBLE_ROWS) {
                SetRectEmpty(&g_paletteRect[i]);
                continue;
            }
            const int x0  = pad + col * (colW + colGap);
            SetRect(&g_paletteRect[i], x0, top + row * pitch, x0 + colW, top + row * pitch + h);
        }
        y = top + PALETTE_VISIBLE_ROWS * pitch;
    }

    y += 6;
    /* brush size: [-]  size N  [+] */
    SetRect(&g_sizeDec, pad,            y, pad + 24,     y + h);
    SetRect(&g_sizeBox, pad + 24 + 4,   y, pad + w - 28, y + h);
    SetRect(&g_sizeInc, pad + w - 24,   y, pad + w,      y + h);
    y += pitch;

    /* speed: [1x][2x][4x], one segment each, as a radio row rather than a
       -/+ stepper because there are only three values and showing which one is
       live matters more than nudging between them. */
    for (int i = 0; i < N_SPEED; ++i) {
        const int x0 = pad + (w * i) / N_SPEED;
        const int x1 = pad + (w * (i + 1)) / N_SPEED;
        SetRect(&g_speedRect[i], x0, y, x1 - 3, y + h);
    }
    y += pitch;

    /* Zoom has the same three, directly-selectable values as simulation speed.
       Showing them together makes clear that speed advances time while zoom
       only changes how much world is visible. */
    for (int i = 0; i < N_ZOOM; ++i) {
        const int x0 = pad + (w * i) / N_ZOOM;
        const int x1 = pad + (w * (i + 1)) / N_ZOOM;
        SetRect(&g_zoomRect[i], x0, y, x1 - 3, y + h);
    }
    y += pitch + 6;

    for (int i = 0; i < N_ACT; ++i) {
        SetRect(&g_actRect[i], pad, y, pad + w, y + h);
        y += pitch;
    }
}

/* The menu is laid out fresh each time rather than in layoutPanel(), because it
   is centred on the viewport and nothing else depends on where it lands. */
/* --- the controls list -----------------------------------------------------
   Added because the line tool's binding had to be ASKED FOR, which is the only
   evidence that matters about whether a shortcut is discoverable. Every binding
   here was equally invisible; the line tool is just the one that got noticed,
   because it is the only one with no button on the panel doing the same job.

   In the pause menu rather than a screen of its own: it is where you already
   go when you want to stop and work something out, and a help screen nobody
   opens is the same as no help screen. */
struct KeyHint { const char* key; const char* what; };
static const KeyHint KEY_HINTS[] = {
    { "WASD / arrows", "move, and jump" },
    { "hold R + drag", "draw a straight line" },
    { "F",             "toggle one-cell wire mode" },
    { "X",             "toggle circuit-wire linking" },
    { "tap R",         "respawn at the cursor" },
    { "left / right",  "build / dig" },
    { "right-click",   "open a machine, or a door" },
    { "wheel",         "pick a hotbar slot" },
    { "Q + wheel",     "brush size" },
    { "C",             "crafting" },
    { "F5 / F9",       "save / load" },
    { "Tab",           "the item grid" },
    { "L",             "build on the backdrop" },
    { "V / K",         "cycle view / lights" },
    { "P / .",         "pause / step one frame" },
};
static const int N_KEY_HINTS = (int)(sizeof(KEY_HINTS) / sizeof(KEY_HINTS[0]));

static void layoutMenu() {
    const int w = 344, h = 150 + N_KEY_HINTS * 15 + 22
                    + (saveTotalBytes() > 0 ? 26 + 8 * 15 : 0);
    const int cx = PANEL_W + VIEW_W / 2, cy = VIEW_H / 2;
    SetRect(&g_menuPanel, cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
    const int bw = w - 120, bx = cx - bw / 2;
    const int top = g_menuPanel.top;
    SetRect(&g_menuResume, bx, top + 42, bx + bw, top + 74);
    SetRect(&g_menuQuit,   bx, top + 82, bx + bw, top + 114);
}

/* Laid out fresh on open, like the pause menu, and for the same reason: it is
   centred on the viewport and nothing else depends on where it lands. The row
   count is derived from how many items there are, so the panel grows with the
   material table instead of clipping it. */
static bool creativeMatches(const char* name) {
    for (int a = 0; g_creSearch[a]; ++a) {
        bool found = false;
        for (int b = 0; name[b]; ++b) {
            char x = name[b], q = g_creSearch[a];
            if (x >= 'A' && x <= 'Z') x = (char)(x + ('a' - 'A'));
            if (q >= 'A' && q <= 'Z') q = (char)(q + ('a' - 'A'));
            if (x == q) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

static void layoutCreative() {
    g_creCount = 0;
    const bool signalPicker = g_signalPickerDevice >= 0;
    if (signalPicker) {
        for (int signal = CIR_SIG_1; signal < CIRCUIT_SIGNAL_COUNT; ++signal)
            if (creativeMatches(circuitSignalName(signal))) g_creItem[g_creCount++] = signal;
        for (int material = 1; material < MAT_COUNT; ++material) {
            if (material == MAT_DEVICE) continue;
            if (creativeMatches(circuitSignalName(material))) g_creItem[g_creCount++] = material;
        }
    } else for (int i = 0; i < ITEM_COUNT; ++i) {
        if (ITEMS[i].maxStack == 0 || !creativeMatches(ITEMS[i].name)) continue;
        g_creItem[g_creCount++] = i;
    }
    /* The bench only appears when there is a tool to show, and its height is
       part of the panel's height rather than an overlay -- so picking up a
       multitool makes the window taller instead of pushing the grid under it. */
    g_toolPackSlot   = signalPicker ? -1 : g_inv.firstToolSlot();
    g_toolSlotCount  = 0;
    if (g_toolPackSlot >= 0)
        g_toolSlotCount = imin(ITEMS[g_inv.slot[g_toolPackSlot].item].toolSlots, TOOL_SLOTS_MAX);

    /* --- the palette scrolls ---------------------------------------------
       A fixed window of CRE_VIS_ROWS rows with the rest scrolled past, so the
       panel's height no longer depends on how many materials exist. That
       matters more every time one is added: the crops alone put eight new rows
       in here.

       Rects for rows outside the window are set EMPTY rather than merely being
       skipped when drawing. inRect() then fails on them for free, so a click
       cannot land on a row that is scrolled out of sight -- which is the bug
       this shape avoids rather than the bug it would otherwise have. */
    const int cw = 168, ch = 26, gap = 4, pad = 14;
    const int ps = 34, pgap = 3;
    g_creRowCount = (g_creCount + CRE_COLS - 1) / CRE_COLS;
    const int visRows = imin(CRE_VIS_ROWS, g_creRowCount);
    const int maxScroll = imax(0, g_creRowCount - CRE_VIS_ROWS);
    if (g_creScroll > maxScroll) g_creScroll = maxScroll;
    if (g_creScroll < 0) g_creScroll = 0;

    const int benchH = g_toolSlotCount ? 62 : 0;
    const int equipH = signalPicker ? 0 : 62;
    bool hasDrone = false;
    if (!signalPicker) for (int i = 0; i < MAX_DRONES; ++i) {
        const int eq = i == 0 ? EQ_LIGHT_DRONE : (i == 1 ? EQ_DRONE_A : EQ_DRONE_B);
        if (!g_inv.equip[eq].empty()) { hasDrone = true; break; }
    }
    const int droneModuleH = hasDrone ? 62 : 0;
    const int paletteH = visRows * (ch + gap);
    const int packH    = signalPicker ? 0 : 22 + INV_ROWS * (ps + pgap) + 10;
    const int barW     = 10;
    const int w = pad * 2 + CRE_COLS * cw + (CRE_COLS - 1) * gap + barW + 6;
    const int h = pad + 56 + paletteH + 10 + packH + equipH + droneModuleH + benchH + 38;
    const int cx = PANEL_W + VIEW_W / 2, cy = VIEW_H / 2;
    const int x0 = cx - w / 2, y0 = cy - h / 2;
    SetRect(&g_crePanel, x0, y0, x0 + w, y0 + h);

    const int listTop = y0 + pad + 56;
    SetRect(&g_creSearchBox, x0 + pad, y0 + pad + 24, x0 + w - pad - 18, y0 + pad + 46);
    for (int i = 0; i < g_creCount; ++i) {
        const int c = i % CRE_COLS, r = i / CRE_COLS - g_creScroll;
        if (r < 0 || r >= visRows) { SetRectEmpty(&g_creRect[i]); continue; }
        const int bx = x0 + pad + c * (cw + gap);
        const int by = listTop + r * (ch + gap);
        SetRect(&g_creRect[i], bx, by, bx + cw, by + ch);
    }

    /* The bar. A track the full height of the window with a thumb sized to the
       fraction on screen, which is the one piece of information a scrollbar has
       to carry: how much of the list you are looking at. */
    const int trackX = x0 + w - pad - barW + 4;
    SetRect(&g_creTrack, trackX, listTop, trackX + barW, listTop + paletteH);
    if (maxScroll > 0) {
        const int th = imax(24, paletteH * visRows / g_creRowCount);
        const int ty = listTop + (paletteH - th) * g_creScroll / maxScroll;
        SetRect(&g_creThumb, trackX, ty, trackX + barW, ty + th);
    } else {
        g_creThumb = g_creTrack;
    }

    /* The pack, then equipment, then the tool bench. */
    const int packY = listTop + paletteH + 10 + 22;
    if (signalPicker) {
        for (int i = 0; i < INV_SLOTS; ++i) SetRectEmpty(&g_packRect[i]);
        for (int i = 0; i < EQ_COUNT; ++i) SetRectEmpty(&g_eqRect[i]);
        for (int d = 0; d < MAX_DRONES; ++d)
            for (int i = 0; i < Inventory::DRONE_MODULE_SLOTS_MAX; ++i) SetRectEmpty(&g_droneModuleRect[d][i]);
        SetRectEmpty(&g_trashRect);
        for (int i = 0; i < TOOL_SLOTS_MAX; ++i) SetRectEmpty(&g_toolSlotRect[i]);
    }
    for (int i = 0; i < INV_SLOTS; ++i) {
        if (signalPicker) break;
        const int c = i % HOTBAR_SLOTS, r = i / HOTBAR_SLOTS;
        /* The HOTBAR row drawn LAST, at the bottom, the way it sits on screen.
           Slots 0..9 are the hotbar and they belong under the rest of the pack,
           not above it, or the grid contradicts the bar it describes. */
        const int rr = (r == 0) ? INV_ROWS - 1 : r - 1;
        const int bx = x0 + pad + c * (ps + pgap);
        const int by = packY + rr * (ps + pgap);
        SetRect(&g_packRect[i], bx, by, bx + ps, by + ps);
    }

    const int eqY = packY + packH;
    for (int d = 0; d < MAX_DRONES; ++d)
        for (int i = 0; i < Inventory::DRONE_MODULE_SLOTS_MAX; ++i) SetRectEmpty(&g_droneModuleRect[d][i]);
    if (!signalPicker) for (int i = 0; i < EQ_COUNT; ++i)
        SetRect(&g_eqRect[i], x0 + pad + i * 40, eqY, x0 + pad + i * 40 + 34, eqY + 34);
    /* A gap of half a slot before it, so it reads as separate from the things
       you are wearing rather than as a fifth equipment slot. */
    if (signalPicker) SetRectEmpty(&g_trashRect);
    else SetRect(&g_trashRect, x0 + pad + EQ_COUNT * 40 + 20, eqY,
                 x0 + pad + EQ_COUNT * 40 + 54, eqY + 34);

    const int droneY = eqY + equipH;
    if (!signalPicker) for (int d = 0; d < MAX_DRONES; ++d) {
        const int eq = d == 0 ? EQ_LIGHT_DRONE : (d == 1 ? EQ_DRONE_A : EQ_DRONE_B);
        if (g_inv.equip[eq].empty()) continue;
        /* Current chassis all expose one socket. The other two rects remain
           empty until an upgraded chassis unlocks them, so the UI cannot
           accidentally accept a future slot early. */
        const int bayX = x0 + pad + eq * 40;
        SetRect(&g_droneModuleRect[d][0], bayX, droneY, bayX + 34, droneY + 34);
    }

    /* Module slots: square, and noticeably bigger than a grid row, because they
       are the one place on this screen where the arrangement carries meaning
       (slot order decides which module is the shot). */
    const int by2 = eqY + equipH + droneModuleH;
    for (int i = 0; !signalPicker && i < g_toolSlotCount; ++i) {
        const int bx = x0 + pad + i * 40;
        SetRect(&g_toolSlotRect[i], bx, by2, bx + 34, by2 + 34);
    }
    /* One slot further along than the last module -- a visible gap would
       misread as "another module slot the tool does not have", so it sits
       flush against them and earns its own label in drawCreative() instead. */
    if (!signalPicker && g_toolPackSlot >= 0) {
        const int bx = x0 + pad + g_toolSlotCount * 40;
        SetRect(&g_toolPayloadRect, bx, by2, bx + 34, by2 + 34);
    } else {
        SetRectEmpty(&g_toolPayloadRect);
    }

    SetRect(&g_creClear, x0 + pad, y0 + h - 32, x0 + pad + 120, y0 + h - 8);
}

/* --- one gesture ----------------------------------------------------------
   Everything the inventory screen does is this: the cursor either holds a stack
   or it does not, and a click on a slot resolves the two against each other.

     cursor empty, slot full   ->  lift it (right-click lifts half)
     cursor full, slot empty   ->  put it down (right-click puts one)
     cursor full, slot same    ->  merge, up to the stack limit
     cursor full, slot other   ->  swap them

   That one rule replaces four separate ones -- click-to-equip, click-to-unequip,
   click-to-install, click-to-uninstall -- and it replaces them with the rule
   every player already knows from every other game with an inventory screen.

   Splitting on the RIGHT button is the half that makes it worth having over
   click-to-move. Stacks here run to a hundred thousand, so "take some" is the
   common case and "take all" is the rare one, and a screen whose only way to
   move thirty sand is to move a hundred thousand sand and put most of it back
   is a screen you fight.

   Returns whether anything happened, so a caller can tell a handled click from
   one that landed on the background. */
static bool slotClick(ItemStack& st, bool right) {
    if (g_drag.empty()) {
        if (st.empty()) return false;
        if (right && st.count > 1) {
            /* Half, rounded UP, so a stack of one still splits into something
               rather than into nothing and a confused player. */
            const u32 half = (st.count + 1) / 2;
            g_drag = st;
            g_drag.count = half;
            /* The instance handle stays with the part left behind. A tool never
               stacks above one so this branch cannot split one, and copying the
               handle would put the same tool in two places. */
            g_drag.inst = 0;
            st.count -= half;
            if (st.count == 0) st.item = ITEM_NONE;
        } else {
            g_drag = st;
            st.item = ITEM_NONE; st.count = 0; st.inst = 0;
        }
        return true;
    }

    if (st.empty()) {
        if (right && g_drag.count > 1) {
            st.item = g_drag.item; st.count = 1; st.inst = 0;
            --g_drag.count;
        } else {
            st = g_drag;
            g_drag.item = ITEM_NONE; g_drag.count = 0; g_drag.inst = 0;
        }
        return true;
    }

    if (st.item == g_drag.item && g_drag.inst == 0 && st.inst == 0) {
        const u32 cap  = ITEMS[st.item].maxStack;
        const u32 room = cap > st.count ? cap - st.count : 0;
        if (room == 0) return false;
        const u32 move = right ? 1u : (g_drag.count < room ? g_drag.count : room);
        st.count     += move;
        g_drag.count -= move;
        if (g_drag.count == 0) { g_drag.item = ITEM_NONE; g_drag.inst = 0; }
        return true;
    }

    /* Two different things: swap, and only on the left button. Swapping on the
       right as well would make "put one down" and "exchange everything" the
       same gesture whenever the target happened to be occupied. */
    if (right) return false;
    const ItemStack tmp = st;
    st = g_drag;
    g_drag = tmp;
    return true;
}

/* An equipment slot: the same gesture, refusing anything that does not belong
   there. Without the check the boots slot would accept a stack of sand, which
   is not a rule anybody should have to be told. */
static bool equipClick(int eqSlot, bool right) {
    ItemStack& eq = g_inv.equip[eqSlot];
    const int droneBay = eqSlot == EQ_LIGHT_DRONE ? 0 : eqSlot == EQ_DRONE_A ? 1 : eqSlot == EQ_DRONE_B ? 2 : -1;
    if (droneBay >= 0 && !eq.empty()) {
        for (int i = 0; i < Inventory::DRONE_MODULE_SLOTS_MAX; ++i)
            if (!g_inv.droneModule[droneBay][i].empty()) return false;
    }
    if (!g_drag.empty() && !equipFits(g_drag.item, eqSlot)) return false;
    /* Never split into or out of a worn slot: you wear one of a thing. */
    if (right) return false;
    return slotClick(eq, false);
}

static bool droneModuleClick(int droneBay, int slot, bool right) {
    if (right) return false;
    ItemStack& st = g_inv.droneModule[droneBay][slot];
    if (!g_drag.empty() && (ITEMS[g_drag.item].kind != ITEMK_DRONE_MODULE || g_drag.count != 1)) return false;
    return slotClick(st, false);
}

/* A module slot holds a bare ItemId rather than a stack, so the same rules are
   spelled out against one. Modules are unique and unstackable, which collapses
   the four cases to two. */
static bool moduleClick(ItemId& m, bool right) {
    if (right) return false;
    if (g_drag.empty()) {
        if (m == ITEM_NONE) return false;
        g_drag.item = m; g_drag.count = 1; g_drag.inst = 0;
        m = ITEM_NONE;
        return true;
    }
    if (ITEMS[g_drag.item].kind != ITEMK_MODULE || g_drag.count != 1) return false;
    const ItemId was = m;
    m = g_drag.item;
    if (was != ITEM_NONE) { g_drag.item = was; g_drag.count = 1; }
    else                  { g_drag.item = ITEM_NONE; g_drag.count = 0; }
    return true;
}

/* Put whatever the cursor is holding back in the pack. Called when the screen
   closes, because a stack on a cursor that is no longer drawn is a stack that
   has silently ceased to exist. If it will not fit it stays on the cursor and
   comes back with the screen, which is the only lossless answer. */
static void dragStow() {
    if (g_drag.empty()) return;
    for (int i = 0; i < INV_SLOTS && !g_drag.empty(); ++i) {
        ItemStack& st = g_inv.slot[i];
        if (st.empty() || (st.item == g_drag.item && st.inst == 0 && g_drag.inst == 0))
            slotClick(st, false);
    }
}

static void openChest(int index) {
    if (index < 0 || index >= MAX_DEVICES || !g_devices[index].used) return;
    Device& d = g_devices[index];
    g_chestOpen = index; g_devPanel = -1; g_logisticsUiOpen = true;
    g_chestStack.item = d.count ? (ItemId)d.mat : ITEM_NONE;
    g_chestStack.count = d.count; g_chestStack.inst = 0;
    const int x = PANEL_W + (VIEW_W - 560) / 2, y = (VIEW_H - 360) / 2;
    SetRect(&g_chestPanel, x, y, x + 560, y + 360);
    SetRect(&g_chestSlot, x + 54, y + 68, x + 106, y + 120);
    SetRect(&g_chestClose, x + 520, y + 10, x + 544, y + 32);
    for (int i = 0; i < INV_SLOTS; ++i) {
        const int c = i % HOTBAR_SLOTS, r = i / HOTBAR_SLOTS;
        const int rr = r == 0 ? INV_ROWS - 1 : r - 1;
        SetRect(&g_chestPack[i], x + 54 + c * 46, y + 170 + rr * 42,
                x + 94 + c * 46, y + 210 + rr * 42);
    }
}

static void closeChest() {
    if (g_chestOpen >= 0 && g_chestOpen < MAX_DEVICES && g_devices[g_chestOpen].used) {
        Device& d = g_devices[g_chestOpen];
        d.mat = g_chestStack.empty() ? (u8)MAT_EMPTY : (u8)g_chestStack.item;
        d.count = (i32)g_chestStack.count;
    }
    g_chestOpen = -1; g_logisticsUiOpen = false; dragStow();
}

static bool handleChestClick(int mx, int my, bool right) {
    if (g_chestOpen < 0) return false;
    if (inRect(g_chestClose, mx, my)) { closeChest(); return true; }
    if (inRect(g_chestSlot, mx, my)) { slotClick(g_chestStack, right); return true; }
    for (int i = 0; i < INV_SLOTS; ++i)
        if (inRect(g_chestPack[i], mx, my)) { slotClick(g_inv.slot[i], right); return true; }
    return true;
}

static void drawChestStack(HDC hdc, const RECT& r, const ItemStack& st) {
    FillRect(hdc, &r, inRect(r, g_mx, g_my) ? g_btnBgHot : g_btnBg);
    FrameRect(hdc, &r, g_borderBrush);
    if (st.empty()) return;
    RECT ir = r; ir.left += 3; ir.right -= 3; ir.top += 2; ir.bottom -= 11;
    drawItemIcon(hdc, ir, st.item);
    if (st.count > 1) { char s[20]; sprintf(s, "%u", (unsigned)st.count); RECT tr = r; tr.top = r.bottom - 12; tr.right -= 2; SetTextColor(hdc, RGB(210,216,224)); DrawTextA(hdc, s, -1, &tr, DT_RIGHT | DT_TOP | DT_SINGLELINE); }
}

static void drawChest(HDC hdc) {
    FillRect(hdc, &g_chestPanel, g_panelBg); FrameRect(hdc, &g_chestPanel, g_accentBrush);
    SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, RGB(245,224,150));
    RECT tr = g_chestPanel; tr.left += 18; tr.top += 12;
    DrawTextA(hdc, "CHEST", -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE);
    FillRect(hdc, &g_chestClose, inRect(g_chestClose, g_mx, g_my) ? g_btnBgHot : g_btnBg);
    FrameRect(hdc, &g_chestClose, g_borderBrush);
    DrawTextA(hdc, "x", -1, &g_chestClose, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SetTextColor(hdc, RGB(160,168,182)); tr.top = g_chestSlot.top - 20;
    DrawTextA(hdc, "CHEST STORAGE", -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE);
    drawChestStack(hdc, g_chestSlot, g_chestStack);
    tr.top = g_chestPack[0].top - 20; DrawTextA(hdc, "YOUR INVENTORY", -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE);
    for (int i = 0; i < INV_SLOTS; ++i) drawChestStack(hdc, g_chestPack[i], g_inv.slot[i]);
}

/* Returns true if the click was consumed, which while this is open is always:
   it is modal, and letting a click through to the world would paint under it. */
static void closeSignalPicker() {
    g_signalPickerDevice = -1;
    g_signalPickerField = CIR_PICK_NONE;
    g_creativeOpen = false;
    g_creSearchFocus = false;
}

/* Signal choice is a real selection task now, not an overloaded little cycle
   button. Reuse the creative browser because it already has the two things a
   material-rich circuit needs: every possible signal and a forgiving search. */
static void openCircuitSignalPicker(int device, CircuitPickField field) {
    if (device < 0 || device >= MAX_DEVICES || !g_devices[device].used) return;
    g_filterDevice = -1;
    g_signalPickerDevice = device;
    g_signalPickerField = field;
    g_creativeOpen = true;
    g_creSearch[0] = 0;
    g_creScroll = 0;
    g_creSearchFocus = true;
    layoutCreative();
}

static bool handleCreativeClick(int mx, int my, bool remove) {
    if (inRect(g_creSearchBox, mx, my)) { g_creSearchFocus = true; return true; }
    if (inRect(g_creClear, mx, my)) {
        if (g_signalPickerDevice >= 0) { closeSignalPicker(); return true; }
        if (g_filterDevice >= 0) {
            g_devices[g_filterDevice].value = 0;
            g_filterDevice = -1; g_creativeOpen = false; g_creSearchFocus = false;
            return true;
        }
        /* For the dig whitelist this button is "untick everything", not
           "cancel" -- the panel's own close is how you leave, and a set you
           spent time building should take a deliberate act to discard. */
        if (g_digFilterPicking) {
            memset(g_digFilterMat, 0, sizeof(g_digFilterMat));
            return true;
        }
        g_inv.clear();
        g_drag.item = ITEM_NONE; g_drag.count = 0; g_drag.inst = 0;
        layoutCreative();
        return true;
    }

    /* The scrollbar. Clicking the track pages toward the click, which is the
       behaviour of every scrollbar and needs no drag handling to be useful --
       and dragging a thumb across a modal panel is a second input mode for a
       list the wheel already scrolls. */
    if (inRect(g_creTrack, mx, my)) {
        if      (my < g_creThumb.top)    g_creScroll -= CRE_VIS_ROWS;
        else if (my > g_creThumb.bottom) g_creScroll += CRE_VIS_ROWS;
        layoutCreative();
        return true;
    }

    if (g_signalPickerDevice < 0) for (int i = 0; i < INV_SLOTS; ++i)
        if (inRect(g_packRect[i], mx, my)) {
            slotClick(g_inv.slot[i], remove);
            /* Picking a tool up or putting one down changes whether the bench
               exists, which changes the panel height. */
            layoutCreative();
            return true;
        }

    if (g_signalPickerDevice < 0) for (int i = 0; i < EQ_COUNT; ++i)
        if (inRect(g_eqRect[i], mx, my)) { equipClick(i, remove); layoutCreative(); return true; }

    if (g_signalPickerDevice < 0) for (int d = 0; d < MAX_DRONES; ++d)
        for (int i = 0; i < Inventory::DRONE_MODULE_SLOTS_MAX; ++i)
            if (inRect(g_droneModuleRect[d][i], mx, my)) { droneModuleClick(d, i, remove); layoutCreative(); return true; }

    /* The bin is an ORDINARY slot, clicked with the same slotClick every other
       slot uses -- which is what makes taking something back out work without a
       line of code for it. What makes it a bin is only that nothing refills it
       and nothing saves it.

       One thing it must do that a plain slot does not: return the tool instance
       of whatever it displaces. Dropping a multitool in and then a second item
       on top would otherwise leak the handle, and the pool is 32 deep -- the
       symptom is the 33rd multitool of a session arriving inert, with nothing
       anywhere connecting it to a bin. */
    if (g_signalPickerDevice < 0 && inRect(g_trashRect, mx, my)) {
        if (g_drag.empty()) {
            /* Empty hand: take back whatever is in there. This is the whole
               recovery affordance and it is an ordinary pick-up. */
            if (!g_trash.empty()) { g_drag = g_trash; g_trash = ItemStack(); }
        } else {
            /* Holding something: it goes in, and WHATEVER WAS THERE IS GONE.
               Not a swap, which is what reusing slotClick gave and which quietly
               made this a one-slot pocket -- put stone in, put dirt in, and the
               stone came back to your hand. Nothing was ever destroyed, so it
               was not a bin at all.

               Destroying on displacement is what makes "until you next use it"
               the recovery window: one undo, always available, never two. */
            if (g_trash.inst) toolInstFree(g_trash.inst);
            g_trash = g_drag;
            g_drag  = ItemStack();
        }
        layoutCreative();
        return true;
    }

    if (g_signalPickerDevice < 0 && g_toolPackSlot >= 0 && g_inv.slot[g_toolPackSlot].inst) {
        ToolInst& ti = g_toolInst[g_inv.slot[g_toolPackSlot].inst];
        for (int i = 0; i < g_toolSlotCount; ++i)
            if (inRect(g_toolSlotRect[i], mx, my)) { moduleClick(ti.slot[i], remove); return true; }
        /* The payload slot is a genuine ItemStack, so it speaks slotClick's
           ordinary lift/drop/merge/split language rather than moduleClick's
           unique-item swap -- loading ammunition is a pack interaction, not
           an installation. Only a MATERIAL may go here: dropping a tool or
           a module into your own ammunition slot is not a thing to allow
           quietly, so a non-material is simply refused rather than
           accepted and then doing something nobody asked for. */
        if (inRect(g_toolPayloadRect, mx, my)) {
            if (!g_drag.empty() && ITEMS[g_drag.item].kind != ITEMK_MATERIAL) return true;
            slotClick(ti.payload, remove);
            return true;
        }

    }

    for (int i = 0; i < g_creCount; ++i) {
        if (!inRect(g_creRect[i], mx, my)) continue;
        const int it = g_creItem[i];
        if (g_signalPickerDevice >= 0) {
            if (g_signalPickerDevice < MAX_DEVICES && g_devices[g_signalPickerDevice].used) {
                CircuitDeviceConfig& cc = g_circuitConfig[g_signalPickerDevice];
                if      (g_signalPickerField == CIR_PICK_SIGNAL) cc.signal = (u8)it;
                else if (g_signalPickerField == CIR_PICK_A)      cc.signalA = (u8)it;
                else if (g_signalPickerField == CIR_PICK_B)      cc.signalB = (u8)it;
                else if (g_signalPickerField == CIR_PICK_OUT)    cc.signalOut = (u8)it;
            }
            closeSignalPicker();
            return true;
        }
        if (g_filterDevice >= 0) {
            if (it < MAT_COUNT) g_devices[g_filterDevice].value = it;
            g_filterDevice = -1; g_creativeOpen = false; g_creSearchFocus = false;
            return true;
        }
        /* The dig whitelist TOGGLES and stays open, unlike every other picker
           here, which chooses one thing and closes. That difference is the
           feature: you are building a set, and a panel that shut after each
           choice would mean reopening it and re-searching for every material.
           Only real materials can be ticked -- a whitelist entry for a
           multitool would be a row you could click that could never match a
           cell. */
        if (g_digFilterPicking) {
            if (it > MAT_EMPTY && it < MAT_COUNT)
                g_digFilterMat[it] = !g_digFilterMat[it];
            return true;
        }
        if (remove) {
            /* Take everything, not one: the point of the right-click is to
               clear a slot out, and holding the button down to drain a hundred
               thousand sand one unit at a time is not a feature. */
            g_inv.take(it, 100000);
        } else if (g_drag.item == it && g_drag.inst == 0) {
            g_drag.count = ITEMS[it].maxStack;
        } else {
            /* Onto the CURSOR, not into the pack, so the palette obeys the same
               rule as everything else here and you can put the stack exactly
               where you want it.

               Whatever the cursor was carrying is discarded, which makes the
               palette the bin as well as the source. That is the only sensible
               meaning for "click an infinite supply while holding something",
               and it saves inventing a separate trash square. */
            if (g_drag.inst) toolInstFree(g_drag.inst);
            g_drag.item  = it;
            g_drag.count = ITEMS[it].maxStack;
            g_drag.inst  = (ITEMS[it].kind == ITEMK_TOOL) ? toolInstNew() : 0;
        }
        layoutCreative();
        return true;
    }
    return true;
}

/* ======================================================================
   Input
   ====================================================================== */
/* --- the camera ----------------------------------------------------------
   Centres on the character, eased rather than snapped, and clamped so the
   window never leaves the world.

   Eased because a camera welded to the character makes the WORLD the thing
   that moves, and at this pixel density that reads as the terrain sliding
   about rather than as walking. A lag of a few frames keeps the character
   near the middle while letting the background stay still enough to be a
   background.

   Snapping past a threshold matters as much as the easing: without it a
   respawn or a fall across half the world would take several seconds to
   catch up, during which the character is off screen entirely. */
static void updateCamera(bool snap) {
    float tx = g_camXf, ty = g_camYf;
    if (g_playerOn) {
        tx = g_player.centreX() - viewCellsW() * 0.5f;
        ty = g_player.centreY() - viewCellsH() * 0.5f;
    }

    /* Clamp the TARGET, not the eased position, so easing never has to chase a
       point outside the world and stall against the edge. */
    const float maxX = (float)(SIM_W - viewCellsW());
    const float maxY = (float)(SIM_H - viewCellsH());
    if (tx < 0.0f) tx = 0.0f;
    if (tx > maxX) tx = maxX;
    if (ty < 0.0f) ty = 0.0f;
    if (ty > maxY) ty = maxY;

    const float dx = tx - g_camXf, dy = ty - g_camYf;
    const float far2 = dx * dx + dy * dy;
    if (snap || far2 > (float)(viewCellsW() * viewCellsW())) {
        g_camXf = tx; g_camYf = ty;
    } else {
        const float EASE = 0.16f;
        g_camXf += dx * EASE;
        g_camYf += dy * EASE;
        /* Settle exactly, or the camera creeps by fractions forever and the
           world jitters by a pixel while the character stands still. */
        if (dx > -0.5f && dx < 0.5f) g_camXf = tx;
        if (dy > -0.5f && dy < 0.5f) g_camYf = ty;
    }
    g_camX = (int)g_camXf;
    g_camY = (int)g_camYf;
}

/* Pan the camera by hand. Only reachable with the character switched off,
   where the arrow keys have nothing else to do -- the sandbox this grew out of
   still has to be usable, and in a world sixteen screens across it is not
   without some way to get about. */
static void panCamera(float dx, float dy) {
    g_camXf += dx; g_camYf += dy;
    const float maxX = (float)(SIM_W - viewCellsW());
    const float maxY = (float)(SIM_H - viewCellsH());
    if (g_camXf < 0.0f) g_camXf = 0.0f;
    if (g_camXf > maxX) g_camXf = maxX;
    if (g_camYf < 0.0f) g_camYf = 0.0f;
    if (g_camYf > maxY) g_camYf = maxY;
    g_camX = (int)g_camXf; g_camY = (int)g_camYf;
}

static void cycleView()      { g_view = (g_view + 1) % VIEW_COUNT; }
static void changeSize(int d){ g_brushRadius = imax(1, imin(64, g_brushRadius + d)); }
static void changeZoom(int d) {
    const int oldW = viewCellsW(), oldH = viewCellsH();
    const int next = imax(0, imin(N_ZOOM - 1, g_zoomIdx + d));
    if (next == g_zoomIdx) return;

    /* Preserve the point at the centre of the screen when freely panning.
       With the character enabled updateCamera() then deliberately recentres on
       the character, which is the less surprising anchor while walking. */
    const float cx = g_camXf + oldW * 0.5f, cy = g_camYf + oldH * 0.5f;
    g_zoomIdx = next;
    g_camXf = cx - viewCellsW() * 0.5f;
    g_camYf = cy - viewCellsH() * 0.5f;
    updateCamera(g_playerOn);
    sprintf(g_saveMsg, "Zoom %dx  (+/-)", zoomFactor());
    g_saveMsgFrames = 120;
}

/* What a new character starts holding.

   Two items, and each one closes a hole that made the opening unplayable rather
   than merely hard. Without the Bolt Caster nothing living could be hurt at all
   -- damage lives on modules, modules need a Multitool, and both need copper --
   so the first descent had no answer to a rock mite. Without the striker there
   was no way to light a fire, and every thermal step in the game is downstream
   of that.

   Added rather than assigned, and only when absent, so using the Clear button
   on a world you have been playing does not quietly duplicate them. */
static void giveStartingKit() {
    if (g_inv.countOf(ITEM_BOLTER) == 0) g_inv.add(ITEM_BOLTER, 1);
    if (g_inv.countOf(ITEM_FLINT)  == 0) g_inv.add(ITEM_FLINT, 1);
}

/* Builds the world. See worldgen.cpp -- plains to the left, a mountain to the
   right, and the flats beyond it. */
static void makeWorld() {
    /* Machines are entities beside the grid, so clearing the world does not clear
       them -- they have to be dropped explicitly or a fresh world arrives haunted
       by the last one's contraptions. Same reason roomsClear() exists. */
    devClear();
    g_restBed = -1;
    sparkClear();
    /* Creatures are transient (see entity.h) but they are not automatically
       gone: they live in a global array beside the grid, exactly like devices,
       so a fresh world would otherwise arrive with the last one's wildlife
       standing in mid-air where its floor used to be. */
    entReset();
    droneReset();
    accessoryReset();
    /* A new world has not been explored. Without this a Clear leaves the old
       world's map showing, which is worse than a blank one: it is a confident
       picture of somewhere that no longer exists. */
    mapClear();
    generateWorld(g_world);
    /* Generation writes cells straight into the grid and reset() has just
       emptied the dirty lists, so there is no record of anything having
       changed -- the lighting would happily go on reusing a field describing
       the world this one replaced. */
    lightInvalidate();
    giveStartingKit();
    /* Generation rebuilds every cell, so any room that existed described a
       building that no longer does. Nothing else clears them: a room outlives
       everything short of a new world. */
    roomsClear(g_world);
}

/* Returns true if the click was consumed by a panel control. */
static bool handlePanelClick(int mx, int my) {
    /* While the menu is up it takes every click, including ones over the
       viewport -- otherwise you paint into the world through the overlay. */
    if (g_menuOpen) {
        if (inRect(g_menuResume, mx, my)) { g_menuOpen = false; return true; }
        if (inRect(g_menuQuit,   mx, my)) { g_running = false; PostQuitMessage(0); return true; }
        return true;
    }
    if (g_survival && g_playerOn) {
        for (int i = 0; i < HOTBAR_SLOTS; ++i)
            if (inRect(g_hotRect[i], mx, my)) { g_inv.selected = i; return true; }
    }
    for (int i = 0; i < N_PALETTE; ++i) {
        if (!inRect(g_paletteRect[i], mx, my)) continue;
        if (i < N_BRUSH) {
            g_brushMat = BRUSHES[i].brush;
            g_paletteDevice = -1;
        } else {
            g_paletteDevice = i - N_BRUSH;
        }
        return true;
    }
    if (inRect(g_paletteScrollTrack, mx, my) && g_paletteMaxScroll > 0) {
        const int trackH = g_paletteScrollTrack.bottom - g_paletteScrollTrack.top;
        const int thumbH = g_paletteScrollThumb.bottom - g_paletteScrollThumb.top;
        const int travel = imax(1, trackH - thumbH);
        int at = my - g_paletteScrollTrack.top - thumbH / 2;
        at = imax(0, imin(at, travel));
        g_paletteScroll = (at * g_paletteMaxScroll + travel / 2) / travel;
        layoutPanel();
        return true;
    }
    for (int i = 0; i < N_SPEED; ++i) {
        if (inRect(g_speedRect[i], mx, my)) { g_speedIdx = i; return true; }
    }
    for (int i = 0; i < N_ZOOM; ++i) {
        if (inRect(g_zoomRect[i], mx, my)) { changeZoom(i - g_zoomIdx); return true; }
    }
    if (inRect(g_sizeDec, mx, my)) { changeSize(-1); return true; }
    if (inRect(g_sizeInc, mx, my)) { changeSize(+1); return true; }
    if (inRect(g_actRect[ACT_OVERWRITE], mx, my)) { g_overwrite = !g_overwrite; return true; }
    if (inRect(g_actRect[ACT_LAYER],     mx, my)) { g_bgLayer   = !g_bgLayer;   return true; }
    if (inRect(g_actRect[ACT_WIRE],      mx, my)) { g_wireMode  = !g_wireMode;  return true; }
    if (inRect(g_actRect[ACT_CIRCUIT],   mx, my)) {
        g_circuitWireMode = !g_circuitWireMode;
        g_circuitWireFrom = -1; g_circuitWireFromPort = 0;
        return true;
    }
    if (inRect(g_actRect[ACT_VIEW],      mx, my)) { cycleView();                return true; }
    if (inRect(g_actRect[ACT_LIGHT],     mx, my)) { g_lightOn  = !g_lightOn;    return true; }
    if (inRect(g_actRect[ACT_PLAYER],    mx, my)) {
        g_playerOn = !g_playerOn;
        /* Into the middle of the VIEW, not the middle of the world -- switching
           the character on should put them where you are looking. */
        if (g_playerOn) {
            g_player.reset((float)(g_camX + viewCellsW() / 2),
                           (float)(g_camY + viewCellsH() / 2));
            updateCamera(true);
        }
        return true;
    }
    if (inRect(g_actRect[ACT_PAUSE],     mx, my)) { g_paused = !g_paused;       return true; }
    if (inRect(g_actRect[ACT_FILTER],    mx, my)) {
        /* On -> off is just off. Off -> on opens the picker, because a filter
           with nothing ticked would silently stop you digging at all, and a
           tool that does nothing and does not say why is worse than no tool.
           Re-opening when a selection already exists is deliberate too: the
           common case is "turn it on and add one more material". */
        if (g_digFilterOn) {
            g_digFilterOn = false;
            g_digFilterPicking = false;
            g_creativeOpen = false;
            g_creSearchFocus = false;
        } else {
            g_digFilterOn = true;
            g_digFilterPicking = true;
            g_filterDevice = -1;
            g_signalPickerDevice = -1;
            g_creativeOpen = true;
            g_creSearch[0] = 0;
            g_creScroll = 0;
            g_creSearchFocus = true;
            layoutCreative();
        }
        return true;
    }
    if (inRect(g_actRect[ACT_CLEAR],     mx, my)) { g_world.reset(); makeWorld(); return true; }
    /* Any other spot on the panel is dead space: swallow it so it never paints. */
    return mx < PANEL_W;
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DESTROY:
    case WM_CLOSE:
        g_running = false;
        PostQuitMessage(0);
        return 0;

    case WM_ERASEBKGND:
        return 1;   /* we repaint every pixel ourselves; skip the flicker */

    case WM_MOUSEMOVE:
        g_mx = (short)LOWORD(lp);
        g_my = (short)HIWORD(lp);
        return 0;

    case WM_CHAR:
        if (g_creativeOpen && g_creSearchFocus) {
            const char ch = (char)wp;
            int n = (int)strlen(g_creSearch);
            if (ch == '\b' && n > 0) g_creSearch[n - 1] = 0;
            else if (ch >= 32 && ch < 127 && n < (int)sizeof(g_creSearch) - 1) {
                g_creSearch[n] = ch; g_creSearch[n + 1] = 0;
            }
            g_creScroll = 0; layoutCreative();
            return 0;
        }
        break;

    case WM_LBUTTONDOWN:
        g_mx = (short)LOWORD(lp);
        g_my = (short)HIWORD(lp);
        if (g_chestOpen >= 0) { handleChestClick(g_mx, g_my, false); g_uiCapture = true; }
        else if (g_creativeOpen) { handleCreativeClick(g_mx, g_my, false); g_uiCapture = true; }
        /* The device panel floats over the world, so it has to swallow the click
           before the world does -- otherwise nudging a setpoint also digs a hole
           in whatever is behind the button. */
        else if (handleCraftClick(g_mx, g_my)) g_uiCapture = true;
        else if (handleDevPanelClick(g_mx, g_my)) g_uiCapture = true;
        else if (handlePanelClick(g_mx, g_my)) g_uiCapture = true;
        else if (g_circuitWireMode) {
            circuitWireClick();
            g_uiCapture = true;
        } else {
            g_lmb = true;
            if (g_wireMode) startWire(); else startLine();
        }
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        commitWire();       /* before the button clears -- see commitLine */
        commitLine();
        g_lmb = false; g_useLatch = false; g_uiCapture = false; ReleaseCapture(); return 0;
    case WM_RBUTTONDOWN:
        g_mx = (short)LOWORD(lp);
        g_my = (short)HIWORD(lp);
        if (g_chestOpen >= 0)     handleChestClick(g_mx, g_my, true);
        else if (g_creativeOpen)  handleCreativeClick(g_mx, g_my, true);
        else if (g_mx >= PANEL_W) {
            /* Right-click INTERACTS with a machine and DIGS everywhere else.
               Ordered this way round rather than needing a modifier because the
               two are never ambiguous -- a device is a solid object you can see,
               and "poke the thing under the cursor" is what a right-click means
               in every game with machines in it. The cost is that you cannot
               right-click-dig a device out; the tool does that, and losing a
               contraption to a stray drag would be far worse. */
            const Aim a = currentAim();
            Device* d = devAt(a.x, a.y);
            if (d) {
                const int idx = (int)(d - g_devices);
                if (d->type == DEV_BED) {
                    /* A bed is used from beside it, rather than as a remote
                       clock control. The loose box covers standing on either
                       side and on top, but not reaching across a room. */
                    const float px = g_player.centreX(), py = g_player.centreY();
                    const bool atBed = px >= d->x - 4 && px <= d->x + DEV_W + 4 &&
                                       py >= d->y - 8 && py <= d->y + DEV_H + 8;
                    if (g_restBed == idx) {
                        g_restBed = -1;
                    } else if (g_playerOn && atBed) {
                        g_restBed = idx;
                    } else {
                        sprintf(g_saveMsg, "Stand by the bed to rest");
                        g_saveMsgFrames = 120;
                    }
                    break;
                }
                if (d->type == DEV_CHEST) { openChest((int)(d - g_devices)); break; }
                if (d->type == DEV_PULSE_BUTTON) { d->poked = true; break; }
                /* Toggle: clicking the same machine again closes it. */
                g_devPanel = (g_devPanel == idx) ? -1 : idx;
            } else if (doorToggle(g_world, a.x, a.y)) {
                /* A door is the other thing you poke rather than dig, and it
                   sits here for the same reason a machine does: right-click
                   means "operate the thing under the cursor" wherever there is
                   a thing under the cursor.

                   Ahead of the dig branch and BELOW the device branch, so a
                   door behind a machine still belongs to the machine. Note the
                   drag flag is deliberately not set -- a door opens on the
                   click, and a right-DRAG across a row of them should not flap
                   every one it crosses. Digging a door out is the tool's job,
                   exactly as it is for a device.

                   Requires a real door under the cursor: doorToggle returns 0
                   for anything else, so ordinary right-click digging is
                   untouched everywhere in the world that is not a door. */
                roomsNotifyEdit(g_world, a.x, a.y);
            } else {
                g_devPanel = -1;
                g_rmb = true;   /* right-drag digs, but only over the sim */
                startLine();
            }
        }
        SetCapture(hwnd);
        return 0;
    case WM_RBUTTONUP:
        commitLine();
        g_rmb = false; ReleaseCapture(); return 0;

    case WM_SETCURSOR:
        /* Hide the arrow over the playfield so the crosshair is the only
           pointer there. Still the system cursor over the panel and the menu,
           where you are clicking buttons rather than aiming. */
        /* Every modal screen has to be listed, and the crafting menu was
           missing -- so opening it left the playfield's hidden cursor in
           force and there was nothing to click its rows with. */
        if (LOWORD(lp) == HTCLIENT && g_mx >= PANEL_W
            && !g_menuOpen && !g_creativeOpen && !g_craftOpen && g_chestOpen < 0) {
            SetCursor(NULL);
            return TRUE;
        }
        break;

    case WM_MOUSEWHEEL: {
        const int dir = (short)HIWORD(wp) > 0 ? 1 : -1;
        /* While the item grid is up the wheel scrolls IT, and nothing else.
           A wheel that quietly changed which hotbar slot was selected while you
           were reading a list would be a wheel you learn not to touch. Three
           rows a notch, which is about a third of the window -- one row a notch
           is a long way from Stone to Wheat Seed. */
        if (g_creativeOpen) {
            g_creScroll -= dir * 3;
            layoutCreative();
            return 0;
        }
        /* Same three-row stride as the creative palette, and for the same
           reason: one row a notch is a long way from the torch recipe to
           the last combinator now that there are seventy of them. */
        if (g_craftOpen) {
            g_craftScroll -= dir * 3;
            layoutCraft();
            return 0;
        }
        /* The left catalog owns the wheel while the pointer is over it.  This
           keeps browsing devices from quietly changing a hotbar slot or brush
           size, and matches the creative inventory's three-row stride. */
        if (inRect(g_paletteArea, g_mx, g_my) || inRect(g_paletteScrollTrack, g_mx, g_my)) {
            g_paletteScroll = imax(0, imin(g_paletteMaxScroll, g_paletteScroll - dir * 3));
            layoutPanel();
            return 0;
        }
        /* The wheel picks what you are holding, which is what it does in every
           game with a hotbar, and Q turns it into a size dial. Sizing is the
           rarer action once you are playing rather than drawing, so it is the
           one that takes a modifier -- but it stays on the bare wheel whenever
           the hotbar is not on screen, since then there is nothing to select. */
        const bool hotbarUp = g_survival && g_playerOn;
        if (!hotbarUp || (GetAsyncKeyState('Q') & 0x8000)) {
            changeSize(dir);
        } else {
            /* Up scrolls left along the bar, matching the usual convention. */
            g_inv.selected = (g_inv.selected - dir + HOTBAR_SLOTS) % HOTBAR_SLOTS;
        }
        return 0;
    }

    case WM_KEYDOWN:
        /* Inventory screens own the keyboard. This matters particularly for
           search: typing "iron" used to send I through to the gameplay toggle
           and silently hide the survival hotbar underneath the open inventory. */
        if (g_creativeOpen || g_chestOpen >= 0) {
            if (wp == VK_ESCAPE) {
                if (g_chestOpen >= 0) closeChest();
                else { g_creativeOpen = false; g_filterDevice = -1; g_digFilterPicking = false; g_signalPickerDevice = -1; g_signalPickerField = CIR_PICK_NONE; g_creSearchFocus = false; dragStow(); }
            } else if (wp == VK_TAB && g_creativeOpen) {
                g_creativeOpen = false; g_filterDevice = -1; g_digFilterPicking = false; g_signalPickerDevice = -1; g_signalPickerField = CIR_PICK_NONE; g_creSearchFocus = false; dragStow();
            }
            return 0;
        }
        /* Shortcuts still work -- they are just no longer the only way in. */
        switch (wp) {
        /* The number row now selects hotbar slots rather than materials. The
           palette has had buttons for a long time and the shortcuts were
           vestigial; a game wants 1-9 on the thing you are carrying. */
        case '1': case '2': case '3': case '4': case '5':
        case '6': case '7': case '8': case '9':
            g_inv.selected = (int)(wp - '1');
            break;
        case '0': g_inv.selected = 9; break;
        case 'M': g_brushMat = MAT_COPPER;   g_paletteDevice = -1; break;   /* M for metal */
        case 'F': g_wireMode = !g_wireMode;   break;   /* F for wire feed */
        case 'X': g_circuitWireMode = !g_circuitWireMode; g_circuitWireFrom = -1; g_circuitWireFromPort = 0; break;
        case 'G': g_brushMat = MAT_GRAPHENE; g_paletteDevice = -1; break;
        case 'B': g_brushMat = MAT_WALL;     g_paletteDevice = -1; break;
        case 'E': g_brushMat = MAT_EMPTY;    g_paletteDevice = -1; break;
        case 'I': g_survival = !g_survival; break;
        case 'H': g_brushMat = TOOL_HEAT; g_paletteDevice = -1; break;
        case 'J': g_brushMat = TOOL_COOL; g_paletteDevice = -1; break;
        /* C crafts. Regenerating the world moved to N, which it should
           arguably always have been -- C for "clear" was a sandbox verb from
           before there was a game to be in the middle of, and losing your world
           to a mistyped craft key would be unforgivable. */
        case 'C':
            g_craftOpen = !g_craftOpen;
            if (g_craftOpen) { layoutCraft(); g_lmb = g_rmb = false; }
            break;
        case 'N': g_world.reset(); makeWorld(); break;

        /* F5 saves, F9 loads -- the pair every game has used for thirty years,
           and deliberately not on letters: the letters are all brush and tool
           shortcuts, and losing your world to a mistyped one is the failure
           this whole feature exists to prevent. */
        case VK_F5: {
            const bool ok = saveWrite(SAVE_PATH, g_world);
            if (ok) {
                double mb = (double)saveTotalBytes() / (1024.0 * 1024.0);
                sprintf(g_saveMsg, "Saved %s -- %.2f MB", SAVE_PATH, mb);
            } else {
                sprintf(g_saveMsg, "SAVE FAILED: %s", saveError());
            }
            g_saveMsgFrames = 240;
            break;
        }
        case VK_F9: {
            const bool ok = saveRead(SAVE_PATH, g_world);
            if (ok) {
                double mb = (double)saveTotalBytes() / (1024.0 * 1024.0);
                sprintf(g_saveMsg, "Loaded %s -- %.2f MB%s%s", SAVE_PATH, mb,
                        saveError()[0] ? " -- " : "", saveError());
                updateCamera(true);
                droneReset();
                accessoryReset();
            } else {
                /* A failed load leaves the world half-written, so it is not
                   somewhere to carry on from. Regenerating is the honest
                   recovery and it is better than a plausible-looking ruin. */
                sprintf(g_saveMsg, "LOAD FAILED: %s", saveError());
                g_world.reset(); makeWorld();
            }
            g_saveMsgFrames = 240;
            break;
        }
        /* R HELD is the line tool; R TAPPED still respawns at the cursor. The
           respawn moved to key-up so the two can share one key -- see the note
           on g_lineKey. Auto-repeat means this arrives many times while held,
           so the flags are set rather than toggled. */
        case 'R':
            g_lineKey = true;
            break;
        case 'O': g_overwrite = !g_overwrite; break;
        case 'L': g_bgLayer   = !g_bgLayer;   break;   /* L for layer */
        case 'V': cycleView();                break;
        case 'K': g_lightOn = !g_lightOn;     break;   /* K for keep the lights on */
        /* Space jumps. Pause moved to P -- in a sandbox you are drawing in,
           space-to-pause is the obvious binding; the moment there is a
           character to control it is the obvious binding for something else,
           and every player will try it. The panel button still pauses too. */
        case 'P': g_paused = !g_paused; break;
        /* T for the full map, Z for the corner one.

           Q first, and Q was wrong: it is already held for brush resize and the
           brush outline, through GetAsyncKeyState rather than a case label --
           so a scan of `case` labels reported it free and pressing it did both
           things at once. Held keys and pressed keys are two registers of
           bindings in this file and only one of them is greppable; check both.

           Not M either, which every game with a map uses and which is taken
           here by the copper brush. Moving an existing binding to get the nicer
           letter is available if wanted, but it is not this feature's call.

           Opening the full map re-centres it on the player: a map you have to
           find yourself on every time is a map you stop opening. */
        case 'T': g_mapOpen = !g_mapOpen; if (g_mapOpen) g_mapPanned = false; break;
        case 'Z': g_miniOn = !g_miniOn; break;
        case VK_OEM_PLUS: case VK_ADD:      changeZoom(+1); break;
        case VK_OEM_MINUS: case VK_SUBTRACT: changeZoom(-1); break;
        case VK_OEM_PERIOD: g_stepOnce = true; break;
        case VK_OEM_4: changeSize(-1); break;  /* [ */
        case VK_OEM_6: changeSize(+1); break;  /* ] */
        case VK_TAB:
            g_creativeOpen = !g_creativeOpen;
            if (g_creativeOpen) { g_creSearchFocus = true; layoutCreative(); g_lmb = g_rmb = false; }
            else { g_filterDevice = -1; g_digFilterPicking = false; g_signalPickerDevice = -1; g_signalPickerField = CIR_PICK_NONE; g_creSearchFocus = false; dragStow(); }
            break;
        /* Escape backs out of the creative grid before it reaches the pause
           menu -- one key that always means "close the thing in front of me" is
           worth more than a second binding to remember. */
        case VK_ESCAPE:
            if (g_mapOpen)           g_mapOpen = false;
            else if (g_chestOpen >= 0) closeChest();
            else if (g_craftOpen)    g_craftOpen = false;
            else if (g_creativeOpen) { g_creativeOpen = false; g_filterDevice = -1; g_digFilterPicking = false; g_signalPickerDevice = -1; g_signalPickerField = CIR_PICK_NONE; g_creSearchFocus = false; dragStow(); }
            else                     g_menuOpen = !g_menuOpen;
            break;
        }
        return 0;

    case WM_KEYUP:
        if (wp == 'R') {
            /* A TAP of R respawns. A HOLD that drew a line does not, because
               teleporting to the cursor every time you finish drawing a wall
               would be an unforgettable way to lose your place. Note this fires
               on the release rather than the press: with the key doing two jobs
               there is nothing to act on until it is known which one it was. */
            if (!g_lineDrew && g_mx >= PANEL_W && !g_menuOpen && !g_creativeOpen)
                g_player.reset((float)((g_mx - PANEL_W) / cellPixels() + g_camX),
                               (float)(g_my / cellPixels() + g_camY));
            g_lineKey = false;
            g_lineDrew = false;
            /* A drag still in progress when R comes up is committed rather than
               dropped -- the line was already anchored, and letting go of the
               modifier mid-drag should not throw away the work. */
            commitLine();
            g_lineDrew = false;
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* Paint along the segment the mouse covered since last frame, so a fast drag
   lays down a continuous stroke instead of dotted blobs. Window x maps to a
   cell by subtracting the panel and dividing by the scale. */
/* Where the tool actually acts, and where the mouse actually is.

   Past the character's reach the aim point is clamped to the reach circle,
   along the line from the body's centre to the cursor -- so it slides around
   the boundary staying as close to the mouse as it can rather than sticking in
   one place. That is a plain vector clamp: take the offset, and if it is longer
   than the reach, rescale it to exactly the reach. Landing on the line between
   the two falls out of the arithmetic rather than needing to be arranged.

   `ghost` is the raw mouse position, drawn faintly so the relationship between
   the two is legible -- without it a cursor that stops moving reads as the
   window having lost focus. */
static Aim currentAim() {
    Aim a;
    /* Window pixel -> view cell -> WORLD cell. Everything downstream of here
       works in world cells, because that is what the tools act on. */
    a.ghostX = (g_mx - PANEL_W) / cellPixels() + g_camX;
    a.ghostY = g_my / cellPixels() + g_camY;
    a.x = a.ghostX; a.y = a.ghostY;
    a.clamped = false;

    /* Unlimited in the sandbox: reach is a rule about a character, and with no
       character on screen it would just be an obstacle to drawing. */
    if (!g_survival || !g_playerOn) return a;

    const float pcx = g_player.centreX(), pcy = g_player.centreY();
    const float dx = (float)a.ghostX - pcx, dy = (float)a.ghostY - pcy;
    const float d2 = dx * dx + dy * dy;
    const float R  = (float)currentReach();
    if (d2 <= R * R) return a;

    const float d = sqrtf(d2);
    a.x = (int)(pcx + dx * (R / d));
    a.y = (int)(pcy + dy * (R / d));
    a.clamped = true;
    return a;
}

/* Wiring is normally aiming for an existing terminal or wire, not the empty
   cell beside it. Within four cells, choose the nearest conductive cell as the
   actual endpoint. The tight radius makes it a deliberate finishing assist,
   rather than a general magnet that pulls a long wire toward a metal wall. */
static Aim currentWireAim() {
    Aim a = currentAim();
    static const int SNAP = 4;
    int bestD2 = SNAP * SNAP + 1;
    int bestX = a.x, bestY = a.y;
    for (int y = imax(PLAY_Y0, a.y - SNAP); y <= imin(PLAY_Y1, a.y + SNAP); ++y) {
        for (int x = imax(PLAY_X0, a.x - SNAP); x <= imin(PLAY_X1, a.x + SNAP); ++x) {
            const int dx = x - a.x, dy = y - a.y, d2 = dx * dx + dy * dy;
            if (d2 >= bestD2 || !g_matConducts[g_world.at(x, y).mat]) continue;
            bestD2 = d2; bestX = x; bestY = y;
        }
    }
    a.x = bestX; a.y = bestY;
    return a;
}

static void circuitWireClick() {
    const Aim a = currentAim();
    Device* d = devAt(a.x, a.y);
    if (!d) { g_circuitWireFrom = -1; g_circuitWireFromPort = 0; return; }
    const int index = (int)(d - g_devices);
    const int port = circuitHasSeparatePorts(d->type) && a.x >= d->x + DEV_W / 2 ? 1 : 0;
    if (g_circuitWireFrom < 0) { g_circuitWireFrom = index; g_circuitWireFromPort = port; return; }
    circuitToggleWirePorts(g_circuitWireFrom, g_circuitWireFromPort, index, port);
    g_circuitWireFrom = -1; g_circuitWireFromPort = 0;
}

/* Frames until the hands can take another bite. Counted down every frame
   whether or not you are digging, so the first click after a pause acts
   immediately rather than waiting out a stale cooldown. */
static int g_digCool = 0;

/* --- firing ---------------------------------------------------------------
   Direction comes from the aim point, and it is worth noticing that the CLAMPED
   aim gives the same answer as the raw mouse would: the clamp rescales the
   offset from the body's centre without rotating it, so both lie on one line
   through the player. So the crosshair always points where the shot goes, even
   though the shot itself is not limited by reach -- a thrown thing does not care
   how far your arm is. */
static void fireTool(const Aim& aim) {
    ItemStack& h = g_inv.held();
    if (h.empty() || ITEMS[h.item].kind != ITEMK_TOOL || h.inst == 0) return;

    const ToolShot s = toolResolve(h);
    if (!s.canFire) return;                       /* a tool with no modules */

    ToolInst& ti = g_toolInst[h.inst];
    if (ti.cooldown > 0) return;
    ti.cooldown = accessoryShotDelay(g_inv, s.delay);

    const float pcx = g_player.centreX(), pcy = g_player.centreY();
    float dx = (float)aim.x - pcx, dy = (float)aim.y - pcy;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) { dx = 1.0f; dy = 0.0f; d = 1.0f; }   /* aimed at own feet */
    dx /= d; dy /= d;

    /* Start clear of the body. Spawning at the centre would put the shot inside
       whatever the character is standing in, so firing while waist-deep in sand
       would spend the whole shot on the cells around your own legs. */
    const float MUZZLE = PLAYER_H * 0.5f + 2.0f;
    /* Speed comes from the shot now rather than being one constant here, which
       is what lets a flat bolt and a lobbed grenade exist under one gravity.
       Note the aim is NOT compensated for drop: the reticle marks where you
       pointed, not where the shot lands, and learning that gap is the skill the
       arc adds. Auto-correcting it would mean adding gravity and then hiding
       every consequence of it. */
    const float SPEED  = s.speed;
    /* The payload is consumed HERE, on firing, not on impact -- a shot that
       missed everything and fizzled out over open sky still cost its LN2,
       the same way a mining tool spends bites whether or not it hit
       anything worth digging. Confirmed against the pack's actual count
       rather than trusted from the resolved ToolShot, which only reports
       there being SOME payload loaded, not how much. */
    int payload = MAT_EMPTY;
    if (s.payloadMat != MAT_EMPTY && ti.payload.count > 0) {
        payload = s.payloadMat;
        if (--ti.payload.count == 0) { ti.payload.item = ITEM_NONE; ti.payload.inst = 0; }
    }
    const float vx = dx * SPEED, vy = dy * SPEED;
    const bool fired = projSpawn(pcx + dx * MUZZLE, pcy + dy * MUZZLE, vx, vy,
                                 s.power, s.pierce, 90, s.colour, s.blast,
                                 payload, s.damage, false, s.gravity);
    if (fired && accessoryTwinShot(g_inv)) {
        /* Duplicate the command, as the drone controller does. One payload was
           spent above; the accessory rewards the slot with a second delivery,
           fanned just enough that both projectiles remain individually visible. */
        const float fanX = -vy * 0.10f, fanY = vx * 0.10f;
        projSpawn(pcx + dx * MUZZLE, pcy + dy * MUZZLE,
                  vx + fanX, vy + fanY, s.power, s.pierce, 90, 0xD8A4FF,
                  s.blast, payload, s.damage, false, s.gravity);
    }
}

/* A Glowflare is ammunition in its own container, not a durable weapon. It
   therefore owns no ToolInst and fires once per click from an ordinary stack.
   Returning projSpawn's answer matters: a saturated projectile pool has not
   actually used the flare, so it must not quietly consume one. */
static bool throwGlowflare(const Aim& aim) {
    const ItemStack& h = g_inv.held();
    if (h.empty() || h.item != ITEM_GLOW_FLARE) return false;
    const ItemDef& d = ITEMS[h.item];

    const float pcx = g_player.centreX(), pcy = g_player.centreY();
    float dx = (float)aim.x - pcx, dy = (float)aim.y - pcy;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) { dx = 1.0f; dy = 0.0f; len = 1.0f; }
    dx /= len; dy /= len;

    const float muzzle = PLAYER_H * 0.5f + 2.0f;
    const float speed = d.shotSpeed > 0.0f ? d.shotSpeed : SHOT_SPEED_DEFAULT;
    return projSpawn(pcx + dx * muzzle, pcy + dy * muzzle,
                     dx * speed, dy * speed,
                     d.power, d.pierce, 90, d.shotColour, d.blast,
                     MAT_GLOWFLUID, d.damage, false,
                     d.shotBeam ? 0.0f : PROJ_GRAVITY,
                     PROJ_EFFECT_GLOWFLARE);
}

/* Devices are discrete, but a drag is still a useful way to lay out a run. Walk
   every crossed cell so a fast pipe stroke cannot skip a lattice slot. devPlace
   rejects overlapping footprints; charging only on success makes that rejection
   free and lets a stroke safely pass across devices already in place. */
static void placeDeviceStroke(u8 type, bool consume, const Aim& aim) {
    const int x0 = g_pmx, y0 = g_pmy;
    const int steps = imax(abs(aim.x - x0), abs(aim.y - y0));
    for (int s = 0; s <= steps; ++s) {
        const int x = steps ? x0 + (aim.x - x0) * s / steps : aim.x;
        const int y = steps ? y0 + (aim.y - y0) * s / steps : aim.y;
        if (devPlace(g_world, type, x, y) && consume) g_inv.take(g_inv.held().item, 1);
        if (consume && g_inv.held().empty()) break;
    }
    g_pmx = aim.x; g_pmy = aim.y;
}

static void applyBrush() {
    if (g_uiCapture || (!g_lmb && !g_rmb)) { g_pmx = -1; return; }

    /* A line drag lays nothing down until it is released. The anchor is already
       in g_pmx/g_pmy (set on the press), and leaving it there is what makes the
       committing call draw a straight line rather than the path the mouse
       wandered along. */
    if (g_lineOn || g_wireOn) return;

    /* Stroke interpolation runs between successive CLAMPED points, not raw
       mouse positions. Interpolating the raw ones and clamping afterwards would
       draw a straight line through the middle of the reach circle whenever the
       cursor swung around the outside of it. */
    const Aim aim = currentAim();
    if (g_pmx < 0) { g_pmx = aim.x; g_pmy = aim.y; }

    /* --- digging by hand is rate-limited, and deliberately does NOT stroke ---
       Once there is a per-frame cell budget, interpolating a drag is
       meaningless: the budget would be spent at the first interpolated point
       and the rest of the stroke would do nothing, so a fast drag would chew a
       hole where the mouse WAS rather than where it is. Nibbling at the current
       aim point is both simpler and what it looks like it should do. */
    /* A throwable consumes one item per deliberate click. It precedes tools
       because it is intentionally NOT a unique stateful tool: that distinction
       is what lets a stack split and merge without duplicating an instance. */
    if (g_survival && g_playerOn && g_lmb && !g_rmb
        && !g_inv.held().empty() && ITEMS[g_inv.held().item].kind == ITEMK_THROWABLE) {
        if (!g_useLatch) {
            const ItemId what = g_inv.held().item;
            if (throwGlowflare(aim)) g_inv.take(what, 1);
            g_useLatch = true;
        }
        g_pmx = aim.x; g_pmy = aim.y;
        return;
    }

    /* Holding a tool replaces the build verb with the fire verb. Digging stays
       on the right button either way -- you can always claw at the wall, and a
       tool that took away your hands would be a strange upgrade. */
    if (g_survival && g_playerOn && g_lmb && !g_rmb
        && !g_inv.held().empty() && ITEMS[g_inv.held().item].kind == ITEMK_TOOL) {
        fireTool(aim);
        g_pmx = aim.x; g_pmy = aim.y;
        return;
    }

    /* Character-off mode is the freeform builder. A device selected from the
       left catalog follows a click or drag directly, without inventing a hidden
       infinite hotbar. The explicit !g_playerOn guard keeps the survival economy
       authoritative whenever the player is active. */
    if (!g_playerOn && g_paletteDevice >= 0 && g_lmb && !g_rmb && !g_bgLayer) {
        placeDeviceStroke((u8)g_paletteDevice, false, aim);
        return;
    }

    /* Machines place rectangles, but are deliberately strokeable: a click makes
       one and a drag lays a contiguous run. Failed placements cost nothing. */
    if (g_survival && g_playerOn && g_lmb && !g_rmb && !g_bgLayer
        && !g_inv.held().empty() && ITEMS[g_inv.held().item].kind == ITEMK_DEVICE) {
        placeDeviceStroke(ITEMS[g_inv.held().item].deviceType, true, aim);
        return;
    }

    /* Seeds convert rather than place, so they get their own branch before
       the build/dig split -- see ITEMK_SEED. Sowing is not rate-limited: it is
       not destruction, and the seeds themselves are the cost. */
    if (g_survival && g_playerOn && g_lmb && !g_rmb && !g_bgLayer
        && !g_inv.held().empty() && ITEMS[g_inv.held().item].kind == ITEMK_SEED) {
        sowSeeds(g_world, g_inv, aim.x, aim.y, buildRadius());
        g_pmx = aim.x; g_pmy = aim.y;
        return;
    }

    /* Food. One per click like the eggs, and for the same reason: holding the
       button would put the whole stack away in a third of a second. */
    if (g_playerOn && g_lmb && !g_rmb && !g_bgLayer
        && !g_inv.held().empty() && ITEMS[g_inv.held().item].kind == ITEMK_FOOD) {
        if (!g_useLatch) {
            const ItemId what = g_inv.held().item;
            /* Refuse at full health rather than silently eating it. Wasting a
               loaf because you mis-clicked is exactly the kind of small theft
               players remember. */
            if (g_player.hp < PLAYER_HP_MAX && g_inv.take(what, 1) == 1)
                g_player.heal(ITEMS[what].heal);
            g_useLatch = true;
        }
        return;
    }

    /* The striker. Not rate-limited and not consumed: holding the button is
       exactly the gesture -- you are working at a spark until it catches -- and
       IGNITE_MAX means holding it forever still cannot do anything a fire could
       not. See ITEM_FLINT. */
    if (g_survival && g_playerOn && g_lmb && !g_rmb && !g_bgLayer
        && !g_inv.held().empty() && ITEMS[g_inv.held().item].kind == ITEMK_IGNITE) {
        g_world.ignite(aim.x, aim.y, IGNITE_RADIUS);
        return;
    }

    /* Spawn eggs. Like a seed in that they consume the item and place nothing,
       and unlike everything else on this path in that what they create is not
       in the grid at all -- so they get their own branch rather than being
       squeezed into placeFrom. Rate-limited by g_useLatch so that holding the
       button does not empty the pool in a second. */
    if (g_playerOn && g_lmb && !g_rmb && !g_bgLayer
        && !g_inv.held().empty() && ITEMS[g_inv.held().item].kind == ITEMK_EGG) {
        if (!g_useLatch) {
            const int type = ITEMS[g_inv.held().item].summons;
            if (type && entSpawn(g_world, type, (float)aim.x, (float)aim.y) >= 0)
                g_inv.take(g_inv.held().item, 1);
            g_useLatch = true;
        }
        return;
    }

    /* Background mode. Rate-limited on the dig side exactly like the
       foreground, so scraping a wall costs the same effort as mining one. */
    if (g_survival && g_playerOn && g_bgLayer) {
        if (g_rmb) {
            const ToolSpec d = digSpec();
            if (g_digCool <= 0) {
                digBg(g_world, g_inv, aim.x, aim.y, digRadius(), d.cellsPerBite);
                g_digCool = d.cooldown;
            }
        } else {
            placeBg(g_world, g_inv, aim.x, aim.y, buildRadius());
        }
        roomsNotifyEdit(g_world, aim.x, aim.y);
        g_pmx = aim.x; g_pmy = aim.y;
        return;
    }

    if (g_survival && g_playerOn && g_rmb) {
        const ToolSpec d = digSpec();
        if (g_digCool <= 0) {
            digInto(g_world, g_inv, aim.x, aim.y, digRadius(), d.cellsPerBite,
                    d.plantsOnly, d.power,
                    g_digFilterOn ? g_digFilterMat : 0);
            g_digCool = d.cooldown;
        }
        roomsNotifyEdit(g_world, aim.x, aim.y);
        g_pmx = aim.x; g_pmy = aim.y;
        return;
    }

    /* Overwrite is construction at mining pace, not a creative erase brush.
       It lets a player plug a flooded tunnel with the material in hand, but
       every occupied cell still needs to be breakable by their best carried
       miner and is limited by that miner's bite and cooldown. Empty cells keep
       the normal fast placement behaviour around the replacement work. */
    if (g_survival && g_playerOn && g_lmb && !g_rmb && !g_bgLayer && g_overwrite
        && !g_inv.held().empty() && ITEMS[g_inv.held().item].kind == ITEMK_MATERIAL) {
        const ToolSpec d = digSpec();
        if (g_digCool <= 0) {
            const int replaced = overwriteFrom(g_world, g_inv, aim.x, aim.y,
                                               digRadius(), d.cellsPerBite, d.power);
            if (replaced > 0) g_digCool = d.cooldown;
        }
        placeFrom(g_world, g_inv, aim.x, aim.y, buildRadius());
        roomsNotifyEdit(g_world, aim.x, aim.y);
        g_pmx = aim.x; g_pmy = aim.y;
        return;
    }

    int x0 = g_pmx, y0 = g_pmy;
    int x1 = aim.x, y1 = aim.y;
    int steps = imax(abs(x1 - x0), abs(y1 - y0));

    const int sel = g_rmb ? (int)MAT_EMPTY : g_brushMat;
    for (int s = 0; s <= steps; ++s) {
        int px = steps ? x0 + (x1 - x0) * s / steps : x1;
        int py = steps ? y0 + (y1 - y0) * s / steps : y1;

        if (g_survival && g_playerOn) {
            /* Survival: right-click digs into the pack, left-click builds out
               of it. The heat and cool brushes stay available regardless --
               they are diagnostic tools, not materials, and there is nothing
               for them to consume. */
            /* Digging never reaches here -- it returned above. What is left is
               building, which is limited by what is in the pack rather than by
               a rate, and the two diagnostic brushes. */
            if (g_brushMat == TOOL_HEAT)      g_world.heat(px, py, g_brushRadius,  HEAT_STEP);
            else if (g_brushMat == TOOL_COOL) g_world.heat(px, py, g_brushRadius, -HEAT_STEP);
            else                              placeFrom(g_world, g_inv, px, py, buildRadius());
        } else {
            if (sel == TOOL_HEAT)      g_world.heat(px, py, g_brushRadius,  HEAT_STEP);
            else if (sel == TOOL_COOL) g_world.heat(px, py, g_brushRadius, -HEAT_STEP);
            else if (g_bgLayer)        g_world.paintBg(px, py, g_brushRadius, (u8)sel);
            else                       g_world.paint(px, py, g_brushRadius, (u8)sel, g_overwrite);
        }
        if (!steps) break;
    }
    /* Once per stroke rather than once per interpolated step. A stroke can
       cover hundreds of cells and only its two ends can be the block that
       sealed or breached anything reachable from where the cursor now is --
       and roomScan rejects a seed that is not open air behind placed
       background on its first two reads, which is what the overwhelming
       majority of these calls are. */
    roomsNotifyEdit(g_world, aim.x, aim.y);
    g_pmx = aim.x;
    g_pmy = aim.y;
}

/* ======================================================================
   Drawing
   ====================================================================== */
static void drawText(HDC hdc, int x, int y, COLORREF c, const char* s) {
    SetTextColor(hdc, c);
    TextOutA(hdc, x, y, s, (int)strlen(s));
}

/* A generic panel button: filled background, framed, centred (or left-of-
   swatch) label. selected gets an accent frame, hovered a lighter fill. */
static void drawButton(HDC hdc, const RECT& r, const char* label,
                       HBRUSH swatch, bool selected, bool hot) {
    RECT rr = r;
    FillRect(hdc, &rr, selected ? g_btnBgSel : (hot ? g_btnBgHot : g_btnBg));

    int textX = rr.left + 8;
    if (swatch) {
        RECT sw = { rr.left + 5, rr.top + 4, rr.left + 21, rr.bottom - 4 };
        FillRect(hdc, &sw, swatch);
        FrameRect(hdc, &sw, g_borderBrush);
        textX = sw.right + 7;
    }

    FrameRect(hdc, &rr, selected ? g_accentBrush : g_borderBrush);

    SetBkMode(hdc, TRANSPARENT);
    RECT t = rr; t.left = textX;
    SetTextColor(hdc, selected ? RGB(245, 224, 150) : RGB(214, 216, 224));
    DrawTextA(hdc, label, -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

/* A signal button reserves an explicit icon well. This is the visual cue that
   "Copper" is a named circuit signal here, not merely decorative text. */
static void drawCircuitSignalButton(HDC hdc, const RECT& r, const char* prefix,
                                    int signal, bool hot) {
    char label[80];
    sprintf(label, "%s %s", prefix, circuitSignalName(signal));
    drawButton(hdc, r, label, g_panelBg, false, hot);
    RECT icon = { r.left + 4, r.top + 2, r.left + 22, r.bottom - 2 };
    FillRect(hdc, &icon, g_panelBg);
    drawCircuitSignalIcon(hdc, icon, signal);
}


/* --- the device panel ------------------------------------------------------
   What right-clicking a machine gets you: its name, what it is sensing, and the
   one number you can change.

   Drawn over the VIEWPORT rather than in the side panel, and anchored near the
   machine rather than at a fixed spot on screen. Both for the same reason the
   hotbar sits over the world: this is a thing you are doing to an object you are
   looking at, and making your eye travel to a fixed corner to adjust the device
   under the cursor breaks the connection between the two. Clamped so it never
   hangs off an edge.

   ONE adjustable number, deliberately. See the note on DeviceInfo::valueLabel --
   a machine with six settings is a machine nobody can understand by looking at
   it, and the whole interaction should be "read it, nudge it, get on with it". */
/* Circuit controls name their selected signal and show an icon, so they need
   enough width for a material name rather than the old tiny cycle buttons. */
static const int DEVP_W = 420, DEVP_H = 96, DEVP_CIRCUIT_H = 218;
static RECT g_devpBox, g_devpDec, g_devpInc, g_devpTake, g_devpTurn, g_devpClose;

static void layoutDevPanel(const Device& d) {
    /* Sit it just above and right of the machine, in screen pixels. */
    const int h = circuitIsCombinator(d.type) ? DEVP_CIRCUIT_H : DEVP_H;
    int px = PANEL_W + (d.x + DEV_W - g_camX) * cellPixels() + 8;
    int py = (d.y - g_camY) * cellPixels() - h - 6;
    if (px + DEVP_W > WIN_W - 6) px = PANEL_W + (d.x - g_camX) * cellPixels() - DEVP_W - 8;
    if (px < PANEL_W + 6)        px = PANEL_W + 6;
    if (py < 6)                  py = (d.y + DEV_H - g_camY) * cellPixels() + 6;
    if (py + h > WIN_H - 6) py = WIN_H - 6 - h;

    SetRect(&g_devpBox, px, py, px + DEVP_W, py + h);
    const int by = py + h - 30;
    SetRect(&g_devpDec,   px + 10,  by, px + 118, by + 22);
    SetRect(&g_devpInc,   px + 122, by, px + 230, by + 22);
    SetRect(&g_devpTake,  px + 234, by, px + 342, by + 22);
    SetRect(&g_devpTurn,  px + 346, by, px + 374, by + 22);
    SetRect(&g_devpClose, px + DEVP_W - 40, by, px + DEVP_W - 10, by + 22);
}

/* True if the click was consumed. Returning that matters: a click on the panel
   must not also dig the world behind it. */
static bool handleDevPanelClick(int mx, int my) {
    if (g_devPanel < 0) return false;
    Device& d = g_devices[g_devPanel];
    if (!d.used) { g_devPanel = -1; return false; }
    layoutDevPanel(d);

    POINT pt = { mx, my };
    if (!PtInRect(&g_devpBox, pt)) return false;

    const DeviceInfo& di = DEVS[d.type];
    const int index = g_devPanel;
    CircuitDeviceConfig& cc = g_circuitConfig[index];
    /* Combinators own their whole bottom row. Signal buttons open the same
       searchable inventory browser as filters; cycling was too easy to mistake
       for a numeric adjustment and became miserable once material signals
       joined the virtual 1-9 channels. */
    if (d.type == DEV_CONSTANT_COMBINATOR || d.type == DEV_ARITHMETIC_COMBINATOR ||
        d.type == DEV_DECIDER_COMBINATOR) {
        if (PtInRect(&g_devpClose, pt)) { g_devPanel = -1; return true; }
        if (d.type == DEV_CONSTANT_COMBINATOR) {
            if (PtInRect(&g_devpDec, pt)) d.value -= 1;
            else if (PtInRect(&g_devpInc, pt)) d.value += 1;
            else if (PtInRect(&g_devpTake, pt)) { openCircuitSignalPicker(index, CIR_PICK_SIGNAL); return true; }
            if (d.value < di.vMin) d.value = di.vMin;
            if (d.value > di.vMax) d.value = di.vMax;
        } else {
            if (PtInRect(&g_devpDec, pt)) { openCircuitSignalPicker(index, CIR_PICK_A); return true; }
            else if (PtInRect(&g_devpInc, pt)) { openCircuitSignalPicker(index, CIR_PICK_B); return true; }
            else if (PtInRect(&g_devpTake, pt)) { openCircuitSignalPicker(index, CIR_PICK_OUT); return true; }
            else if (PtInRect(&g_devpTurn, pt)) {
                const int first = d.type == DEV_DECIDER_COMBINATOR ? CIR_OP_GREATER : CIR_OP_ADD;
                const int last  = d.type == DEV_DECIDER_COMBINATOR ? CIR_OP_NOT_EQUAL : CIR_OP_MODULO;
                cc.op = (u8)(cc.op < first || cc.op >= last ? first : cc.op + 1);
            }
        }
        return true;
    }
    if (d.type == DEV_PIPE || d.type == DEV_CROSSOVER) {
        if (PtInRect(&g_devpClose, pt)) { g_devPanel = -1; return true; }
        return true; /* status-only: pipes have no settings or inventory action */
    }
    if (d.type == DEV_DRAIN && PtInRect(&g_devpTake, pt)) {
        g_filterDevice = g_devPanel; g_creativeOpen = true; g_creSearch[0] = 0;
        g_creScroll = 0; g_creSearchFocus = true; layoutCreative();
        return true;
    }
    if (d.type == DEV_THERMOCOUPLE && PtInRect(&g_devpTake, pt)) {
        openCircuitSignalPicker(index, CIR_PICK_SIGNAL);
        return true;
    }
    if (PtInRect(&g_devpDec, pt))        d.value -= di.vStep;
    else if (PtInRect(&g_devpInc, pt))   d.value += di.vStep;
    else if (PtInRect(&g_devpClose, pt)) { g_devPanel = -1; return true; }
    else if (PtInRect(&g_devpTurn, pt) && di.aimable) {
        d.face = (u8)((d.face + 1) & 3);
        return true;
    }
    else if (PtInRect(&g_devpTake, pt) && (d.count > 0 || d.type == DEV_CHEST || d.type == DEV_SPOUT)) {
        /* Empty the machine's buffer into the pack. The counterpart to loading a
           placer by pouring onto it -- a miner fills up with what it has broken
           and this is how you get it out. Moves only what actually fits, so a full
           pack leaves the rest in the machine rather than destroying it. */
        ItemStack& held = g_inv.held();
        if ((d.type == DEV_CHEST || d.type == DEV_SPOUT) && !held.empty()
            && ITEMS[held.item].kind == ITEMK_MATERIAL
            && (d.count == 0 || d.mat == held.item)) {
            const int cap = d.type == DEV_CHEST ? CHEST_CAP : DEV_CAP;
            const int moved = imin((int)held.count, cap - (int)d.count);
            d.mat = (u8)held.item; d.count += moved; held.count -= moved;
            if (held.count == 0) held = ItemStack();
        } else if (d.count > 0) {
            const int moved = g_inv.add((ItemId)d.mat, (int)d.count);
            d.count -= moved;
            if (d.count <= 0) { d.count = 0; d.mat = MAT_EMPTY; }
        }
        return true;
    }
    if (d.value < di.vMin) d.value = di.vMin;
    if (d.value > di.vMax) d.value = di.vMax;
    /* Changing the setpoint has to clear the latch, or a thermocouple you have
       just raised the mark on stays tripped from the old one and never fires
       again until it cools all the way past the NEW mark. Measured as a real
       confusion the first time the panel existed. */
    d.latched = false;
    return true;
}

static void drawCircuitPortSignals(HDC hdc, int device, int port, int x, int y) {
    int shown = 0;
    char row[72];
    for (int s = 1; s < CIRCUIT_SIGNAL_COUNT && shown < 3; ++s) {
        const int value = circuitInputPort(device, port, s);
        if (!value) continue;
        sprintf(row, "%s = %d", circuitSignalName(s), value);
        RECT icon = { x, y + shown * 15, x + 14, y + shown * 15 + 14 };
        drawCircuitSignalIcon(hdc, icon, s);
        drawText(hdc, x + 18, y + shown * 15, RGB(174, 190, 214), row);
        ++shown;
    }
    if (!shown) drawText(hdc, x, y, RGB(112, 122, 138), "(no signals)");
}

static void drawDevPanel(HDC hdc) {
    if (g_devPanel < 0) return;
    /* Revalidated every frame by index, because devTick can delete a device out
       from under an open panel at any moment -- somebody digs its corner out and
       the machine is gone. This is why g_devPanel is an index and not a pointer. */
    if (g_devPanel >= MAX_DEVICES || !g_devices[g_devPanel].used) { g_devPanel = -1; return; }
    Device& d = g_devices[g_devPanel];
    layoutDevPanel(d);

    HGDIOBJ oldFont = SelectObject(hdc, g_font);
    SetBkMode(hdc, TRANSPARENT);
    FillRect(hdc, &g_devpBox, g_panelBg);
    FrameRect(hdc, &g_devpBox, g_accentBrush);

    const DeviceInfo& di = DEVS[d.type];
    const int tx = g_devpBox.left + 10;
    POINT pt = { g_mx, g_my };
    if (d.type == DEV_CONSTANT_COMBINATOR || d.type == DEV_ARITHMETIC_COMBINATOR ||
        d.type == DEV_DECIDER_COMBINATOR) {
        const CircuitDeviceConfig& cc = g_circuitConfig[g_devPanel];
        char circuitBuf[96], a[24], b[24], op[24];
        drawText(hdc, tx, g_devpBox.top + 6, RGB(218, 180, 250), di.name);
        if (d.type == DEV_CONSTANT_COMBINATOR) {
            sprintf(circuitBuf, "emit  %s = %d", circuitSignalName(cc.signal), (int)d.value);
            drawText(hdc, tx, g_devpBox.top + 28, RGB(200, 206, 218), circuitBuf);
            sprintf(a, "- %d", (int)d.value); sprintf(b, "+ %d", (int)d.value);
            drawButton(hdc, g_devpDec, a, 0, false, PtInRect(&g_devpDec, pt) != 0);
            drawButton(hdc, g_devpInc, b, 0, false, PtInRect(&g_devpInc, pt) != 0);
            drawCircuitSignalButton(hdc, g_devpTake, "out", cc.signal,
                                    PtInRect(&g_devpTake, pt) != 0);
        } else {
            sprintf(circuitBuf, "%s %s %s  ->  %s", circuitSignalName(cc.signalA), circuitOpName(cc.op),
                    circuitSignalName(cc.signalB), circuitSignalName(cc.signalOut));
            drawText(hdc, tx, g_devpBox.top + 28, RGB(200, 206, 218), circuitBuf);
            sprintf(circuitBuf, "result  %d", (int)d.reading);
            drawText(hdc, tx, g_devpBox.top + 46, RGB(160, 200, 230), circuitBuf);
            sprintf(op, "%s", circuitOpName(cc.op));
            drawCircuitSignalButton(hdc, g_devpDec, "A", cc.signalA,
                                    PtInRect(&g_devpDec, pt) != 0);
            drawCircuitSignalButton(hdc, g_devpInc, "B", cc.signalB,
                                    PtInRect(&g_devpInc, pt) != 0);
            drawCircuitSignalButton(hdc, g_devpTake, "out", cc.signalOut,
                                    PtInRect(&g_devpTake, pt) != 0);
            drawButton(hdc, g_devpTurn, op, 0, false, PtInRect(&g_devpTurn, pt) != 0);
        }
        if (d.type == DEV_CONSTANT_COMBINATOR) {
            drawText(hdc, tx, g_devpBox.top + 68, RGB(184, 140, 232), "NETWORK");
            drawCircuitPortSignals(hdc, g_devPanel, 0, tx + 8, g_devpBox.top + 84);
        } else {
            /* Factorio's most useful combinator affordance is being able to
               inspect both sides while configuring it. These are separate
               graphs here too: left reads inputs, right receives results. */
            drawText(hdc, tx, g_devpBox.top + 68, RGB(184, 140, 232), "INPUT NETWORK  (left terminal)");
            drawCircuitPortSignals(hdc, g_devPanel, 0, tx + 8, g_devpBox.top + 84);
            drawText(hdc, tx, g_devpBox.top + 132, RGB(240, 202, 112), "OUTPUT NETWORK  (right terminal)");
            drawCircuitPortSignals(hdc, g_devPanel, 1, tx + 8, g_devpBox.top + 148);
        }
        drawButton(hdc, g_devpClose, "x", 0, false, PtInRect(&g_devpClose, pt) != 0);
        SelectObject(hdc, oldFont);
        return;
    }
    if (d.type == DEV_PIPE || d.type == DEV_CROSSOVER) {
        char pipeBuf[96];
        sprintf(pipeBuf, "carrying %d %s", (int)d.count,
                d.count ? MATS[d.mat].name : "items");
        drawText(hdc, tx, g_devpBox.top + 30, RGB(200, 206, 218), pipeBuf);
        if (d.type == DEV_CROSSOVER) {
            sprintf(pipeBuf, "horizontal lane %d %s", (int)d.count2,
                    d.count2 ? MATS[d.mat2].name : "items");
            drawText(hdc, tx, g_devpBox.top + 48, RGB(160, 200, 230), pipeBuf);
        }
        drawButton(hdc, g_devpClose, "x", 0, false, PtInRect(&g_devpClose, pt) != 0);
        SelectObject(hdc, oldFont);
        return;
    }
    if (d.type == DEV_SPOUT || d.type == DEV_DRAIN || d.type == DEV_BLOCK_WATCHER) {
        char flow[112];
        drawText(hdc, tx, g_devpBox.top + 6, RGB(245, 224, 150), di.name);
        if (d.type == DEV_BLOCK_WATCHER) {
            sprintf(flow, "watching  %s", d.value ? MATS[d.value].name : "any block");
            drawText(hdc, tx, g_devpBox.top + 27, RGB(200, 206, 218), flow);
            drawText(hdc, tx, g_devpBox.top + 45,
                     d.reading ? RGB(255, 230, 140) : RGB(160, 168, 182),
                     d.reading ? "CONTACT  —  signal sent" : "waiting for contact");
            drawButton(hdc, g_devpDec, "<", 0, false, PtInRect(&g_devpDec, pt) != 0);
            drawButton(hdc, g_devpInc, ">", 0, false, PtInRect(&g_devpInc, pt) != 0);
        } else if (d.type == DEV_DRAIN) {
            int circuitFilter = MAT_EMPTY, best = 0;
            for (int m = 1; m < MAT_COUNT; ++m) {
                const int v = circuitInput(g_devPanel, m);
                if (v > best) { best = v; circuitFilter = m; }
            }
            sprintf(flow, "collecting  %s", circuitFilter ? MATS[circuitFilter].name :
                    (d.value ? MATS[d.value].name : "all materials"));
            drawText(hdc, tx, g_devpBox.top + 27, RGB(200, 206, 218), flow);
            sprintf(flow, "buffer  %d %s", (int)d.count, d.count ? MATS[d.mat].name : "items");
            drawText(hdc, tx, g_devpBox.top + 45, RGB(160, 200, 230), flow);
            drawButton(hdc, g_devpTake, "pick filter", 0, false, PtInRect(&g_devpTake, pt) != 0);
        } else {
            sprintf(flow, "emitting  %d cells / tick", (int)d.value);
            drawText(hdc, tx, g_devpBox.top + 27, RGB(200, 206, 218), flow);
            sprintf(flow, "buffer  %d %s", (int)d.count, d.count ? MATS[d.mat].name : "items");
            drawText(hdc, tx, g_devpBox.top + 45, RGB(160, 200, 230), flow);
            drawButton(hdc, g_devpDec, "-", 0, false, PtInRect(&g_devpDec, pt) != 0);
            drawButton(hdc, g_devpInc, "+", 0, false, PtInRect(&g_devpInc, pt) != 0);
        }
        if (d.type == DEV_SPOUT || d.type == DEV_DRAIN)
            drawText(hdc, tx + 145, g_devpBox.top + 45,
                     d.enabled ? RGB(130, 220, 150) : RGB(232, 116, 100),
                     d.enabled ? "ON" : "OFF");
        static const char* FACE[4] = { "aim down", "aim up", "aim left", "aim right" };
        drawButton(hdc, g_devpTurn, FACE[d.face & 3], 0, false, PtInRect(&g_devpTurn, pt) != 0);
        drawButton(hdc, g_devpClose, "x", 0, false, PtInRect(&g_devpClose, pt) != 0);
        SelectObject(hdc, oldFont);
        return;
    }
    char buf[128];
    drawText(hdc, tx, g_devpBox.top + 6, RGB(245, 224, 150), di.name);

    sprintf(buf, "reading  %d %s", d.reading, di.valueUnit);
    drawText(hdc, tx, g_devpBox.top + 26, RGB(200, 206, 218), buf);

    if (di.vMin != di.vMax) {
        if (d.type == DEV_DRAIN)
            sprintf(buf, "filter  %s", d.value ? MATS[d.value].name : "all materials");
        else
            sprintf(buf, "%s  %d %s", di.valueLabel, (int)d.value, di.valueUnit);
        drawText(hdc, tx, g_devpBox.top + 42, RGB(214, 216, 224), buf);
    }

    /* Sparks received. The first thing you want to know about a machine that is
       not doing what you expected is whether the signal is reaching it at all. */
    sprintf(buf, "sparks in  %d", (int)d.received);
    drawText(hdc, tx, g_devpBox.top + 58, RGB(160, 200, 230), buf);

    /* The buffer, for machines that have one. Named rather than shown as a
       swatch: "holds 340 Fe Ore" is what you need to know, and at this size a
       colour chip would be indistinguishable between the two ores. */
    if (d.type == DEV_PLACER || d.type == DEV_MINER || d.type == DEV_CHEST || d.type == DEV_SPOUT || d.type == DEV_DRAIN) {
        if (d.count > 0) sprintf(buf, "holds %d %s", (int)d.count, MATS[d.mat].name);
        else             sprintf(buf, "holds nothing");
        drawText(hdc, tx + 96, g_devpBox.top + 58, RGB(214, 216, 224), buf);
    }

    /* The state line. "armed" rather than "off" because a thermocouple below its
       mark is not idle, it is waiting -- and that distinction is the whole
       difference between a device you can sequence with and a thermostat. */
    const char* st = d.firing ? "FIRING" : (d.latched ? "tripped" : "armed");
    drawText(hdc, tx + 120, g_devpBox.top + 26,
             d.firing ? RGB(255, 240, 170) : RGB(160, 168, 182), st);

    /* A device with nothing to adjust shows no -/+ rather than two dead buttons. */
    if (di.vMin != di.vMax) {
        drawButton(hdc, g_devpDec, "-", 0, false, PtInRect(&g_devpDec, pt) != 0);
        drawButton(hdc, g_devpInc, "+", 0, false, PtInRect(&g_devpInc, pt) != 0);
    }
    if (di.aimable) {
        static const char* FACE[4] = { "aim down", "aim up", "aim left", "aim right" };
        drawButton(hdc, g_devpTurn, FACE[d.face & 3], 0, false,
                   PtInRect(&g_devpTurn, pt) != 0);
    }
    if (d.type == DEV_THERMOCOUPLE) {
        drawCircuitSignalButton(hdc, g_devpTake, "signal", g_circuitConfig[g_devPanel].signal,
                                PtInRect(&g_devpTake, pt) != 0);
    }
    if (d.type == DEV_PLACER || d.type == DEV_MINER || d.type == DEV_CHEST || d.type == DEV_SPOUT || d.type == DEV_DRAIN)
        drawButton(hdc, g_devpTake, (d.type == DEV_CHEST || d.type == DEV_SPOUT) ? "store/take" : "take", 0, false, PtInRect(&g_devpTake, pt) != 0);
    drawButton(hdc, g_devpClose, "x", 0, false, PtInRect(&g_devpClose, pt) != 0);
    SelectObject(hdc, oldFont);
}

/* The hotbar sits over the foot of the viewport rather than in the panel. The
   panel is already full, and more to the point what you are carrying belongs
   next to the world you are carrying it through -- glancing down at your hands
   should not mean looking away to the side. */
static void layoutHotbar() {
    const int totalW = HOTBAR_SLOTS * HOTBAR_SLOT;
    const int x0 = PANEL_W + (VIEW_W - totalW) / 2;
    const int y0 = VIEW_H - HOTBAR_SLOT - 10;
    for (int i = 0; i < HOTBAR_SLOTS; ++i)
        SetRect(&g_hotRect[i], x0 + i * HOTBAR_SLOT, y0,
                x0 + i * HOTBAR_SLOT + HOTBAR_SLOT - 3, y0 + HOTBAR_SLOT - 3);
}

static void drawHotbar(HDC hdc) {
    layoutHotbar();
    HGDIOBJ oldFont = SelectObject(hdc, g_font);
    SetBkMode(hdc, TRANSPARENT);

    /* --- the health bar -----------------------------------------------------
       Always drawn, unlike the fuel gauge, and the difference is deliberate:
       fuel is a property of gear you may not be wearing, health is a property of
       being alive. A bar that comes and goes teaches you not to look at it.

       Directly above the hotbar and the full width of it, so the two read as one
       block of "your state" rather than as decorations at opposite corners.

       It carries a temperature warning as well, on the same strip. Heat and cold
       are the two things in this game that will kill you without touching you,
       and they are survivable for several seconds -- which is long enough to
       react to and far too long to notice by watching a number tick down. The
       bar goes orange or blue while it is happening, so the warning is in the
       place you are already looking for the consequence. */
    {
        /* ABOVE the fuel gauge, which is above the two text rows. The stack from
           the hotbar upward is: name (top-20..top-4), stats (top-36..top-20),
           fuel (top-47..top-40), health here. Anything lower overprints the
           readout, which is exactly what a first attempt at top-6 did.

           Health keeps its slot whether or not flight gear is worn, so the bar
           does not jump when you take a jetpack off -- the gap where the fuel
           gauge would be is the better cost. */
        const int x0 = g_hotRect[0].left;
        const int y1 = g_hotRect[0].top - 51, y0 = y1 - 9;
        const int x1 = x0 + 132;
        RECT bar = { x0, y0, x1, y1 };
        FillRect(hdc, &bar, g_btnBg);

        const float frac = (float)g_player.hp / (float)PLAYER_HP_MAX;
        RECT fill = bar;
        fill.right = x0 + (int)((float)(x1 - x0) * (frac < 0.0f ? 0.0f : frac));
        if (fill.right > fill.left) {
            COLORREF c = RGB(96, 176, 96);
            if (frac <= 0.25f)     c = RGB(210, 60, 52);
            else if (frac <= 0.5f) c = RGB(214, 158, 60);
            /* The environment warning WINS over the health colour, because it
               is the more urgent of the two: a full bar turning orange is the
               only notice you get that standing here is a mistake. */
            if (g_player.hurtingHot())       c = RGB(238, 120, 40);
            else if (g_player.hurtingCold()) c = RGB(90, 170, 236);
            HBRUSH b = CreateSolidBrush(c);
            FillRect(hdc, &fill, b);
            DeleteObject(b);
        }
        FrameRect(hdc, &bar, g_borderBrush);

        /* --- the breath bar -------------------------------------------------
           Shown ONLY while it is draining or refilling, unlike health. A full
           lungful is the normal state of affairs and a permanently full bar
           beside a permanently visible health bar is two things to ignore
           instead of one; this one appearing is itself the warning.

           Directly under the health bar and the same width, so the pair read as
           one readout rather than as two unrelated gauges. */
        if (g_player.breath < BREATH_MAX) {
            RECT br = { x0, y1 + 2, x1, y1 + 7 };
            FillRect(hdc, &br, g_btnBg);
            const float bf = (float)g_player.breath / (float)BREATH_MAX;
            RECT bfill = br;
            bfill.right = x0 + (int)((float)(x1 - x0) * bf);
            if (bfill.right > bfill.left) {
                HBRUSH b = CreateSolidBrush(bf > 0.25f ? RGB(90, 170, 236)
                                                       : RGB(210, 60, 52));
                FillRect(hdc, &bfill, b);
                DeleteObject(b);
            }
            FrameRect(hdc, &br, g_borderBrush);
        }

        char hpTxt[64];
        if (!g_player.alive)               sprintf(hpTxt, "DEAD  -  tap R to respawn");
        else if (g_player.breath == 0)     sprintf(hpTxt, "%d   DROWNING", g_player.hp);
        else if (g_player.hurtingHot())    sprintf(hpTxt, "%d   BURNING", g_player.hp);
        else if (g_player.hurtingCold())   sprintf(hpTxt, "%d   FREEZING", g_player.hp);
        else if (g_player.underwater)      sprintf(hpTxt, "%d   %ds of air",
                                                   g_player.hp, g_player.breath / 60);
        else                               sprintf(hpTxt, "%d", g_player.hp);
        /* Beside the bar rather than above it, so the whole readout is one row
           and cannot collide with anything below. */
        SetTextColor(hdc, g_player.alive ? RGB(206, 212, 224) : RGB(232, 96, 88));
        RECT t = { x1 + 8, y0 - 3, x1 + 260, y1 + 3 };
        DrawTextA(hdc, hpTxt, -1, &t, DT_LEFT | DT_SINGLELINE);
    }

    /* --- the fuel gauge -----------------------------------------------------
       Only drawn when there is flight gear on, because a permanent empty bar is
       a permanent question. It goes above the hotbar rather than beside the
       character: fuel is something you plan a jump with before you leave the
       ground, so it wants to be where you are already looking for your
       loadout, not attached to a figure that is about to move. */
    if (g_player.fly.any()) {
        /* Above BOTH text rows, not one of them. The name row occupies
           top-20..top-4 and the stats line sits 16px above that, so anything
           nearer than top-38 overprints the readout -- which is exactly what
           top-30 did: an orange bar straight through "no module installed". */
        const int x0 = g_hotRect[0].left, x1 = g_hotRect[HOTBAR_SLOTS - 1].right;
        const int y1 = g_hotRect[0].top - 40, y0 = y1 - 7;
        RECT bar = { x0, y0, x1, y1 };
        FillRect(hdc, &bar, g_btnBg);
        const float frac = g_player.fuel / (float)g_player.fly.fuel;
        RECT fill = bar;
        fill.right = x0 + (int)((float)(x1 - x0) * (frac < 0.0f ? 0.0f : frac));
        if (fill.right > fill.left) {
            /* Warm while it lasts, and red once there is not enough left to do
               anything with -- the number that matters is not "how full" but
               "can I still get out of this hole". */
            HBRUSH b = CreateSolidBrush(frac > 0.25f ? RGB(226, 168, 74) : RGB(210, 88, 60));
            FillRect(hdc, &fill, b);
            DeleteObject(b);
        }
        FrameRect(hdc, &bar, g_borderBrush);
    }

    for (int i = 0; i < HOTBAR_SLOTS; ++i) {
        RECT r = g_hotRect[i];
        const bool sel = (i == g_inv.selected);
        const ItemStack& st = g_inv.slot[i];

        FillRect(hdc, &r, sel ? g_btnBgSel : g_btnBg);
        FrameRect(hdc, &r, sel ? g_accentBrush : g_borderBrush);

        if (!st.empty()) {
            /* A block of the item's own colour, inset, with the count under it.
               No icons: every material already has a colour that means
               something in this game, and a swatch reads faster than a glyph. */
            RECT sw = r;
            sw.left += 4; sw.right -= 4; sw.top += 3; sw.bottom -= 14;
            drawItemIcon(hdc, sw, st.item);

            /* Four digits do not fit a 31px slot at this font, so anything
               past 999 reads as thousands to one decimal: 1.2k, 9.9k.

               TRUNCATED, not rounded, and that is the whole reason the stack
               cap is 9999 rather than 10000. "%.1f" on a full stack rounds
               9.999 up to "10.0k" -- five characters, which overflows the slot,
               and reads as more than the cap allows. Integer division cannot
               do that, and it renders a full stack as the 9.9k the cap was
               picked to show. */
            char n[16];
            /* Three characters of digits at most, whatever the number. A
               hotbar slot is 34 px wide and stacks now reach 100000, which
               spelled out as "100.0k" is six characters and overruns into its
               neighbour -- so the decimal is dropped once it stops earning its
               place. */
            const unsigned c = st.count;
            if      (c >= 10000) sprintf(n, "%uk", c / 1000);
            else if (c >= 1000)  sprintf(n, "%u.%uk", c / 1000, (c % 1000) / 100);
            else                 sprintf(n, "%u", c);
            RECT cr = r; cr.top = r.bottom - 14;
            SetTextColor(hdc, RGB(226, 230, 238));
            DrawTextA(hdc, n, -1, &cr, DT_CENTER | DT_TOP | DT_SINGLELINE);
        }
    }

    /* Name of what is held, above the bar, so a swatch is never ambiguous. */
    RECT nr;
    SetRect(&nr, PANEL_W, g_hotRect[0].top - 20, WIN_W, g_hotRect[0].top - 4);
    const ItemStack& h = g_inv.held();
    if (!h.empty()) {
        SetTextColor(hdc, RGB(200, 206, 216));
        DrawTextA(hdc, ITEMS[h.item].name, -1, &nr, DT_CENTER | DT_TOP | DT_SINGLELINE);
    }

    /* What you are digging with, on the same line. Reach is here rather than in
       the stats block because it is a thing a carried item changes -- a number
       that moves when you pick something up belongs next to the pack that moved
       it, not down among the frame timings. The bonus is called out separately
       so it is obvious which part of it you would lose by dropping the item. */
    {
        char s[96];
        const int bonus = g_inv.reachBonus();
        const ToolShot sh = toolResolve(h);
        /* Holding a tool, the line describes the tool -- because that is what
           the left button now does, and a readout that kept saying "Hands"
           while you were shooting would be describing the wrong verb. */
        if (!h.empty() && ITEMS[h.item].kind == ITEMK_THROWABLE) {
            sprintf(s, "%s  x%u  click to throw  reach %d", ITEMS[h.item].name,
                    (unsigned)h.count, currentReach());
        }
        else if (!h.empty() && ITEMS[h.item].kind == ITEMK_TOOL) {
            if (sh.canFire) sprintf(s, "%s  pow %d  pierce %d  %d/s  reach %d",
                                    ITEMS[h.item].name, sh.power, sh.pierce,
                                    60 / imax(1, sh.delay), currentReach());
            else            sprintf(s, "%s  no module installed  reach %d",
                                    ITEMS[h.item].name, currentReach());
        }
        else {
            const ToolSpec d = digSpec();
            const int perSec = (60 * d.cellsPerBite) / imax(1, d.cooldown);
            if (bonus > 0) sprintf(s, "%s  dig r%d  %d/s  reach %d (+%d)%s", d.name,
                                   digRadius(), perSec, currentReach(), bonus,
                                   g_bgLayer ? "   [BACKGROUND]" : "");
            else           sprintf(s, "%s  dig r%d  %d/s  reach %d%s", d.name,
                                   digRadius(), perSec, currentReach(),
                                   g_bgLayer ? "   [BACKGROUND]" : "");
        }
        /* Its own line ABOVE the name row, not sharing it. Left-aligned text and
           centred text on one line collide as soon as either gets long, and
           "Focusing Lens" over a reach of 84 (+28) was already long enough --
           the two overprinted into an unreadable smear. */
        RECT tr = nr; tr.left = g_hotRect[0].left;
        tr.top -= 16; tr.bottom -= 16;
        SetTextColor(hdc, bonus > 0 ? RGB(150, 200, 226) : RGB(140, 146, 158));
        DrawTextA(hdc, s, -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE);
    }
    SelectObject(hdc, oldFont);
}

/* --- the tool in the character's hand ---------------------------------------

   Drawn procedurally along the aim vector rather than as a rotated sprite.
   Rotating 14x14 pixel art to an arbitrary angle produces a different mangled
   shape every frame -- edges break up, the tip wanders, and the whole thing
   shimmers as you sweep the mouse. A rod built from its own geometry points
   exactly where it is aimed at every angle, which is the only property this
   thing actually needs.

   It shares the icons' palette so the held tool and its inventory picture read
   as the same object: gold handle, steel shaft, white tip. */
static void drawHeldTool(u32* px, const Aim& aim, bool lit) {
    const ItemStack& h = g_inv.held();
    if (h.empty() || ITEMS[h.item].kind != ITEMK_TOOL) return;

    const float pcx = g_player.centreX(), pcy = g_player.centreY();
    float dx = (float)aim.x - pcx, dy = (float)aim.y - pcy;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) { dx = 1.0f; dy = 0.0f; d = 1.0f; }
    dx /= d; dy /= d;

    /* Mk II is longer and two cells thick; the tiers have to be told apart at a
       glance in the world, not only in the inventory. */
    const bool mk2 = (h.item == ITEM_MULTITOOL2);
    const int  len = mk2 ? 13 : 10;
    const int  grip = mk2 ? 5 : 4;
    const u32  handle = mk2 ? 0xE8D9A0 : 0xC8B070;

    /* Start at the shoulder rather than the centre, so the tool reads as held
       rather than skewered through the middle of the body. */
    const float ox = pcx + dx * 2.0f - g_camX, oy = pcy - 1.0f + dy * 2.0f - g_camY;
    const float px2 = -dy, py2 = dx;   /* perpendicular, for thickness */

    for (int t = 0; t < len; ++t) {
        u32 c;
        if      (t < grip)     c = handle;
        else if (t == grip)    c = 0x6E7684;    /* collar */
        else if (t == len - 1) c = 0xF2F5FF;    /* working tip */
        else                   c = 0xAEB6C4;    /* steel */

        const float fx = ox + dx * t, fy = oy + dy * t;
        const int x = (int)fx, y = (int)fy;
        /* Lit with the character holding it, or a tool would glow in the dark
           while the hand around it did not. */
        if (x >= 0 && x < VIEW_CELLS_W && y >= 0 && y < VIEW_CELLS_H)
            px[y * VIEW_CELLS_W + x] = lit ? shadeColor(c, viewShade(x, y)) : c;
        if (mk2 || t < grip) {
            const int x2 = (int)(fx + px2), y2 = (int)(fy + py2);
            if (x2 >= 0 && x2 < VIEW_CELLS_W && y2 >= 0 && y2 < VIEW_CELLS_H)
                px[y2 * VIEW_CELLS_W + x2] = lit ? shadeColor(c, viewShade(x2, y2)) : c;
        }
    }
}

/* Celestials are a backdrop overlay, not cells: they never collide, shadow, or
   move with the terrain. Their path is tied to the same saved clock that drives
   skylight, so dawn cannot show a moon while the lighting says noon. */
static void drawCelestials(u32* px) {
    const float turn = (float)g_worldTime / (float)DAY_LENGTH;
    const bool sun = turn < 0.5f;
    const float leg = sun ? turn * 2.0f : (turn - 0.5f) * 2.0f;
    const int cx = (int)(leg * (float)(VIEW_CELLS_W + 120)) - 60;
    const float arc = 1.0f - (leg * 2.0f - 1.0f) * (leg * 2.0f - 1.0f);
    const int cy = 172 - (int)(arc * 130.0f);
    const int r = sun ? 13 : 10;
    const u32 col = sun ? 0xFFF0A0 : 0xC9D8FF;
    for (int y = cy - r; y <= cy + r; ++y) for (int x = cx - r; x <= cx + r; ++x) {
        const int dx = x - cx, dy = y - cy;
        if (dx * dx + dy * dy > r * r || x < 0 || x >= VIEW_CELLS_W || y < 0 || y >= VIEW_CELLS_H) continue;
        const int wx = g_camX + x, wy = g_camY + y;
        if (wx < 0 || wx >= SIM_W || wy < 0 || wy >= SIM_H) continue;
        if (g_world.zoneAt(wx, wy) != ZONE_SKY || g_world.at(wx, wy).mat != MAT_EMPTY ||
            g_world.bgAt(wx, wy) != MAT_EMPTY) continue;
        px[y * VIEW_CELLS_W + x] = col;
    }
}

/* A crosshair, drawn in SCREEN pixels rather than cells. Drawing it into the
   sim buffer would scale it with SCALE and put it on 2px steps, and a cursor
   that lands only on even pixels feels loose in a way that is hard to place. */
/* Outlined: each arm is drawn as a black rect inflated by one pixel, then the
   coloured arm inside it. Without that the crosshair vanishes wherever it
   happens to match what is behind it -- and this world contains everything from
   near-black stone to white-hot lava to bright sand, so there is no single
   colour that stays visible. A dark edge around a light core reads against both
   ends of that range at once, which is why every game reticle has one. */
static void drawCross(HDC hdc, int sx, int sy, COLORREF c, int gap, int arm) {
    RECT bar[4];
    SetRect(&bar[0], sx - gap - arm,     sy,              sx - gap,             sy + 1);
    SetRect(&bar[1], sx + gap + 1,       sy,              sx + gap + arm + 1,   sy + 1);
    SetRect(&bar[2], sx,                 sy - gap - arm,  sx + 1,               sy - gap);
    SetRect(&bar[3], sx,                 sy + gap + 1,    sx + 1,               sy + gap + arm + 1);

    HBRUSH outline = CreateSolidBrush(RGB(0, 0, 0));
    for (int i = 0; i < 4; ++i) {
        RECT o = bar[i];
        o.left -= 1; o.top -= 1; o.right += 1; o.bottom += 1;
        FillRect(hdc, &o, outline);
    }
    DeleteObject(outline);

    HBRUSH b = CreateSolidBrush(c);
    for (int i = 0; i < 4; ++i) FillRect(hdc, &bar[i], b);
    DeleteObject(b);
}

/* Circuit wiring needs a reticle that cannot be mistaken for the ordinary
   painting cross. The violet ring says "information link", while the two
   horizontal terminals make the cursor read as a cable end instead of a
   brush. */
static void drawCircuitReticle(HDC hdc, int x, int y, bool pending) {
    const COLORREF col = pending ? RGB(255, 220, 120) : RGB(202, 140, 255);
    HPEN edge = CreatePen(PS_SOLID, 3, RGB(0, 0, 0));
    HGDIOBJ oldPen = SelectObject(hdc, edge);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Ellipse(hdc, x - 9, y - 9, x + 10, y + 10);
    SelectObject(hdc, oldPen); DeleteObject(edge);

    HPEN ring = CreatePen(PS_SOLID, 1, col);
    oldPen = SelectObject(hdc, ring);
    Ellipse(hdc, x - 8, y - 8, x + 9, y + 9);
    MoveToEx(hdc, x - 14, y, NULL); LineTo(hdc, x - 8, y);
    MoveToEx(hdc, x + 9, y, NULL);  LineTo(hdc, x + 15, y);
    MoveToEx(hdc, x, y - 3, NULL);  LineTo(hdc, x, y + 4);
    SelectObject(hdc, oldPen); DeleteObject(ring);
    SelectObject(hdc, oldBrush);
}

/* --- the dig filter's cursor -----------------------------------------------
   A FUNNEL with a crosshair through it: wide mouth, narrow throat, which is
   what a filter looks like and what this one does -- everything goes in, one
   thing comes out.

   A distinct SHAPE rather than a recoloured crosshair, and that is the whole
   requirement. This is a mode that silently makes the dig button refuse most of
   the world, so the cursor has to say so at a glance and from the corner of an
   eye; a colour change reads as decoration and gets missed, and being unable to
   dig for reasons you cannot see is the single worst way for this feature to
   fail. Amber for the same reason the wire mode is: warm means "a mode is on".
   Drawn with a black under-stroke so it survives a bright cave wall. */
static void drawFilterReticle(HDC hdc, int x, int y) {
    static const POINT FUNNEL[] = {
        { -9, -9 }, { 9, -9 }, { 3, -1 }, { 3, 8 }, { -3, 8 }, { -3, -1 }, { -9, -9 }
    };
    const int n = (int)(sizeof(FUNNEL) / sizeof(FUNNEL[0]));
    POINT p[7];
    for (int i = 0; i < n; ++i) { p[i].x = x + FUNNEL[i].x; p[i].y = y + FUNNEL[i].y; }

    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    HPEN edge = CreatePen(PS_SOLID, 3, RGB(0, 0, 0));
    HGDIOBJ oldPen = SelectObject(hdc, edge);
    Polyline(hdc, p, n);
    SelectObject(hdc, oldPen); DeleteObject(edge);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 196, 74));
    oldPen = SelectObject(hdc, pen);
    Polyline(hdc, p, n);
    /* The crosshair through the throat, so it is still a cursor you can aim
       with rather than an icon floating near the point. */
    MoveToEx(hdc, x - 15, y, NULL); LineTo(hdc, x - 10, y);
    MoveToEx(hdc, x + 11, y, NULL); LineTo(hdc, x + 16, y);
    SelectObject(hdc, oldPen); DeleteObject(pen);
    SelectObject(hdc, oldBrush);
}

/* A radius in cells is drawn to the outside of its affected cells, not merely
   to their centres. That half-cell matters at small sizes: a size-one brush is
   visibly a three-cell disc, and the outline should promise exactly that. */
static void drawBrushOutline(HDC hdc, int x, int y, int radius) {
    const int r = radius * cellPixels() + cellPixels() / 2;
    HPEN pen = CreatePen(PS_DOT, 1, RGB(114, 156, 196));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Ellipse(hdc, x - r, y - r, x + r + 1, y + r + 1);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

/* ======================================================================
   The map
   ====================================================================== */

/* Painted into the pixel buffer rather than through GDI, for the same reason
   everything else in the viewport is: one blit at the end beats a few hundred
   thousand SetPixel calls, and the map is at its largest a full viewport of
   them. */
static void mapBlit(u32* px, int dstX, int dstY, int dstW, int dstH,
                    int srcX, int srcY, int zoom, bool fogged) {
    for (int y = 0; y < dstH; ++y) {
        const int vy = dstY + y;
        if (vy < 0 || vy >= VIEW_CELLS_H) continue;
        const int my = srcY + y / zoom;
        for (int x = 0; x < dstW; ++x) {
            const int vx = dstX + x;
            if (vx < 0 || vx >= VIEW_CELLS_W) continue;
            const int mx = srcX + x / zoom;
            u32 c;
            if (!mapColour(mx, my, &c)) {
                /* Unexplored. Drawn as a flat dark field rather than left
                   transparent: a map with holes you can see the game through
                   reads as a rendering fault, and the whole point of a fog is
                   that it is a positive statement about not knowing. */
                if (!fogged) continue;
                c = (mx < 0 || mx >= MAP_W || my < 0 || my >= MAP_H)
                    ? 0x000000 : 0x0B0D11;
            }
            px[vy * VIEW_CELLS_W + vx] = c;
        }
    }
}

/* A box outline, for the "you are here" marker and the minimap frame. */
static void mapBox(u32* px, int x0, int y0, int w, int h, u32 c) {
    for (int x = 0; x < w; ++x) {
        const int a = x0 + x;
        if (a < 0 || a >= VIEW_CELLS_W) continue;
        if (y0 >= 0 && y0 < VIEW_CELLS_H)          px[y0 * VIEW_CELLS_W + a] = c;
        const int b = y0 + h - 1;
        if (b >= 0 && b < VIEW_CELLS_H)            px[b * VIEW_CELLS_W + a] = c;
    }
    for (int y = 0; y < h; ++y) {
        const int b = y0 + y;
        if (b < 0 || b >= VIEW_CELLS_H) continue;
        if (x0 >= 0 && x0 < VIEW_CELLS_W)          px[b * VIEW_CELLS_W + x0] = c;
        const int a = x0 + w - 1;
        if (a >= 0 && a < VIEW_CELLS_W)            px[b * VIEW_CELLS_W + a] = c;
    }
}

/* The corner minimap. Small, always on, and centred on the player -- its job is
   "which way is the tunnel I came from", not navigation. */
static const int MINI_W = 132, MINI_H = 108, MINI_PAD = 6;

static void drawMinimap(u32* px) {
    if (!g_miniOn || g_mapOpen) return;
    const int x0 = VIEW_CELLS_W - MINI_W - MINI_PAD, y0 = MINI_PAD;
    const int pcx = (int)g_player.centreX() >> MAP_SHIFT;
    const int pcy = (int)g_player.centreY() >> MAP_SHIFT;

    mapBlit(px, x0, y0, MINI_W, MINI_H, pcx - MINI_W / 2, pcy - MINI_H / 2, 1, true);
    mapBox(px, x0 - 1, y0 - 1, MINI_W + 2, MINI_H + 2, 0x6E7684);
    /* The player, dead centre by construction. Two cells so it is visible
       against a busy map. */
    for (int oy = -1; oy <= 1; ++oy)
        for (int ox = -1; ox <= 1; ++ox) {
            const int a = x0 + MINI_W / 2 + ox, b = y0 + MINI_H / 2 + oy;
            if (a < 0 || a >= VIEW_CELLS_W || b < 0 || b >= VIEW_CELLS_H) continue;
            px[b * VIEW_CELLS_W + a] = 0xFFD24A;
        }
}

/* The full map. Takes the viewport, scaled so the world's WIDTH fits, and
   scrolls vertically -- the world is 2.25 times taller than it is wide, so
   there is no zoom at which the whole thing is usefully on screen at once. */
static void drawFullMap(u32* px) {
    if (!g_mapOpen) return;
    /* Integer zoom only. A fractional scale on a pixel map produces seams and
       shimmer as it pans, and the map is a diagram rather than a photograph. */
    const int zoom = VIEW_CELLS_W / MAP_W > 0 ? VIEW_CELLS_W / MAP_W : 1;
    const int viewMapW = VIEW_CELLS_W / zoom, viewMapH = VIEW_CELLS_H / zoom;

    int cx = g_mapPanX, cy = g_mapPanY;
    if (!g_mapPanned) {
        cx = (int)g_player.centreX() >> MAP_SHIFT;
        cy = (int)g_player.centreY() >> MAP_SHIFT;
    }
    /* Drag to look around. Held-button deltas rather than a scrollbar, because
       the map is a thing you push about rather than a document you seek in. */
    static int lastX = -1, lastY = -1;
    if (g_lmb && g_mx >= PANEL_W) {
        if (lastX >= 0) {
            cx -= (g_mx - lastX) / (zoom * SCALE);
            cy -= (g_my - lastY) / (zoom * SCALE);
            if (g_mx != lastX || g_my != lastY) g_mapPanned = true;
        }
        lastX = g_mx; lastY = g_my;
    } else { lastX = lastY = -1; }
    /* Clamped so the map cannot be scrolled off into blackness. */
    int sx = cx - viewMapW / 2, sy = cy - viewMapH / 2;
    if (sx > MAP_W - viewMapW) sx = MAP_W - viewMapW;
    if (sy > MAP_H - viewMapH) sy = MAP_H - viewMapH;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;

    mapBlit(px, 0, 0, VIEW_CELLS_W, VIEW_CELLS_H, sx, sy, zoom, true);

    /* You, and the patch of world currently on screen. The box is what turns
       the map from a picture into a position: it says how much of what you are
       looking at you can actually see right now. */
    const int pmx = ((int)g_player.centreX() >> MAP_SHIFT) - sx;
    const int pmy = ((int)g_player.centreY() >> MAP_SHIFT) - sy;
    const int bw = (VIEW_CELLS_W >> MAP_SHIFT) * zoom;
    const int bh = (VIEW_CELLS_H >> MAP_SHIFT) * zoom;
    mapBox(px, pmx * zoom - bw / 2, pmy * zoom - bh / 2, bw, bh, 0x5A6472);
    for (int oy = -2; oy <= 2; ++oy)
        for (int ox = -2; ox <= 2; ++ox) {
            const int a = pmx * zoom + ox, b = pmy * zoom + oy;
            if (a < 0 || a >= VIEW_CELLS_W || b < 0 || b >= VIEW_CELLS_H) continue;
            px[b * VIEW_CELLS_W + a] = 0xFFD24A;
        }
    g_mapPanX = sx + viewMapW / 2;
    g_mapPanY = sy + viewMapH / 2;
}

static void drawCursor(HDC hdc) {
    if (g_mx < PANEL_W) return;                 /* over the panel: system cursor */

    const Aim aim = g_wireMode ? currentWireAim() : currentAim();
    /* Where the tool acts -- inside the reach limit. */
    const int ax = PANEL_W + (aim.x - g_camX) * cellPixels() + cellPixels() / 2;
    const int ay = (aim.y - g_camY) * cellPixels() + cellPixels() / 2;
    /* Where the mouse actually is. */
    const int gx = PANEL_W + (aim.ghostX - g_camX) * cellPixels() + cellPixels() / 2;
    const int gy = (aim.ghostY - g_camY) * cellPixels() + cellPixels() / 2;

    /* Q is already the size modifier. Holding it also reveals the exact disc
       the next brush action will affect, which makes large heat/terrain edits
       deliberate instead of a guess from a number in the sidebar. */
    if ((GetAsyncKeyState('Q') & 0x8000) && !g_wireMode && !g_circuitWireMode) {
        const bool digging = g_survival && g_playerOn && (GetAsyncKeyState(VK_RBUTTON) & 0x8000);
        drawBrushOutline(hdc, ax, ay, digging ? digRadius() : buildRadius());
    }

    /* Once the first endpoint is selected, keep a faint cable attached to the
       live reticle. It is intentionally a preview in screen space rather than
       a world wire: it must follow the mouse smoothly and cost no simulation. */
    if (g_circuitWireMode && g_circuitWireFrom >= 0 &&
        g_circuitWireFrom < MAX_DEVICES && g_devices[g_circuitWireFrom].used) {
        const Device& from = g_devices[g_circuitWireFrom];
        const int tx = from.x + (circuitHasSeparatePorts(from.type) && g_circuitWireFromPort ? DEV_W - 2 : 1);
        const int ty = from.y + DEV_H / 2;
        const int fx = PANEL_W + (tx - g_camX) * cellPixels() + cellPixels() / 2;
        const int fy = (ty - g_camY) * cellPixels() + cellPixels() / 2;

        HPEN edge = CreatePen(PS_SOLID, 3, RGB(0, 0, 0));
        HGDIOBJ oldPen = SelectObject(hdc, edge);
        MoveToEx(hdc, fx, fy, NULL); LineTo(hdc, gx, gy);
        SelectObject(hdc, oldPen); DeleteObject(edge);
        HPEN cable = CreatePen(PS_DOT, 1, RGB(202, 140, 255));
        oldPen = SelectObject(hdc, cable);
        MoveToEx(hdc, fx, fy, NULL); LineTo(hdc, gx, gy);
        SelectObject(hdc, oldPen); DeleteObject(cable);
        drawCross(hdc, fx, fy, RGB(255, 220, 120), 1, 3);
    }

    const bool snapped = g_wireMode && (aim.x != aim.ghostX || aim.y != aim.ghostY);
    if (aim.clamped || snapped) {
        /* The BRIGHT crosshair follows the mouse and the ghost is left behind
           at the reach limit -- the reverse of how this started.

           Pinning the bright one to the reach limit meant the thing your eye
           tracks stopped moving whenever you left the circle, which reads as
           the game having stuttered rather than as a rule about your arms. The
           pointer should always answer "where am I pointing"; the ghost is a
           second, quieter answer to "and where can I actually act". Leaving the
           dim marker behind says that without ever taking the cursor away from
           the hand moving it.

           The dotted tether still joins them, and is what makes the pair read
           as one cursor with a constraint rather than as two cursors. */
        HPEN pen = CreatePen(PS_DOT, 1, RGB(70, 78, 92));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        MoveToEx(hdc, ax, ay, NULL);
        LineTo(hdc, gx, gy);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);

        drawCross(hdc, ax, ay, snapped ? RGB(232, 151, 72) : RGB(150, 158, 174), 3, 4);
    }
    if (g_circuitWireMode)  drawCircuitReticle(hdc, gx, gy, g_circuitWireFrom >= 0);
    else if (g_digFilterOn) drawFilterReticle(hdc, gx, gy);
    else                    drawCross(hdc, gx, gy, RGB(236, 240, 248), 3, 5);

    /* The line preview. Solid rather than dotted, and drawn on top of the
       tether, because the tether means "you cannot reach that" and this means
       "this is what will happen" -- two different messages that should not look
       alike. A ring at the anchor says which end is pinned, which is the only
       thing about a line drag that is not obvious from watching it. */
    if (g_lineOn) {
        const int lx = PANEL_W + (g_lineX - g_camX) * cellPixels() + cellPixels() / 2;
        const int ly = (g_lineY - g_camY) * cellPixels() + cellPixels() / 2;

        HPEN edge = CreatePen(PS_SOLID, 3, RGB(0, 0, 0));
        HGDIOBJ o = SelectObject(hdc, edge);
        MoveToEx(hdc, lx, ly, NULL); LineTo(hdc, ax, ay);
        SelectObject(hdc, o); DeleteObject(edge);

        HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 214, 120));
        o = SelectObject(hdc, pen);
        MoveToEx(hdc, lx, ly, NULL); LineTo(hdc, ax, ay);
        SelectObject(hdc, o); DeleteObject(pen);

        drawCross(hdc, lx, ly, RGB(255, 214, 120), 2, 3);
    }

    if (g_wireOn) {
        const int wx = PANEL_W + (g_wireX - g_camX) * cellPixels() + cellPixels() / 2;
        const int wy = (g_wireY - g_camY) * cellPixels() + cellPixels() / 2;
        HPEN edge = CreatePen(PS_SOLID, 3, RGB(0, 0, 0));
        HGDIOBJ o = SelectObject(hdc, edge);
        MoveToEx(hdc, wx, wy, NULL); LineTo(hdc, ax, ay);
        SelectObject(hdc, o); DeleteObject(edge);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(232, 151, 72));
        o = SelectObject(hdc, pen);
        MoveToEx(hdc, wx, wy, NULL); LineTo(hdc, ax, ay);
        SelectObject(hdc, o); DeleteObject(pen);
        drawCross(hdc, wx, wy, RGB(232, 151, 72), 2, 3);
    }
}

/* Dim the world behind a modal, by halving every channel of the frame we are
   about to blit.

   This used to be a checkerboard of SetPixelV calls, on the theory that GDI had
   no cheap per-pixel alpha. That was wrong twice over. It was catastrophically
   slow -- 196k GDI calls measured at 511 ms per frame, which is the 2fps the
   creative grid opened at -- and it was solving the problem in the wrong place.
   The frame is our own memory before it is handed to GDI at all, so there is
   nothing to blend against and no call to make: it is one pass of a shift and a
   mask, measured at 0.06 ms, eight thousand times faster than the version it
   replaces and thirty-seven times faster than an AlphaBlend of the window.

   It also runs at 512x384 rather than 1024x768, since it happens before the 2x
   upscale -- a quarter of the pixels for free.

   The panel and hotbar are drawn afterwards and so stay bright behind the
   overlay. That is deliberate for the creative grid, where watching the hotbar
   fill up as you click is the point. */
static void dimPixels() {
    for (int i = 0; i < VIEW_CELLS_W * VIEW_CELLS_H; ++i)
        g_pixels[i] = (g_pixels[i] >> 1) & 0x7F7F7F;
}

static void drawCreative(HDC hdc) {
    FillRect(hdc, &g_crePanel, g_panelBg);
    FrameRect(hdc, &g_crePanel, g_accentBrush);

    HGDIOBJ oldFont = SelectObject(hdc, g_font);
    SetBkMode(hdc, TRANSPARENT);
    const bool signalPicker = g_signalPickerDevice >= 0;

    RECT title = g_crePanel;
    title.top += 10; title.left += 14;
    SetTextColor(hdc, RGB(226, 190, 90));
    DrawTextA(hdc, signalPicker ? "SELECT CIRCUIT SIGNAL  --  search or choose"
                   : g_filterDevice >= 0 ? "SELECT DRAIN FILTER  --  click a material, or clear for all"
                   : g_digFilterPicking ? digFilterTitle()
                   : "CREATIVE  --  click a swatch to fill the cursor; click a slot to drop it, right-click for half", -1, &title,
              DT_LEFT | DT_TOP | DT_SINGLELINE);

    FillRect(hdc, &g_creSearchBox, g_btnBg);
    FrameRect(hdc, &g_creSearchBox, g_creSearchFocus ? g_accentBrush : g_borderBrush);
    char searchLabel[64]; sprintf(searchLabel, "search: %s", g_creSearch[0] ? g_creSearch : "");
    SetTextColor(hdc, RGB(200, 206, 218));
    DrawTextA(hdc, searchLabel, -1, &g_creSearchBox, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    for (int i = 0; i < g_creCount; ++i) {
        const int it = g_creItem[i];
        const RECT& r = g_creRect[i];
        /* Scrolled out of the window: layoutCreative left the rect empty, and
           an empty rect must not be painted or it lands as a stripe at the
           panel's top-left corner. */
        if (IsRectEmpty(&r)) continue;
        const int have = signalPicker ? 0 : g_inv.countOf((ItemId)it);

        /* Swatches are made and destroyed per frame here rather than cached,
           unlike the palette's. This is a modal screen that is open for a
           second at a time, and 40 brush creations once in a while is nothing
           next to keeping a second parallel array in step with ITEMS[]. */
        /* Every entry reserves the same icon box. Materials now use their
           generated 14px sprites here too, so the creative list no longer
           falls back to anonymous colour swatches. */
        HBRUSH iconSpace = CreateSolidBrush(RGB(36, 40, 49));
        drawButton(hdc, r, signalPicker ? circuitSignalName(it) : ITEMS[it].name,
                   iconSpace, have > 0, inRect(r, g_mx, g_my));
        DeleteObject(iconSpace);
        RECT ir = { r.left + 4, r.top + 1, r.left + 22, r.bottom - 1 };
        FillRect(hdc, &ir, g_panelBg);
        if (signalPicker) drawCircuitSignalIcon(hdc, ir, it);
        else              drawItemIcon(hdc, ir, (ItemId)it);

        /* Ticked, when this grid is being used to build the dig whitelist. The
           accent frame is the whole feedback for a toggle -- without it the
           panel is a list of things you have clicked and cannot see. */
        if (g_digFilterPicking && it > MAT_EMPTY && it < MAT_COUNT && g_digFilterMat[it]) {
            RECT tick = r;
            FrameRect(hdc, &tick, g_accentBrush);
            InflateRect(&tick, -1, -1);
            FrameRect(hdc, &tick, g_accentBrush);
        }

        /* What you are already carrying, right-aligned, so the grid doubles as
           a readout of the pack -- otherwise you cannot tell a click landed. */
        if (have > 0) {
            char n[16];
            if (have >= 10000) sprintf(n, "%dk", have / 1000);
            else               sprintf(n, "%d", have);
            RECT cr = r; cr.right -= 7;
            SetTextColor(hdc, RGB(150, 210, 150));
            DrawTextA(hdc, n, -1, &cr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    }

    /* The bar. Drawn even when the whole list fits, so the panel does not
       change width the moment somebody adds the material that makes it
       scroll -- a layout that reflows on a content threshold is a layout that
       looks broken exactly once and nobody can reproduce it. */
    {
        FillRect(hdc, &g_creTrack, g_btnBg);
        if (g_creRowCount > CRE_VIS_ROWS) {
            FillRect(hdc, &g_creThumb, inRect(g_creTrack, g_mx, g_my)
                                       ? g_btnBgHot : g_btnBgSel);
            FrameRect(hdc, &g_creThumb, g_borderBrush);
        }
    }

    /* --- the pack ---------------------------------------------------------
       Forty squares in the same grid the hotbar is the bottom row of. Drawn
       with the hotbar row highlighted and the held slot ringed, so the screen
       answers "which of these am I actually swinging" without being asked. */
    if (!signalPicker) {
        RECT lr = g_crePanel;
        lr.left = g_packRect[0].left;
        lr.top  = g_packRect[HOTBAR_SLOTS].top - 18;
        SetTextColor(hdc, RGB(150, 156, 168));
        DrawTextA(hdc, "PACK  --  click to lift a stack, right-click for half",
                  -1, &lr, DT_LEFT | DT_TOP | DT_SINGLELINE);

        for (int i = 0; i < INV_SLOTS; ++i) {
            RECT r = g_packRect[i];
            const ItemStack& st = g_inv.slot[i];
            const bool hot   = inRect(r, g_mx, g_my);
            const bool onBar = (i < HOTBAR_SLOTS);
            const bool held  = (i == g_inv.selected);
            FillRect(hdc, &r, hot ? g_btnBgHot : (onBar ? g_btnBgSel : g_btnBg));
            FrameRect(hdc, &r, held ? g_accentBrush : g_borderBrush);
            if (st.empty()) continue;
            RECT ir = r;
            ir.left += 3; ir.right -= 3; ir.top += 2; ir.bottom -= 11;
            drawItemIcon(hdc, ir, st.item);
            /* Counts under the swatch, and only when there is more than one:
               a "1" on every tool is noise on a screen that is already dense. */
            if (st.count > 1) {
                char n[24];
                if (st.count >= 10000) sprintf(n, "%uk", (unsigned)(st.count / 1000));
                else                   sprintf(n, "%u", (unsigned)st.count);
                RECT cr = r; cr.top = r.bottom - 12; cr.right -= 3;
                SetTextColor(hdc, RGB(200, 206, 218));
                DrawTextA(hdc, n, -1, &cr, DT_RIGHT | DT_TOP | DT_SINGLELINE);
            }
        }
    }

    /* --- equipment --- */
    if (!signalPicker) {
        const FlightSpec fly = flightSpec(g_inv);
        const TempSpec temp = g_inv.tempResist();
        RECT lr = g_crePanel;
        lr.left = g_eqRect[0].left;
        lr.top  = g_eqRect[0].top - 20;
        char s[200];
        /* The RESOLVED numbers, not an item's own, for the same reason the tool
           bench states its resolved delay: two pieces of flight gear do not add
           up (see flightSpec), so the only figure worth reading is the one you
           will actually fly at. tempResist() is resolved the same way -- a
           helmet and a suit do not stack -- so this is the number that
           actually moves heatLine()/coldLine(), not either item's own stat. */
        char tempPart[64] = "";
        if (temp.heat || temp.cold)
            sprintf(tempPart, ", %d/%d heat/cold resist", temp.heat, temp.cold);
        if (fly.any())
            sprintf(s, "EQUIPPED  --  climb %.1f cells/s, %.1fs of fuel, reach +%d, speed +%d%%%s",
                    fly.riseCap * 60.0f, (float)fly.fuel / 60.0f,
                    g_inv.reachBonus(), g_inv.speedBonus(), tempPart);
        else
            sprintf(s, "EQUIPPED  --  nothing to fly with, reach +%d, speed +%d%%%s",
                    g_inv.reachBonus(), g_inv.speedBonus(), tempPart);
        SetTextColor(hdc, fly.any() ? RGB(226, 190, 90) : RGB(150, 156, 168));
        DrawTextA(hdc, s, -1, &lr, DT_LEFT | DT_TOP | DT_SINGLELINE);

        for (int i = 0; i < EQ_COUNT; ++i) {
            RECT r = g_eqRect[i];
            const ItemStack& eq = g_inv.equip[i];
            const bool hot = inRect(r, g_mx, g_my);
            FillRect(hdc, &r, hot ? g_btnBgHot : g_btnBg);
            FrameRect(hdc, &r, eq.empty() ? g_borderBrush : g_accentBrush);
            if (!eq.empty()) {
                RECT ir = r; ir.left += 2; ir.top += 2; ir.right -= 2; ir.bottom -= 2;
                drawItemIcon(hdc, ir, eq.item);
            } else {
                /* The slot's own name when it is empty, so the row explains
                   itself rather than being four identical grey squares. */
                SetTextColor(hdc, RGB(110, 116, 128));
                RECT tr = r; tr.top += 10;
                DrawTextA(hdc, EQ_NAMES[i], -1, &tr, DT_CENTER | DT_TOP | DT_SINGLELINE);
            }
        }

        /* The bin. Framed in the warning colour when it holds something, which
           is the whole recovery affordance: a lit bin says "the thing you just
           threw away is still in here". */
        {
            RECT r = g_trashRect;
            const bool hot = inRect(r, g_mx, g_my);
            FillRect(hdc, &r, hot ? g_btnBgHot : g_btnBg);
            FrameRect(hdc, &r, g_trash.empty() ? g_borderBrush : g_warnBrush);
            if (!g_trash.empty()) {
                RECT ir = r; ir.left += 2; ir.top += 2; ir.right -= 2; ir.bottom -= 2;
                drawItemIcon(hdc, ir, g_trash.item);
            } else {
                SetTextColor(hdc, RGB(110, 116, 128));
                RECT tr = r; tr.top += 10;
                DrawTextA(hdc, "Bin", -1, &tr, DT_CENTER | DT_TOP | DT_SINGLELINE);
            }
        }

        /* Drone chips are deliberately beneath their chassis row: player gear
           stays above, companion gear stays below, and the labels identify the
           bay even when two identical drones are equipped. */
        bool anyDroneChipSlot = false;
        for (int d = 0; d < MAX_DRONES; ++d) if (!IsRectEmpty(&g_droneModuleRect[d][0])) anyDroneChipSlot = true;
        if (anyDroneChipSlot) {
            RECT dr = g_crePanel;
            dr.left = g_eqRect[0].left; dr.top = g_droneModuleRect[0][0].top - 20;
            SetTextColor(hdc, RGB(150, 156, 168));
            DrawTextA(hdc, "DRONE CHIPS  --  remove chips before moving a drone", -1, &dr,
                      DT_LEFT | DT_TOP | DT_SINGLELINE);
            for (int d = 0; d < MAX_DRONES; ++d) {
                RECT r = g_droneModuleRect[d][0];
                if (IsRectEmpty(&r)) continue;
                const ItemStack& chip = g_inv.droneModule[d][0];
                const bool hot = inRect(r, g_mx, g_my);
                FillRect(hdc, &r, hot ? g_btnBgHot : g_btnBg);
                FrameRect(hdc, &r, chip.empty() ? g_borderBrush : g_accentBrush);
                if (!chip.empty()) {
                    RECT ir = r; ir.left += 2; ir.top += 2; ir.right -= 2; ir.bottom -= 2;
                    drawItemIcon(hdc, ir, chip.item);
                } else {
                    RECT tr = r; tr.top += 10;
                    DrawTextA(hdc, "Chip", -1, &tr, DT_CENTER | DT_TOP | DT_SINGLELINE);
                }
            }
        }
    }

    /* --- the tool bench --- */
    if (!signalPicker && g_toolSlotCount > 0 && g_toolPackSlot >= 0) {
        const ItemStack& ts = g_inv.slot[g_toolPackSlot];
        const ToolShot sh = toolResolve(ts);

        RECT lr = g_crePanel;
        lr.left = g_toolSlotRect[0].left;
        lr.top  = g_toolSlotRect[0].top - 20;
        char s[128];
        /* State the resolved numbers, not the tool's own -- the delay shown is
           what it will actually fire at with the modules currently in it, which
           is the only version of the number worth reading. */
        if (sh.canFire)
            sprintf(s, "%s  --  %d slots, %d frame delay, power %d, pierce %d",
                    ITEMS[ts.item].name, g_toolSlotCount, sh.delay, sh.power, sh.pierce);
        else
            sprintf(s, "%s  --  %d slots, empty (install a module to fire)",
                    ITEMS[ts.item].name, g_toolSlotCount);
        SetTextColor(hdc, sh.canFire ? RGB(226, 190, 90) : RGB(150, 156, 168));
        DrawTextA(hdc, s, -1, &lr, DT_LEFT | DT_TOP | DT_SINGLELINE);

        for (int i = 0; i < g_toolSlotCount; ++i) {
            RECT r = g_toolSlotRect[i];
            const ItemId m = ts.inst ? g_toolInst[ts.inst].slot[i] : ITEM_NONE;
            const bool hot = inRect(r, g_mx, g_my);
            FillRect(hdc, &r, hot ? g_btnBgHot : g_btnBg);
            FrameRect(hdc, &r, m != ITEM_NONE ? g_accentBrush : g_borderBrush);
            if (m != ITEM_NONE) {
                RECT sw = r;
                sw.left += 2; sw.right -= 2; sw.top += 2; sw.bottom -= 2;
                drawItemIcon(hdc, sw, m);
            }
        }

        /* The payload slot: ammunition, not a module, so it gets its own
           colour (the orange the blast module's shot already uses, since
           both read as "this changes what leaves the muzzle") and shows a
           COUNT the way module slots never need to -- see ToolInst::payload. */
        if (ts.inst) {
            RECT r = g_toolPayloadRect;
            const ItemStack& pl = g_toolInst[ts.inst].payload;
            const bool hot = inRect(r, g_mx, g_my);
            FillRect(hdc, &r, hot ? g_btnBgHot : g_btnBg);
            FrameRect(hdc, &r, pl.empty() ? g_borderBrush : g_accentBrush);
            if (!pl.empty()) {
                RECT sw = r;
                sw.left += 2; sw.right -= 2; sw.top += 2; sw.bottom -= 2;
                drawItemIcon(hdc, sw, pl.item);
                char cnt[16]; sprintf(cnt, "%u", pl.count);
                SetTextColor(hdc, RGB(20, 22, 28));
                RECT ct = r; ct.left += 2; ct.top = r.bottom - 13;
                DrawTextA(hdc, cnt, -1, &ct, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
            } else {
                SetTextColor(hdc, RGB(110, 116, 128));
                RECT tr = r; tr.top += 10;
                DrawTextA(hdc, "ammo", -1, &tr, DT_CENTER | DT_TOP | DT_SINGLELINE);
            }
        }
    }

    drawButton(hdc, g_creClear, signalPicker ? "Cancel"
                   : g_filterDevice >= 0 ? "All materials"
                   : g_digFilterPicking ? "Untick all"
                   : "Empty pack",
               NULL, false, inRect(g_creClear, g_mx, g_my));
    {
        RECT hint = g_crePanel;
        hint.left = g_creClear.right + 12; hint.top = g_creClear.top + 4;
        SetTextColor(hdc, RGB(120, 126, 138));
        DrawTextA(hdc, "Tab or Esc to close", -1, &hint, DT_LEFT | DT_TOP | DT_SINGLELINE);
    }
    /* --- what the cursor is carrying -------------------------------------
       Drawn LAST and at the pointer, over every panel on the screen, because
       that is the entire illusion: the stack is attached to the mouse rather
       than living in a slot. Anything drawn after it would break that, which is
       why this sits at the bottom of the function and not with the pack.

       Offset down and right by a few pixels so the system arrow is still
       readable on top of it -- a stack drawn centred on the hotspot swallows
       the pointer and you lose track of where you are actually clicking. */
    if (!signalPicker && !g_drag.empty()) {
        RECT r = { g_mx + 8, g_my + 8, g_mx + 8 + 30, g_my + 8 + 30 };
        FillRect(hdc, &r, g_btnBgSel);
        FrameRect(hdc, &r, g_accentBrush);
        RECT ir = r; ir.left += 3; ir.right -= 3; ir.top += 2; ir.bottom -= 11;
        drawItemIcon(hdc, ir, g_drag.item);
        if (g_drag.count > 1) {
            char n[24];
            if (g_drag.count >= 10000) sprintf(n, "%uk", (unsigned)(g_drag.count / 1000));
            else                       sprintf(n, "%u", (unsigned)g_drag.count);
            RECT cr = r; cr.top = r.bottom - 12; cr.right -= 3;
            SetTextColor(hdc, RGB(226, 190, 90));
            DrawTextA(hdc, n, -1, &cr, DT_RIGHT | DT_TOP | DT_SINGLELINE);
        }
    }

    SelectObject(hdc, oldFont);
}

/* A modal overlay, deliberately plain: dim the world behind it so it is
   obviously not interactive, then the two things anyone opens a pause menu
   for. Escape closes it again. */
/* --- the crafting panel ----------------------------------------------------
   Every recipe you can make RIGHT NOW, one per row, click to make one. See
   craft.h for why it is a filtered list rather than a grid.

   Recipes you cannot afford are shown greyed rather than hidden, and that is
   the one real UI decision here. Hiding them makes the panel shorter and makes
   the game unlearnable: you would never discover that wood makes rope until the
   moment you already had wood, and by then you have probably built a staircase.
   Greyed-out rows are a shopping list. */
static RECT g_craftPanel;
static RECT g_craftRow[128];

/* --- making more than one -------------------------------------------------
   Crafting a hundred platform one click at a time is not a decision repeated a
   hundred times, it is the same decision and ninety-nine chores. Two ways out,
   and they answer different questions:

     HOLD    keep the button down and it repeats, accelerating. For "I want a
             lot and I will stop when it looks right" -- you watch the number
             climb and let go.
     SHIFT   one click, sixty-four made. For "I know exactly how many", which
             is what a stack is.

   The ramp rather than a fixed repeat rate because both ends matter: the first
   repeat has to be slow enough that a slightly long click does not make five of
   something expensive, and the tail has to be fast enough that a hundred is a
   second rather than a chore with a different shape. */
static const int  CRAFT_REPEAT_DELAY = 26;   /* frames before the first repeat */
static const int  CRAFT_REPEAT_MIN   = 1;    /* the floor it accelerates to */
static const int  CRAFT_SHIFT_BATCH  = 64;
static int g_craftHeldRow  = -1;   /* which row the button is down on */
static int g_craftHeldFor  = 0;    /* frames it has been down */
static int g_craftCool     = 0;    /* frames until the next repeat */
bool g_craftOpen = false;

/* A fixed window of rows with the rest scrolled past, exactly the shape the
   creative palette already uses -- see g_paletteScroll's own note for why
   this matters more every time the list grows: it went from 8 recipes to
   over 70 in one pass, and a panel sized to hold every row unconditionally
   would now run well past the bottom of the window. */
static const int CRAFT_VIS_ROWS = 14;
int  g_craftScroll = 0;
static RECT g_craftTrack, g_craftThumb;

static void layoutCraft() {
    /* Stations are read fresh here rather than in craftCan() itself --
       see the note on craftScanStations() in craft.h for why this has to
       be a per-frame call rather than something craftCan() triggers on its
       own, and layoutCraft() already runs once a frame for exactly as long
       as the panel is open (drawCraft() calls it at its own top). */
    craftScanStations(g_world, g_player);

    /* 430, up from the 260 this panel had when every recipe took one or two
       ingredients. Three-part recipes arrived with the station ladder, and a
       cost line like "0/2 Gold  0/2 Steel  1/1 Glass" is most of a row on its
       own -- at the old width the label beside it ellipsised down to "A...".
       The viewport is 1024 wide, so the room was there for the asking. */
    const int w = 430;
    const int visRows = imin(CRAFT_VIS_ROWS, N_RECIPES);
    const int maxScroll = imax(0, N_RECIPES - CRAFT_VIS_ROWS);
    g_craftScroll = imax(0, imin(g_craftScroll, maxScroll));

    const int h = 46 + visRows * 30 + 12;
    const int cx = PANEL_W + VIEW_W / 2, cy = VIEW_H / 2;
    SetRect(&g_craftPanel, cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);

    const int barW = 10;
    /* Rects for rows scrolled out of the visible window are set EMPTY, not
       merely skipped when drawing -- inRect() then fails on them for free,
       so a click cannot land on a row that is not on screen. Same
       reasoning as the creative palette's own g_creRect. */
    for (int i = 0; i < N_RECIPES && i < 128; ++i) {
        const int row = i - g_craftScroll;
        if (row < 0 || row >= visRows) { SetRectEmpty(&g_craftRow[i]); continue; }
        SetRect(&g_craftRow[i], g_craftPanel.left + 12, g_craftPanel.top + 40 + row * 30,
                g_craftPanel.right - 12 - barW - 4, g_craftPanel.top + 40 + row * 30 + 26);
    }

    const int trackX = g_craftPanel.right - 12 - barW;
    const int trackY0 = g_craftPanel.top + 40, trackY1 = trackY0 + visRows * 30 - 4;
    SetRect(&g_craftTrack, trackX, trackY0, trackX + barW, trackY1);
    if (maxScroll > 0) {
        const int trackH = trackY1 - trackY0;
        const int thumbH = imax(20, trackH * visRows / N_RECIPES);
        const int travel  = trackH - thumbH;
        const int thumbY  = trackY0 + travel * g_craftScroll / maxScroll;
        SetRect(&g_craftThumb, trackX, thumbY, trackX + barW, thumbY + thumbH);
    } else {
        g_craftThumb = g_craftTrack;
    }
}

static bool handleCraftClick(int mx, int my) {
    if (!g_craftOpen) return false;
    for (int i = 0; i < N_RECIPES && i < 128; ++i)
        if (inRect(g_craftRow[i], mx, my)) {
            /* Shift makes a stack in one go. Stopping the moment one fails --
               out of ingredients, or no room for the result -- rather than
               pushing on: craftMake is already all-or-nothing per item, and
               sixty-four attempts against an empty pack should cost nothing
               and change nothing. */
            const int n = (GetKeyState(VK_SHIFT) & 0x8000) ? CRAFT_SHIFT_BATCH : 1;
            for (int k = 0; k < n; ++k)
                if (!craftMake(g_inv, i)) break;
            /* Arm the repeat on THIS row, so sliding the mouse onto another
               recipe mid-hold does not silently start making that one. */
            g_craftHeldRow = i;
            g_craftHeldFor = 0;
            g_craftCool    = CRAFT_REPEAT_DELAY;
            return true;
        }
    /* Clicking the track pages toward the click -- the same gesture every
       scrollbar supports, and it needs no drag handling to be useful since
       the wheel already covers fine scrolling. */
    if (inRect(g_craftTrack, mx, my)) {
        const int maxScroll = imax(0, N_RECIPES - CRAFT_VIS_ROWS);
        if (maxScroll > 0)
            g_craftScroll += (my < g_craftThumb.top) ? -CRAFT_VIS_ROWS : CRAFT_VIS_ROWS;
        return true;
    }
    /* Anywhere else inside the panel is swallowed, so a miss does not dig a
       hole in the world behind it. */
    return inRect(g_craftPanel, mx, my);
}

static void drawCraft(HDC hdc) {
    layoutCraft();
    FillRect(hdc, &g_craftPanel, g_panelBg);
    FrameRect(hdc, &g_craftPanel, g_accentBrush);

    HGDIOBJ oldFont = SelectObject(hdc, g_font);
    SetBkMode(hdc, TRANSPARENT);

    RECT title = g_craftPanel; title.top += 12;
    SetTextColor(hdc, RGB(226, 190, 90));
    DrawTextA(hdc, "CRAFTING", -1, &title, DT_CENTER | DT_TOP | DT_SINGLELINE);

    for (int i = 0; i < N_RECIPES && i < 128; ++i) {
        RECT r = g_craftRow[i];
        if (r.right <= r.left) continue;   /* scrolled out of the window */
        const Recipe& rc = RECIPES[i];
        /* Missing the station specifically, as against missing an ingredient
           -- the row greys out either way, but the READER needs to know which
           one to go fix, and "go build an anvil" and "go find more copper"
           are different errands.

           craftHasStation() rather than deriving it from craftCan(), which
           cannot answer this: craftCan folds both failures into one false, so
           the first version of this line worked out to "!can && hasStation"
           and told you to go build an anvil whenever you were STANDING at one
           and merely short of copper -- hiding the ingredient shortfall on
           every gated row, which is the whole point of the greyed-out list. */
        const bool stationMissing = !craftHasStation(i);
        const bool can = craftCan(g_inv, i);
        const bool hot = can && inRect(r, g_mx, g_my);

        FillRect(hdc, &r, hot ? g_btnBgHot : g_btnBg);
        FrameRect(hdc, &r, can ? g_borderBrush : g_panelBg);

        /* The output's own swatch, the same one the hotbar uses, so a row and
           the thing it makes are recognisably the same object. */
        RECT sw = { r.left + 5, r.top + 4, r.left + 21, r.bottom - 4 };
        drawItemIcon(hdc, sw, rc.out);

        /* The RIGHT-HAND text is measured and laid out FIRST, so the label can
           be told where to stop. Both are drawn into the same row rect --
           label left, cost right -- which was fine while no recipe had more
           than two ingredients, and stopped being fine the moment three-part
           recipes arrived: "Assembly Table" against "0/2 Gold  0/2 Steel
           1/1 Glass" drew the two strings straight through each other. */
        char right[128]; right[0] = 0;
        if (stationMissing) {
            /* The station name stands in for the cost line when the station
               itself is what is missing -- ingredients do not matter yet if
               there is nowhere to work them, so showing both would say two
               things at once and the station is the more useful one. */
            sprintf(right, "need: %s", STATION_NAMES[rc.station]);
        } else {
            /* What it costs, coloured per ingredient so a row you cannot
               afford says WHICH part you are short of rather than just being
               dim. That is the difference between a locked door and a sign. */
            for (int k = 0; k < CRAFT_MAX_IN; ++k) {
                if (rc.in[k].item == ITEM_NONE || rc.in[k].count <= 0) continue;
                char one[48];
                const int have = g_inv.countOf(rc.in[k].item);
                sprintf(one, "%s%d/%d %s", right[0] ? "  " : "",
                        have > rc.in[k].count ? rc.in[k].count : have,
                        rc.in[k].count, ITEMS[rc.in[k].item].name);
                if (strlen(right) + strlen(one) < sizeof(right) - 1) strcat(right, one);
            }
        }
        RECT meas = { 0, 0, 0, 0 };
        DrawTextA(hdc, right, -1, &meas, DT_CALCRECT | DT_SINGLELINE);
        const int rightW = meas.right - meas.left;

        /* The output count belongs alongside its name, not its ingredients:
           crafting is where you decide whether another lamp or drone is worth
           making, and the useful answer is how many of THAT result you already
           own. Equipment is intentionally excluded because it is worn, not in
           the available inventory that a new craft will enter. */
        char output[112];
        sprintf(output, "%s  [%d]", rc.label, g_inv.countOf(rc.out));
        SetTextColor(hdc, can ? RGB(226, 232, 244) : RGB(104, 110, 122));
        RECT t = r; t.left = sw.right + 8;
        /* Ellipsised rather than simply clipped, so a truncated name reads as
           truncated instead of as a different item. */
        t.right = imax(t.left, r.right - 8 - rightW - 8);
        DrawTextA(hdc, output, -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        SetTextColor(hdc, stationMissing ? RGB(190, 150, 110)
                                         : (can ? RGB(150, 190, 140) : RGB(190, 120, 110)));
        RECT ct = r; ct.right -= 8;
        DrawTextA(hdc, right, -1, &ct, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    }

    /* The scrollbar, same track-plus-thumb the creative palette draws --
       when nothing needs scrolling g_craftThumb equals g_craftTrack (set in
       layoutCraft()), so this just paints one full bar rather than needing
       a separate "hide it" case. */
    FillRect(hdc, &g_craftTrack, g_btnBg);
    FillRect(hdc, &g_craftThumb, g_borderBrush);

    RECT hint = g_craftPanel;
    hint.top = g_craftPanel.bottom - 20;
    SetTextColor(hdc, RGB(120, 126, 138));
    DrawTextA(hdc, "C to close", -1, &hint, DT_CENTER | DT_TOP | DT_SINGLELINE);

    SelectObject(hdc, oldFont);
}

static void drawMenu(HDC hdc) {
    layoutMenu();

    FillRect(hdc, &g_menuPanel, g_panelBg);
    FrameRect(hdc, &g_menuPanel, g_accentBrush);

    HGDIOBJ oldFont = SelectObject(hdc, g_font);
    SetBkMode(hdc, TRANSPARENT);

    RECT title = g_menuPanel;
    title.top += 14;
    SetTextColor(hdc, RGB(226, 190, 90));
    DrawTextA(hdc, "PAUSED", -1, &title, DT_CENTER | DT_TOP | DT_SINGLELINE);

    drawButton(hdc, g_menuResume, "Resume", NULL, false, inRect(g_menuResume, g_mx, g_my));
    drawButton(hdc, g_menuQuit,   "Quit",   NULL, false, inRect(g_menuQuit,   g_mx, g_my));

    /* Two columns: the key on the left, what it does on the right. Aligned on
       a fixed split rather than measured per row, because the alternative is a
       ragged left edge on the descriptions, which is what makes a list of this
       length hard to scan. */
    const int keyX  = g_menuPanel.left + 16;
    const int whatX = g_menuPanel.left + 130;
    int ry = g_menuQuit.bottom + 14;
    for (int i = 0; i < N_KEY_HINTS; ++i, ry += 15) {
        SetTextColor(hdc, RGB(226, 190, 90));
        RECT kr = { keyX, ry, whatX - 6, ry + 14 };
        DrawTextA(hdc, KEY_HINTS[i].key, -1, &kr, DT_LEFT | DT_TOP | DT_SINGLELINE);
        SetTextColor(hdc, RGB(176, 182, 194));
        RECT wr = { whatX, ry, g_menuPanel.right - 12, ry + 14 };
        DrawTextA(hdc, KEY_HINTS[i].what, -1, &wr, DT_LEFT | DT_TOP | DT_SINGLELINE);
    }

    /* --- where the save's bytes went ------------------------------------
       Here rather than on the HUD because it is a thing to STUDY, not glance
       at: the interesting part is watching a section grow as the game does,
       and that is a comparison across sessions rather than a number you keep
       an eye on while playing.

       Biggest first, and only sections above a kilobyte -- below that a row is
       noise, and eight rows of noise would bury the two that matter. */
    if (saveTotalBytes() > 0) {
        int ry = g_menuQuit.bottom + 14 + N_KEY_HINTS * 15 + 10;
        char s[128];
        sprintf(s, "last save  %.2f MB", (double)saveTotalBytes() / (1024.0 * 1024.0));
        SetTextColor(hdc, RGB(226, 190, 90));
        RECT tr = { g_menuPanel.left + 16, ry, g_menuPanel.right - 12, ry + 14 };
        DrawTextA(hdc, s, -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE);
        ry += 16;
        const SaveStat* st = saveStats();
        for (int i = 0; i < saveStatCount(); ++i) {
            if (st[i].bytes < 1024) break;
            if (st[i].bytes >= 1024 * 1024)
                sprintf(s, "%.2f MB", (double)st[i].bytes / (1024.0 * 1024.0));
            else
                sprintf(s, "%.1f KB", (double)st[i].bytes / 1024.0);
            SetTextColor(hdc, RGB(150, 156, 168));
            RECT nr = { g_menuPanel.left + 24, ry, g_menuPanel.left + 200, ry + 14 };
            DrawTextA(hdc, st[i].name, -1, &nr, DT_LEFT | DT_TOP | DT_SINGLELINE);
            SetTextColor(hdc, RGB(190, 196, 208));
            RECT vr = { g_menuPanel.left + 200, ry, g_menuPanel.right - 16, ry + 14 };
            DrawTextA(hdc, s, -1, &vr, DT_RIGHT | DT_TOP | DT_SINGLELINE);
            ry += 15;
        }
    }

    RECT hint = g_menuPanel;
    hint.top = g_menuPanel.bottom - 22;
    SetTextColor(hdc, RGB(120, 126, 138));
    DrawTextA(hdc, "Esc to resume", -1, &hint, DT_CENTER | DT_TOP | DT_SINGLELINE);

    SelectObject(hdc, oldFont);
}

static void drawPanel(HDC hdc) {
    RECT panel = { 0, 0, PANEL_W, WIN_H };
    FillRect(hdc, &panel, g_panelBg);

    HGDIOBJ oldFont = SelectObject(hdc, g_font);
    SetBkMode(hdc, TRANSPARENT);
    drawText(hdc, 10, 9, RGB(240, 240, 246), "CRUCIBLE");
    drawText(hdc, 94, 9, RGB(144, 154, 172), "catalog (wheel)");

    for (int i = 0; i < N_PALETTE; ++i) {
        if (g_paletteRect[i].right <= g_paletteRect[i].left) continue;
        const bool hot = inRect(g_paletteRect[i], g_mx, g_my);
        if (i < N_BRUSH) {
            const bool sel = g_paletteDevice < 0 && BRUSHES[i].brush == g_brushMat;
            drawButton(hdc, g_paletteRect[i], BRUSHES[i].label, g_swatchBrush[i], sel, hot);
        } else {
            const int type = i - N_BRUSH;
            drawButton(hdc, g_paletteRect[i], DEVS[type].name, NULL,
                       g_paletteDevice == type, hot);
        }
    }
    if (g_paletteMaxScroll > 0) {
        RECT rail = g_paletteScrollTrack;
        FillRect(hdc, &rail, g_btnBg);
        FrameRect(hdc, &rail, g_borderBrush);
        RECT thumb = g_paletteScrollThumb;
        FillRect(hdc, &thumb, g_btnBgHot);
        FrameRect(hdc, &thumb, g_accentBrush);
    }

    /* brush size row */
    drawButton(hdc, g_sizeDec, "-", NULL, false, inRect(g_sizeDec, g_mx, g_my));
    drawButton(hdc, g_sizeInc, "+", NULL, false, inRect(g_sizeInc, g_mx, g_my));
    {
        RECT rr = g_sizeBox;
        FillRect(hdc, &rr, g_btnBg);
        FrameRect(hdc, &rr, g_borderBrush);
        /* Show the clamp when one is in force. Displaying "Size 20" while the
           hands work at 6 is a straightforward lie, and the player's conclusion
           would be that the size control is broken rather than that their arms
           are. */
        char s[40];
        /* Only ever a cap on DIGGING now, so the label says so -- "Size 20 -> 6"
           on its own reads as though building were limited too, which it is
           not. */
        const bool capped = g_survival && g_playerOn && digRadius() < g_brushRadius;
        if (capped) sprintf(s, "Size %d  (dig %d)", g_brushRadius, digRadius());
        else        sprintf(s, "Size %d", g_brushRadius);
        SetTextColor(hdc, capped ? RGB(226, 190, 90) : RGB(214, 216, 224));
        DrawTextA(hdc, s, -1, &rr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* speed row */
    for (int i = 0; i < N_SPEED; ++i) {
        char sp[8]; sprintf(sp, "%dx", SPEEDS[i]);
        RECT rr = g_speedRect[i];
        const bool sel = (i == g_speedIdx);
        FillRect(hdc, &rr, sel ? g_btnBgSel : (inRect(rr, g_mx, g_my) ? g_btnBgHot : g_btnBg));
        FrameRect(hdc, &rr, sel ? g_accentBrush : g_borderBrush);
        SetTextColor(hdc, sel ? RGB(255, 236, 180) : RGB(214, 216, 224));
        DrawTextA(hdc, sp, -1, &rr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    for (int i = 0; i < N_ZOOM; ++i) {
        char zp[12]; sprintf(zp, "Zoom %dx", ZOOMS[i]);
        RECT rr = g_zoomRect[i];
        const bool sel = (i == g_zoomIdx);
        FillRect(hdc, &rr, sel ? g_btnBgSel : (inRect(rr, g_mx, g_my) ? g_btnBgHot : g_btnBg));
        FrameRect(hdc, &rr, sel ? g_accentBrush : g_borderBrush);
        SetTextColor(hdc, sel ? RGB(255, 236, 180) : RGB(214, 216, 224));
        DrawTextA(hdc, zp, -1, &rr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* toggles / actions, with live state in the label */
    char lbl[32];
    sprintf(lbl, "Overwrite: %s", g_overwrite ? "On" : "Off");
    drawButton(hdc, g_actRect[ACT_OVERWRITE], lbl, NULL, !g_overwrite, inRect(g_actRect[ACT_OVERWRITE], g_mx, g_my));
    drawButton(hdc, g_actRect[ACT_LAYER], g_bgLayer ? "Layer: Background" : "Layer: Foreground",
               NULL, g_bgLayer, inRect(g_actRect[ACT_LAYER], g_mx, g_my));
    drawButton(hdc, g_actRect[ACT_WIRE], g_wireMode ? "Wire: Copper (F)" : "Wire: Off (F)",
               NULL, g_wireMode, inRect(g_actRect[ACT_WIRE], g_mx, g_my));
    {
        char circuitLabel[48];
        if (g_circuitWireMode && g_circuitWireFrom >= 0) sprintf(circuitLabel, "Circuit: choose device");
        else sprintf(circuitLabel, g_circuitWireMode ? "Circuit Wire: On (X)" : "Circuit Wire: Off (X)");
        drawButton(hdc, g_actRect[ACT_CIRCUIT], circuitLabel, NULL, g_circuitWireMode,
                   inRect(g_actRect[ACT_CIRCUIT], g_mx, g_my));
    }
    const char* vn = g_view == VIEW_NORMAL ? "View: Glow" :
                     g_view == VIEW_MATERIAL ? "View: Material" : "View: Heat";
    drawButton(hdc, g_actRect[ACT_VIEW], vn, NULL, g_view != VIEW_NORMAL, inRect(g_actRect[ACT_VIEW], g_mx, g_my));
    drawButton(hdc, g_actRect[ACT_LIGHT], g_lightOn ? "Light: on" : "Light: off",
               NULL, g_lightOn, inRect(g_actRect[ACT_LIGHT], g_mx, g_my));
    {
        const char* pl = !g_playerOn ? "Player: Off"
                       : g_player.buried ? "Player: Stuck" : "Player: On";
        drawButton(hdc, g_actRect[ACT_PLAYER], pl, NULL, g_playerOn, inRect(g_actRect[ACT_PLAYER], g_mx, g_my));
    }
    drawButton(hdc, g_actRect[ACT_PAUSE], g_paused ? "Paused" : "Pause", NULL, g_paused, inRect(g_actRect[ACT_PAUSE], g_mx, g_my));
    /* The filter button carries its own state in its label -- how many
       materials are ticked -- because the mode is otherwise only visible at the
       cursor, and a mode you can leave on without noticing is a mode that will
       be blamed for a tool that "stopped working". */
    {
        char fl[48];
        if (g_digFilterOn) sprintf(fl, "Filter: ON (%d)", digFilterCount());
        else               strcpy(fl, "Filter: Off");
        drawButton(hdc, g_actRect[ACT_FILTER], fl, NULL, g_digFilterOn,
                   inRect(g_actRect[ACT_FILTER], g_mx, g_my));
    }
    drawButton(hdc, g_actRect[ACT_CLEAR], "Clear", NULL, false, inRect(g_actRect[ACT_CLEAR], g_mx, g_my));

    /* stats, bottom of the panel */
    char s[96];
    int sy = STATS_TOP;
    /* What the cursor is over, in the material's own colour so it reads as a
       label for the thing under the pointer rather than as another statistic.
       Temperature comes along for free and is the number you usually want when
       you are pointing at something anyway. */
    {
        u32 col = 0x9AA0AA;
        if (hoverCell(s, sizeof(s), &col)) {
            drawText(hdc, 10, sy, RGB((col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF), s);
        } else {
            drawText(hdc, 10, sy, RGB(110, 116, 126), "-");
        }
    }
    sprintf(s, "%.0f fps", g_fps);                         drawText(hdc, 10, sy + 20, RGB(150, 200, 150), s);
    /* Lighting sits beside the sim time because it is the other half of the
       frame, and for a long time it was the larger half without ever saying so.
       What it did matters as much as what it cost: the same millisecond figure
       means something quite different after a recut than after a patch. */
    if (!g_lightOn)                        sprintf(s, "sim %.2f ms   light off", g_simMs);
    else if (g_lightWork == LIGHT_REUSED)  sprintf(s, "sim %.2f ms   light reuse", g_simMs);
    else if (g_lightWork == LIGHT_PATCHED) sprintf(s, "sim %.2f ms   light %d%%", g_simMs, g_lightWorkPct);
    else                                   sprintf(s, "sim %.2f ms   light recut", g_simMs);
    drawText(hdc, 10, sy + 36, RGB(170, 178, 190), s);
    /* The save total shares the cells line rather than taking one of its own.
       A line of its own at sy+84 ran off the bottom edge of the window -- the
       stats block already finishes about ten pixels from it. */
    if (saveTotalBytes() > 0)
        sprintf(s, "cells %d   save %.2f MB", g_cellCount,
                (double)saveTotalBytes() / (1024.0 * 1024.0));
    else
        sprintf(s, "cells %d", g_cellCount);
    drawText(hdc, 10, sy + 52, RGB(170, 178, 190), s);
    sprintf(s, "chunks %d/%d   rooms %d (+%d)", g_world.activeChunks, CHUNK_COUNT,
            roomCount(), g_world.keptChunks);
    drawText(hdc, 10, sy + 68, RGB(170, 178, 190), s);

    /* --- where the save's bytes went ------------------------------------
       Shown after a save or load and then left up, rather than flashed with
       the status line, because the interesting thing about it is watching it
       CHANGE as the game grows -- which you cannot do with a number that
       vanishes after four seconds.

       Biggest section first, and only the ones worth a line: below about a
       kilobyte a section is noise, and eleven rows of noise would bury the
       three that matter. */
    /* The status line last, over the top of everything, since it is the one
       thing that is a REPLY to something you just pressed. */
    if (g_saveMsgFrames > 0) {
        --g_saveMsgFrames;
        RECT r = { PANEL_W + 16, 14, WIN_W - 16, 34 };
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(20, 22, 28));
        RECT sh = r; sh.left += 1; sh.top += 1;
        DrawTextA(hdc, g_saveMsg, -1, &sh, DT_LEFT | DT_TOP | DT_SINGLELINE);
        SetTextColor(hdc, strstr(g_saveMsg, "FAILED") ? RGB(232, 96, 88) : RGB(226, 190, 90));
        DrawTextA(hdc, g_saveMsg, -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE);
    }

    SelectObject(hdc, oldFont);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    initMaterials();
    g_world.reset();
    initItems();
    g_inv.clear();
    layoutPanel();
    layoutHotbar();

    /* Start near the top middle. The world is four screens wide and eight deep,
       so the old "middle of the world" would drop the character four screens
       underground with no way to tell which way was up. */
    makeWorld();
    {
        float sx, sy;
        worldSpawnPoint(&sx, &sy);
        g_player.reset(sx, sy);
    }
    updateCamera(true);

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = wndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "CrucibleWnd";
    RegisterClassA(&wc);

    DWORD style = (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX);
    RECT r = { 0, 0, WIN_W, WIN_H };
    AdjustWindowRect(&r, style, FALSE);

    HWND hwnd = CreateWindowA("CrucibleWnd", "Crucible", style,
                              CW_USEDEFAULT, CW_USEDEFAULT,
                              r.right - r.left, r.bottom - r.top,
                              NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOW);

    g_font = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, FF_DONTCARE, "Consolas");

    {
        HDC sdc = GetDC(hwnd);
        g_backDC     = CreateCompatibleDC(sdc);
        g_backBmp    = CreateCompatibleBitmap(sdc, WIN_W, WIN_H);
        g_backOldBmp = SelectObject(g_backDC, g_backBmp);
        ReleaseDC(hwnd, sdc);
    }
    SetStretchBltMode(g_backDC, COLORONCOLOR);   /* nearest neighbour: crisp pixels */

    g_panelBg     = CreateSolidBrush(RGB(26, 28, 34));
    g_btnBg       = CreateSolidBrush(RGB(42, 46, 56));
    g_btnBgHot    = CreateSolidBrush(RGB(64, 70, 84));
    g_btnBgSel    = CreateSolidBrush(RGB(58, 64, 82));
    g_borderBrush = CreateSolidBrush(RGB(88, 94, 108));
    g_accentBrush = CreateSolidBrush(RGB(226, 190, 90));
    /* The bin's frame when it is holding something. Warm red rather than the
       accent gold, because it means "this is about to be lost", not "this is
       equipped". */
    g_warnBrush   = CreateSolidBrush(RGB(200, 96, 76));
    buildIcons();

    /* Swatch colours come from each material's own palette (a dry, mid-tint
       sample), so the picker always matches what lands in the world. Tools and
       the eraser get synthetic swatches. */
    for (int i = 0; i < N_BRUSH; ++i) {
        int b = BRUSHES[i].brush;
        COLORREF cr;
        if      (b == TOOL_HEAT) cr = RGB(226, 96, 40);
        else if (b == TOOL_COOL) cr = RGB(80, 152, 226);
        else if (b == MAT_EMPTY) cr = RGB(38, 40, 48);
        else {
            u32 c = g_colorLut[(b << 8) | 0x08];
            cr = RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
        }
        g_swatchBrush[i] = CreateSolidBrush(cr);
    }

    memset(&g_bmi, 0, sizeof(g_bmi));
    g_bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    g_bmi.bmiHeader.biWidth       = VIEW_CELLS_W;
    g_bmi.bmiHeader.biHeight      = -VIEW_CELLS_H;   /* negative = top-down rows */
    g_bmi.bmiHeader.biPlanes      = 1;
    g_bmi.bmiHeader.biBitCount    = 32;
    g_bmi.bmiHeader.biCompression = BI_RGB;

    timeBeginPeriod(1);   /* otherwise Sleep granularity is ~15 ms */

    LARGE_INTEGER freq, tPrev, tFpsBase;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&tPrev);
    tFpsBase = tPrev;
    int fpsFrames = 0;

    while (g_running) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g_running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!g_running) break;

        /* --- holding the craft button -------------------------------------
           Repeats while the button stays down on the row it was pressed on,
           accelerating from CRAFT_REPEAT_DELAY toward CRAFT_REPEAT_MIN.

           Driven from the frame loop rather than from mouse messages because
           the rate has to be in FRAMES: WM_MOUSEMOVE arrives when the mouse
           moves, and a held button on a still mouse generates nothing at all. */
        if (g_craftHeldRow >= 0) {
            if (!g_lmb || !g_craftOpen) {
                g_craftHeldRow = -1;
            } else if (--g_craftCool <= 0) {
                ++g_craftHeldFor;
                /* Halve the gap every five repeats, down to the floor. A
                   geometric ramp rather than a linear one because what it has to
                   span is a factor of twenty-six, and stepping down by one frame a
                   time would spend most of the hold in the slow half. */
                int gap = CRAFT_REPEAT_DELAY >> (g_craftHeldFor / 5);
                if (gap < CRAFT_REPEAT_MIN) gap = CRAFT_REPEAT_MIN;
                g_craftCool = gap;
                const int n = (GetKeyState(VK_SHIFT) & 0x8000) ? CRAFT_SHIFT_BATCH : 1;
                for (int k = 0; k < n; ++k)
                    if (!craftMake(g_inv, g_craftHeldRow)) { g_craftHeldRow = -1; break; }
            }
        }

        /* Ticked unconditionally, so a cooldown can never outlive the drag that
           set it and the first click after a pause always acts at once. */
        if (g_digCool > 0) --g_digCool;
        /* Tool cooldowns tick on the instance, not on a global, so two tools
           recharge independently and swapping between them does not reset
           either -- which is the whole reason firing state lives on the
           instance rather than beside the input handler. */
        for (int i = 1; i < MAX_TOOL_INST; ++i)
            if (g_toolInst[i].used && g_toolInst[i].cooldown > 0) --g_toolInst[i].cooldown;
        /* The map covers the viewport, so a click while it is up must not dig
           the world underneath it -- you cannot see what you would be hitting. */
        if (!g_menuOpen && !g_creativeOpen && !g_craftOpen && g_chestOpen < 0 && !g_mapOpen)
            applyBrush();
        /* Whether the sim actually advanced this frame, so the character moves
           in lockstep with the world -- including on a single frame-advance. */
        bool steppedThisFrame = false;

        /* Publish the body's box before stepping, so this frame's simulation
           already respects it -- otherwise sand gets one frame of free passage
           through the player every time they move. */
        if (g_playerOn) g_player.occupy(g_world);
        else            g_world.clearBlockBox();

        /* Tell the world what to simulate. Everything outside is frozen in
           place -- state is kept, it simply does not advance -- which is what
           bounds the cost of a world this size. See setLiveWindow(). */
        g_world.setLiveWindow(g_camX - SIM_MARGIN, g_camY - SIM_MARGIN,
                              g_camX + viewCellsW()  + SIM_MARGIN,
                              g_camY + viewCellsH() + SIM_MARGIN);

        LARGE_INTEGER tA, tB;
        QueryPerformanceCounter(&tA);
        if (g_stepOnce) {
            /* Frame-advance stays one step regardless of the multiplier -- the
               whole point of it is to inspect a single frame of the sim. */
            g_world.step();
            g_stepOnce = false;
            steppedThisFrame = true;
        } else if (!g_paused && !g_menuOpen) {
            for (int s = 0; s < SPEEDS[g_speedIdx]; ++s) g_world.step();
        }
        /* Projectiles move with the world, so the speed multiplier speeds them
           up too -- a shot that crawled at 1x speed through a 4x world would
           look like it was fired underwater. */
        if (steppedThisFrame) projUpdate(g_world);
        else if (!g_paused && !g_menuOpen)
            for (int s = 0; s < SPEEDS[g_speedIdx]; ++s) projUpdate(g_world);
        QueryPerformanceCounter(&tB);
        g_simMs = 1000.0 * (double)(tB.QuadPart - tA.QuadPart) / (double)freq.QuadPart;

        /* The character runs after the world, on the settled grid, and only
           when the sim is actually advancing -- stepping while paused would
           let you walk around a frozen world, which reads as a bug. Input is
           polled rather than event-driven because held keys are the whole
           interface here, and WM_KEYDOWN repeat rates are a user setting. */
        if (g_restBed >= 0 && (!g_devices[g_restBed].used ||
                               g_devices[g_restBed].type != DEV_BED))
            g_restBed = -1;       /* bed removed while resting */
        /* Any movement key wakes the player before the movement input is read.
           Right-clicking the bed also wakes them; neither route needs a hidden
           sleep-only key to remember. */
        if (g_restBed >= 0 && ((GetAsyncKeyState('A') & 0x8000) || (GetAsyncKeyState('D') & 0x8000) ||
                               (GetAsyncKeyState('W') & 0x8000) || (GetAsyncKeyState('S') & 0x8000) ||
                               (GetAsyncKeyState(VK_SPACE) & 0x8000)))
            g_restBed = -1;
        if (g_playerOn && g_restBed < 0 && !g_menuOpen && !g_creativeOpen && !g_craftOpen && g_chestOpen < 0
            && !g_mapOpen && (!g_paused || steppedThisFrame)) {
            PlayerInput in;
            in.left  = (GetAsyncKeyState('A') & 0x8000) || (GetAsyncKeyState(VK_LEFT)  & 0x8000);
            in.right = (GetAsyncKeyState('D') & 0x8000) || (GetAsyncKeyState(VK_RIGHT) & 0x8000);
            in.jump  = (GetAsyncKeyState('W') & 0x8000) || (GetAsyncKeyState(VK_UP) & 0x8000)
                    || (GetAsyncKeyState(VK_SPACE) & 0x8000);
            /* Down: climbs a rope, and drops through a platform. S and the down
               arrow, matching the other three. */
            in.down  = (GetAsyncKeyState('S') & 0x8000) || (GetAsyncKeyState(VK_DOWN) & 0x8000);
            /* Published before the step, so swapping a jetpack takes effect on
               the same frame -- the same arrangement the collision box uses,
               and the reason player.cpp knows nothing about inventories. */
            g_player.fly = flightSpec(g_inv);
            g_player.speedMul = 1.0f + (float)g_inv.speedBonus() / 100.0f;
            g_player.resist   = g_inv.tempResist();
            g_player.update(g_world, in);
            /* Re-publish after movement before asking doors to close: the
               closing guard must see the body's current position, not where it
               stood at the start of this frame. Only this player path calls
               doorAuto, so creatures cannot operate player doors. */
            g_player.occupy(g_world);
            doorAuto(g_world, g_player);
        }

        /* After the character has moved, so the camera never lags a frame
           behind what it is following. With the character off, the arrow keys
           drive the camera instead. */
        if (!g_playerOn && !g_menuOpen && !g_creativeOpen && !g_craftOpen && g_chestOpen < 0) {
            const float PAN = 6.0f;
            float px = 0.0f, py = 0.0f;
            if ((GetAsyncKeyState('A') & 0x8000) || (GetAsyncKeyState(VK_LEFT)  & 0x8000)) px -= PAN;
            if ((GetAsyncKeyState('D') & 0x8000) || (GetAsyncKeyState(VK_RIGHT) & 0x8000)) px += PAN;
            if ((GetAsyncKeyState('W') & 0x8000) || (GetAsyncKeyState(VK_UP)    & 0x8000)) py -= PAN;
            if ((GetAsyncKeyState('S') & 0x8000) || (GetAsyncKeyState(VK_DOWN)  & 0x8000)) py += PAN;
            if (px != 0.0f || py != 0.0f) panCamera(px, py);
        }
        updateCamera(false);

        /* One room revalidated per ROOM_RECHECK frames -- see roomsTick(). The
           edit path handles anything you did on purpose; this catches rooms
           the SIMULATION undid, which is the case nothing else would notice:
           a wall melted through, a floor washed out, a fire that ate the
           ceiling. */
        /* --- these advance exactly when the WORLD does -------------------
           Gated on the same condition as g_world.step() above, and they were
           not, which made pausing actively destructive rather than merely
           incomplete.

           devTick is the whole of electricity: it steps every spark, fires
           every clock, and calls sparkCrowdHeat, which puts HEAT into the grid
           where fronts are dense. Running that while the world is frozen is the
           worst of both -- the clocks go on emitting, the crowd goes on
           heating, and updateHeat is not running to spread or shed any of it,
           so it piles up in one cell without limit. Reported from play as
           pausing the game and coming back to a melted circuit.

           The frame-advance case runs them too, deliberately: single-stepping
           exists to watch a mechanism work, and a step that moved the sand but
           not the sparks would be useless for the one thing it is for. */
        if (steppedThisFrame || (!g_paused && !g_menuOpen)) {
            roomsTick(g_world);
            devTick(g_world);
            treesTick(g_world);
        }

        /* Creatures run AFTER the character has moved, so contact damage is
           tested against where the player actually ended up this frame rather
           than where they were at the start of it -- walking into something
           and being hit by it are the same event and must not be a frame
           apart. Gated on the same conditions the character is: nothing should
           be stalking you while the crafting screen is open.

           The clock advances here too, on exactly the frames the world does, so
           time cannot pass while paused. It is deliberately NOT multiplied by
           the sim speed: the speed control is a debugging tool for watching the
           simulation, and having 4x quietly mean "and also make it night four
           times faster" would be a surprise nobody asked for. */
        if (!g_paused && !g_menuOpen && !g_creativeOpen && !g_craftOpen && g_chestOpen < 0) {
            /* Rest moves the sun and moon four times as quickly, but leaves
               world simulation, machines, and enemies at normal speed. A bed
               is a way to wait through a night, not a 4x simulation switch. */
            const int daySteps = g_restBed >= 0 ? 4 : 1;
            for (int i = 0; i < daySteps; ++i) dayAdvance();
            /* Creatures MOVE whenever there is a character for them to move
               relative to, but they only APPEAR on their own in survival. The
               split matters for the spawn eggs: those are a debug tool reached
               through the creative menu, and an egg that produced something
               frozen in place until you switched modes would be useless for the
               one job it has. Sandbox stays free of unasked-for wildlife. */
            if (g_playerOn) {
                entTick(g_world, g_player, g_inv);
                accessoryTick(g_player, g_inv);
                droneTick(g_world, g_player, g_inv);
                if (g_survival) entSpawnTick(g_world, g_player, g_camX, g_camY);
            }
        }

        /* Light is computed for this camera position and consumed immediately
           by renderView. The two must agree about where the camera is, which
           is why this sits here and not up beside the sim step. */
        lightClearDynamic();
        droneRegisterLights();
        devRegisterLights();
        projRegisterLights();
        if (g_lightOn) lightUpdate(g_world, g_camX, g_camY);
        g_cellCount = renderView(g_world, g_pixels, g_view, g_camX, g_camY, g_lightOn);
        drawCelestials(g_pixels);
        /* Reveal AFTER rendering, so the map records the frame you actually
           saw rather than the one before it, and only while the world is
           running -- standing on a pause screen should not fill in terrain. */
        if (!g_paused && !g_menuOpen) mapReveal(g_world, g_camX, g_camY);
        /* Machines draw whether or not the character is enabled -- they are part
           of the world, not part of the player, and the sandbox half of this
           program is exactly where you want to inspect a contraption. Before the
           character, so walking in front of one puts you in front of it. */
        circuitDraw(g_pixels, g_camX, g_camY, g_lightOn, g_circuitWireFrom, g_circuitWireFromPort);
        devDraw(g_world, g_pixels, g_camX, g_camY, g_lightOn);
        if (g_playerOn) {
            g_player.draw(g_pixels, g_camX, g_camY, g_lightOn);
            if (g_survival) drawHeldTool(g_pixels, currentAim(), g_lightOn);
        }
        /* Before sparks and shots so that something being hit is drawn UNDER
           the projectile hitting it, and after the player so a creature in
           front of you cannot hide the character you are steering. */
        entDraw(g_pixels, g_camX, g_camY, g_lightOn);
        droneDraw(g_pixels, g_camX, g_camY, g_lightOn);
        sparkDraw(g_pixels, g_camX, g_camY);
        /* After the fronts, so a mote falling in front of a lit wire is drawn
           over it rather than under. */
        shedDraw(g_pixels, g_camX, g_camY);
        projDraw(g_pixels, g_camX, g_camY);
        /* The map, over the world and the machines and under every panel. Last
           of the PIXEL-BUFFER passes and before StretchDIBits, which is the
           part that matters: these write into g_pixels, and once the blit has
           happened that buffer is not looked at again this frame. Putting them
           after it -- which is where they went first -- draws a perfectly
           correct map into memory and shows none of it. The symptom is that the
           map key appears to do nothing except stop the character, because the
           input gate works and the drawing does not. */
        drawFullMap(g_pixels);
        drawMinimap(g_pixels);

        /* Modals dim the world in the pixel buffer, before it becomes a blit --
           see dimPixels(). Doing it to the window instead cost 500ms a frame. */
        if (g_menuOpen || g_creativeOpen || g_craftOpen || g_chestOpen >= 0) dimPixels();

        /* Compose off-screen: sim into the viewport, then the panel, then out
           to the window in one BitBlt. */
        /* The full map is its own screen, not a piece of the world: keep it at
           its native scale while the world behind it may be zoomed. */
        const int blitW = g_mapOpen ? VIEW_CELLS_W : viewCellsW();
        const int blitH = g_mapOpen ? VIEW_CELLS_H : viewCellsH();
        const u32* blitPixels = g_pixels;
        BITMAPINFO blitBmi = g_bmi;
        if (blitW != VIEW_CELLS_W || blitH != VIEW_CELLS_H) {
            for (int y = 0; y < blitH; ++y)
                memcpy(g_zoomPixels + y * blitW, g_pixels + y * VIEW_CELLS_W,
                       (size_t)blitW * sizeof(u32));
            blitPixels = g_zoomPixels;
            blitBmi.bmiHeader.biWidth = blitW;
            blitBmi.bmiHeader.biHeight = -blitH;
        }
        StretchDIBits(g_backDC, PANEL_W, 0, VIEW_W, VIEW_H, 0, 0, blitW, blitH,
                      blitPixels, &blitBmi, DIB_RGB_COLORS, SRCCOPY);
        g_hoverItem = ITEM_NONE;
        drawPanel(g_backDC);
        if (g_survival && g_playerOn) drawHotbar(g_backDC);
        if (g_restBed >= 0) {
            RECT rest = { PANEL_W + 16, VIEW_H - 34, WIN_W - 16, VIEW_H - 14 };
            SetBkMode(g_backDC, TRANSPARENT);
            SetTextColor(g_backDC, RGB(226, 190, 90));
            DrawTextA(g_backDC, "RESTING  -  time passes 4x  -  move or right-click bed to wake", -1,
                      &rest, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        drawDevPanel(g_backDC);
        /* No reticle over the map -- it points at world cells, and the map is
           not showing world cells. */
        if (!g_menuOpen && !g_creativeOpen && !g_craftOpen && g_chestOpen < 0 && !g_mapOpen)
            drawCursor(g_backDC);
        if (g_creativeOpen) drawCreative(g_backDC);
        if (g_craftOpen)    drawCraft(g_backDC);
        if (g_chestOpen >= 0) drawChest(g_backDC);
        if (g_menuOpen)     drawMenu(g_backDC);
        drawItemTooltip(g_backDC);

        HDC hdc = GetDC(hwnd);
        BitBlt(hdc, 0, 0, WIN_W, WIN_H, g_backDC, 0, 0, SRCCOPY);
        ReleaseDC(hwnd, hdc);

        /* Pace to 60 Hz. */
        ++fpsFrames;
        LARGE_INTEGER tNow;
        QueryPerformanceCounter(&tNow);
        double elapsed = (double)(tNow.QuadPart - tPrev.QuadPart) / (double)freq.QuadPart;
        if (elapsed < FRAME_SECONDS) {
            int ms = (int)((FRAME_SECONDS - elapsed) * 1000.0);
            if (ms > 1) Sleep(ms - 1);
            do {
                QueryPerformanceCounter(&tNow);
                elapsed = (double)(tNow.QuadPart - tPrev.QuadPart) / (double)freq.QuadPart;
            } while (elapsed < FRAME_SECONDS);
        }
        tPrev = tNow;

        double since = (double)(tNow.QuadPart - tFpsBase.QuadPart) / (double)freq.QuadPart;
        if (since >= 0.25) {
            g_fps = fpsFrames / since;
            fpsFrames = 0;
            tFpsBase = tNow;
        }
    }

    timeEndPeriod(1);
    SelectObject(g_backDC, g_backOldBmp);
    DeleteObject(g_backBmp);
    DeleteDC(g_backDC);
    DeleteObject(g_panelBg);
    DeleteObject(g_btnBg);
    DeleteObject(g_btnBgHot);
    DeleteObject(g_btnBgSel);
    DeleteObject(g_borderBrush);
    DeleteObject(g_accentBrush);
    for (int i = 0; i < N_BRUSH; ++i) DeleteObject(g_swatchBrush[i]);
    for (int s = 1; s < SPR_COUNT; ++s) if (g_iconBmp[s]) DeleteObject(g_iconBmp[s]);
    if (g_iconDC) DeleteDC(g_iconDC);
    DeleteObject(g_font);
    return 0;
}
