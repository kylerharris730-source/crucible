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
static char g_hostCode[3][8192];
static char g_joinCode[8192];
static char g_fault[256];

void webNetBeginHost() {
    for (int i = 0; i < 3; ++i) g_hostCode[i][0] = 0;
    g_fault[0] = 0;
    EM_ASM({ if (window.CinderNet) window.CinderNet.beginHost(); });
}

void webNetBeginJoin(const char* code) {
    g_joinCode[0] = 0; g_fault[0] = 0;
    EM_ASM({
        if (window.CinderNet) window.CinderNet.beginJoin(UTF8ToString($0));
    }, code);
}

void webNetBeginAccept(int slot, const char* code) {
    g_fault[0] = 0;
    EM_ASM({
        if (window.CinderNet) window.CinderNet.beginAccept($0, UTF8ToString($1));
    }, slot, code);
}

const char* webNetHostCode(int slot) {
    if (slot < 0 || slot >= 3) return "";
    EM_ASM({
        var s = window.CinderNet ? window.CinderNet.hostCode($2) : '';
        stringToUTF8(s || '', $0, $1);
    }, g_hostCode[slot], (int)sizeof(g_hostCode[slot]), slot);
    return g_hostCode[slot];
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

bool webNetOpen(SOCKET socket) {
    return EM_ASM_INT({ return (window.CinderNet && window.CinderNet.isOpen($0 - 1)) ? 1 : 0; }, socket) != 0;
}

bool webNetFailed(SOCKET socket) {
    return EM_ASM_INT({
        return (window.CinderNet && window.CinderNet.state($0 - 1) === 'failed') ? 1 : 0;
    }, socket) != 0;
}

void webNetClose(SOCKET socket) {
    EM_ASM({ if (window.CinderNet) window.CinderNet.close($0 - 1); }, socket);
}

int webNetRecv(SOCKET socket, u8* buf, int cap) {
    /* The JS side owns the queue and copies into our heap. Passing the pointer
       through as an integer is how emscripten hands wasm memory to JS: the
       HEAPU8 view is the same bytes. */
    return EM_ASM_INT({
        if (!window.CinderNet || !window.CinderNet.pending($0 - 1)) return 0;
        var view = HEAPU8.subarray($1, $1 + $2);
        return window.CinderNet.read(view, $2, $0 - 1);
    }, socket, buf, cap);
}

int webNetSend(SOCKET socket, const u8* buf, int n) {
    return EM_ASM_INT({
        if (!window.CinderNet || !window.CinderNet.isOpen($0 - 1)) return 0;
        /* Copied out of the heap before handing it over: the data channel may
           queue this, and wasm memory can move under it if the heap grows. A
           subarray would then be a view onto the wrong bytes -- the kind of
           corruption that looks like a protocol bug for a week. */
        var bytes = HEAPU8.slice($1, $1 + $2);
        return window.CinderNet.send(bytes, $0 - 1) ? $2 : 0;
    }, socket, buf, n);
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

EMSCRIPTEN_KEEPALIVE int webMpPeerCount(void) {
    return netPeerCount();
}

}
