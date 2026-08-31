#include "multiplayer.h"
#include <string.h>

PlayerSession g_playerSessions[MAX_PLAYERS];
RememberedPlayer g_roster[MAX_REMEMBERED];
static u32 g_rosterClock = 0;
Player& g_player = g_playerSessions[LOCAL_PLAYER_ID].body;
Inventory& g_inv = g_playerSessions[LOCAL_PLAYER_ID].inventory;

static void clearSession(PlayerSession& s) {
    s.inventory.clear();
    memset(s.drones, 0, sizeof(s.drones));
    s.garlicCooldown = 0;
    s.healCooldown = 0;
    s.cursor = ItemStack();
    s.trash = ItemStack();
    s.body.reset(0.0f, 0.0f);
    s.previousAimX = s.previousAimY = -1;
    s.digCooldown = 0;
    s.restBed = -1;
    s.respawnBedX = s.respawnBedY = -1;
    s.respawnFrames = 0;
    s.openDevice = -1;
    s.wireX = s.wireY = -1;
    s.circuitWireFrom = -1; s.circuitWirePort = 0;
    s.suppressRightUse = false;
    s.lineActive = false; s.lineBits = s.lineSelected = 0; s.lineRadius = 1; s.lineBrush = MAT_EMPTY;
    s.lineBackground = s.lineOverwrite = s.lineFilterOn = false;
    memset(s.lineFilter, 0, sizeof(s.lineFilter));
    s.previousCommandBits = 0;
    s.connected = false;
    s.local = false;
    s.networkId = PLAYER_NONE;
}

bool playerConsumeHealing(PlayerSession& session, ItemId item) {
    if (item == ITEM_NONE || item >= ITEM_COUNT || ITEMS[item].kind != ITEMK_FOOD ||
        ITEMS[item].heal <= 0 || !session.body.alive ||
        session.body.hp >= PLAYER_HP_MAX || session.healCooldown > 0) return false;
    if (session.inventory.take(item, 1) != 1) return false;
    session.body.heal(ITEMS[item].heal);
    session.healCooldown = HEAL_COOLDOWN_FRAMES;
    return true;
}

void playerHealingCooldownTick(PlayerSession& session) {
    if (session.healCooldown > 0) --session.healCooldown;
}

void playerSessionsReset() {
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        /* Increment instead of resetting: packets from before a world/session
           reset must not become valid again merely because slot numbers match. */
        ++g_playerSessions[i].generation;
        if (!g_playerSessions[i].generation) ++g_playerSessions[i].generation;
        clearSession(g_playerSessions[i]);
    }
    g_playerSessions[LOCAL_PLAYER_ID].connected = true;
    g_playerSessions[LOCAL_PLAYER_ID].local = true;
    g_playerSessions[LOCAL_PLAYER_ID].networkId = LOCAL_PLAYER_ID;
}

void rosterClear() {
    memset(g_roster, 0, sizeof(g_roster));
    g_rosterClock = 0;
}

static RememberedPlayer* rosterFind(const char* id) {
    if (!id || !id[0]) return 0;
    for (int i = 0; i < MAX_REMEMBERED; ++i)
        if (g_roster[i].used && strcmp(g_roster[i].id, id) == 0) return &g_roster[i];
    return 0;
}

/* Every stack that can hold a tool handle, in one list, so that remembering
   and restoring cannot disagree about which ones they walk. */
static int rosterStacks(Inventory& inv, ItemStack** out) {
    int n = 0;
    for (int i = 0; i < INV_SLOTS; ++i) out[n++] = &inv.slot[i];
    for (int i = 0; i < EQ_COUNT;   ++i) out[n++] = &inv.equip[i];
    return n;
}

static void freeInventoryTools(Inventory& inv) {
    ItemStack* stacks[INV_SLOTS + EQ_COUNT];
    const int n = rosterStacks(inv, stacks);
    for (int i = 0; i < n; ++i)
        if (stacks[i]->inst) { toolInstFree(stacks[i]->inst); stacks[i]->inst = 0; }
    for (int d = 0; d < DRONE_BAY_COUNT; ++d)
        for (int m = 0; m < Inventory::DRONE_MODULE_SLOTS_MAX; ++m)
            if (inv.droneModule[d][m].inst) {
                toolInstFree(inv.droneModule[d][m].inst);
                inv.droneModule[d][m].inst = 0;
            }
}

void rosterRemember(const char* id, const PlayerSession& session) {
    if (!id || !id[0]) return;
    RememberedPlayer* slot = rosterFind(id);
    if (!slot)
        for (int i = 0; i < MAX_REMEMBERED && !slot; ++i)
            if (!g_roster[i].used) slot = &g_roster[i];
    if (!slot) {
        /* Full: the least recently seen makes way. */
        slot = &g_roster[0];
        for (int i = 1; i < MAX_REMEMBERED; ++i)
            if (g_roster[i].lastSeen < slot->lastSeen) slot = &g_roster[i];
    }
    memset(slot, 0, sizeof(*slot));
    strncpy(slot->id, id, PLAYER_IDENTITY_CHARS);
    slot->id[PLAYER_IDENTITY_CHARS] = 0;
    slot->inventory = session.inventory;
    slot->x = session.body.x;
    slot->y = session.body.y;

    /* Trade live handles for copies of what they point at. Afterwards the
       entry owns nothing in the global pool, which is precisely what lets
       the caller close the session and hand those instances straight back. */
    ItemStack* stacks[INV_SLOTS + EQ_COUNT];
    const int n = rosterStacks(slot->inventory, stacks);
    for (int i = 0; i < n; ++i) {
        const u16 live = stacks[i]->inst;
        stacks[i]->inst = 0;
        if (!live || live >= MAX_TOOL_INST || !g_toolInst[live].used) continue;
        if (slot->toolCount >= REMEMBERED_TOOLS) continue;   /* returns blank */
        slot->tools[slot->toolCount] = g_toolInst[live];
        stacks[i]->inst = (u16)(slot->toolCount + 1);        /* 1-based index */
        ++slot->toolCount;
    }
    for (int d = 0; d < DRONE_BAY_COUNT; ++d)
        for (int m = 0; m < Inventory::DRONE_MODULE_SLOTS_MAX; ++m)
            slot->inventory.droneModule[d][m].inst = 0;

    slot->lastSeen = ++g_rosterClock;
    slot->used = true;
}

bool rosterRestore(const char* id, PlayerSession& session) {
    RememberedPlayer* slot = rosterFind(id);
    if (!slot) return false;

    /* Whatever this session was handed a moment ago -- a starting kit, most
       likely -- is about to be overwritten, so its instances go back to the
       pool first or they are stranded with nothing referencing them. */
    freeInventoryTools(session.inventory);

    session.inventory = slot->inventory;
    /* Put them back where they stood. A returning player materialising at
       the host's feet would be a second surprise on top of the first. */
    session.body.x = slot->x;
    session.body.y = slot->y;

    ItemStack* stacks[INV_SLOTS + EQ_COUNT];
    const int n = rosterStacks(session.inventory, stacks);
    for (int i = 0; i < n; ++i) {
        const u16 index = stacks[i]->inst;
        stacks[i]->inst = 0;
        if (!index || index > slot->toolCount) continue;
        if (stacks[i]->empty() || ITEMS[stacks[i]->item].kind != ITEMK_TOOL) continue;
        const u16 live = toolInstNew(stacks[i]->item);
        if (!live) continue;        /* pool full: a blank tool beats no tool */
        g_toolInst[live] = slot->tools[index - 1];
        g_toolInst[live].used = true;
        g_toolInst[live].cooldown = 0;
        stacks[i]->inst = live;
    }

    slot->lastSeen = ++g_rosterClock;
    return true;
}

PlayerId playerSessionOpen(bool local, float spawnX, float spawnY) {
    for (int i = 1; i < MAX_PLAYERS; ++i) {
        PlayerSession& s = g_playerSessions[i];
        if (s.connected) continue;
        ++s.generation;
        if (!s.generation) ++s.generation;
        clearSession(s);
        s.body.reset(spawnX, spawnY);
        /* The same handout the host got when the world was made. Without
           this a guest spawns with an empty pack and no way to light a
           fire, which is not a difficulty curve, it is an oversight: the
           kit was written when there was only ever one character. */
        inventoryStartingKit(s.inventory);
        s.connected = true;
        s.local = local;
        s.networkId = (PlayerId)i; /* authoritative host slots map 1:1 */
        return (PlayerId)i;
    }
    return PLAYER_NONE;
}

void playerSessionClose(PlayerId id) {
    if (id == LOCAL_PLAYER_ID || id >= MAX_PLAYERS) return;
    PlayerSession& s = g_playerSessions[id];
    for (int i = 0; i < INV_SLOTS; ++i) if (s.inventory.slot[i].inst) toolInstFree(s.inventory.slot[i].inst);
    for (int i = 0; i < EQ_COUNT; ++i) if (s.inventory.equip[i].inst) toolInstFree(s.inventory.equip[i].inst);
    for (int d = 0; d < DRONE_BAY_COUNT; ++d)
        for (int m = 0; m < Inventory::DRONE_MODULE_SLOTS_MAX; ++m)
            if (s.inventory.droneModule[d][m].inst) toolInstFree(s.inventory.droneModule[d][m].inst);
    if (s.cursor.inst) toolInstFree(s.cursor.inst);
    if (s.trash.inst) toolInstFree(s.trash.inst);
    ++s.generation;
    if (!s.generation) ++s.generation;
    clearSession(s);
}

bool playerSessionConnected(PlayerId id) {
    return id < MAX_PLAYERS && g_playerSessions[id].connected;
}

int playerSessionSlotForNetworkId(PlayerId networkId) {
    for (int slot = 0; slot < MAX_PLAYERS; ++slot)
        if (g_playerSessions[slot].connected &&
            g_playerSessions[slot].networkId == networkId) return slot;
    return -1;
}

void playerSessionsReturnToOffline() {
    for (int slot = 1; slot < MAX_PLAYERS; ++slot) playerSessionClose((PlayerId)slot);
    PlayerSession& s = g_playerSessions[0];
    ++s.generation; if (!s.generation) ++s.generation;
    s.connected = true; s.local = true; s.networkId = LOCAL_PLAYER_ID;
    s.previousAimX = s.previousAimY = -1; s.digCooldown = 0;
    s.openDevice = -1; s.wireX = s.wireY = -1;
    s.circuitWireFrom = -1; s.circuitWirePort = 0;
    s.suppressRightUse = s.lineActive = false;
    s.previousCommandBits = 0;
}
