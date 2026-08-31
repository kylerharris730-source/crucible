#pragma once
#include "../common.h"

/* ============================================================================
   netshim.h -- what network.cpp needs from a platform that has no sockets.

   The same idea as web/win32.h: reimplement the API the game already speaks
   rather than fork the game. network.cpp holds the entire protocol -- the
   handshake, packet framing, state serialization, chunk repair -- and none of
   that cares whether the bytes arrive over TCP or a WebRTC data channel. A
   second copy of it maintained beside the first would drift, and drift between
   two implementations of a network protocol shows up as desyncs, which is the
   most expensive kind of bug this project could give itself.

   So the transport is swapped underneath and the protocol is untouched.

   --- what a socket means here ------------------------------------------------
   Nothing, really. There is one connection in a tab: you are either the host
   with one guest, or the guest. `SOCKET` is a token that says "this peer slot
   is in use", and the only values it takes are 0 and 1. There is no listening
   socket, no accept queue, and no address -- WebRTC connects two peers that
   have exchanged descriptions, and the description is what the player pastes.

   That is why netJoin's `ipv4` argument carries an offer code here instead of
   an address. The signature is the same; the meaning is what the transport can
   actually use.
   ========================================================================== */

typedef int SOCKET;
static const SOCKET INVALID_SOCKET = 0;
static const SOCKET WEB_PEER_SOCKET = 1;

/* --- the transport, backed by web/webrtc.js -------------------------------- */

/* Host: start building an offer. The code appears in webNetHostCode() a second
   or two later, once ICE has finished gathering -- see the note in webrtc.js
   about why it waits rather than trickling. */
void webNetBeginHost();

/* Guest: consume the host's code and start building the reply. */
void webNetBeginJoin(const char* code);

/* Host: consume the guest's reply and finish the connection. */
void webNetBeginAccept(const char* code);

/* Empty until ready. Both are the text the player copies. */
const char* webNetHostCode();
const char* webNetJoinCode();

/* Whatever went wrong, for the player rather than the log. Empty when fine. */
const char* webNetFault();

/* Is the data channel carrying bytes yet? */
bool webNetOpen();
/* Has it given up? Without TURN this is a real outcome, not an error path. */
bool webNetFailed();

void webNetClose();

/* >0 bytes read, 0 for nothing waiting. Never blocks. */
int  webNetRecv(u8* buf, int cap);
/* >0 bytes taken, 0 if the channel is not open. Never blocks. */
int  webNetSend(const u8* buf, int n);
