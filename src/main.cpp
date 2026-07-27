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
static const int VIEW_W  = SIM_W * SCALE;
static const int VIEW_H  = SIM_H * SCALE;
static const int WIN_W   = PANEL_W + VIEW_W;
static const int WIN_H   = VIEW_H;
static const double FRAME_SECONDS = 1.0 / 60.0;

/* Top of the stats block. layoutPanel() stops the buttons above this and
   drawPanel() writes from it downward, so the two cannot drift apart -- which
   is the failure this panel has already had twice, buttons silently drawn on
   top of the stats text. Five lines now: the hover readout joins fps, sim ms,
   cells and chunks. */
static const int STATS_TOP = VIEW_H - 92;

static u32         g_pixels[SIM_W * SIM_H];
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
    { MAT_STONE, "Stone" },
    { MAT_WOOD,  "Wood"  },
    /* Rubber only -- molten rubber, molten iron and molten copper are all
       simulated but unplaceable, for the same reason mercury's vapour and
       frozen forms are: they are states you put a material INTO, not things
       you build with. */
    { MAT_RUBBER,"Rubber"},
    { MAT_IRON,  "Iron"  },
    { MAT_COPPER,"Copper"},
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
    { MAT_HEATER,"Heater"},
    { MAT_COOLER,"Cooler"},
    { TOOL_HEAT, "Heat"  },
    { TOOL_COOL, "Cool"  },
    { MAT_EMPTY, "Erase" },
};
static const int N_BRUSH = (int)(sizeof(BRUSHES) / sizeof(BRUSHES[0]));

enum ActionId { ACT_OVERWRITE, ACT_VIEW, ACT_PLAYER, ACT_PAUSE, ACT_CLEAR, N_ACT };

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
/* Grown from 34 to make room for a square icon. At 34 the swatch area was 21
   wide by 13 tall once the count row was reserved, so a 14x14 sprite had to be
   letterboxed into a strip and the module chips were unreadable. Icons want
   square space, and the row still spans well under half the viewport. */
static const int HOTBAR_SLOT = 42;   /* screen pixels per hotbar cell */
static RECT g_hotRect[INV_SLOTS];
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
static RECT g_creRect[ITEM_COUNT];
static RECT g_crePanel, g_creClear;
static int  g_creCount = 0;              /* entries actually laid out */
static ItemId g_creItem[ITEM_COUNT];     /* which item each rect belongs to */

/* The tool bench: the carried multitool's own slots, drawn inside the inventory
   screen. It belongs here rather than in a screen of its own because installing
   a module is a transfer between two containers, and putting both on screen at
   once is what makes that legible without a drag-and-drop system. */
static RECT g_toolSlotRect[TOOL_SLOTS_MAX];
static int  g_toolSlotCount = 0;
static int  g_toolPackSlot  = -1;   /* which inventory slot the bench is showing */

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

/* Base reach plus whatever the pack is carrying. Reading it through a function
   rather than a constant is what lets an item change it -- and later a tool. */
static int currentReach() { return PLAYER_REACH + g_inv.reachBonus(); }

/* The working radius of bare hands, clamped no matter what the size control
   says. It caps building as well as digging: the cap is a statement about how
   much world you can reach around at once, and letting you place a radius-40
   blob but only scrape a radius-6 hole would be a strange pair of arms.

   The size control is left free to go higher rather than clamped at the source,
   so the number you set survives picking up a better tool. */
static int handRadius() { return imin(g_brushRadius, HAND.maxRadius); }

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
static void layoutMenu() {
    const int w = 220, h = 150;
    const int cx = PANEL_W + VIEW_W / 2, cy = VIEW_H / 2;
    SetRect(&g_menuPanel, cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
    const int bw = w - 48, bx = cx - bw / 2;
    SetRect(&g_menuResume, bx, cy - 18, bx + bw, cy + 14);
    SetRect(&g_menuQuit,   bx, cy + 26, bx + bw, cy + 58);
}

/* Laid out fresh on open, like the pause menu, and for the same reason: it is
   centred on the viewport and nothing else depends on where it lands. The row
   count is derived from how many items there are, so the panel grows with the
   material table instead of clipping it. */
static void layoutCreative() {
    g_creCount = 0;
    for (int i = 0; i < ITEM_COUNT; ++i) {
        if (ITEMS[i].maxStack == 0) continue;   /* air, and anything unfinished */
        g_creItem[g_creCount++] = (ItemId)i;
    }
    /* The bench only appears when there is a tool to show, and its height is
       part of the panel's height rather than an overlay -- so picking up a
       multitool makes the window taller instead of pushing the grid under it. */
    g_toolPackSlot   = g_inv.firstToolSlot();
    g_toolSlotCount  = 0;
    if (g_toolPackSlot >= 0)
        g_toolSlotCount = imin(ITEMS[g_inv.slot[g_toolPackSlot].item].toolSlots, TOOL_SLOTS_MAX);

    const int rows = (g_creCount + CRE_COLS - 1) / CRE_COLS;
    const int cw = 168, ch = 26, gap = 4, pad = 14;
    const int benchH = g_toolSlotCount ? 62 : 0;
    const int w = pad * 2 + CRE_COLS * cw + (CRE_COLS - 1) * gap;
    const int h = pad + 26 + rows * (ch + gap) + benchH + 38;
    const int cx = PANEL_W + VIEW_W / 2, cy = VIEW_H / 2;
    const int x0 = cx - w / 2, y0 = cy - h / 2;
    SetRect(&g_crePanel, x0, y0, x0 + w, y0 + h);

    for (int i = 0; i < g_creCount; ++i) {
        const int c = i % CRE_COLS, r = i / CRE_COLS;
        const int bx = x0 + pad + c * (cw + gap);
        const int by = y0 + pad + 26 + r * (ch + gap);
        SetRect(&g_creRect[i], bx, by, bx + cw, by + ch);
    }

    /* Module slots: square, and noticeably bigger than a grid row, because they
       are the one place on this screen where the arrangement carries meaning
       (slot order decides which module is the shot). */
    const int by = y0 + pad + 26 + rows * (ch + gap) + 22;
    for (int i = 0; i < g_toolSlotCount; ++i) {
        const int bx = x0 + pad + i * 40;
        SetRect(&g_toolSlotRect[i], bx, by, bx + 34, by + 34);
    }

    SetRect(&g_creClear, x0 + pad, y0 + h - 32, x0 + pad + 120, y0 + h - 8);
}

/* First module sitting loose in the pack, or -1. */
static int packModuleSlot() {
    for (int i = 0; i < INV_SLOTS; ++i) {
        const ItemStack& s = g_inv.slot[i];
        if (!s.empty() && ITEMS[s.item].kind == ITEMK_MODULE) return i;
    }
    return -1;
}

/* Returns true if the click was consumed, which while this is open is always:
   it is modal, and letting a click through to the world would paint under it. */
static bool handleCreativeClick(int mx, int my, bool remove) {
    if (inRect(g_creClear, mx, my)) { g_inv.clear(); layoutCreative(); return true; }

    /* Module slots. Click an empty one to install the first loose module in the
       pack, click a filled one to pull it back out. No drag-and-drop: with one
       module type and three slots, click-to-move says everything a drag would
       and needs no notion of a cursor carrying something. */
    if (g_toolPackSlot >= 0 && g_inv.slot[g_toolPackSlot].inst) {
        ToolInst& ti = g_toolInst[g_inv.slot[g_toolPackSlot].inst];
        for (int i = 0; i < g_toolSlotCount; ++i) {
            if (!inRect(g_toolSlotRect[i], mx, my)) continue;
            if (ti.slot[i] != ITEM_NONE) {
                /* Only clear the slot if the pack actually took it back, or a
                   full pack would quietly delete the module. */
                if (g_inv.add(ti.slot[i], 1) == 0) ti.slot[i] = ITEM_NONE;
            } else if (!remove) {
                const int src = packModuleSlot();
                if (src >= 0) {
                    const ItemId m = g_inv.slot[src].item;
                    if (g_inv.take(m, 1) == 1) ti.slot[i] = m;
                }
            }
            return true;
        }
    }

    for (int i = 0; i < g_creCount; ++i) {
        if (!inRect(g_creRect[i], mx, my)) continue;
        const ItemId it = g_creItem[i];
        if (remove) {
            /* Take everything, not one: the point of the right-click is to
               clear a slot out, and holding the button down to drain 9999 sand
               one unit at a time is not a feature. */
            g_inv.take(it, 100000);
        } else {
            g_inv.add(it, ITEMS[it].maxStack);
        }
        /* Taking or dropping a tool changes whether the bench exists at all,
           which changes the panel's height. Re-laying out here means the rects
           the next click tests against are the ones actually on screen. */
        layoutCreative();
        return true;
    }
    return true;
}

/* ======================================================================
   Input
   ====================================================================== */
static void cycleView()      { g_view = (g_view + 1) % VIEW_COUNT; }
static void changeSize(int d){ g_brushRadius = imax(1, imin(64, g_brushRadius + d)); }

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
        for (int i = 0; i < INV_SLOTS; ++i)
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
    if (inRect(g_actRect[ACT_VIEW],      mx, my)) { cycleView();                return true; }
    if (inRect(g_actRect[ACT_PLAYER],    mx, my)) {
        g_playerOn = !g_playerOn;
        if (g_playerOn) g_player.reset(SIM_W * 0.5f, SIM_H * 0.25f);
        return true;
    }
    if (inRect(g_actRect[ACT_PAUSE],     mx, my)) { g_paused = !g_paused;       return true; }
    if (inRect(g_actRect[ACT_CLEAR],     mx, my)) { g_world.reset();            return true; }
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

    case WM_LBUTTONDOWN:
        g_mx = (short)LOWORD(lp);
        g_my = (short)HIWORD(lp);
        if (g_creativeOpen) { handleCreativeClick(g_mx, g_my, false); g_uiCapture = true; }
        else if (handlePanelClick(g_mx, g_my)) g_uiCapture = true;
        else                                   g_lmb = true;
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        g_lmb = false; g_uiCapture = false; ReleaseCapture(); return 0;
    case WM_RBUTTONDOWN:
        g_mx = (short)LOWORD(lp);
        g_my = (short)HIWORD(lp);
        if (g_creativeOpen)       handleCreativeClick(g_mx, g_my, true);
        else if (g_mx >= PANEL_W) g_rmb = true;   /* right-drag digs, but only over the sim */
        SetCapture(hwnd);
        return 0;
    case WM_RBUTTONUP:
        g_rmb = false; ReleaseCapture(); return 0;

    case WM_SETCURSOR:
        /* Hide the arrow over the playfield so the crosshair is the only
           pointer there. Still the system cursor over the panel and the menu,
           where you are clicking buttons rather than aiming. */
        if (LOWORD(lp) == HTCLIENT && g_mx >= PANEL_W && !g_menuOpen && !g_creativeOpen) {
            SetCursor(NULL);
            return TRUE;
        }
        break;

    case WM_MOUSEWHEEL: {
        const int dir = (short)HIWORD(wp) > 0 ? 1 : -1;
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
            g_inv.selected = (g_inv.selected - dir + INV_SLOTS) % INV_SLOTS;
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
        case 'C': g_world.reset();        break;
        /* Respawn at the cursor. Indispensable while tuning movement, and the
           obvious escape hatch when you bury yourself. */
        case 'R':
            if (g_mx >= PANEL_W)
                g_player.reset((float)((g_mx - PANEL_W) / SCALE), (float)(g_my / SCALE));
            break;
        case 'O': g_overwrite = !g_overwrite; break;
        case 'V': cycleView();                break;
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
            if (g_creativeOpen) { layoutCreative(); g_lmb = g_rmb = false; }
            break;
        /* Escape backs out of the creative grid before it reaches the pause
           menu -- one key that always means "close the thing in front of me" is
           worth more than a second binding to remember. */
        case VK_ESCAPE:
            if (g_creativeOpen) g_creativeOpen = false;
            else                g_menuOpen = !g_menuOpen;
            break;
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
    a.ghostX = (g_mx - PANEL_W) / SCALE;
    a.ghostY = g_my / SCALE;
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

    if (g_survival && g_playerOn && g_rmb) {
        if (g_digCool <= 0) {
            digInto(g_world, g_inv, aim.x, aim.y, handRadius(), HAND.cellsPerBite);
            g_digCool = HAND.cooldown;
        }
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
            else                              placeFrom(g_world, g_inv, px, py, handRadius());
        } else {
            if (sel == TOOL_HEAT)      g_world.heat(px, py, g_brushRadius,  HEAT_STEP);
            else if (sel == TOOL_COOL) g_world.heat(px, py, g_brushRadius, -HEAT_STEP);
            else                       g_world.paint(px, py, g_brushRadius, (u8)sel, g_overwrite);
        }
        if (!steps) break;
    }
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

/* The hotbar sits over the foot of the viewport rather than in the panel. The
   panel is already full, and more to the point what you are carrying belongs
   next to the world you are carrying it through -- glancing down at your hands
   should not mean looking away to the side. */
static void layoutHotbar() {
    const int totalW = INV_SLOTS * HOTBAR_SLOT;
    const int x0 = PANEL_W + (VIEW_W - totalW) / 2;
    const int y0 = VIEW_H - HOTBAR_SLOT - 10;
    for (int i = 0; i < INV_SLOTS; ++i)
        SetRect(&g_hotRect[i], x0 + i * HOTBAR_SLOT, y0,
                x0 + i * HOTBAR_SLOT + HOTBAR_SLOT - 3, y0 + HOTBAR_SLOT - 3);
}

static void drawHotbar(HDC hdc) {
    layoutHotbar();
    HGDIOBJ oldFont = SelectObject(hdc, g_font);
    SetBkMode(hdc, TRANSPARENT);

    for (int i = 0; i < INV_SLOTS; ++i) {
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
            if (st.count >= 1000) sprintf(n, "%d.%dk", st.count / 1000, (st.count % 1000) / 100);
            else                  sprintf(n, "%d", st.count);
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
        else if (bonus > 0) sprintf(s, "%s  r%d  reach %d (+%d)", HAND.name, handRadius(),
                                    currentReach(), bonus);
        else                sprintf(s, "%s  r%d  reach %d", HAND.name, handRadius(), currentReach());
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
static void drawHeldTool(u32* px, const Aim& aim) {
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
    const float ox = pcx + dx * 2.0f, oy = pcy - 1.0f + dy * 2.0f;
    const float px2 = -dy, py2 = dx;   /* perpendicular, for thickness */

    for (int t = 0; t < len; ++t) {
        u32 c;
        if      (t < grip)     c = handle;
        else if (t == grip)    c = 0x6E7684;    /* collar */
        else if (t == len - 1) c = 0xF2F5FF;    /* working tip */
        else                   c = 0xAEB6C4;    /* steel */

        const float fx = ox + dx * t, fy = oy + dy * t;
        const int x = (int)fx, y = (int)fy;
        if (x >= 0 && x < SIM_W && y >= 0 && y < SIM_H) px[y * SIM_W + x] = c;
        if (mk2 || t < grip) {
            const int x2 = (int)(fx + px2), y2 = (int)(fy + py2);
            if (x2 >= 0 && x2 < SIM_W && y2 >= 0 && y2 < SIM_H) px[y2 * SIM_W + x2] = c;
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
    const int ax = PANEL_W + aim.x * SCALE + SCALE / 2;
    const int ay = aim.y * SCALE + SCALE / 2;
    /* Where the mouse actually is. */
    const int gx = PANEL_W + aim.ghostX * SCALE + SCALE / 2;
    const int gy = aim.ghostY * SCALE + SCALE / 2;

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
    for (int i = 0; i < SIM_W * SIM_H; ++i)
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
    DrawTextA(hdc, "CREATIVE  --  click to take, right-click to drop", -1, &title,
              DT_LEFT | DT_TOP | DT_SINGLELINE);

    for (int i = 0; i < g_creCount; ++i) {
        const ItemId it = g_creItem[i];
        const RECT& r = g_creRect[i];
        const int have = g_inv.countOf(it);

        /* Swatches are made and destroyed per frame here rather than cached,
           unlike the palette's. This is a modal screen that is open for a
           second at a time, and 30 brush creations once in a while is nothing
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
            char n[16]; sprintf(n, "%d", have);
            RECT cr = r; cr.right -= 7;
            SetTextColor(hdc, RGB(150, 210, 150));
            DrawTextA(hdc, n, -1, &cr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
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

    drawButton(hdc, g_creClear, "Empty pack", NULL, false, inRect(g_creClear, g_mx, g_my));
    {
        RECT hint = g_crePanel;
        hint.left = g_creClear.right + 12; hint.top = g_creClear.top + 4;
        SetTextColor(hdc, RGB(120, 126, 138));
        DrawTextA(hdc, "Tab or Esc to close", -1, &hint, DT_LEFT | DT_TOP | DT_SINGLELINE);
    }
    SelectObject(hdc, oldFont);
}

/* A modal overlay, deliberately plain: dim the world behind it so it is
   obviously not interactive, then the two things anyone opens a pause menu
   for. Escape closes it again. */
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

    RECT hint = g_menuPanel;
    hint.top = g_menuPanel.bottom - 26;
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
        char s[32];
        const bool capped = g_survival && g_playerOn && handRadius() < g_brushRadius;
        if (capped) sprintf(s, "Size %d -> %d", g_brushRadius, handRadius());
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
    const char* vn = g_view == VIEW_NORMAL ? "View: Glow" :
                     g_view == VIEW_MATERIAL ? "View: Material" : "View: Heat";
    drawButton(hdc, g_actRect[ACT_VIEW], vn, NULL, g_view != VIEW_NORMAL, inRect(g_actRect[ACT_VIEW], g_mx, g_my));
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
    sprintf(s, "cells %d", g_cellCount);                   drawText(hdc, 10, sy + 52, RGB(170, 178, 190), s);
    sprintf(s, "chunks %d/%d", g_world.activeChunks, CHUNK_COUNT);
    drawText(hdc, 10, sy + 68, RGB(170, 178, 190), s);

    SelectObject(hdc, oldFont);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    initMaterials();
    g_world.reset();
    initItems();
    g_inv.clear();
    layoutPanel();
    layoutHotbar();

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
    g_bmi.bmiHeader.biWidth       = SIM_W;
    g_bmi.bmiHeader.biHeight      = -SIM_H;   /* negative = top-down rows */
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
        if (!g_menuOpen && !g_creativeOpen) applyBrush();
        /* Whether the sim actually advanced this frame, so the character moves
           in lockstep with the world -- including on a single frame-advance. */
        bool steppedThisFrame = false;

        /* Publish the body's box before stepping, so this frame's simulation
           already respects it -- otherwise sand gets one frame of free passage
           through the player every time they move. */
        if (g_playerOn) g_player.occupy(g_world);
        else            g_world.clearBlockBox();

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
            g_player.update(g_world, in);
        }

        g_cellCount = renderWorld(g_world, g_pixels, g_view);
        if (g_playerOn) {
            g_player.draw(g_pixels);
            if (g_survival) drawHeldTool(g_pixels, currentAim());
        }
        projDraw(g_pixels);
        /* Modals dim the world in the pixel buffer, before it becomes a blit --
           see dimPixels(). Doing it to the window instead cost 500ms a frame. */
        if (g_menuOpen || g_creativeOpen) dimPixels();

        /* Compose off-screen: sim into the viewport, then the panel, then out
           to the window in one BitBlt. */
        StretchDIBits(g_backDC, PANEL_W, 0, VIEW_W, VIEW_H, 0, 0, SIM_W, SIM_H,
                      g_pixels, &g_bmi, DIB_RGB_COLORS, SRCCOPY);
        drawPanel(g_backDC);
        if (g_survival && g_playerOn) drawHotbar(g_backDC);
        if (!g_menuOpen && !g_creativeOpen) drawCursor(g_backDC);
        if (g_creativeOpen) drawCreative(g_backDC);
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
