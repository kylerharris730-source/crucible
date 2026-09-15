/* --- the Emberwing Feather on a joined client ---------------------------------

   Reported in play: "emberwing feather doesnt work in multiplayer on the client
   side, its glitchy and triggers a lot and it looks like theyre flying".

   The feather gives one jump in midair, and that jump is EDGE triggered: it
   fires on a fresh press, judged against `jumpHeld`, and spends `airJumpsUsed`.
   Both are memory the body carries from frame to frame.

   A joined client does not keep its body. Every state packet rebuilds it from
   the host's copy -- applyState calls playerSessionsReset, then codecPlayer
   fills in what the packet carries -- and the client then REPLAYS the inputs
   the host has not acknowledged yet on top. codecPlayer never carried
   `jumpHeld` or `airJumpsUsed`, so every rebuild handed the replay a body that
   had never pressed jump and had every air jump left. Hold jump through a
   fall and each packet read as a brand new press: a fresh feather jump several
   times a second, which is flying, and the host, which never saw those jumps,
   pulling the body back down, which is the glitching.

   This reproduces exactly that loop without a socket -- the host simulates one
   body straight through, the "client" rebuilds its body from the host's encoded
   copy every few frames and replays the frames since -- and holds the two to
   the same height. Compile with every src/*.cpp except main.cpp. Do not name
   the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "player.h"
#include "codec.h"
#include "multiplayer.h"
#include <stdio.h>
#include <string.h>
#include <vector>

static World g_testWorld;
static int failures = 0;

static void check(bool ok, const char* what) {
    printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

/* The input for frame t of the scenario: jump on the ground, let go, press
   again at the top of the leap for the feather's jump, then HOLD it all the
   way down. The hold through the fall is the part that went wrong. */
static PlayerInput inputAt(int t) {
    PlayerInput in; memset(&in, 0, sizeof(in));
    if (t < 6) in.jump = true;          /* the ground jump */
    else if (t < 20) in.jump = false;   /* released on the way up */
    else in.jump = true;                /* pressed again and held */
    return in;
}

/* What a fresh session body looks like after playerSessionsReset, with the
   budget the client re-publishes from the pack before each replayed frame. */
static void prepare(Player& p) {
    p.airJumps = 1;
    p.fallGuardPct = 100;
    p.speedMul = 1.0f;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    World& w = g_testWorld;
    w.reset();

    /* A floor well below, so the whole flight is in open air. */
    const int FX = 900, FY = 900;
    for (int x = FX - 200; x < FX + 200; ++x)
        for (int y = FY; y < FY + 4; ++y) w.setCell(x, y, MAT_STONE);
    w.setLiveWindow(FX - 300, FY - 300, FX + 300, FY + 50);

    const int FRAMES = 90;
    const int PACKET_EVERY = 4;   /* a state packet every few frames */
    const int LAG = 3;            /* inputs the host has not acknowledged yet */

    /* --- the host: one body, simulated straight through --------------------- */
    Player host;
    host.reset((float)FX, (float)(FY - PLAYER_H / 2 - 1));
    prepare(host);
    std::vector<float> hostY(FRAMES);
    std::vector<std::vector<u8> > hostState(FRAMES);
    int hostAirJumps = 0;
    for (int t = 0; t < FRAMES; ++t) {
        prepare(host);
        const int before = host.airJumpsUsed;
        host.update(w, inputAt(t));
        if (host.airJumpsUsed > before) ++hostAirJumps;
        hostY[t] = host.y;
        Blob out(hostState[t]);
        codecPlayer(out, host);
    }

    /* --- the client: rebuilt from the host every packet, then replaying ------ */
    Player client;
    client.reset((float)FX, (float)(FY - PLAYER_H / 2 - 1));
    prepare(client);
    float worstGap = 0.0f;
    float clientHighest = 1e9f, hostHighest = 1e9f;
    int clientAirJumps = 0;
    for (int t = 0; t < FRAMES; ++t) {
        if (t % PACKET_EVERY == 0 && t >= LAG) {
            /* The packet describes the host LAG frames ago. Rebuild the way
               applyState does: a reset session, then the decoded body. */
            const int base = t - LAG;
            Player rebuilt;
            rebuilt.reset(0.0f, 0.0f);
            Blob in(&hostState[base][0], hostState[base].size());
            codecPlayer(in, rebuilt);
            client = rebuilt;
            /* ...and replay every input after the acknowledged one. */
            for (int r = base + 1; r < t; ++r) {
                prepare(client);
                client.update(w, inputAt(r));
            }
        }
        prepare(client);
        const int before = client.airJumpsUsed;
        client.update(w, inputAt(t));
        if (client.airJumpsUsed > before) ++clientAirJumps;
        const float gap = client.y - hostY[t];
        if (gap < 0.0f ? -gap > worstGap : gap > worstGap) worstGap = gap < 0.0f ? -gap : gap;
        if (client.y < clientHighest) clientHighest = client.y;
        if (hostY[t] < hostHighest) hostHighest = hostY[t];
    }

    printf("host peaked at y=%.1f, client at y=%.1f; worst gap %.1f cells\n",
           hostHighest, clientHighest, worstGap);
    printf("air jumps seen on the client's live frames: %d (host used %d)\n",
           clientAirJumps, hostAirJumps);

    check(hostAirJumps == 1, "control: the host uses the feather's jump exactly once");
    check(clientHighest >= hostHighest - 1.0f,
          "the client climbs no higher than the host does");
    check(worstGap <= 1.0f, "and never drifts from the host's body by more than a cell");

    if (failures) { fprintf(stderr, "\n%d air-jump check(s) failed\n", failures); return 1; }
    printf("\nPASS\n");
    return 0;
}
