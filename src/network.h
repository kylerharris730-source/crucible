#pragma once
#include "multiplayer.h"

/* Direct-IP, host-authoritative LAN transport. TCP is intentional for the
   first playable milestone: world snapshots and chunk corrections must arrive
   reliably and in order, and one peer on a LAN does not justify rebuilding
   reliability, congestion and fragmentation on top of UDP yet. */
enum NetRole { NET_OFF = 0, NET_HOST, NET_CLIENT };

enum PlayerCommandBits {
    PCMD_LEFT      = 1 << 0,
    PCMD_RIGHT     = 1 << 1,
    PCMD_JUMP      = 1 << 2,
    PCMD_DOWN      = 1 << 3,
    PCMD_USE_LEFT  = 1 << 4,
    PCMD_USE_RIGHT = 1 << 5,
    PCMD_INTERACT  = 1 << 6,
    PCMD_RESPAWN   = 1 << 7
};

struct PlayerCommand {
    u32 sequence;
    PlayerId player;
    u16 generation;
    u8 bits;
    u8 pressed; /* rising-edge verbs preserved even if several commands coalesce */
    u8 selected;
    u8 brushRadius;
    i16 brush;
    bool background;
    bool overwrite;
    bool line;
    bool lineCommit;
    u8 lineCommitBits;
    i32 lineStartX, lineStartY;
    bool digFilterOn;
    static const int FILTER_BYTES = (MAT_COUNT + 7) / 8;
    u8 digFilter[FILTER_BYTES];
    i32 aimX, aimY;
};

enum NetActionType {
    NACT_NONE = 0,
    NACT_SLOT,
    NACT_CRAFT,
    NACT_CLOSE_DEVICE,
    NACT_DEVICE,
    NACT_WIRE_POINT,
    NACT_CIRCUIT_TERMINAL,
    NACT_STOW_CURSOR,
    NACT_CREATIVE_ITEM
};

enum NetDeviceOperation {
    NDEV_DEC = 0,
    NDEV_INC,
    NDEV_TAKE,
    NDEV_TURN,
    NDEV_SET_FILTER,
    NDEV_SET_SIGNAL,
    NDEV_SET_A,
    NDEV_SET_B,
    NDEV_SET_OUT
};

enum NetSlotContainer {
    NSLOT_PACK = 0,
    NSLOT_EQUIP,
    NSLOT_DRONE_MODULE,
    NSLOT_TOOL_MODULE,
    NSLOT_TOOL_PAYLOAD,
    NSLOT_TRASH,
    NSLOT_CHEST
};

struct NetAction {
    u32 sequence;
    PlayerId player;
    u16 generation;
    u8 type;
    u8 container;
    u8 a, b;
    u8 flags;
    i32 x, y;
};

static const u16 NET_DEFAULT_PORT = 27841;

/* `loopbackOnly` binds 127.0.0.1 instead of every interface. Windows Firewall
   prompts the moment a program listens on a real interface, and each freshly
   built test binary is a new program as far as it is concerned -- so a test
   run means a stack of dialogs, and until they are answered the listening
   socket is blocked, which stalls the very test that raised them. Tests only
   ever talk to themselves, so they ask for loopback and are never seen by the
   firewall at all. Real hosting still binds everything. */
bool netHost(u16 port = NET_DEFAULT_PORT, bool loopbackOnly = false);
bool netJoin(const char* ipv4, u16 port = NET_DEFAULT_PORT);
void netStop();
void netPoll(World& world);
void netHostFrame(World& world);
void netClientFrame(World& world);
void netMarkWorldEdit(int x, int y, int radius);
/* Protect locally predicted cells from chunk snapshots captured before the
   host processed the command which created them. The authoritative snapshot
   carrying this sequence (or a later one) is still accepted as correction. */
void netMarkPredictedWorldEdit(int x0, int y0, int x1, int y1, int radius,
                               u32 commandSequence);

bool netSendCommand(const PlayerCommand& command);
/* Held input is coalesced per player, so the caller names the slot it is about
   to tick. A single queue would let one player's packet rate decide how often
   another player moved. */
bool netPopRemoteCommand(PlayerId player, PlayerCommand* command);
/* The host acknowledges a command only after the authoritative player tick has
   consumed it. Clients use that watermark to replay only inputs which the host
   state does not contain yet, rather than snapping back on every snapshot.
   The watermark is per connection: telling one client that another client's
   sequence had been applied would discard its own pending movement. */
void netMarkRemoteCommandApplied(PlayerId player, u32 sequence);
u32 netAcknowledgedCommand();
void netMarkRemoteActionApplied(PlayerId player, u32 sequence);
u32 netAcknowledgedAction();
u32 netStateSerial();
bool netSendAction(const NetAction& action);
bool netPopRemoteAction(NetAction* action);

NetRole netRole();
bool netConnected();
/* Live joined sockets. Zero on a host that is only listening. */
int netPeerCount();
bool netReady();
bool netClientReady();
PlayerId netAssignedPlayer();
const char* netStatus();
const char* netLocalAddress();
