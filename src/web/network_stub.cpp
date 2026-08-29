/* ============================================================================
   network_stub.cpp -- multiplayer, declined politely.

   A browser tab cannot open a TCP socket, and network.cpp is winsock from top
   to bottom. Rather than teach the game a second transport, the web build
   satisfies network.h with honest refusals: netHost and netJoin fail, the role
   is permanently NET_OFF, and every per-frame entry point does nothing.

   That is a real limitation and it is deliberate rather than temporary. Making
   direct-IP play work in a tab needs a relay server standing between the two
   players -- something to host, secure and keep running -- which is exactly
   the ongoing burden this port exists to avoid. Native builds keep the
   winsock path untouched, so multiplayer is unaffected where it already works.

   netStatus() carries that explanation to the menu, so a player who clicks
   Host in the browser is told why instead of watching nothing happen.
   ========================================================================== */
#include "network.h"

bool netHost(u16, bool)              { return false; }
bool netJoin(const char*, u16)       { return false; }
void netStop()                       {}
void netPoll(World&)                 {}
void netHostFrame(World&)            {}
void netClientFrame(World&)          {}

void netMarkWorldEdit(int, int, int) {}
void netMarkPredictedWorldEdit(int, int, int, int, int, u32) {}

bool netSendCommand(const PlayerCommand&)          { return false; }
bool netPopRemoteCommand(PlayerId, PlayerCommand*) { return false; }
void netMarkRemoteCommandApplied(PlayerId, u32)    {}
u32  netAcknowledgedCommand()                      { return 0; }
void netMarkRemoteActionApplied(PlayerId, u32)     {}
u32  netAcknowledgedAction()                       { return 0; }
u32  netStateSerial()                              { return 0; }
bool netSendAction(const NetAction&)               { return false; }
bool netPopRemoteAction(NetAction*)                { return false; }

NetRole  netRole()          { return NET_OFF; }
bool     netConnected()     { return false; }
int      netPeerCount()     { return 0; }
bool     netReady()         { return false; }
bool     netClientReady()   { return false; }
PlayerId netAssignedPlayer(){ return 0; }

const char* netStatus() {
    return "Multiplayer is not available in the browser -- download the game to host or join";
}
const char* netLocalAddress() { return "browser"; }
