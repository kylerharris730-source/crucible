#include "network.h"
#include "materials.h"
#include "item.h"
#include "device.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

/* Two copies of this executable form a real TCP connection over loopback. It
   exercises the handshake, exact-build check, full compressed save snapshot,
   player-state mapping, ready acknowledgement and a client command arriving
   at the authority. Unit-testing the packet codec alone would miss every
   socket/framing/order failure in that chain. */
int main(int argc, char** argv) {
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "host") && strcmp(argv[1], "client"))) return 2;
    initMaterials(); initItems(); playerSessionsReset(); g_world.reset(); devClear();
    g_player.reset(400.0f, 400.0f);
    /* The client deliberately corrupts this authoritative cell after joining.
       Its rotating hash report must make the host resend the chunk. */
    const int repairX = 420, repairY = 400;
    g_world.setCell(repairX, repairY, MAT_STONE);
    const bool host = argc == 1 || strcmp(argv[1], "host") == 0;
    if (host ? !netHost(27842) : !netJoin("127.0.0.1", 27842)) {
        fprintf(stderr, "%s\n", netStatus()); return 3;
    }
    PROCESS_INFORMATION child; memset(&child, 0, sizeof(child));
    if (argc == 1) {
        STARTUPINFOA startup; memset(&startup, 0, sizeof(startup)); startup.cb = sizeof(startup);
        char command[MAX_PATH + 32]; sprintf(command, "\"%s\" client", argv[0]);
        if (!CreateProcessA(0, command, 0, 0, FALSE, 0, 0, 0, &startup, &child)) {
            fprintf(stderr, "could not launch network client: %lu\n", GetLastError()); return 7;
        }
    }
    const DWORD start = GetTickCount();
    DWORD lastReport = start;
    bool sent = false, corrupted = false, repairAckSent = false;
    bool gotCommand = false, gotAction = false, gotRepairAck = false;
    while (GetTickCount() - start < 30000) {
        netPoll(g_world);
        if (!host) netClientFrame(g_world);
        if (GetTickCount() - lastReport >= 2000) {
            fprintf(stderr, "%s: connected=%d ready=%d status=%s\n",
                    host ? "host" : "client", netConnected() ? 1 : 0,
                    netReady() ? 1 : 0, netStatus());
            lastReport = GetTickCount();
        }
        if (host) {
            netHostFrame(g_world);
            PlayerCommand c;
            if (netPopRemoteCommand(&c)) {
                if (c.player != 1 || c.aimX != 777 || c.aimY != 888 ||
                    !(c.bits & PCMD_RIGHT) || !c.line || c.brush != MAT_WATER ||
                    !c.lineCommit || c.lineCommitBits != PCMD_USE_LEFT ||
                    c.lineStartX != 700 || c.lineStartY != 701 ||
                    !c.digFilterOn || !(c.digFilter[MAT_SAND >> 3] & (1u << (MAT_SAND & 7)))) {
                    fprintf(stderr, "host received corrupt command\n"); return 4;
                }
                gotCommand = true;
            }
            NetAction action;
            if (netPopRemoteAction(&action)) {
                if (action.player != 1) {
                    fprintf(stderr, "host received corrupt action\n"); return 9;
                }
                if (action.type == NACT_CRAFT && action.a == 7 && action.b == 3 &&
                    action.x == 1234 && action.y == 4321) gotAction = true;
                else if (action.type == NACT_CLOSE_DEVICE && action.x == 2468)
                    gotRepairAck = true;
                else { fprintf(stderr, "host received corrupt action\n"); return 9; }
            }
            if (gotCommand && gotAction && gotRepairAck) {
                netStop();
                if (child.hProcess) {
                    const DWORD wait = WaitForSingleObject(child.hProcess, 5000);
                    DWORD code = 99; GetExitCodeProcess(child.hProcess, &code);
                    CloseHandle(child.hThread); CloseHandle(child.hProcess);
                    if (wait != WAIT_OBJECT_0 || code != 0) {
                        fprintf(stderr, "network client failed: wait=%lu code=%lu\n", wait, code); return 8;
                    }
                }
                puts("network host smoke passed"); return 0;
            }
        } else if (netClientReady() && !sent) {
            g_world.setCell(repairX, repairY, MAT_SAND); corrupted = true;
            PlayerCommand c; memset(&c, 0, sizeof(c));
            c.sequence = 1; c.player = netAssignedPlayer();
            c.generation = g_playerSessions[0].generation;
            c.bits = PCMD_RIGHT; c.selected = 0; c.brushRadius = 6;
            c.line = true; c.brush = MAT_WATER; c.digFilterOn = true;
            c.lineCommit = true; c.lineCommitBits = PCMD_USE_LEFT;
            c.lineStartX = 700; c.lineStartY = 701;
            c.digFilter[MAT_SAND >> 3] |= (u8)(1u << (MAT_SAND & 7));
            c.aimX = 777; c.aimY = 888;
            if (!netSendCommand(c)) return 5;
            NetAction action; memset(&action, 0, sizeof(action));
            action.sequence = 1; action.player = netAssignedPlayer();
            action.generation = g_playerSessions[0].generation;
            action.type = NACT_CRAFT; action.a = 7; action.b = 3;
            action.x = 1234; action.y = 4321;
            if (!netSendAction(action)) return 10;
            sent = true;
        } else if (sent && corrupted && g_world.at(repairX, repairY).mat == MAT_STONE && !repairAckSent) {
            NetAction ack; memset(&ack, 0, sizeof(ack));
            ack.sequence = 2; ack.player = netAssignedPlayer();
            ack.generation = g_playerSessions[0].generation;
            ack.type = NACT_CLOSE_DEVICE; ack.x = 2468;
            if (!netSendAction(ack)) return 11;
            repairAckSent = true;
        } else if (repairAckSent) {
            /* Give the nonblocking send queue time to drain before exiting. */
            netPoll(g_world); Sleep(250);
            puts("network client smoke passed (chunk resync repaired divergence)"); netStop(); return 0;
        }
        Sleep(5);
    }
    fprintf(stderr, "%s timed out: %s\n", host ? "host" : "client", netStatus());
    if (child.hProcess) {
        TerminateProcess(child.hProcess, 9);
        CloseHandle(child.hThread); CloseHandle(child.hProcess);
    }
    netStop(); return 6;
}
