/* ============================================================================
   win32.h -- the slice of Win32 that Crucible actually uses.

   This is not a Windows emulator and must never grow into one. It is the
   EXACT surface main.cpp calls -- 56 functions, 17 window messages, 16 virtual
   keys -- reimplemented against a plain pixel buffer so that the same
   main.cpp compiles for the browser.

   The whole point is that main.cpp is NOT forked. Its UI code, its wndProc,
   its input handling and its frame loop are the Windows ones, unmodified,
   down to the argument. A panel changed next month reaches the web build
   because the web build consumes the same FillRect call -- there is no second
   copy of the UI to keep in step, which is the only reason a port like this
   stays maintainable instead of rotting on a branch.

   Scope rule: nothing goes in here speculatively. If main.cpp does not call
   it, it does not exist. That is what keeps this file small enough to trust.
   ========================================================================== */
#pragma once
#ifdef _WIN32
#error "win32.h is the NON-Windows shim; Windows builds include <windows.h>"
#endif

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* Microsoft spells it with a leading underscore and everyone else does not.
   The two differ in one respect -- _snprintf leaves the buffer unterminated
   when the text does not fit, while snprintf always terminates -- so this
   alias is, if anything, the safer of the two. */
#define _snprintf snprintf

/* --- calling conventions, which mean nothing off Windows ------------------ */
#define WINAPI
#define CALLBACK
#define APIENTRY

#ifndef NULL
#define NULL 0
#endif
#define TRUE  1
#define FALSE 0

/* --- scalar types -------------------------------------------------------- */
typedef int             BOOL;
typedef unsigned char   BYTE;
typedef unsigned short  WORD;
typedef unsigned int    DWORD;
typedef unsigned int    UINT;
typedef long            LONG;
typedef char*           LPSTR;
typedef const char*     LPCSTR;
typedef uintptr_t       WPARAM;
typedef intptr_t        LPARAM;
typedef intptr_t        LRESULT;
typedef uintptr_t       UINT_PTR;
typedef DWORD           COLORREF;

/* --- handles -------------------------------------------------------------
   Real pointers to distinct structs, so a mixed-up handle is a compile error
   rather than a mystery at runtime. HGDIOBJ stays void* because SelectObject
   genuinely is polymorphic over pen/brush/font/bitmap. */
typedef struct WWindow*  HWND;
typedef struct WDC*      HDC;
typedef struct WBrush*   HBRUSH;
typedef struct WPen*     HPEN;
typedef struct WFont*    HFONT;
typedef struct WBitmap*  HBITMAP;
typedef struct WCursor*  HCURSOR;
typedef struct WMonitor* HMONITOR;
typedef struct WInst*    HINSTANCE;
typedef struct WMenu*    HMENU;
typedef struct WIcon*    HICON;
typedef void*            HGDIOBJ;

/* --- COLORREF is 0x00BBGGRR, NOT the 0x00RRGGBB the pixel buffers use -----
   Both orders appear in this program, and confusing them silently swaps red
   and blue -- which reads as "the UI looks wrong somehow" rather than as a
   bug with a location. The conversion lives in gdi.cpp; the ordering is
   recorded here because RGB() is where it originates. */
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r)) | (((BYTE)(g)) << 8) | (((BYTE)(b)) << 16)))
#define GetRValue(c) ((BYTE)((c) & 0xFF))
#define GetGValue(c) ((BYTE)(((c) >> 8) & 0xFF))
#define GetBValue(c) ((BYTE)(((c) >> 16) & 0xFF))

#define LOWORD(l) ((WORD)(((uintptr_t)(l)) & 0xFFFF))
#define HIWORD(l) ((WORD)((((uintptr_t)(l)) >> 16) & 0xFFFF))
#define MAKELPARAM(lo, hi) ((LPARAM)(((WORD)(lo)) | (((DWORD)((WORD)(hi))) << 16)))

/* --- geometry ------------------------------------------------------------ */
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;

/* main.cpp reads .QuadPart and nothing else, so this is a struct rather than
   the real union -- there is no Low/HighPart code that needs to keep working. */
typedef struct { long long QuadPart; } LARGE_INTEGER;

/* --- messages ------------------------------------------------------------ */
typedef struct {
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
} MSG;

typedef LRESULT (CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct {
    UINT      style;
    WNDPROC   lpfnWndProc;
    int       cbClsExtra;
    int       cbWndExtra;
    HINSTANCE hInstance;
    HICON     hIcon;
    HCURSOR   hCursor;
    HBRUSH    hbrBackground;
    LPCSTR    lpszMenuName;
    LPCSTR    lpszClassName;
} WNDCLASSA;

typedef struct {
    UINT  length;
    UINT  flags;
    UINT  showCmd;
    POINT ptMinPosition;
    POINT ptMaxPosition;
    RECT  rcNormalPosition;
} WINDOWPLACEMENT;

typedef struct {
    DWORD cbSize;
    RECT  rcMonitor;
    RECT  rcWork;
    DWORD dwFlags;
} MONITORINFO;

/* --- DIB description -----------------------------------------------------
   bmiHeader MUST be the first member: main.cpp builds a bare
   BITMAPINFOHEADER on the stack and casts its address to BITMAPINFO*. */
typedef struct {
    DWORD biSize;
    LONG  biWidth;
    LONG  biHeight;        /* negative means top-down rows */
    WORD  biPlanes;
    WORD  biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG  biXPelsPerMeter;
    LONG  biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER;

typedef struct { BITMAPINFOHEADER bmiHeader; DWORD bmiColors[1]; } BITMAPINFO;

#define BI_RGB         0
#define DIB_RGB_COLORS 0
#define SRCCOPY        0x00CC0020
#define COLORONCOLOR   3
#define HALFTONE       4

/* --- window messages (only the 17 wndProc actually handles) --------------- */
#define WM_DESTROY        0x0002
#define WM_SIZE           0x0005
#define WM_CLOSE          0x0010
#define WM_QUIT           0x0012
#define WM_ERASEBKGND     0x0014
#define WM_ACTIVATEAPP    0x001C
#define WM_SETCURSOR      0x0020
#define WM_KEYDOWN        0x0100
#define WM_KEYUP          0x0101
#define WM_CHAR           0x0102
#define WM_MOUSEMOVE      0x0200
#define WM_LBUTTONDOWN    0x0201
#define WM_LBUTTONUP      0x0202
#define WM_RBUTTONDOWN    0x0204
#define WM_RBUTTONUP      0x0205
#define WM_MOUSEWHEEL     0x020A
#define WM_CAPTURECHANGED 0x0215

#define PM_NOREMOVE 0
#define PM_REMOVE   1

/* --- virtual keys (the 16 the game binds; ASCII maps to itself) ----------- */
#define VK_LBUTTON  0x01
#define VK_RBUTTON  0x02
#define VK_BACK     0x08
#define VK_TAB      0x09
#define VK_RETURN   0x0D
#define VK_SHIFT    0x10
#define VK_CONTROL  0x11
#define VK_MENU     0x12
#define VK_ESCAPE   0x1B
#define VK_SPACE    0x20
#define VK_LEFT     0x25
#define VK_UP       0x26
#define VK_RIGHT    0x27
#define VK_DOWN     0x28
#define VK_ADD      0x6B
#define VK_SUBTRACT 0x6D
#define VK_F5       0x74
#define VK_F9       0x78
#define VK_F11      0x7A
#define VK_OEM_1      0xBA
#define VK_OEM_PLUS   0xBB
#define VK_OEM_COMMA  0xBC
#define VK_OEM_MINUS  0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2      0xBF
#define VK_OEM_3      0xC0
#define VK_OEM_4      0xDB
#define VK_OEM_5      0xDC
#define VK_OEM_6      0xDD
#define VK_OEM_7      0xDE

#define WHEEL_DELTA 120

/* --- window styles -------------------------------------------------------
   The real Win32 values. Nothing outside this shim inspects them, but keeping
   them honest means a bit test copied out of Microsoft docs into main.cpp
   behaves here the way it does there. */
#define WS_OVERLAPPED       0x00000000
#define WS_CAPTION          0x00C00000
#define WS_SYSMENU          0x00080000
#define WS_THICKFRAME       0x00040000
#define WS_MINIMIZEBOX      0x00020000
#define WS_MAXIMIZEBOX      0x00010000
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | \
                             WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_POPUP            0x80000000
#define WS_VISIBLE          0x10000000

#define CW_USEDEFAULT ((int)0x80000000)
#define SW_HIDE       0
#define SW_SHOWNORMAL 1
#define SW_SHOW       5

#define GWL_STYLE   (-16)
#define GWL_EXSTYLE (-20)

#define SWP_NOSIZE        0x0001
#define SWP_NOMOVE        0x0002
#define SWP_NOZORDER      0x0004
#define SWP_FRAMECHANGED  0x0020
#define SWP_NOOWNERZORDER 0x0200
#define HWND_TOP          ((HWND)0)

#define MONITOR_DEFAULTTONEAREST 2
#define HTCLIENT     1
#define IDC_ARROW    ((LPCSTR)32512)
#define COLOR_WINDOW 5

/* --- GDI object flavours ------------------------------------------------- */
#define PS_SOLID 0
#define PS_DOT   2

#define TRANSPARENT 1
#define OPAQUE      2

#define WHITE_BRUSH  0
#define BLACK_BRUSH  4
#define NULL_BRUSH   5
#define HOLLOW_BRUSH 5

/* --- DrawText flags ------------------------------------------------------ */
#define DT_TOP          0x0000
#define DT_LEFT         0x0000
#define DT_CENTER       0x0001
#define DT_RIGHT        0x0002
#define DT_VCENTER      0x0004
#define DT_BOTTOM       0x0008
#define DT_WORDBREAK    0x0010
#define DT_SINGLELINE   0x0020
#define DT_CALCRECT     0x0400
#define DT_END_ELLIPSIS 0x8000

#define FW_NORMAL           400
#define FW_BOLD             700
#define DEFAULT_CHARSET       1
#define OUT_DEFAULT_PRECIS    0
#define CLIP_DEFAULT_PRECIS   0
#define DEFAULT_QUALITY       0
#define CLEARTYPE_QUALITY     5
#define FF_DONTCARE           0

#ifdef __cplusplus
extern "C" {
#endif

/* --- rect helpers, which are pure arithmetic ----------------------------- */
BOOL SetRect(RECT* r, int left, int top, int right, int bottom);
BOOL SetRectEmpty(RECT* r);
BOOL IsRectEmpty(const RECT* r);
BOOL InflateRect(RECT* r, int dx, int dy);
BOOL PtInRect(const RECT* r, POINT pt);

/* --- GDI object lifetime ------------------------------------------------- */
HBRUSH  CreateSolidBrush(COLORREF c);
HPEN    CreatePen(int style, int width, COLORREF c);
HFONT   CreateFontA(int height, int width, int esc, int orient, int weight,
                    DWORD italic, DWORD underline, DWORD strikeout,
                    DWORD charset, DWORD outPrec, DWORD clipPrec,
                    DWORD quality, DWORD pitch, LPCSTR face);
HGDIOBJ GetStockObject(int which);
BOOL    DeleteObject(HGDIOBJ o);
HGDIOBJ SelectObject(HDC dc, HGDIOBJ o);

/* --- device contexts ----------------------------------------------------- */
HDC     GetDC(HWND w);
int     ReleaseDC(HWND w, HDC dc);
HDC     CreateCompatibleDC(HDC dc);
BOOL    DeleteDC(HDC dc);
HBITMAP CreateCompatibleBitmap(HDC dc, int w, int h);
int     SetStretchBltMode(HDC dc, int mode);

/* --- drawing ------------------------------------------------------------- */
int      FillRect(HDC dc, const RECT* r, HBRUSH b);
int      FrameRect(HDC dc, const RECT* r, HBRUSH b);
BOOL     MoveToEx(HDC dc, int x, int y, POINT* old);
BOOL     LineTo(HDC dc, int x, int y);
BOOL     Polyline(HDC dc, const POINT* pts, int n);
BOOL     Ellipse(HDC dc, int l, int t, int r, int b);
COLORREF SetTextColor(HDC dc, COLORREF c);
int      SetBkMode(HDC dc, int mode);
BOOL     TextOutA(HDC dc, int x, int y, LPCSTR s, int len);
int      DrawTextA(HDC dc, LPCSTR s, int len, RECT* r, UINT flags);

/* --- blits --------------------------------------------------------------- */
BOOL BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, DWORD rop);
BOOL StretchBlt(HDC dst, int x, int y, int w, int h,
                HDC src, int sx, int sy, int sw, int sh, DWORD rop);
BOOL TransparentBlt(HDC dst, int x, int y, int w, int h,
                    HDC src, int sx, int sy, int sw, int sh, UINT keyColour);
int  StretchDIBits(HDC dst, int x, int y, int w, int h,
                   int sx, int sy, int sw, int sh, const void* bits,
                   const BITMAPINFO* bmi, UINT usage, DWORD rop);

/* --- window / message pump ----------------------------------------------- */
short    RegisterClassA(const WNDCLASSA* wc);
HWND     CreateWindowA(LPCSTR cls, LPCSTR title, DWORD style, int x, int y,
                       int w, int h, HWND parent, HMENU menu, HINSTANCE inst,
                       void* param);
BOOL     ShowWindow(HWND w, int cmd);
BOOL     GetClientRect(HWND w, RECT* r);
BOOL     AdjustWindowRect(RECT* r, DWORD style, BOOL menu);
BOOL     PeekMessage(MSG* m, HWND w, UINT first, UINT last, UINT remove);
BOOL     TranslateMessage(const MSG* m);
LRESULT  DispatchMessage(const MSG* m);
LRESULT  DefWindowProc(HWND w, UINT msg, WPARAM wp, LPARAM lp);
void     PostQuitMessage(int code);
HWND     GetForegroundWindow(void);
HWND     SetCapture(HWND w);
BOOL     ReleaseCapture(void);
HCURSOR  LoadCursor(HINSTANCE inst, LPCSTR name);
HCURSOR  SetCursor(HCURSOR c);
intptr_t GetWindowLongPtr(HWND w, int index);
intptr_t SetWindowLongPtr(HWND w, int index, intptr_t value);
BOOL     GetWindowPlacement(HWND w, WINDOWPLACEMENT* p);
BOOL     SetWindowPlacement(HWND w, const WINDOWPLACEMENT* p);
BOOL     SetWindowPos(HWND w, HWND after, int x, int y, int cx, int cy, UINT flags);
HMONITOR MonitorFromWindow(HWND w, DWORD flags);
BOOL     GetMonitorInfo(HMONITOR m, MONITORINFO* mi);

/* --- input --------------------------------------------------------------- */
short GetAsyncKeyState(int vk);
short GetKeyState(int vk);

/* --- time ---------------------------------------------------------------- */
BOOL  QueryPerformanceCounter(LARGE_INTEGER* v);
BOOL  QueryPerformanceFrequency(LARGE_INTEGER* v);
void  Sleep(DWORD ms);
DWORD timeBeginPeriod(UINT ms);
DWORD timeEndPeriod(UINT ms);

#ifdef __cplusplus
}
#endif

/* main.cpp's entry point is WinMain; the shim provides main() and calls it. */
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int);

/* --- what the platform layer needs from the GDI layer --------------------
   The window's DC owns the pixels that reach the canvas. Presenting a frame
   means handing that buffer to SDL, so window.cpp has to be able to ask for
   it; nothing in main.cpp ever sees this. */
#ifdef __cplusplus
extern "C" {
#endif
uint32_t* wdcPixels(HDC dc);
int       wdcWidth(HDC dc);
int       wdcHeight(HDC dc);
#ifdef __cplusplus
}
#endif
