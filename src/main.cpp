#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>   /* timeBeginPeriod; excluded by WIN32_LEAN_AND_MEAN */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "world.h"
#include "render.h"

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

enum ActionId { ACT_OVERWRITE, ACT_VIEW, ACT_PAUSE, ACT_CLEAR, N_ACT };

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

/* UI/input state */
static bool g_uiCapture = false;   /* the click landed on the panel, not the sim */
static bool g_overwrite = true;    /* false = brush only fills empty space */
static int  g_view      = VIEW_NORMAL;
static bool g_lmb = false, g_rmb = false;
static int  g_mx = 0, g_my = 0;      /* current mouse, window pixels */
static int  g_pmx = -1, g_pmy = -1;  /* previous, for stroke interpolation */
static int  g_brushMat = MAT_SAND;
static int  g_brushRadius = 6;
static bool g_paused = false;
static bool g_stepOnce = false;
static int  g_speedIdx = 0;      /* index into SPEEDS */

/* stats */
static double g_fps = 0.0, g_simMs = 0.0;
static int    g_cellCount = 0;

/* What is under the cursor, for the panel readout. Returns false when the
   pointer is off the playfield (over the panel, or outside the window), so the
   caller can show a placeholder instead of a stale name.

   Air is reported as "Air" with its temperature rather than skipped: pointing
   at apparently empty space to see how hot it is turns out to be one of the
   more useful things the readout does, since heat is invisible in Material
   view and only roughly shaded in Glow view. */
static bool hoverCell(char* out, int cap, u32* colOut) {
    const int cx = (g_mx - PANEL_W) / SCALE, cy = g_my / SCALE;
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

/* ======================================================================
   Input
   ====================================================================== */
static void cycleView()      { g_view = (g_view + 1) % VIEW_COUNT; }
static void changeSize(int d){ g_brushRadius = imax(1, imin(64, g_brushRadius + d)); }

/* Returns true if the click was consumed by a panel control. */
static bool handlePanelClick(int mx, int my) {
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
        if (handlePanelClick(g_mx, g_my)) g_uiCapture = true;
        else                              g_lmb = true;
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        g_lmb = false; g_uiCapture = false; ReleaseCapture(); return 0;
    case WM_RBUTTONDOWN:
        g_mx = (short)LOWORD(lp);
        g_my = (short)HIWORD(lp);
        if (g_mx >= PANEL_W) g_rmb = true;   /* right-drag erases, but only over the sim */
        SetCapture(hwnd);
        return 0;
    case WM_RBUTTONUP:
        g_rmb = false; ReleaseCapture(); return 0;

    case WM_MOUSEWHEEL:
        changeSize((short)HIWORD(wp) > 0 ? 1 : -1);
        return 0;

    case WM_KEYDOWN:
        /* Shortcuts still work -- they are just no longer the only way in. */
        switch (wp) {
        case '1': g_brushMat = MAT_SAND;  break;
        case '2': g_brushMat = MAT_WATER; break;
        case '3': g_brushMat = MAT_DIRT;  break;
        case '4': g_brushMat = MAT_STONE; break;
        case '5': g_brushMat = MAT_FIRE;  break;
        case '6': g_brushMat = MAT_STEAM; break;
        case '7': g_brushMat = MAT_WOOD;  break;
        case '8': g_brushMat = MAT_IRON;  break;
        case 'M': g_brushMat = MAT_COPPER;   break;   /* M for metal */
        case 'G': g_brushMat = MAT_GRAPHENE; break;
        case '9': g_brushMat = MAT_LAVA;  break;
        case 'B': g_brushMat = MAT_WALL;  break;
        case '0':
        case 'E': g_brushMat = MAT_EMPTY; break;
        case 'H': g_brushMat = TOOL_HEAT; break;
        case 'J': g_brushMat = TOOL_COOL; break;
        case 'C': g_world.reset();        break;
        case 'O': g_overwrite = !g_overwrite; break;
        case 'V': cycleView();                break;
        case VK_SPACE:  g_paused = !g_paused; break;
        case 'S': g_speedIdx = (g_speedIdx + 1) % N_SPEED; break;
        case VK_OEM_PERIOD: g_stepOnce = true; break;
        case VK_OEM_4: changeSize(-1); break;  /* [ */
        case VK_OEM_6: changeSize(+1); break;  /* ] */
        case VK_ESCAPE: g_running = false; PostQuitMessage(0); break;
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* Paint along the segment the mouse covered since last frame, so a fast drag
   lays down a continuous stroke instead of dotted blobs. Window x maps to a
   cell by subtracting the panel and dividing by the scale. */
static void applyBrush() {
    if (g_uiCapture || (!g_lmb && !g_rmb)) { g_pmx = -1; return; }

    int sel = g_rmb ? (int)MAT_EMPTY : g_brushMat;
    if (g_pmx < 0) { g_pmx = g_mx; g_pmy = g_my; }

    int x0 = (g_pmx - PANEL_W) / SCALE, y0 = g_pmy / SCALE;
    int x1 = (g_mx  - PANEL_W) / SCALE, y1 = g_my  / SCALE;
    int steps = imax(abs(x1 - x0), abs(y1 - y0));

    for (int s = 0; s <= steps; ++s) {
        int px = steps ? x0 + (x1 - x0) * s / steps : x1;
        int py = steps ? y0 + (y1 - y0) * s / steps : y1;
        if (sel == TOOL_HEAT)      g_world.heat(px, py, g_brushRadius,  HEAT_STEP);
        else if (sel == TOOL_COOL) g_world.heat(px, py, g_brushRadius, -HEAT_STEP);
        else                       g_world.paint(px, py, g_brushRadius, (u8)sel, g_overwrite);
        if (!steps) break;
    }
    g_pmx = g_mx;
    g_pmy = g_my;
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

static void drawPanel(HDC hdc) {
    RECT panel = { 0, 0, PANEL_W, WIN_H };
    FillRect(hdc, &panel, g_panelBg);

    HGDIOBJ oldFont = SelectObject(hdc, g_font);
    SetBkMode(hdc, TRANSPARENT);
    drawText(hdc, 10, 9, RGB(240, 240, 246), "POWDER");

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
        char s[32]; sprintf(s, "Size %d", g_brushRadius);
        SetTextColor(hdc, RGB(214, 216, 224));
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
    layoutPanel();

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = wndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "PowderWnd";
    RegisterClassA(&wc);

    DWORD style = (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX);
    RECT r = { 0, 0, WIN_W, WIN_H };
    AdjustWindowRect(&r, style, FALSE);

    HWND hwnd = CreateWindowA("PowderWnd", "Powder", style,
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

        applyBrush();

        LARGE_INTEGER tA, tB;
        QueryPerformanceCounter(&tA);
        if (g_stepOnce) {
            /* Frame-advance stays one step regardless of the multiplier -- the
               whole point of it is to inspect a single frame of the sim. */
            g_world.step();
            g_stepOnce = false;
        } else if (!g_paused) {
            for (int s = 0; s < SPEEDS[g_speedIdx]; ++s) g_world.step();
        }
        QueryPerformanceCounter(&tB);
        g_simMs = 1000.0 * (double)(tB.QuadPart - tA.QuadPart) / (double)freq.QuadPart;

        g_cellCount = renderWorld(g_world, g_pixels, g_view);

        /* Compose off-screen: sim into the viewport, then the panel, then out
           to the window in one BitBlt. */
        StretchDIBits(g_backDC, PANEL_W, 0, VIEW_W, VIEW_H, 0, 0, SIM_W, SIM_H,
                      g_pixels, &g_bmi, DIB_RGB_COLORS, SRCCOPY);
        drawPanel(g_backDC);

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
    DeleteObject(g_font);
    return 0;
}
