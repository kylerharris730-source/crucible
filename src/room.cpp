#include "room.h"
#include <string.h>

Room g_rooms[MAX_ROOMS];

/* The fill's scratch space. Both are sized from the caps in room.h rather than
   from the world, which is the point of having caps: room detection uses a
   fixed 8 KB and 16 KB no matter how big the world gets or how much of it you
   have built in. */
static const int ROOM_SIDE = ROOM_REACH * 2 + 1;
static u8  g_seen[(ROOM_SIDE * ROOM_SIDE + 7) / 8];
/* Pushed-but-not-popped cells can never exceed the number of cells the fill
   has accepted, so a stack this size cannot overflow on a room that is going
   to succeed. If it ever would, the room is over cap and failing is the right
   answer anyway -- which is why the overflow branch below is a plain `return
   false` rather than something defensive. */
static i32 g_stack[ROOM_MAX_CELLS + 64];

/* Passable to AIR, not to the player. See the note on doors in room.h. */
static inline bool roomOpen(u8 m) {
    return m == MAT_EMPTY || MATS[m].kind == KIND_GAS;
}

static const int RDX[4] = { 0, 0, -1, 1 };
static const int RDY[4] = { -1, 1, 0, 0 };

bool roomScan(const World& w, int sx, int sy, Room* out) {
    if (sx < PLAY_X0 || sx > PLAY_X1 || sy < PLAY_Y0 || sy > PLAY_Y1) return false;
    if (!roomOpen(w.at(sx, sy).mat)) return false;
    /* The seed itself must be backed, or every scan from open ground would
       walk the entire outdoors before the cap stopped it. Cheapest possible
       rejection of the overwhelmingly common case. */
    if (!w.bgPlaced(sx, sy)) return false;

    const int bx0 = sx - ROOM_REACH, by0 = sy - ROOM_REACH;
    memset(g_seen, 0, sizeof(g_seen));

    int sp = 0, n = 0;
    int rx0 = sx, ry0 = sy, rx1 = sx, ry1 = sy;

    const int seedLocal = ROOM_REACH * ROOM_SIDE + ROOM_REACH;
    g_seen[seedLocal >> 3] |= (u8)(1 << (seedLocal & 7));
    g_stack[sp++] = seedLocal;

    while (sp > 0) {
        const int li = g_stack[--sp];
        const int lx = li % ROOM_SIDE, ly = li / ROOM_SIDE;
        const int wx = bx0 + lx,       wy = by0 + ly;

        if (++n > ROOM_MAX_CELLS) return false;
        if (wx < rx0) rx0 = wx;
        if (wx > rx1) rx1 = wx;
        if (wy < ry0) ry0 = wy;
        if (wy > ry1) ry1 = wy;

        for (int k = 0; k < 4; ++k) {
            const int nx = wx + RDX[k], ny = wy + RDY[k];
            /* Off the world is the world's border wall, which seals like any
               other solid. Tested here only so the read below is in bounds. */
            if (nx < 0 || nx >= SIM_W || ny < 0 || ny >= SIM_H) continue;
            if (!roomOpen(w.cells[ny * SIM_W + nx].mat)) continue;   /* sealed */

            /* Open air with nothing you placed behind it is the outdoors, or a
               natural cave. Either way this is not an enclosure you built, and
               no amount of further searching will make it one -- so fail
               outright rather than carrying on and reporting a smaller room
               than the space really is. */
            if (!w.bgPlaced(nx, ny)) return false;

            const int nlx = lx + RDX[k], nly = ly + RDY[k];
            if (nlx < 0 || nlx >= ROOM_SIDE || nly < 0 || nly >= ROOM_SIDE) return false;

            const int nli = nly * ROOM_SIDE + nlx;
            const int byte = nli >> 3, bit = 1 << (nli & 7);
            if (g_seen[byte] & bit) continue;
            g_seen[byte] |= (u8)bit;

            if (sp >= (int)(sizeof(g_stack) / sizeof(g_stack[0]))) return false;
            g_stack[sp++] = nli;
        }
    }

    if (n < ROOM_MIN_CELLS) return false;

    if (out) {
        out->seedX = sx; out->seedY = sy;
        out->x0 = rx0; out->y0 = ry0; out->x1 = rx1; out->y1 = ry1;
        out->cells = n;
        out->used  = true;
    }
    return true;
}

int roomCount() {
    int n = 0;
    for (int i = 0; i < MAX_ROOMS; ++i) if (g_rooms[i].used) ++n;
    return n;
}

static inline bool inBounds(const Room& r, int x, int y, int grow) {
    return x >= r.x0 - grow && x <= r.x1 + grow
        && y >= r.y0 - grow && y <= r.y1 + grow;
}

int roomAt(int x, int y) {
    for (int i = 0; i < MAX_ROOMS; ++i)
        if (g_rooms[i].used && inBounds(g_rooms[i], x, y, 0)) return i;
    return -1;
}

/* Rebuilt wholesale rather than incrementally. Rooms overlap chunks and each
   other's chunks, so removing one room's flags is not a matter of clearing the
   chunks it covered -- a neighbouring room may need half of them. Recomputing
   from the room list is 12 KB of memset and at most 64 small loops, it happens
   only when a room appears or disappears, and it cannot drift out of step with
   the truth the way a running total can. */
static void applyKeepAlive(World& w) {
    memset(w.keepAlive, 0, sizeof(w.keepAlive));
    int kept = 0;
    for (int i = 0; i < MAX_ROOMS; ++i) {
        const Room& r = g_rooms[i];
        if (!r.used) continue;
        const int cx0 = imax(0, r.x0 >> CHUNK_SHIFT), cx1 = imin(CHUNKS_X - 1, r.x1 >> CHUNK_SHIFT);
        const int cy0 = imax(0, r.y0 >> CHUNK_SHIFT), cy1 = imin(CHUNKS_Y - 1, r.y1 >> CHUNK_SHIFT);
        for (int cy = cy0; cy <= cy1; ++cy)
            for (int cx = cx0; cx <= cx1; ++cx) {
                u8& f = w.keepAlive[cy * CHUNKS_X + cx];
                if (!f) { f = 1; ++kept; }
            }
    }
    w.keptChunks = kept;
}

void roomsClear(World& w) {
    memset(g_rooms, 0, sizeof(g_rooms));
    applyKeepAlive(w);
}

int roomRegister(World& w, int x, int y) {
    /* The centre and its four neighbours. The cell you place to CLOSE a room is
       part of the wall by definition, so a scan that only ever started where
       you clicked would never find the room you just finished. */
    /* Include diagonal neighbours too. A final corner of a hand-drawn room is
       diagonally adjacent to its interior, and the old cross-only probe made a
       perfectly sealed room wait for a later unrelated edit before registering. */
    for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
        Room r;
        if (!roomScan(w, x + ox, y + oy, &r)) continue;

        /* Already known? Two seeds inside the same enclosure describe the same
           room, so match on containment rather than on the seed being equal --
           otherwise clicking twice in one room would fill the table with
           copies of it. */
        int slot = -1;
        for (int i = 0; i < MAX_ROOMS; ++i)
            if (g_rooms[i].used && inBounds(r, g_rooms[i].seedX, g_rooms[i].seedY, 0)) { slot = i; break; }
        if (slot < 0)
            for (int i = 0; i < MAX_ROOMS; ++i)
                if (!g_rooms[i].used) { slot = i; break; }
        if (slot < 0) return -1;          /* table full: the cap doing its job */

        const Room old = g_rooms[slot];
        g_rooms[slot] = r;
        if (!old.used || old.x0 != r.x0 || old.y0 != r.y0 || old.x1 != r.x1 || old.y1 != r.y1)
            applyKeepAlive(w);
        return slot;
    }
    return -1;
}

void roomsNotifyEdit(World& w, int x, int y) {
    bool changed = false;

    /* Anything that could plausibly have broken a room gets that room checked
       now rather than waiting for its turn in the round robin. Grown by one
       cell because the edit that breaches a room is in its WALL, which is
       outside the interior bounds. */
    for (int i = 0; i < MAX_ROOMS; ++i) {
        Room& r = g_rooms[i];
        if (!r.used || !inBounds(r, x, y, 2)) continue;
        Room fresh;
        if (roomScan(w, r.seedX, r.seedY, &fresh)) {
            if (fresh.x0 != r.x0 || fresh.y0 != r.y0 || fresh.x1 != r.x1 || fresh.y1 != r.y1)
                changed = true;
            r = fresh;
        } else {
            /* The seed may simply have been built over while the room around
               it is intact. Dropping it is still right: roomRegister below
               re-finds the room from the edit point on this same call, with a
               seed that exists. */
            r.used = false;
            changed = true;
        }
    }
    if (changed) applyKeepAlive(w);

    roomRegister(w, x, y);
}

void roomsTick(World& w) {
    static int cursor = 0;
    if ((w.frame % ROOM_RECHECK) != 0) return;

    for (int tries = 0; tries < MAX_ROOMS; ++tries) {
        const int i = cursor;
        cursor = (cursor + 1) % MAX_ROOMS;
        Room& r = g_rooms[i];
        if (!r.used) continue;

        Room fresh;
        if (roomScan(w, r.seedX, r.seedY, &fresh)) {
            const bool moved = fresh.x0 != r.x0 || fresh.y0 != r.y0
                            || fresh.x1 != r.x1 || fresh.y1 != r.y1;
            r = fresh;
            if (moved) applyKeepAlive(w);
        } else {
            r.used = false;
            applyKeepAlive(w);
        }
        return;                 /* exactly one scan per tick, whatever happens */
    }
}
