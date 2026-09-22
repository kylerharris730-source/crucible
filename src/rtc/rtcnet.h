#pragma once
#include "../common.h"
#include <string>

/* ============================================================================
   rtcnet.h -- WebRTC data channels for the Windows build.

   The browser build has always connected over WebRTC, because a tab cannot
   open a socket. The Windows build connected by typing an IPv4 address, which
   is perfect on a LAN and useless across the internet unless the host
   forwards a port -- a thing most players cannot do and should not have to.

   This is the browser's transport, done natively with libdatachannel. It
   speaks exactly the same connection codes as web/webrtc.js ('CLZ'/'CLR' plus
   a JSON-wrapped SDP), so a desktop and a browser can hand each other codes
   and connect; and it signals through the same room broker as
   web/roomcode.js, so both builds use the same five-character codes.

   Shape of the API is web/netshim.h's on purpose: network.cpp already knows
   how to drive that, and a peer carried by a data channel should look the
   same to the protocol whichever platform opened it.

   --- links -------------------------------------------------------------------
   A LINK is one peer connection plus its data channel. The host owns three,
   one per guest seat; a guest owns link one. Links are numbered from one so
   the number can live in Peer::sock beside real sockets without colliding
   with zero. Slots (seat numbers, the broker's word) are link minus one.

   Each link carries a GENERATION that bumps whenever it is rebuilt. A peer
   records the generation it adopted, and closing with a stale generation does
   nothing -- which is what stops the game tearing down a seat the room
   service has already re-offered to the returning player.

   Everything here is safe to call from any thread.
   ========================================================================== */

enum RtcLinkState {
    RTC_LINK_IDLE = 0,
    RTC_LINK_GATHERING,   /* building our description                         */
    RTC_LINK_WAITING,     /* host: offer ready, no answer yet                  */
    RTC_LINK_CONNECTING,  /* descriptions exchanged, ICE and DTLS under way    */
    RTC_LINK_OPEN,
    RTC_LINK_CLOSED,
    RTC_LINK_FAILED
};

static const int RTC_MAX_LINKS = 3;

/* Host: build offers for every seat. The codes appear in rtcNetHostCode a
   second or so later, once ICE has finished gathering. */
void rtcNetBeginHost();
/* Host: throw one seat away and build a fresh offer for it. A WebRTC link
   that dropped cannot be resumed, so this is the only way back into a seat. */
void rtcNetRehost(int slot);
/* Guest: consume the host's code and build the reply. */
void rtcNetBeginJoin(const char* code);
/* Host: consume a guest's reply for one seat. */
void rtcNetBeginAccept(int slot, const char* code);

/* Empty until ready. */
std::string rtcNetHostCode(int slot);
std::string rtcNetJoinCode();

/* The latest thing that went wrong, worded for the player. Empty when fine. */
std::string rtcNetFault();
/* A line in the CINDERLIFT_RTC_LOG file, when there is one. For the game's
   side of a connection's story, beside libdatachannel's. */
void rtcNetTrace(const char* what, int a, int b);
void rtcNetSetFault(const char* text);

RtcLinkState rtcNetState(int slot);
u32 rtcNetGeneration(int slot);

/* The socket-shaped half, addressed by LINK (slot + 1). */
bool rtcNetOpen(int link);
bool rtcNetFailed(int link);
void rtcNetClose(int link, u32 generation);
void rtcNetCloseAll();
/* >0 bytes read, 0 for nothing waiting. Never blocks. */
int  rtcNetRecv(int link, u8* buf, int cap);
/* >0 bytes taken, 0 when the channel is closed or its send queue is full.
   Never blocks. */
int  rtcNetSend(int link, const u8* buf, int n);

/* --- connection codes ------------------------------------------------------
   Exposed for tests. `type` is "offer" or "answer". */
std::string rtcPackCode(const char* type, const std::string& sdp);
bool rtcUnpackCode(const char* code, std::string* type, std::string* sdp,
                   std::string* error);

/* ============================================================================
   Rooms: the five-character codes, through the broker in signal/worker.js.
   Signalling runs on a background thread; these only start and stop it.
   ========================================================================== */

/* Host: build three offers and open a room. The code appears in
   rtcRoomCode() once the broker has answered. */
void rtcRoomHost();
/* Guest: fetch an offer for `code`, answer it, post the answer. Rejoins
   the same seat by itself if the link drops later. */
void rtcRoomJoin(const char* code);
void rtcRoomStop();
/* The room's code, or empty while it is still being opened. */
std::string rtcRoomCode();
/* Guest only: something to tell the player while signalling or rejoining,
   or empty. */
std::string rtcRoomStatus();
