/* ============================================================================
   powderlike -- a second front-end over Cinderlift's simulation.

   What it is: the falling-sand toy this project grew out of, with the game
   taken off it. No character, no camera, no minimap, no inventory, no
   progression -- a fixed screen, a palette, and the physics.

   WHY IT IS A SEPARATE main() AND NOT A FLAG IN THE GAME'S ONE.
   src/main.cpp is ten thousand lines of shell: panels, hotbar, crafting,
   devices, save screens, multiplayer authority. Threading a POWDERLIKE define
   through it would put a second product's worth of branches into the file that
   is already hardest to change, and every one of those branches would be a
   place the two could disagree. The project already has the right pattern for
   this and has had it for a long time: every test in tests/ is a standalone
   main() that links all of src/ except src/main.cpp. This is that, with a
   window on it.

   WHAT IT INHERITS, WHICH IS THE POINT. It links world.cpp, materials.cpp,
   device.cpp and tree.cpp as they are. A change to how sand piles, how heat
   conducts, how a gas finds its vent, what titanium melts at -- any of it --
   lands here the next time it is built, with nothing to keep in step. The
   palette is shared too, for the same reason: see src/brushes.h.

   WHAT IT DELIBERATELY DOES NOT DO. Saving, because a toy you cannot lose
   anything in does not need it and the save format is bound up with player
   and device state this front-end has none of. Lighting, because the light
   field's geometry is fixed to the game's 512x384 window and the whole feature
   here is a view that is not that size (see render.h). Machines, because they
   are the game's layer and this is the simulation's.

   THE ONE FEATURE THE GAME DOES NOT HAVE. Cell scale -- see SCALES below.
   ========================================================================== */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "../common.h"
#include "../materials.h"
#include "../world.h"
#include "../render.h"
#include "../device.h"
#include "../tree.h"
#include "../brushes.h"
#include "../version.h"

/* --- the window ------------------------------------------------------------
   A fixed viewport, exactly like the game's: a stable pixel canvas means the
   window->cell mapping is arithmetic rather than a resize handler, and the
   amount of world on screen cannot change by dragging a corner.

   1024x768 is the game's viewport (512x384 cells at two pixels each), reused
   so the two look like the same program. The panel is its own strip rather
   than an overlay, so a stroke can never paint underneath a button. */
static const int VIEWPORT_W = 1024;
static const int VIEWPORT_H = 768;
static const int PANEL_W    = 264;   /* two columns of material buttons */
static const int WIN_W      = PANEL_W + VIEWPORT_W;
static const int WIN_H      = VIEWPORT_H;
static const double FRAME_SECONDS = 1.0 / 60.0;

/* --- cell scale, which IS the world size ----------------------------------
   The whole world is what is on screen. There is no camera, so making a cell
   smaller does not zoom out -- it makes the world bigger, because the same
   1024x768 pixels now hold four or sixteen times as many cells. That is the
   one thing this front-end has that the game does not, and it is why it was
   worth building.

   Three scales rather than two, because where a cell stops reading as a
   material is not answerable from a description, and each row here costs one
   line. Measured on an empty world in the real window, which is the renderer's
   most expensive case -- every cell takes the backdrop path instead of a flat
   palette lookup:

                cell      cells        chunks    draw ms (was)
     Normal     2 px      512x384         192      2.0   (4.4)
     Half       1 px     1024x768         768      1.5   (6.8)
     Quarter    0.5 px   2048x1536      3,072      4.0  (15.8)

   The "was" column is the first version of this file, and the difference is
   three things: renderView and the Quarter downsample now run on the sim's
   thread pool, which used to sit idle while the frame was drawn, and the
   panel is cached instead of redrawn as ninety GDI calls a frame. Normal is
   the slowest to draw now because GDI's 2x blow-up of the cell buffer is the
   largest thing left, and it is GDI's.

   The sim is cheap by comparison and does not depend on the scale: 0.2 ms at
   rest, and 3.4 ms (p90 5.7) at Half while a 30-cell brush of water is being
   dragged across the top -- see tools/powderbench.cpp, which reproduces that
   stroke headless. That includes the fluid pass, which gives resting liquid
   and stored gas pressure a second turn each frame (FLUID_SUBSTEPS in
   world.h) and is most of why a pool now settles promptly. Pouring is the expensive transient because every falling cell
   takes a turn every frame; a settled pool is nearly free. The panel reports
   sim and draw separately so it is clear which one a slow frame is paying.

   ONE MORE CONSEQUENCE, and it surprises people. Material moves a fixed number
   of CELLS per frame, so at Quarter everything crosses the screen four times
   slower in seconds -- sand takes about twenty-five to fall the height of the
   view rather than six. The speed control is what that is for, and it is why
   this front-end kept one.

   `up` scales the cell buffer onto the viewport and `down` shrinks it; exactly
   one of them is ever above 1. Normal is a clean 2x nearest-neighbour blow-up,
   which GDI does for free and which keeps pixels crisp. Half is one cell per
   pixel and needs neither.

   Quarter is the odd one: two cells share a screen pixel in each axis, so
   something has to decide what that pixel shows. It is box-averaged in
   software (see shrinkCells) rather than dropped, because dropping half the
   cells makes a one-cell-wide stream of sand flicker in and out of existence
   depending on which column it happens to land in -- the picture stops being a
   picture of the simulation.

   Changing scale rebuilds the world, and it has to: the walls that hold the
   material in are in different places. The status line says so rather than
   letting it look like a crash. */
struct ScaleDef { const char* label; int cellsW, cellsH; int up, down; };
static const ScaleDef SCALES[] = {
    { "Normal",   512,  384, 2, 1 },
    { "Half",    1024,  768, 1, 1 },
    { "Quarter", 2048, 1536, 1, 2 },
};
static const int N_SCALE = (int)(sizeof(SCALES) / sizeof(SCALES[0]));
static const int SCALE_MAX_CELLS_W = 2048;
static const int SCALE_MAX_CELLS_H = 1536;

/* --- where in the world the play area sits --------------------------------
   Chunk-aligned and the same corner at every scale, so the live window and the
   containing walls land on chunk edges and a scale change moves only the far
   side of the box. Anywhere inside SIM_W x SIM_H would do; this leaves room
   for the largest scale in both axes with the world's own edge well clear.

   World storage is not sized to the play area, and that is a deliberate
   non-optimisation: SIM_W and SIM_H are compile-time constants that worldgen,
   the save format and the stripe geometry are all built against, and
   overriding them for this one binary would fork all of that to save address
   space a 64-bit process does not miss. The unused remainder is never touched:
   it is outside the live window, so it is never simulated, and outside the
   view, so it is never drawn. */
static const int PLAY_ORIGIN_X = 1024;
static const int PLAY_ORIGIN_Y = 3072;

/* How thick the wall around the play area is, and how much of it you can see.
   The seen part is a frame two cells wide so the edge of the world is visible
   rather than implied. The rest is backing, and eight cells total is not
   arbitrary: GRASS_DEPTH is 5, and world.h is explicit that anything standing
   in for the edge of the world has to be thicker than that, or grass crawls
   through it into the genuinely empty space beyond. */
static const int BORDER_SEEN  = 2;
static const int BORDER_TOTAL = 8;

/* Simulation speed: sim steps per displayed frame, not a frame-rate change.
   Same mechanism and the same honest cost as the game's -- at 4x every rule
   simply happens four times before you see it. */
static const int SPEEDS[] = { 1, 2, 4 };
static const int N_SPEED  = (int)(sizeof(SPEEDS) / sizeof(SPEEDS[0]));

static const int BRUSH_RADIUS_MIN = 1;
static const int BRUSH_RADIUS_MAX = 64;

/* ==========================================================================
   State
   ========================================================================== */
static bool  g_running = true;
static HDC   g_backDC  = NULL;
static HBITMAP g_backBmp = NULL;
static HGDIOBJ g_backOldBmp = NULL;
/* The panel above the stats, drawn once and blitted until something on it
   changes. Redrawn every frame it was about ninety GDI fills and text draws --
   measured, the largest fixed cost in a frame at Normal scale, bigger than
   rendering the world. */
static HDC     g_panelDC = NULL;
static HBITMAP g_panelBmp = NULL;
static HGDIOBJ g_panelOldBmp = NULL;
static u32     g_panelKey = 0;
static bool    g_panelValid = false;
static HFONT g_font   = NULL;
static BITMAPINFO g_bmi;

static HBRUSH g_panelBg, g_btnBg, g_btnBgHot, g_btnBgSel, g_borderBrush, g_accentBrush;
static HBRUSH g_swatchBrush[N_BRUSH];

/* The cell buffer, sized for the largest scale. 12.6 MB, written once a frame
   by renderView and read once by the blit. */
static u32 g_cells[SCALE_MAX_CELLS_W * SCALE_MAX_CELLS_H];
/* Where a downscaled frame is averaged into. Only Quarter uses it. */
static u32 g_shrunk[VIEWPORT_W * VIEWPORT_H];

static int  g_scaleIdx = 0;
static int  g_speedIdx = 0;
static int  g_view     = VIEW_NORMAL;
static bool g_paused   = false;
static bool g_stepOnce = false;
static bool g_overwrite = true;   /* a brush replaces what is there */
static int  g_brushMat = MAT_SAND;
static int  g_brushRadius = 10;
static bool g_bgLayer  = false;

static int  g_mx = 0, g_my = 0;
static bool g_lmb = false, g_rmb = false;
/* Middle-drag blows on the air: the drag's direction and speed are pushed
   into the wind field under the brush. W draws the field over the world. */
static bool g_mmb = false;
static int  g_wmx = -1, g_wmy = -1;
static bool g_showWind = false;
static bool g_uiCapture = false;  /* the press landed on the panel */
static int  g_pmx = -1, g_pmy = -1;   /* previous stroke point, in cells */

static double g_fps = 0.0, g_simMs = 0.0, g_frameMs = 0.0;
/* Drawing, split out from the frame. It is reported because at the smaller
   scales it is the LARGER half and that is the opposite of what one expects
   from a sandbox: the sim only visits chunks something is happening in, while
   the renderer and the downsample visit every cell on screen every frame. A
   readout that showed only sim ms would say 1 ms while the frame was 17. */
static double g_drawMs = 0.0;
static int    g_cellCount = 0;

static char g_status[96] = "";
static int  g_statusFrames = 0;

static int cellsW() { return SCALES[g_scaleIdx].cellsW; }
static int cellsH() { return SCALES[g_scaleIdx].cellsH; }

/* ==========================================================================
   The world
   ========================================================================== */

/* Paint the containing box, and label the play area underground.

   The label is a rendering decision and nothing else. An unlabelled chunk is
   ZONE_SKY, which renders empty cells as the daylit sky gradient -- a blue
   backdrop that shifts with the day cycle under a toy that has no day in it.
   ZONE_LAYER1 gives the cave backdrop instead: dark, static, and the thing
   small bright pixels show up against. The zone rect reaches a chunk PAST the
   play area on every side on purpose: renderView fades the first underground
   chunk down from sky (see backdrop() in render.cpp), and without the margin
   that fade would draw a blue band across the top of the view. */
static void buildWorld() {
    devClear();
    g_world.reset();

    const int w = cellsW(), h = cellsH();
    const int x1 = PLAY_ORIGIN_X + w - 1, y1 = PLAY_ORIGIN_Y + h - 1;

    g_world.setZoneRect(PLAY_ORIGIN_X - CHUNK, PLAY_ORIGIN_Y - CHUNK, x1 + CHUNK, y1 + CHUNK,
                        ZONE_LAYER1);

    /* Written straight into the grid rather than through setCell: these are
       static walls being laid down on a world that has not stepped yet, so
       there is nothing to dirty and no temperature to seed. */
    const int outer = BORDER_TOTAL - BORDER_SEEN;
    for (int y = PLAY_ORIGIN_Y - outer; y <= y1 + outer; ++y) {
        for (int x = PLAY_ORIGIN_X - outer; x <= x1 + outer; ++x) {
            const bool inside = x >= PLAY_ORIGIN_X + BORDER_SEEN && x <= x1 - BORDER_SEEN &&
                                y >= PLAY_ORIGIN_Y + BORDER_SEEN && y <= y1 - BORDER_SEEN;
            if (!inside) g_world.cells[y * SIM_W + x].mat = MAT_WALL;
        }
    }

    /* The entire play area, every frame, for good. This is the one place this
       front-end is unlike the game: the game's window follows a camera and
       everything outside it is frozen, which is what makes a 4096x9216 world
       affordable. Here the world IS the window, so there is nothing to freeze
       and nothing to gain by pretending otherwise. */
    g_world.setLiveWindow(PLAY_ORIGIN_X, PLAY_ORIGIN_Y, x1, y1);

    /* The brush stops at the inside face of the wall. Without this, erasing
       the frame -- or painting over it in Replace -- opened the box and the
       whole world poured out of the gap into the dark outside. */
    g_world.setEditBounds(PLAY_ORIGIN_X + BORDER_SEEN, PLAY_ORIGIN_Y + BORDER_SEEN,
                          x1 - BORDER_SEEN, y1 - BORDER_SEEN);
}

static void setStatus(const char* s) {
    _snprintf(g_status, sizeof(g_status), "%s", s);
    g_status[sizeof(g_status) - 1] = 0;
    g_statusFrames = 150;
}

static void setScale(int idx) {
    if (idx < 0 || idx >= N_SCALE || idx == g_scaleIdx) return;
    g_scaleIdx = idx;
    buildWorld();
    char s[96];
    sprintf(s, "%s  -  %d x %d cells  -  world cleared",
            SCALES[idx].label, cellsW(), cellsH());
    setStatus(s);
}

/* ==========================================================================
   Panel layout
   ========================================================================== */
static const int PAL_ROWS = 15;

static RECT g_palRect[N_BRUSH];
static RECT g_palArea, g_palTrack, g_palThumb;
static RECT g_sizeTrack, g_sizeThumb;
static RECT g_speedRect[N_SPEED];
static RECT g_scaleRect[N_SCALE];
/* A fourth view beside the three the renderer knows: the world dimmed, with
   the wind drawn over it. Local to this front-end -- the renderer's views are
   shared with the game, and this one is a tuning instrument. */
static const int VIEW_AIR = VIEW_COUNT;
static const int PL_VIEW_COUNT = VIEW_COUNT + 1;
static RECT g_viewRect[PL_VIEW_COUNT];
static RECT g_bgRect, g_overwriteRect, g_pauseRect, g_stepRect, g_clearRect;
static int  g_palScroll = 0, g_palMaxScroll = 0;
static int  g_statsTop  = 0;

static void layoutPanel() {
    const int pad = 10, gap = 4, h = 20, pitch = h + gap, railW = 8;
    const int catalogW = PANEL_W - pad * 2 - railW - gap;
    const int colW = (catalogW - gap) / 2;
    int y = 30;

    const int rows = (N_BRUSH + 1) / 2;
    g_palMaxScroll = imax(0, rows - PAL_ROWS);
    g_palScroll = imax(0, imin(g_palScroll, g_palMaxScroll));
    SetRect(&g_palArea, pad, y, pad + catalogW, y + PAL_ROWS * pitch - gap);
    SetRect(&g_palTrack, pad + catalogW + gap, y,
            pad + catalogW + gap + railW, y + PAL_ROWS * pitch - gap);
    if (g_palMaxScroll > 0) {
        const int trackH = g_palTrack.bottom - g_palTrack.top;
        const int thumbH = imax(20, trackH * PAL_ROWS / rows);
        const int travel = trackH - thumbH;
        const int thumbY = g_palTrack.top + travel * g_palScroll / g_palMaxScroll;
        SetRect(&g_palThumb, g_palTrack.left, thumbY, g_palTrack.right, thumbY + thumbH);
    } else {
        SetRectEmpty(&g_palThumb);
    }

    for (int i = 0; i < N_BRUSH; ++i) SetRectEmpty(&g_palRect[i]);
    for (int i = 0; i < N_BRUSH; ++i) {
        const int row = i / 2 - g_palScroll;
        if (row < 0 || row >= PAL_ROWS) continue;
        const int x0 = pad + (i & 1) * (colW + gap);
        SetRect(&g_palRect[i], x0, y + row * pitch, x0 + colW, y + row * pitch + h);
    }
    y += PAL_ROWS * pitch + 8;

    /* Brush size: a label row, then the track under it. */
    y += 16;
    SetRect(&g_sizeTrack, pad, y, PANEL_W - pad, y + 14);
    {
        const int thumbW = 12;
        const int travel = imax(1, (g_sizeTrack.right - g_sizeTrack.left) - thumbW);
        const int at = (g_brushRadius - BRUSH_RADIUS_MIN) * travel /
                       (BRUSH_RADIUS_MAX - BRUSH_RADIUS_MIN);
        SetRect(&g_sizeThumb, g_sizeTrack.left + at, g_sizeTrack.top,
                g_sizeTrack.left + at + thumbW, g_sizeTrack.bottom);
    }
    /* Room for the Speed label that the next row draws above itself, which is
       what this gap is for -- without it the label lands on the track. */
    y += 14 + 20;

    /* Three segmented rows, each laid out the same way. */
    const int segW = PANEL_W - pad * 2;
    for (int i = 0; i < N_SPEED; ++i)
        SetRect(&g_speedRect[i], pad + segW * i / N_SPEED, y,
                pad + segW * (i + 1) / N_SPEED - 3, y + h);
    y += pitch + 14;
    for (int i = 0; i < N_SCALE; ++i)
        SetRect(&g_scaleRect[i], pad + segW * i / N_SCALE, y,
                pad + segW * (i + 1) / N_SCALE - 3, y + h);
    y += pitch + 14;
    for (int i = 0; i < PL_VIEW_COUNT; ++i)
        SetRect(&g_viewRect[i], pad + segW * i / PL_VIEW_COUNT, y,
                pad + segW * (i + 1) / PL_VIEW_COUNT - 3, y + h);
    y += pitch + 6;

    /* Two rows of paired buttons. */
    const int halfW = (segW - gap) / 2;
    SetRect(&g_overwriteRect, pad, y, pad + halfW, y + h);
    SetRect(&g_bgRect, pad + halfW + gap, y, pad + segW, y + h);
    y += pitch;
    SetRect(&g_pauseRect, pad, y, pad + halfW, y + h);
    SetRect(&g_stepRect, pad + halfW + gap, y, pad + segW, y + h);
    y += pitch;
    SetRect(&g_clearRect, pad, y, pad + segW, y + h);
    y += pitch + 6;

    g_statsTop = y;
}

/* ==========================================================================
   Drawing the panel
   ========================================================================== */
static bool inRect(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

static void drawText(HDC hdc, int x, int y, COLORREF c, const char* s) {
    SetTextColor(hdc, c);
    SetBkMode(hdc, TRANSPARENT);
    TextOutA(hdc, x, y, s, (int)strlen(s));
}

static void drawButton(HDC hdc, const RECT& r, const char* label, HBRUSH swatch,
                       bool selected, bool hot) {
    if (IsRectEmpty(&r)) return;
    RECT box = r;
    FillRect(hdc, &box, selected ? g_btnBgSel : (hot ? g_btnBgHot : g_btnBg));
    FrameRect(hdc, &box, selected ? g_accentBrush : g_borderBrush);
    int textX = box.left + 6;
    if (swatch) {
        RECT sw = { box.left + 4, box.top + 4, box.left + 16, box.bottom - 4 };
        FillRect(hdc, &sw, swatch);
        FrameRect(hdc, &sw, g_borderBrush);
        textX = sw.right + 5;
    }
    RECT t = { textX, box.top, box.right - 3, box.bottom };
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, selected ? RGB(240, 226, 186) : RGB(206, 212, 222));
    DrawTextA(hdc, label, -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

/* The cell the brush would act on, or false when the mouse is not over the
   world. There is no reach limit and no camera, so this is a plain divide --
   which is the whole reason the viewport is a fixed integer scale. */
static bool aimCell(int* ax, int* ay) {
    if (g_mx < PANEL_W || g_mx >= WIN_W || g_my < 0 || g_my >= WIN_H) return false;
    const ScaleDef& s = SCALES[g_scaleIdx];
    const int vx = ((g_mx - PANEL_W) * s.down) / s.up;
    const int vy = (g_my * s.down) / s.up;
    if (vx < 0 || vx >= s.cellsW || vy < 0 || vy >= s.cellsH) return false;
    *ax = PLAY_ORIGIN_X + vx;
    *ay = PLAY_ORIGIN_Y + vy;
    return true;
}

static void drawPanel(HDC hdc) {
    RECT panel = { 0, 0, PANEL_W, WIN_H };
    FillRect(hdc, &panel, g_panelBg);
    SelectObject(hdc, g_font);

    char s[160];
    sprintf(s, "powderlike  %s", CINDERLIFT_VERSION);
    drawText(hdc, 10, 8, RGB(226, 190, 90), s);

    for (int i = 0; i < N_BRUSH; ++i) {
        if (IsRectEmpty(&g_palRect[i])) continue;
        const bool hot = inRect(g_palRect[i], g_mx, g_my);
        drawButton(hdc, g_palRect[i], BRUSHES[i].label, g_swatchBrush[i],
                   BRUSHES[i].brush == g_brushMat, hot);
    }
    if (!IsRectEmpty(&g_palThumb)) {
        FillRect(hdc, &g_palTrack, g_btnBg);
        FillRect(hdc, &g_palThumb, g_borderBrush);
    }

    sprintf(s, "Brush size  %d", g_brushRadius);
    drawText(hdc, 10, g_sizeTrack.top - 18, RGB(178, 186, 198), s);
    FillRect(hdc, &g_sizeTrack, g_btnBg);
    FrameRect(hdc, &g_sizeTrack, g_borderBrush);
    FillRect(hdc, &g_sizeThumb, g_accentBrush);

    drawText(hdc, 10, g_speedRect[0].top - 15, RGB(178, 186, 198), "Speed");
    for (int i = 0; i < N_SPEED; ++i) {
        sprintf(s, "%dx", SPEEDS[i]);
        drawButton(hdc, g_speedRect[i], s, NULL, i == g_speedIdx,
                   inRect(g_speedRect[i], g_mx, g_my));
    }
    drawText(hdc, 10, g_scaleRect[0].top - 15, RGB(178, 186, 198), "World size");
    for (int i = 0; i < N_SCALE; ++i)
        drawButton(hdc, g_scaleRect[i], SCALES[i].label, NULL, i == g_scaleIdx,
                   inRect(g_scaleRect[i], g_mx, g_my));
    static const char* VIEW_LABEL[PL_VIEW_COUNT] = { "Normal", "Material", "Heat", "Air" };
    drawText(hdc, 10, g_viewRect[0].top - 15, RGB(178, 186, 198), "View");
    for (int i = 0; i < PL_VIEW_COUNT; ++i)
        drawButton(hdc, g_viewRect[i], VIEW_LABEL[i], NULL, i == g_view,
                   inRect(g_viewRect[i], g_mx, g_my));

    drawButton(hdc, g_overwriteRect, g_overwrite ? "Replace" : "Fill only", NULL,
               g_overwrite, inRect(g_overwriteRect, g_mx, g_my));
    drawButton(hdc, g_bgRect, g_bgLayer ? "Backdrop" : "Foreground", NULL,
               g_bgLayer, inRect(g_bgRect, g_mx, g_my));
    drawButton(hdc, g_pauseRect, g_paused ? "Paused" : "Pause", NULL,
               g_paused, inRect(g_pauseRect, g_mx, g_my));
    drawButton(hdc, g_stepRect, "Step", NULL, false, inRect(g_stepRect, g_mx, g_my));
    drawButton(hdc, g_clearRect, "Clear world", NULL, false,
               inRect(g_clearRect, g_mx, g_my));
}

/* The part of the panel that changes every frame, drawn over the cached rest.
   Its own background fill first, since it is no longer drawn onto a freshly
   cleared panel. */
static void drawStats(HDC hdc) {
    RECT area = { 0, g_statsTop, PANEL_W, WIN_H };
    FillRect(hdc, &area, g_panelBg);
    SelectObject(hdc, g_font);
    char s[160];
    int y = g_statsTop;
    sprintf(s, "%.0f fps   frame %.2f ms", g_fps, g_frameMs);
    drawText(hdc, 10, y, RGB(150, 158, 170), s); y += 15;
    sprintf(s, "sim %.2f ms   draw %.2f ms   %d threads", g_simMs, g_drawMs, simWorkers());
    drawText(hdc, 10, y, RGB(150, 158, 170), s); y += 15;
    sprintf(s, "%d cells   %d / %d chunks", g_cellCount, g_world.activeChunks,
            (cellsW() >> CHUNK_SHIFT) * (cellsH() >> CHUNK_SHIFT));
    drawText(hdc, 10, y, RGB(150, 158, 170), s); y += 15;

    /* What is under the cursor. The one readout that is worth more than its
       row of pixels at Quarter scale, where a cell is half a screen pixel and
       there is no other way to tell what you are looking at. */
    int ax, ay;
    if (aimCell(&ax, &ay)) {
        const Cell& c = g_world.at(ax, ay);
        const int t = (int)g_world.temp[ay * SIM_W + ax] - TEMP_OFFSET;
        const char* name = (c.mat == MAT_EMPTY) ? "Air" : MATS[c.mat].name;
        sprintf(s, "%s  %+d C", name, t);
        drawText(hdc, 10, y, RGB(196, 204, 216), s);
    }
    y += 15;
    {
        float vx = 0.0f, vy = 0.0f;
        if (aimCell(&ax, &ay)) g_world.windAt(ax, ay, vx, vy);
        sprintf(s, "wind %+.2f %+.2f   %d chunks%s", vx, vy, g_world.windChunks,
                (g_showWind || g_view == VIEW_AIR) ? "" : "   (W overlay)");
        drawText(hdc, 10, y, RGB(150, 158, 170), s);
    }
    y += 15;

    if (g_statusFrames > 0) {
        --g_statusFrames;
        drawText(hdc, 10, y, RGB(226, 190, 90), g_status);
    }
}

/* ==========================================================================
   Input
   ========================================================================== */
static void changeBrushRadius(int d) {
    g_brushRadius = imax(BRUSH_RADIUS_MIN, imin(BRUSH_RADIUS_MAX, g_brushRadius + d));
}

static void sizeFromSlider(int mx) {
    static const int thumbW = 12;
    const int travel = imax(1, (g_sizeTrack.right - g_sizeTrack.left) - thumbW);
    const int at = imax(0, imin(travel, mx - g_sizeTrack.left - thumbW / 2));
    const int range = BRUSH_RADIUS_MAX - BRUSH_RADIUS_MIN;
    g_brushRadius = BRUSH_RADIUS_MIN + (at * range + travel / 2) / travel;
}
static bool g_sizeDragging = false;
static bool g_palDragging  = false;

static void palScrollFromThumb(int my) {
    if (g_palMaxScroll <= 0) return;
    const int trackH = g_palTrack.bottom - g_palTrack.top;
    const int thumbH = g_palThumb.bottom - g_palThumb.top;
    const int travel = imax(1, trackH - thumbH);
    const int at = imax(0, imin(travel, my - g_palTrack.top - thumbH / 2));
    g_palScroll = (at * g_palMaxScroll + travel / 2) / travel;
}

static bool handlePanelClick(int mx, int my) {
    if (mx >= PANEL_W) return false;

    for (int i = 0; i < N_BRUSH; ++i) {
        if (IsRectEmpty(&g_palRect[i]) || !inRect(g_palRect[i], mx, my)) continue;
        g_brushMat = BRUSHES[i].brush;
        return true;
    }
    if (inRect(g_palTrack, mx, my)) { palScrollFromThumb(my); g_palDragging = true; return true; }
    if (inRect(g_sizeTrack, mx, my)) { sizeFromSlider(mx); g_sizeDragging = true; return true; }
    for (int i = 0; i < N_SPEED; ++i)
        if (inRect(g_speedRect[i], mx, my)) { g_speedIdx = i; return true; }
    for (int i = 0; i < N_SCALE; ++i)
        if (inRect(g_scaleRect[i], mx, my)) { setScale(i); return true; }
    for (int i = 0; i < PL_VIEW_COUNT; ++i)
        if (inRect(g_viewRect[i], mx, my)) { g_view = i; return true; }
    if (inRect(g_overwriteRect, mx, my)) { g_overwrite = !g_overwrite; return true; }
    if (inRect(g_bgRect, mx, my))        { g_bgLayer = !g_bgLayer; return true; }
    if (inRect(g_pauseRect, mx, my))     { g_paused = !g_paused; return true; }
    if (inRect(g_stepRect, mx, my))      { g_stepOnce = true; g_paused = true; return true; }
    if (inRect(g_clearRect, mx, my))     { buildWorld(); setStatus("World cleared"); return true; }
    return true;   /* the panel swallows clicks that miss a control */
}

/* Lay material along the path the mouse took since the last frame, not just at
   where it is now. A fast drag covers dozens of cells between frames, and
   painting only the endpoints leaves a dotted line. */
static void applyBrush() {
    if (g_uiCapture || (!g_lmb && !g_rmb)) { g_pmx = -1; return; }
    int ax, ay;
    if (!aimCell(&ax, &ay)) { g_pmx = -1; return; }
    if (g_pmx < 0) { g_pmx = ax; g_pmy = ay; }

    const int sel = g_rmb ? (int)MAT_EMPTY : g_brushMat;
    const int steps = imax(abs(ax - g_pmx), abs(ay - g_pmy));
    for (int i = 0; i <= steps; ++i) {
        const int px = steps ? g_pmx + (ax - g_pmx) * i / steps : ax;
        const int py = steps ? g_pmy + (ay - g_pmy) * i / steps : ay;
        if (sel == TOOL_HEAT)       g_world.heat(px, py, g_brushRadius,  HEAT_STEP);
        else if (sel == TOOL_COOL)  g_world.heat(px, py, g_brushRadius, -HEAT_STEP);
        else if (sel == TOOL_SPARK) shedPlace(px, py);
        else if (g_bgLayer)         g_world.paintBg(px, py, g_brushRadius, (u8)sel);
        else                        g_world.paint(px, py, g_brushRadius, (u8)sel, g_overwrite);
        if (!steps) break;
    }
    g_pmx = ax; g_pmy = ay;
}

/* A drag's own velocity, in cells a frame, becomes the wind under the brush.
   Scaled down and capped so a flick is a gust rather than a hurricane; the
   solver clamps anything faster anyway. */
static void applyWind() {
    int ax, ay;
    if (!g_mmb || !aimCell(&ax, &ay)) { g_wmx = -1; return; }
    if (g_wmx >= 0) {
        const float k = 0.15f;
        float dx = (float)(ax - g_wmx) * k, dy = (float)(ay - g_wmy) * k;
        const float m = sqrtf(dx * dx + dy * dy);
        if (m > 0.6f) { dx *= 0.6f / m; dy *= 0.6f / m; }
        if (m > 0.0f) g_world.pushWind(ax, ay, g_brushRadius, dx, dy);
    }
    g_wmx = ax; g_wmy = ay;
}

/* Speed to colour for the Air view: deep blue for a breath, through cyan and
   yellow, to white at half a cell a frame and up. Typical plume speeds are a
   tenth to a third of a cell a frame, which is the middle of the ramp. */
static u32 windColour(float sp) {
    const float t = sp * 2.0f > 1.0f ? 1.0f : sp * 2.0f;
    int r, g, b;
    if (t < 0.33f)      { const float k = t / 0.33f;          r = 30;                    g = (int)(80 + 150 * k);  b = 255; }
    else if (t < 0.66f) { const float k = (t - 0.33f) / 0.33f; r = (int)(30 + 225 * k);   g = 230;                  b = (int)(255 - 205 * k); }
    else                { const float k = (t - 0.66f) / 0.34f; r = 255;                   g = (int)(230 + 25 * k);  b = (int)(50 + 205 * k); }
    return 0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

/* The wind field as strokes from each block's centre, drawn into the cell
   buffer so it scales with the view. As an overlay (W) the strokes are short
   and only where there is wind. In the Air view they are longer, coloured by
   speed with a bright head so direction reads at a glance, and every block
   gets a dot, so still air is visibly still rather than just missing. */
static void drawWind(u32* px, int w, int h, bool airView) {
    const int ax0 = PLAY_ORIGIN_X >> WIND_SHIFT, ay0 = PLAY_ORIGIN_Y >> WIND_SHIFT;
    const int ax1 = (PLAY_ORIGIN_X + w - 1) >> WIND_SHIFT, ay1 = (PLAY_ORIGIN_Y + h - 1) >> WIND_SHIFT;
    for (int ay = ay0; ay <= ay1; ++ay)
        for (int ax = ax0; ax <= ax1; ++ax) {
            const int wi = ay * WIND_PITCH + ax;
            const float vx = g_world.windVX[wi], vy = g_world.windVY[wi];
            const float sp = sqrtf(vx * vx + vy * vy);
            const int cx = (ax << WIND_SHIFT) + WIND_CELL / 2 - PLAY_ORIGIN_X;
            const int cy = (ay << WIND_SHIFT) + WIND_CELL / 2 - PLAY_ORIGIN_Y;
            if (cx < 0 || cy < 0 || cx >= w || cy >= h) continue;
            if (airView) px[cy * w + cx] = 0xFF3A4254u;
            if (sp < (airView ? 0.005f : 0.01f)) continue;
            const int len = airView ? imin(14, 2 + (int)(sp * 24.0f))
                                    : imax(1, imin(12, (int)(sp * 4.0f + 0.5f)));
            u32 col;
            if (airView) col = windColour(sp);
            else {
                const int g = imin(255, 120 + (int)(sp * 160.0f));
                col = 0xFF000000u | ((u32)imin(255, (int)(sp * 200.0f)) << 16) | ((u32)g << 8) | 0xFFu;
            }
            const float ux = vx / sp, uy = vy / sp;
            for (int t = 0; t <= len; ++t) {
                const int x = cx + (int)(ux * (float)t + (ux < 0 ? -0.5f : 0.5f));
                const int y = cy + (int)(uy * (float)t + (uy < 0 ? -0.5f : 0.5f));
                if (x < 0 || y < 0 || x >= w || y >= h) break;
                /* The tail at half brightness, the last two cells full: an
                   arrowhead without the cost of drawing one. */
                px[y * w + x] = (!airView || t >= len - 1) ? col
                              : 0xFF000000u | ((col >> 1) & 0x7F7F7Fu);
            }
        }
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DESTROY: g_running = false; return 0;
    case WM_MOUSEMOVE:
        g_mx = (short)LOWORD(lp); g_my = (short)HIWORD(lp);
        if (g_sizeDragging) sizeFromSlider(g_mx);
        if (g_palDragging)  palScrollFromThumb(g_my);
        return 0;
    case WM_LBUTTONDOWN:
        g_mx = (short)LOWORD(lp); g_my = (short)HIWORD(lp);
        g_lmb = true;
        g_uiCapture = handlePanelClick(g_mx, g_my);
        SetCapture(hwnd);
        return 0;
    case WM_RBUTTONDOWN:
        g_mx = (short)LOWORD(lp); g_my = (short)HIWORD(lp);
        g_rmb = true;
        if (g_mx < PANEL_W) g_uiCapture = true;
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        g_lmb = false;
        g_sizeDragging = false;
        g_palDragging = false;
        if (!g_rmb) { g_uiCapture = false; g_pmx = -1; ReleaseCapture(); }
        return 0;
    case WM_MBUTTONDOWN:
        g_mx = (short)LOWORD(lp); g_my = (short)HIWORD(lp);
        g_mmb = true;
        SetCapture(hwnd);
        return 0;
    case WM_MBUTTONUP:
        g_mmb = false;
        g_wmx = -1;
        if (!g_lmb && !g_rmb) ReleaseCapture();
        return 0;
    case WM_RBUTTONUP:
        g_rmb = false;
        if (!g_lmb) { g_uiCapture = false; g_pmx = -1; ReleaseCapture(); }
        return 0;
    case WM_MOUSEWHEEL: {
        const int notches = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        POINT p = { (short)LOWORD(lp), (short)HIWORD(lp) };
        ScreenToClient(hwnd, &p);
        /* Over the palette it scrolls the palette; anywhere else it is the
           brush size, which is the thing you want the wheel for while drawing
           and the reason the wheel does not also pan or zoom here. */
        if (inRect(g_palArea, p.x, p.y) || inRect(g_palTrack, p.x, p.y))
            g_palScroll = imax(0, imin(g_palMaxScroll, g_palScroll - notches));
        else
            changeBrushRadius(notches * 2);
        return 0;
    }
    case WM_KEYDOWN:
        switch (wp) {
        case VK_ESCAPE: g_running = false; break;
        case VK_SPACE:  g_paused = !g_paused; break;
        case VK_OEM_PERIOD: g_stepOnce = true; g_paused = true; break;
        case VK_OEM_4: changeBrushRadius(-2); break;   /* [ */
        case VK_OEM_6: changeBrushRadius(2);  break;   /* ] */
        case 'V': g_view = (g_view + 1) % PL_VIEW_COUNT; break;
        case 'W': g_showWind = !g_showWind; break;
        case 'C': buildWorld(); setStatus("World cleared"); break;
        case '1': case '2': case '3':
            setScale((int)wp - '1');
            break;
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* ==========================================================================
   Presenting
   ========================================================================== */

/* Average two pixels channel-wise. The low bit of each channel is dropped
   before the shift so red cannot borrow from green; losing it costs one 255th
   of a channel and is not visible. */
static inline u32 avg2(u32 p, u32 q) {
    return ((p & 0xFEFEFEu) >> 1) + ((q & 0xFEFEFEu) >> 1);
}

/* Box-average the cell buffer down by `down` in each axis. Only ever called
   with down == 2 -- see the note on SCALES for why an average rather than a
   drop, and what it costs. */
struct ShrinkJob { int srcW, dstW, dstH, down; };
static const int SHRINK_BAND = 32;   /* output rows per job */
static void shrinkBand(void* ctx, int band) {
    const ShrinkJob& j = *(const ShrinkJob*)ctx;
    const int y1 = imin(j.dstH, (band + 1) * SHRINK_BAND);
    for (int y = band * SHRINK_BAND; y < y1; ++y) {
        const u32* a = g_cells + (y * j.down) * j.srcW;
        const u32* b = a + j.srcW;
        u32* out = g_shrunk + y * j.dstW;
        for (int x = 0; x < j.dstW; ++x) {
            const int sx = x * j.down;
            out[x] = avg2(avg2(a[sx], a[sx + 1]), avg2(b[sx], b[sx + 1]));
        }
    }
}
/* On the sim's pool, in bands of output rows, for the reason renderView is:
   the pool is idle while the frame is drawn, and every output row reads only
   its own two input rows. */
static void shrinkCells(int srcW, int srcH, int down) {
    ShrinkJob j = { srcW, srcW / down, srcH / down, down };
    simParallel((j.dstH + SHRINK_BAND - 1) / SHRINK_BAND, shrinkBand, &j);
}

/* Everything the cached panel shows, folded into one number. The mouse only
   counts while it is over the panel, where it decides which button is lit;
   painting in the world does not redraw the buttons. */
static u32 panelKey() {
    u32 h = 2166136261u;
    const int v[] = { g_mx < PANEL_W ? g_mx : -1, g_mx < PANEL_W ? g_my : -1,
                      g_brushMat, g_brushRadius, g_palScroll, g_speedIdx, g_scaleIdx,
                      g_view, g_overwrite, g_bgLayer, g_paused };
    for (unsigned k = 0; k < sizeof(v) / sizeof(v[0]); ++k) { h ^= (u32)v[k]; h *= 16777619u; }
    return h;
}

static void present(HWND hwnd, const LARGE_INTEGER& freq) {
    LARGE_INTEGER drawBegin, drawEnd;
    QueryPerformanceCounter(&drawBegin);
    const ScaleDef& s = SCALES[g_scaleIdx];
    g_cellCount = renderView(g_world, g_cells, g_view == VIEW_AIR ? (int)VIEW_MATERIAL : g_view,
                             PLAY_ORIGIN_X, PLAY_ORIGIN_Y, false,
                             s.cellsW, s.cellsH);
    if (g_view == VIEW_AIR) {
        /* The world at a third, so the wind is what you see and the world is
           only where it is. */
        const int n = s.cellsW * s.cellsH;
        for (int i = 0; i < n; ++i) {
            const u32 c = g_cells[i];
            g_cells[i] = 0xFF000000u | (((c >> 2) & 0x3F3F3Fu) + ((c >> 3) & 0x1F1F1Fu));
        }
        drawWind(g_cells, s.cellsW, s.cellsH, true);
    } else if (g_showWind) {
        drawWind(g_cells, s.cellsW, s.cellsH, false);
    }

    const u32* src = g_cells;
    int srcW = s.cellsW, srcH = s.cellsH;
    if (s.down > 1) {
        shrinkCells(s.cellsW, s.cellsH, s.down);
        src = g_shrunk;
        srcW = s.cellsW / s.down;
        srcH = s.cellsH / s.down;
    }
    g_bmi.bmiHeader.biWidth  = srcW;
    g_bmi.bmiHeader.biHeight = -srcH;   /* negative = top-down rows */
    StretchDIBits(g_backDC, PANEL_W, 0, VIEWPORT_W, VIEWPORT_H, 0, 0, srcW, srcH,
                  src, &g_bmi, DIB_RGB_COLORS, SRCCOPY);

    const u32 key = panelKey();
    if (!g_panelValid || key != g_panelKey) {
        layoutPanel();
        drawPanel(g_panelDC);
        g_panelKey = key;
        g_panelValid = true;
    }
    BitBlt(g_backDC, 0, 0, PANEL_W, g_statsTop, g_panelDC, 0, 0, SRCCOPY);
    drawStats(g_backDC);

    /* The brush outline, drawn on the window rather than into the cell buffer:
       at Quarter scale a one-cell ring would be half a pixel wide and would
       disappear, and it is a cursor, not part of the world. */
    int ax, ay;
    if (aimCell(&ax, &ay)) {
        const int px = PANEL_W + ((ax - PLAY_ORIGIN_X) * s.up) / s.down;
        const int py = ((ay - PLAY_ORIGIN_Y) * s.up) / s.down;
        const int r = imax(2, (g_brushRadius * s.up) / s.down);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(226, 190, 90));
        HGDIOBJ oldPen = SelectObject(g_backDC, pen);
        HGDIOBJ oldBrush = SelectObject(g_backDC, GetStockObject(NULL_BRUSH));
        Ellipse(g_backDC, px - r, py - r, px + r, py + r);
        SelectObject(g_backDC, oldBrush);
        SelectObject(g_backDC, oldPen);
        DeleteObject(pen);
    }

    HDC hdc = GetDC(hwnd);
    BitBlt(hdc, 0, 0, WIN_W, WIN_H, g_backDC, 0, 0, SRCCOPY);
    ReleaseDC(hwnd, hdc);

    QueryPerformanceCounter(&drawEnd);
    {
        const double ms = 1000.0 * (double)(drawEnd.QuadPart - drawBegin.QuadPart) /
                          (double)freq.QuadPart;
        g_drawMs = g_drawMs > 0.0 ? g_drawMs * 0.9 + ms * 0.1 : ms;
    }
}

/* ==========================================================================
   Entry
   ========================================================================== */
static int simThreadsAuto(void) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    return imax(1, imin(MAX_LANE_THREADS, (int)si.dwNumberOfProcessors));
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    simSetWorkers(simThreadsAuto());
    initMaterials();
    buildWorld();

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = wndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_CROSS);
    wc.lpszClassName = "PowderlikeWnd";
    RegisterClassA(&wc);

    DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME);
    RECT r = { 0, 0, WIN_W, WIN_H };
    AdjustWindowRect(&r, style, FALSE);
    HWND hwnd = CreateWindowA("PowderlikeWnd", "powderlike", style,
                              CW_USEDEFAULT, CW_USEDEFAULT,
                              r.right - r.left, r.bottom - r.top,
                              NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOW);

    g_font = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    {
        HDC sdc = GetDC(hwnd);
        g_backDC     = CreateCompatibleDC(sdc);
        g_backBmp    = CreateCompatibleBitmap(sdc, WIN_W, WIN_H);
        g_backOldBmp = SelectObject(g_backDC, g_backBmp);
        g_panelDC     = CreateCompatibleDC(sdc);
        g_panelBmp    = CreateCompatibleBitmap(sdc, PANEL_W, WIN_H);
        g_panelOldBmp = SelectObject(g_panelDC, g_panelBmp);
        ReleaseDC(hwnd, sdc);
    }
    SetStretchBltMode(g_backDC, COLORONCOLOR);   /* nearest neighbour: crisp pixels */

    g_panelBg     = CreateSolidBrush(RGB(26, 28, 34));
    g_btnBg       = CreateSolidBrush(RGB(42, 46, 56));
    g_btnBgHot    = CreateSolidBrush(RGB(64, 70, 84));
    g_btnBgSel    = CreateSolidBrush(RGB(58, 64, 82));
    g_borderBrush = CreateSolidBrush(RGB(88, 94, 108));
    g_accentBrush = CreateSolidBrush(RGB(226, 190, 90));

    /* Swatches come from each material's own palette, so a button matches what
       lands in the world. Tools and the eraser get synthetic colours. */
    for (int i = 0; i < N_BRUSH; ++i) {
        const int b = BRUSHES[i].brush;
        COLORREF cr;
        if      (b == TOOL_HEAT)  cr = RGB(226, 96, 40);
        else if (b == TOOL_COOL)  cr = RGB(80, 152, 226);
        else if (b == TOOL_SPARK) cr = RGB(255, 216, 112);
        else if (b == MAT_EMPTY)  cr = RGB(38, 40, 48);
        else {
            const u32 c = g_colorLut[(b << 8) | 0x08];
            cr = RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
        }
        g_swatchBrush[i] = CreateSolidBrush(cr);
    }

    memset(&g_bmi, 0, sizeof(g_bmi));
    g_bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    g_bmi.bmiHeader.biPlanes      = 1;
    g_bmi.bmiHeader.biBitCount    = 32;
    g_bmi.bmiHeader.biCompression = BI_RGB;

    layoutPanel();
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

        applyBrush();
        applyWind();

        LARGE_INTEGER simBegin, simEnd;
        QueryPerformanceCounter(&simBegin);
        {
            /* The sim substeps, and everything that ticks alongside them. Trees
               are here because seeds growing is material behaviour; devices are
               here for the spark brush, whose shed lives in devTick. Rooms,
               entities, projectiles and player authority are not, because this
               front-end has none of them. */
            const int steps = g_stepOnce ? 1 : (g_paused ? 0 : SPEEDS[g_speedIdx]);
            for (int i = 0; i < steps; ++i) {
                g_world.step();
                devTick(g_world);
                treesTick(g_world);
            }
            g_stepOnce = false;
        }
        QueryPerformanceCounter(&simEnd);
        g_simMs = 1000.0 * (double)(simEnd.QuadPart - simBegin.QuadPart) /
                  (double)freq.QuadPart;

        present(hwnd, freq);

        ++fpsFrames;
        LARGE_INTEGER tNow;
        QueryPerformanceCounter(&tNow);
        {
            const double work = 1000.0 * (double)(tNow.QuadPart - tWorkBegin.QuadPart) /
                                (double)freq.QuadPart;
            g_frameMs = g_frameMs > 0.0 ? g_frameMs * 0.9 + work * 0.1 : work;
        }
        double elapsed = (double)(tNow.QuadPart - tPrev.QuadPart) / (double)freq.QuadPart;
        if (elapsed < FRAME_SECONDS) {
            const int ms = (int)((FRAME_SECONDS - elapsed) * 1000.0);
            if (ms > 1) Sleep(ms - 1);
            do {
                QueryPerformanceCounter(&tNow);
                elapsed = (double)(tNow.QuadPart - tPrev.QuadPart) / (double)freq.QuadPart;
            } while (elapsed < FRAME_SECONDS);
        }
        tPrev = tNow;

        const double since = (double)(tNow.QuadPart - tFpsBase.QuadPart) /
                             (double)freq.QuadPart;
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
    SelectObject(g_panelDC, g_panelOldBmp);
    DeleteObject(g_panelBmp);
    DeleteDC(g_panelDC);
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
