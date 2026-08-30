#define WIN32_LEAN_AND_MEAN
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>   /* timeBeginPeriod; excluded by WIN32_LEAN_AND_MEAN */
#else
/* The browser build. web/win32.h reimplements the exact slice of Win32 this
   file uses against a pixel buffer, so everything below this line -- the
   panels, the wndProc, the frame loop -- is compiled unchanged for both
   targets. This include is the ONLY concession in main.cpp to there being a
   second platform, and it needs to stay that way: the moment UI code starts
   branching on the target, there are two interfaces to maintain instead of
   one, and the web build begins drifting away from the game. */
#include "web/win32.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>   /* the save screen stamps each slot */
#include <vector>
#include <unordered_set>

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
#include "multiplayer.h"
#include "network.h"

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

/* Interface content scale is intentionally independent from window size. The
   game still renders one fixed logical canvas so aim and world visibility do
   not change; this setting sizes text and item artwork within the existing,
   generous hit targets. One hundred percent is the new compact default. */
static const int UI_SCALE_VALUES[] = { 80, 90, 100, 110, 120 };
static const int UI_SCALE_COUNT = (int)(sizeof(UI_SCALE_VALUES) / sizeof(UI_SCALE_VALUES[0]));
static int g_uiScaleIndex = 2;
static int uiScalePct() { return UI_SCALE_VALUES[g_uiScaleIndex]; }
static int uiScaled(int px) { return imax(1, (px * uiScalePct() + 50) / 100); }
#ifndef CINDERLIFT_CONFIG_PATH
#define CINDERLIFT_CONFIG_PATH "cinderlift.cfg"
#endif

static void uiSettingsLoad() {
    FILE* f = fopen(CINDERLIFT_CONFIG_PATH, "rb");
    if (!f) return;
    char line[96];
    while (fgets(line, sizeof(line), f)) {
        int pct = 0;
        if (sscanf(line, "ui_scale=%d", &pct) != 1) continue;
        int best = 0;
        for (int i = 1; i < UI_SCALE_COUNT; ++i)
            if (abs(UI_SCALE_VALUES[i] - pct) < abs(UI_SCALE_VALUES[best] - pct)) best = i;
        g_uiScaleIndex = best;
    }
    fclose(f);
}

static void uiSettingsSave() {
    FILE* f = fopen(CINDERLIFT_CONFIG_PATH, "wb");
    if (!f) return;
    fprintf(f, "ui_scale=%d\n", uiScalePct());
    fclose(f);
}

static void rebuildUiFont() {
    HFONT next = CreateFontA(uiScaled(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, FF_DONTCARE, "Consolas");
    if (!next) return;
    if (g_font) DeleteObject(g_font);
    g_font = next;
}

static void changeUiScale(int direction) {
    const int next = imax(0, imin(UI_SCALE_COUNT - 1, g_uiScaleIndex + direction));
    if (next == g_uiScaleIndex) return;
    g_uiScaleIndex = next;
    rebuildUiFont();
    uiSettingsSave();
}

/* Off-screen back buffer. The sim blit, the panel and the HUD are all composed
   into this and sent to the window as one BitBlt. Drawing them straight to the
   window DC in stages is what used to flicker. */
static HDC     g_backDC;
static HBITMAP g_backBmp;
static HGDIOBJ g_backOldBmp;

/* The game continues to compose at one fixed logical resolution. A resizable
   window (and fullscreen) presents that canvas inside the client area with
   aspect-preserving nearest-neighbour scaling. Keeping layout in logical
   pixels means resizing cannot subtly change aim, UI hit boxes, or how much of
   the simulated world is visible. */
static RECT    g_presentRect = { 0, 0, WIN_W, WIN_H };
static bool    g_fullscreen = false;
static DWORD   g_windowedStyle = 0;
static WINDOWPLACEMENT g_windowedPlacement = {};

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
    { MAT_EMPTY, "Erase" },
};
static const int N_BRUSH = (int)(sizeof(BRUSHES) / sizeof(BRUSHES[0]));

/* ACT_DAY and ACT_NIGHT are adjacent because they SHARE A ROW -- see
   layoutPanel, which pairs them into two half-width buttons. They are the only
   pair on this panel that does, and it is worth the special case: they are one
   control with two values, and stacking them as two full-width rows would read
   as two unrelated commands. */
enum ActionId { ACT_OVERWRITE, ACT_LAYER, ACT_WIRE, ACT_CIRCUIT, ACT_VIEW, ACT_LIGHT, ACT_PLAYER, ACT_PAUSE, ACT_FILTER, ACT_DAY, ACT_NIGHT, ACT_CLEAR, N_ACT };

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

/* --- the left panel, when there is a character ------------------------------
   With the character off, this panel is a sandbox catalog: every material and
   every machine, because there is no pack to draw from and the point is to be
   able to paint with anything.

   With the character ON, that list is the wrong list. What you can actually
   place is what you are CARRYING, and the catalog offers you two hundred things
   of which you own four. So the same strip becomes a view of the pack -- one
   readable row per stack, scrollable, click to put it in your hand.

   It is the same widget rather than a second panel deliberately. The strip is
   already the most legible surface in the game: full item names at a readable
   size, a scrollbar, and a fixed place to look. The inventory screen has to
   cram forty slots into a grid of 34-pixel squares and can only show icons; a
   list can show "Titanium Sword" and "Copper 1.2k" and be read at a glance.
   Having both is not redundancy, it is the difference between browsing and
   arranging.

   ONE COLUMN here against the catalog's two, and that is what buys the
   readability: a material's label is "Sand" and an item's is "Harvesting
   Sickle" with a count after it, which does not fit half a 244-pixel panel. */
static int  g_packList[INV_SLOTS];   /* pack slot indices, in pack order */
static int  g_packListCount = 0;
static RECT g_packListRect[INV_SLOTS];
/* The pack list may temporarily point `Inventory::selected` at any of the
   forty carried slots. Keep the player's real hotbar choice separately so a
   wheel notch or number key can dismiss that override instead of cycling from
   an unrelated pack index. This value is also what remains ringed on the bar:
   the list shows what is temporarily in hand, while the bar shows where input
   will return. */
static int g_hotbarSelected = 0;

static void selectHotbar(int slot) {
    g_hotbarSelected = imax(0, imin(HOTBAR_SLOTS - 1, slot));
    g_inv.selected = g_hotbarSelected;
}
/* Whether the strip is currently showing the pack rather than the catalog.
   Derived from the character being on, and stored so the click handler and the
   draw cannot disagree about which list they are looking at. */
static bool g_panelShowsPack = false;
static RECT g_actRect[N_ACT];
static RECT g_sizeDec, g_sizeInc, g_sizeBox, g_sizeTrack;
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
static const int ICON_SCALE   = 2; /* cache 21x21 art at a crisp 42x42 */
static const int ICON_PX      = INV_SPR_W * ICON_SCALE;
/* 38px was the previous compact pass. The new 100% baseline is another ten
   percent smaller; UI scale lets the player move away from that default. */
static const int ICON_DRAW_BASE_PX = 34;
static int iconDrawPx() { return uiScaled(ICON_DRAW_BASE_PX); }
static HDC     g_iconDC;
static HBITMAP g_iconBmp[SPR_COUNT];
static HBITMAP g_matIconBmp[MAT_COUNT];
static ItemId  g_hoverItem = ITEM_NONE;
static RECT    g_hoverRect;
/* What an EMPTY slot is for, shown on hover. The tooltip already existed for
   items, and an empty slot is precisely the case where the player most needs
   telling: a filled slot shows an icon they can recognise, an empty one is a
   grey square with four characters in it. Kept as a separate variable rather
   than a fake ItemId so the tooltip cannot be tricked into indexing ITEMS[]. */
static const char* g_hoverLabel = 0;
static int     g_mx = 0, g_my = 0;
/* GetAsyncKeyState reports the keyboard, not this window. Without a focus gate
   an unfocused Cinderlift walks your character while you type somewhere else --
   and four local windows in a loopback test all move as one.

   Asked of the system every time rather than tracked through WM_ACTIVATEAPP.
   A flag has to start somewhere, and a window which has never been focused is
   never sent a deactivation, so it would sit at its initial value and keep
   reading the keyboard -- which is exactly the bug this gate exists to fix. */
static HWND    g_hwnd = 0;
static bool keyHeld(int vk) {
    return g_hwnd && GetForegroundWindow() == g_hwnd && (GetAsyncKeyState(vk) & 0x8000) != 0;
}
static bool inRect(const RECT& r, int x, int y);

static void updatePresentRect(HWND hwnd) {
    RECT client = { 0, 0, WIN_W, WIN_H };
    if (hwnd) GetClientRect(hwnd, &client);
    const int cw = imax(1, client.right - client.left);
    const int ch = imax(1, client.bottom - client.top);
    int w = cw, h = w * WIN_H / WIN_W;
    if (h > ch) { h = ch; w = h * WIN_W / WIN_H; }
    g_presentRect.left = (cw - w) / 2;
    g_presentRect.top = (ch - h) / 2;
    g_presentRect.right = g_presentRect.left + imax(1, w);
    g_presentRect.bottom = g_presentRect.top + imax(1, h);
}

static void clientToLogical(int cx, int cy, int* x, int* y) {
    const int w = g_presentRect.right - g_presentRect.left;
    const int h = g_presentRect.bottom - g_presentRect.top;
    if (cx < g_presentRect.left || cx >= g_presentRect.right ||
        cy < g_presentRect.top || cy >= g_presentRect.bottom || w < 1 || h < 1) {
        *x = *y = -1;
        return;
    }
    *x = (cx - g_presentRect.left) * WIN_W / w;
    *y = (cy - g_presentRect.top) * WIN_H / h;
}

static void updateMouseFromLParam(LPARAM lp) {
    clientToLogical((short)LOWORD(lp), (short)HIWORD(lp), &g_mx, &g_my);
}

static void toggleFullscreen(HWND hwnd) {
    if (!g_fullscreen) {
        g_windowedStyle = (DWORD)GetWindowLongPtr(hwnd, GWL_STYLE);
        g_windowedPlacement.length = sizeof(g_windowedPlacement);
        GetWindowPlacement(hwnd, &g_windowedPlacement);
        MONITORINFO mi; memset(&mi, 0, sizeof(mi)); mi.cbSize = sizeof(mi);
        GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtr(hwnd, GWL_STYLE, g_windowedStyle & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = true;
    } else {
        SetWindowLongPtr(hwnd, GWL_STYLE, g_windowedStyle);
        SetWindowPlacement(hwnd, &g_windowedPlacement);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = false;
    }
    updatePresentRect(hwnd);
}

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
        for (int y = 0; y < INV_SPR_H; ++y) for (int x = 0; x < INV_SPR_W; ++x) {
            bool on = false;
            if (MATS[m].kind == KIND_POWDER) on = x >= 1 && x <= 19 && y >= 9 + abs(x - 10) / 2;
            else if (MATS[m].kind == KIND_LIQUID) on = x >= 1 && x <= 19 && y >= 11 + ((x + m) % 4 == 0 ? 1 : 0);
            else if (MATS[m].kind == KIND_GAS) { const int dx = x - 10, dy = y - 11; on = dx*dx + dy*dy < 66 || ((x-5)*(x-5)+(y-7)*(y-7)<27); }
            else on = x >= 3 && x <= 17 && y >= 3 && y <= 17;
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
        for (int y = 0; y < INV_SPR_H; ++y)
            for (int x = 0; x < INV_SPR_W; ++x) {
                /* Exact 3:2 nearest-neighbour enlargement. Authored shapes,
                   palette identity, and transparency survive unchanged while
                   every inventory icon receives the new 21px reading grid. */
                const int sx = x * SPR_W / INV_SPR_W;
                const int sy = y * SPR_H / INV_SPR_H;
                const u32 c = g_sprite[s][sy * SPR_W + sx];
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
        const int side = imin(imin(r.right-r.left-2, r.bottom-r.top-2), iconDrawPx());
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
        if (side > iconDrawPx()) side = iconDrawPx();
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
    const int side = imin(imin(r.right - r.left - 2, r.bottom - r.top - 2), iconDrawPx());
    if (side <= 0 || !g_iconBmp[sprite]) return;
    const int x = r.left + (r.right - r.left - side) / 2;
    const int y = r.top + (r.bottom - r.top - side) / 2;
    HGDIOBJ old = SelectObject(g_iconDC, g_iconBmp[sprite]);
    TransparentBlt(hdc, x, y, side, side, g_iconDC, 0, 0, ICON_PX, ICON_PX, ICON_KEY);
    SelectObject(g_iconDC, old);
}

static void itemTooltipStats(const ItemDef& d, char* out, int cap) {
    if (!out || cap < 1) return;
    out[0] = 0;
    if (d.kind == ITEMK_MODULE) {
        snprintf(out, cap, "%u energy  |  %d damage  |  %d frame delay",
                 (unsigned)d.energyCost, d.damage, (int)d.addDelay);
    } else if (d.kind == ITEMK_TOOL && d.energyCapacity) {
        snprintf(out, cap, "%u energy  |  +%u/sec  |  %u slots",
                 (unsigned)d.energyCapacity, (unsigned)d.energyRecharge * 60u,
                 (unsigned)d.toolSlots);
    } else if (d.kind == ITEMK_MINING) {
        snprintf(out, cap, "radius %u  |  %u cells/action",
                 (unsigned)d.mineRadius, (unsigned)d.mineBite);
    } else if (d.kind == ITEMK_MELEE) {
        snprintf(out, cap, "%d damage  |  %u reach  |  %.1f knockback",
                 d.damage, (unsigned)d.meleeReach, d.meleeKnock);
    } else if (d.kind == ITEMK_FOOD) {
        snprintf(out, cap, "+%d health  |  %d sec shared cooldown",
                 (int)d.heal, HEAL_COOLDOWN_FRAMES / 60);
    } else if (d.kind == ITEMK_THROWABLE) {
        snprintf(out, cap, "%d damage", d.damage);
    } else if (d.armour || d.heatResist || d.coldResist) {
        snprintf(out, cap, "%d armour  |  %d heat  |  %d cold",
                 (int)d.armour, (int)d.heatResist, (int)d.coldResist);
    } else if (d.fly.fuel > 0) {
        snprintf(out, cap, "%d fuel  |  %.2f rise speed", d.fly.fuel, d.fly.riseCap);
    } else if (d.reachBonus) {
        snprintf(out, cap, "+%d build reach", (int)d.reachBonus);
    } else if (d.speedPct) {
        snprintf(out, cap, "+%d%% movement speed", (int)d.speedPct);
    } else if (d.regenPer) {
        snprintf(out, cap, "1 health every %.1f sec", (float)d.regenPer / 60.0f);
    } else if (d.lightGlow) {
        snprintf(out, cap, "%d light", (int)d.lightGlow);
    } else if (d.pickupRadius) {
        snprintf(out, cap, "+%d pickup range", (int)d.pickupRadius);
    } else if (d.shotSpeedPct) {
        snprintf(out, cap, "+%d%% projectile speed", (int)d.shotSpeedPct);
    } else if (d.damagePct) {
        snprintf(out, cap, "+%d%% projectile damage", (int)d.damagePct);
    } else if (d.cooldownPct) {
        snprintf(out, cap, "%d%% shorter firing delay", (int)d.cooldownPct);
    }
}

static void drawItemTooltip(HDC hdc) {
    /* An item under the cursor wins over a slot label: if the slot has
       something in it, what that something IS is the more useful fact, and the
       slot's own name is already implied by the group heading above it. */
    const char* name = 0;
    const char* description = 0;
    char stats[192]; stats[0] = 0;
    if (g_hoverItem != ITEM_NONE && g_hoverItem < ITEM_COUNT) {
        const ItemDef& d = ITEMS[g_hoverItem];
        name = d.name;
        description = d.description;
        itemTooltipStats(d, stats, (int)sizeof(stats));
    }
    else if (g_hoverLabel) name = g_hoverLabel;
    if (!name || !name[0]) return;
    HGDIOBJ oldFont = SelectObject(hdc, g_font);

    /* Materials and empty-slot labels retain the small strip. Dirt and water
       should identify themselves immediately without obscuring the playfield. */
    if ((!description || !description[0]) && !stats[0]) {
        const int stripW = uiScaled(250), stripH = uiScaled(18);
        RECT r = { g_hoverRect.left, g_hoverRect.top - stripH - 4,
                   g_hoverRect.left + stripW, g_hoverRect.top - 4 };
        if (r.right > WIN_W - 2) { r.left -= r.right - (WIN_W - 2); r.right = WIN_W - 2; }
        if (r.top < 2) { r.top = g_hoverRect.bottom + 4; r.bottom = r.top + stripH; }
        FillRect(hdc, &r, g_panelBg); FrameRect(hdc, &r, g_accentBrush);
        SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, RGB(245,224,150));
        DrawTextA(hdc, name, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
        return;
    }

    const int width = uiScaled(320), pad = uiScaled(9), titleH = uiScaled(18);
    const int statsH = stats[0] ? uiScaled(18) : 0;
    RECT calc = { 0, 0, width - pad * 2, 0 };
    int descH = 0;
    if (description && description[0]) {
        DrawTextA(hdc, description, -1, &calc, DT_CALCRECT | DT_WORDBREAK);
        descH = imax(16, calc.bottom - calc.top);
    }
    const int gap = descH ? 4 : 0;
    const int height = pad + titleH + statsH + gap + descH + pad;
    int left = g_hoverRect.left;
    if (left + width > WIN_W - 2) left = WIN_W - width - 2;
    if (left < 2) left = 2;
    int top = g_hoverRect.top - height - 4;
    if (top < 2) top = g_hoverRect.bottom + 4;
    if (top + height > VIEW_H - 2) top = imax(2, VIEW_H - height - 2);
    RECT box = { left, top, left + width, top + height };
    FillRect(hdc, &box, g_panelBg); FrameRect(hdc, &box, g_accentBrush);
    SetBkMode(hdc, TRANSPARENT);

    RECT line = { left + pad, top + pad, left + width - pad, top + pad + titleH };
    SetTextColor(hdc, RGB(245,224,150));
    DrawTextA(hdc, name, -1, &line, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (stats[0]) {
        line.top += titleH; line.bottom += titleH;
        SetTextColor(hdc, RGB(142,198,220));
        DrawTextA(hdc, stats, -1, &line, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    if (descH) {
        RECT body = { left + pad, top + pad + titleH + statsH + gap,
                      left + width - pad, top + height - pad };
        SetTextColor(hdc, RGB(220,220,210));
        DrawTextA(hdc, description, -1, &body, DT_LEFT | DT_WORDBREAK);
    }
    SelectObject(hdc, oldFont);
}

/* UI/input state */
static bool g_uiCapture = false;   /* the click landed on the panel, not the sim */
static bool g_overwrite = false;   /* false = placement only fills empty space */
static int  g_view      = VIEW_NORMAL;
static bool g_lmb = false, g_rmb = false;
/* A right-click on an interactable is a one-frame command, distinct from the
   held right-button mining verb. The client cannot operate the local replica;
   it asks the host and receives the resulting device/door state back. */
static bool g_interactPulse = false;
static bool g_respawnPulse = false; /* temporary tap-R cursor teleport */
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

   A quick tap still teleports an ALIVE player to the cursor for now. Death does
   not accept that shortcut: its ten-second, authority-owned respawn remains
   automatic. A completed line suppresses the tap action on key-up. */
/* What the last save or load said, and for how long to keep saying it. A save
   that reports nothing is a save you do not trust; four seconds is long enough
   to read and short enough not to become furniture. */
static char g_saveMsg[256] = "";
static int  g_saveMsgFrames = 0;

/* --- one-stroke terrain undo ---------------------------------------------
   A stroke remembers the cells as they were BEFORE its first touch. A set
   keeps an interpolated drag from storing the same cell hundreds of times,
   while the vector preserves the compact snapshots needed after the stroke.

   Inventory is recorded as a delta rather than copied wholesale. Undoing a
   dig therefore returns the rock to the wall and takes its drops back out of
   the pack, but it does not also rewind a hotbar selection or an unrelated
   item move made after the stroke. */
struct UndoCell {
    i32 index;
    Cell cell;
    u8 temp, bg;
};
struct UndoPlacedDevice {
    bool torch;
    i32 index, x, y;
    u8 type;
};
struct ActiveUndoStroke {
    bool active, overflow;
    std::vector<UndoCell> cell;
    std::vector<UndoPlacedDevice> placed;
    std::unordered_set<i32> seen;
    int before[ITEM_COUNT];
    ActiveUndoStroke() : active(false), overflow(false) { memset(before, 0, sizeof(before)); }
};
struct FinishedUndoStroke {
    bool valid;
    std::vector<UndoCell> cell;
    std::vector<UndoPlacedDevice> placed;
    int inventoryDelta[ITEM_COUNT];
    FinishedUndoStroke() : valid(false) { memset(inventoryDelta, 0, sizeof(inventoryDelta)); }
};
static ActiveUndoStroke   g_activeUndo[MAX_PLAYERS];
static FinishedUndoStroke g_lastUndo[MAX_PLAYERS];
static const size_t UNDO_CELL_LIMIT = 500000;

static bool undoCellChanged(const UndoCell& old) {
    const Cell& now = g_world.cells[old.index];
    return now.mat != old.cell.mat || now.moisture != old.cell.moisture ||
           now.tint != old.cell.tint || now.flags != old.cell.flags ||
           g_world.temp[old.index] != old.temp || g_world.bg[old.index] != old.bg;
}

static void undoBegin(int slot, const Inventory* inventory) {
    if (slot < 0 || slot >= MAX_PLAYERS || g_activeUndo[slot].active) return;
    ActiveUndoStroke& stroke = g_activeUndo[slot];
    stroke.active = true; stroke.overflow = false;
    stroke.cell.clear(); stroke.placed.clear(); stroke.seen.clear();
    for (int item = 0; item < ITEM_COUNT; ++item)
        stroke.before[item] = inventory ? inventory->countOf((ItemId)item) : 0;
}

static void undoCaptureCell(int slot, int x, int y) {
    if (slot < 0 || slot >= MAX_PLAYERS || x < PLAY_X0 || x > PLAY_X1 ||
        y < PLAY_Y0 || y > PLAY_Y1) return;
    ActiveUndoStroke& stroke = g_activeUndo[slot];
    if (!stroke.active || stroke.overflow) return;
    const i32 index = y * SIM_W + x;
    if (!stroke.seen.insert(index).second) return;
    if (stroke.cell.size() >= UNDO_CELL_LIMIT) {
        stroke.overflow = true; stroke.cell.clear(); stroke.seen.clear(); return;
    }
    UndoCell old;
    old.index = index; old.cell = g_world.cells[index];
    old.temp = g_world.temp[index]; old.bg = g_world.bg[index];
    stroke.cell.push_back(old);
}

static void undoCaptureDisc(int slot, int cx, int cy, int radius) {
    const int r = imax(0, radius), rr = r * r;
    for (int y = imax(PLAY_Y0, cy - r); y <= imin(PLAY_Y1, cy + r); ++y)
        for (int x = imax(PLAY_X0, cx - r); x <= imin(PLAY_X1, cx + r); ++x) {
            const int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= rr) undoCaptureCell(slot, x, y);
        }
}

static void undoFinish(int slot, const Inventory* inventory) {
    if (slot < 0 || slot >= MAX_PLAYERS) return;
    ActiveUndoStroke& active = g_activeUndo[slot];
    if (!active.active) return;
    active.active = false; active.seen.clear();
    if (active.overflow) {
        active.cell.clear(); active.placed.clear();
        g_lastUndo[slot].valid = false; g_lastUndo[slot].cell.clear();
        g_lastUndo[slot].placed.clear();
        if (slot == LOCAL_PLAYER_ID) {
            sprintf(g_saveMsg, "Stroke too large to undo"); g_saveMsgFrames = 150;
        }
        return;
    }
    size_t kept = 0;
    for (size_t i = 0; i < active.cell.size(); ++i)
        if (undoCellChanged(active.cell[i])) active.cell[kept++] = active.cell[i];
    active.cell.resize(kept);
    if (active.cell.empty() && active.placed.empty()) return;

    FinishedUndoStroke& done = g_lastUndo[slot];
    done.cell.swap(active.cell); done.placed.swap(active.placed); done.valid = true;
    for (int item = 0; item < ITEM_COUNT; ++item)
        done.inventoryDelta[item] = inventory ?
            inventory->countOf((ItemId)item) - active.before[item] : 0;
}

static bool undoApply(int slot, Inventory* inventory) {
    if (slot < 0 || slot >= MAX_PLAYERS || netRole() == NET_CLIENT) return false;
    undoFinish(slot, inventory);
    FinishedUndoStroke& stroke = g_lastUndo[slot];
    if (!stroke.valid) {
        if (slot == LOCAL_PLAYER_ID) {
            sprintf(g_saveMsg, "Nothing to undo"); g_saveMsgFrames = 100;
        }
        return false;
    }

    /* An external device list accompanies machine footprints. Require every
       placed object still to be the same object before removing it; otherwise
       a destroyed/reused machine slot could turn undo into a free refund. */
    for (size_t i = 0; i < stroke.placed.size(); ++i) {
        const UndoPlacedDevice& placed = stroke.placed[i];
        if (placed.torch) {
            if (torchAt(placed.x, placed.y) < 0) {
                if (slot == LOCAL_PLAYER_ID) {
                    sprintf(g_saveMsg, "Placed object changed; undo cancelled"); g_saveMsgFrames = 150;
                }
                return false;
            }
        } else if (placed.index < 0 || placed.index >= MAX_DEVICES ||
                   !g_devices[placed.index].used ||
                   g_devices[placed.index].type != placed.type ||
                   g_devices[placed.index].x != placed.x || g_devices[placed.index].y != placed.y ||
                   g_devices[placed.index].count != 0 || g_devices[placed.index].count2 != 0) {
            if (slot == LOCAL_PLAYER_ID) {
                sprintf(g_saveMsg, "Placed machine changed; undo cancelled"); g_saveMsgFrames = 150;
            }
            return false;
        }
    }

    /* Test the inventory reversal on a copy first. If mined drops have already
       been spent, or refunded building material no longer fits, refusing is
       safer than duplicating or deleting anything. Remove gains before adding
       refunds so an overwrite stroke can free the space its refund needs. */
    Inventory trial;
    if (inventory) {
        trial = *inventory;
        for (int item = 1; item < ITEM_COUNT; ++item)
            if (stroke.inventoryDelta[item] > 0 &&
                trial.take((ItemId)item, stroke.inventoryDelta[item]) != stroke.inventoryDelta[item]) {
                if (slot == LOCAL_PLAYER_ID) {
                    sprintf(g_saveMsg, "Undo needs the mined items in your pack");
                    g_saveMsgFrames = 150;
                }
                return false;
            }
        for (int item = 1; item < ITEM_COUNT; ++item)
            if (stroke.inventoryDelta[item] < 0 &&
                trial.add((ItemId)item, -stroke.inventoryDelta[item]) != 0) {
                if (slot == LOCAL_PLAYER_ID) {
                    sprintf(g_saveMsg, "Undo needs room to refund building material");
                    g_saveMsgFrames = 150;
                }
                return false;
            }
    }

    for (size_t i = stroke.placed.size(); i-- > 0;) {
        const UndoPlacedDevice& placed = stroke.placed[i];
        if (placed.torch) torchRemoveAt(torchAt(placed.x, placed.y));
        else devRemove(g_world, &g_devices[placed.index]);
    }

    int minX = PLAY_X1, minY = PLAY_Y1, maxX = PLAY_X0, maxY = PLAY_Y0;
    for (size_t i = 0; i < stroke.cell.size(); ++i) {
        const UndoCell& old = stroke.cell[i];
        g_world.cells[old.index] = old.cell;
        g_world.temp[old.index] = old.temp;
        g_world.bg[old.index] = old.bg;
        const int x = old.index % SIM_W, y = old.index / SIM_W;
        minX = imin(minX, x); minY = imin(minY, y);
        maxX = imax(maxX, x); maxY = imax(maxY, y);
    }
    if (inventory) *inventory = trial;
    if (!stroke.cell.empty()) {
        g_world.dirtyArea(minX, minY, maxX, maxY);
        roomsNotifyEdit(g_world, (minX + maxX) / 2, (minY + maxY) / 2);
        netMarkWorldEdit((minX + maxX) / 2, (minY + maxY) / 2,
                         imax(maxX - minX, maxY - minY) / 2 + 2);
    }
    stroke.valid = false; stroke.cell.clear(); stroke.placed.clear();
    if (slot == LOCAL_PLAYER_ID) {
        sprintf(g_saveMsg, "Undid last build/dig stroke"); g_saveMsgFrames = 100;
    }
    return true;
}

static void undoClearAll() {
    for (int slot = 0; slot < MAX_PLAYERS; ++slot) {
        g_activeUndo[slot].active = false; g_activeUndo[slot].overflow = false;
        g_activeUndo[slot].cell.clear(); g_activeUndo[slot].placed.clear();
        g_activeUndo[slot].seen.clear();
        g_lastUndo[slot].valid = false; g_lastUndo[slot].cell.clear();
        g_lastUndo[slot].placed.clear();
    }
}
/* Overridable at compile time so a diagnostic build can be run ALONGSIDE a
   real session without the two fighting over one file. An agent or a soak test
   that shares the player's save will eventually overwrite a character somebody
   cared about, and "be careful" is not a mechanism. */
#ifndef CINDERLIFT_SAVE_PATH
#define CINDERLIFT_SAVE_PATH "cinderlift.sav"
#endif
static const char* const SAVE_PATH = CINDERLIFT_SAVE_PATH;

/* The name this game shipped under before it was Cinderlift.

   Saves are found BY FILENAME, so a rename with nothing else done to it makes
   every world a player already had vanish from the load screen -- still on
   disk, simply never looked for again. That is indistinguishable from data
   loss to the person it happens to, and it happens silently on first launch
   of the new version.

   So a slot resolves to the old name when the old file is there and the new
   one is not, for reading AND for writing. Writing matters as much as
   reading: resolve only on load and the next save drops a second file beside
   the first, leaving the player with two half-worlds in one slot and no way
   to tell which is which. Resolving both ways means an existing save simply
   keeps being the file it always was, and only genuinely new slots take the
   new name. Nothing is moved, copied or rewritten, so there is no migration
   step that can fail halfway. */
#ifndef CINDERLIFT_LEGACY_SAVE_PATH
#define CINDERLIFT_LEGACY_SAVE_PATH "crucible.sav"
#endif
static const char* const LEGACY_SAVE_PATH = CINDERLIFT_LEGACY_SAVE_PATH;

static bool g_lineKey  = false;   /* R is down */
static bool g_lineOn   = false;   /* ...and a drag is in progress */
static bool g_lineDrew = false;   /* this hold of R drew something */
static int  g_lineX = 0, g_lineY = 0;
static bool g_lineCommitPulse = false;
static u8   g_lineCommitBits = 0;
static int  g_lineCommitX = 0, g_lineCommitY = 0;
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
static int  g_closeDevicePending = -1; /* client-side close until host echoes it */
static bool handleDevPanelClick(int mx, int my);
static bool handleCraftClick(int mx, int my);
static void layoutCraft();
extern bool g_craftOpen;
extern int  g_craftScroll;
static void drawDevPanel(HDC hdc);
/* The two machines that work on a BOX rather than a row -- see devBoxCell.
   Up here because both the network action handler and the panel need it, and
   they sit a long way apart. */
static bool devHasBox(u8 type) { return type == DEV_MINER || type == DEV_PLACER; }
/* Defined beside the other device-interaction helpers, declared here because
   the creative input path reaches it several hundred lines earlier. */
static void pedestalUse(Device& d, Inventory& inv);
static int  g_pmx = -1, g_pmy = -1;  /* previous aim point, in cells */
static int  g_brushMat = MAT_SAND;
/* -1 means a material/tool brush is selected.  Device buttons only become a
   free placement source while the character is off; survival inventory
   placement remains exactly as before. */
static int  g_paletteDevice = -1;
static int  g_paletteScroll = 0;
static int  g_paletteMaxScroll = 0;
static const int BRUSH_RADIUS_MIN = 1;
static const int BRUSH_RADIUS_MAX = 64;
static int  g_brushRadius = 6;
static bool g_sizeDragging = false;
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
static int& g_restBed = g_playerSessions[0].restBed;
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
/* Grown from 34, then again for the 21px reading grid. The count keeps its own
   strip without forcing the 38px displayed sprite into a letterbox. */
static const int HOTBAR_SLOT = 60;   /* room for a 38px icon plus its count */
static RECT g_hotRect[HOTBAR_SLOTS];
static RECT g_menuResume, g_menuHost, g_menuJoin, g_menuIp, g_menuStop, g_menuQuit, g_menuPanel;
static RECT g_menuSave, g_menuLoad;
static RECT g_menuUiMinus, g_menuUiValue, g_menuUiPlus;

/* --- the save screen --------------------------------------------------------
   Ten slots, each showing a picture of where you were when you wrote it.

   One save file was fine while the game was a sandbox you reset constantly. It
   stops being fine the moment there is a character with a pack and a boss flag:
   the only way to keep a world you like is to not save over it, which makes
   saving a thing you avoid rather than a thing you do. Ten slots and a picture
   of each turns that around.

   The picture is doing the real work rather than decorating. A column of ten
   filenames and timestamps is a puzzle -- "was the good one at 14:20 or
   14:48?" -- and a column of ten pictures of the places is not a puzzle at all.
   See SAVE_THUMB_W for why it is small enough to be free. */
static const int SAVE_SLOTS = 10;
enum SaveScreenMode { SAVESCREEN_OFF = 0, SAVESCREEN_SAVE, SAVESCREEN_LOAD };
static int  g_saveScreen = SAVESCREEN_OFF;
static RECT g_savePanel, g_saveSlotRect[SAVE_SLOTS], g_saveBack;
/* Refreshed when the screen opens, not every frame: ten savePeek calls is ten
   file opens, which is nothing once but is not something to do at 60 Hz. */
static SaveSlotInfo g_saveSlot[SAVE_SLOTS];

/* "cinderlift.sav" -> "cinderlift3.sav". Derived from SAVE_PATH rather than being a
   second constant, so a build with its own save path (a diagnostic one, say)
   gets its own ten slots for free and cannot land in the player's. */
static void saveSlotName(char* out, size_t cap, const char* base, int slot) {
    char stem[48];
    strncpy(stem, base, sizeof(stem) - 1);
    stem[sizeof(stem) - 1] = 0;
    char* dot = strrchr(stem, '.');
    if (dot) *dot = 0;
    if (cap > 0) {
        out[0] = 0;
        sprintf(out, "%s%d.sav", stem, slot + 1);
    }
}

static bool saveFileExists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

static const char* saveSlotPath(int slot) {
    static char buf[SAVE_SLOTS][64];
    if (slot < 0 || slot >= SAVE_SLOTS) return SAVE_PATH;
    saveSlotName(buf[slot], sizeof(buf[slot]), SAVE_PATH, slot);
    /* An existing world keeps the filename it already has -- see the note on
       LEGACY_SAVE_PATH. Only a slot with nothing under either name takes the
       new one, so a player who never had the old build never sees this. */
    if (!saveFileExists(buf[slot])) {
        char legacy[64];
        saveSlotName(legacy, sizeof(legacy), LEGACY_SAVE_PATH, slot);
        if (saveFileExists(legacy)) {
            strncpy(buf[slot], legacy, sizeof(buf[slot]) - 1);
            buf[slot][sizeof(buf[slot]) - 1] = 0;
        }
    }
    return buf[slot];
}

static void saveSlotsRefresh() {
    for (int i = 0; i < SAVE_SLOTS; ++i)
        if (!savePeek(saveSlotPath(i), &g_saveSlot[i]))
            memset(&g_saveSlot[i], 0, sizeof(g_saveSlot[i]));
}
static char g_joinIp[64] = "127.0.0.1";
static bool g_joinIpFocus = false;

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
/* These rows carry full names, so the icon is a quick visual index rather than
   the only way to identify an item. Keep them materially smaller than the
   icon-only pack/equipment squares, which still need the larger art. */
/* Sized DOWNWARD, not inward. The row is a name with an icon beside it, so
   height and icon are the slack -- the icon is a visual index and 22px indexes
   as well as 32 -- while width is the one dimension that cannot be spent,
   because it is what holds the name. An owned row also reserves 42px on the
   right for its count, so at 148 wide with a 32 icon the label had only 64px
   and was already ellipsising. Trimming the icon harder than the row means the
   label ends up with MORE room than before, at a smaller overall size.

   The height buys the real win: at 26 a fourth row fits in the same panel.
   Worst case -- equipment, drone chips and tool bench all present -- measures
   734px against the 768 canvas, where three 34px rows measured 729. So a third
   more of the list is visible for five pixels. Five rows would be 763 and is
   not taken: four pixels of headroom is not headroom. */
static const int CRE_ENTRY_W = 140;
static const int CRE_ENTRY_H = 26;
static const int CRE_ENTRY_GAP = 3;
static const int CRE_ENTRY_ICON_W = 22;
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
/* FOUR now, and it is the row height that paid for it rather than the budget
   changing. Three was right when a row was 34px tall with 32px art; at 26 the
   worst-case inventory -- equipment, drone chips and tool bench all present --
   measures 734px against the same 768px canvas, where three taller rows
   measured 729. A third more of the list for five pixels.

   Still bounded by that canvas and not by taste: five rows measures 763, and
   four pixels of headroom is not headroom. If a row grows again, this comes
   back down with it. */
/* Not a fixed count any more -- see creativeVisRows(). A constant here had to
   be sized for the WORST case (equipment, drone chips and tool bench all
   present at once), and then everyone got that number: the player with no
   bench and no drone was shown four rows while having room for seven. Those
   two blocks are conditional, so the worst case is the rare case.

   The bounds instead. The floor keeps the list usable when everything is on
   screen at once; the ceiling stops a nearly empty panel from turning into one
   enormous list, and is what the scroll-page jump is measured against. */
static const int CRE_MIN_ROWS = 3;
static const int CRE_MAX_ROWS = 8;
/* Breathing room top and bottom so the panel never sits flush against the
   canvas edge. */
static const int CRE_PANEL_MARGIN = 10;
/* How many rows the CURRENT panel contents leave room for. Defined after the
   layout constants it needs; g_creVisRows caches the last answer so the
   scrollbar and the wheel agree with what was drawn. */
static int g_creVisRows = 4;
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
static ItemStack& g_drag = g_playerSessions[0].cursor;
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
/* --- how the equipment row is arranged on screen ---------------------------
   A flat line of squares said nothing about which of them went together. So
   the row is drawn in three
   GROUPS with a heading over each -- what you are wearing, what you have
   chosen, what is flying beside you -- and the enum order is no longer the
   screen order.

   That separation is the point rather than an inconvenience. EquipSlot is
   append-only because its numbers are in every save (see the note there), so
   the two new trinkets are at the END of the enum and would otherwise have been
   drawn on the far side of the drone bays, three slots away from the two they
   are interchangeable with. A layout that has to match a compatibility
   constraint is a layout that gets worse every time the constraint is
   honoured. */
static const int EQ_ORDER[EQ_COUNT] = {
    EQ_FEET, EQ_BACK, EQ_HEAD, EQ_BODY,
    EQ_TRINKET_A, EQ_TRINKET_B, EQ_TRINKET_C, EQ_TRINKET_D,
    EQ_LIGHT_DRONE, EQ_DRONE_A, EQ_DRONE_B, EQ_DRONE_C
};
/* First screen position of each group, and one past the end. */
static const int EQ_GROUP_AT[4]      = { 0, 4, 8, EQ_COUNT };
static const char* const EQ_GROUP_NAME[3] = { "WORN", "TRINKETS", "DRONES" };
static const int EQ_SLOT_PITCH = 50;
static const int EQ_GROUP_GAP  = 16;

/* Screen x of the square at position `pos`, measured from the row's left edge.
   One function so the layout and the drawing cannot disagree about where a box
   is -- which they did, silently, the first time the drone bays were positioned
   from their enum index while everything else used its screen position. */
/* --- the equipped slots take two rows ---------------------------------------
   WORN and TRINKETS on the first, DRONES and the bin on the second.

   Twelve slots in a line came to 628 pixels, and the panel holding them was
   605. The panel's width is computed from the PALETTE grid and nothing else,
   so it had no idea the row existed. That arrangement fit for a while purely
   by luck -- at the old palette size the panel came out 728 wide against a 720
   requirement, eight pixels of margin propping up something nothing checked.
   Narrowing the palette took the panel to 605 and pushed the last drone bay
   and the bin straight off the right-hand edge.

   Folding at a GROUP boundary rather than after some slot count is what makes
   the break read as deliberate rather than as a wrap: the row already carried
   three headings, so the second row begins exactly where a heading already
   announced that something different starts.

   The panel is now sized around these rows as well as the palette -- see
   layoutCreative -- so neither narrowing the palette again nor adding a fifth
   bay can put a slot out of frame without the panel growing to meet it. */
static const int EQ_ROWS = 2;
static const int EQ_GROUP_ROW[3] = { 0, 0, 1 };
/* A heading sits 17 above its slots and a slot is 46 tall; the rest is the gap
   that keeps the second heading off the first row's squares. */
static const int EQ_ROW_PITCH = 76;

static int eqGroupOf(int pos) {
    int group = 0;
    while (group < 3 && pos >= EQ_GROUP_AT[group + 1]) ++group;
    return group;
}
/* Screen position of the first slot on a row: x is measured from there, not
   from the start of the whole sequence. */
static int eqRowFirstPos(int row) {
    for (int g = 0; g < 3; ++g) if (EQ_GROUP_ROW[g] == row) return EQ_GROUP_AT[g];
    return 0;
}
static int eqPosX(int pos) {
    const int group = eqGroupOf(pos), row = EQ_GROUP_ROW[group];
    int first = 0;
    while (EQ_GROUP_ROW[first] != row) ++first;
    return (pos - eqRowFirstPos(row)) * EQ_SLOT_PITCH
         + (group - first) * EQ_GROUP_GAP;
}
static int eqPosY(int pos) { return EQ_GROUP_ROW[eqGroupOf(pos)] * EQ_ROW_PITCH; }
/* Width of ONE row, for placing the bin after the last group on it and for
   sizing the panel around whichever row is widest. */
static int eqRowWidth(int row) {
    int last = -1;
    for (int pos = 0; pos < EQ_COUNT; ++pos)
        if (EQ_GROUP_ROW[eqGroupOf(pos)] == row) last = pos;
    return last < 0 ? 0 : eqPosX(last) + 46;
}
/* The row the bin shares, and the space it needs after that row's last slot. */
static const int EQ_BIN_GAP = 26;
static int eqBinRow() { return EQ_GROUP_ROW[2]; }

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
static ItemStack& g_trash = g_playerSessions[0].trash;

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
/* Whole-frame work, excluding the 60 Hz pacing sleep. g_simMs covers only
   world.step() and projUpdate, so it cannot show what input handling, player
   authority, UI and rendering cost -- which at 60 fps is exactly where
   headroom disappears without the frame rate moving at all. */
static double g_frameMs = 0.0;
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
static bool sendClientAction(u8 type, u8 container = 0, u8 a = 0, u8 b = 0, u8 flags = 0,
                             i32 x = 0, i32 y = 0);
static void applyPlayerAction(const NetAction& action);
static void applyPlayerUses(PlayerSession& session, const PlayerCommand& command);
static void fireToolFor(Player& player, Inventory& inventory, const Aim& aim);
static bool throwGlowflareFor(Player& player, Inventory& inventory, const Aim& aim);
static void placeDeviceStrokeFor(Inventory& inventory, int& previousX, int& previousY,
                                 u8 type, bool consume, const Aim& aim, int undoSlot = -1);

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
    if (g_survival && g_playerOn) {
        g_lineCommitPulse = true;
        g_lineCommitBits = (u8)((g_lmb ? PCMD_USE_LEFT : 0) | (g_rmb ? PCMD_USE_RIGHT : 0));
        g_lineCommitX = g_lineX; g_lineCommitY = g_lineY;
    }
    g_lineOn   = false;
    g_lineDrew = true;      /* so releasing R does not also respawn */
    if (g_survival && g_playerOn) { g_pmx = -1; return; }
    applyBrush();
    g_pmx = -1;
}

static void startWire() {
    if (!g_wireMode || g_uiCapture || g_mx < PANEL_W) return;
    const Aim a = currentWireAim();
    g_wireX = a.x; g_wireY = a.y;
    g_wireOn = true;
    if (g_survival && g_playerOn)
        sendClientAction(NACT_WIRE_POINT, 0, 0, 0, 0, a.ghostX, a.ghostY);
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
    undoBegin(LOCAL_PLAYER_ID, 0);
    undoCaptureCell(LOCAL_PLAYER_ID, x, y);
    g_world.setCell(x, y, MAT_COPPER);
}

static void commitWire() {
    if (!g_wireOn) return;
    g_wireOn = false;
    const Aim a = currentWireAim();
    if (g_survival && g_playerOn) {
        sendClientAction(NACT_WIRE_POINT, 0, 0, 0, 1, a.ghostX, a.ghostY);
        return;
    }
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
    char pressure[24] = "";
    if (MATS[c.mat].kind == KIND_GAS && (c.moisture & GAS_EXCESS_MASK))
        sprintf(pressure, "  pressure %u", (unsigned)(c.moisture & GAS_EXCESS_MASK));
    /* Names what is BEHIND as well as in front, because in background mode the
       thing you are about to act on is the one you cannot otherwise identify --
       a backdrop is deliberately too dark to tell apart by colour alone. */
    const u8 b = g_world.bgAt(cx, cy);
    if (b != MAT_EMPTY)
        _snprintf(out, cap, "%s  %+d C%s  / %s%s", name, t, pressure, MATS[b].name,
                  g_world.bgPlaced(cx, cy) ? " (built)" : "");
    else
        _snprintf(out, cap, "%s  %+d C%s", name, t, pressure);
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
    const int rowCount  = PALETTE_VISIBLE_ROWS + 4 + N_ACT; /* + size label, slider, speed, zoom */

    int pitch = (statsTop - top - sepTotal) / rowCount;
    if (pitch > 30) pitch = 30;
    if (pitch < 14) pitch = 14;   /* past this the labels are unreadable anyway;
                                     better to overflow visibly than to compute
                                     a zero or negative row height */
    const int gap = 4;
    const int h = pitch - gap;
    int y = top;

    /* The catalog is row-major: scrolling through it reads in the same order
       as its definitions, first all materials then all devices. With a
       character on it is the PACK instead -- see the note on g_packList. */
    {
        const int colGap = 6;
        const int railW = 10;
        const int catalogW = w - railW - 4;
        const int colW   = (catalogW - colGap) / 2;

        g_panelShowsPack = g_playerOn;

        /* Rebuilt every layout rather than cached, and layoutPanel runs on any
           panel interaction. The pack changes constantly -- every cell you dig
           adds to it -- and a list that only refreshed when something asked it
           to would be a list that is wrong most of the time. It is forty
           comparisons. */
        g_packListCount = 0;
        if (g_panelShowsPack)
            for (int i = 0; i < INV_SLOTS; ++i)
                if (!g_inv.slot[i].empty()) g_packList[g_packListCount++] = i;

        const int totalRows = g_panelShowsPack ? g_packListCount
                                               : (N_PALETTE + 1) / 2;
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

        /* BOTH sets of rects are cleared and only the live one filled, rather
           than leaving the other holding last frame's boxes. An empty RECT
           fails inRect() for free, so a stale rect from the other mode cannot
           take a click -- which is the failure this shape avoids rather than
           the failure it would otherwise have. */
        for (int i = 0; i < N_PALETTE; ++i) SetRectEmpty(&g_paletteRect[i]);
        for (int i = 0; i < INV_SLOTS; ++i) SetRectEmpty(&g_packListRect[i]);

        if (g_panelShowsPack) {
            for (int i = 0; i < g_packListCount; ++i) {
                const int row = i - g_paletteScroll;
                if (row < 0 || row >= PALETTE_VISIBLE_ROWS) continue;
                SetRect(&g_packListRect[i], pad, top + row * pitch,
                        pad + catalogW, top + row * pitch + h);
            }
        } else {
            for (int i = 0; i < N_PALETTE; ++i) {
                const int col = i & 1;
                const int row = i / 2 - g_paletteScroll;
                if (row < 0 || row >= PALETTE_VISIBLE_ROWS) continue;
                const int x0  = pad + col * (colW + colGap);
                SetRect(&g_paletteRect[i], x0, top + row * pitch, x0 + colW, top + row * pitch + h);
            }
        }
        y = top + PALETTE_VISIBLE_ROWS * pitch;
    }

    y += 6;
    /* Brush size keeps the precise stepper, with a full-width slider beneath
       it for crossing the 1..64 range quickly. Giving the rail its own row
       keeps the numeric readout legible instead of drawing text through it. */
    SetRect(&g_sizeDec, pad,            y, pad + 24,     y + h);
    SetRect(&g_sizeBox, pad + 24 + 4,   y, pad + w - 28, y + h);
    SetRect(&g_sizeInc, pad + w - 24,   y, pad + w,      y + h);
    y += pitch;
    SetRect(&g_sizeTrack, pad, y, pad + w, y + h);
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
        /* The one pair that shares a row. ACT_NIGHT is placed WITH its partner
           rather than on its own pass, so the two halves cannot drift apart --
           and the row is only advanced once, by the second of them. */
        if (i == ACT_DAY) {
            const int half = (w - 4) / 2;
            SetRect(&g_actRect[ACT_DAY],   pad,            y, pad + half,     y + h);
            SetRect(&g_actRect[ACT_NIGHT], pad + half + 4, y, pad + w,        y + h);
            continue;
        }
        if (i == ACT_NIGHT) { y += pitch; continue; }
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
    { "tap R",         "teleport to the cursor" },
    { "left / right",  "build / dig" },
    { "Ctrl+Z",        "undo last build/dig stroke" },
    { "right-click",   "open a machine, or a door" },
    { "wheel",         "pick a hotbar slot" },
    { "Q + wheel",     "brush size" },
    { "C",             "crafting" },
    { "F5 / F9",       "save / load" },
    { "F11",           "toggle fullscreen" },
    { "Tab",           "the item grid" },
    { "L",             "build on the backdrop" },
    { "V / K",         "cycle view / lights" },
    { "P / .",         "pause / step one frame" },
};
static const int N_KEY_HINTS = (int)(sizeof(KEY_HINTS) / sizeof(KEY_HINTS[0]));

/* The most recent clean picture of the world, kept for whenever a save is
   written. See the capture site in the render loop for why it cannot simply be
   taken at the moment of saving. */
static u8 g_thumbLatest[SAVE_THUMB_BYTES];

/* Defined further down; the save screen sits up here beside the menu it is
   reached from, and reaches back for the camera, the regenerate path and the
   shared button painter. */
static void updateCamera(bool snap);
static void makeWorld();
static void drawButton(HDC hdc, const RECT& r, const char* label,
                       HBRUSH swatch, bool selected, bool hot);

/* --- the picture on a slot --------------------------------------------------
   Box-filtered down from the live frame buffer rather than point-sampled. At
   4x1 reduction a nearest-neighbour shrink of a world made of one-cell speckle
   is almost pure noise -- it picks one arbitrary cell out of every sixteen, so
   a dirt wall and a stone wall come out as the same grey static. Averaging the
   block is what makes the thumbnail look like the place.

   Taken from g_pixels, which is the WORLD view only: no panel, no hotbar, no
   HUD. That is the right frame to keep. The panel is the same in every save and
   would waste two thirds of a small picture saying so. */
static void captureThumbnail(u8* rgb) {
    const int bx = VIEW_CELLS_W / SAVE_THUMB_W;   /* 512/128 = 4 */
    const int by = VIEW_CELLS_H / SAVE_THUMB_H;   /* 384/96  = 4 */
    for (int ty = 0; ty < SAVE_THUMB_H; ++ty) {
        for (int tx = 0; tx < SAVE_THUMB_W; ++tx) {
            u32 r = 0, g = 0, b = 0;
            for (int oy = 0; oy < by; ++oy) {
                const int sy = ty * by + oy;
                if (sy >= VIEW_CELLS_H) continue;
                for (int ox = 0; ox < bx; ++ox) {
                    const int sx = tx * bx + ox;
                    if (sx >= VIEW_CELLS_W) continue;
                    const u32 c = g_pixels[sy * VIEW_CELLS_W + sx];
                    r += (c >> 16) & 0xFF; g += (c >> 8) & 0xFF; b += c & 0xFF;
                }
            }
            const u32 n = (u32)(bx * by);
            u8* out = rgb + (ty * SAVE_THUMB_W + tx) * 3;
            out[0] = (u8)(r / n); out[1] = (u8)(g / n); out[2] = (u8)(b / n);
        }
    }
}

/* One place that writes a slot, so the message, the refresh and the client
   refusal cannot be got right in one caller and wrong in another. */
static void saveToSlot(int slot) {
    if (netRole() == NET_CLIENT) {
        sprintf(g_saveMsg, "The host owns this world's save");
        g_saveMsgFrames = 180; return;
    }
    const char* path = saveSlotPath(slot);
    /* A panel-picked stack is a transient hand override, not saved character
       state. Serialize the underlying hotbar choice, then restore the active
       override so pressing Save does not disturb what the player is doing. */
    const int temporarySelection = g_inv.selected;
    g_inv.selected = g_hotbarSelected;
    const bool wrote = saveWrite(path, g_world, g_thumbLatest);
    g_inv.selected = temporarySelection;
    if (wrote) {
        /* The bytes are in the filesystem; on the web that is not yet the
           same as being kept. See savePersist(). */
        savePersist();
        const double mb = (double)saveTotalBytes() / (1024.0 * 1024.0);
        sprintf(g_saveMsg, "Saved slot %d -- %.2f MB", slot + 1, mb);
    } else {
        sprintf(g_saveMsg, "SAVE FAILED: %s", saveError());
    }
    g_saveMsgFrames = 240;
    saveSlotsRefresh();
}

static void loadFromSlot(int slot) {
    if (netRole() == NET_CLIENT) {
        sprintf(g_saveMsg, "Only the host can load this world");
        g_saveMsgFrames = 180; return;
    }
    if (netConnected()) netStop();   /* loaded topology needs a fresh join snapshot */
    undoClearAll();
    const char* path = saveSlotPath(slot);
    if (saveRead(path, g_world)) {
        /* Saves contain the durable hotbar selection only. Older or malformed
           files are clamped here before `held()` can index the pack. */
        selectHotbar(g_inv.selected);
        const double mb = (double)saveTotalBytes() / (1024.0 * 1024.0);
        sprintf(g_saveMsg, "Loaded slot %d -- %.2f MB%s%s", slot + 1, mb,
                saveError()[0] ? " -- " : "", saveError());
        updateCamera(true);
        droneReset();
        accessoryReset();
    } else {
        /* A failed load leaves the world half-written, so it is not somewhere
           to carry on from -- the same recovery F9 has always made. */
        sprintf(g_saveMsg, "LOAD FAILED: %s", saveError());
        g_world.reset(); makeWorld();
    }
    g_saveMsgFrames = 240;
}

/* Five across and two down, which is the shape that fits ten 4:3 pictures into
   the view without either running off the side or shrinking them past the point
   of being recognisable. */
static void layoutSaveScreen() {
    const int cols = 5, rows = 2;
    const int cellW = 176, cellH = 158, gap = 10, pad = 18;
    const int w = pad * 2 + cols * cellW + (cols - 1) * gap;
    const int h = pad + 40 + rows * cellH + (rows - 1) * gap + 52;
    const int cx = PANEL_W + VIEW_W / 2, cy = VIEW_H / 2;
    SetRect(&g_savePanel, cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
    for (int i = 0; i < SAVE_SLOTS; ++i) {
        const int c = i % cols, r = i / cols;
        const int x = g_savePanel.left + pad + c * (cellW + gap);
        const int y = g_savePanel.top + pad + 40 + r * (cellH + gap);
        SetRect(&g_saveSlotRect[i], x, y, x + cellW, y + cellH);
    }
    SetRect(&g_saveBack, g_savePanel.left + pad, g_savePanel.bottom - 44,
            g_savePanel.left + pad + 120, g_savePanel.bottom - 14);
}

static void drawSaveScreen(HDC hdc) {
    layoutSaveScreen();
    const bool saving = (g_saveScreen == SAVESCREEN_SAVE);

    FillRect(hdc, &g_savePanel, g_panelBg);
    FrameRect(hdc, &g_savePanel, g_accentBrush);

    RECT title = g_savePanel;
    title.left += 18; title.top += 12;
    SetTextColor(hdc, RGB(245, 224, 150));
    DrawTextA(hdc, saving ? "SAVE  --  click a slot to write it"
                          : "LOAD  --  click a slot to open it",
              -1, &title, DT_LEFT | DT_TOP | DT_SINGLELINE);

    for (int i = 0; i < SAVE_SLOTS; ++i) {
        RECT r = g_saveSlotRect[i];
        const SaveSlotInfo& info = g_saveSlot[i];
        const bool hot = inRect(r, g_mx, g_my);
        /* An empty slot is not clickable in LOAD mode, so it must not light up
           under the cursor either -- a hover state on a dead control is the
           interface promising something it will not do. */
        const bool live = saving || (info.used && info.readable);

        FillRect(hdc, &r, (hot && live) ? g_btnBgHot : g_btnBg);
        FrameRect(hdc, &r, (hot && live) ? g_accentBrush : g_borderBrush);

        RECT pic = r;
        pic.left += 6; pic.right -= 6; pic.top += 22; pic.bottom = pic.top + 96;

        if (info.hasThumb) {
            /* Bottom-up DIB with a negative height, which is how a Windows
               bitmap is told its rows run top-down. The rows are BGR on the
               wire, so the channel swap happens here rather than at capture --
               the file stays plain RGB and only the one place that hands it to
               GDI has to know about Windows' order. */
            static u8 bgr[SAVE_THUMB_BYTES];
            for (int k = 0; k < SAVE_THUMB_W * SAVE_THUMB_H; ++k) {
                bgr[k * 3 + 0] = info.rgb[k * 3 + 2];
                bgr[k * 3 + 1] = info.rgb[k * 3 + 1];
                bgr[k * 3 + 2] = info.rgb[k * 3 + 0];
            }
            BITMAPINFOHEADER bi;
            memset(&bi, 0, sizeof(bi));
            bi.biSize = sizeof(bi);
            bi.biWidth = SAVE_THUMB_W;
            bi.biHeight = -SAVE_THUMB_H;
            bi.biPlanes = 1;
            bi.biBitCount = 24;
            bi.biCompression = BI_RGB;
            SetStretchBltMode(hdc, COLORONCOLOR);
            StretchDIBits(hdc, pic.left, pic.top,
                          pic.right - pic.left, pic.bottom - pic.top,
                          0, 0, SAVE_THUMB_W, SAVE_THUMB_H,
                          bgr, (BITMAPINFO*)&bi, DIB_RGB_COLORS, SRCCOPY);
            FrameRect(hdc, &pic, g_borderBrush);
        } else {
            FillRect(hdc, &pic, g_btnBgSel);
            FrameRect(hdc, &pic, g_borderBrush);
            SetTextColor(hdc, RGB(112, 120, 134));
            RECT t = pic; t.top += 38;
            DrawTextA(hdc, info.used ? "(no preview)" : "empty", -1, &t,
                      DT_CENTER | DT_TOP | DT_SINGLELINE);
        }

        char head[48];
        sprintf(head, "Slot %d", i + 1);
        RECT hr = r; hr.left += 8; hr.top += 4;
        SetTextColor(hdc, live ? RGB(226, 230, 238) : RGB(120, 128, 142));
        DrawTextA(hdc, head, -1, &hr, DT_LEFT | DT_TOP | DT_SINGLELINE);

        /* The stamp under the picture. Date and time rather than "3 hours ago":
           a relative time is friendlier to read once and useless for telling two
           slots apart a day later, which is the entire job here. */
        char foot[80] = "";
        if (!info.used) {
            strcpy(foot, "-");
        } else if (!info.readable) {
            sprintf(foot, "%s", info.note);
        } else if (info.when) {
            const time_t t = (time_t)info.when;
            const struct tm* lt = localtime(&t);
            if (lt) strftime(foot, sizeof(foot), "%d %b  %H:%M", lt);
            sprintf(foot + strlen(foot), "   %.0f MB",
                    (double)info.bytes / (1024.0 * 1024.0));
        } else {
            sprintf(foot, "%.0f MB", (double)info.bytes / (1024.0 * 1024.0));
        }
        RECT fr = r; fr.left += 8; fr.right -= 8; fr.top = pic.bottom + 6;
        SetTextColor(hdc, info.readable || !info.used ? RGB(160, 168, 182)
                                                     : RGB(214, 130, 110));
        DrawTextA(hdc, foot, -1, &fr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    drawButton(hdc, g_saveBack, "Back", NULL, false, inRect(g_saveBack, g_mx, g_my));

    RECT hint = g_savePanel;
    hint.left += 150; hint.top = g_savePanel.bottom - 38;
    SetTextColor(hdc, RGB(140, 148, 162));
    DrawTextA(hdc, saving ? "Writing a slot replaces whatever is in it. Esc to go back."
                          : "Esc to go back.",
              -1, &hint, DT_LEFT | DT_TOP | DT_SINGLELINE);
}

static bool handleSaveScreenClick(int mx, int my) {
    if (inRect(g_saveBack, mx, my)) { g_saveScreen = SAVESCREEN_OFF; return true; }
    for (int i = 0; i < SAVE_SLOTS; ++i) {
        if (!inRect(g_saveSlotRect[i], mx, my)) continue;
        if (g_saveScreen == SAVESCREEN_SAVE) {
            saveToSlot(i);
        } else {
            /* Refused rather than silently ignored. Clicking an empty slot in
               load mode is a reasonable thing to try, and saying nothing at all
               reads as the button being broken. */
            if (!g_saveSlot[i].used) {
                sprintf(g_saveMsg, "Slot %d is empty", i + 1);
                g_saveMsgFrames = 150;
                return true;
            }
            if (!g_saveSlot[i].readable) {
                sprintf(g_saveMsg, "Slot %d: %s", i + 1, g_saveSlot[i].note);
                g_saveMsgFrames = 240;
                return true;
            }
            loadFromSlot(i);
            g_saveScreen = SAVESCREEN_OFF;
            g_menuOpen = false;
        }
        return true;
    }
    /* Anywhere inside the panel but not on a control is swallowed, so a stray
       click cannot fall through and dig a hole in the world behind it. */
    return inRect(g_savePanel, mx, my);
}

static void layoutMenu() {
    /* Two rows taller than it was, for Save and Load. They go directly under
       Resume rather than down beside Quit, because they are the things you open
       this menu FOR -- the networking block below them is a thing you set up
       once a session. */
    const int keyPitch = uiScaled(15);
    const int savePitch = uiScaled(15);
    const int w = 380, h = 300 + 120 + N_KEY_HINTS * keyPitch + 22
                    + (saveTotalBytes() > 0 ? 26 + 8 * savePitch : 0);
    const int cx = PANEL_W + VIEW_W / 2, cy = VIEW_H / 2;
    SetRect(&g_menuPanel, cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
    const int bw = w - 120, bx = cx - bw / 2;
    const int top = g_menuPanel.top;
    SetRect(&g_menuResume, bx, top + 42, bx + bw, top + 74);
    SetRect(&g_menuSave,   bx, top + 82, bx + bw, top + 114);
    SetRect(&g_menuLoad,   bx, top + 122, bx + bw, top + 154);
    SetRect(&g_menuHost,   bx, top + 162, bx + bw, top + 194);
    SetRect(&g_menuIp,     bx, top + 202, bx + bw - 88, top + 234);
    SetRect(&g_menuJoin,   bx + bw - 80, top + 202, bx + bw, top + 234);
    SetRect(&g_menuStop,   bx, top + 242, bx + bw, top + 274);
    SetRect(&g_menuQuit,   bx, top + 282, bx + bw, top + 314);
    const int uy = top + 322;
    SetRect(&g_menuUiMinus, bx, uy, bx + 42, uy + 30);
    SetRect(&g_menuUiValue, bx + 50, uy, bx + bw - 50, uy + 30);
    SetRect(&g_menuUiPlus,  bx + bw - 42, uy, bx + bw, uy + 30);
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
       A window of however many rows fit, with the rest scrolled past, so the
       panel's height no longer depends on how many materials exist. That
       matters more every time one is added: the crops alone put eight new rows
       in here.

       Rects for rows outside the window are set EMPTY rather than merely being
       skipped when drawing. inRect() then fails on them for free, so a click
       cannot land on a row that is scrolled out of sight -- which is the bug
       this shape avoids rather than the bug it would otherwise have. */
    const int cw = CRE_ENTRY_W, ch = CRE_ENTRY_H;
    const int gap = CRE_ENTRY_GAP, pad = 14;
    const int ps = 50, pgap = 3;
    g_creRowCount = (g_creCount + CRE_COLS - 1) / CRE_COLS;

    const int benchH = g_toolSlotCount ? 74 : 0;
    /* Taller than it was, and every one of the extra pixels is text: two lines
       of resolved stats above the row instead of one that ran off the end of
       the panel, and a line of group headings between them and the squares. */
    const int equipH = signalPicker ? 0 : 108 + (EQ_ROWS - 1) * EQ_ROW_PITCH;
    bool hasDrone = false;
    if (!signalPicker) for (int i = 0; i < MAX_DRONES; ++i) {
        const int eq = i == 0 ? EQ_LIGHT_DRONE : i == 1 ? EQ_DRONE_A :
                       i == 2 ? EQ_DRONE_B : EQ_DRONE_C;
        if (!g_inv.equip[eq].empty()) { hasDrone = true; break; }
    }
    const int droneModuleH = hasDrone ? 74 : 0;
    const int packH    = signalPicker ? 0 : 22 + INV_ROWS * (ps + pgap) + 10;

    /* --- how many rows fit, rather than how many are allowed ---------------
       Everything above is conditional: no tool bench is 74px back, no drone
       another 74, and the signal picker drops the pack and the equipment strip
       entirely. Deriving the count from what is actually there gives the list
       every pixel nobody else is using, and gives it back the moment they need
       it -- a bench appearing takes a row away rather than pushing the panel
       off the canvas, which is what a fixed count sized for the light case
       would have done. */
    const int fixedH = pad + 56 + 10 + packH + equipH + droneModuleH + benchH + 38;
    const int roomH  = VIEW_H - CRE_PANEL_MARGIN * 2 - fixedH;
    int visRows = roomH / (ch + gap);
    if (visRows < CRE_MIN_ROWS) visRows = CRE_MIN_ROWS;
    if (visRows > CRE_MAX_ROWS) visRows = CRE_MAX_ROWS;
    if (visRows > g_creRowCount) visRows = g_creRowCount;
    if (visRows < 1) visRows = 1;
    /* Cached for the scrollbar and the wheel, which run outside this function
       and must page by exactly what was drawn. */
    g_creVisRows = visRows;

    const int maxScroll = imax(0, g_creRowCount - visRows);
    if (g_creScroll > maxScroll) g_creScroll = maxScroll;
    if (g_creScroll < 0) g_creScroll = 0;

    const int paletteH = visRows * (ch + gap);
    const int barW     = 10;
    const int paletteW = pad * 2 + CRE_COLS * cw + (CRE_COLS - 1) * gap + barW + 6;
    /* The palette used to decide this alone, which is how the equipment row
       came to hang off the side of the panel containing it. Ask the rows how
       much they need as well and take the larger: a layout that cannot state
       its own width will eventually be given the wrong one. */
    int equipW = 0;
    if (!signalPicker) {
        for (int row = 0; row < EQ_ROWS; ++row) {
            int need = eqRowWidth(row);
            if (row == eqBinRow()) need += EQ_BIN_GAP + 46;
            if (need > equipW) equipW = need;
        }
        equipW += pad * 2;
    }
    const int w = imax(paletteW, equipW);
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

    /* Two stat lines and a row of group headings sit above the squares, so the
       squares themselves start well below the top of their band. */
    const int eqY = packY + packH + 40;
    for (int d = 0; d < MAX_DRONES; ++d)
        for (int i = 0; i < Inventory::DRONE_MODULE_SLOTS_MAX; ++i) SetRectEmpty(&g_droneModuleRect[d][i]);
    if (!signalPicker) for (int pos = 0; pos < EQ_COUNT; ++pos) {
        const int bx = x0 + pad + eqPosX(pos);
        const int by = eqY + eqPosY(pos);
        SetRect(&g_eqRect[EQ_ORDER[pos]], bx, by, bx + 46, by + 46);
    }
    /* Well clear of the last group, so it reads as separate from the things you
       are wearing rather than as another equipment slot. */
    if (signalPicker) SetRectEmpty(&g_trashRect);
    else {
        const int binX = x0 + pad + eqRowWidth(eqBinRow()) + EQ_BIN_GAP;
        const int binY = eqY + eqBinRow() * EQ_ROW_PITCH;
        SetRect(&g_trashRect, binX, binY, binX + 46, binY + 46);
    }

    const int droneY = eqY + equipH - 40;
    if (!signalPicker) for (int d = 0; d < MAX_DRONES; ++d) {
        const int eq = d == 0 ? EQ_LIGHT_DRONE : d == 1 ? EQ_DRONE_A :
                       d == 2 ? EQ_DRONE_B : EQ_DRONE_C;
        if (g_inv.equip[eq].empty()) continue;
        /* Directly under the bay it belongs to, which means asking where that
           bay was DRAWN rather than where it sits in the enum. Reading the rect
           back is the version that cannot drift: there is one answer to "where
           is this slot" and it was computed a few lines ago.

           Current chassis all expose one socket. The other two rects remain
           empty until an upgraded chassis unlocks them, so the UI cannot
           accidentally accept a future slot early. */
        const int bayX = g_eqRect[eq].left;
        SetRect(&g_droneModuleRect[d][0], bayX, droneY, bayX + 46, droneY + 46);
    }

    /* Module slots: square, and noticeably bigger than a grid row, because they
       are the main interaction target. Their left-to-right order is also the
       firing sequence, wrapping after the last occupied slot. */
    const int by2 = eqY + (equipH - 40) + droneModuleH;
    for (int i = 0; !signalPicker && i < g_toolSlotCount; ++i) {
        const int bx = x0 + pad + i * 52;
        SetRect(&g_toolSlotRect[i], bx, by2, bx + 46, by2 + 46);
    }
    /* One slot further along than the last module -- a visible gap would
       misread as "another module slot the tool does not have", so it sits
       flush against them and earns its own label in drawCreative() instead. */
    if (!signalPicker && g_toolPackSlot >= 0) {
        const int bx = x0 + pad + g_toolSlotCount * 52;
        SetRect(&g_toolPayloadRect, bx, by2, bx + 46, by2 + 46);
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
static NetAction g_localActions[256];
static int g_localActionHead = 0, g_localActionCount = 0;
static NetAction g_predictedActions[256];
static int g_predictedActionHead = 0, g_predictedActionCount = 0;
static u32 g_actionPredictionStateSerial = 0;

static void actionPredictionClear() {
    g_predictedActionHead = g_predictedActionCount = 0;
    g_actionPredictionStateSerial = netStateSerial();
}

static void actionPredictionRemember(const NetAction& action) {
    if (g_predictedActionCount == 256) {
        g_predictedActionHead = (g_predictedActionHead + 1) % 256;
        --g_predictedActionCount;
    }
    const int tail = (g_predictedActionHead + g_predictedActionCount) % 256;
    g_predictedActions[tail] = action; ++g_predictedActionCount;
}

static void actionPredictionReconcile() {
    const u32 serial = netStateSerial();
    if (serial == g_actionPredictionStateSerial) return;
    g_actionPredictionStateSerial = serial;
    const u32 acknowledged = netAcknowledgedAction();
    while (g_predictedActionCount > 0 &&
           (i32)(g_predictedActions[g_predictedActionHead].sequence - acknowledged) <= 0) {
        g_predictedActionHead = (g_predictedActionHead + 1) % 256;
        --g_predictedActionCount;
    }
    for (int n = 0; n < g_predictedActionCount; ++n)
        applyPlayerAction(g_predictedActions[(g_predictedActionHead + n) % 256]);
}

/* One submission API for every player gesture. A joined client serializes it;
   the authority (including ordinary single-player) places the exact same
   action into a local loopback queue. UI code therefore cannot accidentally
   grow a trusted host-only version of an inventory or crafting operation. */
static bool sendClientAction(u8 type, u8 container, u8 a, u8 b, u8 flags, i32 x, i32 y) {
    if (netRole() == NET_CLIENT && !netClientReady()) return false;
    static u32 sequence = 0;
    NetAction action; memset(&action, 0, sizeof(action));
    action.sequence = ++sequence;
    action.player = netRole() == NET_CLIENT ? netAssignedPlayer() : LOCAL_PLAYER_ID;
    action.generation = g_playerSessions[0].generation;
    action.type = type; action.container = container;
    action.a = a; action.b = b; action.flags = flags;
    action.x = x; action.y = y;
    if (netRole() == NET_CLIENT) {
        if (!netSendAction(action)) return false;
        actionPredictionRemember(action);
        applyPlayerAction(action);
        return true;
    }
    if (g_localActionCount >= (int)(sizeof(g_localActions) / sizeof(g_localActions[0]))) return false;
    const int tail = (g_localActionHead + g_localActionCount) %
                     (int)(sizeof(g_localActions) / sizeof(g_localActions[0]));
    g_localActions[tail] = action; ++g_localActionCount; return true;
}

/* Open a machine's panel, or close it with -1.

   Two variables describe one fact and both have to know it. `g_devPanel` is
   what the UI draws; `PlayerSession::openDevice` is what applyDeviceAction
   reads to find out which machine a button press is about. In survival the
   session owns it -- interactFor sets it and syncClientDeviceUi mirrors it back
   into g_devPanel -- but with the character switched OFF there is no command
   stream to carry an interaction, so the mouse handler opens the panel
   directly and used to set only the UI half.

   The panel therefore appeared and every control in it was inert: Turn, plus,
   minus, Take, depth and mode all resolved against openDevice == -1 and
   returned immediately. Reported as not being able to change a spout's facing
   with the player off, which was the visible corner of all of them.

   Only written here when the character is off. Doing it unconditionally would
   let a survival click on empty ground clear an openDevice the command stream
   still believes in, which is the session's to decide and not the UI's. */
static void setDevicePanel(int index) {
    g_devPanel = index;
    if (!(g_survival && g_playerOn)) g_playerSessions[0].openDevice = index;
}

static bool popLocalAction(NetAction* action) {
    if (!action || g_localActionCount <= 0) return false;
    *action = g_localActions[g_localActionHead];
    g_localActionHead = (g_localActionHead + 1) %
                        (int)(sizeof(g_localActions) / sizeof(g_localActions[0]));
    --g_localActionCount; return true;
}

/* Put whatever the cursor is holding back in the pack. Called when the screen
   closes, because a stack on a cursor that is no longer drawn is a stack that
   has silently ceased to exist. If it will not fit it stays on the cursor and
   comes back with the screen, which is the only lossless answer. */
static void dragStow() {
    if (g_drag.empty()) return;
    sendClientAction(NACT_STOW_CURSOR);
}

static void openChest(int index) {
    if (index < 0 || index >= MAX_DEVICES || !g_devices[index].used) return;
    Device& d = g_devices[index];
    g_playerSessions[0].openDevice = index;
    g_chestOpen = index; g_devPanel = -1; g_logisticsUiOpen = true;
    g_chestStack.item = d.count ? (ItemId)d.mat : ITEM_NONE;
    g_chestStack.count = d.count; g_chestStack.inst = 0;
    const int x = PANEL_W + (VIEW_W - 620) / 2, y = (VIEW_H - 430) / 2;
    SetRect(&g_chestPanel, x, y, x + 620, y + 430);
    SetRect(&g_chestSlot, x + 54, y + 68, x + 106, y + 120);
    SetRect(&g_chestClose, x + 580, y + 10, x + 604, y + 32);
    for (int i = 0; i < INV_SLOTS; ++i) {
        const int c = i % HOTBAR_SLOTS, r = i / HOTBAR_SLOTS;
        const int rr = r == 0 ? INV_ROWS - 1 : r - 1;
        SetRect(&g_chestPack[i], x + 54 + c * 52, y + 170 + rr * 54,
                x + 104 + c * 52, y + 220 + rr * 54);
    }
}

static void closeChest() {
    if (netRole() == NET_CLIENT) {
        g_closeDevicePending = g_chestOpen;
        g_playerSessions[0].openDevice = -1;
    }
    sendClientAction(NACT_CLOSE_DEVICE);
    g_chestOpen = -1; g_logisticsUiOpen = false; dragStow();
}

static bool handleChestClick(int mx, int my, bool right) {
    if (g_chestOpen < 0) return false;
    if (inRect(g_chestClose, mx, my)) { closeChest(); return true; }
    if (inRect(g_chestSlot, mx, my)) {
        sendClientAction(NACT_SLOT, NSLOT_CHEST, 0, 0, right ? 1 : 0);
        return true;
    }
    for (int i = 0; i < INV_SLOTS; ++i)
        if (inRect(g_chestPack[i], mx, my)) {
            sendClientAction(NACT_SLOT, NSLOT_PACK, (u8)i, 0, right ? 1 : 0);
            return true;
        }
    return true;
}

static void drawChestStack(HDC hdc, const RECT& r, const ItemStack& st) {
    FillRect(hdc, &r, inRect(r, g_mx, g_my) ? g_btnBgHot : g_btnBg);
    FrameRect(hdc, &r, g_borderBrush);
    if (st.empty()) return;
    RECT ir = r; InflateRect(&ir, -3, -3);
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
            sendClientAction(NACT_DEVICE, 0, NDEV_SET_FILTER, 0);
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
        if      (my < g_creThumb.top)    g_creScroll -= g_creVisRows;
        else if (my > g_creThumb.bottom) g_creScroll += g_creVisRows;
        layoutCreative();
        return true;
    }

    if (g_signalPickerDevice < 0) for (int i = 0; i < INV_SLOTS; ++i)
        if (inRect(g_packRect[i], mx, my)) {
            sendClientAction(NACT_SLOT, NSLOT_PACK, (u8)i, 0, remove ? 1 : 0);
            /* Picking a tool up or putting one down changes whether the bench
               exists, which changes the panel height. */
            layoutCreative();
            return true;
        }

    if (g_signalPickerDevice < 0) for (int i = 0; i < EQ_COUNT; ++i)
        if (inRect(g_eqRect[i], mx, my)) {
            sendClientAction(NACT_SLOT, NSLOT_EQUIP, (u8)i, 0, remove ? 1 : 0);
            layoutCreative(); return true;
        }

    if (g_signalPickerDevice < 0) for (int d = 0; d < MAX_DRONES; ++d)
        for (int i = 0; i < Inventory::DRONE_MODULE_SLOTS_MAX; ++i)
            if (inRect(g_droneModuleRect[d][i], mx, my)) {
                sendClientAction(NACT_SLOT, NSLOT_DRONE_MODULE, (u8)d, (u8)i, remove ? 1 : 0);
                layoutCreative(); return true;
            }

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
        sendClientAction(NACT_SLOT, NSLOT_TRASH);
        layoutCreative();
        return true;
    }

    if (g_signalPickerDevice < 0 && g_toolPackSlot >= 0 && g_inv.slot[g_toolPackSlot].inst) {
        for (int i = 0; i < g_toolSlotCount; ++i)
            if (inRect(g_toolSlotRect[i], mx, my)) {
                sendClientAction(NACT_SLOT, NSLOT_TOOL_MODULE,
                                 (u8)g_toolPackSlot, (u8)i, remove ? 1 : 0);
                return true;
            }
        /* The payload slot is a genuine ItemStack, so it speaks slotClick's
           ordinary lift/drop/merge/split language rather than moduleClick's
           unique-item swap -- loading ammunition is a pack interaction, not
           an installation. Only a MATERIAL may go here: dropping a tool or
           a module into your own ammunition slot is not a thing to allow
           quietly, so a non-material is simply refused rather than
           accepted and then doing something nobody asked for. */
        if (inRect(g_toolPayloadRect, mx, my)) {
            if (!g_drag.empty() && ITEMS[g_drag.item].kind != ITEMK_MATERIAL) return true;
            sendClientAction(NACT_SLOT, NSLOT_TOOL_PAYLOAD,
                             (u8)g_toolPackSlot, 0, remove ? 1 : 0);
            return true;
        }

    }

    for (int i = 0; i < g_creCount; ++i) {
        if (!inRect(g_creRect[i], mx, my)) continue;
        const int it = g_creItem[i];
        if (g_signalPickerDevice >= 0) {
            const u8 op = g_signalPickerField == CIR_PICK_SIGNAL ? NDEV_SET_SIGNAL :
                          g_signalPickerField == CIR_PICK_A ? NDEV_SET_A :
                          g_signalPickerField == CIR_PICK_B ? NDEV_SET_B : NDEV_SET_OUT;
            sendClientAction(NACT_DEVICE, 0, op, (u8)it);
            closeSignalPicker();
            return true;
        }
        if (g_filterDevice >= 0) {
            if (it < MAT_COUNT) {
                sendClientAction(NACT_DEVICE, 0, NDEV_SET_FILTER, (u8)it);
            }
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
        if (netRole() == NET_CLIENT) {
            sendClientAction(NACT_CREATIVE_ITEM, 0, (u8)it, 0, remove ? 1 : 0);
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
            g_drag.inst  = (ITEMS[it].kind == ITEMK_TOOL) ? toolInstNew(it) : 0;
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
static void changeSize(int d){
    g_brushRadius = imax(BRUSH_RADIUS_MIN,
                         imin(BRUSH_RADIUS_MAX, g_brushRadius + d));
}

/* The thumb's centre represents the selected radius. The same geometry drives
   drawing and pointer mapping, so size 1/64 land exactly on the rail ends and
   the displayed thumb cannot disagree with the value a click produces. */
static RECT sizeThumbRect() {
    static const int thumbW = 12;
    const int travel = imax(1, g_sizeTrack.right - g_sizeTrack.left - thumbW);
    const int range = BRUSH_RADIUS_MAX - BRUSH_RADIUS_MIN;
    const int at = (g_brushRadius - BRUSH_RADIUS_MIN) * travel / range;
    RECT r = { g_sizeTrack.left + at, g_sizeTrack.top + 1,
               g_sizeTrack.left + at + thumbW, g_sizeTrack.bottom - 1 };
    return r;
}

static void setSizeFromSlider(int mx) {
    static const int thumbW = 12;
    const int travel = imax(1, g_sizeTrack.right - g_sizeTrack.left - thumbW);
    const int at = imax(0, imin(travel, mx - g_sizeTrack.left - thumbW / 2));
    const int range = BRUSH_RADIUS_MAX - BRUSH_RADIUS_MIN;
    g_brushRadius = BRUSH_RADIUS_MIN + (at * range + travel / 2) / travel;
}
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
/* Wood is the third, and it is a BLUNT fix rather than a designed one, which
   is worth stating so nobody mistakes it for a considered number. Wood gates
   the workbench, the torch and most of the early ladder, and gathering it was
   simply tedious -- so a thousand of it removes the tedium without anybody
   having to decide yet what the real economy should be.

   The actual fix went in beside it: seeds no longer need wet soil (see
   rootableSoil in tree.cpp), so planting a forest works the first time you try
   it rather than failing invisibly. If that turns out to be enough, this
   number should come back down hard -- a starting stack this large also
   deletes the first hour of scarcity, which is a real cost and not obviously
   one worth paying twice. */
static const int STARTING_WOOD = 1000;

static void giveStartingKit() {
    if (g_inv.countOf(ITEM_BOLTER) == 0) g_inv.add(ITEM_BOLTER, 1);
    if (g_inv.countOf(ITEM_FLINT)  == 0) g_inv.add(ITEM_FLINT, 1);
    if (g_inv.countOf((ItemId)MAT_WOOD) == 0)
        g_inv.add((ItemId)MAT_WOOD, STARTING_WOOD);
}

/* Builds the world. See worldgen.cpp -- plains to the left, a mountain to the
   right, and the flats beyond it. */
static void makeWorld() {
    /* Machines are entities beside the grid, so clearing the world does not clear
       them -- they have to be dropped explicitly or a fresh world arrives haunted
       by the last one's contraptions. Same reason roomsClear() exists. */
    undoClearAll();
    devClear();
    g_restBed = -1;
    g_playerSessions[0].respawnBedX = g_playerSessions[0].respawnBedY = -1;
    g_playerSessions[0].respawnFrames = 0;
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
    /* The save screen is drawn over the menu, so it takes the click first --
       otherwise a slot in the top row would land on the menu button behind it. */
    if (g_saveScreen != SAVESCREEN_OFF) return handleSaveScreenClick(mx, my);
    if (g_menuOpen) {
        if (inRect(g_menuResume, mx, my)) { g_menuOpen = false; return true; }
        if (inRect(g_menuHost, mx, my)) {
            g_survival = true; g_playerOn = true;
            netHost(); g_joinIpFocus = false; return true;
        }
        if (inRect(g_menuSave, mx, my)) {
            g_saveScreen = SAVESCREEN_SAVE; saveSlotsRefresh(); return true;
        }
        if (inRect(g_menuLoad, mx, my)) {
            g_saveScreen = SAVESCREEN_LOAD; saveSlotsRefresh(); return true;
        }
        if (inRect(g_menuIp, mx, my)) { g_joinIpFocus = true; return true; }
        if (inRect(g_menuJoin, mx, my)) {
            g_survival = true; g_playerOn = true;
            netJoin(g_joinIp); g_joinIpFocus = false; return true;
        }
        if (inRect(g_menuStop, mx, my)) { netStop(); g_joinIpFocus = false; return true; }
        if (inRect(g_menuUiMinus, mx, my)) { changeUiScale(-1); return true; }
        if (inRect(g_menuUiPlus,  mx, my)) { changeUiScale(+1); return true; }
        if (inRect(g_menuQuit,   mx, my)) { g_running = false; PostQuitMessage(0); return true; }
        return true;
    }
    if (g_survival && g_playerOn) {
        for (int i = 0; i < HOTBAR_SLOTS; ++i)
            if (inRect(g_hotRect[i], mx, my)) { selectHotbar(i); return true; }
    }
    /* --- picking something out of the pack list --------------------------
       This list is a quick-use surface, not an inventory-arrangement surface.
       Point the held index straight at the clicked stack without moving either
       it or the selected hotbar stack. Wheel/number input calls selectHotbar()
       and therefore dismisses this temporary override in one gesture. */
    for (int i = 0; i < g_packListCount; ++i) {
        if (!inRect(g_packListRect[i], mx, my)) continue;
        const int slot = g_packList[i];
        g_inv.selected = imax(0, imin(INV_SLOTS - 1, slot));
        layoutPanel();
        return true;
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
        if (inRect(g_speedRect[i], mx, my)) {
            if (netRole() != NET_CLIENT) g_speedIdx = i;
            return true;
        }
    }
    for (int i = 0; i < N_ZOOM; ++i) {
        if (inRect(g_zoomRect[i], mx, my)) { changeZoom(i - g_zoomIdx); return true; }
    }
    if (inRect(g_sizeTrack, mx, my)) {
        setSizeFromSlider(mx);
        g_sizeDragging = true;
        return true;
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
        if (netRole() != NET_OFF) return true;
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
    if (inRect(g_actRect[ACT_PAUSE],     mx, my)) {
        if (netRole() != NET_CLIENT) g_paused = !g_paused;
        return true;
    }
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
    /* --- setting the time -----------------------------------------------
       Straight to the middle of each half rather than to its start, so the
       result is unambiguous: "day" lands at full daylight with a long way to go
       before dusk, and "night" lands in the flat dark rather than in a dawn
       that is already brightening. Jumping to t=0.50 would technically be night
       and would begin lightening within seconds, which is not what anybody
       clicking a button called Night wants to see.

       See dayLight() for the shape these two numbers are read off: full
       daylight below 0.42, dusk to 0.50, night to 0.92, then dawn. */
    if (inRect(g_actRect[ACT_DAY], mx, my)) {
        /* The host owns the clock, the same way it owns the save and the pause.
           A client setting its own time would desynchronise the one piece of
           world state that drives spawning. */
        if (netRole() != NET_CLIENT) {
            g_worldTime = (u32)((float)DAY_LENGTH * 0.20f);
            sprintf(g_saveMsg, "Set to midday");
            g_saveMsgFrames = 120;
        }
        return true;
    }
    if (inRect(g_actRect[ACT_NIGHT], mx, my)) {
        if (netRole() != NET_CLIENT) {
            g_worldTime = (u32)((float)DAY_LENGTH * 0.70f);
            sprintf(g_saveMsg, "Set to midnight");
            g_saveMsgFrames = 120;
        }
        return true;
    }
    if (inRect(g_actRect[ACT_CLEAR],     mx, my)) {
        if (netRole() == NET_CLIENT) return true;
        if (netConnected()) netStop();
        g_world.reset(); makeWorld(); return true;
    }
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

    case WM_SIZE:
        updatePresentRect(hwnd);
        return 0;

    case WM_MOUSEMOVE:
        updateMouseFromLParam(lp);
        if (g_sizeDragging) setSizeFromSlider(g_mx);
        return 0;

    case WM_CHAR:
        if (g_menuOpen && g_joinIpFocus) {
            const char ch = (char)wp; int n = (int)strlen(g_joinIp);
            if (ch == '\b' && n > 0) g_joinIp[n - 1] = 0;
            else if ((ch == '.' || (ch >= '0' && ch <= '9')) && n < (int)sizeof(g_joinIp) - 1) {
                g_joinIp[n] = ch; g_joinIp[n + 1] = 0;
            } else if (ch == '\r') {
                g_survival = true; g_playerOn = true;
                netJoin(g_joinIp); g_joinIpFocus = false;
            }
            return 0;
        }
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
        updateMouseFromLParam(lp);
        if (g_mx < 0 || g_my < 0) return 0; /* letterbox, not game space */
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
        if (!(g_survival && g_playerOn)) undoFinish(LOCAL_PLAYER_ID, 0);
        g_lmb = false; g_useLatch = false; g_uiCapture = false;
        g_sizeDragging = false; ReleaseCapture(); return 0;
    case WM_RBUTTONDOWN:
        updateMouseFromLParam(lp);
        if (g_mx < 0 || g_my < 0) return 0; /* letterbox, not game space */
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
            if (g_survival && g_playerOn && (d || isDoor(g_world.at(a.x, a.y).mat))) {
                g_interactPulse = true;
                break;
            }
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
                        g_playerSessions[0].respawnBedX = d->x;
                        g_playerSessions[0].respawnBedY = d->y;
                    } else {
                        sprintf(g_saveMsg, "Stand by the bed to rest");
                        g_saveMsgFrames = 120;
                    }
                    break;
                }
                if (d->type == DEV_PEDESTAL) { pedestalUse(*d, g_inv); break; }
                if (d->type == DEV_CHEST) { openChest((int)(d - g_devices)); break; }
                if (d->type == DEV_PULSE_BUTTON) { d->poked = true; break; }
                /* Toggle: clicking the same machine again closes it. */
                setDevicePanel(g_devPanel == idx ? -1 : idx);
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
                setDevicePanel(-1);
                g_rmb = true;   /* right-drag digs, but only over the sim */
                startLine();
            }
        }
        SetCapture(hwnd);
        return 0;
    case WM_RBUTTONUP:
        commitLine();
        if (!(g_survival && g_playerOn)) undoFinish(LOCAL_PLAYER_ID, 0);
        g_rmb = false; ReleaseCapture(); return 0;

    case WM_CAPTURECHANGED:
        g_sizeDragging = false;
        return 0;

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
        if (!hotbarUp || keyHeld('Q')) {
            changeSize(dir);
        } else {
            /* Up scrolls left along the bar, matching the usual convention. */
            selectHotbar((g_hotbarSelected - dir + HOTBAR_SLOTS) % HOTBAR_SLOTS);
        }
        return 0;
    }

    case WM_KEYDOWN:
        /* Display controls remain available on every modal screen. F11 is
           intentionally handled before inventory/menu keyboard capture. */
        if (wp == VK_F11) {
            if ((lp & (1L << 30)) == 0) toggleFullscreen(hwnd);
            return 0;
        }
        if (wp == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if ((lp & (1L << 30)) == 0) {
                if (g_survival && g_playerOn) sendClientAction(NACT_UNDO);
                else undoApply(LOCAL_PLAYER_ID, 0);
            }
            return 0;
        }
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
            selectHotbar((int)(wp - '1'));
            break;
        case '0': selectHotbar(9); break;
        case 'M': g_brushMat = MAT_COPPER;   g_paletteDevice = -1; break;   /* M for metal */
        /* Wire mode and circuit-wire linking have no key. Both are modes that
           change what a click DOES rather than what it places, so hitting one
           by accident leaves the mouse doing something other than what the
           panel says -- and unlike a brush change, nothing about the cursor
           makes that obvious until you have already drawn with it. They keep
           their panel buttons, which is where a mode belongs. */
        case 'G': g_brushMat = MAT_GRAPHENE; g_paletteDevice = -1; break;
        case 'B': g_brushMat = MAT_WALL;     g_paletteDevice = -1; break;
        case 'E': g_brushMat = MAT_EMPTY;    g_paletteDevice = -1; break;
        case 'I': if (netRole() == NET_OFF) g_survival = !g_survival; break;
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
        case 'N':
            if (netRole() == NET_CLIENT) break;
            if (netConnected()) netStop();
            g_world.reset(); makeWorld(); break;

        /* F5 saves, F9 loads -- the pair every game has used for thirty years,
           and deliberately not on letters: the letters are all brush and tool
           shortcuts, and losing your world to a mistyped one is the failure
           this whole feature exists to prevent. */
        case VK_F5: {
            if (netRole() == NET_CLIENT) {
                sprintf(g_saveMsg, "The host owns this world's save");
                g_saveMsgFrames = 180; break;
            }
            /* The quick keys are slot 1 now rather than a file of their own.
               Two save systems side by side would be two places your world
               might be, which is exactly the confusion the slot screen exists
               to remove -- and F5 landing somewhere the Load screen cannot see
               would be the worst version of it. */
            saveToSlot(0);
            break;
        }
        case VK_F9: {
            if (netRole() == NET_CLIENT) {
                sprintf(g_saveMsg, "Only the host can load this world");
                g_saveMsgFrames = 180; break;
            }
            loadFromSlot(0);
            break;
        }
        /* R held is the line tool and a quick tap is the temporary teleport.
           Auto-repeat means this arrives many times while held, so the flag is
           set rather than toggled. */
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
        case 'P': if (netRole() != NET_CLIENT) g_paused = !g_paused; break;
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
        case VK_OEM_PERIOD: if (netRole() != NET_CLIENT) g_stepOnce = true; break;
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
            if (g_saveScreen != SAVESCREEN_OFF) g_saveScreen = SAVESCREEN_OFF;
            else if (g_mapOpen)      g_mapOpen = false;
            else if (g_chestOpen >= 0) closeChest();
            else if (g_devPanel >= 0) {
                if (netRole() == NET_CLIENT) {
                    g_closeDevicePending = g_devPanel;
                    g_playerSessions[0].openDevice = -1;
                }
                sendClientAction(NACT_CLOSE_DEVICE);
                g_devPanel = -1;
            }
            else if (g_craftOpen)    g_craftOpen = false;
            else if (g_creativeOpen) { g_creativeOpen = false; g_filterDevice = -1; g_digFilterPicking = false; g_signalPickerDevice = -1; g_signalPickerField = CIR_PICK_NONE; g_creSearchFocus = false; dragStow(); }
            else                     g_menuOpen = !g_menuOpen;
            break;
        }
        return 0;

    case WM_KEYUP:
        if (wp == 'R') {
            /* A completed line suppresses the tap verb. Survival sends the
               teleport through authority; character-off creative moves its
               observer directly. Dead players cannot use it to skip respawn. */
            if (!g_lineDrew && g_mx >= PANEL_W && !g_menuOpen && !g_creativeOpen) {
                if (g_survival && g_playerOn) {
                    if (g_player.alive) g_respawnPulse = true;
                } else
                    g_player.reset((float)((g_mx - PANEL_W) / cellPixels() + g_camX),
                                   (float)(g_my / cellPixels() + g_camY));
            }
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

static void syncClientDeviceUi() {
    if (!(g_survival && g_playerOn) || (netRole() == NET_CLIENT && !netClientReady())) return;
    int index = g_playerSessions[0].openDevice;
    if (netRole() == NET_CLIENT && g_closeDevicePending >= 0) {
        if (index == g_closeDevicePending) {
            /* A state packet sent before the close action was consumed must not
               reopen the panel. The pending marker clears as soon as the host
               echoes any other open-device state, normally on the next packet. */
            g_playerSessions[0].openDevice = -1; index = -1;
        } else {
            g_closeDevicePending = -1;
        }
    }
    if (index >= 0 && index < MAX_DEVICES && g_devices[index].used &&
        g_devices[index].type == DEV_CHEST) {
        if (g_chestOpen != index) openChest(index);
        const Device& d = g_devices[index];
        g_chestStack.item = d.count ? (ItemId)d.mat : ITEM_NONE;
        g_chestStack.count = d.count > 0 ? (u32)d.count : 0;
        g_chestStack.inst = 0;
        g_devPanel = -1;
    } else if (index >= 0 && index < MAX_DEVICES && g_devices[index].used) {
        g_devPanel = index;
        if (g_chestOpen >= 0) { g_chestOpen = -1; g_logisticsUiOpen = false; }
    } else {
        g_devPanel = -1;
        if (g_chestOpen >= 0) { g_chestOpen = -1; g_logisticsUiOpen = false; }
    }
}

/* The local client describes intent in world coordinates. The host remains
   responsible for reach-clamping, inventory, cooldowns and the resulting
   mutation; keeping raw cursor pixels out of the command also makes different
   window sizes and camera positions irrelevant to authority. */
static PlayerCommand localPlayerCommand() {
    static u32 sequence = 0;
    static u8 previousBits = 0;
    PlayerCommand c; memset(&c, 0, sizeof(c));
    c.sequence = ++sequence;
    c.player = netRole() == NET_CLIENT ? netAssignedPlayer() : LOCAL_PLAYER_ID;
    c.generation = g_playerSessions[0].generation;
    c.selected = (u8)imax(0, imin(INV_SLOTS - 1, g_inv.selected));
    c.brushRadius = (u8)g_brushRadius;
    c.brush = (i16)g_brushMat;
    c.background = g_bgLayer; c.overwrite = g_overwrite;
    c.line = g_lineOn;
    c.lineCommit = g_lineCommitPulse; c.lineCommitBits = g_lineCommitBits;
    c.lineStartX = g_lineCommitX; c.lineStartY = g_lineCommitY;
    c.digFilterOn = g_digFilterOn;
    for (int mat = 0; mat < MAT_COUNT; ++mat)
        if (g_digFilterMat[mat]) c.digFilter[mat >> 3] |= (u8)(1u << (mat & 7));
    if (!g_menuOpen && !g_creativeOpen && !g_craftOpen && g_chestOpen < 0 && !g_mapOpen) {
        if (keyHeld('A') || keyHeld(VK_LEFT)) c.bits |= PCMD_LEFT;
        if (keyHeld('D') || keyHeld(VK_RIGHT)) c.bits |= PCMD_RIGHT;
        if (keyHeld('W') || keyHeld(VK_UP) ||
            keyHeld(VK_SPACE)) c.bits |= PCMD_JUMP;
        if (keyHeld('S') || keyHeld(VK_DOWN)) c.bits |= PCMD_DOWN;
        if (g_lmb && !g_uiCapture) c.bits |= PCMD_USE_LEFT;
        if (g_rmb && !g_uiCapture) c.bits |= PCMD_USE_RIGHT;
        if (g_interactPulse) c.bits |= PCMD_INTERACT;
        if (g_respawnPulse) c.bits |= PCMD_RESPAWN;
    }
    c.pressed = (u8)(c.bits & ~previousBits);
    previousBits = c.bits;
    const Aim aim = currentAim(); c.aimX = aim.ghostX; c.aimY = aim.ghostY;
    return c;
}

static void predictClientPlayer(const PlayerCommand& command, bool predictUses = false) {
    PlayerSession& session = g_playerSessions[0];
    if (!session.connected) return;
    session.inventory.selected = imax(0, imin(INV_SLOTS - 1, (int)command.selected));
    if (session.body.alive && session.restBed < 0) {
        PlayerInput input;
        input.left = (command.bits & PCMD_LEFT) != 0;
        input.right = (command.bits & PCMD_RIGHT) != 0;
        input.jump = (command.bits & PCMD_JUMP) != 0;
        input.down = (command.bits & PCMD_DOWN) != 0;
        session.body.fly = flightSpec(session.inventory);
        session.body.speedMul = 1.0f + (float)session.inventory.speedBonus() / 100.0f;
        session.body.resist = session.inventory.tempResist();
        session.body.update(g_world, input);
    }
    if (predictUses) {
        /* Predict the visible result immediately, but let the host's inventory
           count remain authoritative. Replaying a placement against a cell the
           client already filled cannot infer how many items to consume, so
           speculative inventory is intentionally not retained here. */
        const bool worldEdit = (command.bits & (PCMD_USE_LEFT | PCMD_USE_RIGHT)) != 0 ||
                               (command.pressed & PCMD_INTERACT) != 0 || command.lineCommit;
        if (worldEdit) {
            int x0 = command.aimX, y0 = command.aimY;
            if (command.lineCommit) { x0 = command.lineStartX; y0 = command.lineStartY; }
            else if (command.line && session.lineActive) {
                x0 = session.previousAimX; y0 = session.previousAimY;
            }
            netMarkPredictedWorldEdit(x0, y0, command.aimX, command.aimY,
                                      imax(4, (int)command.brushRadius + 3),
                                      command.sequence);
        }
        const Inventory authoritativeInventory = session.inventory;
        applyPlayerUses(session, command);
        session.inventory = authoritativeInventory;
        session.inventory.selected = imax(0, imin(INV_SLOTS - 1, (int)command.selected));
    }
}

/* Commands sent after the host's acknowledged watermark are the small slice of
   time represented on this client but not in the authoritative snapshot. Keep
   them so a snapshot becomes a stable base to replay from, not a visible rewind. */
static const int PREDICTION_HISTORY = 256;
static PlayerCommand g_predictionHistory[PREDICTION_HISTORY];
static int g_predictionHead = 0, g_predictionCount = 0;
static u32 g_predictionStateSerial = 0;
static Player g_remoteVisual[MAX_PLAYERS];
static PlayerId g_remoteVisualId[MAX_PLAYERS];
static u16 g_remoteVisualGeneration[MAX_PLAYERS];
static bool g_remoteVisualValid[MAX_PLAYERS];

static void predictionClear() {
    g_predictionHead = g_predictionCount = 0;
    g_predictionStateSerial = netStateSerial();
}

static void predictionRemember(const PlayerCommand& command) {
    if (g_predictionCount == PREDICTION_HISTORY) {
        g_predictionHead = (g_predictionHead + 1) % PREDICTION_HISTORY;
        --g_predictionCount;
    }
    const int tail = (g_predictionHead + g_predictionCount) % PREDICTION_HISTORY;
    g_predictionHistory[tail] = command;
    ++g_predictionCount;
}

static bool sequenceAtOrBefore(u32 sequence, u32 acknowledged) {
    return (i32)(sequence - acknowledged) <= 0;
}

static void predictionReconcile() {
    const u32 serial = netStateSerial();
    if (serial == g_predictionStateSerial) return;
    g_predictionStateSerial = serial;
    const u32 acknowledged = netAcknowledgedCommand();
    while (g_predictionCount > 0 &&
           sequenceAtOrBefore(g_predictionHistory[g_predictionHead].sequence, acknowledged)) {
        g_predictionHead = (g_predictionHead + 1) % PREDICTION_HISTORY;
        --g_predictionCount;
    }
    const PlayerSession& local = g_playerSessions[0];
    for (int n = 0; n < g_predictionCount; ++n) {
        const PlayerCommand& command =
            g_predictionHistory[(g_predictionHead + n) % PREDICTION_HISTORY];
        if (command.player == local.networkId && command.generation == local.generation)
            predictClientPlayer(command);
    }
}

static void remoteVisualTick() {
    for (int slot = 1; slot < MAX_PLAYERS; ++slot) {
        const PlayerSession& session = g_playerSessions[slot];
        if (netRole() != NET_CLIENT || !session.connected) {
            g_remoteVisualValid[slot] = false;
            continue;
        }
        const Player& target = session.body;
        const bool sameLife = g_remoteVisualValid[slot] &&
            g_remoteVisualId[slot] == session.networkId &&
            g_remoteVisualGeneration[slot] == session.generation &&
            g_remoteVisual[slot].alive == target.alive;
        const float dx = sameLife ? target.x - g_remoteVisual[slot].x : 9999.0f;
        const float dy = sameLife ? target.y - g_remoteVisual[slot].y : 9999.0f;
        if (!sameLife || dx * dx + dy * dy > 64.0f * 64.0f) {
            g_remoteVisual[slot] = target;
        } else {
            const float x = g_remoteVisual[slot].x + dx * 0.55f;
            const float y = g_remoteVisual[slot].y + dy * 0.55f;
            g_remoteVisual[slot] = target;
            g_remoteVisual[slot].x = x; g_remoteVisual[slot].y = y;
        }
        g_remoteVisualId[slot] = session.networkId;
        g_remoteVisualGeneration[slot] = session.generation;
        g_remoteVisualValid[slot] = true;
    }
}

/* One held command per remote slot. Held input persists between packets, so a
   shared latest-command would apply whoever spoke last to every other player
   and leave the quiet ones motionless. */
static PlayerCommand g_remoteInput[MAX_PLAYERS];
static int g_remoteInputAge[MAX_PLAYERS] = { 1000, 1000, 1000, 1000 };
static PlayerCommand g_localInput;

static Aim commandAimFor(const PlayerSession& session, const PlayerCommand& command) {
    Aim a;
    a.ghostX = command.aimX; a.ghostY = command.aimY;
    a.x = a.ghostX; a.y = a.ghostY; a.clamped = false;
    const float pcx = session.body.centreX(), pcy = session.body.centreY();
    const float dx = (float)a.x - pcx, dy = (float)a.y - pcy;
    const float reach = (float)(PLAYER_REACH + session.inventory.reachBonus());
    const float d2 = dx * dx + dy * dy;
    if (d2 > reach * reach) {
        const float d = sqrtf(d2);
        a.x = (int)(pcx + dx * reach / d);
        a.y = (int)(pcy + dy * reach / d);
        a.clamped = true;
    }
    a.x = imax(PLAY_X0, imin(PLAY_X1, a.x));
    a.y = imax(PLAY_Y0, imin(PLAY_Y1, a.y));
    return a;
}

/* --- one gesture, both directions ------------------------------------------
   Interact with a full pedestal and you take what is on it; interact with an
   empty one and you put down what you are holding.

   No panel either way. A pedestal holds exactly one thing and there is nothing
   to arrange, so a chest's two-grid screen would be a window you open in order
   to close it -- and the whole appeal of the object is that its contents are
   already visible from across the room.

   Both directions, rather than only taking, because otherwise the craftable
   pedestal is furniture that can never do anything: worldgen would be the only
   thing in the game able to put an item on one, and a recipe whose product is
   permanently empty is a recipe that reads as broken. It also turns out to be
   the more interesting half -- a lit plinth is how you show somebody your first
   Forge Core, and in a world with four players that is a use worth having. */
static void pedestalUse(Device& d, Inventory& inv) {
    const ItemId held = pedestalItem(d);

    if (held == ITEM_NONE) {
        /* Putting down. ONE unit off the held stack, never the whole thing: a
           pedestal is a display, and a display that swallowed a stack of 100000
           stone would be a hole in the pack rather than an ornament. */
        ItemStack& hand = inv.held();
        if (hand.empty()) return;
        const ItemId what = hand.item;
        /* A tool carries an instance handle (see ToolInst) and a pedestal has
           nowhere to keep one, so standing a loaded multitool on a plinth would
           either duplicate its loadout or lose it. Refused outright, with a
           reason, rather than silently accepted and quietly stripped. */
        if (hand.inst != 0) {
            sprintf(g_saveMsg, "A %s will not sit on a pedestal", ITEMS[what].name);
            g_saveMsgFrames = 150;
            return;
        }
        if (inv.take(what, 1) != 1) return;
        pedestalSet(d, what, 1);
        sprintf(g_saveMsg, "Placed the %s", ITEMS[what].name);
        g_saveMsgFrames = 150;
        return;
    }

    /* Taking. Refuses when the pack is full rather than taking what fits and
       dropping the rest, and leaves the pedestal lit. A reward you can see is a
       reward you can come back for; a reward that fell on the floor beside a
       full pack is one you walk away from without knowing it existed. */
    const int count = pedestalCount(d);
    const int left  = inv.add(held, count);
    if (left == count) {
        sprintf(g_saveMsg, "No room for the %s", ITEMS[held].name);
        g_saveMsgFrames = 150;
        return;
    }
    pedestalSet(d, left > 0 ? held : ITEM_NONE, left);
    sprintf(g_saveMsg, "Took the %s", ITEMS[held].name);
    g_saveMsgFrames = 150;
}

static void interactFor(PlayerSession& session, const Aim& aim) {
    Device* d = devAt(aim.x, aim.y);
    if (d) {
        const int idx = (int)(d - g_devices);
        if (d->type == DEV_BED) {
            const float px = session.body.centreX(), py = session.body.centreY();
            const bool atBed = px >= d->x - 4 && px <= d->x + DEV_W + 4 &&
                               py >= d->y - 8 && py <= d->y + DEV_H + 8;
            if (session.restBed == idx) session.restBed = -1;
            else if (atBed) {
                session.restBed = idx;
                session.respawnBedX = d->x; session.respawnBedY = d->y;
            }
        } else if (d->type == DEV_PULSE_BUTTON) {
            d->poked = true;
        } else if (d->type == DEV_PEDESTAL) {
            pedestalUse(*d, session.inventory);
        } else if (d->type == DEV_CHEST) {
            session.openDevice = session.openDevice == idx ? -1 : idx;
        } else {
            session.openDevice = session.openDevice == idx ? -1 : idx;
        }
        return;
    }
    if (doorToggle(g_world, aim.x, aim.y)) {
        roomsNotifyEdit(g_world, aim.x, aim.y);
        /* A painted door may span several chunks. The ordinary hash scan will
           eventually find all of them, but interaction should arrive as one
           visible action on joined clients rather than a patchwork over the
           next few scans. */
        netMarkWorldEdit(aim.x, aim.y, DOOR_REACH);
    }
}

/* ===========================================================================
   Melee
   ===========================================================================

   A stroke is a shape that moves over a span of frames, so it is split in two:
   `meleeStart` commits to one when the button allows it, and `meleeTickFor`
   advances it and works out what it cut through. They are separate because
   they run at different times -- starting is an input decision and lives in
   applyPlayerUses with the other verbs, while advancing happens every frame
   whether or not a command arrived, beside the drone and accessory ticks.

   Folding both into the input path was the obvious first shape and it is wrong
   in a way that would have taken a while to see: applyPlayerUses returns early
   in four places and calls itself once (see the line-tool commit), so a stroke
   ticked there would sometimes advance twice in a frame and sometimes not at
   all -- a sword that hit harder while you were drawing a line. */

/* Where the blade is right now, as a segment from hilt to tip. `phase` runs
   0..1 across the stroke.

   Both styles are expressed as the same two points because everything
   downstream -- the hit test, the drawing -- only wants to know where the metal
   is. What differs is how the two ends MOVE. */
static void meleeBlade(const Player& body, const ItemDef& def,
                       float reach,
                       float dirX, float dirY, float phase,
                       float* hx, float* hy, float* tx, float* ty) {
    const float cx = body.centreX(), cy = body.centreY();

    if (def.meleeStyle == MELEE_SWING) {
        /* An arc. The blade starts `arc/2` degrees off the aim on one side and
           finishes the same distance off it on the other, so what you pointed
           at is the MIDDLE of the stroke rather than its start -- which is what
           makes aiming at a creature connect, instead of aiming a half-arc
           ahead of it the way it does if the sweep begins where you clicked.

           Which way round it sweeps follows the aim: pointing right swings from
           high to low, pointing left mirrors it. Always sweeping the same
           direction in world space would make a left-handed swing look like the
           character was winding up backwards. */
        const float half = (float)def.meleeArc * 0.5f * 3.14159265f / 180.0f;
        const float base = atan2f(dirY, dirX);
        const float sweep = (dirX < 0.0f) ? -1.0f : 1.0f;
        const float angle = base - sweep * half + sweep * (2.0f * half) * phase;
        const float ca = cosf(angle), sa = sinf(angle);
        /* The hilt sits a little out from the body, so the sword reads as held
           rather than growing out of the character's middle -- the same two
           cells drawHeldTool offsets by, and for the same reason. */
        *hx = cx + ca * 2.0f; *hy = cy + sa * 2.0f;
        *tx = cx + ca * reach;
        *ty = cy + sa * reach;
        return;
    }

    /* A stab. Out and back over the stroke, so the tip is furthest at the
       halfway point. sinf gives that for free and eases both ends of it, which
       is what stops the thrust looking like the spear teleporting to full
       extension and back. */
    const float out = sinf(phase * 3.14159265f);
    const float lead = 2.0f + (reach - 2.0f) * out;
    *hx = cx + dirX * (lead - reach * 0.55f);
    *hy = cy + dirY * (lead - reach * 0.55f);
    *tx = cx + dirX * lead;
    *ty = cy + dirY * lead;
}

static float meleeReachFor(const Inventory& inv, const ItemDef& def) {
    return (float)def.meleeReach * (1.0f + (float)inv.meleeReachPct() / 100.0f);
}

static int meleeFramesFor(const Inventory& inv, const ItemDef& def) {
    const int pct = inv.meleeSpeedPct();
    return imax(1, (int)def.meleeFrames - (int)def.meleeFrames * pct / 100);
}

static int meleeCooldownFor(const Inventory& inv, const ItemDef& def) {
    const int pct = inv.meleeSpeedPct();
    return imax(1, (int)def.meleeCooldown - (int)def.meleeCooldown * pct / 100);
}

static int meleeDamageFor(const Inventory& inv, int base) {
    const int accessory = accessoryShotDamage(inv, base);
    const int pct = inv.meleeDamagePct();
    return pct > 0 ? imax(accessory + 1, accessory + accessory * pct / 100)
                   : accessory;
}

/* Begin a stroke, if the weapon is ready. Returns false when it is still on
   cooldown, which is every frame but one in six for a copper sword -- so this
   is called freely while the button is held and the rhythm comes from the
   weapon rather than from how fast somebody can click.

   Auto-repeat rather than one swing per press, deliberately. Every other verb
   in this game repeats while held -- digging, building, sowing, firing -- and a
   melee weapon that alone demanded a click per swing would read as a bug in the
   input handling long before it read as a design. */
static bool meleeStart(PlayerSession& session, const ItemDef& def, const Aim& aim) {
    if (session.swingCool > 0 || session.swingFrame > 0) return false;

    const float cx = session.body.centreX(), cy = session.body.centreY();
    float dx = (float)aim.x - cx, dy = (float)aim.y - cy;
    const float d = sqrtf(dx * dx + dy * dy);
    if (d > 0.001f) { dx /= d; dy /= d; }
    else {
        /* Aimed at your own feet. Fall back to the way the character is facing
           rather than refusing the swing: a weapon that silently does nothing
           when the cursor drifts onto the body is a weapon that feels broken at
           exactly the range it is meant to be used at. */
        dx = (float)(session.body.facing >= 0 ? 1 : -1); dy = 0.0f;
    }
    session.swingDirX = dx; session.swingDirY = dy;
    session.swingFrame = meleeFramesFor(session.inventory, def);
    session.swingCool  = imax(meleeCooldownFor(session.inventory, def),
                              session.swingFrame);
    memset(session.swingHit, 0, sizeof(session.swingHit));
    return true;
}

/* One frame of whatever stroke is in progress. Called for every connected
   player once a frame, beside the accessory and drone ticks. */
static void meleeTickFor(PlayerSession& session) {
    if (session.swingCool > 0) --session.swingCool;
    if (session.swingFrame <= 0) return;

    const ItemStack& held = session.inventory.held();
    /* Swapping the weapon out mid-stroke abandons it rather than finishing with
       whatever is now in hand. The alternative is a copper sword's stroke
       landing tungsten damage because the hotbar changed on frame nine. */
    if (held.empty() || ITEMS[held.item].kind != ITEMK_MELEE) {
        session.swingFrame = 0;
        return;
    }
    const ItemDef& def = ITEMS[held.item];

    const int total = meleeFramesFor(session.inventory, def);
    /* swingFrame counts down, so progress is what is left subtracted from the
       whole. Sampled at the END of this frame's motion rather than the start,
       so the first thing that happens after a click is the blade being
       somewhere other than at rest. */
    const float phase = (float)(total - session.swingFrame) / (float)total;

    float hx, hy, tx, ty;
    meleeBlade(session.body, def, meleeReachFor(session.inventory, def),
               session.swingDirX, session.swingDirY,
               phase, &hx, &hy, &tx, &ty);

    /* Damage is the AUTHORITY's to resolve. A client runs the animation so its
       own swing does not lag a round trip behind the button, but a client that
       also took health off its local replica would show creatures dying and
       then coming back when the next snapshot disagreed. */
    if (netRole() != NET_CLIENT)
        entHitSegment(hx, hy, tx, ty, session.body.centreX(), session.body.centreY(),
                      meleeDamageFor(session.inventory, ITEMS[held.item].damage),
                      def.meleeKnock, session.swingHit);

    --session.swingFrame;
}

static void applyPlayerUses(PlayerSession& session, const PlayerCommand& command) {
    const int undoSlot = (int)(&session - g_playerSessions);
    const bool recordUndo = netRole() != NET_CLIENT;
    const u8 bits = command.bits;
    const u8 pressed = (u8)(command.pressed | (bits & ~session.previousCommandBits));
    const bool left = (bits & PCMD_USE_LEFT) != 0;
    bool right = (bits & PCMD_USE_RIGHT) != 0;
    const Aim aim = commandAimFor(session, command);
    if (left || right || (pressed & PCMD_INTERACT) || command.lineCommit)
        netMarkWorldEdit(aim.x, aim.y, imax(4, (int)command.brushRadius + 3));

    /* Mouse down and up can both arrive between two 60 Hz samples. Carrying a
       discrete commit with its anchor means a quick line is still a line,
       rather than disappearing because no sampled frame observed line=true. */
    if (command.lineCommit && !session.lineActive && command.lineCommitBits) {
        PlayerCommand start = command;
        start.aimX = command.lineStartX; start.aimY = command.lineStartY;
        const Aim startAim = commandAimFor(session, start);
        session.lineActive = true;
        session.lineBits = (u8)(command.lineCommitBits & (PCMD_USE_LEFT | PCMD_USE_RIGHT));
        session.lineSelected = command.selected; session.lineRadius = command.brushRadius;
        session.lineBrush = command.brush;
        session.lineBackground = command.background; session.lineOverwrite = command.overwrite;
        session.lineFilterOn = command.digFilterOn;
        memcpy(session.lineFilter, command.digFilter, sizeof(session.lineFilter));
        session.previousAimX = startAim.x; session.previousAimY = startAim.y;
    }

    if (command.line && (left || right)) {
        if (!session.lineActive) {
            session.lineActive = true;
            session.lineBits = (u8)(bits & (PCMD_USE_LEFT | PCMD_USE_RIGHT));
            session.lineSelected = command.selected;
            session.lineRadius = command.brushRadius;
            session.lineBrush = command.brush;
            session.lineBackground = command.background;
            session.lineOverwrite = command.overwrite;
            session.lineFilterOn = command.digFilterOn;
            memcpy(session.lineFilter, command.digFilter, sizeof(session.lineFilter));
            session.previousAimX = aim.x; session.previousAimY = aim.y;
        }
        if (session.digCooldown > 0) --session.digCooldown;
        session.previousCommandBits = 0;
        return;
    }
    if (session.lineActive) {
        PlayerCommand commit = command;
        commit.bits = session.lineBits; commit.pressed = session.lineBits;
        commit.selected = session.lineSelected; commit.brushRadius = session.lineRadius;
        commit.brush = session.lineBrush;
        commit.background = session.lineBackground; commit.overwrite = session.lineOverwrite;
        commit.digFilterOn = session.lineFilterOn;
        memcpy(commit.digFilter, session.lineFilter, sizeof(commit.digFilter));
        commit.line = false; commit.lineCommit = false;
        session.lineActive = false; session.previousCommandBits = 0;
        session.inventory.selected = imax(0, imin(INV_SLOTS - 1, (int)commit.selected));
        applyPlayerUses(session, commit);
        return;
    }
    if (session.digCooldown > 0) --session.digCooldown;

    const bool interactiveTarget = devAt(aim.x, aim.y) ||
                                   isDoor(g_world.at(aim.x, aim.y).mat);
    if (pressed & PCMD_INTERACT) {
        interactFor(session, aim);
        /* A stale held-right bit can accompany the discrete interaction after
           capture loss or packet coalescing. It is still one click: never
           toggle the door a second time or fall through into right-use. */
        if (interactiveTarget && (bits & PCMD_USE_RIGHT)) session.suppressRightUse = true;
    } else if ((pressed & PCMD_USE_RIGHT) && interactiveTarget) {
        interactFor(session, aim);
        session.suppressRightUse = true;
    }
    if (!(bits & PCMD_USE_RIGHT)) session.suppressRightUse = false;
    if (session.suppressRightUse) right = false;
    if ((pressed & PCMD_RESPAWN) && session.body.alive) {
        const float x = (float)imax(PLAY_X0, imin(PLAY_X1, command.aimX));
        const float y = (float)imax(PLAY_Y0, imin(PLAY_Y1, command.aimY));
        session.body.reset(x, y);
        session.restBed = -1;
    }
    if (!left && !right) {
        if (recordUndo) undoFinish(undoSlot, &session.inventory);
        session.previousAimX = session.previousAimY = -1;
        session.previousCommandBits = bits;
        return;
    }
    if (session.previousAimX < 0) {
        session.previousAimX = aim.x; session.previousAimY = aim.y;
    }

    Inventory& inv = session.inventory;
    Player& player = session.body;
    const int radius = imax(1, imin(BRUSH_RADIUS_MAX, (int)command.brushRadius));
    ItemStack& held = inv.held();
    bool digFilter[MAT_COUNT];
    for (int mat = 0; mat < MAT_COUNT; ++mat)
        digFilter[mat] = (command.digFilter[mat >> 3] & (1u << (mat & 7))) != 0;

    if (left && !right && (command.brush == TOOL_HEAT || command.brush == TOOL_COOL)) {
        g_world.heat(aim.x, aim.y, radius, command.brush == TOOL_HEAT ? HEAT_STEP : -HEAT_STEP);
    } else if (left && !right && !held.empty() && ITEMS[held.item].kind == ITEMK_THROWABLE) {
        if (pressed & PCMD_USE_LEFT) {
            const ItemId item = held.item;
            if (throwGlowflareFor(player, inv, aim)) inv.take(item, 1);
        }
    } else if (left && !right && !held.empty() && ITEMS[held.item].kind == ITEMK_TOOL) {
        fireToolFor(player, inv, aim);
    } else if (left && !right && !held.empty() && ITEMS[held.item].kind == ITEMK_MELEE) {
        /* Only STARTS the stroke -- see the note above meleeStart for why the
           advancing half is somewhere else. Deliberately before the background
           check that every branch below carries: a sword swings the same
           whether or not you happen to have the wall layer selected, because it
           is not editing cells either way. */
        meleeStart(session, ITEMS[held.item], aim);
    } else if (left && !right && !command.background && !held.empty() &&
               ITEMS[held.item].kind == ITEMK_DEVICE) {
        placeDeviceStrokeFor(inv, session.previousAimX, session.previousAimY,
                             ITEMS[held.item].deviceType, true, aim,
                             recordUndo ? undoSlot : -1);
    } else if (left && !right && !command.background && !held.empty() &&
               ITEMS[held.item].kind == ITEMK_SEED) {
        if (recordUndo) {
            undoBegin(undoSlot, &inv);
            undoCaptureDisc(undoSlot, aim.x, aim.y, radius);
        }
        sowSeeds(g_world, inv, aim.x, aim.y, radius);
    } else if (left && !right && !command.background && !held.empty() &&
               ITEMS[held.item].kind == ITEMK_FOOD) {
        if (pressed & PCMD_USE_LEFT) playerConsumeHealing(session, held.item);
    } else if (left && !right && !command.background && !held.empty() &&
               ITEMS[held.item].kind == ITEMK_IGNITE) {
        g_world.ignite(aim.x, aim.y, IGNITE_RADIUS);
    } else if (left && !right && !command.background && !held.empty() &&
               ITEMS[held.item].kind == ITEMK_EGG) {
        if (pressed & PCMD_USE_LEFT) {
            const int type = ITEMS[held.item].summons;
            const ItemId item = held.item;
            if (type && entSpawn(g_world, type, (float)aim.x, (float)aim.y) >= 0)
                inv.take(item, 1);
        }
    } else if (command.background) {
        const ToolSpec tool = miningSpec(inv);
        if (recordUndo) {
            undoBegin(undoSlot, &inv);
            undoCaptureDisc(undoSlot, aim.x, aim.y,
                            right ? imin(radius, tool.maxRadius) : radius);
        }
        if (right) {
            if (session.digCooldown <= 0) {
                digBg(g_world, inv, aim.x, aim.y, imin(radius, tool.maxRadius), tool.cellsPerBite);
                session.digCooldown = tool.cooldown;
            }
        } else {
            placeBg(g_world, inv, aim.x, aim.y, radius);
        }
        roomsNotifyEdit(g_world, aim.x, aim.y);
    } else if (right) {
        const ToolSpec tool = miningSpec(inv);
        if (recordUndo) {
            undoBegin(undoSlot, &inv);
            undoCaptureDisc(undoSlot, aim.x, aim.y, imin(radius, tool.maxRadius));
        }
        if (session.digCooldown <= 0) {
            digInto(g_world, inv, aim.x, aim.y, imin(radius, tool.maxRadius), tool.cellsPerBite,
                    tool.plantsOnly, tool.power, command.digFilterOn ? digFilter : 0);
            session.digCooldown = tool.cooldown;
        }
        roomsNotifyEdit(g_world, aim.x, aim.y);
    } else if (left && !held.empty() && ITEMS[held.item].kind == ITEMK_MATERIAL) {
        if (recordUndo) undoBegin(undoSlot, &inv);
        if (command.overwrite) {
            if (recordUndo) undoCaptureDisc(undoSlot, aim.x, aim.y, radius);
            const ToolSpec tool = miningSpec(inv);
            if (session.digCooldown <= 0) {
                const int replaced = overwriteFrom(g_world, inv, aim.x, aim.y,
                    imin(radius, tool.maxRadius), tool.cellsPerBite, tool.power);
                if (replaced > 0) session.digCooldown = tool.cooldown;
            }
        }
        if (command.overwrite) {
            placeFrom(g_world, inv, aim.x, aim.y, radius);
        } else {
            const int x0 = session.previousAimX, y0 = session.previousAimY;
            const int steps = imax(abs(aim.x - x0), abs(aim.y - y0));
            for (int step = 0; step <= steps && !inv.held().empty(); ++step) {
                const int x = steps ? x0 + (aim.x - x0) * step / steps : aim.x;
                const int y = steps ? y0 + (aim.y - y0) * step / steps : aim.y;
                if (recordUndo) undoCaptureDisc(undoSlot, x, y, radius);
                placeFrom(g_world, inv, x, y, radius);
            }
        }
        roomsNotifyEdit(g_world, aim.x, aim.y);
    }
    session.previousAimX = aim.x; session.previousAimY = aim.y;
    session.previousCommandBits = bits;
}

static bool slotClickFor(ItemStack& cursor, ItemStack& slot, bool right) {
    if (cursor.empty()) {
        if (slot.empty()) return false;
        if (right && slot.count > 1) {
            const u32 half = (slot.count + 1) / 2;
            cursor = slot; cursor.count = half; cursor.inst = 0;
            slot.count -= half;
            if (!slot.count) slot = ItemStack();
        } else { cursor = slot; slot = ItemStack(); }
        return true;
    }
    if (slot.empty()) {
        if (right && cursor.count > 1) {
            slot.item = cursor.item; slot.count = 1; slot.inst = 0; --cursor.count;
        } else { slot = cursor; cursor = ItemStack(); }
        return true;
    }
    if (slot.item == cursor.item && !slot.inst && !cursor.inst) {
        const u32 cap = ITEMS[slot.item].maxStack;
        const u32 room = cap > slot.count ? cap - slot.count : 0;
        if (!room) return false;
        const u32 move = right ? 1u : imin((int)cursor.count, (int)room);
        slot.count += move; cursor.count -= move;
        if (!cursor.count) cursor = ItemStack();
        return true;
    }
    if (right) return false;
    const ItemStack swap = slot; slot = cursor; cursor = swap; return true;
}

static void applySlotAction(PlayerSession& session, const NetAction& action) {
    Inventory& inv = session.inventory;
    ItemStack& cursor = session.cursor;
    const bool right = (action.flags & 1) != 0;
    switch (action.container) {
    case NSLOT_PACK:
        if (action.a < INV_SLOTS) slotClickFor(cursor, inv.slot[action.a], right);
        break;
    case NSLOT_EQUIP:
        if (action.a < EQ_COUNT && !right) {
            ItemStack& eq = inv.equip[action.a];
            const int bay = action.a == EQ_LIGHT_DRONE ? 0 : action.a == EQ_DRONE_A ? 1 :
                            action.a == EQ_DRONE_B ? 2 : action.a == EQ_DRONE_C ? 3 : -1;
            if (bay >= 0 && !eq.empty()) {
                for (int i = 0; i < Inventory::DRONE_MODULE_SLOTS_MAX; ++i)
                    if (!inv.droneModule[bay][i].empty()) return;
            }
            if (!cursor.empty() &&
                (!equipFits(cursor.item, action.a) || !inv.droneBayUnlocked(action.a))) return;
            slotClickFor(cursor, eq, false);
        }
        break;
    case NSLOT_DRONE_MODULE:
        if (action.a < MAX_DRONES && action.b < Inventory::DRONE_MODULE_SLOTS_MAX && !right) {
            if (!cursor.empty() && (ITEMS[cursor.item].kind != ITEMK_DRONE_MODULE || cursor.count != 1)) return;
            const int eq = action.a == 0 ? EQ_LIGHT_DRONE : action.a == 1 ? EQ_DRONE_A :
                           action.a == 2 ? EQ_DRONE_B : EQ_DRONE_C;
            if (!cursor.empty() && !inv.droneBayUnlocked(eq)) return;
            slotClickFor(cursor, inv.droneModule[action.a][action.b], false);
        }
        break;
    case NSLOT_TOOL_MODULE:
        if (action.a < INV_SLOTS && action.b < TOOL_SLOTS_MAX && !right) {
            ItemStack& tool = inv.slot[action.a];
            if (!tool.inst || !g_toolInst[tool.inst].used ||
                action.b >= imin((int)ITEMS[tool.item].toolSlots, TOOL_SLOTS_MAX)) return;
            ItemId& module = g_toolInst[tool.inst].slot[action.b];
            if (cursor.empty()) {
                if (module == ITEM_NONE) return;
                cursor.item = module; cursor.count = 1; cursor.inst = 0; module = ITEM_NONE;
            } else if (ITEMS[cursor.item].kind == ITEMK_MODULE && cursor.count == 1) {
                const ItemId old = module; module = cursor.item;
                if (old == ITEM_NONE) cursor = ItemStack(); else cursor.item = old;
            }
        }
        break;
    case NSLOT_TOOL_PAYLOAD:
        if (action.a < INV_SLOTS) {
            ItemStack& tool = inv.slot[action.a];
            if (!tool.inst || !g_toolInst[tool.inst].used) return;
            if (!cursor.empty() && ITEMS[cursor.item].kind != ITEMK_MATERIAL) return;
            slotClickFor(cursor, g_toolInst[tool.inst].payload, right);
        }
        break;
    case NSLOT_TRASH:
        if (cursor.empty()) {
            if (!session.trash.empty()) { cursor = session.trash; session.trash = ItemStack(); }
        } else {
            if (session.trash.inst) toolInstFree(session.trash.inst);
            session.trash = cursor; cursor = ItemStack();
        }
        break;
    case NSLOT_CHEST:
        if (session.openDevice >= 0 && session.openDevice < MAX_DEVICES) {
            Device& d = g_devices[session.openDevice];
            if (!d.used || d.type != DEV_CHEST) { session.openDevice = -1; return; }
            ItemStack chest;
            chest.item = d.count ? (ItemId)d.mat : ITEM_NONE;
            chest.count = d.count > 0 ? (u32)d.count : 0; chest.inst = 0;
            slotClickFor(cursor, chest, right);
            d.mat = chest.empty() ? (u8)MAT_EMPTY : (u8)chest.item;
            d.count = (i32)chest.count;
        }
        break;
    default: break;
    }
}

static void applyDeviceAction(PlayerSession& session, const NetAction& action) {
    const int index = session.openDevice;
    if (index < 0 || index >= MAX_DEVICES || !g_devices[index].used) {
        session.openDevice = -1; return;
    }
    Device& d = g_devices[index];
    const DeviceInfo& info = DEVS[d.type];
    CircuitDeviceConfig& cc = g_circuitConfig[index];
    switch (action.a) {
    case NDEV_DEC:
        d.value -= d.type == DEV_CONSTANT_COMBINATOR ? 1 : info.vStep;
        break;
    case NDEV_INC:
        d.value += d.type == DEV_CONSTANT_COMBINATOR ? 1 : info.vStep;
        break;
    case NDEV_TURN:
        if (d.type == DEV_ARITHMETIC_COMBINATOR || d.type == DEV_DECIDER_COMBINATOR) {
            const int first = d.type == DEV_DECIDER_COMBINATOR ? CIR_OP_GREATER : CIR_OP_ADD;
            const int last = d.type == DEV_DECIDER_COMBINATOR ? CIR_OP_NOT_EQUAL : CIR_OP_MODULO;
            cc.op = (u8)(cc.op < first || cc.op >= last ? first : cc.op + 1);
        } else if (info.aimable) d.face = (u8)((d.face + 1) & 3);
        break;
    case NDEV_TAKE: {
        ItemStack& held = session.inventory.held();
        if ((d.type == DEV_CHEST || d.type == DEV_SPOUT) && !held.empty() &&
            ITEMS[held.item].kind == ITEMK_MATERIAL && (d.count == 0 || d.mat == held.item)) {
            const int cap = d.type == DEV_CHEST ? CHEST_CAP : DEV_CAP;
            const int moved = imin((int)held.count, cap - (int)d.count);
            d.mat = (u8)held.item; d.count += moved; held.count -= moved;
            if (!held.count) held = ItemStack();
        } else if (d.count > 0) {
            const int moved = session.inventory.add((ItemId)d.mat, (int)d.count);
            d.count -= moved;
            if (d.count <= 0) { d.count = 0; d.mat = MAT_EMPTY; }
        }
        break;
    }
    case NDEV_SET_FILTER:
        /* A drain keeps its filter in `value`, which it can afford because it
           has no rate to store. A miner's `value` IS its rate, so its filter
           lives in the field devFilterMat owns. One action, because from the
           player's side it is the same gesture. */
        if (action.b < MAT_COUNT) {
            if (devHasBox(d.type)) devSetFilterMat(d, action.b);
            else                   d.value = action.b;
        }
        break;
    case NDEV_DEPTH_DEC: devSetBoxDepth(d, devBoxDepth(d) - 1); break;
    case NDEV_DEPTH_INC: devSetBoxDepth(d, devBoxDepth(d) + 1); break;
    case NDEV_MODE:      devSetRunMode(d, devRunMode(d) + 1);   break;
    case NDEV_SET_SIGNAL: cc.signal = action.b; break;
    case NDEV_SET_A: cc.signalA = action.b; break;
    case NDEV_SET_B: cc.signalB = action.b; break;
    case NDEV_SET_OUT: cc.signalOut = action.b; break;
    default: return;
    }
    if (d.value < info.vMin) d.value = info.vMin;
    if (d.value > info.vMax) d.value = info.vMax;
    d.latched = false;
}

static Aim playerWireAim(const PlayerSession& session, int rawX, int rawY) {
    PlayerCommand command; memset(&command, 0, sizeof(command));
    command.aimX = rawX; command.aimY = rawY;
    Aim aim = commandAimFor(session, command);
    const int baseX = aim.x, baseY = aim.y;
    int bestX = baseX, bestY = baseY;
    const int snap = 4; int bestD2 = snap * snap + 1;
    for (int y = imax(PLAY_Y0, baseY - snap); y <= imin(PLAY_Y1, baseY + snap); ++y)
        for (int x = imax(PLAY_X0, baseX - snap); x <= imin(PLAY_X1, baseX + snap); ++x) {
            const int dx = x - baseX, dy = y - baseY, d2 = dx * dx + dy * dy;
            if (d2 >= bestD2 || !g_matConducts[g_world.at(x, y).mat]) continue;
            bestD2 = d2; bestX = x; bestY = y;
        }
    aim.x = bestX; aim.y = bestY;
    return aim;
}

static void playerWireCell(int slot, PlayerSession& session, int x, int y) {
    if (x < PLAY_X0 || x > PLAY_X1 || y < PLAY_Y0 || y > PLAY_Y1) return;
    const u8 old = g_world.at(x, y).mat;
    if (old == MAT_COPPER || old != MAT_EMPTY) return;
    if (!session.inventory.take(MAT_COPPER, 1)) return;
    undoCaptureCell(slot, x, y);
    g_world.setCell(x, y, MAT_COPPER);
}

static void applyPlayerWirePoint(PlayerSession& session, const NetAction& action) {
    const int slot = (int)(&session - g_playerSessions);
    const Aim aim = playerWireAim(session, action.x, action.y);
    if (!(action.flags & 1)) { session.wireX = aim.x; session.wireY = aim.y; return; }
    if (session.wireX < 0) return;
    if (netRole() != NET_CLIENT) undoBegin(slot, &session.inventory);
    int x = session.wireX, y = session.wireY;
    const int dx = abs(aim.x - x), sx = x < aim.x ? 1 : -1;
    const int dy = abs(aim.y - y), sy = y < aim.y ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        if (netRole() != NET_CLIENT) playerWireCell(slot, session, x, y);
        else playerWireCell(-1, session, x, y);
        if (x == aim.x && y == aim.y) break;
        const int twice = err * 2; const bool moveX = twice > -dy; const bool moveY = twice < dx;
        int nx = x, ny = y;
        if (moveX) { err -= dy; nx += sx; }
        if (moveY) { err += dx; ny += sy; }
        if (moveX && moveY) {
            if (netRole() != NET_CLIENT) playerWireCell(slot, session, nx, y);
            else playerWireCell(-1, session, nx, y);
        }
        x = nx; y = ny;
    }
    session.wireX = session.wireY = -1;
    roomsNotifyEdit(g_world, aim.x, aim.y);
    if (netRole() != NET_CLIENT) undoFinish(slot, &session.inventory);
}

static void applyPlayerAction(const NetAction& action) {
    const int slot = playerSessionSlotForNetworkId(action.player);
    if (slot < 0 || slot >= MAX_PLAYERS) return;
    PlayerSession& session = g_playerSessions[slot];
    if (!session.connected || session.generation != action.generation) return;
    if (action.type == NACT_UNDO) undoApply(slot, &session.inventory);
    else if (action.type == NACT_SLOT) applySlotAction(session, action);
    else if (action.type == NACT_CRAFT && action.a < N_RECIPES) {
        craftScanStations(g_world, session.body);
        const int count = imax(1, imin(50, (int)action.b));
        for (int i = 0; i < count; ++i) if (!craftMake(session.inventory, action.a)) break;
    } else if (action.type == NACT_CLOSE_DEVICE) {
        session.openDevice = -1;
    } else if (action.type == NACT_DEVICE) {
        applyDeviceAction(session, action);
    } else if (action.type == NACT_WIRE_POINT) {
        applyPlayerWirePoint(session, action);
    } else if (action.type == NACT_CIRCUIT_TERMINAL && action.a < MAX_DEVICES &&
               g_devices[action.a].used) {
        const Device& terminal = g_devices[action.a];
        const float dx = terminal.x + DEV_W * 0.5f - session.body.centreX();
        const float dy = terminal.y + DEV_H * 0.5f - session.body.centreY();
        const float reach = (float)(PLAYER_REACH + session.inventory.reachBonus() + DEV_W);
        if (dx * dx + dy * dy > reach * reach) return;
        if (session.circuitWireFrom < 0) {
            session.circuitWireFrom = action.a; session.circuitWirePort = action.b & 1;
        } else {
            circuitToggleWirePorts(session.circuitWireFrom, session.circuitWirePort,
                                   action.a, action.b & 1);
            session.circuitWireFrom = -1; session.circuitWirePort = 0;
        }
    } else if (action.type == NACT_STOW_CURSOR) {
        for (int i = 0; i < INV_SLOTS && !session.cursor.empty(); ++i) {
            ItemStack& stack = session.inventory.slot[i];
            if (stack.empty() || (stack.item == session.cursor.item && !stack.inst && !session.cursor.inst))
                slotClickFor(session.cursor, stack, false);
        }
    } else if (action.type == NACT_CREATIVE_ITEM && action.a < ITEM_COUNT) {
        const ItemId item = (ItemId)action.a;
        if (action.flags & 1) {
            session.inventory.take(item, 100000);
        } else if (session.cursor.item == item && !session.cursor.inst) {
            session.cursor.count = ITEMS[item].maxStack;
        } else {
            if (session.cursor.inst) toolInstFree(session.cursor.inst);
            session.cursor.item = item;
            session.cursor.count = ITEMS[item].maxStack;
            session.cursor.inst = ITEMS[item].kind == ITEMK_TOOL ? toolInstNew(item) : 0;
        }
    }
}

static void processPlayerActions() {
    NetAction action;
    int budget = 128;
    while (budget > 0 && popLocalAction(&action)) { applyPlayerAction(action); --budget; }
    while (budget > 0 && netPopRemoteAction(&action)) {
        applyPlayerAction(action); netMarkRemoteActionApplied(action.player, action.sequence); --budget;
    }
}

static void refreshHostLogisticsPause() {
    if (netRole() == NET_CLIENT) return;
    bool open = g_chestOpen >= 0;
    for (int slot = 1; slot < MAX_PLAYERS && !open; ++slot) {
        const PlayerSession& session = g_playerSessions[slot];
        const int d = session.openDevice;
        open = session.connected && d >= 0 && d < MAX_DEVICES &&
               g_devices[d].used && g_devices[d].type == DEV_CHEST;
    }
    g_logisticsUiOpen = open;
}

static const int RESPAWN_DELAY_FRAMES = 10 * 60;

static void tickDeadPlayer(int slot, PlayerSession& session) {
    if (session.respawnFrames <= 0) session.respawnFrames = RESPAWN_DELAY_FRAMES;
    if (--session.respawnFrames > 0) return;

    float spawnX = 0.0f, spawnY = 0.0f;
    Device* bed = (session.respawnBedX >= 0 && session.respawnBedY >= 0)
        ? devAt(session.respawnBedX, session.respawnBedY) : 0;
    if (bed && bed->used && bed->type == DEV_BED &&
        bed->x == session.respawnBedX && bed->y == session.respawnBedY) {
        /* Centre the body directly above the platform-topped bed. The one-cell
           gap avoids spawning with the feet already intersecting its cells. */
        spawnX = (float)bed->x + DEV_W * 0.5f;
        spawnY = (float)bed->y - PLAYER_H * 0.5f - 1.0f;
    } else {
        session.respawnBedX = session.respawnBedY = -1;
        worldSpawnPoint(&spawnX, &spawnY);
    }

    session.body.reset(spawnX, spawnY);
    session.restBed = -1; session.openDevice = -1;
    session.previousAimX = session.previousAimY = -1;
    session.previousCommandBits = 0; session.suppressRightUse = false;
    session.lineActive = false; session.wireX = session.wireY = -1;
    session.circuitWireFrom = -1; session.circuitWirePort = 0;
    session.respawnFrames = 0;
    session.body.occupy(g_world, slot);
}

static void updatePlayerFromCommand(int slot, PlayerSession& session, PlayerCommand& command,
                                    bool allowMovement) {
    if (!session.connected || session.generation != command.generation) return;
    playerHealingCooldownTick(session);
    session.inventory.selected = imax(0, imin(INV_SLOTS - 1, (int)command.selected));
    if (!session.body.alive) {
        undoFinish(slot, &session.inventory);
        tickDeadPlayer(slot, session);
        command.pressed = 0;
        return;
    }
    session.respawnFrames = 0;
    if (session.restBed >= 0 && (!g_devices[session.restBed].used ||
                                g_devices[session.restBed].type != DEV_BED))
        session.restBed = -1;
    PlayerInput in;
    in.left = (command.bits & PCMD_LEFT) != 0;
    in.right = (command.bits & PCMD_RIGHT) != 0;
    in.jump = (command.bits & PCMD_JUMP) != 0;
    in.down = (command.bits & PCMD_DOWN) != 0;
    if (session.restBed >= 0 && (in.left || in.right || in.jump || in.down)) session.restBed = -1;
    session.body.fly = flightSpec(session.inventory);
    session.body.speedMul = 1.0f + (float)session.inventory.speedBonus() / 100.0f;
    session.body.resist = session.inventory.tempResist();
    if (allowMovement && session.restBed < 0) session.body.update(g_world, in);
    if (!session.body.alive) {
        undoFinish(slot, &session.inventory);
        session.respawnFrames = RESPAWN_DELAY_FRAMES;
        command.pressed = 0;
        return;
    }
    session.body.occupy(g_world, slot);
    if (allowMovement) doorAutoOpen(g_world, session.body);
    applyPlayerUses(session, command);
    command.pressed = 0; /* edge verbs are consumed once, even if this held command is reused */
}

static void updateRemotePlayersFromCommands(bool allowMovement) {
    for (int slot = 1; slot < MAX_PLAYERS; ++slot) {
        if (!g_playerSessions[slot].connected) { g_remoteInputAge[slot] = 1000; continue; }
        PlayerCommand fresh;
        if (netPopRemoteCommand((PlayerId)slot, &fresh)) {
            g_remoteInput[slot] = fresh; g_remoteInputAge[slot] = 0;
        } else if (g_remoteInputAge[slot] < 1000) ++g_remoteInputAge[slot];
        /* A dropped or stalled connection must not leave movement held down. */
        if (g_remoteInputAge[slot] > 30) g_remoteInput[slot].bits = 0;
        if (g_remoteInputAge[slot] >= 1000) continue; /* never heard from */
        if (playerSessionSlotForNetworkId(g_remoteInput[slot].player) != slot) continue;
        updatePlayerFromCommand(slot, g_playerSessions[slot], g_remoteInput[slot], allowMovement);
        netMarkRemoteCommandApplied((PlayerId)slot, g_remoteInput[slot].sequence);
    }
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
    if (g_survival && g_playerOn) {
        sendClientAction(NACT_CIRCUIT_TERMINAL, 0, (u8)index, (u8)port);
        if (g_circuitWireFrom < 0) { g_circuitWireFrom = index; g_circuitWireFromPort = port; }
        else { g_circuitWireFrom = -1; g_circuitWireFromPort = 0; }
        return;
    }
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
static void fireToolFor(Player& player, Inventory& inventory, const Aim& aim) {
    ItemStack& h = inventory.held();
    if (h.empty() || ITEMS[h.item].kind != ITEMK_TOOL || h.inst == 0) return;

    const ToolShot s = toolResolve(h);
    if (!s.canFire) return;                       /* a tool with no modules */

    ToolInst& ti = g_toolInst[h.inst];
    if (ti.cooldown > 0) return;
    /* Tools created by an older save acquire the current chassis battery on
       first use. Normal creation configures this in toolInstNew(). */
    if (!ti.energyCapacity) {
        ti.energyCapacity = ITEMS[h.item].energyCapacity;
        ti.energyRecharge = ITEMS[h.item].energyRecharge;
        ti.energy = ti.energyCapacity;
    }
    if (!toolShotEnergyAvailable(h, s)) return;
    /* The two charms that change the shot itself. Resolved here, beside the
       delay, rather than inside toolResolve: what a TOOL does is a property of
       the tool and its modules, and folding the wearer's jewellery into that
       answer would mean the bench panel had to state a number that changes when
       you take a ring off. */
    int shotDamage = accessoryShotDamage(inventory, s.damage);
    const int rangedDamagePct = inventory.rangedDamagePct();
    if (rangedDamagePct > 0)
        shotDamage = imax(shotDamage + 1,
                          shotDamage + shotDamage * rangedDamagePct / 100);
    const float shotSpeed  = accessoryShotSpeed(inventory, s.speed);
    const int rangedRangePct = inventory.rangedRangePct();
    const int shotLife = s.life + s.life * rangedRangePct / 100;

    const float pcx = player.centreX(), pcy = player.centreY();
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
    const float SPEED  = shotSpeed;
    /* The payload is consumed HERE, on firing, not on impact -- a shot that
       missed everything and fizzled out over open sky still cost its LN2,
       the same way a mining tool spends bites whether or not it hit
       anything worth digging. Confirmed against the pack's actual count
       rather than trusted from the resolved ToolShot, which only reports
       there being SOME payload loaded, not how much. */
    int payload = MAT_EMPTY;
    if (s.payloadMat != MAT_EMPTY && ti.payload.count > 0) {
        payload = s.payloadMat;
    }
    const float vx = dx * SPEED, vy = dy * SPEED;
    u8 owner = PLAYER_NONE;
    for (int slot = 0; slot < MAX_PLAYERS; ++slot)
        if (&g_playerSessions[slot].inventory == &inventory) { owner = (u8)slot; break; }
    const bool fired = projSpawn(pcx + dx * MUZZLE, pcy + dy * MUZZLE, vx, vy,
                                 s.power, s.pierce, shotLife, s.colour, s.blast,
                                 payload, shotDamage, false, s.gravity, s.effect,
                                 s.bounces, s.homing, owner);
    if (!fired) return;

    toolCommitShot(h, s, accessoryShotDelay(inventory, s.delay));
    if (payload != MAT_EMPTY && --ti.payload.count == 0) {
        ti.payload.item = ITEM_NONE; ti.payload.inst = 0;
    }
    if (fired && accessoryTwinShot(inventory)) {
        /* Duplicate the command, as the drone controller does. One payload was
           spent above; the accessory rewards the slot with a second delivery,
           fanned just enough that both projectiles remain individually visible. */
        const float fanX = -vy * 0.10f, fanY = vx * 0.10f;
        projSpawn(pcx + dx * MUZZLE, pcy + dy * MUZZLE,
                  vx + fanX, vy + fanY, s.power, s.pierce, shotLife, 0xD8A4FF,
                  s.blast, payload, shotDamage, false, s.gravity, s.effect,
                  s.bounces, s.homing, owner);
    }
}

static void fireTool(const Aim& aim) { fireToolFor(g_player, g_inv, aim); }

/* A Glowflare is ammunition in its own container, not a durable weapon. It
   therefore owns no ToolInst and fires once per click from an ordinary stack.
   Returning projSpawn's answer matters: a saturated projectile pool has not
   actually used the flare, so it must not quietly consume one. */
static bool throwGlowflareFor(Player& player, Inventory& inventory, const Aim& aim) {
    const ItemStack& h = inventory.held();
    if (h.empty() || h.item != ITEM_GLOW_FLARE) return false;
    const ItemDef& d = ITEMS[h.item];

    const float pcx = player.centreX(), pcy = player.centreY();
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

static bool throwGlowflare(const Aim& aim) { return throwGlowflareFor(g_player, g_inv, aim); }

/* Devices are discrete, but a drag is still a useful way to lay out a run. Walk
   every crossed cell so a fast pipe stroke cannot skip a lattice slot. devPlace
   rejects overlapping footprints; charging only on success makes that rejection
   free and lets a stroke safely pass across devices already in place. */
static void placeDeviceStrokeFor(Inventory& inventory, int& previousX, int& previousY,
                                 u8 type, bool consume, const Aim& aim, int undoSlot) {
    const int x0 = previousX, y0 = previousY;
    const int steps = imax(abs(aim.x - x0), abs(aim.y - y0));
    for (int s = 0; s <= steps; ++s) {
        const int x = steps ? x0 + (aim.x - x0) * s / steps : aim.x;
        const int y = steps ? y0 + (aim.y - y0) * s / steps : aim.y;
        if (undoSlot >= 0) {
            undoBegin(undoSlot, consume ? &inventory : 0);
            /* Covers centred devices and the at-most-one-footprint logistics
               lattice snap. Only genuinely changed cells survive finish. */
            undoCaptureDisc(undoSlot, x, y, DEV_W * 2);
        }
        if (devPlace(g_world, type, x, y)) {
            if (undoSlot >= 0) {
                UndoPlacedDevice placed; memset(&placed, 0, sizeof(placed));
                placed.torch = type == DEV_TORCH; placed.type = type;
                if (placed.torch) {
                    const int ti = torchAt(x, y);
                    const TorchFixture* fixtures = torchData();
                    if (ti >= 0 && fixtures) { placed.index = ti; placed.x = fixtures[ti].x; placed.y = fixtures[ti].y; }
                    else placed.index = -1;
                } else {
                    Device* d = devAt(x, y);
                    placed.index = d ? (int)(d - g_devices) : -1;
                    placed.x = d ? d->x : devOriginX(x); placed.y = d ? d->y : devOriginY(y);
                }
                if (placed.index >= 0) g_activeUndo[undoSlot].placed.push_back(placed);
            }
            if (consume) inventory.take(inventory.held().item, 1);
        }
        if (consume && inventory.held().empty()) break;
    }
    previousX = aim.x; previousY = aim.y;
}

static void placeDeviceStroke(u8 type, bool consume, const Aim& aim) {
    placeDeviceStrokeFor(g_inv, g_pmx, g_pmy, type, consume, aim,
                         netRole() == NET_CLIENT ? -1 : LOCAL_PLAYER_ID);
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
            playerConsumeHealing(g_playerSessions[0], what);
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
    const bool undoable = sel != TOOL_HEAT && sel != TOOL_COOL;
    if (undoable) undoBegin(LOCAL_PLAYER_ID, 0);
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
            if (undoable) undoCaptureDisc(LOCAL_PLAYER_ID, px, py, g_brushRadius);
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
/* One row taller, for the box controls. */
static const int DEVP_BOX_H = 128;
static RECT g_devpBox, g_devpDec, g_devpInc, g_devpTake, g_devpTurn, g_devpClose;
/* A second control row, for the miner and the placer only. They are the one
   pair with more to say than "read it, nudge it" -- a direction, a depth, a
   filter and a trigger mode -- and cramming that onto the single row every
   other machine uses would make the row unreadable for all of them. */
static RECT g_devpDepthDec, g_devpDepthBox, g_devpDepthInc, g_devpFilter, g_devpMode;

static void layoutDevPanel(const Device& d) {
    /* Sit it just above and right of the machine, in screen pixels. */
    const int h = circuitIsCombinator(d.type) ? DEVP_CIRCUIT_H
                : devHasBox(d.type)            ? DEVP_BOX_H
                                               : DEVP_H;
    int px = PANEL_W + (d.x + DEV_W - g_camX) * cellPixels() + 8;
    int py = (d.y - g_camY) * cellPixels() - h - 6;
    if (px + DEVP_W > WIN_W - 6) px = PANEL_W + (d.x - g_camX) * cellPixels() - DEVP_W - 8;
    if (px < PANEL_W + 6)        px = PANEL_W + 6;
    if (py < 6)                  py = (d.y + DEV_H - g_camY) * cellPixels() + 6;
    if (py + h > WIN_H - 6) py = WIN_H - 6 - h;

    SetRect(&g_devpBox, px, py, px + DEVP_W, py + h);
    /* --- widths, rebalanced -------------------------------------------
       The turn button used to be 28 pixels, which fits "x" and not "aim down"
       -- it rendered as a clipped "aim", i.e. a control that looks broken
       rather than abbreviated. The steppers are the ones that can afford to
       shrink, since their labels are a single character. */
    const int by = py + h - 30;
    SetRect(&g_devpDec,   px + 10,  by, px + 70,  by + 22);
    SetRect(&g_devpInc,   px + 74,  by, px + 134, by + 22);
    SetRect(&g_devpTake,  px + 138, by, px + 228, by + 22);
    SetRect(&g_devpTurn,  px + 232, by, px + 340, by + 22);
    SetRect(&g_devpClose, px + DEVP_W - 40, by, px + DEVP_W - 10, by + 22);

    if (devHasBox(d.type)) {
        /* [-] [deep N] [+], with the middle a READOUT rather than a button.
           It was a two-button row with the value painted on the second one,
           which reads as "press this to make it deeper" -- so the number and
           the control it belonged to were the same object, and pressing what
           looked like a label changed it. */
        const int by2 = by - 26;
        SetRect(&g_devpDepthDec, px + 10,  by2, px + 44,  by2 + 22);
        SetRect(&g_devpDepthBox, px + 48,  by2, px + 122, by2 + 22);
        SetRect(&g_devpDepthInc, px + 126, by2, px + 160, by2 + 22);
        SetRect(&g_devpFilter,   px + 164, by2, px + 274, by2 + 22);
        SetRect(&g_devpMode,     px + 278, by2, px + 410, by2 + 22);
    } else {
        SetRectEmpty(&g_devpDepthDec); SetRectEmpty(&g_devpDepthBox);
        SetRectEmpty(&g_devpDepthInc);
        SetRectEmpty(&g_devpFilter);   SetRectEmpty(&g_devpMode);
    }
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
    if (PtInRect(&g_devpClose, pt)) {
        if (netRole() == NET_CLIENT) {
            g_closeDevicePending = g_devPanel;
            g_playerSessions[0].openDevice = -1;
        }
        sendClientAction(NACT_CLOSE_DEVICE); g_devPanel = -1; return true;
    }
    /* Combinators own their whole bottom row. Signal buttons open the same
       searchable inventory browser as filters; actual mutations still travel
       through the player action queue on both local and network authorities. */
    if (d.type == DEV_CONSTANT_COMBINATOR) {
        if (PtInRect(&g_devpDec, pt)) sendClientAction(NACT_DEVICE, 0, NDEV_DEC);
        else if (PtInRect(&g_devpInc, pt)) sendClientAction(NACT_DEVICE, 0, NDEV_INC);
        else if (PtInRect(&g_devpTake, pt)) openCircuitSignalPicker(index, CIR_PICK_SIGNAL);
        return true;
    }
    if (d.type == DEV_ARITHMETIC_COMBINATOR || d.type == DEV_DECIDER_COMBINATOR) {
        if (PtInRect(&g_devpDec, pt)) openCircuitSignalPicker(index, CIR_PICK_A);
        else if (PtInRect(&g_devpInc, pt)) openCircuitSignalPicker(index, CIR_PICK_B);
        else if (PtInRect(&g_devpTake, pt)) openCircuitSignalPicker(index, CIR_PICK_OUT);
        else if (PtInRect(&g_devpTurn, pt)) sendClientAction(NACT_DEVICE, 0, NDEV_TURN);
        return true;
    }
    if (d.type == DEV_PIPE || d.type == DEV_CROSSOVER) return true;
    if (devHasBox(d.type)) {
        if (PtInRect(&g_devpDepthDec, pt)) {
            sendClientAction(NACT_DEVICE, 0, NDEV_DEPTH_DEC); return true;
        }
        if (PtInRect(&g_devpDepthInc, pt)) {
            sendClientAction(NACT_DEVICE, 0, NDEV_DEPTH_INC); return true;
        }
        if (PtInRect(&g_devpMode, pt)) {
            sendClientAction(NACT_DEVICE, 0, NDEV_MODE); return true;
        }
        if (PtInRect(&g_devpFilter, pt)) {
            /* The same material picker the drain uses -- one gesture for
               "choose a material", wherever it is being chosen. */
            g_filterDevice = g_devPanel; g_creativeOpen = true; g_creSearch[0] = 0;
            g_creScroll = 0; g_creSearchFocus = true; layoutCreative();
            return true;
        }
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
    if (PtInRect(&g_devpDec, pt)) sendClientAction(NACT_DEVICE, 0, NDEV_DEC);
    else if (PtInRect(&g_devpInc, pt)) sendClientAction(NACT_DEVICE, 0, NDEV_INC);
    else if (PtInRect(&g_devpTurn, pt) && di.aimable) sendClientAction(NACT_DEVICE, 0, NDEV_TURN);
    else if (PtInRect(&g_devpTake, pt)) sendClientAction(NACT_DEVICE, 0, NDEV_TAKE);
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
    if (devHasBox(d.type)) {
        drawButton(hdc, g_devpDepthDec, "-", 0, false, PtInRect(&g_devpDepthDec, pt) != 0);
        drawButton(hdc, g_devpDepthInc, "+", 0, false, PtInRect(&g_devpDepthInc, pt) != 0);
        {
            /* The readout, framed but never lit by hover -- it is not a
               control and should not offer to be pressed. */
            char depthLabel[32];
            sprintf(depthLabel, "deep %d", devBoxDepth(d));
            RECT rr = g_devpDepthBox;
            FillRect(hdc, &rr, g_btnBg);
            FrameRect(hdc, &rr, g_borderBrush);
            SetTextColor(hdc, RGB(214, 216, 224));
            DrawTextA(hdc, depthLabel, -1, &rr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        /* The filter names the material or says it takes anything, because an
           unset filter and a filter set to something are the two states you
           most need to tell apart at a glance. */
        const int filterMat = devFilterMat(d);
        char filterLabel[48];
        if (filterMat == MAT_EMPTY) strcpy(filterLabel, "any material");
        else sprintf(filterLabel, "only %s", MATS[filterMat].name);
        drawButton(hdc, g_devpFilter, filterLabel, 0, filterMat != MAT_EMPTY,
                   PtInRect(&g_devpFilter, pt) != 0);

        char modeLabel[48];
        sprintf(modeLabel, "runs %s", devRunModeName(devRunMode(d)));
        drawButton(hdc, g_devpMode, modeLabel, 0, false, PtInRect(&g_devpMode, pt) != 0);
    }
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
        /* Above the optional energy and fuel gauges. The stack from the hotbar
           upward is: name, stats, energy, fuel, breath, then health. Reserving
           every slot keeps health fixed when equipment or the held item changes.

           Health keeps its slot whether or not flight gear is worn, so the bar
           does not jump when you take a jetpack off -- the gap where the fuel
           gauge would be is the better cost. */
        const int x0 = g_hotRect[0].left;
        const int y1 = g_hotRect[0].top - 67, y0 = y1 - 9;
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
        if (!g_player.alive) {
            const int frames = g_playerSessions[0].respawnFrames > 0
                ? g_playerSessions[0].respawnFrames : RESPAWN_DELAY_FRAMES;
            sprintf(hpTxt, "DEAD  -  respawning in %ds", (frames + 59) / 60);
        }
        else if (g_player.breath == 0)     sprintf(hpTxt, "%d   DROWNING", g_player.hp);
        else if (g_player.hurtingHot())    sprintf(hpTxt, "%d   BURNING", g_player.hp);
        else if (g_player.hurtingCold())   sprintf(hpTxt, "%d   FREEZING", g_player.hp);
        else if (g_player.underwater)      sprintf(hpTxt, "%d   %ds of air",
                                                   g_player.hp, g_player.breath / 60);
        else                               sprintf(hpTxt, "%d", g_player.hp);
        if (g_player.alive && g_playerSessions[0].healCooldown > 0) {
            const int seconds = (g_playerSessions[0].healCooldown + 59) / 60;
            const int used = (int)strlen(hpTxt);
            snprintf(hpTxt + used, sizeof(hpTxt) - (size_t)used,
                     "   HEAL LOCK %ds", seconds);
        }
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
        const int y1 = g_hotRect[0].top - 49, y0 = y1 - 7;
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

    /* --- held multitool energy ---------------------------------------------
       A number is useful for exact loadout arithmetic, but poor moment-to-
       moment feedback. The bar answers "can I fire yet" at a glance; its gold
       notch marks the cost of the next module in the left-to-right sequence.
       It appears only for an energy chassis, so the starter Bolt Caster does
       not grow an unexplained permanently empty gauge. */
    {
        const ItemStack& held = g_inv.held();
        if (!held.empty() && held.inst && ITEMS[held.item].kind == ITEMK_TOOL &&
            ITEMS[held.item].energyCapacity > 0) {
            const ToolInst& ti = g_toolInst[held.inst];
            const ToolShot shot = toolResolve(held);
            const int x0 = g_hotRect[0].left, x1 = g_hotRect[HOTBAR_SLOTS - 1].right;
            const int y1 = g_hotRect[0].top - 40, y0 = y1 - 7;
            RECT bar = { x0, y0, x1, y1 };
            FillRect(hdc, &bar, g_btnBg);
            const float frac = ti.energyCapacity
                ? (float)ti.energy / (float)ti.energyCapacity : 0.0f;
            const float shown = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
            RECT fill = bar;
            fill.right = x0 + (int)((float)(x1 - x0) * shown);
            if (fill.right > fill.left) {
                const bool ready = !shot.canFire || ti.energy >= shot.energyCost;
                HBRUSH b = CreateSolidBrush(ready ? RGB(78, 190, 224) : RGB(154, 86, 190));
                FillRect(hdc, &fill, b);
                DeleteObject(b);
            }
            FrameRect(hdc, &bar, g_borderBrush);
            if (shot.canFire && shot.energyCost > 0 && ti.energyCapacity > 0) {
                const int markX = x0 + (int)((float)(x1 - x0) *
                    ((float)shot.energyCost / (float)ti.energyCapacity));
                RECT mark = { imax(x0 + 1, imin(x1 - 2, markX)), y0 + 1,
                              imax(x0 + 2, imin(x1 - 1, markX + 1)), y1 - 1 };
                FillRect(hdc, &mark, g_accentBrush);
            }
        }
    }

    for (int i = 0; i < HOTBAR_SLOTS; ++i) {
        RECT r = g_hotRect[i];
        const bool sel = (i == g_hotbarSelected);
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
        char s[160];
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
            if (sh.canFire && h.inst) {
                const ToolInst& ti = g_toolInst[h.inst];
                sprintf(s, "%s  E %u/%u  next %dE  dmg %d  %d/s",
                        ITEMS[h.item].name, (unsigned)ti.energy,
                        (unsigned)ti.energyCapacity, sh.energyCost, sh.damage,
                        60 / imax(1, sh.delay));
            }
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
/* --- the blade, in the world -----------------------------------------------
   Rasterised as a thick line from hilt to tip, the same way drawHeldTool draws
   a multitool, rather than as a rotated sprite. Two reasons, and the second is
   the one that decided it.

   A 14x14 icon rotated to an arbitrary angle at two screen pixels a cell is a
   smear -- there is not enough resolution in the source for the rotation to
   preserve the silhouette, so all seven tiers would arrive at the same grey
   blob. And the shape that matters here is not the icon, it is WHERE THE METAL
   IS: the thing the player has to read mid-fight is exactly the segment the hit
   test uses, so drawing that segment means what you see is what is dangerous,
   by construction rather than by keeping two descriptions in step.

   Drawn only for the local player. Remote swings are not replicated -- see the
   note on PlayerSession::swingFrame. */
static void drawMeleeSegment(u32* px, bool lit, ItemId item,
                             float x0, float y0, float x1, float y1) {
    const ItemDef& def = ITEMS[item];
    const bool sword = def.meleeStyle == MELEE_SWING;
    const float dx = x1 - x0, dy = y1 - y0;
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.5f) return;
    const float nx = -dy / len, ny = dx / len;
    const int steps = imax(1, (int)len);
    /* Fixed-size furniture keeps the doubled swords looking like weapons.
       Percentage furniture made a thirty-cell blade grow a five-cell handle
       and an eight-cell guard region, which read as a striped pole. */
    const int grip = sword ? 4 : imax(3, imin(5, steps / 4));

    const auto put = [&](float fx, float fy, u32 colour) {
        const int x = (int)fx, y = (int)fy;
        if (x < 0 || x >= VIEW_CELLS_W || y < 0 || y >= VIEW_CELLS_H) return;
        px[y * VIEW_CELLS_W + x] = lit ? shadeColor(colour, viewShade(x, y)) : colour;
    };

    for (int i = 0; i <= steps; ++i) {
        const float along = (float)i / (float)steps;
        const float fx = x0 + dx * along, fy = y0 + dy * along;
        u32 colour;
        if      (i < grip)       colour = 0xC8B070;       /* wrapped grip */
        else if (i == grip)      colour = 0x6E7684;       /* steel collar */
        else if (i >= steps - 1) colour = 0xF2F5FF;       /* bright point */
        else                     colour = ITEMS[item].colour;

        /* Long swords get a symmetric three-cell blade for their lower half,
           narrowing to a single-cell point. The former one-sided thickness
           made the blade visibly wobble around its hit segment as it rotated. */
        int halfWidth = 0;
        if (sword && i > grip && i < steps - 2 &&
            i < grip + (steps - grip) * 3 / 5) halfWidth = 1;
        for (int w = -halfWidth; w <= halfWidth; ++w)
            put(fx + nx * (float)w, fy + ny * (float)w, colour);

        /* A real crossguard, perpendicular to the blade, replaces the old
           guard-coloured section running along it. This silhouette remains
           unmistakably a sword at every aim angle and facing direction. */
        if (sword && i == grip)
            for (int w = -3; w <= 3; ++w)
                put(fx + nx * (float)w, fy + ny * (float)w, 0x6E7684);
    }
}

static void drawMeleeSwing(u32* px, bool lit) {
    PlayerSession& session = g_playerSessions[0];
    if (session.swingFrame <= 0) return;
    const ItemStack& h = g_inv.held();
    if (h.empty() || ITEMS[h.item].kind != ITEMK_MELEE) return;
    const ItemDef& def = ITEMS[h.item];

    const int total = meleeFramesFor(g_inv, def);
    const float phase = (float)(total - session.swingFrame) / (float)total;
    float hx, hy, tx, ty;
    meleeBlade(g_player, def, meleeReachFor(g_inv, def),
               session.swingDirX, session.swingDirY,
               phase, &hx, &hy, &tx, &ty);

    drawMeleeSegment(px, lit, h.item,
                     hx - (float)g_camX, hy - (float)g_camY,
                     tx - (float)g_camX, ty - (float)g_camY);
}

static void drawHeldTool(u32* px, const Aim& aim, bool lit) {
    /* A melee weapon at rest is drawn the same way a multitool is -- pointing
       where you are aiming -- so the character is visibly holding something
       between strokes rather than producing a sword out of nothing each time
       the button goes down. The swing itself takes over the moment one starts. */
    {
        const ItemStack& m = g_inv.held();
        if (!m.empty() && ITEMS[m.item].kind == ITEMK_MELEE) {
            if (g_playerSessions[0].swingFrame > 0) { drawMeleeSwing(px, lit); return; }
            const ItemDef& def = ITEMS[m.item];
            const float pcx = g_player.centreX(), pcy = g_player.centreY();
            float ax = (float)aim.x - pcx, ay = (float)aim.y - pcy;
            const float ad = sqrtf(ax * ax + ay * ay);
            if (ad > 0.001f) { ax /= ad; ay /= ad; }
            else { ax = (float)(g_player.facing >= 0 ? 1 : -1); ay = 0.0f; }
            /* A sword is rigid: its resting segment must be exactly as long as
               the visible swing segment, or starting an attack looks like the
               blade telescopes outward. meleeBlade places a swing hilt two
               cells from the player and its tip at meleeReach, hence reach-2.
               Spears retain a withdrawn carry pose because extension is the
               readable motion of a stab. */
            const float meleeReach = meleeReachFor(g_inv, def);
            const float rest = def.meleeStyle == MELEE_SWING
                             ? meleeReach - 2.0f
                             : meleeReach * 0.55f;
            const float x0 = pcx + ax * 2.0f - (float)g_camX;
            const float y0 = pcy - 1.0f + ay * 2.0f - (float)g_camY;
            drawMeleeSegment(px, lit, m.item, x0, y0,
                             x0 + ax * rest, y0 + ay * rest);
            return;
        }
    }

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
    if (keyHeld('Q') && !g_wireMode && !g_circuitWireMode) {
        const bool digging = g_survival && g_playerOn && keyHeld(VK_RBUTTON);
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
                   : "CREATIVE  --  click an item to hold it; right-click to remove it", -1, &title,
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

        /* The name carries exact identity here, so this is a compact icon well
           rather than the full icon-only square used by the pack and hotbar. */
        const bool rowHot = inRect(r, g_mx, g_my);
        FillRect(hdc, &r, have > 0 ? g_btnBgSel : (rowHot ? g_btnBgHot : g_btnBg));
        FrameRect(hdc, &r, have > 0 ? g_accentBrush : g_borderBrush);
        RECT ir = { r.left + 3, r.top + 2,
                    r.left + 3 + CRE_ENTRY_ICON_W, r.bottom - 2 };
        FillRect(hdc, &ir, g_panelBg);
        if (signalPicker) drawCircuitSignalIcon(hdc, ir, it);
        else              drawItemIcon(hdc, ir, (ItemId)it);
        RECT label = r; label.left = ir.right + 7; label.right -= have > 0 ? 42 : 7;
        SetTextColor(hdc, have > 0 ? RGB(245, 224, 150) : RGB(214, 216, 224));
        DrawTextA(hdc, signalPicker ? circuitSignalName(it) : ITEMS[it].name,
                  -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

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
        if (g_creRowCount > g_creVisRows) {
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
            InflateRect(&ir, -3, -3);
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
        char s[200];

        /* --- two lines, not one ------------------------------------------
           This was a single sprintf that concatenated everything the character
           had, and it grew past the width of the panel the moment there was
           anything to say: with flight gear on it already read "EQUIPPED --
           climb 8.4 cells/s, 3.0s of fuel, reach +12, speed +18%, 40/0
           heat/cold resist", and the charms add six more numbers to that.
           DrawTextA with DT_SINGLELINE does not wrap, it CLIPS, so the growth
           was invisible until a stat silently disappeared off the right edge.

           Split by what the numbers are ABOUT rather than by length, so which
           line a stat is on stays stable as gear changes: the body -- how it
           moves, what it survives -- and then the weapon. A line that reflows
           its contents is a line you have to re-read every time.

           The RESOLVED numbers throughout, not any item's own. Two pieces of
           flight gear do not add up (see flightSpec), the passives take the
           largest rather than summing (see ItemDef::regenPer), so the only
           figures worth printing are the ones that will actually apply. */
        RECT lr = g_crePanel;
        lr.left = g_eqRect[EQ_FEET].left;
        lr.top  = g_eqRect[EQ_FEET].top - 40;

        int n = sprintf(s, "EQUIPPED  --  reach +%d, speed +%d%%, armour %d",
                        g_inv.reachBonus(), g_inv.speedBonus(), g_inv.armour());
        if (fly.any())
            n += sprintf(s + n, ", climb %.1f/s for %.1fs",
                         fly.riseCap * 60.0f, (float)fly.fuel / 60.0f);
        if (temp.heat || temp.cold)
            n += sprintf(s + n, ", resist %dC hot / %dC cold", temp.heat, temp.cold);
        if (g_inv.regenPer() > 0)
            n += sprintf(s + n, ", regen 1 per %.1fs", (float)g_inv.regenPer() / 60.0f);
        SetTextColor(hdc, fly.any() ? RGB(226, 190, 90) : RGB(150, 156, 168));
        DrawTextA(hdc, s, -1, &lr, DT_LEFT | DT_TOP | DT_SINGLELINE);

        /* The second line exists only when there is something on it. A row of
           permanent zeroes teaches nothing and takes up the space the row of
           group headings needs. */
        n = 0;
        if (g_inv.damagePct())    n += sprintf(s + n, "%sdamage +%d%%", n ? ", " : "", g_inv.damagePct());
        if (g_inv.cooldownPct())  n += sprintf(s + n, "%sfire rate +%d%%", n ? ", " : "", g_inv.cooldownPct());
        if (g_inv.shotSpeedPct()) n += sprintf(s + n, "%sshot speed +%d%%", n ? ", " : "", g_inv.shotSpeedPct());
        if (g_inv.rangedDamagePct()) n += sprintf(s + n, "%sranged damage +%d%%", n ? ", " : "", g_inv.rangedDamagePct());
        if (g_inv.rangedRangePct()) n += sprintf(s + n, "%srange +%d%%", n ? ", " : "", g_inv.rangedRangePct());
        if (g_inv.meleeDamagePct()) n += sprintf(s + n, "%smelee damage +%d%%", n ? ", " : "", g_inv.meleeDamagePct());
        if (g_inv.meleeReachPct()) n += sprintf(s + n, "%smelee reach +%d%%", n ? ", " : "", g_inv.meleeReachPct());
        if (g_inv.meleeSpeedPct()) n += sprintf(s + n, "%sswing speed +%d%%", n ? ", " : "", g_inv.meleeSpeedPct());
        if (g_inv.droneDamagePct()) n += sprintf(s + n, "%sdrone damage +%d%%", n ? ", " : "", g_inv.droneDamagePct());
        if (g_inv.pickupRadius()) n += sprintf(s + n, "%spickup +%d cells", n ? ", " : "", g_inv.pickupRadius());
        if (g_inv.lightGlow())    n += sprintf(s + n, "%sglow", n ? ", " : "");
        if (n) {
            RECT br = lr; br.top = lr.top + 15;
            SetTextColor(hdc, RGB(160, 200, 230));
            DrawTextA(hdc, s, -1, &br, DT_LEFT | DT_TOP | DT_SINGLELINE);
        }

        /* --- the group headings --------------------------------------------
           Three words carrying what eleven identical squares could not. They
           are also what lets the squares themselves be labelled in four
           characters or fewer: "TRINKETS" is written once above the group, so
           the boxes only have to say WHICH trinket, and "1" fits where
           "Trinket" was being clipped to "rinke". */
        for (int g = 0; g < 3; ++g) {
            RECT hr = g_crePanel;
            hr.left = g_eqRect[EQ_ORDER[EQ_GROUP_AT[g]]].left;
            /* Its own group's first slot, not EQ_FEET's. Reading the top off a
               fixed slot was invisible while every heading shared a row and
               would have stranded DRONES above the wrong one the moment they
               stopped sharing it. */
            hr.top  = g_eqRect[EQ_ORDER[EQ_GROUP_AT[g]]].top - 17;
            SetTextColor(hdc, RGB(126, 134, 150));
            DrawTextA(hdc, EQ_GROUP_NAME[g], -1, &hr, DT_LEFT | DT_TOP | DT_SINGLELINE);
        }

        for (int pos = 0; pos < EQ_COUNT; ++pos) {
            const int i = EQ_ORDER[pos];
            RECT r = g_eqRect[i];
            const ItemStack& eq = g_inv.equip[i];
            const bool hot = inRect(r, g_mx, g_my);
            const bool locked = !g_inv.droneBayUnlocked(i);
            FillRect(hdc, &r, hot ? g_btnBgHot : g_btnBg);
            FrameRect(hdc, &r, eq.empty() ? g_borderBrush : g_accentBrush);
            if (!eq.empty()) {
                RECT ir = r; ir.left += 2; ir.top += 2; ir.right -= 2; ir.bottom -= 2;
                drawItemIcon(hdc, ir, eq.item);
            } else {
                /* The SHORT name -- see EQ_SHORT. The long one is on the
                   tooltip, which is where there is room for it, and hovering is
                   the moment somebody is actually asking. */
                SetTextColor(hdc, RGB(110, 116, 128));
                RECT tr = r; tr.top += 10;
                DrawTextA(hdc, EQ_SHORT[i], -1, &tr, DT_CENTER | DT_TOP | DT_SINGLELINE);
                if (hot) { g_hoverLabel = EQ_NAMES[i]; g_hoverRect = r; }
            }
            if (locked) {
                /* Stored equipment remains visible and removable after losing
                   a capacity bonus, but LOCK makes it clear that it is inert. */
                SetTextColor(hdc, RGB(190, 126, 92));
                RECT tr = r; tr.top = r.bottom - 17;
                DrawTextA(hdc, "LOCK", -1, &tr, DT_CENTER | DT_TOP | DT_SINGLELINE);
                if (hot) {
                    g_hoverLabel = "Locked combat-drone bay -- use 2 Drone Armour pieces or a Drone Beacon";
                    g_hoverRect = r;
                }
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
                if (hot) { g_hoverLabel = "Bin -- drop a stack here to discard it"; g_hoverRect = r; }
            }
        }

        /* Drone chips are deliberately beneath their chassis row: player gear
           stays above, companion gear stays below, and the labels identify the
           bay even when two identical drones are equipped. */
        bool anyDroneChipSlot = false;
        for (int d = 0; d < MAX_DRONES; ++d) if (!IsRectEmpty(&g_droneModuleRect[d][0])) anyDroneChipSlot = true;
        if (anyDroneChipSlot) {
            RECT dr = g_crePanel;
            dr.left = g_eqRect[EQ_FEET].left;
            /* Anchored to whichever bay actually has a socket. Bay 0's rect is
               empty whenever the light bay is, and an empty RECT is at the
               origin -- so reading its top put this label in the top-left
               corner of the window the moment somebody equipped a weapon
               without a lamp. */
            int chipTop = 0;
            for (int d = 0; d < MAX_DRONES; ++d)
                if (!IsRectEmpty(&g_droneModuleRect[d][0])) { chipTop = g_droneModuleRect[d][0].top; break; }
            dr.top = chipTop - 20;
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
        char s[256];
        /* State the resolved numbers, not the tool's own -- the delay shown is
           what it will actually fire at with the modules currently in it, which
           is the only version of the number worth reading. */
        if (sh.canFire) {
            const ToolInst& ti = g_toolInst[ts.inst];
            const ItemId next = sh.moduleSlot >= 0 ? ti.slot[sh.moduleSlot] : ts.item;
            sprintf(s, "%s  E %u/%u +%u/s  next %s: %dE, %df, dmg %d",
                    ITEMS[ts.item].name, (unsigned)ti.energy,
                    (unsigned)ti.energyCapacity, (unsigned)ti.energyRecharge * 60u,
                    ITEMS[next].name, sh.energyCost, sh.delay, sh.damage);
        }
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
        RECT r = { g_mx + 8, g_my + 8, g_mx + 56, g_my + 56 };
        FillRect(hdc, &r, g_btnBgSel);
        FrameRect(hdc, &r, g_accentBrush);
        RECT ir = r; InflateRect(&ir, -3, -3);
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
static const int CRAFT_VIS_ROWS = 12;
static const int CRAFT_ROW_PITCH = 50;
static const int CRAFT_ROW_H = 46;
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

    const int h = 46 + visRows * CRAFT_ROW_PITCH + 12;
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
        SetRect(&g_craftRow[i], g_craftPanel.left + 12,
                g_craftPanel.top + 40 + row * CRAFT_ROW_PITCH,
                g_craftPanel.right - 12 - barW - 4,
                g_craftPanel.top + 40 + row * CRAFT_ROW_PITCH + CRAFT_ROW_H);
    }

    const int trackX = g_craftPanel.right - 12 - barW;
    const int trackY0 = g_craftPanel.top + 40;
    const int trackY1 = trackY0 + visRows * CRAFT_ROW_PITCH - 4;
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
            sendClientAction(NACT_CRAFT, 0, (u8)i, (u8)imin(n, 50));
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
        RECT sw = { r.left + 3, r.top + 2, r.left + 47, r.bottom - 2 };
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
    DrawTextA(hdc, netRole() == NET_OFF ? "PAUSED" : "MULTIPLAYER", -1,
              &title, DT_CENTER | DT_TOP | DT_SINGLELINE);

    drawButton(hdc, g_menuResume, "Resume", NULL, false, inRect(g_menuResume, g_mx, g_my));
    char hostLabel[96];
    sprintf(hostLabel, "Host LAN  (%s:%u)", netLocalAddress(), (unsigned)NET_DEFAULT_PORT);
    drawButton(hdc, g_menuHost, hostLabel, NULL, netRole() == NET_HOST,
               inRect(g_menuHost, g_mx, g_my));
    {
        RECT ip = g_menuIp;
        FillRect(hdc, &ip, inRect(ip, g_mx, g_my) ? g_btnBgHot : g_btnBg);
        FrameRect(hdc, &ip, g_joinIpFocus ? g_accentBrush : g_borderBrush);
        char text[80]; sprintf(text, "%s%s", g_joinIp, g_joinIpFocus ? "_" : "");
        SetTextColor(hdc, RGB(214, 216, 224));
        ip.left += 8; DrawTextA(hdc, text, -1, &ip, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    drawButton(hdc, g_menuSave, "Save game", NULL, false, inRect(g_menuSave, g_mx, g_my));
    drawButton(hdc, g_menuLoad, "Load game", NULL, false, inRect(g_menuLoad, g_mx, g_my));
    drawButton(hdc, g_menuJoin, "Join", NULL, netRole() == NET_CLIENT,
               inRect(g_menuJoin, g_mx, g_my));
    drawButton(hdc, g_menuStop, netRole() == NET_OFF ? "Offline" : "Disconnect / stop hosting",
               NULL, false, inRect(g_menuStop, g_mx, g_my));
    drawButton(hdc, g_menuQuit,   "Quit",   NULL, false, inRect(g_menuQuit,   g_mx, g_my));

    char uiScaleLabel[64];
    sprintf(uiScaleLabel, "UI scale  %d%%", uiScalePct());
    drawButton(hdc, g_menuUiMinus, "-", NULL, g_uiScaleIndex == 0,
               inRect(g_menuUiMinus, g_mx, g_my));
    drawButton(hdc, g_menuUiValue, uiScaleLabel, NULL, false, false);
    drawButton(hdc, g_menuUiPlus, "+", NULL, g_uiScaleIndex == UI_SCALE_COUNT - 1,
               inRect(g_menuUiPlus, g_mx, g_my));

    SetTextColor(hdc, netConnected() ? RGB(150, 210, 155) : RGB(176, 182, 194));
    RECT netLine = { g_menuPanel.left + 16, g_menuUiValue.bottom + 9,
                     g_menuPanel.right - 16, g_menuUiValue.bottom + 29 };
    DrawTextA(hdc, netStatus(), -1, &netLine, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

    /* Two columns: the key on the left, what it does on the right. Aligned on
       a fixed split rather than measured per row, because the alternative is a
       ragged left edge on the descriptions, which is what makes a list of this
       length hard to scan. */
    const int keyX  = g_menuPanel.left + 16;
    const int whatX = g_menuPanel.left + 130;
    const int keyPitch = uiScaled(15), keyHeight = uiScaled(14);
    int ry = g_menuUiValue.bottom + 38;
    for (int i = 0; i < N_KEY_HINTS; ++i, ry += keyPitch) {
        SetTextColor(hdc, RGB(226, 190, 90));
        RECT kr = { keyX, ry, whatX - 6, ry + keyHeight };
        DrawTextA(hdc, KEY_HINTS[i].key, -1, &kr, DT_LEFT | DT_TOP | DT_SINGLELINE);
        SetTextColor(hdc, RGB(176, 182, 194));
        RECT wr = { whatX, ry, g_menuPanel.right - 12, ry + keyHeight };
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
        const int savePitch = uiScaled(15), saveHeight = uiScaled(14);
        int ry = g_menuUiValue.bottom + 38 + N_KEY_HINTS * keyPitch + 10;
        char s[128];
        sprintf(s, "last save  %.2f MB", (double)saveTotalBytes() / (1024.0 * 1024.0));
        SetTextColor(hdc, RGB(226, 190, 90));
        RECT tr = { g_menuPanel.left + 16, ry, g_menuPanel.right - 12, ry + saveHeight };
        DrawTextA(hdc, s, -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE);
        ry += uiScaled(16);
        const SaveStat* st = saveStats();
        for (int i = 0; i < saveStatCount(); ++i) {
            if (st[i].bytes < 1024) break;
            if (st[i].bytes >= 1024 * 1024)
                sprintf(s, "%.2f MB", (double)st[i].bytes / (1024.0 * 1024.0));
            else
                sprintf(s, "%.1f KB", (double)st[i].bytes / 1024.0);
            SetTextColor(hdc, RGB(150, 156, 168));
            RECT nr = { g_menuPanel.left + 24, ry, g_menuPanel.left + 200, ry + saveHeight };
            DrawTextA(hdc, st[i].name, -1, &nr, DT_LEFT | DT_TOP | DT_SINGLELINE);
            SetTextColor(hdc, RGB(190, 196, 208));
            RECT vr = { g_menuPanel.left + 200, ry, g_menuPanel.right - 16, ry + saveHeight };
            DrawTextA(hdc, s, -1, &vr, DT_RIGHT | DT_TOP | DT_SINGLELINE);
            ry += savePitch;
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
    drawText(hdc, 10, 9, RGB(240, 240, 246), "CINDERLIFT");
    drawText(hdc, 94, 9, RGB(144, 154, 172),
             g_panelShowsPack ? "pack (click to hold)" : "catalog (wheel)");

    /* --- the pack, as a list ---------------------------------------------
       One row per stack: icon, name, count. The selected row is ringed in the
       accent colour, the same way the held hotbar slot is, so "what am I
       holding" has one answer shown in two places rather than two answers. */
    if (g_panelShowsPack) {
        for (int i = 0; i < g_packListCount; ++i) {
            RECT r = g_packListRect[i];
            if (r.right <= r.left) continue;
            const int slot = g_packList[i];
            const ItemStack& st = g_inv.slot[slot];
            if (st.empty()) continue;

            const bool hot = inRect(r, g_mx, g_my);
            const bool sel = (slot == g_inv.selected);
            FillRect(hdc, &r, hot ? g_btnBgHot : (sel ? g_btnBgSel : g_btnBg));
            FrameRect(hdc, &r, sel ? g_accentBrush : g_borderBrush);

            RECT ic = r;
            ic.left += 3; ic.top += 2; ic.bottom -= 2;
            ic.right = ic.left + (ic.bottom - ic.top);
            drawItemIcon(hdc, ic, st.item);

            /* The count on the right, the name filling what is left. Drawn in
               that order because the count is fixed-width and the name is not:
               reserving the right-hand strip first means a long name is clipped
               at its tail rather than overprinting the number, and a clipped
               name is still readable where an overprinted one is not. */
            char n[24];
            const unsigned c = st.count;
            if      (c >= 10000) sprintf(n, "%uk", c / 1000);
            else if (c >= 1000)  sprintf(n, "%u.%uk", c / 1000, (c % 1000) / 100);
            else                 sprintf(n, "%u", c);

            RECT cr = r; cr.right -= 6; cr.left = cr.right - 42;
            if (st.count > 1) {
                SetTextColor(hdc, RGB(180, 190, 206));
                DrawTextA(hdc, n, -1, &cr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }
            RECT tr = r; tr.left = ic.right + 5; tr.right = cr.left - 4;
            SetTextColor(hdc, sel ? RGB(245, 224, 150) : RGB(214, 220, 230));
            DrawTextA(hdc, ITEMS[st.item].name, -1, &tr,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        /* Something has to be said when it is empty, or an empty pack reads as
           a broken panel. */
        if (g_packListCount == 0) {
            RECT er = g_paletteArea; er.top += 6;
            SetTextColor(hdc, RGB(120, 128, 142));
            DrawTextA(hdc, "(pack is empty)", -1, &er, DT_CENTER | DT_TOP | DT_SINGLELINE);
        }
    }

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
    {
        /* A scrollbar-style rail rather than a progress bar: the thumb is the
           value, and clicking or dragging anywhere on the row moves it. */
        RECT rail = g_sizeTrack;
        FillRect(hdc, &rail, inRect(rail, g_mx, g_my) ? g_btnBgHot : g_btnBg);
        FrameRect(hdc, &rail, g_borderBrush);
        RECT groove = { rail.left + 6, (rail.top + rail.bottom) / 2 - 2,
                        rail.right - 6, (rail.top + rail.bottom) / 2 + 2 };
        FillRect(hdc, &groove, g_panelBg);
        RECT thumb = sizeThumbRect();
        FillRect(hdc, &thumb, g_sizeDragging ? g_btnBgSel : g_btnBgHot);
        FrameRect(hdc, &thumb, g_accentBrush);
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
    drawButton(hdc, g_actRect[ACT_WIRE], g_wireMode ? "Wire: Copper" : "Wire: Off",
               NULL, g_wireMode, inRect(g_actRect[ACT_WIRE], g_mx, g_my));
    {
        char circuitLabel[48];
        if (g_circuitWireMode && g_circuitWireFrom >= 0) sprintf(circuitLabel, "Circuit: choose device");
        else sprintf(circuitLabel, g_circuitWireMode ? "Circuit Wire: On" : "Circuit Wire: Off");
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
    /* Lit when the clock is actually in that half, so the pair doubles as a
       readout of what time it is -- which the panel could not otherwise tell
       you at all, short of looking at the sky. */
    drawButton(hdc, g_actRect[ACT_DAY],   "Day",   NULL, !isNight(),
               inRect(g_actRect[ACT_DAY],   g_mx, g_my));
    drawButton(hdc, g_actRect[ACT_NIGHT], "Night", NULL, isNight(),
               inRect(g_actRect[ACT_NIGHT], g_mx, g_my));
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
    sprintf(s, "%.0f fps   frame %.2f ms", g_fps, g_frameMs);
    drawText(hdc, 10, sy + 20, RGB(150, 200, 150), s);
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

/* ======================================================================
   Network-shaped frame pipeline
   ======================================================================
   Input/presentation and authority are deliberately separate even when they
   live in one process. Offline play feeds commands and actions into the local
   loopback queues; hosting consumes those plus a remote peer; joining never
   enters serverTick at all. */
static void clientInputTick() {
    netPoll(g_world);
    const bool authoritative = netRole() != NET_CLIENT;
    if (netRole() == NET_CLIENT && netClientReady()) {
        predictionReconcile();
        actionPredictionReconcile();
        const PlayerCommand command = localPlayerCommand();
        if (netSendCommand(command)) {
            predictionRemember(command);
            predictClientPlayer(command, true);
        }
        g_interactPulse = false;
        g_respawnPulse = false;
        g_lineCommitPulse = false;
    } else if (authoritative && g_survival && g_playerOn) {
        g_localInput = localPlayerCommand();
        g_interactPulse = false;
        g_respawnPulse = false;
        g_lineCommitPulse = false;
    } else if (netRole() == NET_CLIENT) {
        predictionClear();
        actionPredictionClear();
        /* Do not replay a click made on the loading screen after READY. Held
           movement/buttons are sampled fresh; only one-frame verbs need
           explicit disposal here. */
        g_interactPulse = false;
        g_respawnPulse = false;
        g_lineCommitPulse = false;
    }

    /* Held crafting is an input repeater, not authority. Every repeat becomes
       another validated craft action whether this process is offline, hosting,
       or joined to somebody else. */
    if (g_craftHeldRow >= 0) {
        if (!g_lmb || !g_craftOpen) {
            g_craftHeldRow = -1;
        } else if (--g_craftCool <= 0) {
            ++g_craftHeldFor;
            int gap = CRAFT_REPEAT_DELAY >> (g_craftHeldFor / 5);
            if (gap < CRAFT_REPEAT_MIN) gap = CRAFT_REPEAT_MIN;
            g_craftCool = gap;
            const int count = (GetKeyState(VK_SHIFT) & 0x8000) ? CRAFT_SHIFT_BATCH : 1;
            if (!sendClientAction(NACT_CRAFT, 0, (u8)g_craftHeldRow, (u8)imin(count, 50)))
                g_craftHeldRow = -1;
        }
    }
    if (netRole() == NET_CLIENT) netClientFrame(g_world);
}

static void publishServerRegions() {
    g_world.clearBlockBoxes();
    if (g_playerOn) g_player.occupy(g_world, LOCAL_PLAYER_ID);
    for (int slot = 1; slot < MAX_PLAYERS; ++slot)
        if (g_playerSessions[slot].connected)
            g_playerSessions[slot].body.occupy(g_world, slot);

    const int localX = g_playerOn ? (int)g_player.centreX() - viewCellsW() / 2 : g_camX;
    const int localY = g_playerOn ? (int)g_player.centreY() - viewCellsH() / 2 : g_camY;
    g_world.setLiveWindow(localX - SIM_MARGIN, localY - SIM_MARGIN,
                          localX + viewCellsW() + SIM_MARGIN,
                          localY + viewCellsH() + SIM_MARGIN);
    /* Independent islands avoid simulating the enormous rectangle between two
       players who explore in opposite directions. */
    for (int slot = 1; slot < MAX_PLAYERS; ++slot) {
        if (!g_playerSessions[slot].connected) continue;
        const Player& p = g_playerSessions[slot].body;
        const int cx = (int)p.centreX(), cy = (int)p.centreY();
        const int halfW = viewCellsW() / 2, halfH = viewCellsH() / 2;
        g_world.addLiveWindow(cx - halfW - SIM_MARGIN, cy - halfH - SIM_MARGIN,
                              cx + halfW + SIM_MARGIN, cy + halfH + SIM_MARGIN);
    }
}

static void serverTick(const LARGE_INTEGER& perfFrequency) {
    if (netRole() == NET_CLIENT) {
        if (!netReady()) { g_simMs = 0.0; return; }
        /* A joined game is still host-authoritative, but an inert replica can
           only display falling sand, fluids, machines, enemies and shots when
           their next packet arrives. Advance the replicated state locally for
           immediate presentation; state/chunk packets remain corrections and
           are the only data ever accepted by the host. The authoritative RNG
           seed in each state keeps ordinary stretches close, while generic
           chunk repair handles unavoidable divergence from unseen host input. */
        toolInstTick();
        publishServerRegions();
        LARGE_INTEGER begin, end; QueryPerformanceCounter(&begin);
        g_world.step();
        projUpdate(g_world);
        roomsTick(g_world);
        devTick(g_world);
        treesTick(g_world);
        dayAdvance();
        if (g_playerOn) {
            entTickPlayers(g_world);
            for (int slot = 0; slot < MAX_PLAYERS; ++slot) {
                PlayerSession& session = g_playerSessions[slot];
                if (!session.connected || !session.body.alive) continue;
                accessoryTickFor(slot, session.body, session.inventory);
                meleeTickFor(session);
                droneTickFor(slot, g_world, session.body, session.inventory);
            }
        }
        QueryPerformanceCounter(&end);
        g_simMs = 1000.0 * (double)(end.QuadPart - begin.QuadPart) /
                  (double)perfFrequency.QuadPart;
        return;
    }
    const bool onlineHost = netRole() == NET_HOST;
    const bool menuPausesWorld = g_menuOpen && !onlineHost;
    const bool uiPausesActors = !onlineHost &&
        (g_menuOpen || g_creativeOpen || g_craftOpen || g_chestOpen >= 0);

    toolInstTick();
    /* Survival always uses PlayerCommand. The direct brush remains only for
       the character-off creative sandbox, which has no player authority. */
    if (!(g_survival && g_playerOn) && !g_menuOpen && !g_creativeOpen &&
        !g_craftOpen && g_chestOpen < 0 && !g_mapOpen) applyBrush();

    publishServerRegions();
    bool singleStep = false;
    LARGE_INTEGER begin, end; QueryPerformanceCounter(&begin);
    if (g_stepOnce) {
        g_world.step(); g_stepOnce = false; singleStep = true;
    } else if (!g_paused && !menuPausesWorld) {
        for (int step = 0; step < SPEEDS[g_speedIdx]; ++step) g_world.step();
    }
    if (singleStep) projUpdate(g_world);
    else if (!g_paused && !menuPausesWorld)
        for (int step = 0; step < SPEEDS[g_speedIdx]; ++step) projUpdate(g_world);
    QueryPerformanceCounter(&end);
    g_simMs = 1000.0 * (double)(end.QuadPart - begin.QuadPart) /
              (double)perfFrequency.QuadPart;

    if (g_survival && g_playerOn) {
        const bool localCanMove = (!g_paused || singleStep) && !g_menuOpen &&
            !g_creativeOpen && !g_craftOpen && g_chestOpen < 0 && !g_mapOpen;
        updatePlayerFromCommand(0, g_playerSessions[0], g_localInput, localCanMove);
    }
    if (onlineHost) updateRemotePlayersFromCommands(!g_paused || singleStep);
    /* Closing is a group decision. Every connected body has published its
       latest occupancy by now, so one distant client cannot undo the nearby
       client's automatic open earlier in this same frame. */
    if ((!g_paused || singleStep) && g_survival && g_playerOn) doorAutoClose(g_world);
    processPlayerActions();
    refreshHostLogisticsPause();

    if (singleStep || (!g_paused && !menuPausesWorld)) {
        roomsTick(g_world);
        devTick(g_world);
        treesTick(g_world);
    }
    if (!g_paused && !uiPausesActors) {
        bool everyoneResting = true;
        for (int slot = 0; slot < MAX_PLAYERS; ++slot)
            if (g_playerSessions[slot].connected && g_playerSessions[slot].body.alive &&
                g_playerSessions[slot].restBed < 0) everyoneResting = false;
        for (int step = 0; step < (everyoneResting ? 4 : 1); ++step) dayAdvance();

        if (g_playerOn) {
            entTickPlayers(g_world);
            for (int slot = 0; slot < MAX_PLAYERS; ++slot) {
                PlayerSession& session = g_playerSessions[slot];
                if (!session.connected || !session.body.alive) continue;
                accessoryTickFor(slot, session.body, session.inventory);
                meleeTickFor(session);
                droneTickFor(slot, g_world, session.body, session.inventory);
            }
            if (g_survival) {
                static int spawnTurn = 0;
                for (int tries = 0; tries < MAX_PLAYERS; ++tries) {
                    const int slot = (spawnTurn + tries) % MAX_PLAYERS;
                    PlayerSession& session = g_playerSessions[slot];
                    if (!session.connected || !session.body.alive) continue;
                    spawnTurn = (slot + 1) % MAX_PLAYERS;
                    const int spawnCamX = (int)session.body.centreX() - viewCellsW() / 2;
                    const int spawnCamY = (int)session.body.centreY() - viewCellsH() / 2;
                    entSpawnTick(g_world, session.body, spawnCamX, spawnCamY, slot == 0);
                    break;
                }
            }
        } else {
            /* --- creatures without a character ------------------------
               They used to freeze solid the moment the character was
               switched off, which makes the sandbox half of the program
               useless for the one thing you would switch it off to do:
               watch something behave. A spawn egg produced a statue.

               Everything a creature does needs someone to do it RELATIVE
               to -- chase, flee, keep or lose interest -- so rather than
               teach every one of them a second mode with no target, the
               camera stands in for the character. Creatures head for where
               you are looking, which is both the useful behaviour for
               watching them and the honest reading of "the observer is
               over there".

               The stand-in is NOT alive, and that one flag is what keeps it
               from being a character in every other respect: nothing takes
               contact damage, nothing collects the drops, and no hit ever
               lands on an inventory that is not in the world.

               Single-player only by construction: with the character off
               there are no connected sessions to tick, and a joined game
               always has one. */
            Player observer = g_player;
            observer.alive = false;
            observer.x = (float)(g_camX + viewCellsW() / 2);
            observer.y = (float)(g_camY + viewCellsH() / 2);
            entTick(g_world, observer, g_inv);
        }
    }
    if (onlineHost) netHostFrame(g_world);
}

static void clientCameraTick() {
    if (!g_playerOn && !g_menuOpen && !g_creativeOpen && !g_craftOpen && g_chestOpen < 0) {
        const float pan = 6.0f;
        float dx = 0.0f, dy = 0.0f;
        if (keyHeld('A') || keyHeld(VK_LEFT)) dx -= pan;
        if (keyHeld('D') || keyHeld(VK_RIGHT)) dx += pan;
        if (keyHeld('W') || keyHeld(VK_UP)) dy -= pan;
        if (keyHeld('S') || keyHeld(VK_DOWN)) dy += pan;
        if (dx != 0.0f || dy != 0.0f) panCamera(dx, dy);
    }
    remoteVisualTick();
    updateCamera(false);
}

static void clientRender(HWND hwnd) {
    /* Light is computed for this camera position and consumed immediately
       by renderView. The two must agree about where the camera is, which
       is why this sits here and not up beside the sim step. */
    lightClearDynamic();
    droneRegisterLights();
    accessoryRegisterLights();
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
    for (int slot = 1; slot < MAX_PLAYERS; ++slot)
        if (g_playerSessions[slot].connected)
            (netRole() == NET_CLIENT && g_remoteVisualValid[slot]
                ? g_remoteVisual[slot] : g_playerSessions[slot].body)
                .draw(g_pixels, g_camX, g_camY, g_lightOn);
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
    /* --- the save thumbnail, taken HERE ---------------------------------
       The last moment g_pixels holds the world and nothing else. Everything
       above this line is the place you are standing in; everything below is
       HUD -- the map, the minimap, the modal dimming -- and a save preview with
       the minimap baked into its corner is a preview of the interface rather
       than of the world. The first cut captured at save time, which is after
       all of it, and the little grey minimap box in every thumbnail is what
       gave it away.

       Every THUMB_EVERY frames rather than on demand, because "on demand" is
       exactly what cannot work: the save happens on a click, and by then the
       frame has already been finished and overlaid. Half a second of staleness
       in a picture of a landscape is invisible; the cost is one 196k-sample
       average twice a second, which is far less than a single frame of
       lighting. */
    {
        static int thumbTick = 0;
        static const int THUMB_EVERY = 30;
        if (--thumbTick <= 0) { thumbTick = THUMB_EVERY; captureThumbnail(g_thumbLatest); }
    }

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
    g_hoverLabel = 0;
    /* Re-laid every frame, not only when the panel is clicked. With a
       character on, the strip is a view of the PACK, and the pack changes on
       every dug cell -- a list rebuilt only on interaction would be showing
       what you were carrying the last time you touched the panel. It is a few
       dozen SetRects and one pass over forty slots, which is nothing against a
       frame that also lights and renders the world. */
    layoutPanel();
    drawPanel(g_backDC);
    if (g_survival && g_playerOn) drawHotbar(g_backDC);
    if (g_restBed >= 0) {
        RECT rest = { PANEL_W + 16, VIEW_H - 34, WIN_W - 16, VIEW_H - 14 };
        SetBkMode(g_backDC, TRANSPARENT);
        SetTextColor(g_backDC, RGB(226, 190, 90));
        DrawTextA(g_backDC, "RESTING  -  respawn set  -  time passes 4x  -  move to wake", -1,
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
    if (g_saveScreen != SAVESCREEN_OFF) drawSaveScreen(g_backDC);
    drawItemTooltip(g_backDC);

    HDC hdc = GetDC(hwnd);
    RECT client; GetClientRect(hwnd, &client);
    /* Paint only the letterbox. Clearing the whole client immediately before
       StretchBlt exposed a complete black frame whenever the compositor
       presented between those two GDI calls -- most visibly while walking,
       when every frame changed. The playfield itself must be replaced by the
       single blit below. */
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RECT bar = { client.left, client.top, client.right, g_presentRect.top };
    if (bar.bottom > bar.top) FillRect(hdc, &bar, black);
    bar.left = client.left; bar.top = g_presentRect.bottom;
    bar.right = client.right; bar.bottom = client.bottom;
    if (bar.bottom > bar.top) FillRect(hdc, &bar, black);
    bar.left = client.left; bar.top = g_presentRect.top;
    bar.right = g_presentRect.left; bar.bottom = g_presentRect.bottom;
    if (bar.right > bar.left) FillRect(hdc, &bar, black);
    bar.left = g_presentRect.right; bar.right = client.right;
    if (bar.right > bar.left) FillRect(hdc, &bar, black);
    SetStretchBltMode(hdc, COLORONCOLOR);
    StretchBlt(hdc, g_presentRect.left, g_presentRect.top,
               g_presentRect.right - g_presentRect.left,
               g_presentRect.bottom - g_presentRect.top,
               g_backDC, 0, 0, WIN_W, WIN_H, SRCCOPY);
    ReleaseDC(hwnd, hdc);
}

/* Headless proof that offline play really traverses the same command/action
   authority used by a joined peer. Kept behind a command-line switch so the
   production executable itself—not a rewritten test double—covers placement,
   crafting, and inventory transfer. */
static int runLocalCommandSmoke() {
    netStop(); g_world.reset(); devClear(); playerSessionsReset();
    g_survival = true; g_playerOn = true;
    g_player.reset(400.0f, 400.0f); g_inv.clear();

    g_inv.add((ItemId)MAT_WOOD, 4); g_inv.add((ItemId)MAT_COAL, 1);
    if (!sendClientAction(NACT_CRAFT, 0, 0, 1)) return 201;
    processPlayerActions();
    if (g_inv.countOf(ITEM_TORCH_DEV) != 4) return 202;

    g_inv.clear(); g_drag = ItemStack(); g_inv.add((ItemId)MAT_SAND, 5);
    int sandSlot = -1;
    for (int i = 0; i < INV_SLOTS; ++i) if (g_inv.slot[i].item == (ItemId)MAT_SAND) { sandSlot = i; break; }
    if (sandSlot < 0 || !sendClientAction(NACT_SLOT, NSLOT_PACK, (u8)sandSlot)) return 203;
    processPlayerActions();
    if (g_drag.item != (ItemId)MAT_SAND || g_drag.count != 5 || !g_inv.slot[sandSlot].empty()) return 204;

    g_drag = ItemStack(); g_inv.clear(); g_inv.add((ItemId)MAT_STONE, 100); g_inv.selected = 0;
    PlayerCommand command; memset(&command, 0, sizeof(command));
    command.player = LOCAL_PLAYER_ID; command.generation = g_playerSessions[0].generation;
    command.bits = command.pressed = PCMD_USE_LEFT; command.selected = 0;
    command.brushRadius = 1; command.brush = MAT_STONE;
    command.aimX = (int)g_player.centreX() + 24; command.aimY = (int)g_player.centreY();
    updatePlayerFromCommand(0, g_playerSessions[0], command, false);
    if (g_world.at(command.aimX, command.aimY).mat != MAT_STONE) return 205;

    g_inv.add((ItemId)MAT_STONE, 100);
    memset(&command, 0, sizeof(command));
    command.player = LOCAL_PLAYER_ID; command.generation = g_playerSessions[0].generation;
    command.selected = 0; command.brushRadius = 1; command.brush = MAT_STONE;
    command.lineCommit = true; command.lineCommitBits = PCMD_USE_LEFT;
    command.lineStartX = (int)g_player.centreX() + 12;
    command.lineStartY = (int)g_player.centreY() - 8;
    command.aimX = command.lineStartX + 12; command.aimY = command.lineStartY;
    updatePlayerFromCommand(0, g_playerSessions[0], command, false);
    if (g_world.at(command.lineStartX, command.lineStartY).mat != MAT_STONE ||
        g_world.at(command.aimX, command.aimY).mat != MAT_STONE) return 206;

    /* A joined peer can deliver the discrete interaction edge beside a stale
       held-right bit. That packet still represents one click and must toggle a
       door exactly once, not open then immediately close it. */
    g_world.reset();
    const int smokeDoorX = (int)g_player.centreX() + 24;
    const int smokeDoorY = (int)g_player.centreY();
    g_world.setCell(smokeDoorX, smokeDoorY, MAT_DOOR);
    memset(&command, 0, sizeof(command));
    command.player = LOCAL_PLAYER_ID; command.generation = g_playerSessions[0].generation;
    command.bits = command.pressed = PCMD_INTERACT | PCMD_USE_RIGHT;
    command.aimX = smokeDoorX; command.aimY = smokeDoorY;
    updatePlayerFromCommand(0, g_playerSessions[0], command, false);
    if (g_world.at(smokeDoorX, smokeDoorY).mat != MAT_DOOR_OPEN) return 216;
    memset(&command, 0, sizeof(command));
    command.player = LOCAL_PLAYER_ID; command.generation = g_playerSessions[0].generation;
    updatePlayerFromCommand(0, g_playerSessions[0], command, false);
    command.bits = command.pressed = PCMD_USE_RIGHT;
    command.aimX = smokeDoorX; command.aimY = smokeDoorY;
    updatePlayerFromCommand(0, g_playerSessions[0], command, false);
    if (g_world.at(smokeDoorX, smokeDoorY).mat != MAT_DOOR) return 217;

    /* Death owns its destination and its clock. Exercise the actual authority
       path for all 600 frames, first with a live bed and then with that bed
       removed so the checkpoint must fall back to world spawn. */
    g_world.reset(); devClear();
    if (!devPlace(g_world, DEV_BED, 400, 400)) return 207;
    g_player.reset(400.0f, 390.0f);
    Aim bedAim; bedAim.x = bedAim.ghostX = 400; bedAim.y = bedAim.ghostY = 400; bedAim.clamped = false;
    interactFor(g_playerSessions[0], bedAim);
    if (g_playerSessions[0].restBed < 0 || g_playerSessions[0].respawnBedX < 0) return 208;
    const char* respawnSave = "build\\respawn-smoke.tmp";
    if (!saveWrite(respawnSave, g_world)) return 212;
    const int savedBedX = g_playerSessions[0].respawnBedX;
    const int savedBedY = g_playerSessions[0].respawnBedY;
    g_playerSessions[0].respawnBedX = g_playerSessions[0].respawnBedY = -1;
    if (!saveRead(respawnSave, g_world) || g_playerSessions[0].respawnBedX != savedBedX ||
        g_playerSessions[0].respawnBedY != savedBedY) {
        remove(respawnSave); return 213;
    }
    remove(respawnSave);
    g_playerSessions[0].restBed = -1;
    g_player.damage((float)PLAYER_HP_MAX * 2.0f);
    memset(&command, 0, sizeof(command));
    command.player = LOCAL_PLAYER_ID; command.generation = g_playerSessions[0].generation;
    for (int frame = 0; frame < RESPAWN_DELAY_FRAMES - 1; ++frame)
        updatePlayerFromCommand(0, g_playerSessions[0], command, false);
    if (g_player.alive || g_playerSessions[0].respawnFrames != 1) return 209;
    updatePlayerFromCommand(0, g_playerSessions[0], command, false);
    Device* bed = devAt(400, 400);
    if (!g_player.alive || !bed || fabsf(g_player.centreX() - (bed->x + DEV_W * 0.5f)) > 0.1f)
        return 210;

    devRemove(g_world, bed);
    const int worldSpawnX = SIM_W / 5; g_surfaceY[worldSpawnX] = 300;
    g_player.damage((float)PLAYER_HP_MAX * 2.0f);
    for (int frame = 0; frame < RESPAWN_DELAY_FRAMES; ++frame)
        updatePlayerFromCommand(0, g_playerSessions[0], command, false);
    if (!g_player.alive || fabsf(g_player.centreX() - (float)worldSpawnX) > 0.1f ||
        g_playerSessions[0].respawnBedX >= 0) return 211;

    memset(&command, 0, sizeof(command));
    command.player = LOCAL_PLAYER_ID; command.generation = g_playerSessions[0].generation;
    command.bits = command.pressed = PCMD_RESPAWN; command.aimX = 600; command.aimY = 620;
    updatePlayerFromCommand(0, g_playerSessions[0], command, false);
    if (!g_player.alive || fabsf(g_player.centreX() - 600.0f) > 0.1f ||
        fabsf(g_player.centreY() - 620.0f) > 0.1f) return 214;
    g_player.damage((float)PLAYER_HP_MAX * 2.0f);
    updatePlayerFromCommand(0, g_playerSessions[0], command, false);
    if (g_player.alive || g_playerSessions[0].respawnFrames != RESPAWN_DELAY_FRAMES - 1)
        return 215;

    /* --- the device panel with the character switched OFF ---------------
       Reported from play: a spout's facing could not be changed with the
       player off. The panel drew, the button drew, and nothing happened,
       because the sandbox path set only the UI half of "which machine is
       open" and applyDeviceAction reads the other half.

       Checked HERE rather than in tests/, because the bug lives in main.cpp
       and the harnesses do not compile it -- which is the whole reason this
       switch exists. It exercises the real action queue, not a double. */
    g_playerOn = false;
    devClear();
    if (!devPlace(g_world, DEV_SPOUT, 800, 800)) return 216;
    Device* spout = devAt(800, 800);
    if (!spout) return 217;
    const int spoutIndex = (int)(spout - g_devices);
    const u8 before = spout->face;

    setDevicePanel(spoutIndex);
    if (g_playerSessions[0].openDevice != spoutIndex) return 218;

    if (!sendClientAction(NACT_DEVICE, 0, NDEV_TURN)) return 219;
    processPlayerActions();
    if (spout->face == before) return 220;

    /* And the other controls in the same panel, which were inert for exactly
       the same reason -- the facing was just the one that got noticed.

       DEC rather than INC: a spout's rate defaults to its own vMax of 14, so
       INC correctly clamps and changes nothing, which would fail this for a
       reason that has nothing to do with the bug. */
    const i32 valueBefore = spout->value;
    if (!sendClientAction(NACT_DEVICE, 0, NDEV_DEC)) return 221;
    processPlayerActions();
    if (spout->value == valueBefore) return 222;

    setDevicePanel(-1);
    if (g_playerSessions[0].openDevice != -1) return 223;
    g_playerOn = true;

    puts("local command loopback smoke passed");
    return 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR commandLine, int) {
    uiSettingsLoad();
    initMaterials();
    g_world.reset();
    initItems();
    playerSessionsReset();
    layoutPanel();
    layoutHotbar();
    if (commandLine && strstr(commandLine, "--command-smoke")) return runLocalCommandSmoke();

    const char* joinSwitch = commandLine ? strstr(commandLine, "--join ") : 0;
    const bool emptyTestHost = commandLine && strstr(commandLine, "--host-empty");
    const bool savedTestHost = commandLine && strstr(commandLine, "--host-save");
    /* A joining process is about to replace every world cell with the host's
       compressed snapshot. Generating a full throwaway world first made a CLI
       join look hung for more than a minute and doubled startup CPU/memory.
       Normal/menu launches still build their local world exactly as before. */
    if (!joinSwitch && !emptyTestHost && !savedTestHost) {
        /* Start near the top middle. The world is four screens wide and eight
           deep, so the old middle-of-world spawn was several screens underground. */
        makeWorld();
        float sx, sy;
        worldSpawnPoint(&sx, &sy);
        g_player.reset(sx, sy);
    } else {
        g_player.reset(emptyTestHost ? 400.0f : 0.0f, emptyTestHost ? 400.0f : 0.0f);
        if (emptyTestHost) {
            for (int y = 430; y < 438; ++y)
                for (int x = 280; x <= 520; ++x) g_world.setCell(x, y, MAT_STONE);
            g_inv.clear();
            g_inv.add((ItemId)MAT_STONE, 200);
            g_inv.add((ItemId)MAT_SAND, 200);
            g_inv.add((ItemId)MAT_WATER, 200);
            g_inv.add(ITEM_TORCH_DEV, 40);
        }
        if (savedTestHost && !saveRead("build\\cinderlift.sav", g_world)) {
            makeWorld();
            float sx, sy; worldSpawnPoint(&sx, &sy); g_player.reset(sx, sy);
        }
    }
    /* Useful both for repeatable two-process testing and for a host that wants
       a shortcut. The menu remains the normal player-facing route. */
    if (commandLine && strstr(commandLine, "--host")) {
        g_survival = true; g_playerOn = true; netHost();
    } else if (joinSwitch) {
        const char* join = joinSwitch;
        {
            char ip[64]; int n = 0; join += 7;
            while (*join == ' ') ++join;
            while (*join && *join != ' ' && n < (int)sizeof(ip) - 1) ip[n++] = *join++;
            ip[n] = 0;
            if (n) { g_survival = true; g_playerOn = true; netJoin(ip); }
        }
    }
    updateCamera(true);

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = wndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "CinderliftWnd";
    RegisterClassA(&wc);

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT r = { 0, 0, WIN_W, WIN_H };
    AdjustWindowRect(&r, style, FALSE);

    /* Several local clients otherwise share one title, which makes a
       four-window loopback session impossible to tell apart on screen. */
    char windowTitle[64];
    strcpy(windowTitle, (savedTestHost || emptyTestHost) ? "Cinderlift - LOCAL HOST" :
                        joinSwitch ? "Cinderlift - LOCAL CLIENT" : "Cinderlift");
    const char* labelSwitch = commandLine ? strstr(commandLine, "--label ") : 0;
    if (labelSwitch) sprintf(windowTitle + strlen(windowTitle), " %d", atoi(labelSwitch + 8));
    HWND hwnd = CreateWindowA("CinderliftWnd", windowTitle, style,
                              CW_USEDEFAULT, CW_USEDEFAULT,
                              r.right - r.left, r.bottom - r.top,
                              NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;
    g_hwnd = hwnd;   /* keyHeld() compares this against the foreground window */
    updatePresentRect(hwnd);
    ShowWindow(hwnd, SW_SHOW);

    rebuildUiFont();

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

        LARGE_INTEGER tWorkBegin;
        QueryPerformanceCounter(&tWorkBegin);

        clientInputTick();
        serverTick(freq);
        syncClientDeviceUi();
        clientCameraTick();

        clientRender(hwnd);

        /* Pace to 60 Hz. */
        ++fpsFrames;
        LARGE_INTEGER tNow;
        QueryPerformanceCounter(&tNow);
        {
            const double work = 1000.0 * (double)(tNow.QuadPart - tWorkBegin.QuadPart) /
                                (double)freq.QuadPart;
            /* Smoothed, because a single frame that happened to include a light
               recut or a chunk save says nothing about the steady state. */
            g_frameMs = g_frameMs > 0.0 ? g_frameMs * 0.9 + work * 0.1 : work;
        }
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
