#include "network.h"
#include "materials.h"
#include "item.h"
#include "device.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

/* Compile this source twice with different CRUCIBLE_BUILD_ID values. The host
   launches the other binary and proves the handshake rejects it before READY
   or any world/gameplay packet can be accepted. */
int main(int argc, char** argv) {
    const bool client = argc == 2 && strcmp(argv[1], "client") == 0;
    if ((!client && argc != 2) || (client && argc != 2)) return 2;
    initMaterials(); initItems(); playerSessionsReset(); g_world.reset(); devClear();

    PROCESS_INFORMATION child; memset(&child, 0, sizeof(child));
    if (client) {
        if (!netJoin("127.0.0.1", 27843)) return 3;
    } else {
        if (!netHost(27843)) return 4;
        STARTUPINFOA startup; memset(&startup, 0, sizeof(startup)); startup.cb = sizeof(startup);
        char command[MAX_PATH + 32]; sprintf(command, "\"%s\" client", argv[1]);
        if (!CreateProcessA(0, command, 0, 0, FALSE, 0, 0, 0, &startup, &child)) return 5;
    }

    const DWORD start = GetTickCount(); bool sawConnection = false;
    while (GetTickCount() - start < 15000) {
        netPoll(g_world);
        if (client) {
            if (netReady()) return 6;
            if (!netConnected() && strstr(netStatus(), "Join rejected")) {
                puts("mismatched client rejected before world sync"); netStop(); return 0;
            }
        } else {
            if (netReady()) return 7;
            if (netConnected()) sawConnection = true;
            if (sawConnection && !netConnected()) {
                const DWORD wait = WaitForSingleObject(child.hProcess, 3000);
                DWORD code = 99; GetExitCodeProcess(child.hProcess, &code);
                CloseHandle(child.hThread); CloseHandle(child.hProcess); netStop();
                if (wait != WAIT_OBJECT_0 || code != 0) return 8;
                puts("exact-build mismatch handshake passed"); return 0;
            }
        }
        Sleep(5);
    }
    if (!client && child.hProcess) {
        TerminateProcess(child.hProcess, 9);
        CloseHandle(child.hThread); CloseHandle(child.hProcess);
    }
    fprintf(stderr, "mismatch test timed out: %s\n", netStatus()); netStop(); return 9;
}
