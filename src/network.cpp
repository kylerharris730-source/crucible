#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "network.h"
#include "save.h"
#include "entity.h"
#include "projectile.h"
#include "device.h"
#include "light.h"
#include "drone.h"
#include "room.h"
#include "tree.h"
#include <vector>
#include <deque>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CRUCIBLE_BUILD_ID
#ifdef CRUCIBLE_TEST_MISMATCH_BUILD
#define CRUCIBLE_BUILD_ID "intentional-mismatch-test-build"
#else
#define CRUCIBLE_BUILD_ID "development"
#endif
#endif

static const u32 NET_MAGIC = 0x54454E43u; /* CNET on little endian */
static const u32 NET_PROTOCOL = 7;
static const u32 NET_STATE_SCHEMA = 6;
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

static NetRole g_role = NET_OFF;
static SOCKET g_listen = INVALID_SOCKET, g_peer = INVALID_SOCKET;
static bool g_wsa = false, g_connecting = false, g_handshake = false;
static bool g_ready = false;
static PlayerId g_assigned = PLAYER_NONE;
static char g_status[192] = "Offline";
static char g_localAddress[64] = "127.0.0.1";
static std::vector<u8> g_recv, g_send;
static size_t g_sendAt = 0;
static PlayerCommand g_remoteCommand;
static bool g_haveRemoteCommand = false;
static std::deque<NetAction> g_remoteActions;
static u32 g_chunkHash[CHUNK_COUNT];
static int g_netFrame = 0;

static void packBytes(Writer& out, const u8* src, size_t n);
static bool unpackBytes(Reader& in, std::vector<u8>& out);

static void statusf(const char* fmt, const char* arg = 0) {
    if (arg) sprintf(g_status, fmt, arg); else strcpy(g_status, fmt);
}

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

static void closeSocket(SOCKET& s) {
    if (s != INVALID_SOCKET) { closesocket(s); s = INVALID_SOCKET; }
}

static void disconnectPeer(const char* reason) {
    closeSocket(g_peer);
    if (g_role == NET_HOST && g_assigned != PLAYER_NONE) {
        playerSessionClose(g_assigned);
    }
    g_assigned = PLAYER_NONE; g_ready = g_handshake = g_connecting = false;
    g_recv.clear(); g_send.clear(); g_sendAt = 0;
    g_haveRemoteCommand = false; g_remoteActions.clear();
    if (g_role == NET_HOST) statusf("Player left -- waiting for player");
    else statusf(reason ? reason : "Disconnected from host");
}

void netStop() {
    const bool wasClient = g_role == NET_CLIENT;
    closeSocket(g_peer); closeSocket(g_listen);
    g_recv.clear(); g_send.clear(); g_sendAt = 0;
    g_connecting = g_handshake = g_ready = false;
    g_assigned = PLAYER_NONE; g_haveRemoteCommand = false;
    g_remoteActions.clear();
    if (g_role == NET_HOST)
        for (int i = 1; i < MAX_PLAYERS; ++i) {
            playerSessionClose((PlayerId)i);
        }
    if (wasClient) playerSessionsReturnToOffline();
    g_role = NET_OFF; statusf("Offline");
}

bool netHost(u16 port) {
    netStop(); if (!startup()) return false;
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen == INVALID_SOCKET) { statusf("Could not create host socket"); return false; }
    BOOL reuse = TRUE; setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    sockaddr_in a; memset(&a, 0, sizeof(a)); a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons(port);
    if (bind(g_listen, (sockaddr*)&a, sizeof(a)) == SOCKET_ERROR || listen(g_listen, 1) == SOCKET_ERROR) {
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
    g_peer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_peer == INVALID_SOCKET) { statusf("Could not create client socket"); return false; }
    if (!nonblocking(g_peer)) {
        closeSocket(g_peer); statusf("Could not configure client socket"); return false;
    }
    sockaddr_in a; memset(&a, 0, sizeof(a)); a.sin_family = AF_INET;
    a.sin_addr.s_addr = inet_addr(ipv4); a.sin_port = htons(port);
    if (a.sin_addr.s_addr == INADDR_NONE) { closeSocket(g_peer); statusf("Enter a numeric IPv4 address"); return false; }
    const int r = connect(g_peer, (sockaddr*)&a, sizeof(a));
    if (r == SOCKET_ERROR) {
        const int e = WSAGetLastError();
        if (e != WSAEWOULDBLOCK && e != WSAEINPROGRESS) {
            closeSocket(g_peer); statusf("Could not start connection"); return false;
        }
    }
    g_role = NET_CLIENT; g_connecting = r == SOCKET_ERROR;
    sprintf(g_status, "Connecting to %s:%u", ipv4, (unsigned)port);
    return true;
}

static void queuePacket(u8 type, const std::vector<u8>& payload) {
    const u32 len = (u32)payload.size() + 1;
    for (int i = 0; i < 4; ++i) g_send.push_back((u8)(len >> (i * 8)));
    g_send.push_back(type);
    g_send.insert(g_send.end(), payload.begin(), payload.end());
}

static void queueHello() {
    Writer w; w.u32v(NET_MAGIC); w.u32v(NET_PROTOCOL); w.string(CRUCIBLE_BUILD_ID);
    queuePacket(PK_HELLO, w.b); g_handshake = true;
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

static void sendSnapshot(World& world) {
    const char* path = "build\\net-host-snapshot.tmp";
    if (!saveWrite(path, world)) { statusf("Could not make join snapshot"); return; }
    std::vector<u8> bytes;
    if (!readWholeFile(path, bytes)) { remove(path); statusf("Could not read join snapshot"); return; }
    remove(path); queuePacket(PK_WORLD_SNAPSHOT, bytes);
    statusf("Sending world snapshot");
}

static void sendState() {
    Writer w;
    w.u32v(NET_STATE_SCHEMA);
    w.u32v((u32)sizeof(Player)); w.u32v((u32)sizeof(Inventory));
    w.u32v((u32)sizeof(Entity)); w.u32v((u32)sizeof(Pickup));
    w.u32v((u32)sizeof(Projectile)); w.u32v((u32)sizeof(Device));
    w.u32v((u32)sizeof(Drone));
    u8 count = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i) if (g_playerSessions[i].connected) ++count;
    w.u8v(count);
    for (int i = 0; i < MAX_PLAYERS; ++i) if (g_playerSessions[i].connected) {
        const PlayerSession& s = g_playerSessions[i];
        w.u8v(s.networkId); w.u16v(s.generation);
        w.bytes(&s.body, sizeof(s.body)); w.bytes(&s.inventory, sizeof(s.inventory));
        w.bytes(&s.cursor, sizeof(s.cursor)); w.bytes(&s.trash, sizeof(s.trash));
        w.i32v(s.restBed); w.i32v(s.openDevice);
        w.bytes(s.drones, sizeof(s.drones));
    }
    w.bytes(g_toolInst, sizeof(g_toolInst));
    w.bytes(g_entities, sizeof(g_entities)); w.bytes(g_pickups, sizeof(g_pickups));
    w.bytes(g_proj, sizeof(g_proj)); w.bytes(g_devices, sizeof(g_devices));
    w.bytes(g_sparks, sizeof(g_sparks)); w.bytes(g_shed, sizeof(g_shed));
    w.bytes(g_circuitConfig, sizeof(g_circuitConfig));
    w.bytes(g_circuitWires, sizeof(g_circuitWires));
    w.bytes(g_rooms, sizeof(g_rooms)); w.bytes(g_trees, sizeof(g_trees));
    const int torchN = torchCount(); w.u32v((u32)torchN);
    if (torchN > 0) w.bytes(torchData(), sizeof(TorchFixture) * (size_t)torchN);
    w.u32v(g_worldTime); w.u32v(g_bossesBeaten);
    Writer packed;
    packBytes(packed, w.b.empty() ? 0 : &w.b[0], w.b.size());
    queuePacket(PK_STATE, packed.b);
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
                if (next >= 3) break;
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
                if (next >= 3) break;
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

static void sendChunk(const World& world, int cx, int cy) {
    static u8 plane[CHUNK * CHUNK];
    Writer w; w.u16v((u16)cx); w.u16v((u16)cy);
    const int x0 = cx << CHUNK_SHIFT, y0 = cy << CHUNK_SHIFT;
    for (int field = 0; field < 5; ++field) {
        int p = 0;
        for (int y = y0; y < y0 + CHUNK; ++y) for (int x = x0; x < x0 + CHUNK; ++x) {
            const int i = y * SIM_W + x; const Cell& c = world.cells[i];
            plane[p++] = field == 0 ? c.mat : field == 1 ? c.moisture : field == 2 ? c.tint
                       : field == 3 ? world.temp[i] : world.bg[i];
        }
        packPlane(w, plane, CHUNK * CHUNK);
    }
    queuePacket(PK_CHUNK, w.b);
}

static void sendChangedChunks(const World& world) {
    const int slot = playerSessionSlotForNetworkId(g_assigned);
    if (slot < 0) return;
    const Player& p = g_playerSessions[slot].body;
    const int x0 = imax(0, (int)p.centreX() - 512), x1 = imin(SIM_W - 1, (int)p.centreX() + 512);
    const int y0 = imax(0, (int)p.centreY() - 448), y1 = imin(SIM_H - 1, (int)p.centreY() + 448);
    const int cx0 = x0 >> CHUNK_SHIFT, cx1 = x1 >> CHUNK_SHIFT;
    const int cy0 = y0 >> CHUNK_SHIFT, cy1 = y1 >> CHUNK_SHIFT;
    const int width = cx1 - cx0 + 1, height = cy1 - cy0 + 1;
    const int total = width * height;
    static int scanCursor = 0;
    /* Hashing the entire interest rectangle fifteen times a second was tens of
       millions of byte operations even in a settled cave. A rotating budget
       covers the full view-plus-margin in about a second, while command/player
       state keeps its low latency. */
    const int budget = imin(48, total);
    for (int n = 0; n < budget; ++n) {
        const int at = (scanCursor + n) % total;
        const int cx = cx0 + at % width, cy = cy0 + at / width;
        const int ci = cy * CHUNKS_X + cx; const u32 h = hashChunk(world, cx, cy);
        if (g_chunkHash[ci] == h) continue;
        g_chunkHash[ci] = h; sendChunk(world, cx, cy);
    }
    scanCursor = (scanCursor + budget) % total;
}

/* A client reports a rotating sample of the chunks it actually has. TCP makes
   loss unlikely, but it does not prove the replica is correct: a decoder bug,
   stale write, or future change to interest management can still leave one
   machine looking at different cells forever. These hashes turn chunk repair
   into a closed loop. The host compares against authoritative state and sends
   the ordinary full chunk packet on any mismatch. */
static void sendClientChunkHashes(const World& world) {
    if (g_role != NET_CLIENT || !g_ready || g_peer == INVALID_SOCKET ||
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
        w.u16v((u16)cx); w.u16v((u16)cy); w.u32v(hashChunk(world, cx, cy));
    }
    cursor = (cursor + count) % total;
    queuePacket(PK_CHUNK_HASHES, w.b);
}

static void applyState(Reader& r) {
    const u32 schema = r.u32v();
    const u32 playerSize = r.u32v(), invSize = r.u32v(), entitySize = r.u32v();
    const u32 pickupSize = r.u32v(), projSize = r.u32v(), deviceSize = r.u32v();
    const u32 droneSize = r.u32v();
    if (schema != NET_STATE_SCHEMA || playerSize != sizeof(Player) || invSize != sizeof(Inventory) ||
        entitySize != sizeof(Entity) || pickupSize != sizeof(Pickup) ||
        projSize != sizeof(Projectile) || deviceSize != sizeof(Device) ||
        droneSize != sizeof(Drone)) { r.ok = false; return; }
    playerSessionsReset();
    for (int i = 0; i < MAX_PLAYERS; ++i) g_playerSessions[i].connected = false;
    const int count = r.u8v(); int nextRemoteSlot = 1;
    for (int n = 0; n < count; ++n) {
        const PlayerId wire = r.u8v(); const u16 generation = r.u16v();
        int slot = wire == g_assigned ? 0 : nextRemoteSlot++;
        if (slot >= MAX_PLAYERS) { r.ok = false; return; }
        PlayerSession& s = g_playerSessions[slot];
        r.bytes(&s.body, sizeof(s.body)); r.bytes(&s.inventory, sizeof(s.inventory));
        r.bytes(&s.cursor, sizeof(s.cursor)); r.bytes(&s.trash, sizeof(s.trash));
        s.restBed = r.i32v(); s.openDevice = r.i32v();
        r.bytes(s.drones, sizeof(s.drones));
        s.connected = true; s.local = slot == 0; s.networkId = wire; s.generation = generation;
    }
    r.bytes(g_toolInst, sizeof(g_toolInst));
    r.bytes(g_entities, sizeof(g_entities)); r.bytes(g_pickups, sizeof(g_pickups));
    r.bytes(g_proj, sizeof(g_proj)); r.bytes(g_devices, sizeof(g_devices));
    r.bytes(g_sparks, sizeof(g_sparks)); r.bytes(g_shed, sizeof(g_shed));
    r.bytes(g_circuitConfig, sizeof(g_circuitConfig));
    r.bytes(g_circuitWires, sizeof(g_circuitWires));
    r.bytes(g_rooms, sizeof(g_rooms)); r.bytes(g_trees, sizeof(g_trees));
    const u32 torchN = r.u32v();
    if (torchN > 1000000u || (size_t)torchN > (r.n - r.at) / sizeof(TorchFixture)) {
        r.ok = false;
    } else {
        std::vector<TorchFixture> fixtures(torchN);
        if (torchN) r.bytes(&fixtures[0], sizeof(TorchFixture) * (size_t)torchN);
        if (r.ok) torchLoad(fixtures.empty() ? 0 : &fixtures[0], (int)fixtures.size());
    }
    g_worldTime = r.u32v() % DAY_LENGTH; g_bossesBeaten = r.u32v();
}

static void applyChunk(World& world, Reader& r) {
    const int cx = r.u16v(), cy = r.u16v();
    if (cx < 0 || cx >= CHUNKS_X || cy < 0 || cy >= CHUNKS_Y) { r.ok = false; return; }
    static u8 plane[5][CHUNK * CHUNK];
    for (int field = 0; field < 5; ++field) if (!unpackPlane(r, plane[field], CHUNK * CHUNK)) return;
    const int x0 = cx << CHUNK_SHIFT, y0 = cy << CHUNK_SHIFT; int p = 0;
    for (int y = y0; y < y0 + CHUNK; ++y) for (int x = x0; x < x0 + CHUNK; ++x, ++p) {
        const int i = y * SIM_W + x; Cell& c = world.cells[i];
        c.mat = plane[0][p]; c.moisture = plane[1][p]; c.tint = plane[2][p]; c.flags = 0;
        world.temp[i] = plane[3][p]; world.bg[i] = plane[4][p];
    }
}

static void handlePacket(u8 type, const u8* data, size_t len, World& world) {
    Reader r(data, len);
    if (type == PK_HELLO && g_role == NET_HOST) {
        char build[80]; const u32 magic = r.u32v(), protocol = r.u32v(); r.string(build, sizeof(build));
        if (!r.ok || magic != NET_MAGIC || protocol != NET_PROTOCOL || strcmp(build, CRUCIBLE_BUILD_ID) != 0) {
            Writer reject; reject.string("Game builds do not match"); queuePacket(PK_REJECT, reject.b); return;
        }
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
        g_assigned = playerSessionOpen(false, sx, sy);
        if (g_assigned == PLAYER_NONE) { Writer reject; reject.string("Game is full"); queuePacket(PK_REJECT, reject.b); return; }
        PlayerSession& joined = g_playerSessions[g_assigned];
        joined.inventory.add(ITEM_BOLTER, 1); joined.inventory.add(ITEM_FLINT, 1);
        Writer welcome; welcome.u8v(g_assigned); welcome.u16v(joined.generation);
        queuePacket(PK_WELCOME, welcome.b); sendSnapshot(world);
    } else if (type == PK_REJECT && g_role == NET_CLIENT) {
        char reason[128]; r.string(reason, sizeof(reason)); statusf("Join rejected: %s", reason); closeSocket(g_peer);
    } else if (type == PK_WELCOME && g_role == NET_CLIENT) {
        g_assigned = r.u8v(); (void)r.u16v(); statusf("Accepted -- receiving world");
    } else if (type == PK_WORLD_SNAPSHOT && g_role == NET_CLIENT) {
        const char* path = "build\\net-client-snapshot.tmp";
        if (!writeWholeFile(path, data, len) || !saveRead(path, world)) {
            remove(path); statusf("Could not load host world snapshot"); return;
        }
        remove(path); Writer ready; queuePacket(PK_READY, ready.b);
        lightInvalidate(); statusf("World received -- syncing players");
    } else if (type == PK_READY && g_role == NET_HOST) {
        g_ready = true; memset(g_chunkHash, 0, sizeof(g_chunkHash));
        statusf("Player joined"); sendState(); sendChangedChunks(world);
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
        const int slot = playerSessionSlotForNetworkId(c.player);
        if (r.ok && slot >= 0 && g_playerSessions[slot].generation == c.generation &&
            (!g_haveRemoteCommand || c.sequence > g_remoteCommand.sequence)) {
            if (g_haveRemoteCommand) c.pressed |= g_remoteCommand.pressed;
            g_remoteCommand = c; g_haveRemoteCommand = true;
        }
    } else if (type == PK_STATE && g_role == NET_CLIENT) {
        std::vector<u8> raw;
        if (unpackBytes(r, raw)) {
            Reader state(raw.empty() ? 0 : &raw[0], raw.size());
            applyState(state);
            if (!state.ok || state.at != state.n) r.ok = false;
        }
        if (r.ok) { g_ready = true; statusf("Joined host"); }
    } else if (type == PK_CHUNK && g_role == NET_CLIENT) {
        applyChunk(world, r); if (r.ok) lightInvalidate();
    } else if (type == PK_ACTION && g_role == NET_HOST) {
        NetAction a;
        a.sequence = r.u32v(); a.player = r.u8v(); a.generation = r.u16v();
        a.type = r.u8v(); a.container = r.u8v(); a.a = r.u8v(); a.b = r.u8v(); a.flags = r.u8v();
        a.x = r.i32v(); a.y = r.i32v();
        const int slot = playerSessionSlotForNetworkId(a.player);
        if (r.ok && slot >= 1 && g_playerSessions[slot].generation == a.generation &&
            g_remoteActions.size() < 256) g_remoteActions.push_back(a);
    } else if (type == PK_CHUNK_HASHES && g_role == NET_HOST && g_ready) {
        const int slot = playerSessionSlotForNetworkId(g_assigned);
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
            g_chunkHash[ci] = authorityHash;
            if (clientHash != authorityHash && g_send.size() - g_sendAt < 8u * 1024u * 1024u)
                sendChunk(world, cx, cy);
        }
    }
    if (!r.ok) statusf("Malformed network packet");
}

static void pumpReceive(World& world) {
    u8 temp[64 * 1024];
    const int n = recv(g_peer, (char*)temp, sizeof(temp), 0);
    if (n > 0) g_recv.insert(g_recv.end(), temp, temp + n);
    else if (n == 0) { disconnectPeer("Host disconnected"); return; }
    else {
        const int e = WSAGetLastError();
        if (e != WSAEWOULDBLOCK) disconnectPeer("Network receive failed");
        return;
    }
    size_t used = 0;
    while (g_recv.size() - used >= 5) {
        const u8* p = &g_recv[used];
        const u32 len = (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
        if (len < 1 || len > NET_MAX_PACKET) { statusf("Invalid network packet size"); closeSocket(g_peer); break; }
        if (g_recv.size() - used < (size_t)len + 4) break;
        handlePacket(p[4], p + 5, len - 1, world); used += len + 4;
    }
    if (used) g_recv.erase(g_recv.begin(), g_recv.begin() + used);
}

static void pumpSend() {
    if (g_peer != INVALID_SOCKET && g_sendAt < g_send.size()) {
        const size_t left = g_send.size() - g_sendAt;
        const int ask = (int)(left > 64 * 1024 ? 64 * 1024 : left);
        const int n = send(g_peer, (const char*)&g_send[g_sendAt], ask, 0);
        if (n > 0) g_sendAt += n;
        else {
            const int e = WSAGetLastError();
            if (e != WSAEWOULDBLOCK) disconnectPeer("Network send failed");
        }
    }
    if (g_sendAt == g_send.size()) { g_send.clear(); g_sendAt = 0; }
}

void netPoll(World& world) {
    if (g_role == NET_HOST && g_peer == INVALID_SOCKET && g_listen != INVALID_SOCKET) {
        SOCKET s = accept(g_listen, 0, 0);
        if (s != INVALID_SOCKET) {
            if (!nonblocking(s)) {
                closesocket(s); statusf("Could not configure joined socket"); return;
            }
            g_peer = s; g_recv.clear(); g_send.clear(); g_sendAt = 0;
            statusf("Player connected -- waiting for handshake");
        }
    }
    if (g_role == NET_CLIENT && g_connecting && g_peer != INVALID_SOCKET) {
        fd_set writeSet, errSet; FD_ZERO(&writeSet); FD_ZERO(&errSet);
        FD_SET(g_peer, &writeSet); FD_SET(g_peer, &errSet); timeval tv = { 0, 0 };
        const int n = select(0, 0, &writeSet, &errSet, &tv);
        if (n > 0) {
            int err = 0; int len = sizeof(err); getsockopt(g_peer, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
            if (err || FD_ISSET(g_peer, &errSet)) { statusf("Connection failed"); closeSocket(g_peer); }
            else { g_connecting = false; queueHello(); }
        }
    } else if (g_role == NET_CLIENT && !g_connecting && !g_handshake && g_peer != INVALID_SOCKET) {
        queueHello();
    }
    if (g_peer != INVALID_SOCKET && !g_connecting) {
        fd_set readSet, writeSet;
        FD_ZERO(&readSet); FD_ZERO(&writeSet);
        FD_SET(g_peer, &readSet);
        if (g_sendAt < g_send.size()) FD_SET(g_peer, &writeSet);
        timeval tv = { 0, 0 };
        const int n = select(0, &readSet, &writeSet, 0, &tv);
        if (n == SOCKET_ERROR) {
            disconnectPeer("Network poll failed");
            return;
        }
        /* Sending first guarantees a just-connected client can deliver HELLO
           before either side waits for an answer. One bounded operation per
           readiness result also makes an accidental blocking socket harmless. */
        if (FD_ISSET(g_peer, &writeSet)) pumpSend();
        if (g_peer != INVALID_SOCKET && FD_ISSET(g_peer, &readSet)) pumpReceive(world);
    }
}

void netHostFrame(World& world) {
    if (g_role != NET_HOST || !g_ready || g_peer == INVALID_SOCKET) return;
    ++g_netFrame;
    if (g_netFrame % 2 == 0 && g_send.size() - g_sendAt < 4u * 1024u * 1024u) sendState();
    if (g_netFrame % 4 == 0 && g_send.size() - g_sendAt < 8u * 1024u * 1024u) sendChangedChunks(world);
    /* TCP prevents packet loss, but a periodic forced refresh also repairs a
       client-side bad write or future decoder bug without rejoining. Spread by
       the scan budget, so this is not a one-frame bandwidth spike. */
    if (g_netFrame % 3600 == 0) memset(g_chunkHash, 0, sizeof(g_chunkHash));
}

void netClientFrame(World& world) {
    /* Sixteen tiny hashes per frame cover a normal view-plus-margin in roughly
       one second and cost only a few kilobytes per second on the LAN. */
    sendClientChunkHashes(world);
}

bool netSendCommand(const PlayerCommand& c) {
    if (g_role != NET_CLIENT || !g_ready || g_peer == INVALID_SOCKET) return false;
    Writer w; w.u32v(c.sequence); w.u8v(c.player); w.u16v(c.generation);
    w.u8v(c.bits); w.u8v(c.pressed); w.u8v(c.selected); w.u8v(c.brushRadius);
    w.u16v((u16)c.brush);
    w.u8v(c.background ? 1 : 0); w.u8v(c.overwrite ? 1 : 0);
    w.u8v(c.line ? 1 : 0);
    w.u8v(c.lineCommit ? 1 : 0); w.u8v(c.lineCommitBits);
    w.i32v(c.lineStartX); w.i32v(c.lineStartY);
    w.u8v(c.digFilterOn ? 1 : 0); w.bytes(c.digFilter, sizeof(c.digFilter));
    w.i32v(c.aimX); w.i32v(c.aimY); queuePacket(PK_COMMAND, w.b); return true;
}

bool netPopRemoteCommand(PlayerCommand* command) {
    if (!g_haveRemoteCommand || !command) return false;
    *command = g_remoteCommand; g_haveRemoteCommand = false; return true;
}

bool netSendAction(const NetAction& a) {
    if (g_role != NET_CLIENT || !g_ready || g_peer == INVALID_SOCKET) return false;
    Writer w; w.u32v(a.sequence); w.u8v(a.player); w.u16v(a.generation);
    w.u8v(a.type); w.u8v(a.container); w.u8v(a.a); w.u8v(a.b); w.u8v(a.flags);
    w.i32v(a.x); w.i32v(a.y);
    queuePacket(PK_ACTION, w.b); return true;
}

bool netPopRemoteAction(NetAction* action) {
    if (!action || g_remoteActions.empty()) return false;
    *action = g_remoteActions.front(); g_remoteActions.pop_front(); return true;
}

NetRole netRole() { return g_role; }
bool netConnected() { return g_peer != INVALID_SOCKET; }
bool netReady() { return g_ready; }
bool netClientReady() { return g_role != NET_CLIENT || g_ready; }
PlayerId netAssignedPlayer() { return g_assigned; }
const char* netStatus() { return g_status; }
const char* netLocalAddress() { startup(); return g_localAddress; }
