/* ============================================================================
   gdi.cpp -- the drawing half of the Win32 shim.

   Everything the game's interface is made of lands here: 63 FillRects, 64
   DrawTextAs, the framed buttons, the icon blits and the one StretchDIBits
   that puts the world on screen. main.cpp is not aware any of this exists --
   it makes the same calls it makes on Windows.

   TWO PIXEL ORDERS LIVE IN THIS FILE and keeping them apart is the single
   thing most likely to go wrong:

     COLORREF  0x00BBGGRR  -- what RGB() builds, what brushes and pens carry
     buffer    0x00RRGGBB  -- what renderView writes and what a 32bpp BI_RGB
                              DIB holds, so blits copy straight through

   Anything arriving as a COLORREF goes through crToPix(). Anything arriving
   as image data does NOT. Getting this backwards swaps red and blue, which
   looks like a vague art problem rather than a bug with an address.
   ========================================================================== */
#include "win32.h"
#include "font8x8.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* --- objects -------------------------------------------------------------
   `stock` marks the handles GetStockObject hands out. Those are owned by the
   shim and shared, so DeleteObject must decline to free them -- main.cpp
   passes stock brushes to the same cleanup paths as its own. */
struct WBrush  { COLORREF colour; bool hollow; bool stock; };
struct WPen    { COLORREF colour; int width; int style; bool stock; };
struct WFont   { int height; };
struct WBitmap { int w, h; uint32_t* px; };

struct WDC {
    uint32_t* px;
    int       w, h;
    WBitmap*  bmp;          /* selected bitmap, if this is a memory DC */
    WPen*     pen;
    WBrush*   brush;
    WFont*    font;
    COLORREF  textColour;
    COLORREF  bkColour;
    int       bkMode;
    int       stretchMode;
    int       curX, curY;   /* MoveToEx/LineTo cursor */
    bool      ownsPixels;
};

static inline uint32_t crToPix(COLORREF c) {
    return ((c & 0x000000FFu) << 16) | (c & 0x0000FF00u) | ((c >> 16) & 0xFFu);
}

static inline int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ---------------------------------------------------------------------------
   Rect helpers -- pure arithmetic, identical to the Win32 originals.
   --------------------------------------------------------------------------- */
extern "C" BOOL SetRect(RECT* r, int left, int top, int right, int bottom) {
    if (!r) return FALSE;
    r->left = left; r->top = top; r->right = right; r->bottom = bottom;
    return TRUE;
}
extern "C" BOOL SetRectEmpty(RECT* r) {
    if (!r) return FALSE;
    r->left = r->top = r->right = r->bottom = 0;
    return TRUE;
}
extern "C" BOOL IsRectEmpty(const RECT* r) {
    if (!r) return TRUE;
    return (r->right <= r->left || r->bottom <= r->top) ? TRUE : FALSE;
}
extern "C" BOOL InflateRect(RECT* r, int dx, int dy) {
    if (!r) return FALSE;
    r->left -= dx; r->right += dx; r->top -= dy; r->bottom += dy;
    return TRUE;
}
extern "C" BOOL PtInRect(const RECT* r, POINT pt) {
    if (!r) return FALSE;
    return (pt.x >= r->left && pt.x < r->right &&
            pt.y >= r->top  && pt.y < r->bottom) ? TRUE : FALSE;
}

/* ---------------------------------------------------------------------------
   Object lifetime
   --------------------------------------------------------------------------- */
/* Which flavour of object a handle is cannot be recovered from a void*, so
   every deletable object carries a tag word as its first member. Rather than
   add one, the shim keeps a small registry: objects are few and short-lived,
   and a wrong guess here would corrupt the heap. */
enum ObjKind { OBJ_BRUSH = 1, OBJ_PEN, OBJ_FONT, OBJ_BITMAP };

struct ObjEntry { void* p; int kind; };
static ObjEntry* g_objs = 0;
static int       g_objCount = 0, g_objCap = 0;

static void objRegister(void* p, int kind) {
    if (g_objCount == g_objCap) {
        g_objCap = g_objCap ? g_objCap * 2 : 256;
        g_objs = (ObjEntry*)realloc(g_objs, (size_t)g_objCap * sizeof(ObjEntry));
    }
    g_objs[g_objCount].p = p;
    g_objs[g_objCount].kind = kind;
    ++g_objCount;
}

static int objKind(void* p) {
    for (int i = g_objCount - 1; i >= 0; --i) if (g_objs[i].p == p) return g_objs[i].kind;
    return 0;
}

static void objForget(void* p) {
    for (int i = g_objCount - 1; i >= 0; --i)
        if (g_objs[i].p == p) { g_objs[i] = g_objs[--g_objCount]; return; }
}

static WBrush g_stockHollow = { 0, true,  true };
static WBrush g_stockWhite  = { 0x00FFFFFF, false, true };
static WBrush g_stockBlack  = { 0x00000000, false, true };

extern "C" HBRUSH CreateSolidBrush(COLORREF c) {
    WBrush* b = new WBrush;
    b->colour = c; b->hollow = false; b->stock = false;
    objRegister(b, OBJ_BRUSH);
    return (HBRUSH)b;
}

extern "C" HPEN CreatePen(int style, int width, COLORREF c) {
    WPen* p = new WPen;
    p->colour = c; p->width = width < 1 ? 1 : width; p->style = style; p->stock = false;
    objRegister(p, OBJ_PEN);
    return (HPEN)p;
}

extern "C" HFONT CreateFontA(int height, int, int, int, int,
                             DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                             DWORD, DWORD, LPCSTR) {
    WFont* f = new WFont;
    /* A negative height is Win32 for "this is the em size, not the cell size".
       The game passes a positive cell height; take the magnitude either way. */
    f->height = height < 0 ? -height : height;
    if (f->height < 6) f->height = 6;
    objRegister(f, OBJ_FONT);
    return (HFONT)f;
}

extern "C" HGDIOBJ GetStockObject(int which) {
    switch (which) {
    case HOLLOW_BRUSH: return (HGDIOBJ)&g_stockHollow;
    case WHITE_BRUSH:  return (HGDIOBJ)&g_stockWhite;
    case BLACK_BRUSH:  return (HGDIOBJ)&g_stockBlack;
    default:           return (HGDIOBJ)&g_stockHollow;
    }
}

extern "C" BOOL DeleteObject(HGDIOBJ o) {
    if (!o) return FALSE;
    if (o == (HGDIOBJ)&g_stockHollow || o == (HGDIOBJ)&g_stockWhite ||
        o == (HGDIOBJ)&g_stockBlack) return TRUE;      /* stock objects persist */
    const int kind = objKind(o);
    objForget(o);
    switch (kind) {
    case OBJ_BRUSH:  delete (WBrush*)o;  break;
    case OBJ_PEN:    delete (WPen*)o;    break;
    case OBJ_FONT:   delete (WFont*)o;   break;
    case OBJ_BITMAP: { WBitmap* b = (WBitmap*)o; free(b->px); delete b; break; }
    default: return FALSE;                              /* already gone */
    }
    return TRUE;
}

extern "C" HGDIOBJ SelectObject(HDC dc, HGDIOBJ o) {
    if (!dc || !o) return 0;
    /* Stock brushes never pass through objRegister, so recognise them first. */
    if (o == (HGDIOBJ)&g_stockHollow || o == (HGDIOBJ)&g_stockWhite ||
        o == (HGDIOBJ)&g_stockBlack) {
        HGDIOBJ old = (HGDIOBJ)dc->brush;
        dc->brush = (WBrush*)o;
        return old;
    }
    switch (objKind(o)) {
    case OBJ_BRUSH:  { HGDIOBJ old = (HGDIOBJ)dc->brush; dc->brush = (WBrush*)o; return old; }
    case OBJ_PEN:    { HGDIOBJ old = (HGDIOBJ)dc->pen;   dc->pen   = (WPen*)o;   return old; }
    case OBJ_FONT:   { HGDIOBJ old = (HGDIOBJ)dc->font;  dc->font  = (WFont*)o;  return old; }
    case OBJ_BITMAP: {
        HGDIOBJ old = (HGDIOBJ)dc->bmp;
        WBitmap* b = (WBitmap*)o;
        dc->bmp = b;
        dc->px  = b->px;
        dc->w   = b->w;
        dc->h   = b->h;
        return old;
    }
    default: return 0;
    }
}

/* ---------------------------------------------------------------------------
   Device contexts
   --------------------------------------------------------------------------- */
static WDC* dcNew(uint32_t* px, int w, int h, bool owns) {
    WDC* dc = new WDC;
    memset(dc, 0, sizeof(*dc));
    dc->px = px; dc->w = w; dc->h = h;
    dc->textColour = 0x00FFFFFF;
    dc->bkColour   = 0x00FFFFFF;
    dc->bkMode     = OPAQUE;      /* the Win32 default; main.cpp overrides it */
    dc->stretchMode = COLORONCOLOR;
    dc->ownsPixels = owns;
    return dc;
}

extern "C" HDC CreateCompatibleDC(HDC) {
    /* A fresh memory DC has a 1x1 bitmap in Win32 too, so drawing before a
       real bitmap is selected goes nowhere instead of anywhere. */
    static uint32_t dummy = 0;
    return (HDC)dcNew(&dummy, 1, 1, false);
}

extern "C" BOOL DeleteDC(HDC dc) {
    if (!dc) return FALSE;
    if (dc->ownsPixels) free(dc->px);
    delete dc;
    return TRUE;
}

extern "C" HBITMAP CreateCompatibleBitmap(HDC, int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    WBitmap* b = new WBitmap;
    b->w = w; b->h = h;
    b->px = (uint32_t*)calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    objRegister(b, OBJ_BITMAP);
    return (HBITMAP)b;
}

extern "C" int SetStretchBltMode(HDC dc, int mode) {
    if (!dc) return 0;
    const int old = dc->stretchMode;
    dc->stretchMode = mode;
    return old;
}

extern "C" uint32_t* wdcPixels(HDC dc) { return dc ? dc->px : 0; }
extern "C" int       wdcWidth (HDC dc) { return dc ? dc->w  : 0; }
extern "C" int       wdcHeight(HDC dc) { return dc ? dc->h  : 0; }

/* Shared with window.cpp, which builds the one DC that owns the frame. */
HDC wdcCreateOwned(int w, int h) {
    uint32_t* px = (uint32_t*)calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    return (HDC)dcNew(px, w, h, true);
}

/* ---------------------------------------------------------------------------
   Primitive raster operations
   --------------------------------------------------------------------------- */
static inline void putPixel(WDC* dc, int x, int y, uint32_t p) {
    if (x < 0 || y < 0 || x >= dc->w || y >= dc->h) return;
    dc->px[(size_t)y * dc->w + x] = p;
}

static void fillSpan(WDC* dc, int x0, int x1, int y, uint32_t p) {
    if (y < 0 || y >= dc->h) return;
    if (x0 < 0) x0 = 0;
    if (x1 > dc->w) x1 = dc->w;
    uint32_t* row = dc->px + (size_t)y * dc->w;
    for (int x = x0; x < x1; ++x) row[x] = p;
}

extern "C" int FillRect(HDC dc, const RECT* r, HBRUSH br) {
    if (!dc || !r || !br) return 0;
    WBrush* b = (WBrush*)br;
    if (b->hollow) return 1;
    const uint32_t p = crToPix(b->colour);
    const int y0 = iclamp((int)r->top, 0, dc->h), y1 = iclamp((int)r->bottom, 0, dc->h);
    for (int y = y0; y < y1; ++y) fillSpan(dc, (int)r->left, (int)r->right, y, p);
    return 1;
}

extern "C" int FrameRect(HDC dc, const RECT* r, HBRUSH br) {
    if (!dc || !r || !br) return 0;
    WBrush* b = (WBrush*)br;
    if (b->hollow) return 1;
    const uint32_t p = crToPix(b->colour);
    const int l = (int)r->left, t = (int)r->top, rt = (int)r->right, bt = (int)r->bottom;
    if (rt <= l || bt <= t) return 1;
    /* One pixel on every side, matching the Win32 border width. */
    fillSpan(dc, l, rt, t, p);
    fillSpan(dc, l, rt, bt - 1, p);
    for (int y = t; y < bt; ++y) { putPixel(dc, l, y, p); putPixel(dc, rt - 1, y, p); }
    return 1;
}

/* A pen stamp: `width` square centred on the point, which is close enough to
   what GDI does for the 1px and 3px pens this game uses. */
static void stamp(WDC* dc, int x, int y, int width, uint32_t p) {
    if (width <= 1) { putPixel(dc, x, y, p); return; }
    const int half = width / 2;
    for (int dy = -half; dy <= half; ++dy)
        for (int dx = -half; dx <= half; ++dx) putPixel(dc, x + dx, y + dy, p);
}

static void lineRaw(WDC* dc, int x0, int y0, int x1, int y1, WPen* pen) {
    const uint32_t p = crToPix(pen->colour);
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int step = 0;
    for (;;) {
        /* PS_DOT alternates on and off along the run, which is all the brush
           outline needs it for. */
        if (pen->style != PS_DOT || ((step >> 1) & 1) == 0)
            stamp(dc, x0, y0, pen->width, p);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
        ++step;
    }
}

extern "C" BOOL MoveToEx(HDC dc, int x, int y, POINT* old) {
    if (!dc) return FALSE;
    if (old) { old->x = dc->curX; old->y = dc->curY; }
    dc->curX = x; dc->curY = y;
    return TRUE;
}

extern "C" BOOL LineTo(HDC dc, int x, int y) {
    if (!dc || !dc->pen) return FALSE;
    lineRaw(dc, dc->curX, dc->curY, x, y, dc->pen);
    dc->curX = x; dc->curY = y;
    return TRUE;
}

extern "C" BOOL Polyline(HDC dc, const POINT* pts, int n) {
    if (!dc || !dc->pen || !pts || n < 2) return FALSE;
    for (int i = 1; i < n; ++i)
        lineRaw(dc, (int)pts[i-1].x, (int)pts[i-1].y, (int)pts[i].x, (int)pts[i].y, dc->pen);
    return TRUE;
}

extern "C" BOOL Ellipse(HDC dc, int l, int t, int r, int b) {
    if (!dc) return FALSE;
    if (r <= l || b <= t) return FALSE;
    const double cx = (l + r - 1) * 0.5, cy = (t + b - 1) * 0.5;
    const double rx = (r - l - 1) * 0.5, ry = (b - t - 1) * 0.5;
    if (rx <= 0.0 || ry <= 0.0) return FALSE;

    if (dc->brush && !dc->brush->hollow) {
        const uint32_t p = crToPix(dc->brush->colour);
        for (int y = t; y < b; ++y) {
            const double ny = (y - cy) / ry;
            const double inside = 1.0 - ny * ny;
            if (inside < 0.0) continue;
            const int half = (int)(rx * sqrt(inside));
            fillSpan(dc, (int)(cx - half), (int)(cx + half) + 1, y, p);
        }
    }
    if (dc->pen) {
        /* Stamped around the perimeter rather than derived from an inside
           test, so a 3px pen produces a 3px ring without a second pass. */
        const uint32_t p = crToPix(dc->pen->colour);
        const double bigger = rx > ry ? rx : ry;
        const int steps = (int)(bigger * 8.0) + 16;
        for (int i = 0; i < steps; ++i) {
            const double a = 6.283185307179586 * i / steps;
            stamp(dc, (int)(cx + rx * cos(a) + 0.5), (int)(cy + ry * sin(a) + 0.5),
                  dc->pen->width, p);
        }
    }
    return TRUE;
}

/* ---------------------------------------------------------------------------
   Text

   Advance width is derived from the font's cell height by the ratio Consolas
   actually has (~0.55), because the Windows layout was measured against
   Consolas and every column position in the panel depends on it. The glyph is
   drawn narrower than the advance and centred in it -- see font8x8.h.
   --------------------------------------------------------------------------- */
static int fontHeight(WDC* dc)  { return dc->font ? dc->font->height : 14; }
static int fontAdvance(int h)   { const int a = (h * 55 + 50) / 100; return a < 6 ? 6 : a; }
static int fontScale(int h)     { const int s = h / 10; return s < 1 ? 1 : s; }

static void drawGlyphRun(WDC* dc, int x, int y, const char* s, int n,
                         uint32_t px, const RECT* clip) {
    const int h  = fontHeight(dc);
    const int adv = fontAdvance(h);
    const int sc  = fontScale(h);
    const int gw  = FONT_COLS * sc;
    const int gy  = y + (h - FONT_ROWS * sc) / 2;
    const int bearing = (adv - gw) / 2;

    for (int i = 0; i < n; ++i) {
        int c = (unsigned char)s[i];
        if (c < FONT_FIRST || c > FONT_LAST) c = '?';
        const uint8_t* g = FONT8X8 + (size_t)(c - FONT_FIRST) * FONT_ROWS;
        const int ox = x + i * adv + bearing;
        for (int row = 0; row < FONT_ROWS; ++row) {
            const uint8_t bits = g[row];
            if (!bits) continue;
            for (int col = 0; col < FONT_COLS; ++col) {
                /* Glyph columns live in bits 6..2, most significant leftmost. */
                if (!(bits & (0x40 >> col))) continue;
                for (int sy = 0; sy < sc; ++sy)
                    for (int sx = 0; sx < sc; ++sx) {
                        const int qx = ox + col * sc + sx, qy = gy + row * sc + sy;
                        if (clip && (qx < clip->left || qx >= clip->right ||
                                     qy < clip->top  || qy >= clip->bottom)) continue;
                        putPixel(dc, qx, qy, px);
                    }
            }
        }
    }
}

extern "C" COLORREF SetTextColor(HDC dc, COLORREF c) {
    if (!dc) return 0;
    const COLORREF old = dc->textColour;
    dc->textColour = c;
    return old;
}

extern "C" int SetBkMode(HDC dc, int mode) {
    if (!dc) return 0;
    const int old = dc->bkMode;
    dc->bkMode = mode;
    return old;
}

extern "C" BOOL TextOutA(HDC dc, int x, int y, LPCSTR s, int len) {
    if (!dc || !s) return FALSE;
    if (len < 0) len = (int)strlen(s);
    if (dc->bkMode == OPAQUE) {
        RECT bg = { x, y, x + len * fontAdvance(fontHeight(dc)), y + fontHeight(dc) };
        const uint32_t bp = crToPix(dc->bkColour);
        for (int yy = bg.top; yy < bg.bottom; ++yy) fillSpan(dc, bg.left, bg.right, yy, bp);
    }
    drawGlyphRun(dc, x, y, s, len, crToPix(dc->textColour), 0);
    return TRUE;
}

/* Word-wrap a string into line offsets. Returns the number of lines. */
static int wrapLines(const char* s, int len, int maxChars,
                     int* start, int* count, int maxLines) {
    int lines = 0, i = 0;
    if (maxChars < 1) maxChars = 1;
    while (i < len && lines < maxLines) {
        /* An explicit newline always breaks, ahead of any width rule. */
        int lineEnd = i, lastSpace = -1, taken = 0;
        while (lineEnd < len && s[lineEnd] != '\n' && taken < maxChars) {
            if (s[lineEnd] == ' ') lastSpace = lineEnd;
            ++lineEnd; ++taken;
        }
        int breakAt = lineEnd;
        if (lineEnd < len && s[lineEnd] != '\n' && lastSpace > i) breakAt = lastSpace;
        start[lines] = i;
        count[lines] = breakAt - i;
        ++lines;
        i = breakAt;
        while (i < len && (s[i] == ' ' )) ++i;
        if (i < len && s[i] == '\n') ++i;
    }
    return lines;
}

extern "C" int DrawTextA(HDC dc, LPCSTR s, int len, RECT* r, UINT flags) {
    if (!dc || !s || !r) return 0;
    if (len < 0) len = (int)strlen(s);

    const int h    = fontHeight(dc);
    const int adv  = fontAdvance(h);
    const int boxW = (int)(r->right - r->left);
    const int boxH = (int)(r->bottom - r->top);

    enum { MAX_LINES = 64 };
    int start[MAX_LINES], count[MAX_LINES];
    int lines;

    if (flags & DT_SINGLELINE) {
        start[0] = 0; count[0] = len; lines = 1;
    } else if (flags & DT_WORDBREAK) {
        lines = wrapLines(s, len, boxW / adv, start, count, MAX_LINES);
    } else {
        lines = 0;
        int i = 0;
        while (i <= len && lines < MAX_LINES) {
            int e = i;
            while (e < len && s[e] != '\n') ++e;
            start[lines] = i; count[lines] = e - i; ++lines;
            if (e >= len) break;
            i = e + 1;
        }
        if (!lines) { start[0] = 0; count[0] = 0; lines = 1; }
    }

    /* Ellipsis is a single-line affordance in this game -- the save screen
       footer. Trim to fit and spend the last three cells on dots. */
    char ellip[512];
    if ((flags & DT_END_ELLIPSIS) && lines == 1 && count[0] * adv > boxW) {
        int fits = boxW / adv;
        if (fits > (int)sizeof(ellip) - 1) fits = (int)sizeof(ellip) - 1;
        if (fits >= 4) {
            memcpy(ellip, s, (size_t)(fits - 3));
            ellip[fits - 3] = '.'; ellip[fits - 2] = '.'; ellip[fits - 1] = '.';
            ellip[fits] = 0;
            s = ellip; start[0] = 0; count[0] = fits;
        }
    }

    int widest = 0;
    for (int i = 0; i < lines; ++i) if (count[i] * adv > widest) widest = count[i] * adv;
    const int totalH = lines * h;

    if (flags & DT_CALCRECT) {
        r->right  = r->left + widest;
        r->bottom = r->top + totalH;
        return totalH;
    }

    int y = (int)r->top;
    if (flags & DT_VCENTER)      y = (int)r->top + (boxH - totalH) / 2;
    else if (flags & DT_BOTTOM)  y = (int)r->bottom - totalH;

    const uint32_t px = crToPix(dc->textColour);
    for (int i = 0; i < lines; ++i) {
        const int lineW = count[i] * adv;
        int x = (int)r->left;
        if (flags & DT_CENTER)     x = (int)r->left + (boxW - lineW) / 2;
        else if (flags & DT_RIGHT) x = (int)r->right - lineW;
        drawGlyphRun(dc, x, y + i * h, s + start[i], count[i], px, r);
    }
    return totalH;
}

/* ---------------------------------------------------------------------------
   Blits. Image data on both sides is already 0x00RRGGBB, so these copy
   straight through with no colour conversion -- unlike everything above.
   --------------------------------------------------------------------------- */
static void blitScaled(WDC* dst, int dx, int dy, int dw, int dh,
                       const uint32_t* src, int srcStride, int srcW, int srcH,
                       int sx, int sy, int sw, int sh,
                       bool keyed, uint32_t key) {
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    for (int y = 0; y < dh; ++y) {
        const int ty = dy + y;
        if (ty < 0 || ty >= dst->h) continue;
        int srcY = sy + (int)((long long)y * sh / dh);
        if (srcY < 0) srcY = 0;
        if (srcY >= srcH) srcY = srcH - 1;
        const uint32_t* srow = src + (size_t)srcY * srcStride;
        uint32_t* drow = dst->px + (size_t)ty * dst->w;
        for (int x = 0; x < dw; ++x) {
            const int tx = dx + x;
            if (tx < 0 || tx >= dst->w) continue;
            int srcX = sx + (int)((long long)x * sw / dw);
            if (srcX < 0) srcX = 0;
            if (srcX >= srcW) srcX = srcW - 1;
            const uint32_t c = srow[srcX];
            if (keyed && (c & 0x00FFFFFF) == (key & 0x00FFFFFF)) continue;
            drow[tx] = c;
        }
    }
}

extern "C" BOOL BitBlt(HDC dst, int x, int y, int w, int h,
                       HDC src, int sx, int sy, DWORD) {
    if (!dst || !src) return FALSE;
    blitScaled(dst, x, y, w, h, src->px, src->w, src->w, src->h, sx, sy, w, h, false, 0);
    return TRUE;
}

extern "C" BOOL StretchBlt(HDC dst, int x, int y, int w, int h,
                           HDC src, int sx, int sy, int sw, int sh, DWORD) {
    if (!dst || !src) return FALSE;
    blitScaled(dst, x, y, w, h, src->px, src->w, src->w, src->h, sx, sy, sw, sh, false, 0);
    return TRUE;
}

extern "C" BOOL TransparentBlt(HDC dst, int x, int y, int w, int h,
                               HDC src, int sx, int sy, int sw, int sh, UINT keyColour) {
    if (!dst || !src) return FALSE;
    /* The key arrives as a COLORREF -- it was handed to CreateSolidBrush when
       the icon was built -- so it converts, while the pixels do not. */
    blitScaled(dst, x, y, w, h, src->px, src->w, src->w, src->h, sx, sy, sw, sh,
               true, crToPix((COLORREF)keyColour));
    return TRUE;
}

extern "C" int StretchDIBits(HDC dst, int x, int y, int w, int h,
                             int sx, int sy, int sw, int sh, const void* bits,
                             const BITMAPINFO* bmi, UINT, DWORD) {
    if (!dst || !bits || !bmi) return 0;
    const int srcW = (int)bmi->bmiHeader.biWidth;
    const int rawH = (int)bmi->bmiHeader.biHeight;
    const int srcH = rawH < 0 ? -rawH : rawH;
    const uint32_t* src = (const uint32_t*)bits;

    if (rawH < 0) {
        /* Top-down rows, which is what the game always supplies. */
        blitScaled(dst, x, y, w, h, src, srcW, srcW, srcH, sx, sy, sw, sh, false, 0);
    } else {
        /* Bottom-up: walk the source backwards rather than copying it. */
        for (int row = 0; row < h; ++row) {
            const int ty = y + row;
            if (ty < 0 || ty >= dst->h) continue;
            int srcY = sy + (int)((long long)row * sh / h);
            srcY = iclamp(srcY, 0, srcH - 1);
            const uint32_t* srow = src + (size_t)(srcH - 1 - srcY) * srcW;
            uint32_t* drow = dst->px + (size_t)ty * dst->w;
            for (int col = 0; col < w; ++col) {
                const int tx = x + col;
                if (tx < 0 || tx >= dst->w) continue;
                int srcX = sx + (int)((long long)col * sw / w);
                drow[tx] = srow[iclamp(srcX, 0, srcW - 1)];
            }
        }
    }
    return h;
}
