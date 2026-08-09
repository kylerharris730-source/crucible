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
    bool background;
    bool overwrite;
    bool line;
    i32 aimX, aimY;
};

enum NetActionType {
    NACT_NONE = 0,
    NACT_SLOT,
    NACT_CRAFT,
    NACT_CLOSE_DEVICE,
    NACT_DEVICE,
    NACT_WIRE_POINT,
    NACT_CIRCUIT_TERMINAL
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

bool netHost(u16 port = NET_DEFAULT_PORT);
bool netJoin(const char* ipv4, u16 port = NET_DEFAULT_PORT);
void netStop();
void netPoll(World& world);
void netHostFrame(World& world);
void netClientFrame(World& world);

bool netSendCommand(const PlayerCommand& command);
bool netPopRemoteCommand(PlayerCommand* command);
bool netSendAction(const NetAction& action);
bool netPopRemoteAction(NetAction* action);

NetRole netRole();
bool netConnected();
bool netReady();
bool netClientReady();
PlayerId netAssignedPlayer();
const char* netStatus();
const char* netLocalAddress();
