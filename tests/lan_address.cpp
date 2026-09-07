/* --- the address on the Host LAN button --------------------------------------

   Reported from play: "sometimes the correct local ipv4 is displayed, but
   sometimes the public ip is displayed, which i dont want. this computer shows
   the wrong one for example."

   It did, and the machine that reported it is the whole test case:

       gethostbyname[0] = 26.34.44.215     a VPN's address, publicly routed
       gethostbyname[1] = 192.168.56.1     a hypervisor's host-only switch
       gethostbyname[2] = 192.168.1.242    the LAN address a player can dial

   The old code took the first entry. All three are really bound to this
   machine, so "is this address real" cannot separate them -- see the note in
   network.cpp for what does.

   That makes this an awkward thing to test, because the right answer depends
   on the machine it runs on and there is no fixture that can supply a VPN.
   What CAN be pinned is the property the report is about, and it is a real
   one: the address offered to a LAN peer must be an address a LAN peer could
   reach, which means private, and it must belong to this machine, which means
   bindable. 26.34.44.215 is bindable here and fails the first; a made-up
   192.168 address would pass the first and fail the second. Both together are
   the claim.

   Compile with every src/*.cpp except main.cpp. No window. Do not name the
   output *_test.exe -- build.bat deletes those. */

#include "network.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static bool privateV4(u32 hostOrder) {
    const u32 a = (hostOrder >> 24) & 0xFF, b = (hostOrder >> 16) & 0xFF;
    return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168);
}

/* Can this machine actually bind it? An address that is not on one of our own
   interfaces fails with WSAEADDRNOTAVAIL, which is the cheapest possible proof
   that we are advertising ourselves and not somebody else. */
static bool bindable(const char* ip) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port   = 0;                    /* any free port */
    a.sin_addr.s_addr = inet_addr(ip);
    const bool ok = bind(s, (sockaddr*)&a, sizeof(a)) == 0;
    closesocket(s);
    return ok;
}

int main() {
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);

    const char* shown = netLocalAddress();
    printf("Host LAN would offer %s\n", shown);

    const u32 addr = ntohl(inet_addr(shown));
    const bool loopback = strcmp(shown, "127.0.0.1") == 0;

    /* Loopback is allowed, and only because it is the truthful answer for a
       machine with no network at all -- a CI container, say. It is not allowed
       to be the answer on a machine that HAS a LAN address, which is the next
       check. */
    check(loopback || privateV4(addr),
          "the address offered is a private LAN address, not a public one");
    check(bindable(shown), "and it is one of this machine's own addresses");

    /* If any private address exists here, we must be showing one. This is what
       catches the reported bug on a machine like the reporter's: a VPN address
       is bindable and would pass the check above on its own. */
    bool haveLan = false;
    char host[256];
    if (gethostname(host, sizeof(host)) == 0) {
        hostent* he = gethostbyname(host);
        if (he) for (int i = 0; he->h_addr_list[i]; ++i) {
            in_addr a;
            memcpy(&a, he->h_addr_list[i], sizeof(a));
            printf("  this machine also has %s\n", inet_ntoa(a));
            if (privateV4(ntohl(a.s_addr))) haveLan = true;
        }
    }
    if (haveLan)
        check(!loopback && privateV4(addr),
              "and a machine with a LAN address shows it rather than loopback");
    else
        printf("  (no private address on this machine to prefer)\n");

    if (failures) {
        fprintf(stderr, "\n%d LAN-address check(s) failed\n", failures);
        return 1;
    }
    printf("\nthe Host LAN button offers a dialable address\n");
    return 0;
}
