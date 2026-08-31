#include "netshim.h"
#include <emscripten.h>
#include <string.h>

/* Everything here is a thin call into web/webrtc.js. The interesting parts --
   why WebRTC rather than a WebSocket, why the codes are pasted by hand, why
   there is no TURN -- are documented there, at the code that implements them.

   Buffers are static and generous. An SDP gzipped and base64'd came to about
   800 characters in testing; 8 KB is room for a description with far more
   candidates in it than a home connection produces, and a truncated code would
   fail in a way nobody could diagnose from the message. */
static char g_hostCode[8192];
static char g_joinCode[8192];
static char g_fault[256];

void webNetBeginHost() {
    g_hostCode[0] = 0; g_fault[0] = 0;
    EM_ASM({ if (window.CinderNet) window.CinderNet.beginHost(); });
}

void webNetBeginJoin(const char* code) {
    g_joinCode[0] = 0; g_fault[0] = 0;
    EM_ASM({
        if (window.CinderNet) window.CinderNet.beginJoin(UTF8ToString($0));
    }, code);
}

void webNetBeginAccept(const char* code) {
    g_fault[0] = 0;
    EM_ASM({
        if (window.CinderNet) window.CinderNet.beginAccept(UTF8ToString($0));
    }, code);
}

const char* webNetHostCode() {
    EM_ASM({
        var s = window.CinderNet ? window.CinderNet.hostCode() : '';
        stringToUTF8(s || '', $0, $1);
    }, g_hostCode, (int)sizeof(g_hostCode));
    return g_hostCode;
}

const char* webNetJoinCode() {
    EM_ASM({
        var s = window.CinderNet ? window.CinderNet.joinCode() : '';
        stringToUTF8(s || '', $0, $1);
    }, g_joinCode, (int)sizeof(g_joinCode));
    return g_joinCode;
}

const char* webNetFault() {
    EM_ASM({
        var s = window.CinderNet ? window.CinderNet.fault() : '';
        stringToUTF8(s || '', $0, $1);
    }, g_fault, (int)sizeof(g_fault));
    return g_fault;
}

bool webNetOpen() {
    return EM_ASM_INT({ return (window.CinderNet && window.CinderNet.isOpen()) ? 1 : 0; }) != 0;
}

bool webNetFailed() {
    return EM_ASM_INT({
        return (window.CinderNet && window.CinderNet.state() === 'failed') ? 1 : 0;
    }) != 0;
}

void webNetClose() {
    EM_ASM({ if (window.CinderNet) window.CinderNet.close(); });
}

int webNetRecv(u8* buf, int cap) {
    /* The JS side owns the queue and copies into our heap. Passing the pointer
       through as an integer is how emscripten hands wasm memory to JS: the
       HEAPU8 view is the same bytes. */
    return EM_ASM_INT({
        if (!window.CinderNet || !window.CinderNet.pending()) return 0;
        var view = HEAPU8.subarray($0, $0 + $1);
        return window.CinderNet.read(view, $1);
    }, buf, cap);
}

int webNetSend(const u8* buf, int n) {
    return EM_ASM_INT({
        if (!window.CinderNet || !window.CinderNet.isOpen()) return 0;
        /* Copied out of the heap before handing it over: the data channel may
           queue this, and wasm memory can move under it if the heap grows. A
           subarray would then be a view onto the wrong bytes -- the kind of
           corruption that looks like a protocol bug for a week. */
        var bytes = HEAPU8.slice($0, $0 + $1);
        return window.CinderNet.send(bytes) ? $1 : 0;
    }, buf, n);
}

/* --- what the page calls ----------------------------------------------------
   The signalling UI is HTML rather than part of the game's own menu, and that
   is not laziness. A player has to COPY a code out and PASTE one back in, and
   a canvas has no clipboard, no text selection and no context menu. Building
   that inside the game would mean reimplementing a textarea in a pixel grid.

   So the page owns the panel and calls in here to start things, which also
   keeps every browser-only wart out of main.cpp. */
#include "../network.h"

extern "C" {

EMSCRIPTEN_KEEPALIVE void webMpHost(void) {
    netHost(0, false);
}

/* The code stands where an IPv4 address would on Windows -- see netshim.h. */
EMSCRIPTEN_KEEPALIVE void webMpJoin(const char* code) {
    netJoin(code, 0);
}

EMSCRIPTEN_KEEPALIVE void webMpStop(void) {
    netStop();
}

/* So the panel can show the same words the game's menu would. */
EMSCRIPTEN_KEEPALIVE const char* webMpStatus(void) {
    return netStatus();
}

EMSCRIPTEN_KEEPALIVE int webMpRole(void) {
    return (int)netRole();
}

EMSCRIPTEN_KEEPALIVE int webMpReady(void) {
    return netReady() ? 1 : 0;
}

}
