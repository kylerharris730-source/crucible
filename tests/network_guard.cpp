#include <winsock2.h>
#include "network.h"
#include "materials.h"
#include "item.h"
#include "device.h"
#include <stdio.h>
#include <string.h>
#include <vector>

/* What a host does with connections that are not becoming players.
 *
 * The host has three seats and listens on every interface, so anybody who can
 * reach the port is a stranger until they have said hello. Each case here is
 * something a stranger could do to a host before this was tightened:
 *
 *   - announce a quarter-gigabyte packet before hello, and have the host
 *     buffer it (three connections at once, three times over);
 *   - connect and say nothing, holding a seat forever -- three of those and
 *     nobody can join;
 *   - be rejected, ignore the rejection, and keep the seat anyway.
 *
 * Raw sockets rather than a second netJoin, because the point is a peer that
 * does NOT follow the protocol. */

static const u16 PORT = 27845;
static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) { fprintf(stderr, "FAIL: %s (host status: %s)\n", what, netStatus()); ++failures; }
}

static SOCKET dial() {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a; memset(&a, 0, sizeof(a)); a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(PORT);
    if (connect(s, (sockaddr*)&a, sizeof(a)) != 0) { closesocket(s); return INVALID_SOCKET; }
    u_long one = 1; ioctlsocket(s, FIONBIO, &one);
    return s;
}

static void sendAll(SOCKET s, const std::vector<u8>& b) {
    size_t at = 0;
    while (at < b.size()) {
        const int n = send(s, (const char*)&b[at], (int)(b.size() - at), 0);
        if (n <= 0) { if (WSAGetLastError() == WSAEWOULDBLOCK) { Sleep(1); continue; } return; }
        at += (size_t)n;
    }
}

/* Runs the host until `done` says so or `ms` passes. Returns whether done. */
template <class F> static bool pumpUntil(DWORD ms, F done) {
    const DWORD start = GetTickCount();
    while (GetTickCount() - start < ms) {
        netPoll(g_world); netHostFrame(g_world);
        if (done()) return true;
        Sleep(1);
    }
    return false;
}

/* True once the host has closed its end: a read returns 0 or a hard error.
   Anything the host sent first is collected into `got`. */
static bool closedByHost(SOCKET s, std::vector<u8>* got = 0) {
    char buf[4096];
    for (;;) {
        const int n = recv(s, buf, sizeof(buf), 0);
        if (n > 0) { if (got) got->insert(got->end(), buf, buf + n); continue; }
        if (n == 0) return true;
        return WSAGetLastError() != WSAEWOULDBLOCK;
    }
}

static void u32le(std::vector<u8>& b, u32 v) { for (int i = 0; i < 4; ++i) b.push_back((u8)(v >> (i * 8))); }
static void str(std::vector<u8>& b, const char* s) {
    const u16 n = (u16)strlen(s); b.push_back((u8)n); b.push_back((u8)(n >> 8));
    b.insert(b.end(), s, s + n);
}

int main() {
    initMaterials(); initItems(); playerSessionsReset(); g_world.reset(); devClear();
    g_player.reset(400.0f, 400.0f);
    if (!netHost(PORT, true)) { fprintf(stderr, "host: %s\n", netStatus()); return 3; }

    /* --- a huge packet before hello -------------------------------------- */
    {
        SOCKET s = dial();
        check(s != INVALID_SOCKET, "the oversize client connects");
        std::vector<u8> b; u32le(b, 200u * 1024u * 1024u); b.push_back(1);
        sendAll(s, b);
        check(pumpUntil(3000, [&] { return closedByHost(s); }),
              "a 200 MiB packet announced before hello is refused at once");
        check(pumpUntil(1000, [] { return netPeerCount() == 0; }), "and its seat is free again");
        closesocket(s);
    }

    /* --- the wrong build, and not listening to the answer ----------------- */
    {
        SOCKET s = dial();
        std::vector<u8> hello;
        u32le(hello, 0x54454E43u); u32le(hello, 0xFFFFu);   /* no real protocol */
        str(hello, "somebody-elses-build"); str(hello, "0123456789abcdef0123456789abcdef");
        std::vector<u8> b; u32le(b, (u32)hello.size() + 1); b.push_back(1);
        b.insert(b.end(), hello.begin(), hello.end());
        sendAll(s, b);
        std::vector<u8> got;
        const bool closed = pumpUntil(7000, [&] { return closedByHost(s, &got); });
        check(closed, "a rejected client is disconnected, not left holding a seat");
        check(got.size() > 5 && got[4] == 2, "after being told why");
        check(pumpUntil(1000, [] { return netPeerCount() == 0; }), "and its seat is free again");
        closesocket(s);
    }

    /* --- three that say nothing ------------------------------------------ */
    {
        SOCKET idle[3];
        for (int i = 0; i < 3; ++i) idle[i] = dial();
        check(pumpUntil(2000, [] { return netPeerCount() == 3; }), "three silent sockets take the seats");
        check(pumpUntil(15000, [] { return netPeerCount() == 0; }),
              "and give them back when they never say hello");
        for (int i = 0; i < 3; ++i) {
            check(closedByHost(idle[i]), "each silent socket is closed by the host");
            closesocket(idle[i]);
        }
    }

    netStop();
    if (failures) { fprintf(stderr, "%d network guard check(s) failed\n", failures); return 1; }
    puts("oversize, rejected and silent connections all give their seats back");
    return 0;
}
