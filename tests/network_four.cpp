#include "network.h"
#include "materials.h"
#include "item.h"
#include "device.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

/* Four real processes on one machine: a host plus three joined clients, and a
   fourth client which must be refused.
 *
 * The single-peer smoke test cannot catch what breaks when the transport stops
 * being a single socket. Everything this test asserts is something that was
 * file-scope state before, and would therefore have been silently shared:
 *
 *   - three sockets are accepted and given three DISTINCT player slots;
 *   - each connection may only drive the player it was given, so a command
 *     naming somebody else's id is discarded rather than applied;
 *   - each peer's aim, sequence and acknowledgement are its own;
 *   - a fourth join is rejected with "full" instead of stealing a slot or
 *     hanging on an unanswered handshake.
 *
 * Each client sends a distinct aim so a crossed wire shows up as a wrong
 * value rather than merely a missing one, and one client also sends a command
 * impersonating another player which must never be seen by the host. */

static const u16 PORT = 27844;
static const int CLIENTS = MAX_PLAYERS - 1;

struct Child { PROCESS_INFORMATION process; char dir[MAX_PATH]; };

static bool spawnChild(const char* exePath, const char* arg, Child& out) {
    memset(&out, 0, sizeof(out));
    char tempRoot[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, tempRoot) ||
        !GetTempFileNameA(tempRoot, "cn4", 0, out.dir) ||
        !DeleteFileA(out.dir) || !CreateDirectoryA(out.dir, 0)) return false;
    STARTUPINFOA startup; memset(&startup, 0, sizeof(startup)); startup.cb = sizeof(startup);
    char command[MAX_PATH + 32]; sprintf(command, "\"%s\" %s", exePath, arg);
    if (!CreateProcessA(exePath, command, 0, 0, FALSE, 0, 0, out.dir, &startup, &out.process)) {
        RemoveDirectoryA(out.dir); return false;
    }
    return true;
}

static void reapChild(Child& child, int& failures, const char* label) {
    if (!child.process.hProcess) return;
    const DWORD wait = WaitForSingleObject(child.process.hProcess, 8000);
    DWORD code = 99; GetExitCodeProcess(child.process.hProcess, &code);
    if (wait != WAIT_OBJECT_0) { TerminateProcess(child.process.hProcess, 1); code = 98; }
    CloseHandle(child.process.hThread); CloseHandle(child.process.hProcess);
    char childBuild[MAX_PATH + 16]; sprintf(childBuild, "%s\\build", child.dir);
    RemoveDirectoryA(childBuild); RemoveDirectoryA(child.dir);
    if (code != 0) {
        fprintf(stderr, "%s exited %lu\n", label, code); ++failures;
    }
}

/* A joined client: connect, wait for readiness, announce itself with an aim
   nobody else uses, then hold until the host has clearly finished. */
static int runClient(int index, bool impersonate) {
    if (!netJoin("127.0.0.1", PORT)) { fprintf(stderr, "client %d: %s\n", index, netStatus()); return 3; }
    const DWORD start = GetTickCount();
    bool sent = false;
    while (GetTickCount() - start < 30000) {
        netPoll(g_world);
        netClientFrame(g_world);
        if (strstr(netStatus(), "Malformed") || strstr(netStatus(), "Invalid network")) {
            fprintf(stderr, "client %d saw %s\n", index, netStatus()); return 12;
        }
        if (strstr(netStatus(), "Join rejected")) return 13;
        if (netClientReady() && netAssignedPlayer() != PLAYER_NONE && !sent) {
            PlayerCommand c; memset(&c, 0, sizeof(c));
            c.sequence = 100u + (u32)index;
            c.player = netAssignedPlayer();
            c.generation = g_playerSessions[0].generation;
            c.bits = PCMD_RIGHT;
            /* Distinct per client, so a crossed peer is a wrong value rather
               than a missing one. */
            c.aimX = 1000 + index; c.aimY = 2000 + index;
            if (!netSendCommand(c)) return 5;
            if (impersonate) {
                /* Must be discarded by the host: this connection was not given
                   that player. A shared command slot would accept it. */
                PlayerCommand evil = c;
                evil.sequence = 900u + (u32)index;
                evil.player = (PlayerId)(c.player == 1 ? 2 : 1);
                evil.aimX = 66666; evil.aimY = 77777;
                netSendCommand(evil);
            }
            sent = true;
        }
        if (sent && netAcknowledgedCommand() >= 100u + (u32)index) {
            /* Stay connected a moment so the host still sees three peers while
               it makes its final assertions. */
            const DWORD linger = GetTickCount();
            while (GetTickCount() - linger < 3000) { netPoll(g_world); netClientFrame(g_world); Sleep(1); }
            netStop(); return 0;
        }
        Sleep(1);
    }
    fprintf(stderr, "client %d timed out: %s\n", index, netStatus());
    return 6;
}

/* The fourth client. The host is full, so this must be told so rather than
   being accepted or left waiting on a handshake that never completes. */
static int runOverflow() {
    if (!netJoin("127.0.0.1", PORT)) { fprintf(stderr, "overflow: %s\n", netStatus()); return 3; }
    const DWORD start = GetTickCount();
    bool sawConnection = false;
    while (GetTickCount() - start < 20000) {
        netPoll(g_world);
        if (netClientReady() && netAssignedPlayer() != PLAYER_NONE) {
            fprintf(stderr, "overflow client was admitted to a full game\n"); return 7;
        }
        if (netConnected()) sawConnection = true;
        if (strstr(netStatus(), "Join rejected")) { netStop(); return 0; }
        /* The host may also simply close a socket it has no room for. */
        if (sawConnection && !netConnected()) { netStop(); return 0; }
        Sleep(1);
    }
    fprintf(stderr, "overflow client neither rejected nor dropped: %s\n", netStatus());
    return 8;
}

int main(int argc, char** argv) {
    initMaterials(); initItems(); playerSessionsReset(); g_world.reset(); devClear();
    g_player.reset(400.0f, 400.0f);
    /* Solid floor under the join site, so bodies are placed rather than
       falling out of the interest rectangle mid-test. */
    for (int x = 300; x < 600; ++x)
        for (int y = 430; y < 440; ++y) g_world.setCell(x, y, MAT_STONE);

    if (argc == 2 && strncmp(argv[1], "client", 6) == 0)
        return runClient(argv[1][6] - '0', argv[1][6] == '0');
    if (argc == 2 && strcmp(argv[1], "overflow") == 0) return runOverflow();
    if (argc != 1) return 2;

    if (!netHost(PORT, true)) { fprintf(stderr, "host: %s\n", netStatus()); return 3; }

    char exePath[MAX_PATH];
    if (!GetFullPathNameA(argv[0], MAX_PATH, exePath, 0)) return 7;

    Child clients[CLIENTS]; memset(clients, 0, sizeof(clients));
    for (int i = 0; i < CLIENTS; ++i) {
        char arg[16]; sprintf(arg, "client%d", i);
        if (!spawnChild(exePath, arg, clients[i])) {
            fprintf(stderr, "could not launch client %d: %lu\n", i, GetLastError()); return 7;
        }
        /* Staggered, because simultaneous joins are a separate concern from
           whether three peers can coexist at all. */
        Sleep(400);
    }

    u32 seen[MAX_PLAYERS]; memset(seen, 0, sizeof(seen));
    i32 aimX[MAX_PLAYERS], aimY[MAX_PLAYERS];
    memset(aimX, 0, sizeof(aimX)); memset(aimY, 0, sizeof(aimY));
    int peakPeers = 0, failures = 0;
    bool overflowLaunched = false, impostorSeen = false;
    Child overflow; memset(&overflow, 0, sizeof(overflow));

    const DWORD start = GetTickCount();
    while (GetTickCount() - start < 40000) {
        netPoll(g_world);
        netHostFrame(g_world);
        if (strstr(netStatus(), "Malformed") || strstr(netStatus(), "Invalid network")) {
            fprintf(stderr, "host saw %s\n", netStatus()); return 12;
        }
        if (netPeerCount() > peakPeers) peakPeers = netPeerCount();

        for (int slot = 1; slot < MAX_PLAYERS; ++slot) {
            PlayerCommand c;
            while (netPopRemoteCommand((PlayerId)slot, &c)) {
                if (c.player != (PlayerId)slot) {
                    /* The per-player queue handed us somebody else's input. */
                    fprintf(stderr, "slot %d received a command for player %u\n",
                            slot, (unsigned)c.player);
                    ++failures;
                }
                if (c.aimX == 66666 || c.aimY == 77777) impostorSeen = true;
                seen[slot] = c.sequence; aimX[slot] = c.aimX; aimY[slot] = c.aimY;
                netMarkRemoteCommandApplied((PlayerId)slot, c.sequence);
            }
        }

        int ready = 0;
        for (int slot = 1; slot < MAX_PLAYERS; ++slot) if (seen[slot]) ++ready;
        if (ready == CLIENTS && !overflowLaunched) {
            overflowLaunched = true;
            if (!spawnChild(exePath, "overflow", overflow)) {
                fprintf(stderr, "could not launch overflow client\n"); return 7;
            }
        }
        if (overflowLaunched && overflow.process.hProcess &&
            WaitForSingleObject(overflow.process.hProcess, 0) == WAIT_OBJECT_0) break;
        Sleep(1);
    }

    reapChild(overflow, failures, "overflow client");
    for (int i = 0; i < CLIENTS; ++i) { char l[24]; sprintf(l, "client %d", i); reapChild(clients[i], failures, l); }
    netStop();

    if (peakPeers != CLIENTS) {
        fprintf(stderr, "peak simultaneous peers was %d, expected %d\n", peakPeers, CLIENTS);
        ++failures;
    }
    for (int slot = 1; slot < MAX_PLAYERS; ++slot) {
        if (!seen[slot]) { fprintf(stderr, "player slot %d never sent a command\n", slot); ++failures; continue; }
        /* Slot n was filled by the nth client to arrive, which used aim
           1000+index. Any two slots sharing an aim means two connections were
           driving one player, or one connection was given two slots. */
        if (aimX[slot] < 1000 || aimX[slot] > 1000 + CLIENTS ||
            aimY[slot] != aimX[slot] + 1000) {
            fprintf(stderr, "player slot %d has aim (%d,%d)\n", slot, aimX[slot], aimY[slot]);
            ++failures;
        }
        for (int other = 1; other < slot; ++other)
            if (aimX[other] == aimX[slot]) {
                fprintf(stderr, "slots %d and %d share aim %d\n", other, slot, aimX[slot]);
                ++failures;
            }
    }
    if (impostorSeen) {
        fprintf(stderr, "a client drove a player it was not assigned\n"); ++failures;
    }

    if (failures) { fprintf(stderr, "four-player test FAILED (%d)\n", failures); return 1; }
    printf("four-player network test passed (%d peers, distinct slots, overflow refused)\n", peakPeers);
    return 0;
}
