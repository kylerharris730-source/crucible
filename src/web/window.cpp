/* ============================================================================
   window.cpp -- the platform half of the Win32 shim: window, message pump,
   input and time, backed by SDL2.

   main.cpp keeps its Win32 frame loop verbatim -- PeekMessage, DispatchMessage,
   QueryPerformanceCounter, Sleep -- and this file makes those mean something in
   a browser tab. SDL events are TRANSLATED INTO WM_ MESSAGES rather than
   handled here, so the game's existing wndProc stays the only place input is
   interpreted. That is what keeps a new key binding from needing a second
   implementation on this side.

   Two details in here are load-bearing:

   * ReleaseDC on the WINDOW dc is the present. main.cpp ends every frame with
     GetDC / StretchBlt / ReleaseDC, so the release is the natural moment to
     hand the pixels to SDL. Nothing had to be added to the game to get a
     frame on screen.

   * WM_KEYDOWN carries the auto-repeat flag in lParam bit 30, and main.cpp
     tests it to distinguish a fresh press from a held one -- F11 and Ctrl-Z
     both depend on it. A shim that always reported zero there would make
     every held key fire continuously.
   ========================================================================== */
#include "win32.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/* Provided by gdi.cpp -- the frame buffer the window owns. */
HDC wdcCreateOwned(int w, int h);

struct WWindow {
    WNDPROC proc;
    HDC     dc;
    int     w, h;
    DWORD   style;
};

static WWindow*     g_win        = 0;
static WNDPROC      g_classProc  = 0;
static SDL_Window*  g_sdlWindow  = 0;
static SDL_Renderer* g_renderer  = 0;
static SDL_Texture* g_texture    = 0;
static bool         g_focused    = true;
static bool         g_quitPosted = false;

/* --- message queue -------------------------------------------------------
   A plain ring. The game drains it fully every frame, so it only has to
   absorb one frame of input; 256 is far more than a frame can produce. */
enum { MSG_CAP = 256 };
static MSG g_queue[MSG_CAP];
static int g_qHead = 0, g_qTail = 0;

static void postMessage(UINT msg, WPARAM wp, LPARAM lp) {
    const int next = (g_qTail + 1) % MSG_CAP;
    if (next == g_qHead) return;          /* full: drop, never overwrite */
    g_queue[g_qTail].hwnd    = (HWND)g_win;
    g_queue[g_qTail].message = msg;
    g_queue[g_qTail].wParam  = wp;
    g_queue[g_qTail].lParam  = lp;
    g_qTail = next;
}

/* --- key state -----------------------------------------------------------
   Indexed by virtual key so GetAsyncKeyState is a lookup. Maintained from the
   same events that produce WM_KEYDOWN/WM_KEYUP, so the two views of the
   keyboard cannot disagree. */
static bool g_keyDown[256];

static int vkFromSdl(SDL_Keycode k) {
    if (k >= SDLK_a && k <= SDLK_z) return 'A' + (k - SDLK_a);   /* VKs are uppercase */
    if (k >= SDLK_0 && k <= SDLK_9) return '0' + (k - SDLK_0);
    switch (k) {
    case SDLK_LSHIFT: case SDLK_RSHIFT:   return VK_SHIFT;
    case SDLK_LCTRL:  case SDLK_RCTRL:    return VK_CONTROL;
    case SDLK_LALT:   case SDLK_RALT:     return VK_MENU;
    case SDLK_ESCAPE:                     return VK_ESCAPE;
    case SDLK_SPACE:                      return VK_SPACE;
    case SDLK_TAB:                        return VK_TAB;
    case SDLK_RETURN: case SDLK_KP_ENTER: return VK_RETURN;
    case SDLK_BACKSPACE:                  return VK_BACK;
    case SDLK_LEFT:                       return VK_LEFT;
    case SDLK_RIGHT:                      return VK_RIGHT;
    case SDLK_UP:                         return VK_UP;
    case SDLK_DOWN:                       return VK_DOWN;
    case SDLK_F5:                         return VK_F5;
    case SDLK_F9:                         return VK_F9;
    case SDLK_F11:                        return VK_F11;
    case SDLK_KP_PLUS:                    return VK_ADD;
    case SDLK_KP_MINUS:                   return VK_SUBTRACT;
    case SDLK_EQUALS:                     return VK_OEM_PLUS;
    case SDLK_MINUS:                      return VK_OEM_MINUS;
    case SDLK_COMMA:                      return VK_OEM_COMMA;
    case SDLK_PERIOD:                     return VK_OEM_PERIOD;
    case SDLK_SLASH:                      return VK_OEM_2;
    case SDLK_SEMICOLON:                  return VK_OEM_1;
    case SDLK_BACKQUOTE:                  return VK_OEM_3;
    case SDLK_LEFTBRACKET:                return VK_OEM_4;
    case SDLK_BACKSLASH:                  return VK_OEM_5;
    case SDLK_RIGHTBRACKET:               return VK_OEM_6;
    case SDLK_QUOTE:                      return VK_OEM_7;
    default:                              return 0;
    }
}

static void pumpSdl(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
#ifdef CINDERLIFT_WEB_INPUT_DEBUG
        printf("[sdl] type=%u key=%d\n", (unsigned)e.type,
               (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) ? (int)e.key.keysym.sym : 0);
#endif
        switch (e.type) {
        case SDL_QUIT:
            postMessage(WM_CLOSE, 0, 0);
            break;

        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) g_focused = true;
            else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                g_focused = false;
                /* Dropping focus with keys held would leave them held forever,
                   which walks the character while the player is elsewhere. */
                memset(g_keyDown, 0, sizeof(g_keyDown));
            }
            break;

        case SDL_MOUSEMOTION:
            postMessage(WM_MOUSEMOVE, 0, MAKELPARAM(e.motion.x, e.motion.y));
            break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            const bool down = (e.type == SDL_MOUSEBUTTONDOWN);
            const LPARAM lp = MAKELPARAM(e.button.x, e.button.y);
            if (e.button.button == SDL_BUTTON_LEFT) {
                g_keyDown[VK_LBUTTON] = down;
                postMessage(down ? WM_LBUTTONDOWN : WM_LBUTTONUP, 0, lp);
            } else if (e.button.button == SDL_BUTTON_RIGHT) {
                g_keyDown[VK_RBUTTON] = down;
                postMessage(down ? WM_RBUTTONDOWN : WM_RBUTTONUP, 0, lp);
            }
            break;
        }

        case SDL_MOUSEWHEEL: {
            /* The game reads the sign out of the high word, the way Windows
               packs it. */
            const int notches = e.wheel.y;
            if (notches) postMessage(WM_MOUSEWHEEL,
                                     (WPARAM)((notches * WHEEL_DELTA) << 16), 0);
            break;
        }

        case SDL_KEYDOWN: {
            const int vk = vkFromSdl(e.key.keysym.sym);
            if (!vk) break;
            /* Bit 30 means "was already down". main.cpp uses it to fire once
               per press for F11 and Ctrl-Z. */
            const LPARAM lp = e.key.repeat ? (LPARAM)(1L << 30) : 0;
            g_keyDown[vk] = true;
            postMessage(WM_KEYDOWN, (WPARAM)vk, lp);
            /* Backspace is a text-editing character on Win32: TranslateMessage
               turns its keydown into WM_CHAR('\b'). SDL_TEXTINPUT deliberately
               reports inserted text only, so browsers never send Backspace
               through that event and our no-op TranslateMessage cannot create
               it later. Synthesize precisely the missing character here;
               repeats remain repeats because SDL repeats the keydown. */
            if (vk == VK_BACK) postMessage(WM_CHAR, (WPARAM)'\b', lp);
            break;
        }

        case SDL_KEYUP: {
            const int vk = vkFromSdl(e.key.keysym.sym);
            if (!vk) break;
            g_keyDown[vk] = false;
            postMessage(WM_KEYUP, (WPARAM)vk, 0);
            break;
        }

        case SDL_TEXTINPUT:
            /* Search and join-IP fields both consume ASCII WM_CHAR messages.
               Editing keys are not text input; Backspace is bridged above. */
            for (const char* p = e.text.text; *p; ++p)
                if ((unsigned char)*p < 128) postMessage(WM_CHAR, (WPARAM)(unsigned char)*p, 0);
            break;

        default:
            break;
        }
    }
}

/* --- window class and creation ------------------------------------------- */
extern "C" short RegisterClassA(const WNDCLASSA* wc) {
    if (wc) g_classProc = wc->lpfnWndProc;
    return 1;
}

extern "C" BOOL AdjustWindowRect(RECT*, DWORD, BOOL) {
    /* On Windows this grows the rect by the frame so the CLIENT area ends up
       the requested size. A canvas has no frame, so the rect is already the
       client area and must be left exactly as it is -- padding it here would
       make the game's backbuffer disagree with the canvas by a title bar. */
    return TRUE;
}

extern "C" HWND CreateWindowA(LPCSTR, LPCSTR title, DWORD style, int, int,
                              int w, int h, HWND, HMENU, HINSTANCE, void*) {
    if (w < 1 || h < 1) return 0;

    /* Without this the game has no keyboard at all, and the failure is
       completely silent.

       SDL names the DOM element it listens on, and its default is the string
       "#window". Emscripten used to special-case "#window" and "#document";
       it no longer does, and now resolves both as ordinary CSS selectors --
       which match nothing, because no element has id="window". The lookup
       returns null, SDL registers no key handlers, and nothing anywhere
       reports a problem. Measured in the browser: SDL delivered mouse motion
       and button events happily while not one SDL_KEYDOWN ever arrived, and
       the registered-handler list contained mouse, touch and focus entries
       with no keydown among them.

       "body" is a selector that does resolve, and it is the right element
       rather than merely a working one: key events bubble to body no matter
       which child holds focus, so the game keeps its keyboard whether or not
       the player has clicked the canvas.

       Must be set BEFORE SDL_Init -- that is when the listeners are installed. */
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "body");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 0;

    g_sdlWindow = SDL_CreateWindow(title ? title : "Cinderlift",
                                   SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   w, h, SDL_WINDOW_SHOWN);
    if (!g_sdlWindow) return 0;

    g_renderer = SDL_CreateRenderer(g_sdlWindow, -1, 0);
    if (!g_renderer) return 0;

    /* ARGB8888 matches the 0x00RRGGBB words the game already produces, so the
       frame uploads without a per-pixel conversion. */
    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!g_texture) return 0;

    g_win = new WWindow;
    g_win->proc  = g_classProc;
    g_win->dc    = wdcCreateOwned(w, h);
    g_win->w     = w;
    g_win->h     = h;
    g_win->style = style;
    SDL_StartTextInput();
    return (HWND)g_win;
}

extern "C" BOOL ShowWindow(HWND, int) { return TRUE; }

extern "C" BOOL GetClientRect(HWND w, RECT* r) {
    if (!r) return FALSE;
    r->left = r->top = 0;
    r->right  = w ? w->w : 0;
    r->bottom = w ? w->h : 0;
    return TRUE;
}

/* --- device contexts on the window --------------------------------------
   GetDC hands back the window's own buffer; ReleaseDC is where a finished
   frame reaches the screen. */
extern "C" HDC GetDC(HWND w) {
    if (!w) {
        /* GetDC(NULL) is only ever used as a template for CreateCompatibleDC
           and CreateCompatibleBitmap, neither of which reads its pixels. */
        static HDC screen = 0;
        if (!screen) screen = wdcCreateOwned(1, 1);
        return screen;
    }
    return w->dc;
}

extern "C" int ReleaseDC(HWND w, HDC dc) {
    if (!w || !dc || dc != w->dc) return 1;
    SDL_UpdateTexture(g_texture, 0, wdcPixels(dc), wdcWidth(dc) * (int)sizeof(uint32_t));
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, 0, 0);
    SDL_RenderPresent(g_renderer);
    return 1;
}

/* --- the pump ------------------------------------------------------------ */
extern "C" BOOL PeekMessage(MSG* m, HWND, UINT, UINT, UINT remove) {
    pumpSdl();
    if (g_qHead == g_qTail) {
        if (g_quitPosted) {
            g_quitPosted = false;
            if (m) { m->hwnd = 0; m->message = WM_QUIT; m->wParam = 0; m->lParam = 0; }
            return TRUE;
        }
        return FALSE;
    }
    if (m) *m = g_queue[g_qHead];
    if (remove & PM_REMOVE) g_qHead = (g_qHead + 1) % MSG_CAP;
    return TRUE;
}

extern "C" BOOL TranslateMessage(const MSG*) {
    /* SDL_TEXTINPUT already produced the WM_CHARs, so there is nothing to
       synthesise here. */
    return TRUE;
}

extern "C" LRESULT DispatchMessage(const MSG* m) {
    if (!m || !g_win || !g_win->proc) return 0;
    return g_win->proc(m->hwnd, m->message, m->wParam, m->lParam);
}

extern "C" LRESULT DefWindowProc(HWND, UINT, WPARAM, LPARAM) { return 0; }

extern "C" void PostQuitMessage(int) { g_quitPosted = true; }

/* --- odds and ends the game touches -------------------------------------- */
extern "C" HWND GetForegroundWindow(void) { return g_focused ? (HWND)g_win : 0; }

extern "C" HWND SetCapture(HWND w)  { return w; }
extern "C" BOOL ReleaseCapture(void) { return TRUE; }

static struct WCursor* g_arrow = (struct WCursor*)1;
extern "C" HCURSOR LoadCursor(HINSTANCE, LPCSTR)  { return (HCURSOR)g_arrow; }
extern "C" HCURSOR SetCursor(HCURSOR c)           { return c; }

extern "C" intptr_t GetWindowLongPtr(HWND w, int index) {
    if (w && index == GWL_STYLE) return (intptr_t)w->style;
    return 0;
}
extern "C" intptr_t SetWindowLongPtr(HWND w, int index, intptr_t value) {
    if (w && index == GWL_STYLE) { const intptr_t old = (intptr_t)w->style;
                                   w->style = (DWORD)value; return old; }
    return 0;
}

/* Fullscreen is the browser's business, not the game's: the canvas is scaled
   to the page by CSS, so honouring F11 in here would fight whatever the tab is
   already doing. These keep the client size fixed and succeed quietly. */
extern "C" BOOL GetWindowPlacement(HWND, WINDOWPLACEMENT* p) {
    if (p) memset(&p->rcNormalPosition, 0, sizeof(p->rcNormalPosition));
    return TRUE;
}
extern "C" BOOL SetWindowPlacement(HWND, const WINDOWPLACEMENT*) { return TRUE; }
extern "C" BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT) { return TRUE; }

extern "C" HMONITOR MonitorFromWindow(HWND, DWORD) { return (HMONITOR)1; }

extern "C" BOOL GetMonitorInfo(HMONITOR, MONITORINFO* mi) {
    if (!mi) return FALSE;
    mi->rcMonitor.left = mi->rcMonitor.top = 0;
    mi->rcMonitor.right  = g_win ? g_win->w : 0;
    mi->rcMonitor.bottom = g_win ? g_win->h : 0;
    mi->rcWork = mi->rcMonitor;
    return TRUE;
}

/* --- input queries ------------------------------------------------------- */
extern "C" short GetAsyncKeyState(int vk) {
    if (vk < 0 || vk > 255) return 0;
    return g_keyDown[vk] ? (short)0x8000 : (short)0;
}
extern "C" short GetKeyState(int vk) { return GetAsyncKeyState(vk); }

/* --- time ----------------------------------------------------------------
   A microsecond clock, so the frequency the game divides by is exactly a
   million and its pacing arithmetic is unchanged. */
extern "C" BOOL QueryPerformanceFrequency(LARGE_INTEGER* v) {
    if (v) v->QuadPart = 1000000LL;
    return TRUE;
}

extern "C" BOOL QueryPerformanceCounter(LARGE_INTEGER* v) {
    if (!v) return FALSE;
#ifdef __EMSCRIPTEN__
    v->QuadPart = (long long)(emscripten_get_now() * 1000.0);
#else
    v->QuadPart = (long long)SDL_GetTicks() * 1000LL;
#endif
    return TRUE;
}

extern "C" void Sleep(DWORD ms) {
#ifdef __EMSCRIPTEN__
    /* This is the yield that lets the page breathe. The game's frame loop
       blocks here between frames, and ASYNCIFY turns that block into a return
       to the browser event loop -- which is the only reason a Win32-shaped
       while(running) loop can run in a tab at all. */
    emscripten_sleep(ms);
#else
    SDL_Delay(ms);
#endif
}

extern "C" DWORD timeBeginPeriod(UINT) { return 0; }
extern "C" DWORD timeEndPeriod(UINT)   { return 0; }

/* --- entry point ---------------------------------------------------------
   main.cpp still defines WinMain; this is the trampoline that reaches it. The
   web build takes no arguments -- there is no --host or --join to pass, since
   the browser cannot open a raw socket -- so the command line is empty and the
   game takes its ordinary single-player startup path. */
int main(int, char**) {
    static char emptyCommandLine[] = "";
    return WinMain((HINSTANCE)1, (HINSTANCE)0, emptyCommandLine, 1);
}
