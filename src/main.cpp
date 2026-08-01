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
    /* The heat ladder. Clay and ceramic sit together because one becomes the
       other; coal and fuel likewise. The burning forms (ember, fuelfire) are NOT
       placeable, on the same line already drawn around molten metal and slag --
       they are states you put a material into, not things you build with. */
    { MAT_CLAY,    "Clay"   },
    { MAT_CERAMIC, "Ceramic"},
    { MAT_COAL,    "Coal"   },
    { MAT_FUEL,    "Fuel"   },
    { MAT_GRAPHENE,"Graphene"},
    { MAT_LAVA,  "Lava"  },
    { MAT_FIRE,  "Fire"  },
    { MAT_PLASMA,"Plasma"},
    { MAT_COLDFIRE,"Cold Fire"},
    { MAT_NITROGEN,"Liquid N2"},
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
    { MAT_LAMP,  "Lamp"  },
    { MAT_HEATER,"Heater"},
    { MAT_COOLER,"Cooler"},
    { TOOL_HEAT, "Heat"  },
    { TOOL_COOL, "Cool"  },
    { MAT_EMPTY, "Erase" },
};
static const int N_BRUSH = (int)(sizeof(BRUSHES) / sizeof(BRUSHES[0]));

enum ActionId { ACT_OVERWRITE, ACT_LAYER, ACT_VIEW, ACT_LIGHT, ACT_PLAYER, ACT_PAUSE, ACT_CLEAR, N_ACT };

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

/* Layout rects, filled once by layoutPanel(). */
static RECT g_brushRect[N_BRUSH];
static RECT g_actRect[N_ACT];
static RECT g_sizeDec, g_sizeInc, g_sizeBox;
static RECT g_speedRect[N_SPEED];

/* GDI objects, all created once -- object churn per frame is not free. */
static HBRUSH g_panelBg, g_btnBg, g_btnBgHot, g_btnBgSel, g_borderBrush, g_accentBrush;
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
    ReleaseDC(NULL, screen);
}

/* Centres an item's icon in a rect, or falls back to a colour swatch for
   everything without one -- which is every material, deliberately. */
static void drawItemIcon(HDC hdc, const RECT& r, ItemId item) {
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

/* UI/input state */
static bool g_uiCapture = false;   /* the click landed on the panel, not the sim */
static bool g_overwrite = true;    /* false = brush only fills empty space */
static int  g_view      = VIEW_NORMAL;
static bool g_lmb = false, g_rmb = false;

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
/* One device per press, not per frame. Cleared on button-up -- see the placement
   branch in applyBrush for why holding must not repeat. */
static bool g_devPlaced = false;
/* Which device's panel is open, or -1. An index rather than a pointer so that a
   device being dug out from under an open panel cannot leave a dangling one --
   devTick can remove a device at any time, and the panel revalidates by index
   every frame it draws. */
static int  g_devPanel = -1;
bool g_logisticsUiOpen = false;
static bool handleDevPanelClick(int mx, int my);
static bool handleCraftClick(int mx, int my);
static void layoutCraft();
extern bool g_craftOpen;
static void drawDevPanel(HDC hdc);
static int  g_mx = 0, g_my = 0;      /* current mouse, window pixels */
static int  g_pmx = -1, g_pmy = -1;  /* previous aim point, in cells */
static int  g_brushMat = MAT_SAND;
static int  g_brushRadius = 6;
static bool g_paused = false;
static bool g_stepOnce = false;
static int  g_speedIdx = 0;      /* index into SPEEDS */
/* The character can be switched off, because the sandbox this grew out of is
   still worth having on its own -- and because a figure standing in the middle
   of a scene you are trying to draw is a nuisance. */
static bool g_playerOn = true;
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
static int g_creScroll = 0;      /* first visible row */
static int g_creRowCount = 0;    /* total rows, for clamping and the bar */
static RECT g_creTrack, g_creThumb;
static RECT g_creRect[ITEM_COUNT];
static RECT g_crePanel, g_creClear;
static RECT g_creSearchBox;
static int  g_creCount = 0;              /* entries actually laid out */
static ItemId g_creItem[ITEM_COUNT];     /* which item each rect belongs to */
static char g_creSearch[32] = "";
static bool g_creSearchFocus = false;
static int  g_filterDevice = -1;         /* drain/watcher material picker */

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
static int  g_toolPackSlot  = -1;   /* which inventory slot the bench is showing */

/* Equipment slots, on the same screen and for the same reason as the tool
   bench: putting a jetpack on is a transfer between two containers, and both
   have to be visible for click-to-move to say what a drag would. Always drawn,
   unlike the bench -- an empty equipment row tells you the slots exist and what
   goes in them, whereas an absent tool bench tells you nothing is missing. */
static RECT g_eqRect[EQ_COUNT];

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
static void applyBrush();

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
static ToolSpec digSpec() { return miningSpec(g_inv.held()); }
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

    /* Materials go in TWO columns, everything below them stays full width. The
       split is what buys the height: 25 buttons in one column forced a 20px
       pitch, 13 rows of two leaves room for 30. */
    const int brushRows = (N_BRUSH + 1) / 2;
    const int rowCount  = brushRows + 2 + N_ACT;   /* +2: size row, speed row */

    int pitch = (statsTop - top - sepTotal) / rowCount;
    if (pitch > 30) pitch = 30;
    if (pitch < 14) pitch = 14;   /* past this the labels are unreadable anyway;
                                     better to overflow visibly than to compute
                                     a zero or negative row height */
    const int gap = 4;
    const int h = pitch - gap;
    int y = top;

    /* Column-major, so the palette's grouping survives the split: the first
       half of BRUSHES fills the left column top to bottom and the second half
       the right, keeping related materials next to each other vertically
       rather than interleaving them across the two columns. */
    {
        const int colGap = 8;
        const int colW   = (w - colGap) / 2;
        for (int i = 0; i < N_BRUSH; ++i) {
            const int col = i / brushRows;
            const int row = i % brushRows;
            const int x0  = pad + col * (colW + colGap);
            SetRect(&g_brushRect[i], x0, top + row * pitch, x0 + colW, top + row * pitch + h);
        }
        y = top + brushRows * pitch;
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
static void layoutCreative() {
    g_creCount = 0;
    for (int i = 0; i < ITEM_COUNT; ++i) {
        if (ITEMS[i].maxStack == 0) continue;   /* air, and anything unfinished */
        bool match = true;
        for (int a = 0; g_creSearch[a]; ++a) {
            bool found = false;
            for (int b = 0; ITEMS[i].name[b]; ++b) {
                char x = ITEMS[i].name[b], q = g_creSearch[a];
                if (x >= 'A' && x <= 'Z') x = (char)(x + ('a' - 'A'));
                if (q >= 'A' && q <= 'Z') q = (char)(q + ('a' - 'A'));
                if (x == q) { found = true; break; }
            }
            if (!found) { match = false; break; }
        }
        if (!match) continue;
        g_creItem[g_creCount++] = (ItemId)i;
    }
    /* The bench only appears when there is a tool to show, and its height is
       part of the panel's height rather than an overlay -- so picking up a
       multitool makes the window taller instead of pushing the grid under it. */
    g_toolPackSlot   = g_inv.firstToolSlot();
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
    const int equipH = 62;
    const int paletteH = visRows * (ch + gap);
    const int packH    = 22 + INV_ROWS * (ps + pgap) + 10;
    const int barW     = 10;
    const int w = pad * 2 + CRE_COLS * cw + (CRE_COLS - 1) * gap + barW + 6;
    const int h = pad + 56 + paletteH + 10 + packH + equipH + benchH + 38;
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
    for (int i = 0; i < INV_SLOTS; ++i) {
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
    for (int i = 0; i < EQ_COUNT; ++i)
        SetRect(&g_eqRect[i], x0 + pad + i * 40, eqY, x0 + pad + i * 40 + 34, eqY + 34);

    /* Module slots: square, and noticeably bigger than a grid row, because they
       are the one place on this screen where the arrangement carries meaning
       (slot order decides which module is the shot). */
    const int by2 = eqY + equipH;
    for (int i = 0; i < g_toolSlotCount; ++i) {
        const int bx = x0 + pad + i * 40;
        SetRect(&g_toolSlotRect[i], bx, by2, bx + 34, by2 + 34);
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
    if (!g_drag.empty() && !equipFits(g_drag.item, eqSlot)) return false;
    /* Never split into or out of a worn slot: you wear one of a thing. */
    if (right) return false;
    return slotClick(eq, false);
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
static bool handleCreativeClick(int mx, int my, bool remove) {
    if (inRect(g_creSearchBox, mx, my)) { g_creSearchFocus = true; return true; }
    if (inRect(g_creClear, mx, my)) {
        if (g_filterDevice >= 0) {
            g_devices[g_filterDevice].value = 0;
            g_filterDevice = -1; g_creativeOpen = false; g_creSearchFocus = false;
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

    for (int i = 0; i < INV_SLOTS; ++i)
        if (inRect(g_packRect[i], mx, my)) {
            slotClick(g_inv.slot[i], remove);
            /* Picking a tool up or putting one down changes whether the bench
               exists, which changes the panel height. */
            layoutCreative();
            return true;
        }

    for (int i = 0; i < EQ_COUNT; ++i)
        if (inRect(g_eqRect[i], mx, my)) { equipClick(i, remove); layoutCreative(); return true; }

    if (g_toolPackSlot >= 0 && g_inv.slot[g_toolPackSlot].inst) {
        ToolInst& ti = g_toolInst[g_inv.slot[g_toolPackSlot].inst];
        for (int i = 0; i < g_toolSlotCount; ++i)
            if (inRect(g_toolSlotRect[i], mx, my)) { moduleClick(ti.slot[i], remove); return true; }
    }

    for (int i = 0; i < g_creCount; ++i) {
        if (!inRect(g_creRect[i], mx, my)) continue;
        const ItemId it = g_creItem[i];
        if (g_filterDevice >= 0) {
            if (it < MAT_COUNT) g_devices[g_filterDevice].value = it;
            g_filterDevice = -1; g_creativeOpen = false; g_creSearchFocus = false;
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
        tx = g_player.centreX() - VIEW_CELLS_W * 0.5f;
        ty = g_player.centreY() - VIEW_CELLS_H * 0.5f;
    }

    /* Clamp the TARGET, not the eased position, so easing never has to chase a
       point outside the world and stall against the edge. */
    const float maxX = (float)(SIM_W - VIEW_CELLS_W);
    const float maxY = (float)(SIM_H - VIEW_CELLS_H);
    if (tx < 0.0f) tx = 0.0f;
    if (tx > maxX) tx = maxX;
    if (ty < 0.0f) ty = 0.0f;
    if (ty > maxY) ty = maxY;

    const float dx = tx - g_camXf, dy = ty - g_camYf;
    const float far2 = dx * dx + dy * dy;
    if (snap || far2 > (float)(VIEW_CELLS_W * VIEW_CELLS_W)) {
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
    const float maxX = (float)(SIM_W - VIEW_CELLS_W);
    const float maxY = (float)(SIM_H - VIEW_CELLS_H);
    if (g_camXf < 0.0f) g_camXf = 0.0f;
    if (g_camXf > maxX) g_camXf = maxX;
    if (g_camYf < 0.0f) g_camYf = 0.0f;
    if (g_camYf > maxY) g_camYf = maxY;
    g_camX = (int)g_camXf; g_camY = (int)g_camYf;
}

static void cycleView()      { g_view = (g_view + 1) % VIEW_COUNT; }
static void changeSize(int d){ g_brushRadius = imax(1, imin(64, g_brushRadius + d)); }

/* Builds the world. See worldgen.cpp -- plains to the left, a mountain to the
   right, and the flats beyond it. */
static void makeWorld() {
    /* Machines are entities beside the grid, so clearing the world does not clear
       them -- they have to be dropped explicitly or a fresh world arrives haunted
       by the last one's contraptions. Same reason roomsClear() exists. */
    devClear();
    sparkClear();
    generateWorld(g_world);
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
    for (int i = 0; i < N_BRUSH; ++i) {
        if (inRect(g_brushRect[i], mx, my)) { g_brushMat = BRUSHES[i].brush; return true; }
    }
    for (int i = 0; i < N_SPEED; ++i) {
        if (inRect(g_speedRect[i], mx, my)) { g_speedIdx = i; return true; }
    }
    if (inRect(g_sizeDec, mx, my)) { changeSize(-1); return true; }
    if (inRect(g_sizeInc, mx, my)) { changeSize(+1); return true; }
    if (inRect(g_actRect[ACT_OVERWRITE], mx, my)) { g_overwrite = !g_overwrite; return true; }
    if (inRect(g_actRect[ACT_LAYER],     mx, my)) { g_bgLayer   = !g_bgLayer;   return true; }
    if (inRect(g_actRect[ACT_VIEW],      mx, my)) { cycleView();                return true; }
    if (inRect(g_actRect[ACT_LIGHT],     mx, my)) { g_lightOn  = !g_lightOn;    return true; }
    if (inRect(g_actRect[ACT_PLAYER],    mx, my)) {
        g_playerOn = !g_playerOn;
        /* Into the middle of the VIEW, not the middle of the world -- switching
           the character on should put them where you are looking. */
        if (g_playerOn) {
            g_player.reset((float)(g_camX + VIEW_CELLS_W / 2),
                           (float)(g_camY + VIEW_CELLS_H / 2));
            updateCamera(true);
        }
        return true;
    }
    if (inRect(g_actRect[ACT_PAUSE],     mx, my)) { g_paused = !g_paused;       return true; }
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
        else                                   { g_lmb = true; startLine(); }
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        commitLine();       /* before the button clears -- see commitLine */
        g_lmb = false; g_devPlaced = false; g_uiCapture = false; ReleaseCapture(); return 0;
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
                if (d->type == DEV_CHEST) { openChest((int)(d - g_devices)); break; }
                /* Toggle: clicking the same machine again closes it. */
                const int idx = (int)(d - g_devices);
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
        case 'M': g_brushMat = MAT_COPPER;   break;   /* M for metal */
        case 'G': g_brushMat = MAT_GRAPHENE; break;
        case 'B': g_brushMat = MAT_WALL;  break;
        case 'E': g_brushMat = MAT_EMPTY; break;
        case 'I': g_survival = !g_survival; break;
        case 'H': g_brushMat = TOOL_HEAT; break;
        case 'J': g_brushMat = TOOL_COOL; break;
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
        case VK_OEM_PERIOD: g_stepOnce = true; break;
        case VK_OEM_4: changeSize(-1); break;  /* [ */
        case VK_OEM_6: changeSize(+1); break;  /* ] */
        case VK_TAB:
            g_creativeOpen = !g_creativeOpen;
            if (g_creativeOpen) { g_creSearchFocus = true; layoutCreative(); g_lmb = g_rmb = false; }
            else { g_filterDevice = -1; g_creSearchFocus = false; dragStow(); }
            break;
        /* Escape backs out of the creative grid before it reaches the pause
           menu -- one key that always means "close the thing in front of me" is
           worth more than a second binding to remember. */
        case VK_ESCAPE:
            if (g_chestOpen >= 0)    closeChest();
            else if (g_craftOpen)    g_craftOpen = false;
            else if (g_creativeOpen) { g_creativeOpen = false; g_filterDevice = -1; g_creSearchFocus = false; dragStow(); }
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
                g_player.reset((float)((g_mx - PANEL_W) / SCALE + g_camX),
                               (float)(g_my / SCALE + g_camY));
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
    a.ghostX = (g_mx - PANEL_W) / SCALE + g_camX;
    a.ghostY = g_my / SCALE + g_camY;
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
    ti.cooldown = s.delay;

    const float pcx = g_player.centreX(), pcy = g_player.centreY();
    float dx = (float)aim.x - pcx, dy = (float)aim.y - pcy;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) { dx = 1.0f; dy = 0.0f; d = 1.0f; }   /* aimed at own feet */
    dx /= d; dy /= d;

    /* Start clear of the body. Spawning at the centre would put the shot inside
       whatever the character is standing in, so firing while waist-deep in sand
       would spend the whole shot on the cells around your own legs. */
    const float MUZZLE = PLAYER_H * 0.5f + 2.0f;
    const float SPEED  = 3.5f;
    projSpawn(pcx + dx * MUZZLE, pcy + dy * MUZZLE, dx * SPEED, dy * SPEED,
              s.power, s.pierce, 90, s.colour, s.blast);
}

static void applyBrush() {
    if (g_uiCapture || (!g_lmb && !g_rmb)) { g_pmx = -1; return; }

    /* A line drag lays nothing down until it is released. The anchor is already
       in g_pmx/g_pmy (set on the press), and leaving it there is what makes the
       committing call draw a straight line rather than the path the mouse
       wandered along. */
    if (g_lineOn) return;

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
    /* Holding a tool replaces the build verb with the fire verb. Digging stays
       on the right button either way -- you can always claw at the wall, and a
       tool that took away your hands would be a strange upgrade. */
    if (g_survival && g_playerOn && g_lmb && !g_rmb
        && !g_inv.held().empty() && ITEMS[g_inv.held().item].kind == ITEMK_TOOL) {
        fireTool(aim);
        g_pmx = aim.x; g_pmy = aim.y;
        return;
    }

    /* Machines place a RECTANGLE, snapped to a lattice, so they get their own
       branch for the same reason seeds do: nothing about it is a brush stroke.
       Deliberately one per click rather than per frame held -- a device is a
       discrete object and a held button should not carpet the world with them,
       which is what stroking would do at 60 a second. */
    if (g_survival && g_playerOn && g_lmb && !g_rmb && !g_bgLayer
        && !g_inv.held().empty() && ITEMS[g_inv.held().item].kind == ITEMK_DEVICE) {
        if (!g_devPlaced) {
            const u8 dt = ITEMS[g_inv.held().item].deviceType;
            /* Charged only if it actually went down -- inv.take() is the same
               door seeds use, so a refused placement costs nothing. */
            if (devPlace(g_world, dt, aim.x, aim.y)) g_inv.take(g_inv.held().item, 1);
            g_devPlaced = true;
        }
        g_pmx = aim.x; g_pmy = aim.y;
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
            digInto(g_world, g_inv, aim.x, aim.y, digRadius(), d.cellsPerBite, d.plantsOnly);
            g_digCool = d.cooldown;
        }
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
static const int DEVP_W = 260, DEVP_H = 96;
static RECT g_devpBox, g_devpDec, g_devpInc, g_devpTake, g_devpTurn, g_devpClose;

static void layoutDevPanel(const Device& d) {
    /* Sit it just above and right of the machine, in screen pixels. */
    int px = PANEL_W + (d.x + DEV_W - g_camX) * SCALE + 8;
    int py = (d.y - g_camY) * SCALE - DEVP_H - 6;
    if (px + DEVP_W > WIN_W - 6) px = PANEL_W + (d.x - g_camX) * SCALE - DEVP_W - 8;
    if (px < PANEL_W + 6)        px = PANEL_W + 6;
    if (py < 6)                  py = (d.y + DEV_H - g_camY) * SCALE + 6;
    if (py + DEVP_H > WIN_H - 6) py = WIN_H - 6 - DEVP_H;

    SetRect(&g_devpBox, px, py, px + DEVP_W, py + DEVP_H);
    const int by = py + DEVP_H - 30;
    SetRect(&g_devpDec,   px + 10,  by, px + 40,  by + 22);
    SetRect(&g_devpInc,   px + 44,  by, px + 74,  by + 22);
    SetRect(&g_devpTake,  px + 82,  by, px + 142, by + 22);
    SetRect(&g_devpTurn,  px + 146, by, px + 200, by + 22);
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
    if (d.type == DEV_PIPE || d.type == DEV_CROSSOVER) {
        if (PtInRect(&g_devpClose, pt)) { g_devPanel = -1; return true; }
        return true; /* status-only: pipes have no settings or inventory action */
    }
    if (PtInRect(&g_devpDec, pt))        d.value -= di.vStep;
    else if (PtInRect(&g_devpInc, pt))   d.value += di.vStep;
    else if (PtInRect(&g_devpClose, pt)) { g_devPanel = -1; return true; }
    else if (PtInRect(&g_devpTurn, pt) && di.aimable) {
        d.face = (u8)((d.face + 1) & 3);
        return true;
    }
    else if (d.type == DEV_DRAIN && PtInRect(&g_devpTake, pt)) {
        g_filterDevice = g_devPanel; g_creativeOpen = true; g_creSearch[0] = 0;
        g_creScroll = 0; g_creSearchFocus = true; layoutCreative();
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
            drawButton(hdc, g_devpTake, "pick filter", 0, false, PtInRect(&g_devpTake, pt) != 0);
        } else if (d.type == DEV_DRAIN) {
            sprintf(flow, "collecting  %s", d.value ? MATS[d.value].name : "all materials");
            drawText(hdc, tx, g_devpBox.top + 27, RGB(200, 206, 218), flow);
            sprintf(flow, "buffer  %d %s", (int)d.count, d.count ? MATS[d.mat].name : "items");
            drawText(hdc, tx, g_devpBox.top + 45, RGB(160, 200, 230), flow);
            drawButton(hdc, g_devpDec, "<", 0, false, PtInRect(&g_devpDec, pt) != 0);
            drawButton(hdc, g_devpInc, ">", 0, false, PtInRect(&g_devpInc, pt) != 0);
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
        if (!h.empty() && ITEMS[h.item].kind == ITEMK_TOOL) {
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

static void drawCursor(HDC hdc) {
    if (g_mx < PANEL_W) return;                 /* over the panel: system cursor */

    const Aim aim = currentAim();
    /* Where the tool acts -- inside the reach limit. */
    const int ax = PANEL_W + (aim.x - g_camX) * SCALE + SCALE / 2;
    const int ay = (aim.y - g_camY) * SCALE + SCALE / 2;
    /* Where the mouse actually is. */
    const int gx = PANEL_W + (aim.ghostX - g_camX) * SCALE + SCALE / 2;
    const int gy = (aim.ghostY - g_camY) * SCALE + SCALE / 2;

    if (aim.clamped) {
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

        drawCross(hdc, ax, ay, RGB(150, 158, 174), 3, 4);
    }
    drawCross(hdc, gx, gy, RGB(236, 240, 248), 3, 5);

    /* The line preview. Solid rather than dotted, and drawn on top of the
       tether, because the tether means "you cannot reach that" and this means
       "this is what will happen" -- two different messages that should not look
       alike. A ring at the anchor says which end is pinned, which is the only
       thing about a line drag that is not obvious from watching it. */
    if (g_lineOn) {
        const int lx = PANEL_W + (g_lineX - g_camX) * SCALE + SCALE / 2;
        const int ly = (g_lineY - g_camY) * SCALE + SCALE / 2;

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

    RECT title = g_crePanel;
    title.top += 10; title.left += 14;
    SetTextColor(hdc, RGB(226, 190, 90));
    DrawTextA(hdc, g_filterDevice >= 0 ? "SELECT DRAIN FILTER  --  click a material, or clear for all" : "CREATIVE  --  click a swatch to fill the cursor; click a slot to drop it, right-click for half", -1, &title,
              DT_LEFT | DT_TOP | DT_SINGLELINE);

    FillRect(hdc, &g_creSearchBox, g_btnBg);
    FrameRect(hdc, &g_creSearchBox, g_creSearchFocus ? g_accentBrush : g_borderBrush);
    char searchLabel[64]; sprintf(searchLabel, "search: %s", g_creSearch[0] ? g_creSearch : "");
    SetTextColor(hdc, RGB(200, 206, 218));
    DrawTextA(hdc, searchLabel, -1, &g_creSearchBox, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    for (int i = 0; i < g_creCount; ++i) {
        const ItemId it = g_creItem[i];
        const RECT& r = g_creRect[i];
        /* Scrolled out of the window: layoutCreative left the rect empty, and
           an empty rect must not be painted or it lands as a stripe at the
           panel's top-left corner. */
        if (IsRectEmpty(&r)) continue;
        const int have = g_inv.countOf(it);

        /* Swatches are made and destroyed per frame here rather than cached,
           unlike the palette's. This is a modal screen that is open for a
           second at a time, and 40 brush creations once in a while is nothing
           next to keeping a second parallel array in step with ITEMS[]. */
        /* Materials keep their colour swatch here; tools and modules get their
           sprite drawn over the top of it, in the same place, so the row layout
           does not change between the two kinds. */
        HBRUSH sw = CreateSolidBrush(RGB((ITEMS[it].colour >> 16) & 0xFF,
                                         (ITEMS[it].colour >> 8) & 0xFF,
                                          ITEMS[it].colour & 0xFF));
        /* The swatch brush is passed either way so drawButton reserves the same
           box and indents the label identically; the icon is then painted over
           that box. Passing NULL for icon rows instead would left-align their
           labels and the column of names would zig-zag. */
        drawButton(hdc, r, ITEMS[it].name, sw, have > 0, inRect(r, g_mx, g_my));
        DeleteObject(sw);
        if (ITEMS[it].sprite > SPR_NONE) {
            RECT ir = { r.left + 4, r.top + 1, r.left + 22, r.bottom - 1 };
            FillRect(hdc, &ir, g_panelBg);
            drawItemIcon(hdc, ir, it);
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
    {
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
    {
        const FlightSpec fly = flightSpec(g_inv);
        RECT lr = g_crePanel;
        lr.left = g_eqRect[0].left;
        lr.top  = g_eqRect[0].top - 20;
        char s[160];
        /* The RESOLVED numbers, not an item's own, for the same reason the tool
           bench states its resolved delay: two pieces of flight gear do not add
           up (see flightSpec), so the only figure worth reading is the one you
           will actually fly at. */
        if (fly.any())
            sprintf(s, "EQUIPPED  --  climb %.1f cells/s, %.1fs of fuel, reach +%d, speed +%d%%",
                    fly.riseCap * 60.0f, (float)fly.fuel / 60.0f,
                    g_inv.reachBonus(), g_inv.speedBonus());
        else
            sprintf(s, "EQUIPPED  --  nothing to fly with, reach +%d, speed +%d%%",
                    g_inv.reachBonus(), g_inv.speedBonus());
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
    }

    /* --- the tool bench --- */
    if (g_toolSlotCount > 0 && g_toolPackSlot >= 0) {
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
    }

    drawButton(hdc, g_creClear, g_filterDevice >= 0 ? "All materials" : "Empty pack", NULL, false, inRect(g_creClear, g_mx, g_my));
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
    if (!g_drag.empty()) {
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
static RECT g_craftRow[64];
bool g_craftOpen = false;

static void layoutCraft() {
    const int w = 260;
    const int h = 46 + N_RECIPES * 30 + 12;
    const int cx = PANEL_W + VIEW_W / 2, cy = VIEW_H / 2;
    SetRect(&g_craftPanel, cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
    for (int i = 0; i < N_RECIPES && i < 64; ++i)
        SetRect(&g_craftRow[i], g_craftPanel.left + 12, g_craftPanel.top + 40 + i * 30,
                g_craftPanel.right - 12, g_craftPanel.top + 40 + i * 30 + 26);
}

static bool handleCraftClick(int mx, int my) {
    if (!g_craftOpen) return false;
    for (int i = 0; i < N_RECIPES && i < 64; ++i)
        if (inRect(g_craftRow[i], mx, my)) { craftMake(g_inv, i); return true; }
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

    for (int i = 0; i < N_RECIPES && i < 64; ++i) {
        const Recipe& rc = RECIPES[i];
        const bool can = craftCan(g_inv, i);
        const bool hot = can && inRect(g_craftRow[i], g_mx, g_my);

        RECT r = g_craftRow[i];
        FillRect(hdc, &r, hot ? g_btnBgHot : g_btnBg);
        FrameRect(hdc, &r, can ? g_borderBrush : g_panelBg);

        /* The output's own swatch, the same one the hotbar uses, so a row and
           the thing it makes are recognisably the same object. */
        RECT sw = { r.left + 5, r.top + 4, r.left + 21, r.bottom - 4 };
        drawItemIcon(hdc, sw, rc.out);

        SetTextColor(hdc, can ? RGB(226, 232, 244) : RGB(104, 110, 122));
        RECT t = r; t.left = sw.right + 8;
        DrawTextA(hdc, rc.label, -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        /* What it costs, right-aligned, and coloured per ingredient so a row
           you cannot afford says WHICH part you are short of rather than just
           being dim. That is the difference between a locked door and a sign. */
        char cost[128]; cost[0] = 0;
        for (int k = 0; k < CRAFT_MAX_IN; ++k) {
            if (rc.in[k].item == ITEM_NONE || rc.in[k].count <= 0) continue;
            char one[48];
            const int have = g_inv.countOf(rc.in[k].item);
            sprintf(one, "%s%d/%d %s", cost[0] ? "  " : "",
                    have > rc.in[k].count ? rc.in[k].count : have,
                    rc.in[k].count, ITEMS[rc.in[k].item].name);
            if (strlen(cost) + strlen(one) < sizeof(cost) - 1) strcat(cost, one);
        }
        SetTextColor(hdc, can ? RGB(150, 190, 140) : RGB(190, 120, 110));
        RECT ct = r; ct.right -= 8;
        DrawTextA(hdc, cost, -1, &ct, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

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

    for (int i = 0; i < N_BRUSH; ++i) {
        bool sel = (BRUSHES[i].brush == g_brushMat);
        bool hot = inRect(g_brushRect[i], g_mx, g_my);
        drawButton(hdc, g_brushRect[i], BRUSHES[i].label, g_swatchBrush[i], sel, hot);
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

    /* toggles / actions, with live state in the label */
    char lbl[32];
    sprintf(lbl, "Overwrite: %s", g_overwrite ? "On" : "Off");
    drawButton(hdc, g_actRect[ACT_OVERWRITE], lbl, NULL, !g_overwrite, inRect(g_actRect[ACT_OVERWRITE], g_mx, g_my));
    drawButton(hdc, g_actRect[ACT_LAYER], g_bgLayer ? "Layer: Background" : "Layer: Foreground",
               NULL, g_bgLayer, inRect(g_actRect[ACT_LAYER], g_mx, g_my));
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
    sprintf(s, "sim %.2f ms", g_simMs);                    drawText(hdc, 10, sy + 36, RGB(170, 178, 190), s);
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

        /* Ticked unconditionally, so a cooldown can never outlive the drag that
           set it and the first click after a pause always acts at once. */
        if (g_digCool > 0) --g_digCool;
        /* Tool cooldowns tick on the instance, not on a global, so two tools
           recharge independently and swapping between them does not reset
           either -- which is the whole reason firing state lives on the
           instance rather than beside the input handler. */
        for (int i = 1; i < MAX_TOOL_INST; ++i)
            if (g_toolInst[i].used && g_toolInst[i].cooldown > 0) --g_toolInst[i].cooldown;
        if (!g_menuOpen && !g_creativeOpen && !g_craftOpen && g_chestOpen < 0) applyBrush();
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
                              g_camX + VIEW_CELLS_W  + SIM_MARGIN,
                              g_camY + VIEW_CELLS_H + SIM_MARGIN);

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
        if (g_playerOn && !g_menuOpen && (!g_paused || steppedThisFrame)) {
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
        }

        /* After the character has moved, so the camera never lags a frame
           behind what it is following. With the character off, the arrow keys
           drive the camera instead. */
        if (!g_playerOn && !g_menuOpen && !g_creativeOpen) {
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
        roomsTick(g_world);
        devTick(g_world);
        treesTick(g_world);

        /* Light is computed for this camera position and consumed immediately
           by renderView. The two must agree about where the camera is, which
           is why this sits here and not up beside the sim step. */
        if (g_lightOn) lightCompute(g_world, g_camX, g_camY);
        g_cellCount = renderView(g_world, g_pixels, g_view, g_camX, g_camY, g_lightOn);
        /* Machines draw whether or not the character is enabled -- they are part
           of the world, not part of the player, and the sandbox half of this
           program is exactly where you want to inspect a contraption. Before the
           character, so walking in front of one puts you in front of it. */
        devDraw(g_world, g_pixels, g_camX, g_camY, g_lightOn);
        if (g_playerOn) {
            g_player.draw(g_pixels, g_camX, g_camY, g_lightOn);
            if (g_survival) drawHeldTool(g_pixels, currentAim(), g_lightOn);
        }
        sparkDraw(g_pixels, g_camX, g_camY);
        projDraw(g_pixels, g_camX, g_camY);
        /* Modals dim the world in the pixel buffer, before it becomes a blit --
           see dimPixels(). Doing it to the window instead cost 500ms a frame. */
        if (g_menuOpen || g_creativeOpen || g_craftOpen || g_chestOpen >= 0) dimPixels();

        /* Compose off-screen: sim into the viewport, then the panel, then out
           to the window in one BitBlt. */
        StretchDIBits(g_backDC, PANEL_W, 0, VIEW_W, VIEW_H, 0, 0, VIEW_CELLS_W, VIEW_CELLS_H,
                      g_pixels, &g_bmi, DIB_RGB_COLORS, SRCCOPY);
        drawPanel(g_backDC);
        if (g_survival && g_playerOn) drawHotbar(g_backDC);
        drawDevPanel(g_backDC);
        if (!g_menuOpen && !g_creativeOpen && !g_craftOpen && g_chestOpen < 0) drawCursor(g_backDC);
        if (g_creativeOpen) drawCreative(g_backDC);
        if (g_craftOpen)    drawCraft(g_backDC);
        if (g_chestOpen >= 0) drawChest(g_backDC);
        if (g_menuOpen)     drawMenu(g_backDC);

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
