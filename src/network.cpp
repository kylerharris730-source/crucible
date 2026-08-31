#define WIN32_LEAN_AND_MEAN
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
/* A tab has no sockets. See web/netshim.h for what stands in and why the
   protocol below is untouched by the difference. */
#include "web/netshim.h"
#endif
#include "network.h"
#include "save.h"
#include "entity.h"
#include "projectile.h"
#include "device.h"
#include "light.h"
#include "drone.h"
#include "room.h"
#include "tree.h"
#include "codec.h"
#include <vector>
#include <deque>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef CINDERLIFT_BUILD_ID
#ifdef CINDERLIFT_TEST_MISMATCH_BUILD
#define CINDERLIFT_BUILD_ID "intentional-mismatch-test-build"
#else
#define CINDERLIFT_BUILD_ID "development"
#endif
#endif

static const u32 NET_MAGIC = 0x54454E43u; /* CNET on little endian */
static const u32 NET_PROTOCOL = 13;
static const u32 NET_STATE_SCHEMA = 16;   /* + the swing, so other players' blades are visible */
static const u32 NET_MAX_PACKET = 256u * 1024u * 1024u;

enum PacketType {
    PK_HELLO = 1,
    PK_REJECT,
    PK_WELCOME,
    PK_WORLD_SNAPSHOT,
    PK_READY,
    PK_COMMAND,
    PK_STATE,
    PK_CHUNK,
    PK_ACTION,
    PK_CHUNK_HASHES
};

struct Writer {
    std::vector<u8> b;
    void u8v(u8 v) { b.push_back(v); }
    void u16v(u16 v) { u8v((u8)v); u8v((u8)(v >> 8)); }
    void u32v(u32 v) { for (int i = 0; i < 4; ++i) u8v((u8)(v >> (i * 8))); }
    void i32v(i32 v) { u32v((u32)v); }
    void bytes(const void* p, size_t n) {
        const u8* q = (const u8*)p; b.insert(b.end(), q, q + n);
    }
    void string(const char* s) {
        const size_t n0 = strlen(s); const u16 n = (u16)(n0 > 65535 ? 65535 : n0);
        u16v(n); bytes(s, n);
    }
};

struct Reader {
    const u8* p; size_t n, at; bool ok;
    Reader(const u8* data, size_t len) : p(data), n(len), at(0), ok(true) {}
    u8 u8v() { if (at + 1 > n) { ok = false; return 0; } return p[at++]; }
    u16 u16v() { u16 v = u8v(); v |= (u16)u8v() << 8; return v; }
    u32 u32v() { u32 v = 0; for (int i = 0; i < 4; ++i) v |= (u32)u8v() << (i * 8); return v; }
    i32 i32v() { return (i32)u32v(); }
    bool bytes(void* out, size_t len) {
        if (at + len > n) { ok = false; return false; }
        memcpy(out, p + at, len); at += len; return true;
    }
    bool string(char* out, size_t cap) {
        const u16 len = u16v();
        if (!ok || at + len > n || cap == 0) { ok = false; return false; }
        const size_t take = len < cap - 1 ? len : cap - 1;
        memcpy(out, p + at, take); out[take] = 0; at += len; return true;
    }
};

/* Slot zero is the host itself, so a full game is the host plus this many
   joined sockets. The world model already carried four stable slots; only the
   transport assumed one. */
static const int MAX_PEERS = MAX_PLAYERS - 1;

/* Everything one connection owns. Each of these was a file-scope global while
   the host accepted a single socket, and every one of them turned out to be
   per-connection the moment a second player could exist: two peers are at
   different points in the handshake, are owed different acknowledgement
   watermarks, and hold different parts of the world. Sharing any of them
   between peers is the failure mode this struct exists to prevent. */
struct Peer {
    SOCKET sock;
    std::vector<u8> recv, send;
    size_t sendAt;
    bool connecting, handshake, ready;
    PlayerId assigned;      /* host: the player slot given to this connection */
    /* Who this connection says it is, from the handshake. An identity, not
       a credential -- see identity.h. The host uses it to give somebody back
       the pack they left with. */
    char identity[PLAYER_IDENTITY_CHARS + 1];
    u32 appliedCommand;     /* newest command from THIS peer the host consumed */
    u32 appliedAction;
    int scanCursor, urgentCursor, frame;
    /* The host's belief about the replica this peer holds. Two players standing
       in different caves must not share one hash table, or each would
       permanently invalidate the other's chunks. */
    u32 chunkHash[CHUNK_COUNT];
    u8 urgentChunk[CHUNK_COUNT];

    Peer() { sock = INVALID_SOCKET; clear(); }
    void clear() {
        identity[0] = 0;
        recv.clear(); send.clear(); sendAt = 0;
        connecting = handshake = ready = false; assigned = PLAYER_NONE;
        appliedCommand = appliedAction = 0;
        scanCursor = urgentCursor = frame = 0;
        memset(chunkHash, 0, sizeof(chunkHash));
        memset(urgentChunk, 0, sizeof(urgentChunk));
    }
    bool live() const { return sock != INVALID_SOCKET; }
};

/* A client uses exactly one of these, index zero, for its outbound connection.
   Host and client therefore share the framing, handshake and pump code rather
   than growing a second copy of it. */
static Peer g_peers[MAX_PEERS];

static NetRole g_role = NET_OFF;
static SOCKET g_listen = INVALID_SOCKET;
static bool g_wsa = false;
static char g_status[192] = "Offline";
static char g_localAddress[64] = "127.0.0.1";
/* Client-side only. The host's equivalents live on each Peer. */
static PlayerId g_assigned = PLAYER_NONE;
static bool g_clientReady = false;
static u32 g_acknowledgedCommand = 0;
static u32 g_acknowledgedAction = 0;
static u32 g_stateSerial = 0;
/* One held command per player slot. Coalescing has to be per player: folding
   two players' inputs into a single latest-command slot would let a fast
   sender starve a slow one, and would apply one player's aim to another. */
static PlayerCommand g_remoteCommand[MAX_PLAYERS];
static bool g_haveRemoteCommand[MAX_PLAYERS];
static std::deque<NetAction> g_remoteActions;
/* Hash of the last AUTHORITATIVE contents received for each client chunk.
   Client-side world prediction is allowed to change g_world between packets;
   reporting that speculative hash would make the host continuously undo it. */
static u32 g_clientAuthorityHash[CHUNK_COUNT];
/* Highest unacknowledged local world-edit command touching each chunk. A
   chunk packet captured before that command must not erase its prediction. */
static u32 g_clientPredictedChunkCommand[CHUNK_COUNT];

static void packBytes(Writer& out, const u8* src, size_t n);
static bool unpackBytes(Reader& in, std::vector<u8>& out);

static void statusf(const char* fmt, const char* arg = 0) {
    if (arg) sprintf(g_status, fmt, arg); else strcpy(g_status, fmt);
}

#ifndef _WIN32
/* Nothing to start, and no local address to report: a tab does not know its
   own IP and does not need to, because nobody dials it. The other player is
   reached with a pasted code, not an address. */
static bool startup() { g_wsa = true; return true; }
static bool nonblocking(SOCKET) { return true; }
static void preferLowLatency(SOCKET) {}
static void closeSocket(SOCKET& s) { if (s != INVALID_SOCKET) { webNetClose(); s = INVALID_SOCKET; } }
#else
static bool startup() {
    if (g_wsa) return true;
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { statusf("Network startup failed"); return false; }
    g_wsa = true;
    char host[256];
    if (gethostname(host, sizeof(host)) == 0) {
        hostent* he = gethostbyname(host);
        if (he) for (int i = 0; he->h_addr_list[i]; ++i) {
            in_addr a; memcpy(&a, he->h_addr_list[i], sizeof(a));
            const char* ip = inet_ntoa(a);
            if (ip && strcmp(ip, "127.0.0.1") != 0) { strncpy(g_localAddress, ip, sizeof(g_localAddress)-1); g_localAddress[sizeof(g_localAddress)-1] = 0; break; }
        }
    }
    return true;
}

static bool nonblocking(SOCKET s) {
    u_long one = 1;
    return ioctlsocket(s, FIONBIO, &one) != SOCKET_ERROR;
}

static void preferLowLatency(SOCKET s) {
    /* Commands are one tiny packet per frame. Nagle plus delayed ACKs can turn
       that pattern into visible bursts even on a zero-loss LAN, so do not wait
       to coalesce them behind a previous small command. */
    BOOL one = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
}

static void closeSocket(SOCKET& s) {
    if (s != INVALID_SOCKET) { closesocket(s); s = INVALID_SOCKET; }
}
#endif

/* ==========================================================================
   The transport seam
   ==========================================================================

   Everything below this line in the file is protocol: the handshake, packet
   framing, state serialization, chunk repair. None of it knows what carries
   the bytes. These functions are the entire surface that does.

   They exist because a browser tab cannot open a TCP socket, and the port
   of this game to the browser is built on reimplementing platform APIs
   rather than forking the game -- see src/web/win32.h, which does the same
   thing for the Win32 API. A second copy of the protocol maintained beside
   this one would drift, and the failures it produced would be desyncs: the
   most expensive kind of bug to find.

   The winsock implementations are the code that was previously inline at
   each of these call sites, moved rather than rewritten. Windows behaviour
   is meant to be identical, which is what network_smoke and network_four
   are there to confirm. */

/* Non-blocking read. >0 bytes, 0 for nothing waiting, -1 for a clean close,
   -2 for a broken connection. Those last two are distinguished because they
   say different things to the player. */
#ifndef _WIN32
/* The data channel is reliable and ordered, so there is no partial-failure
   case to report: either bytes are waiting or they are not. A channel that
   has given up reads as a clean close, which is what the player sees. */
static int txRecv(Peer&, u8* buf, int cap) {
    const int n = webNetRecv(buf, cap);
    if (n > 0) return n;
    return webNetFailed() ? -1 : 0;
}
static int txSend(Peer&, const u8* buf, int n) {
    const int sent = webNetSend(buf, n);
    if (sent > 0) return sent;
    return webNetFailed() ? -1 : 0;
}
/* Connecting finishes when the channel opens. There is no socket to ask, so
   this is the same question the rest of the file asks. */
static bool txConnectPoll(Peer&, bool* failed) {
    *failed = webNetFailed();
    return webNetOpen() || *failed;
}
static bool txReady(Peer& peer, bool* readable, bool* writable) {
    *readable = webNetOpen();
    *writable = webNetOpen() && peer.sendAt < peer.send.size();
    return true;
}
#else
static int txRecv(Peer& peer, u8* buf, int cap) {
    const int n = recv(peer.sock, (char*)buf, cap, 0);
    if (n > 0) return n;
    if (n == 0) return -1;
    return WSAGetLastError() == WSAEWOULDBLOCK ? 0 : -2;
}

/* >0 bytes taken, 0 if the transport is full for now, -1 on failure. */
static int txSend(Peer& peer, const u8* buf, int n) {
    const int sent = send(peer.sock, (const char*)buf, n, 0);
    if (sent > 0) return sent;
    return WSAGetLastError() == WSAEWOULDBLOCK ? 0 : -1;
}

/* Has an in-progress connection resolved? Sets *failed when it resolved
   badly. Returning false means still waiting. */
static bool txConnectPoll(Peer& peer, bool* failed) {
    *failed = false;
    fd_set writeSet, errSet; FD_ZERO(&writeSet); FD_ZERO(&errSet);
    FD_SET(peer.sock, &writeSet); FD_SET(peer.sock, &errSet); timeval tv = { 0, 0 };
    if (select(0, 0, &writeSet, &errSet, &tv) <= 0) return false;
    int err = 0; int len = sizeof(err);
    getsockopt(peer.sock, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
    if (err || FD_ISSET(peer.sock, &errSet)) { *failed = true; return true; }
    return true;
}

/* Whether a read or a write would make progress this frame. Split because
   sending first is what lets a just-connected client deliver HELLO before
   either side waits on the other. */
static bool txReady(Peer& peer, bool* readable, bool* writable) {
    fd_set readSet, writeSet;
    FD_ZERO(&readSet); FD_ZERO(&writeSet);
    FD_SET(peer.sock, &readSet);
    if (peer.sendAt < peer.send.size()) FD_SET(peer.sock, &writeSet);
    timeval tv = { 0, 0 };
    if (select(0, &readSet, &writeSet, 0, &tv) == SOCKET_ERROR) return false;
    *readable = FD_ISSET(peer.sock, &readSet) != 0;
    *writable = FD_ISSET(peer.sock, &writeSet) != 0;
    return true;
}
#endif

static int connectedPeerCount() {
    int n = 0;
    for (int i = 0; i < MAX_PEERS; ++i) if (g_peers[i].live()) ++n;
    return n;
}

/* Drop one connection without touching anybody else's. The player slot is
   released so a later joiner can reuse it; the generation counter in that slot
   is what stops this peer's in-flight packets acting on its replacement. */
static void disconnectPeer(Peer& peer, const char* reason) {
    closeSocket(peer.sock);
    if (g_role == NET_HOST && peer.assigned != PLAYER_NONE) {
        const PlayerId gone = peer.assigned;
        /* Before the close, which is what frees their tools. The roster
           takes copies rather than the handles, so closing is still right. */
        rosterRemember(peer.identity, g_playerSessions[gone]);
        playerSessionClose(gone);
        if (gone < MAX_PLAYERS) g_haveRemoteCommand[gone] = false;
        /* Queued actions from a departed player would otherwise be applied to
           whoever inherits the slot. */
        for (size_t i = g_remoteActions.size(); i-- > 0;)
            if (g_remoteActions[i].player == gone)
                g_remoteActions.erase(g_remoteActions.begin() + (long)i);
    }
    peer.clear();
    if (g_role == NET_HOST) {
        const int left = connectedPeerCount();
        if (left > 0) sprintf(g_status, "Player left -- %d connected", left);
        else statusf("Player left -- waiting for player");
    } else {
        g_clientReady = false; g_assigned = PLAYER_NONE;
        g_acknowledgedCommand = g_acknowledgedAction = 0; g_stateSerial = 0;
        memset(g_clientAuthorityHash, 0, sizeof(g_clientAuthorityHash));
        memset(g_clientPredictedChunkCommand, 0, sizeof(g_clientPredictedChunkCommand));
        statusf(reason ? reason : "Disconnected from host");
    }
}

void netStop() {
    const bool wasClient = g_role == NET_CLIENT;
    for (int i = 0; i < MAX_PEERS; ++i) { closeSocket(g_peers[i].sock); g_peers[i].clear(); }
    closeSocket(g_listen);
    g_assigned = PLAYER_NONE; g_clientReady = false;
    for (int i = 0; i < MAX_PLAYERS; ++i) g_haveRemoteCommand[i] = false;
    g_remoteActions.clear();
    g_acknowledgedCommand = g_acknowledgedAction = 0; g_stateSerial = 0;
    memset(g_clientAuthorityHash, 0, sizeof(g_clientAuthorityHash));
    memset(g_clientPredictedChunkCommand, 0, sizeof(g_clientPredictedChunkCommand));
    if (g_role == NET_HOST)
        for (int i = 1; i < MAX_PLAYERS; ++i) {
            playerSessionClose((PlayerId)i);
        }
    if (wasClient) playerSessionsReturnToOffline();
    g_role = NET_OFF; statusf("Offline");
}

#ifndef _WIN32
/* There is no port and nothing to bind. Hosting in a tab means starting an
   offer; the player hands the resulting code to their friend, and the peer
   slot fills in when the channel opens -- see netPoll. */
bool netHost(u16, bool) {
    netStop(); startup();
    webNetBeginHost();
    g_role = NET_HOST;
    statusf("Making a host code -- this takes a moment");
    return true;
}

/* `ipv4` carries an offer code here rather than an address. See netshim.h:
   the signature is the game's, the meaning is what the transport has. */
bool netJoin(const char* ipv4, u16) {
    netStop(); startup();
    if (!ipv4 || !*ipv4) { statusf("Paste the host's code first"); return false; }
    webNetBeginJoin(ipv4);
    Peer& peer = g_peers[0];
    peer.clear();
    peer.sock = WEB_PEER_SOCKET;
    peer.connecting = true;
    g_role = NET_CLIENT;
    statusf("Making a join code -- send it back to the host");
    return true;
}
#else
bool netHost(u16 port, bool loopbackOnly) {
    netStop(); if (!startup()) return false;
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen == INVALID_SOCKET) { statusf("Could not create host socket"); return false; }
    BOOL reuse = TRUE; setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    sockaddr_in a; memset(&a, 0, sizeof(a)); a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(g_listen, (sockaddr*)&a, sizeof(a)) == SOCKET_ERROR ||
        listen(g_listen, MAX_PEERS) == SOCKET_ERROR) {
        closeSocket(g_listen); statusf("Could not bind LAN host port"); return false;
    }
    if (!nonblocking(g_listen)) {
        closeSocket(g_listen); statusf("Could not configure host socket"); return false;
    }
    g_role = NET_HOST;
    sprintf(g_status, "Hosting on port %u -- waiting for player", (unsigned)port);
    return true;
}

bool netJoin(const char* ipv4, u16 port) {
    netStop(); if (!startup()) return false;
    Peer& peer = g_peers[0];
    peer.sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (peer.sock == INVALID_SOCKET) { statusf("Could not create client socket"); return false; }
    if (!nonblocking(peer.sock)) {
        closeSocket(peer.sock); statusf("Could not configure client socket"); return false;
    }
    preferLowLatency(peer.sock);
    sockaddr_in a; memset(&a, 0, sizeof(a)); a.sin_family = AF_INET;
    a.sin_addr.s_addr = inet_addr(ipv4); a.sin_port = htons(port);
    if (a.sin_addr.s_addr == INADDR_NONE) { closeSocket(peer.sock); statusf("Enter a numeric IPv4 address"); return false; }
    const int r = connect(peer.sock, (sockaddr*)&a, sizeof(a));
    if (r == SOCKET_ERROR) {
        const int e = WSAGetLastError();
        if (e != WSAEWOULDBLOCK && e != WSAEINPROGRESS) {
            closeSocket(peer.sock); statusf("Could not start connection"); return false;
        }
    }
    g_role = NET_CLIENT; peer.connecting = r == SOCKET_ERROR;
    sprintf(g_status, "Connecting to %s:%u", ipv4, (unsigned)port);
    return true;
}
#endif

static void queuePacket(Peer& peer, u8 type, const std::vector<u8>& payload) {
    if (!peer.live()) return;
    const u32 len = (u32)payload.size() + 1;
    for (int i = 0; i < 4; ++i) peer.send.push_back((u8)(len >> (i * 8)));
    peer.send.push_back(type);
    peer.send.insert(peer.send.end(), payload.begin(), payload.end());
}

static size_t backlog(const Peer& peer) { return peer.send.size() - peer.sendAt; }

static void queueHello(Peer& peer) {
    Writer w; w.u32v(NET_MAGIC); w.u32v(NET_PROTOCOL); w.string(CINDERLIFT_BUILD_ID);
    w.string(playerIdentity());
    queuePacket(peer, PK_HELLO, w.b); peer.handshake = true;
    statusf("Connected -- checking game build");
}

static bool readWholeFile(const char* path, std::vector<u8>& out) {
    FILE* f = fopen(path, "rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); const long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return false; }
    out.resize((size_t)n);
    const bool ok = n == 0 || fread(&out[0], 1, (size_t)n, f) == (size_t)n;
    fclose(f); return ok;
}

static bool writeWholeFile(const char* path, const u8* p, size_t n) {
    FILE* f = fopen(path, "wb"); if (!f) return false;
    const bool ok = n == 0 || fwrite(p, 1, n, f) == n;
    fclose(f); return ok;
}

/* Repository launches already have this directory; a copied standalone EXE
   does not. Keep the original fixed snapshot paths that Defender accepts and
   create only their parent directory, rather than moving received network data
   into the system temporary directory. */
#ifdef _WIN32
static bool ensureSnapshotDirectory() {
    if (CreateDirectoryA("build", 0)) return true;
    /* ERROR_ALREADY_EXISTS is 183 in WinError.h; this old MinGW's lean Win32
       headers expose the APIs but omit that symbolic constant here. */
    return GetLastError() == 183u;
}
static const char* SNAPSHOT_SCRATCH = "build\\net-host-snapshot.tmp";
static const char* CLIENT_SNAPSHOT_SCRATCH = "build\\net-client-snapshot.tmp";
#else
/* A tab's filesystem is made at startup and always has a root to write in,
   so there is no directory to create. The scratch file goes there rather
   than into /saves: it is a few megabytes of temporary bytes, and putting it
   in the IDBFS mount would push a whole world through IndexedDB on every
   join for no reason. */
static bool ensureSnapshotDirectory() { return true; }
static const char* SNAPSHOT_SCRATCH = "/net-host-snapshot.tmp";
static const char* CLIENT_SNAPSHOT_SCRATCH = "/net-client-snapshot.tmp";
#endif

static void sendSnapshot(Peer& peer, World& world) {
    const char* path = SNAPSHOT_SCRATCH;
    if (!ensureSnapshotDirectory()) { statusf("Could not create snapshot directory"); return; }
    if (!saveWrite(path, world)) { statusf("Could not make join snapshot"); return; }
    std::vector<u8> bytes;
    if (!readWholeFile(path, bytes)) { remove(path); statusf("Could not read join snapshot"); return; }
    remove(path); queuePacket(peer, PK_WORLD_SNAPSHOT, bytes);
    statusf("Sending world snapshot");
}

/* The world half of a state packet is identical for everybody; only the two
   acknowledgement watermarks are per peer. Keeping those outside the packed
   block lets one serialization and one compression pass serve every peer,
   instead of doing that work once per connection every state frame. */
static void sendState() {
    Writer w;
    w.u32v(NET_STATE_SCHEMA);
    u8 count = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i) if (g_playerSessions[i].connected) ++count;
    w.u8v(count);
    Blob blob(w.b);
    for (int i = 0; i < MAX_PLAYERS; ++i) if (g_playerSessions[i].connected) {
        PlayerSession& s = g_playerSessions[i];
        w.u8v(s.networkId); w.u16v(s.generation);
        codecPlayer(blob, s.body); codecInventory(blob, s.inventory);
        codecItemStack(blob, s.cursor); codecItemStack(blob, s.trash);
        blob.i32f(s.restBed);
        blob.i32f(s.respawnBedX); blob.i32f(s.respawnBedY); blob.i32f(s.respawnFrames);
        blob.i32f(s.healCooldown);
        blob.i32f(s.openDevice);
        for (int d = 0; d < MAX_DRONES; ++d) codecDrone(blob, s.drones[d]);
        /* The stroke, so everyone can see the blade that is doing the
           damage. Only what DRAWING needs: swingCool is the rhythm gate and
           stays the authority's business, since nobody renders a cooldown. */
        blob.i32f(s.swingFrame); blob.f32f(s.swingDirX); blob.f32f(s.swingDirY);
    }
    codecOverlay(blob);
    Writer packed;
    packBytes(packed, w.b.empty() ? 0 : &w.b[0], w.b.size());
    for (int i = 0; i < MAX_PEERS; ++i) {
        Peer& peer = g_peers[i];
        if (!peer.live() || !peer.ready) continue;
        Writer out;
        out.u32v(peer.appliedCommand); out.u32v(peer.appliedAction);
        out.bytes(packed.b.empty() ? 0 : &packed.b[0], packed.b.size());
        queuePacket(peer, PK_STATE, out.b);
    }
}

static u32 hashChunk(const World& world, int cx, int cy) {
    u32 h = 2166136261u;
    const int x0 = cx << CHUNK_SHIFT, y0 = cy << CHUNK_SHIFT;
    for (int y = y0; y < y0 + CHUNK; ++y) for (int x = x0; x < x0 + CHUNK; ++x) {
        const int i = y * SIM_W + x; const Cell& c = world.cells[i];
        const u8 v[5] = { c.mat, c.moisture, c.tint, world.temp[i], world.bg[i] };
        for (int k = 0; k < 5; ++k) { h ^= v[k]; h *= 16777619u; }
    }
    return h ? h : 1;
}

/* PackBits over an arbitrary byte block. State arrays are mostly unused slots,
   so this removes long zero runs without another dependency or platform codec.
   The uncompressed size is explicit and bounded by the decoder. */
static void packBytes(Writer& out, const u8* src, size_t n) {
    out.u32v((u32)n);
    for (size_t i = 0; i < n;) {
        size_t run = 1;
        while (i + run < n && run < 128 && src[i + run] == src[i]) ++run;
        if (run >= 3) {
            out.u8v((u8)(0x80 | (run - 1))); out.u8v(src[i]); i += run;
        } else {
            const size_t start = i; i += run;
            while (i < n && i - start < 128) {
                size_t next = 1;
                while (i + next < n && next < 3 && src[i + next] == src[i]) ++next;
                /* Do not let a two-byte run straddle the 128-byte literal
                   limit. Encoding length 129 truncates to 0x80, whose high bit
                   tells the decoder this is a repeat block instead. */
                if (next >= 3 || i + next - start > 128) break;
                i += next;
            }
            const size_t len = i - start;
            out.u8v((u8)(len - 1)); out.bytes(src + start, len);
        }
    }
}

static bool unpackBytes(Reader& in, std::vector<u8>& out) {
    const u32 rawSize = in.u32v();
    if (!in.ok || rawSize > 16u * 1024u * 1024u) { in.ok = false; return false; }
    out.resize(rawSize); size_t at = 0;
    while (in.ok && at < out.size()) {
        const u8 tag = in.u8v(); const size_t len = (tag & 0x7F) + 1;
        if (at + len > out.size()) { in.ok = false; break; }
        if (tag & 0x80) {
            const u8 value = in.u8v();
            if (in.ok) memset(&out[at], value, len);
        } else {
            if (in.at + len > in.n) { in.ok = false; break; }
            memcpy(&out[at], in.p + in.at, len); in.at += len;
        }
        at += len;
    }
    if (!in.ok || at != out.size() || in.at != in.n) { in.ok = false; return false; }
    return true;
}

/* PackBits-style plane encoding: high bit means a repeated byte, low seven
   bits store length-1. Literal and repeat spans are each capped at 128. */
static void packPlane(Writer& out, const u8* src, int n) {
    Writer packed;
    for (int i = 0; i < n;) {
        int run = 1;
        while (i + run < n && run < 128 && src[i + run] == src[i]) ++run;
        if (run >= 3) {
            packed.u8v((u8)(0x80 | (run - 1))); packed.u8v(src[i]); i += run;
        } else {
            const int start = i; i += run;
            while (i < n && i - start < 128) {
                int next = 1;
                while (i + next < n && next < 3 && src[i + next] == src[i]) ++next;
                if (next >= 3 || i + next - start > 128) break;
                i += next;
            }
            const int len = i - start;
            packed.u8v((u8)(len - 1)); packed.bytes(src + start, len);
        }
    }
    out.u16v((u16)packed.b.size()); out.bytes(packed.b.empty() ? 0 : &packed.b[0], packed.b.size());
}

static bool unpackPlane(Reader& in, u8* dst, int n) {
    const u16 bytes = in.u16v();
    if (!in.ok || in.at + bytes > in.n) { in.ok = false; return false; }
    const size_t end = in.at + bytes; int at = 0;
    while (in.at < end && at < n) {
        const u8 tag = in.u8v(); const int len = (tag & 0x7F) + 1;
        if (tag & 0x80) {
            const u8 v = in.u8v();
            if (at + len > n) { in.ok = false; return false; }
            memset(dst + at, v, len); at += len;
        } else {
            if (at + len > n || in.at + len > end) { in.ok = false; return false; }
            memcpy(dst + at, in.p + in.at, len); in.at += len; at += len;
        }
    }
    if (in.at != end || at != n) { in.ok = false; return false; }
    return true;
}

static void sendChunk(Peer& peer, const World& world, int cx, int cy) {
    static u8 plane[CHUNK * CHUNK];
    Writer w; w.u16v((u16)cx); w.u16v((u16)cy);
    /* This is the newest input from THIS peer represented by the snapshot. The
       client uses it to distinguish a correction from a packet that was already
       old when its locally predicted placement happened. Another player's
       command sequence would be meaningless here. */
    w.u32v(peer.appliedCommand);
    const int x0 = cx << CHUNK_SHIFT, y0 = cy << CHUNK_SHIFT;
    for (int field = 0; field < 6; ++field) {
        int p = 0;
        for (int y = y0; y < y0 + CHUNK; ++y) for (int x = x0; x < x0 + CHUNK; ++x) {
            const int i = y * SIM_W + x; const Cell& c = world.cells[i];
            plane[p++] = field == 0 ? c.mat : field == 1 ? c.moisture : field == 2 ? c.tint
                       : field == 3 ? world.temp[i] : field == 4 ? world.bg[i]
                       /* Bit zero is durable flow/fall state. Bits 1..7 are a
                          host-local frame stamp and are neither useful nor
                          compressible on another simulation. */
                       : (u8)(c.flags & F_DIR);
        }
        packPlane(w, plane, CHUNK * CHUNK);
    }
    queuePacket(peer, PK_CHUNK, w.b);
}

static void sendChangedChunks(Peer& peer, const World& world) {
    const int slot = playerSessionSlotForNetworkId(peer.assigned);
    if (slot < 0) return;
    const Player& p = g_playerSessions[slot].body;
    const int x0 = imax(0, (int)p.centreX() - 512), x1 = imin(SIM_W - 1, (int)p.centreX() + 512);
    const int y0 = imax(0, (int)p.centreY() - 448), y1 = imin(SIM_H - 1, (int)p.centreY() + 448);
    const int cx0 = x0 >> CHUNK_SHIFT, cx1 = x1 >> CHUNK_SHIFT;
    const int cy0 = y0 >> CHUNK_SHIFT, cy1 = y1 >> CHUNK_SHIFT;
    const int width = cx1 - cx0 + 1, height = cy1 - cy0 + 1;
    const int total = width * height;
    if (total <= 0) return;
    /* Hashing the entire interest rectangle fifteen times a second was tens of
       millions of byte operations even in a settled cave. A rotating budget
       covers the full view-plus-margin in about a second, while command/player
       state keeps its low latency. The cursor is per peer: a shared one would
       advance three times as fast and leave each player's own surroundings
       scanned a third as often. */
    const int budget = imin(48, total);
    for (int n = 0; n < budget; ++n) {
        /* A dirty/random chunk can be much larger than its settled equivalent.
           Bound the batch itself, not merely the queue before entering here. */
        if (backlog(peer) >= 1024u * 1024u) break;
        const int at = (peer.scanCursor + n) % total;
        const int cx = cx0 + at % width, cy = cy0 + at / width;
        const int ci = cy * CHUNKS_X + cx; const u32 h = hashChunk(world, cx, cy);
        if (peer.chunkHash[ci] == h) continue;
        peer.chunkHash[ci] = h; sendChunk(peer, world, cx, cy);
    }
    peer.scanCursor = (peer.scanCursor + budget) % total;
}

/* A client reports a rotating sample of the chunks it actually has. TCP makes
   loss unlikely, but it does not prove the replica is correct: a decoder bug,
   stale write, or future change to interest management can still leave one
   machine looking at different cells forever. These hashes turn chunk repair
   into a closed loop. The host compares against authoritative state and sends
   the ordinary full chunk packet on any mismatch. */
static void sendClientChunkHashes(const World& world) {
    if (g_role != NET_CLIENT || !g_clientReady || !g_peers[0].live() ||
        !g_playerSessions[0].connected) return;
    const Player& p = g_playerSessions[0].body;
    const int x0 = imax(0, (int)p.centreX() - 512), x1 = imin(SIM_W - 1, (int)p.centreX() + 512);
    const int y0 = imax(0, (int)p.centreY() - 448), y1 = imin(SIM_H - 1, (int)p.centreY() + 448);
    const int cx0 = x0 >> CHUNK_SHIFT, cx1 = x1 >> CHUNK_SHIFT;
    const int cy0 = y0 >> CHUNK_SHIFT, cy1 = y1 >> CHUNK_SHIFT;
    const int width = cx1 - cx0 + 1, total = width * (cy1 - cy0 + 1);
    if (total <= 0) return;
    static int cursor = 0;
    const int count = imin(16, total);
    Writer w; w.u8v((u8)count);
    for (int n = 0; n < count; ++n) {
        const int at = (cursor + n) % total;
        const int cx = cx0 + at % width, cy = cy0 + at / width;
        const int ci = cy * CHUNKS_X + cx;
        if (!g_clientAuthorityHash[ci])
            g_clientAuthorityHash[ci] = hashChunk(world, cx, cy);
        w.u16v((u16)cx); w.u16v((u16)cy); w.u32v(g_clientAuthorityHash[ci]);
    }
    cursor = (cursor + count) % total;
    queuePacket(g_peers[0], PK_CHUNK_HASHES, w.b);
}

static void rememberClientInterestHashes(const World& world) {
    if (!g_playerSessions[0].connected) return;
    const Player& p = g_playerSessions[0].body;
    const int cx0 = imax(0, ((int)p.centreX() - 512) >> CHUNK_SHIFT);
    const int cx1 = imin(CHUNKS_X - 1, ((int)p.centreX() + 512) >> CHUNK_SHIFT);
    const int cy0 = imax(0, ((int)p.centreY() - 448) >> CHUNK_SHIFT);
    const int cy1 = imin(CHUNKS_Y - 1, ((int)p.centreY() + 448) >> CHUNK_SHIFT);
    for (int cy = cy0; cy <= cy1; ++cy)
        for (int cx = cx0; cx <= cx1; ++cx)
            g_clientAuthorityHash[cy * CHUNKS_X + cx] = hashChunk(world, cx, cy);
}

static void applyState(Reader& r) {
    const PlayerSession predictedLocal = g_playerSessions[0];
    const bool hadPredictedLocal = g_role == NET_CLIENT && predictedLocal.connected &&
                                   predictedLocal.networkId == g_assigned;
    const u32 schema = r.u32v();
    if (schema != NET_STATE_SCHEMA) { r.ok = false; return; }
    playerSessionsReset();
    for (int i = 0; i < MAX_PLAYERS; ++i) g_playerSessions[i].connected = false;
    const int count = r.u8v(); int nextRemoteSlot = 1;
    /* The reader and the blob walk the same buffer. Player identity stays in
       the outer framing because the slot mapping is a decision the receiver
       makes; the bodies themselves are field-wise. */
    Blob blob(r.p, r.n);
    blob.at = r.at;
    for (int n = 0; n < count; ++n) {
        r.at = blob.at;
        const PlayerId wire = r.u8v(); const u16 generation = r.u16v();
        blob.at = r.at;
        int slot = wire == g_assigned ? 0 : nextRemoteSlot++;
        if (slot >= MAX_PLAYERS || !r.ok) { r.ok = false; return; }
        PlayerSession& s = g_playerSessions[slot];
        codecPlayer(blob, s.body); codecInventory(blob, s.inventory);
        codecItemStack(blob, s.cursor); codecItemStack(blob, s.trash);
        blob.i32f(s.restBed);
        blob.i32f(s.respawnBedX); blob.i32f(s.respawnBedY); blob.i32f(s.respawnFrames);
        blob.i32f(s.healCooldown);
        blob.i32f(s.openDevice);
        for (int d = 0; d < MAX_DRONES; ++d) codecDrone(blob, s.drones[d]);
        /* The stroke, so everyone can see the blade that is doing the
           damage. Only what DRAWING needs: swingCool is the rhythm gate and
           stays the authority's business, since nobody renders a cooldown. */
        blob.i32f(s.swingFrame); blob.f32f(s.swingDirX); blob.f32f(s.swingDirY);
        if (!blob.ok) { r.ok = false; return; }
        s.connected = true; s.local = slot == 0; s.networkId = wire; s.generation = generation;
        if (slot == 0 && hadPredictedLocal && predictedLocal.generation == generation) {
            /* These fields are the client's in-progress interpretation of held
               input, not durable results. Resetting them every state packet
               broke drag strokes into disconnected bursts and restarted dig
               cooldowns. Keep them across an ordinary correction. */
            s.previousAimX = predictedLocal.previousAimX;
            s.previousAimY = predictedLocal.previousAimY;
            s.digCooldown = predictedLocal.digCooldown;
            s.wireX = predictedLocal.wireX; s.wireY = predictedLocal.wireY;
            s.circuitWireFrom = predictedLocal.circuitWireFrom;
            s.circuitWirePort = predictedLocal.circuitWirePort;
            s.suppressRightUse = predictedLocal.suppressRightUse;
            s.lineActive = predictedLocal.lineActive;
            s.lineBits = predictedLocal.lineBits; s.lineSelected = predictedLocal.lineSelected;
            s.lineRadius = predictedLocal.lineRadius; s.lineBrush = predictedLocal.lineBrush;
            s.lineBackground = predictedLocal.lineBackground;
            s.lineOverwrite = predictedLocal.lineOverwrite;
            s.lineFilterOn = predictedLocal.lineFilterOn;
            memcpy(s.lineFilter, predictedLocal.lineFilter, sizeof(s.lineFilter));
            s.previousCommandBits = predictedLocal.previousCommandBits;
            /* Your OWN swing is animated locally so it does not lag a round
               trip behind the button. Taking the host's copy back would
               restart the stroke you are already watching. */
            s.swingFrame = predictedLocal.swingFrame;
            s.swingDirX = predictedLocal.swingDirX;
            s.swingDirY = predictedLocal.swingDirY;
            s.garlicCooldown = predictedLocal.garlicCooldown;
            s.healCooldown = imax(s.healCooldown, predictedLocal.healCooldown);
        }
    }
    codecOverlay(blob);
    if (!blob.ok) { r.ok = false; return; }
    g_worldTime %= DAY_LENGTH;
    r.at = blob.at;   /* hand the cursor back so the caller's length check holds */
}

static void applyChunk(World& world, Reader& r) {
    const int cx = r.u16v(), cy = r.u16v();
    const u32 representedCommand = r.u32v();
    if (cx < 0 || cx >= CHUNKS_X || cy < 0 || cy >= CHUNKS_Y) { r.ok = false; return; }
    static u8 plane[6][CHUNK * CHUNK];
    for (int field = 0; field < 6; ++field) if (!unpackPlane(r, plane[field], CHUNK * CHUNK)) return;
    const int ci = cy * CHUNKS_X + cx;
    const u32 predictedCommand = g_clientPredictedChunkCommand[ci];
    if (predictedCommand && (i32)(representedCommand - predictedCommand) < 0) {
        /* A regular scan may already have queued this pre-edit image when the
           client predicts a placement. Applying it produces the exact
           spawn-freeze-jump sequence this guard prevents. */
        return;
    }
    const int x0 = cx << CHUNK_SHIFT, y0 = cy << CHUNK_SHIFT; int p = 0;
    /* Make every replaced cell eligible on the client's next step. Copying a
       host frame stamp could alias the client's current stamp and skip it. */
    const u8 eligibleStamp = (u8)(((world.stamp() - 1u) & STAMP_MASK) << STAMP_SHIFT);
    for (int y = y0; y < y0 + CHUNK; ++y) for (int x = x0; x < x0 + CHUNK; ++x, ++p) {
        const int i = y * SIM_W + x; Cell& c = world.cells[i];
        c.mat = plane[0][p]; c.moisture = plane[1][p]; c.tint = plane[2][p];
        c.flags = (u8)(eligibleStamp | (plane[5][p] & F_DIR));
        world.temp[i] = plane[3][p]; world.bg[i] = plane[4][p];
    }
    g_clientPredictedChunkCommand[ci] = 0;
    g_clientAuthorityHash[ci] = hashChunk(world, cx, cy);
    /* Direct array replacement bypasses World::setCell, so it must explicitly
       wake the corrected chunk. Otherwise falling powders/liquids are loaded
       into a sleeping dirty rectangle and remain frozen until another packet. */
    world.dirtyArea(x0, y0, x0 + CHUNK - 1, y0 + CHUNK - 1);
}

static void handlePacket(Peer& peer, u8 type, const u8* data, size_t len, World& world) {
    Reader r(data, len);
    if (type == PK_HELLO && g_role == NET_HOST) {
        char build[80]; const u32 magic = r.u32v(), protocol = r.u32v(); r.string(build, sizeof(build));
        /* Read before the build check rejects, so the reader stays in step
           with what the client wrote either way. */
        char who[PLAYER_IDENTITY_CHARS + 1]; who[0] = 0;
        r.string(who, sizeof(who));
        if (!r.ok || magic != NET_MAGIC || protocol != NET_PROTOCOL || strcmp(build, CINDERLIFT_BUILD_ID) != 0) {
            Writer reject; reject.string("Game builds do not match"); queuePacket(peer, PK_REJECT, reject.b); return;
        }
        /* One socket must never be handed a second player slot. Without this a
           repeated HELLO would leak sessions until the game reported itself
           full with nobody else actually connected. */
        if (peer.assigned != PLAYER_NONE) { r.ok = false; return; }
        float sx = g_player.centreX() + PLAYER_W + 8.0f, sy = g_player.centreY();
        /* Prefer beside the host, but never knowingly create the joining body
           inside the wall the host happens to be mining against. */
        if (solidBox(world, (int)(sx - PLAYER_W * 0.5f),
                           (int)(sy - PLAYER_H * 0.5f), PLAYER_W, PLAYER_H)) {
            static const int DX[8] = { 1, -1, 0, 0, 1, -1, 1, -1 };
            static const int DY[8] = { 0, 0, -1, 1, -1, -1, 1, 1 };
            bool found = false;
            for (int radius = 16; radius <= 128 && !found; radius += 16)
                for (int k = 0; k < 8; ++k) {
                    const float tx = g_player.centreX() + DX[k] * radius;
                    const float ty = g_player.centreY() + DY[k] * radius;
                    if (solidBox(world, (int)(tx - PLAYER_W * 0.5f),
                                       (int)(ty - PLAYER_H * 0.5f), PLAYER_W, PLAYER_H)) continue;
                    sx = tx; sy = ty; found = true; break;
                }
        }
        /* Two players joining seconds apart would otherwise be placed on the
           same clear cell beside the host and start the session overlapping. */
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            const PlayerSession& other = g_playerSessions[i];
            if (!other.connected) continue;
            if (fabsf(other.body.centreX() - sx) < PLAYER_W &&
                fabsf(other.body.centreY() - sy) < PLAYER_H) { sx += PLAYER_W + 8.0f; i = -1; }
        }
        peer.assigned = playerSessionOpen(false, sx, sy);
        if (peer.assigned != PLAYER_NONE) {
            strncpy(peer.identity, who, PLAYER_IDENTITY_CHARS);
            peer.identity[PLAYER_IDENTITY_CHARS] = 0;
            /* playerSessionOpen has already handed out a starting kit. If
               this is somebody coming back, that kit is replaced by what
               they actually left with -- rosterRestore returns the borrowed
               tool instances before overwriting anything. */
            rosterRestore(peer.identity, g_playerSessions[peer.assigned]);
        }
        if (peer.assigned == PLAYER_NONE) { Writer reject; reject.string("Game is full"); queuePacket(peer, PK_REJECT, reject.b); return; }
        PlayerSession& joined = g_playerSessions[peer.assigned];
        joined.inventory.add(ITEM_BOLTER, 1); joined.inventory.add(ITEM_FLINT, 1);
        Writer welcome; welcome.u8v(peer.assigned); welcome.u16v(joined.generation);
        queuePacket(peer, PK_WELCOME, welcome.b); sendSnapshot(peer, world);
    } else if (type == PK_REJECT && g_role == NET_CLIENT) {
        char reason[128]; r.string(reason, sizeof(reason)); statusf("Join rejected: %s", reason); closeSocket(peer.sock);
    } else if (type == PK_WELCOME && g_role == NET_CLIENT) {
        g_assigned = r.u8v(); (void)r.u16v(); statusf("Accepted -- receiving world");
    } else if (type == PK_WORLD_SNAPSHOT && g_role == NET_CLIENT) {
        const char* path = CLIENT_SNAPSHOT_SCRATCH;
        if (!ensureSnapshotDirectory()) { statusf("Could not create snapshot directory"); return; }
        if (!writeWholeFile(path, data, len) || !saveRead(path, world)) {
            remove(path); statusf("Could not load host world snapshot"); return;
        }
        memset(g_clientAuthorityHash, 0, sizeof(g_clientAuthorityHash));
        memset(g_clientPredictedChunkCommand, 0, sizeof(g_clientPredictedChunkCommand));
        remove(path); Writer ready; queuePacket(peer, PK_READY, ready.b);
        lightInvalidate(); statusf("World received -- syncing players");
    } else if (type == PK_READY && g_role == NET_HOST) {
        if (peer.assigned == PLAYER_NONE) { r.ok = false; return; }
        peer.ready = true; memset(peer.chunkHash, 0, sizeof(peer.chunkHash));
        sprintf(g_status, "Player joined -- %d connected", connectedPeerCount());
        sendState(); sendChangedChunks(peer, world);
    } else if (type == PK_COMMAND && g_role == NET_HOST) {
        PlayerCommand c;
        c.sequence = r.u32v(); c.player = r.u8v(); c.generation = r.u16v();
        c.bits = r.u8v(); c.pressed = r.u8v(); c.selected = r.u8v(); c.brushRadius = r.u8v();
        c.brush = (i16)r.u16v();
        c.background = r.u8v() != 0; c.overwrite = r.u8v() != 0; c.line = r.u8v() != 0;
        c.lineCommit = r.u8v() != 0; c.lineCommitBits = r.u8v();
        c.lineStartX = r.i32v(); c.lineStartY = r.i32v();
        c.digFilterOn = r.u8v() != 0; r.bytes(c.digFilter, sizeof(c.digFilter));
        c.aimX = r.i32v(); c.aimY = r.i32v();
        /* A connection may only drive the player it was given. Trusting the id
           in the packet would let any client move, mine and spend the inventory
           of everybody else in the game. */
        const int slot = c.player == peer.assigned ? playerSessionSlotForNetworkId(c.player) : -1;
        if (r.ok && slot >= 0 && slot < MAX_PLAYERS &&
            g_playerSessions[slot].generation == c.generation &&
            (!g_haveRemoteCommand[slot] || c.sequence > g_remoteCommand[slot].sequence)) {
            if (g_haveRemoteCommand[slot]) c.pressed |= g_remoteCommand[slot].pressed;
            g_remoteCommand[slot] = c; g_haveRemoteCommand[slot] = true;
        }
    } else if (type == PK_STATE && g_role == NET_CLIENT) {
        const u32 acknowledgedCommand = r.u32v();
        const u32 acknowledgedAction = r.u32v();
        std::vector<u8> raw;
        if (r.ok && unpackBytes(r, raw)) {
            Reader state(raw.empty() ? 0 : &raw[0], raw.size());
            applyState(state);
            if (!state.ok || state.at != state.n) r.ok = false;
        }
        if (r.ok) {
            g_acknowledgedCommand = acknowledgedCommand;
            g_acknowledgedAction = acknowledgedAction;
            if (!g_clientReady) rememberClientInterestHashes(world);
            ++g_stateSerial; g_clientReady = true; statusf("Joined host");
        }
    } else if (type == PK_CHUNK && g_role == NET_CLIENT) {
        applyChunk(world, r); if (r.ok) lightInvalidate();
    } else if (type == PK_ACTION && g_role == NET_HOST) {
        NetAction a;
        a.sequence = r.u32v(); a.player = r.u8v(); a.generation = r.u16v();
        a.type = r.u8v(); a.container = r.u8v(); a.a = r.u8v(); a.b = r.u8v(); a.flags = r.u8v();
        a.x = r.i32v(); a.y = r.i32v();
        const int slot = a.player == peer.assigned ? playerSessionSlotForNetworkId(a.player) : -1;
        if (r.ok && slot >= 1 && g_playerSessions[slot].generation == a.generation &&
            g_remoteActions.size() < 256) g_remoteActions.push_back(a);
    } else if (type == PK_CHUNK_HASHES && g_role == NET_HOST && peer.ready) {
        const int slot = playerSessionSlotForNetworkId(peer.assigned);
        const int count = r.u8v();
        if (slot < 1 || count > 16) { r.ok = false; }
        for (int n = 0; r.ok && n < count; ++n) {
            const int cx = r.u16v(), cy = r.u16v(); const u32 clientHash = r.u32v();
            if (cx < 0 || cx >= CHUNKS_X || cy < 0 || cy >= CHUNKS_Y) { r.ok = false; break; }
            const Player& p = g_playerSessions[slot].body;
            const int centreX = (cx << CHUNK_SHIFT) + CHUNK / 2;
            const int centreY = (cy << CHUNK_SHIFT) + CHUNK / 2;
            /* Only accept reports from the joined player's replicated island.
               A malformed or modified client therefore cannot make the host
               stream arbitrary parts of the world indefinitely. */
            if (abs(centreX - (int)p.centreX()) > 512 + CHUNK ||
                abs(centreY - (int)p.centreY()) > 448 + CHUNK) continue;
            const int ci = cy * CHUNKS_X + cx;
            const u32 authorityHash = hashChunk(world, cx, cy);
            peer.chunkHash[ci] = authorityHash;
            if (clientHash != authorityHash && backlog(peer) < 1024u * 1024u)
                sendChunk(peer, world, cx, cy);
        }
    }
    if (!r.ok) {
        sprintf(g_status, "Malformed packet %u at %u/%u", (unsigned)type,
                (unsigned)r.at, (unsigned)r.n);
    }
}

static void pumpReceive(Peer& peer, World& world) {
    u8 temp[64 * 1024];
    size_t received = 0;
    /* A single recv per rendered frame imposed an artificial 3.75 MiB/s cap.
       Snapshot traffic could therefore take seconds to drain on a perfectly
       healthy LAN. Drain a bounded batch while the nonblocking socket has it. */
    while (received < 512u * 1024u) {
        const int n = txRecv(peer, temp, (int)sizeof(temp));
        if (n > 0) { peer.recv.insert(peer.recv.end(), temp, temp + n); received += (size_t)n; continue; }
        if (n == -1) { disconnectPeer(peer, "Host disconnected"); return; }
        if (n == -2) disconnectPeer(peer, "Network receive failed");
        break;
    }
    size_t used = 0;
    while (peer.live() && peer.recv.size() - used >= 5) {
        const u8* p = &peer.recv[used];
        const u32 len = (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
        if (len < 1 || len > NET_MAX_PACKET) {
            statusf("Invalid network packet size"); disconnectPeer(peer, "Invalid network packet size"); return;
        }
        if (peer.recv.size() - used < (size_t)len + 4) break;
        handlePacket(peer, p[4], p + 5, len - 1, world); used += len + 4;
    }
    /* A rejected or failed peer has already had its buffers cleared; erasing a
       range from the fresh empty buffer would be an out-of-bounds iterator. */
    if (used && peer.live() && used <= peer.recv.size())
        peer.recv.erase(peer.recv.begin(), peer.recv.begin() + (long)used);
}

static void pumpSend(Peer& peer) {
    size_t sent = 0;
    while (peer.live() && peer.sendAt < peer.send.size() && sent < 512u * 1024u) {
        const size_t left = peer.send.size() - peer.sendAt;
        const int ask = (int)(left > 64 * 1024 ? 64 * 1024 : left);
        const int n = txSend(peer, &peer.send[peer.sendAt], ask);
        if (n > 0) { peer.sendAt += (size_t)n; sent += (size_t)n; }
        else {
            if (n < 0) disconnectPeer(peer, "Network send failed");
            break;
        }
    }
    if (peer.live() && peer.sendAt == peer.send.size()) { peer.send.clear(); peer.sendAt = 0; }
}

static void pollPeer(Peer& peer, World& world) {
    if (g_role == NET_CLIENT && peer.connecting) {
        bool failed = false;
        if (txConnectPoll(peer, &failed)) {
            if (failed) { statusf("Connection failed"); closeSocket(peer.sock); }
            else { peer.connecting = false; queueHello(peer); }
        }
        return;
    }
    if (g_role == NET_CLIENT && !peer.handshake) queueHello(peer);
    bool readable = false, writable = false;
    if (!txReady(peer, &readable, &writable)) { disconnectPeer(peer, "Network poll failed"); return; }
    /* Sending first guarantees a just-connected client can deliver HELLO
       before either side waits for an answer. One bounded operation per
       readiness result also makes an accidental blocking socket harmless. */
    if (writable) pumpSend(peer);
    if (peer.live() && readable) pumpReceive(peer, world);
}

void netPoll(World& world) {
#ifndef _WIN32
    /* No accept queue: there is one possible guest and they arrive by the
       channel opening. Adopting the peer here rather than at netHost time is
       what keeps the rest of the file honest -- a peer that is `live()` is
       one the transport can actually carry bytes for. */
    if (g_role == NET_HOST && !g_peers[0].live() && webNetOpen()) {
        g_peers[0].clear();
        g_peers[0].sock = WEB_PEER_SOCKET;
        statusf("Player connecting");
    }
#else
    if (g_role == NET_HOST && g_listen != INVALID_SOCKET) {
        /* Accept in a loop: several people can click Join within one frame,
           and an unaccepted backlog entry would otherwise wait a whole frame
           each. A socket arriving with no free slot is told the game is full
           rather than left hanging on an unanswered handshake. */
        for (;;) {
            SOCKET s = accept(g_listen, 0, 0);
            if (s == INVALID_SOCKET) break;
            if (!nonblocking(s)) { closesocket(s); statusf("Could not configure joined socket"); break; }
            preferLowLatency(s);
            int free = -1;
            for (int i = 0; i < MAX_PEERS && free < 0; ++i) if (!g_peers[i].live()) free = i;
            if (free < 0) { closesocket(s); statusf("Refused a join -- game is full"); break; }
            g_peers[free].clear(); g_peers[free].sock = s;
            sprintf(g_status, "Player connecting -- %d connected", connectedPeerCount());
        }
    }
#endif
    for (int i = 0; i < MAX_PEERS; ++i) if (g_peers[i].live()) pollPeer(g_peers[i], world);
}

void netHostFrame(World& world) {
    if (g_role != NET_HOST) return;
    bool anyReady = false;
    for (int i = 0; i < MAX_PEERS; ++i) if (g_peers[i].live() && g_peers[i].ready) anyReady = true;
    if (!anyReady) return;

    /* State is one shared serialization, so it is produced once per host frame
       rather than once per peer. Chunk traffic stays per peer because each
       player has a different interest rectangle and a different replica. */
    static int stateFrame = 0;
    ++stateFrame;
    bool stateBudget = true, scanBudget = true;
    for (int i = 0; i < MAX_PEERS; ++i) {
        const Peer& peer = g_peers[i];
        if (!peer.live() || !peer.ready) continue;
        /* Never manufacture seconds of stale authoritative state. A peer whose
           queue is already deep holds off the shared state packet, since
           sending it only to the others would desynchronize acknowledgement. */
        if (backlog(peer) >= 256u * 1024u) stateBudget = false;
        if (backlog(peer) >= 1024u * 1024u) scanBudget = false;
    }
    if (stateFrame % 6 == 0 && stateBudget) sendState();

    for (int i = 0; i < MAX_PEERS; ++i) {
        Peer& peer = g_peers[i];
        if (!peer.live() || !peer.ready) continue;
        ++peer.frame;
        int urgentBudget = 8;
        for (int n = 0; n < CHUNK_COUNT && urgentBudget > 0; ++n) {
            const int ci = (peer.urgentCursor + n) % CHUNK_COUNT;
            if (!peer.urgentChunk[ci]) continue;
            if (backlog(peer) >= 1024u * 1024u) break;
            peer.urgentChunk[ci] = 0;
            const int cx = ci % CHUNKS_X, cy = ci / CHUNKS_X;
            peer.chunkHash[ci] = hashChunk(world, cx, cy);
            sendChunk(peer, world, cx, cy); --urgentBudget;
            peer.urgentCursor = (ci + 1) % CHUNK_COUNT;
        }
        if (peer.frame % 12 == 0 && scanBudget) sendChangedChunks(peer, world);
        /* TCP prevents packet loss, but a periodic forced refresh also repairs
           a client-side bad write or future decoder bug without rejoining.
           Spread by the scan budget, so this is not a one-frame spike. */
        if (peer.frame % 3600 == 0) memset(peer.chunkHash, 0, sizeof(peer.chunkHash));
    }
}

void netMarkWorldEdit(int x, int y, int radius) {
    if (g_role != NET_HOST) return;
    const int x0 = imax(0, x - radius), x1 = imin(SIM_W - 1, x + radius);
    const int y0 = imax(0, y - radius), y1 = imin(SIM_H - 1, y + radius);
    for (int i = 0; i < MAX_PEERS; ++i) {
        Peer& peer = g_peers[i];
        if (!peer.live() || !peer.ready) continue;
        for (int cy = y0 >> CHUNK_SHIFT; cy <= (y1 >> CHUNK_SHIFT); ++cy)
            for (int cx = x0 >> CHUNK_SHIFT; cx <= (x1 >> CHUNK_SHIFT); ++cx)
                peer.urgentChunk[cy * CHUNKS_X + cx] = 1;
    }
}

void netMarkPredictedWorldEdit(int x0, int y0, int x1, int y1, int radius,
                               u32 commandSequence) {
    if (g_role != NET_CLIENT || !g_clientReady || !commandSequence) return;
    if (x0 > x1) { const int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { const int t = y0; y0 = y1; y1 = t; }
    x0 = imax(0, x0 - radius); x1 = imin(SIM_W - 1, x1 + radius);
    y0 = imax(0, y0 - radius); y1 = imin(SIM_H - 1, y1 + radius);
    for (int cy = y0 >> CHUNK_SHIFT; cy <= (y1 >> CHUNK_SHIFT); ++cy)
        for (int cx = x0 >> CHUNK_SHIFT; cx <= (x1 >> CHUNK_SHIFT); ++cx) {
            u32& protectedThrough = g_clientPredictedChunkCommand[cy * CHUNKS_X + cx];
            if (!protectedThrough || (i32)(commandSequence - protectedThrough) > 0)
                protectedThrough = commandSequence;
        }
}

void netClientFrame(World& world) {
    /* Prediction fills the interval between corrections. Hash reports are a
       repair audit, not a request to replace live chunks every rendered frame. */
    static int reportFrame = 0;
    if (++reportFrame % 15 == 0) sendClientChunkHashes(world);
}

bool netSendCommand(const PlayerCommand& c) {
    if (g_role != NET_CLIENT || !g_clientReady || !g_peers[0].live()) return false;
    Writer w; w.u32v(c.sequence); w.u8v(c.player); w.u16v(c.generation);
    w.u8v(c.bits); w.u8v(c.pressed); w.u8v(c.selected); w.u8v(c.brushRadius);
    w.u16v((u16)c.brush);
    w.u8v(c.background ? 1 : 0); w.u8v(c.overwrite ? 1 : 0);
    w.u8v(c.line ? 1 : 0);
    w.u8v(c.lineCommit ? 1 : 0); w.u8v(c.lineCommitBits);
    w.i32v(c.lineStartX); w.i32v(c.lineStartY);
    w.u8v(c.digFilterOn ? 1 : 0); w.bytes(c.digFilter, sizeof(c.digFilter));
    w.i32v(c.aimX); w.i32v(c.aimY); queuePacket(g_peers[0], PK_COMMAND, w.b); return true;
}

static Peer* peerForPlayer(PlayerId player) {
    if (g_role != NET_HOST || player == PLAYER_NONE) return 0;
    for (int i = 0; i < MAX_PEERS; ++i)
        if (g_peers[i].live() && g_peers[i].assigned == player) return &g_peers[i];
    return 0;
}

bool netPopRemoteCommand(PlayerId player, PlayerCommand* command) {
    if (!command || player >= MAX_PLAYERS || !g_haveRemoteCommand[player]) return false;
    *command = g_remoteCommand[player]; g_haveRemoteCommand[player] = false; return true;
}

/* Acknowledgement is per connection. One shared watermark would tell every
   client that the host had consumed input it has never seen, and each of them
   would stop replaying its own genuinely pending movement. */
void netMarkRemoteCommandApplied(PlayerId player, u32 sequence) {
    Peer* peer = peerForPlayer(player);
    if (peer && sequence > peer->appliedCommand) peer->appliedCommand = sequence;
}

u32 netAcknowledgedCommand() { return g_acknowledgedCommand; }
void netMarkRemoteActionApplied(PlayerId player, u32 sequence) {
    Peer* peer = peerForPlayer(player);
    if (peer && sequence > peer->appliedAction) peer->appliedAction = sequence;
}
u32 netAcknowledgedAction() { return g_acknowledgedAction; }
u32 netStateSerial() { return g_stateSerial; }

bool netSendAction(const NetAction& a) {
    if (g_role != NET_CLIENT || !g_clientReady || !g_peers[0].live()) return false;
    Writer w; w.u32v(a.sequence); w.u8v(a.player); w.u16v(a.generation);
    w.u8v(a.type); w.u8v(a.container); w.u8v(a.a); w.u8v(a.b); w.u8v(a.flags);
    w.i32v(a.x); w.i32v(a.y);
    queuePacket(g_peers[0], PK_ACTION, w.b); return true;
}

bool netPopRemoteAction(NetAction* action) {
    if (!action || g_remoteActions.empty()) return false;
    *action = g_remoteActions.front(); g_remoteActions.pop_front(); return true;
}

NetRole netRole() { return g_role; }
/* Unchanged meaning: at least one live socket. Deliberately NOT true for a
   host that is merely listening, because callers use this to decide whether
   tearing down the session is necessary before replacing the world. */
bool netConnected() { return connectedPeerCount() > 0; }
int netPeerCount() { return connectedPeerCount(); }
bool netReady() {
    if (g_role != NET_HOST) return g_clientReady;
    for (int i = 0; i < MAX_PEERS; ++i) if (g_peers[i].live() && g_peers[i].ready) return true;
    return false;
}
bool netClientReady() { return g_role != NET_CLIENT || g_clientReady; }
PlayerId netAssignedPlayer() { return g_assigned; }
const char* netStatus() { return g_status; }
const char* netLocalAddress() { startup(); return g_localAddress; }
