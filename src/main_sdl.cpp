/* Portable shell: window, input, timing and blit on SDL2.

   This is the only file that knows a platform exists. The simulation
   (world/materials/render) is plain C++11 and is compiled unchanged on every
   target -- which is the whole reason the port is small: if the physics was
   right on Windows it is right on macOS, because it is the same object code
   built from the same source with no platform #ifdefs anywhere in it.

   main.cpp is the original Win32/GDI shell and is still the default on
   Windows, so that build keeps its "no external dependencies" property. This
   file is what runs on macOS and Linux, and can be selected on Windows too
   (see the Makefile). The two are deliberately kept behaviourally identical --
   same layout constants, same hit-testing, same key bindings -- so there is
   one program with two backends rather than two diverging programs. */

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "world.h"
#include "render.h"
#include "font8.h"
#include "panel.h"   /* layout constants, tool ids and the BRUSHES palette,
                        shared with the Win32 shell so a new material appears
                        on both backends from one edit */

static u32 g_pixels[SIM_W * SIM_H];

static SDL_Window*   g_win = NULL;
static SDL_Renderer* g_ren = NULL;
static SDL_Texture*  g_tex = NULL;
static bool          g_running = true;

/* Layout rects, filled once by layoutPanel(). */
static SDL_Rect g_brushRect[N_BRUSH];
static SDL_Rect g_actRect[N_ACT];
static SDL_Rect g_sizeDec, g_sizeInc, g_sizeBox;

struct Color { u8 r, g, b; };
static const Color C_PANEL_BG = { 26,  28,  34  };
static const Color C_BTN_BG   = { 42,  46,  56  };
static const Color C_BTN_HOT  = { 64,  70,  84  };
static const Color C_BTN_SEL  = { 58,  64,  82  };
static const Color C_BORDER   = { 88,  94,  108 };
static const Color C_ACCENT   = { 226, 190, 90  };
static const Color C_LABEL    = { 214, 216, 224 };
static const Color C_LABEL_SEL= { 245, 224, 150 };
static const Color C_TITLE    = { 240, 240, 246 };
static const Color C_STAT     = { 170, 178, 190 };
static const Color C_FPS      = { 150, 200, 150 };

static Color g_swatch[N_BRUSH];

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

/* stats */
static double g_fps = 0.0, g_simMs = 0.0;
static int    g_cellCount = 0;

static bool inRect(const SDL_Rect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/* ======================================================================
   Drawing primitives
   ====================================================================== */
static void setColor(const Color& c) {
    SDL_SetRenderDrawColor(g_ren, c.r, c.g, c.b, 255);
}

static void fillRect(const SDL_Rect& r, const Color& c) {
    setColor(c);
    SDL_RenderFillRect(g_ren, &r);
}

/* Outline only, one pixel, drawn inside the rect -- matches GDI's FrameRect. */
static void frameRect(const SDL_Rect& r, const Color& c) {
    setColor(c);
    SDL_RenderDrawRect(g_ren, &r);
}

/* Text. Every lit pixel of the string is accumulated into one rect array and
   handed to SDL in a single call: a per-pixel SDL_RenderFillRect would be
   thousands of draw calls a frame, which is exactly the kind of object churn
   the GDI shell was careful to avoid. */
static void drawText(int x, int y, const Color& c, int scale, const char* s) {
    static SDL_Rect px[4096];
    int n = 0;

    for (int i = 0; s[i]; ++i) {
        unsigned ch = (unsigned char)s[i];
        if (ch < 32 || ch > 126) ch = '?';
        const u8* glyph = FONT5X7[ch - 32];
        int gx = x + i * FONT_ADVANCE * scale;

        for (int col = 0; col < FONT_W; ++col) {
            u8 bits = glyph[col];
            if (!bits) continue;
            for (int row = 0; row < FONT_H; ++row) {
                if (!(bits & (1 << row))) continue;
                if (n == (int)(sizeof(px) / sizeof(px[0]))) goto flush;
                px[n].x = gx + col * scale;
                px[n].y = y + row * scale;
                px[n].w = scale;
                px[n].h = scale;
                ++n;
            }
        }
    }
flush:
    if (n) {
        setColor(c);
        SDL_RenderFillRects(g_ren, px, n);
    }
}

static void drawTextIn(const SDL_Rect& r, int leftX, bool center,
                       const Color& c, int scale, const char* s) {
    int x = center ? r.x + (r.w - textWidth(s, scale)) / 2 : leftX;
    int y = r.y + (r.h - FONT_H * scale) / 2;
    drawText(x, y, c, scale, s);
}

/* ======================================================================
   Layout
   ====================================================================== */
static void layoutPanel() {
    const int pad = 10, w = PANEL_W - pad * 2, h = 24, gap = 5;
    int y = 34;   /* leave room for the title */

    for (int i = 0; i < N_BRUSH; ++i) {
        g_brushRect[i].x = pad; g_brushRect[i].y = y;
        g_brushRect[i].w = w;   g_brushRect[i].h = h;
        y += h + gap;
    }

    y += 6;
    /* brush size: [-]  size N  [+] */
    g_sizeDec.x = pad;          g_sizeDec.y = y; g_sizeDec.w = 24;     g_sizeDec.h = h;
    g_sizeBox.x = pad + 28;     g_sizeBox.y = y; g_sizeBox.w = w - 56; g_sizeBox.h = h;
    g_sizeInc.x = pad + w - 24; g_sizeInc.y = y; g_sizeInc.w = 24;     g_sizeInc.h = h;
    y += h + gap + 6;

    for (int i = 0; i < N_ACT; ++i) {
        g_actRect[i].x = pad; g_actRect[i].y = y;
        g_actRect[i].w = w;   g_actRect[i].h = h;
        y += h + gap;
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
    if (inRect(g_sizeDec, mx, my)) { changeSize(-1); return true; }
    if (inRect(g_sizeInc, mx, my)) { changeSize(+1); return true; }
    if (inRect(g_actRect[ACT_OVERWRITE], mx, my)) { g_overwrite = !g_overwrite; return true; }
    if (inRect(g_actRect[ACT_VIEW],      mx, my)) { cycleView();                return true; }
    if (inRect(g_actRect[ACT_PAUSE],     mx, my)) { g_paused = !g_paused;       return true; }
    if (inRect(g_actRect[ACT_CLEAR],     mx, my)) { g_world.reset();            return true; }
    /* Any other spot on the panel is dead space: swallow it so it never paints. */
    return mx < PANEL_W;
}

static void handleKey(SDL_Keycode k) {
    /* Shortcuts still work -- they are just no longer the only way in. */
    switch (k) {
    case SDLK_1: g_brushMat = MAT_SAND;  break;
    case SDLK_2: g_brushMat = MAT_WATER; break;
    case SDLK_3: g_brushMat = MAT_DIRT;  break;
    case SDLK_4: g_brushMat = MAT_STONE; break;
    case SDLK_5: g_brushMat = MAT_FIRE;  break;
    case SDLK_6: g_brushMat = MAT_STEAM; break;
    case SDLK_7: g_brushMat = MAT_WOOD;  break;
    case SDLK_8: g_brushMat = MAT_IRON;  break;
    case SDLK_9: g_brushMat = MAT_LAVA;  break;
    case SDLK_b: g_brushMat = MAT_WALL;  break;
    case SDLK_0:
    case SDLK_e: g_brushMat = MAT_EMPTY; break;
    case SDLK_h: g_brushMat = TOOL_HEAT; break;
    case SDLK_j: g_brushMat = TOOL_COOL; break;
    case SDLK_c: g_world.reset();        break;
    case SDLK_o: g_overwrite = !g_overwrite; break;
    case SDLK_v: cycleView();                break;
    case SDLK_SPACE:  g_paused = !g_paused;  break;
    case SDLK_PERIOD: g_stepOnce = true;     break;
    case SDLK_LEFTBRACKET:  changeSize(-1);  break;
    case SDLK_RIGHTBRACKET: changeSize(+1);  break;
    case SDLK_ESCAPE: g_running = false;     break;
    default: break;
    }
}

static void pumpEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            g_running = false;
            break;

        case SDL_MOUSEMOTION:
            g_mx = e.motion.x;
            g_my = e.motion.y;
            break;

        case SDL_MOUSEBUTTONDOWN:
            g_mx = e.button.x;
            g_my = e.button.y;
            if (e.button.button == SDL_BUTTON_LEFT) {
                if (handlePanelClick(g_mx, g_my)) g_uiCapture = true;
                else                              g_lmb = true;
                SDL_CaptureMouse(SDL_TRUE);
            } else if (e.button.button == SDL_BUTTON_RIGHT) {
                if (g_mx >= PANEL_W) g_rmb = true;   /* right-drag erases, but only over the sim */
                SDL_CaptureMouse(SDL_TRUE);
            }
            break;

        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT)  { g_lmb = false; g_uiCapture = false; }
            if (e.button.button == SDL_BUTTON_RIGHT) { g_rmb = false; }
            if (!g_lmb && !g_rmb) SDL_CaptureMouse(SDL_FALSE);
            break;

        case SDL_MOUSEWHEEL: {
            /* macOS "natural" scrolling reports a flipped wheel; honour the flag
               so the size stepper goes the way the user's system says it should. */
            int dy = e.wheel.y;
            if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) dy = -dy;
            if (dy) changeSize(dy > 0 ? 1 : -1);
            break;
        }

        case SDL_KEYDOWN:
            handleKey(e.key.keysym.sym);
            break;
        }
    }
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
   Panel
   ====================================================================== */
/* A generic panel button: filled background, framed, centred (or left-of-
   swatch) label. selected gets an accent frame, hovered a lighter fill. */
static void drawButton(const SDL_Rect& r, const char* label,
                       const Color* swatch, bool selected, bool hot) {
    fillRect(r, selected ? C_BTN_SEL : (hot ? C_BTN_HOT : C_BTN_BG));

    int textX = r.x + 8;
    if (swatch) {
        SDL_Rect sw = { r.x + 5, r.y + 4, 16, r.h - 8 };
        fillRect(sw, *swatch);
        frameRect(sw, C_BORDER);
        textX = sw.x + sw.w + 7;
    }

    frameRect(r, selected ? C_ACCENT : C_BORDER);
    drawTextIn(r, textX, false, selected ? C_LABEL_SEL : C_LABEL, 1, label);
}

static void drawPanel() {
    SDL_Rect panel = { 0, 0, PANEL_W, WIN_H };
    fillRect(panel, C_PANEL_BG);
    drawText(10, 8, C_TITLE, 2, "POWDER");

    for (int i = 0; i < N_BRUSH; ++i) {
        bool sel = (BRUSHES[i].brush == g_brushMat);
        bool hot = inRect(g_brushRect[i], g_mx, g_my);
        drawButton(g_brushRect[i], BRUSHES[i].label, &g_swatch[i], sel, hot);
    }

    /* brush size row */
    drawButton(g_sizeDec, "-", NULL, false, inRect(g_sizeDec, g_mx, g_my));
    drawButton(g_sizeInc, "+", NULL, false, inRect(g_sizeInc, g_mx, g_my));
    {
        char s[32];
        fillRect(g_sizeBox, C_BTN_BG);
        frameRect(g_sizeBox, C_BORDER);
        snprintf(s, sizeof(s), "Size %d", g_brushRadius);
        drawTextIn(g_sizeBox, 0, true, C_LABEL, 1, s);
    }

    /* toggles / actions, with live state in the label */
    char lbl[32];
    snprintf(lbl, sizeof(lbl), "Overwrite: %s", g_overwrite ? "On" : "Off");
    drawButton(g_actRect[ACT_OVERWRITE], lbl, NULL, !g_overwrite, inRect(g_actRect[ACT_OVERWRITE], g_mx, g_my));
    const char* vn = g_view == VIEW_NORMAL ? "View: Glow" :
                     g_view == VIEW_MATERIAL ? "View: Material" : "View: Heat";
    drawButton(g_actRect[ACT_VIEW], vn, NULL, g_view != VIEW_NORMAL, inRect(g_actRect[ACT_VIEW], g_mx, g_my));
    drawButton(g_actRect[ACT_PAUSE], g_paused ? "Paused" : "Pause", NULL, g_paused, inRect(g_actRect[ACT_PAUSE], g_mx, g_my));
    drawButton(g_actRect[ACT_CLEAR], "Clear", NULL, false, inRect(g_actRect[ACT_CLEAR], g_mx, g_my));

    /* stats, bottom of the panel */
    char s[64];
    int sy = WIN_H - 70;
    snprintf(s, sizeof(s), "%.0f fps", g_fps);      drawText(10, sy,      C_FPS,  1, s);
    snprintf(s, sizeof(s), "sim %.2f ms", g_simMs); drawText(10, sy + 16, C_STAT, 1, s);
    snprintf(s, sizeof(s), "cells %d", g_cellCount);drawText(10, sy + 32, C_STAT, 1, s);
    snprintf(s, sizeof(s), "chunks %d/%d", g_world.activeChunks, CHUNK_COUNT);
    drawText(10, sy + 48, C_STAT, 1, s);
}

/* ======================================================================
   Entry point
   ====================================================================== */
int main(int argc, char** argv) {
    (void)argc; (void)argv;

    initMaterials();
    g_world.reset();
    layoutPanel();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    /* Nearest-neighbour everywhere: the sim is pixel art, and on a Retina Mac
       the logical-size scaling below would otherwise blur it. */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    /* Shrink to fit if the display cannot hold the full-size window. The Win32
       shell asks for a fixed WIN_W x WIN_H and simply hangs off the bottom of a
       screen too short for it; that is survivable on a desktop but not on a
       laptop, and a 13" Mac is short enough to hit it once the menu bar and
       title bar are taken out. Usable bounds already exclude the menu bar and
       Dock on macOS and the taskbar on Windows. */
    int winW = WIN_W, winH = WIN_H;
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0) {
        const int TITLEBAR = 40;   /* no portable way to ask; this is generous */
        int availW = usable.w, availH = usable.h - TITLEBAR;
        if (availW > 0 && availH > 0 && (winW > availW || winH > availH)) {
            double f = (double)availW / winW;
            double fh = (double)availH / winH;
            if (fh < f) f = fh;
            winW = (int)(winW * f);
            winH = (int)(winH * f);
        }
    }

    g_win = SDL_CreateWindow("Powder",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             winW, winH,
                             SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    if (!g_win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    /* No PRESENTVSYNC on purpose: the loop paces itself to 60 Hz below, and a
       120 Hz ProMotion display would otherwise run the sim at double speed. */
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    if (!g_ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_win);
        SDL_Quit();
        return 1;
    }

    /* Draw in fixed WIN_W x WIN_H coordinates whatever the backing store is.
       On a Retina display the drawable is 2x, and this scales the whole UI up
       crisply; it also maps incoming mouse events back into these same
       coordinates. That is what makes the shrink-to-fit above and the resizable
       window free: the layout, drawing and hit-testing code never learns that
       the window is not exactly WIN_W x WIN_H. */
    SDL_RenderSetLogicalSize(g_ren, WIN_W, WIN_H);

    /* RGB888 is SDL's name for X8R8G8B8 -- byte-identical to the 0x00RRGGBB
       words renderWorld() already produces, so the sim buffer uploads with no
       conversion pass, exactly as it did into a BI_RGB DIB. */
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGB888,
                              SDL_TEXTUREACCESS_STREAMING, SIM_W, SIM_H);
    if (!g_tex) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        SDL_DestroyRenderer(g_ren);
        SDL_DestroyWindow(g_win);
        SDL_Quit();
        return 1;
    }
    SDL_SetTextureBlendMode(g_tex, SDL_BLENDMODE_NONE);

    for (int i = 0; i < N_BRUSH; ++i) {
        u32 c = brushSwatch(BRUSHES[i].brush);
        Color k = { (u8)((c >> 16) & 0xFF), (u8)((c >> 8) & 0xFF), (u8)(c & 0xFF) };
        g_swatch[i] = k;
    }

    const u64 freq = SDL_GetPerformanceFrequency();
    u64 tPrev = SDL_GetPerformanceCounter();
    u64 tFpsBase = tPrev;
    int fpsFrames = 0;

    while (g_running) {
        pumpEvents();
        if (!g_running) break;

        applyBrush();

        u64 tA = SDL_GetPerformanceCounter();
        if (!g_paused || g_stepOnce) {
            g_world.step();
            g_stepOnce = false;
        }
        u64 tB = SDL_GetPerformanceCounter();
        g_simMs = 1000.0 * (double)(tB - tA) / (double)freq;

        g_cellCount = renderWorld(g_world, g_pixels, g_view);

        SDL_UpdateTexture(g_tex, NULL, g_pixels, SIM_W * (int)sizeof(u32));

        SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
        SDL_RenderClear(g_ren);
        SDL_Rect dst = { PANEL_W, 0, VIEW_W, VIEW_H };
        SDL_RenderCopy(g_ren, g_tex, NULL, &dst);
        drawPanel();
        SDL_RenderPresent(g_ren);

        /* Pace to 60 Hz. */
        ++fpsFrames;
        u64 tNow = SDL_GetPerformanceCounter();
        double elapsed = (double)(tNow - tPrev) / (double)freq;
        if (elapsed < FRAME_SECONDS) {
            int ms = (int)((FRAME_SECONDS - elapsed) * 1000.0);
            if (ms > 1) SDL_Delay((u32)(ms - 1));
            do {
                tNow = SDL_GetPerformanceCounter();
                elapsed = (double)(tNow - tPrev) / (double)freq;
            } while (elapsed < FRAME_SECONDS);
        }
        tPrev = tNow;

        double since = (double)(tNow - tFpsBase) / (double)freq;
        if (since >= 0.25) {
            g_fps = fpsFrames / since;
            fpsFrames = 0;
            tFpsBase = tNow;
        }
    }

    SDL_DestroyTexture(g_tex);
    SDL_DestroyRenderer(g_ren);
    SDL_DestroyWindow(g_win);
    SDL_Quit();
    return 0;
}
