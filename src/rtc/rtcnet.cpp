#include "rtcnet.h"
#define RTC_STATIC
#include <rtc/rtc.h>
#include <mutex>
#include <deque>
#include <vector>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* ============================================================================
   rtcnet.cpp -- links, on libdatachannel's C API.

   This follows web/webrtc.js step for step, because the two have to agree on
   the wire: the same STUN server, one ordered and reliable data channel per
   link named 'cinderlift-<slot>', descriptions exchanged only once ICE has
   finished gathering (no trickle -- there is no channel to trickle over), and
   messages cut to 16 KB, which every browser will accept.

   --- threads -----------------------------------------------------------------
   libdatachannel calls back from its own threads. Everything a callback
   touches is behind g_lock. The one rule that keeps that from deadlocking:
   NEVER delete a peer connection while holding the lock. Deleting waits for
   callbacks in flight, and those callbacks may be waiting for the lock. So a
   close detaches the link's ids under the lock and deletes them after it.

   Callbacks do not trust the id they were handed to still mean the same
   link. Each one carries the (slot, generation) it was made for, packed into
   the user pointer as an integer rather than pointing at anything, so a late
   callback from a torn-down connection finds a generation that no longer
   matches and does nothing -- there is no object for it to reach through.
   ========================================================================== */

namespace {

struct Link {
    int pc, dc;
    RtcLinkState state;
    u32 generation;
    bool host;
    std::string code;              /* our finished description, packed       */
    std::deque<std::vector<u8> > inbox;
    size_t inboxHead;              /* bytes of inbox.front() already read    */
    std::string error;
};

std::mutex g_lock;
Link g_links[RTC_MAX_LINKS];
std::string g_fault;
bool g_initialised = false;
FILE* g_trace = 0;

/* Our own side of the story, into the same log as libdatachannel's. */
void trace(const char* what, int a, int b) {
    if (!g_trace) return;
    fprintf(g_trace, "cinderlift: %s %d %d\n", what, a, b);
    fflush(g_trace);
}

/* Cut to what every browser's SCTP stack takes in one message. webrtc.js
   sends in the same size, for the same reason. */
const int CHUNK = 16384;
/* How much may sit in libdatachannel's send queue before rtcNetSend reports
   "full for now". Without a ceiling a world snapshot would be queued whole,
   which is harmless, but a stalled link would then grow without bound. With
   it, the game's own send buffer holds the rest -- the same back-pressure a
   TCP socket gives it. */
const int SEND_CEILING = 1 << 20;

void* key(int slot, u32 generation) {
    return (void*)(intptr_t)(((intptr_t)generation << 2) | slot);
}
/* Locks, and returns the link only if it is still the one this callback was
   made for. */
Link* claim(void* ptr, std::unique_lock<std::mutex>& hold) {
    const intptr_t v = (intptr_t)ptr;
    const int slot = (int)(v & 3);
    const u32 generation = (u32)(v >> 2);
    hold = std::unique_lock<std::mutex>(g_lock);
    if (slot >= RTC_MAX_LINKS || g_links[slot].generation != generation) return 0;
    return &g_links[slot];
}

void initialise() {
    if (g_initialised) return;
    g_initialised = true;
    /* Silent unless asked. The game is a -mwindows program with nowhere to
       print, and plog's default is stdout. CINDERLIFT_RTC_LOG=1 in the
       environment writes libdatachannel's own account to build/rtc.log, or
       to the file it names if it is anything other than 1 -- the first thing
       to look at when two machines will not connect. */
    const char* want = getenv("CINDERLIFT_RTC_LOG");
    if (want && *want && *want != '0') {
        g_trace = fopen(strcmp(want, "1") == 0 ? "build/rtc.log" : want, "a");
        rtcInitLogger(RTC_LOG_DEBUG, [](rtcLogLevel, const char* message) {
            if (g_trace) { fprintf(g_trace, "%s\n", message); fflush(g_trace); }
        });
    } else {
        rtcInitLogger(RTC_LOG_NONE, 0);
    }
    rtcPreload();
}

/* --- callbacks ---------------------------------------------------------------- */

void RTC_API onOpen(int, void* ptr) {
    std::unique_lock<std::mutex> hold;
    if (Link* link = claim(ptr, hold)) link->state = RTC_LINK_OPEN;
}

void RTC_API onClosed(int, void* ptr) {
    std::unique_lock<std::mutex> hold;
    if (Link* link = claim(ptr, hold))
        if (link->state != RTC_LINK_FAILED) link->state = RTC_LINK_CLOSED;
}

void RTC_API onError(int, const char* error, void* ptr) {
    std::unique_lock<std::mutex> hold;
    if (Link* link = claim(ptr, hold)) {
        link->error = error && *error ? error : "data channel error";
        link->state = RTC_LINK_FAILED;
    }
}

void RTC_API onMessage(int, const char* message, int size, void* ptr) {
    /* Negative size is a text message. The game never sends one, and a peer
       that does is not speaking this protocol. */
    if (size < 0) return;
    std::unique_lock<std::mutex> hold;
    if (Link* link = claim(ptr, hold))
        link->inbox.push_back(std::vector<u8>((const u8*)message, (const u8*)message + size));
}

void wireChannel(int dc, void* ptr) {
    rtcSetUserPointer(dc, ptr);
    rtcSetOpenCallback(dc, onOpen);
    rtcSetClosedCallback(dc, onClosed);
    rtcSetErrorCallback(dc, onError);
    rtcSetMessageCallback(dc, onMessage);
}

void RTC_API onDataChannel(int, int dc, void* ptr) {
    {
        std::unique_lock<std::mutex> hold;
        Link* link = claim(ptr, hold);
        if (!link) { hold.unlock(); rtcDeleteDataChannel(dc); return; }
        link->dc = dc;
    }
    wireChannel(dc, ptr);
    /* The channel can already be open by the time it is announced, in which
       case its open callback has nothing left to report. */
    if (rtcIsOpen(dc)) onOpen(dc, ptr);
}

void RTC_API onState(int, rtcState state, void* ptr) {
    std::unique_lock<std::mutex> hold;
    Link* link = claim(ptr, hold);
    trace("pc state (slot, state)", (int)((intptr_t)ptr & 3), (int)state);
    if (!link) return;
    if (state == RTC_FAILED) {
        link->error = "Could not reach the other player directly";
        link->state = RTC_LINK_FAILED;
    } else if (state == RTC_DISCONNECTED || state == RTC_CLOSED) {
        if (link->state == RTC_LINK_OPEN) link->state = RTC_LINK_CLOSED;
    }
}

/* The description is only handed over once gathering is complete, so every
   candidate is inside it. That is what lets one code do the whole job. */
void RTC_API onGathering(int pc, rtcGatheringState state, void* ptr) {
    if (state != RTC_GATHERING_COMPLETE) return;
    char sdp[16384], type[16];
    if (rtcGetLocalDescription(pc, sdp, sizeof(sdp)) < 0 ||
        rtcGetLocalDescriptionType(pc, type, sizeof(type)) < 0) {
        std::unique_lock<std::mutex> hold;
        if (Link* link = claim(ptr, hold)) {
            link->error = "could not describe this connection";
            link->state = RTC_LINK_FAILED;
        }
        return;
    }
    const std::string code = rtcPackCode(type, sdp);
    std::unique_lock<std::mutex> hold;
    if (Link* link = claim(ptr, hold)) {
        link->code = code;
        if (link->state == RTC_LINK_GATHERING)
            link->state = link->host ? RTC_LINK_WAITING : RTC_LINK_CONNECTING;
    }
}

/* --- building and tearing down ------------------------------------------------ */

/* Detach under the lock; the caller deletes after releasing it. */
void detach(Link& link, int* pc, int* dc) {
    *pc = link.pc; *dc = link.dc;
    link.pc = link.dc = -1;
    link.generation++;
    link.state = RTC_LINK_IDLE;
    link.code.clear(); link.inbox.clear(); link.inboxHead = 0; link.error.clear();
}

void destroy(int pc, int dc) {
    trace("destroy (pc, dc)", pc, dc);
    if (dc >= 0) rtcDeleteDataChannel(dc);
    if (pc >= 0) { rtcClosePeerConnection(pc); rtcDeletePeerConnection(pc); }
}

/* Builds a fresh connection in `slot`. Host links make the data channel,
   which is what starts negotiation; a guest waits to be given one. */
bool build(int slot, bool host, void** outKey) {
    initialise();
    int oldPc, oldDc;
    void* ptr;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        detach(g_links[slot], &oldPc, &oldDc);
        g_links[slot].host = host;
        g_links[slot].state = RTC_LINK_GATHERING;
        ptr = key(slot, g_links[slot].generation);
    }
    destroy(oldPc, oldDc);

    const char* ice[] = { "stun:stun.l.google.com:19302" };
    rtcConfiguration config;
    memset(&config, 0, sizeof(config));
    config.iceServers = ice;
    config.iceServersCount = 1;
    const int pc = rtcCreatePeerConnection(&config);
    if (pc < 0) {
        std::lock_guard<std::mutex> hold(g_lock);
        g_links[slot].error = "could not start a connection";
        g_links[slot].state = RTC_LINK_FAILED;
        return false;
    }
    rtcSetUserPointer(pc, ptr);
    rtcSetStateChangeCallback(pc, onState);
    rtcSetGatheringStateChangeCallback(pc, onGathering);
    if (!host) rtcSetDataChannelCallback(pc, onDataChannel);
    bool superseded;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        /* Rebuilt again from another thread while this one was being made.
           The newer one wins; this one was never visible. Deleted after the
           lock is released, like every other deletion here. */
        superseded = key(slot, g_links[slot].generation) != ptr;
        if (!superseded) g_links[slot].pc = pc;
    }
    if (superseded) { rtcDeletePeerConnection(pc); return false; }
    if (host) {
        char label[32]; sprintf(label, "cinderlift-%d", slot);
        rtcDataChannelInit init;
        memset(&init, 0, sizeof(init));  /* reliable and ordered: TCP's contract */
        const int dc = rtcCreateDataChannelEx(pc, label, &init);
        if (dc < 0) {
            std::lock_guard<std::mutex> hold(g_lock);
            if (key(slot, g_links[slot].generation) == ptr) {
                g_links[slot].error = "could not open a data channel";
                g_links[slot].state = RTC_LINK_FAILED;
            }
            return false;
        }
        {
            std::lock_guard<std::mutex> hold(g_lock);
            superseded = key(slot, g_links[slot].generation) != ptr;
            if (!superseded) g_links[slot].dc = dc;
        }
        if (superseded) { rtcDeleteDataChannel(dc); return false; }
        wireChannel(dc, ptr);
    }
    if (outKey) *outKey = ptr;
    return true;
}

bool validSlot(int slot) { return slot >= 0 && slot < RTC_MAX_LINKS; }

void failSlot(int slot, const std::string& why) {
    std::lock_guard<std::mutex> hold(g_lock);
    g_links[slot].error = why;
    g_links[slot].state = RTC_LINK_FAILED;
}

} /* namespace */

/* ============================================================================ */

void rtcNetBeginHost() {
    rtcNetCloseAll();
    { std::lock_guard<std::mutex> hold(g_lock); g_fault.clear(); }
    for (int slot = 0; slot < RTC_MAX_LINKS; ++slot) build(slot, true, 0);
}

void rtcNetRehost(int slot) {
    if (validSlot(slot)) build(slot, true, 0);
}

void rtcNetBeginJoin(const char* code) {
    rtcNetCloseAll();
    { std::lock_guard<std::mutex> hold(g_lock); g_fault.clear(); }
    std::string type, sdp, error;
    if (!rtcUnpackCode(code, &type, &sdp, &error)) { rtcNetSetFault(error.c_str()); failSlot(0, error); return; }
    if (type != "offer") { rtcNetSetFault("that is not a host code"); failSlot(0, "that is not a host code"); return; }
    if (!build(0, false, 0)) return;
    int pc;
    { std::lock_guard<std::mutex> hold(g_lock); pc = g_links[0].pc; }
    if (rtcSetRemoteDescription(pc, sdp.c_str(), "offer") < 0) {
        rtcNetSetFault("the host's code could not be used");
        failSlot(0, "the host's code could not be used");
    }
}

void rtcNetBeginAccept(int slot, const char* code) {
    if (!validSlot(slot)) return;
    std::string type, sdp, error;
    if (!rtcUnpackCode(code, &type, &sdp, &error)) { failSlot(slot, error); return; }
    if (type != "answer") { failSlot(slot, "that is not a join code"); return; }
    int pc;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        trace("accept answer (slot, link state)", slot, (int)g_links[slot].state);
        pc = g_links[slot].pc;
        if (pc < 0) { g_links[slot].error = "that host slot is not ready"; g_links[slot].state = RTC_LINK_FAILED; return; }
    }
    if (rtcSetRemoteDescription(pc, sdp.c_str(), "answer") < 0) { failSlot(slot, "that reply could not be used"); return; }
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_links[slot].state == RTC_LINK_WAITING) g_links[slot].state = RTC_LINK_CONNECTING;
}

std::string rtcNetHostCode(int slot) {
    if (!validSlot(slot)) return std::string();
    std::lock_guard<std::mutex> hold(g_lock);
    return g_links[slot].host ? g_links[slot].code : std::string();
}

std::string rtcNetJoinCode() {
    std::lock_guard<std::mutex> hold(g_lock);
    return g_links[0].host ? std::string() : g_links[0].code;
}

std::string rtcNetFault() {
    std::lock_guard<std::mutex> hold(g_lock);
    if (!g_fault.empty()) return g_fault;
    for (int i = 0; i < RTC_MAX_LINKS; ++i) if (!g_links[i].error.empty()) return g_links[i].error;
    return std::string();
}

void rtcNetTrace(const char* what, int a, int b) { trace(what, a, b); }

void rtcNetSetFault(const char* text) {
    std::lock_guard<std::mutex> hold(g_lock);
    g_fault = text ? text : "";
}

RtcLinkState rtcNetState(int slot) {
    if (!validSlot(slot)) return RTC_LINK_IDLE;
    std::lock_guard<std::mutex> hold(g_lock);
    return g_links[slot].state;
}

u32 rtcNetGeneration(int slot) {
    if (!validSlot(slot)) return 0;
    std::lock_guard<std::mutex> hold(g_lock);
    return g_links[slot].generation;
}

bool rtcNetOpen(int link) {
    const int slot = link - 1;
    if (!validSlot(slot)) return false;
    std::lock_guard<std::mutex> hold(g_lock);
    return g_links[slot].state == RTC_LINK_OPEN;
}

bool rtcNetFailed(int link) {
    const int slot = link - 1;
    if (!validSlot(slot)) return true;
    std::lock_guard<std::mutex> hold(g_lock);
    return g_links[slot].state == RTC_LINK_FAILED;
}

void rtcNetClose(int link, u32 generation) {
    const int slot = link - 1;
    if (!validSlot(slot)) return;
    int pc, dc;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        if (g_links[slot].generation != generation) return;
        detach(g_links[slot], &pc, &dc);
    }
    destroy(pc, dc);
}

void rtcNetCloseAll() {
    for (int slot = 0; slot < RTC_MAX_LINKS; ++slot) {
        int pc, dc;
        {
            std::lock_guard<std::mutex> hold(g_lock);
            detach(g_links[slot], &pc, &dc);
            g_links[slot].host = false;
        }
        destroy(pc, dc);
    }
    std::lock_guard<std::mutex> hold(g_lock);
    g_fault.clear();
}

int rtcNetRecv(int link, u8* buf, int cap) {
    const int slot = link - 1;
    if (!validSlot(slot) || cap <= 0) return 0;
    std::lock_guard<std::mutex> hold(g_lock);
    Link& l = g_links[slot];
    int wrote = 0;
    while (!l.inbox.empty() && wrote < cap) {
        std::vector<u8>& head = l.inbox.front();
        const int left = (int)(head.size() - l.inboxHead);
        const int take = left < cap - wrote ? left : cap - wrote;
        memcpy(buf + wrote, &head[l.inboxHead], (size_t)take);
        wrote += take; l.inboxHead += (size_t)take;
        if (l.inboxHead >= head.size()) { l.inbox.pop_front(); l.inboxHead = 0; }
    }
    return wrote;
}

int rtcNetSend(int link, const u8* buf, int n) {
    const int slot = link - 1;
    if (!validSlot(slot) || n <= 0) return 0;
    int dc;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        if (g_links[slot].state != RTC_LINK_OPEN) return 0;
        dc = g_links[slot].dc;
    }
    if (dc < 0) return 0;
    /* Sent outside the lock: rtcSendMessage can call straight back into our
       callbacks on this thread if the send fails, and they take the lock. */
    int sent = 0;
    while (sent < n && rtcGetBufferedAmount(dc) < SEND_CEILING) {
        const int piece = n - sent < CHUNK ? n - sent : CHUNK;
        if (rtcSendMessage(dc, (const char*)buf + sent, piece) < 0) break;
        sent += piece;
    }
    return sent;
}
