#include "save.h"
#include "map.h"
#include "entity.h"
#include "light.h"
#include "player.h"
#include "item.h"
#include "room.h"
#include "device.h"
#include "tree.h"
#include "worldgen.h"
#include "multiplayer.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

static char g_err[256] = "";
const char* saveError() { return g_err; }

/* See the note in save.h. Deliberately fire-and-forget: FS.syncfs is
   asynchronous, and there is nothing useful for the game to do while it
   settles -- the bytes are already in the filesystem the game reads back, so a
   load in this same session works whether or not IndexedDB has caught up yet.

   The page supplies the function so that the retry and the reporting live with
   the rest of the page's error handling rather than being duplicated here; the
   direct syncfs is the fallback for a build loaded without it. */
void savePersist() {
#ifdef __EMSCRIPTEN__
    EM_ASM({
        if (typeof cinderliftPersistSaves === 'function') { cinderliftPersistSaves(); return; }
        if (typeof FS !== 'undefined' && FS.syncfs)
            FS.syncfs(false, function (e) { if (e) console.error('save persist failed', e); });
    });
#endif
}

/* ==========================================================================
   Section bookkeeping
   ========================================================================== */

static const int MAX_STATS = 16;
static SaveStat g_stats[MAX_STATS];
static int      g_nStats = 0;
static u64      g_total  = 0;

int             saveStatCount() { return g_nStats; }
const SaveStat* saveStats()     { return g_stats; }
u64             saveTotalBytes(){ return g_total; }

static void statAdd(const char* name, u64 bytes) {
    if (g_nStats < MAX_STATS) { g_stats[g_nStats].name = name;
                                g_stats[g_nStats].bytes = bytes; ++g_nStats; }
    g_total += bytes;
}
static void statSort() {
    for (int i = 1; i < g_nStats; ++i)
        for (int j = i; j > 0 && g_stats[j].bytes > g_stats[j-1].bytes; --j) {
            const SaveStat t = g_stats[j]; g_stats[j] = g_stats[j-1]; g_stats[j-1] = t;
        }
}

/* ==========================================================================
   Run-length encoding, one PLANE at a time
   ==========================================================================

   Per plane and not per Cell, and that is the whole reason this works. A Cell
   is mat, moisture, tint and flags interleaved; the tint byte is random per
   cell, so encoding Cells as 4-byte units finds a run length of one nearly
   everywhere and the output is larger than the input. Split apart, `mat` is
   enormous runs of stone and air, `moisture` is almost entirely zero, and the
   noise is confined to the plane that has to carry it.

   [count:u16][value:u8], count 1..65535. Two bytes a run means a plane of one
   value costs 2 bytes per 65535 cells -- about 400 bytes for the whole world --
   and a plane of pure noise costs 3 bytes per 1: worse than raw, which is why
   nothing incompressible is put through here. */
/* --- speed -----------------------------------------------------------------
   Saving and loading used to take about 190 ms each, which was always a pause
   and became a visible hitch once the game started autosaving every few
   minutes. Measured before anything was changed, and the time was NOT the disk:

     scanning 151 MB for runs, one byte at a time     ~140-210 ms
     copying material and moisture out of the cells    ~35-45 ms
     writing the result (well under a megabyte)            < 1 ms

   The world compresses to about a quarter of a million runs over 151 million
   bytes, so almost all of that scan was walking along long stretches of the
   same rock, air or temperature one byte at a time. Two changes, and neither
   touches the file format -- a save written now is byte-for-byte what the old
   code wrote, and every save written by an old build loads exactly as before:

     1. the scan compares EIGHT bytes at once and steps over a run a word at a
        time, which is about six times faster on its own;
     2. the four world layers are independent, so on Windows each is encoded
        on its own thread, and on load the per-cell passes are split across
        threads by cell range.

   The browser build is single-threaded, so it gets the first change only. */

/* One job per worker. A thread per job rather than a pool: a save or a load
   runs every few minutes at most, and a thread costs a fraction of a
   millisecond to start, which is nothing against what it saves. */
struct SaveJob {
    void (*fn)(void*);
    void* arg;
};

#ifdef _WIN32
static DWORD WINAPI saveJobThunk(LPVOID p) {
    SaveJob* job = (SaveJob*)p;
    job->fn(job->arg);
    return 0;
}
#endif

/* Run every job and return when all are finished. This thread does the first
   one itself rather than sitting idle. A thread that cannot be created is not
   an error -- its job simply runs here afterwards -- so a machine that refuses
   threads still saves, just slowly. */
static void runSaveJobs(SaveJob* jobs, int n) {
#ifdef _WIN32
    HANDLE threads[16] = { 0 };
    for (int i = 1; i < n && i < 16; ++i)
        threads[i] = CreateThread(0, 0, saveJobThunk, &jobs[i], 0, 0);
    if (n > 0) jobs[0].fn(jobs[0].arg);
    for (int i = 1; i < n && i < 16; ++i) {
        if (threads[i]) {
            WaitForSingleObject(threads[i], INFINITE);
            CloseHandle(threads[i]);
        } else {
            jobs[i].fn(jobs[i].arg);
        }
    }
#else
    for (int i = 0; i < n; ++i) jobs[i].fn(jobs[i].arg);
#endif
}

/* How many threads a per-cell pass is split across. Capped, because past a
   handful the passes are limited by memory bandwidth rather than cores. */
static int saveWorkers() {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    int n = (int)info.dwNumberOfProcessors;
    return n < 1 ? 1 : (n > 8 ? 8 : n);
#else
    return 1;
#endif
}

/* fn(ctx, lo, hi) over [0, n) split into saveWorkers() pieces. Only for passes
   where every cell is independent of every other -- which is all of them here:
   each reads shared tables and writes its own index. */
struct RangeJob {
    void (*fn)(void*, u64, u64);
    void* ctx;
    u64 lo, hi;
};
static void rangeThunk(void* p) {
    RangeJob* r = (RangeJob*)p;
    r->fn(r->ctx, r->lo, r->hi);
}
static void parallelCells(u64 n, void (*fn)(void*, u64, u64), void* ctx) {
    const int k = saveWorkers();
    RangeJob ranges[16];
    SaveJob jobs[16];
    for (int i = 0; i < k; ++i) {
        ranges[i].fn = fn; ranges[i].ctx = ctx;
        ranges[i].lo = n * (u64)i / (u64)k;
        ranges[i].hi = n * (u64)(i + 1) / (u64)k;
        jobs[i].fn = rangeThunk; jobs[i].arg = &ranges[i];
    }
    runSaveJobs(jobs, k);
}

/* Run-length encode into memory: a u16 count and a byte, per run, runs capped
   at 65535. Exactly the stream the old one-byte-at-a-time writer produced --
   greedy maximal runs, split at the cap -- because that IS the file format,
   and changing it would make every save in existence a different file.

   The inner loop is the whole speed-up. Eight copies of the run's byte make a
   single 64-bit pattern, and a word of the input equal to it extends the run
   by eight; the byte loop after it finishes the last few. memcpy into a u64 is
   how you read eight bytes without an alignment or aliasing problem, and every
   compiler turns it into one load. */
static void rleEncode(const u8* src, u64 n, std::vector<u8>& out) {
    out.clear();
    u64 i = 0;
    while (i < n) {
        const u8 v = src[i];
        const u64 pattern = 0x0101010101010101ull * v;
        u64 run = 1;
        while (run + 8 <= 65535 && i + run + 8 <= n) {
            u64 word;
            memcpy(&word, src + i + run, 8);
            if (word != pattern) break;
            run += 8;
        }
        while (i + run < n && src[i + run] == v && run < 65535) ++run;
        const u16 c = (u16)run;
        u8 rec[3];
        memcpy(rec, &c, sizeof(c));   /* host order, as fwrite(&c) always wrote it */
        rec[2] = v;
        out.insert(out.end(), rec, rec + 3);
        i += run;
    }
}

/* The reverse, from a section already in memory. The same two refusals the old
   reader made -- a zero count, or a run past the end of the plane -- plus one it
   could not: a section that ends before the plane is full is an error here
   rather than a read that wanders on into the next section's bytes. */
static bool rleDecode(const u8* in, u64 bytes, u8* dst, u64 n) {
    u64 i = 0, at = 0;
    while (i < n) {
        if (at + 3 > bytes) return false;
        u16 c;
        memcpy(&c, in + at, sizeof(c));
        const u8 v = in[at + 2];
        at += 3;
        if (c == 0 || (u64)c > n - i) return false;
        memset(dst + i, v, c);
        i += c;
    }
    return true;
}

/* A whole RLE section off the disk in one read, then decoded. Two freads per
   run was a quarter of a million library calls per plane. The length comes
   from the section's own framing, and is bounded before anything is allocated:
   a real plane cannot need more than three bytes per cell. */
static bool rleReadSection(FILE* f, u64 len, u8* dst, u64 n) {
    if (len == 0 || len > n * 3) return false;
    std::vector<u8> buf((size_t)len);
    if (fread(&buf[0], 1, (size_t)len, f) != (size_t)len) return false;
    return rleDecode(&buf[0], len, dst, n);
}

/* ==========================================================================
   Framing
   ========================================================================== */

static u32 fourcc(const char* s) {
    return (u32)s[0] | ((u32)s[1] << 8) | ((u32)s[2] << 16) | ((u32)s[3] << 24);
}

/* A section is written with its length patched in afterwards, because most of
   them do not know their own size until they have produced it -- an RLE plane
   least of all. Seek back, write the count, seek forward. */
struct SectionWriter {
    FILE* f; long lenPos; const char* name;
    void begin(FILE* file, const char* tag, const char* label) {
        f = file; name = label;
        const u32 t = fourcc(tag);
        fwrite(&t, sizeof(t), 1, f);
        lenPos = ftell(f);
        const u64 zero = 0;
        fwrite(&zero, sizeof(zero), 1, f);
    }
    void end() {
        const long here = ftell(f);
        const u64 len = (u64)(here - lenPos - (long)sizeof(u64));
        fseek(f, lenPos, SEEK_SET);
        fwrite(&len, sizeof(len), 1, f);
        fseek(f, here, SEEK_SET);
        statAdd(name, len + 12);      /* payload, plus tag and length */
    }
};

/* ==========================================================================
   The material name table
   ========================================================================== */

static void writeMatTable(FILE* f) {
    SectionWriter s; s.begin(f, "MATS", "material names");
    const u16 n = (u16)MAT_COUNT;
    fwrite(&n, sizeof(n), 1, f);
    for (int m = 0; m < MAT_COUNT; ++m) {
        const char* nm = MATS[m].name ? MATS[m].name : "";
        u8 len = (u8)strlen(nm);
        if (len > 63) len = 63;
        const u8 id = (u8)m;
        fwrite(&id, 1, 1, f);
        fwrite(&len, 1, 1, f);
        fwrite(nm, 1, len, f);
    }
    s.end();
}

/* saved id -> current id. Anything whose name no longer exists becomes
   MAT_EMPTY: a material that was deleted between saves is genuinely gone, and
   air is the one substitution that cannot break anything downstream -- it
   cannot fall, burn, block, or be mistaken for something valuable. */
static u8  g_remap[256];
static int g_lostMats = 0;
static int g_savedMatCount = 0;

static bool readMatTable(FILE* f, u64 len) {
    for (int i = 0; i < 256; ++i) g_remap[i] = MAT_EMPTY;
    g_lostMats = 0;
    const long end = ftell(f) + (long)len;

    u16 n;
    if (fread(&n, sizeof(n), 1, f) != 1) return false;
    g_savedMatCount = n;
    for (int i = 0; i < (int)n; ++i) {
        u8 id, len8;
        if (fread(&id, 1, 1, f) != 1) return false;
        if (fread(&len8, 1, 1, f) != 1) return false;
        char nm[64];
        if (len8 && fread(nm, 1, len8, f) != len8) return false;
        nm[len8] = 0;

        u8 to = MAT_EMPTY; bool found = false;
        for (int m = 0; m < MAT_COUNT; ++m)
            if (MATS[m].name && strcmp(MATS[m].name, nm) == 0) { to = (u8)m; found = true; break; }
        /* Names a material used to have. Keying on the name makes moving a
           material free and RENAMING one the breaking operation -- which is
           the trade, and it is the right one, but a rename should still not
           silently turn a world to air.

           This is a list of identities, not a migration: it says "this used to
           be called that", and nothing about meaning. When a rename genuinely
           changes what a material IS, the old name belongs in neither column
           and the cells should go. */
        if (!found) {
            static const struct { const char* was; u8 now; } ALIAS[] = {
                { "Tree Seed", MAT_OAK_SEED    },
                { "Sapling",   MAT_OAK_SAPLING },
                { "Leaves",    MAT_OAK_LEAF    },
                { "Seed Pod",  MAT_OAK_POD     },
            };
            for (unsigned a = 0; a < sizeof(ALIAS) / sizeof(ALIAS[0]); ++a)
                if (strcmp(ALIAS[a].was, nm) == 0) { to = ALIAS[a].now; found = true; break; }
        }
        if (!found && len8) ++g_lostMats;
        g_remap[id] = to;
    }
    fseek(f, end, SEEK_SET);
    return true;
}

/* Items deliberately start immediately after MAT_COUNT. Material ids are saved
   by name, but the non-material tail used to be read raw; appending a material
   therefore shifted every tool, drone and device item by one on load. Keep the
   material half name-mapped and move the item tail by the boundary change. */
static ItemId remapSavedItem(ItemId old) {
    if (old == ITEM_NONE) return ITEM_NONE;
    if (old < g_savedMatCount) return (ItemId)g_remap[old];
    const int now = (int)old + MAT_COUNT - g_savedMatCount;
    if (now == ITEM_FLOWER_SEED_LEGACY) return ITEM_FLOWER_SEED;
    return (now >= MAT_COUNT && now < ITEM_COUNT) ? (ItemId)now : ITEM_NONE;
}

/* Taken by reference so the roster's inventories get the same treatment as
   the host's. A saved player carrying iron in a world written before a new
   material existed needs their ids shifted exactly as g_inv does, and
   forgetting them would hand somebody back a pack of the wrong things. */
static void remapStacksIn(Inventory& inv) {
    for (int i = 0; i < INV_SLOTS; ++i) inv.slot[i].item = remapSavedItem(inv.slot[i].item);
    for (int i = 0; i < EQ_COUNT; ++i) inv.equip[i].item = remapSavedItem(inv.equip[i].item);
    for (int d = 0; d < DRONE_BAY_COUNT; ++d)
        for (int i = 0; i < Inventory::DRONE_MODULE_SLOTS_MAX; ++i)
            inv.droneModule[d][i].item = remapSavedItem(inv.droneModule[d][i].item);
}

static void remapInventoryItems() { remapStacksIn(g_inv); }

static void remapToolItems() {
    for (int i = 0; i < MAX_TOOL_INST; ++i) {
        for (int s = 0; s < TOOL_SLOTS_MAX; ++s) g_toolInst[i].slot[s] = remapSavedItem(g_toolInst[i].slot[s]);
        g_toolInst[i].payload.item = remapSavedItem(g_toolInst[i].payload.item);
    }
}

/* ==========================================================================
   Writing
   ========================================================================== */

static u8 g_plane[SIM_W * SIM_H];
/* The moisture plane's own buffer, so material and moisture can be pulled out
   of the cells on two threads at once. */
static u8 g_plane2[SIM_W * SIM_H];

/* One world layer to encode. `copyFrom` is set for the two that live inside
   the Cell struct and have to be pulled out first; temperature and backdrop are
   already flat arrays and are encoded where they sit. */
struct PlaneJob {
    const World* w;
    int which;               /* 0 material, 1 moisture, 2 temperature, 3 backdrop */
    std::vector<u8> encoded;
};
static void encodePlane(void* p) {
    PlaneJob* job = (PlaneJob*)p;
    const u64 n = (u64)SIM_W * SIM_H;
    const World& w = *job->w;
    switch (job->which) {
    case 0:
        for (u64 i = 0; i < n; ++i) g_plane[i] = w.cells[i].mat;
        rleEncode(g_plane, n, job->encoded);
        break;
    case 1:
        for (u64 i = 0; i < n; ++i) g_plane2[i] = w.cells[i].moisture;
        rleEncode(g_plane2, n, job->encoded);
        break;
    case 2: rleEncode(w.temp, n, job->encoded); break;
    default: rleEncode(w.bg, n, job->encoded); break;
    }
}

bool saveWrite(const char* path, const World& w, const u8* thumbRgb) {
    g_nStats = 0; g_total = 0; g_err[0] = 0;

    FILE* f = fopen(path, "wb");
    if (!f) { sprintf(g_err, "could not open %s for writing", path); return false; }

    const u32 magic = fourcc("CRUC");
    const u32 ver   = SAVE_VERSION;
    const i32 dims[2] = { SIM_W, SIM_H };
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&ver,   sizeof(ver),   1, f);
    fwrite(dims,   sizeof(dims),  1, f);
    statAdd("header", sizeof(magic) + sizeof(ver) + sizeof(dims));

    /* --- the two sections the save screen reads ------------------------
       Written FIRST, immediately after the header, and that placement is the
       whole reason the save screen is usable. savePeek walks sections by their
       length framing, so anything before the world planes costs two seeks to
       reach and anything after them costs a walk past fifty megabytes -- times
       ten slots, every time the screen opens.

       They are still ordinary framed sections, so an older build that does not
       know these tags skips them exactly as the format intends. */
    {
        SectionWriter s; s.begin(f, "META", "timestamp");
        const i64 when = (i64)time(0);
        fwrite(&when, sizeof(when), 1, f);
        s.end();
    }
    if (thumbRgb) {
        SectionWriter s; s.begin(f, "SHOT", "thumbnail");
        const i32 tw = SAVE_THUMB_W, th = SAVE_THUMB_H;
        fwrite(&tw, sizeof(tw), 1, f);
        fwrite(&th, sizeof(th), 1, f);
        fwrite(thumbRgb, 1, SAVE_THUMB_BYTES, f);
        s.end();
    }

    writeMatTable(f);

    /* All four layers at once. Sections still go into the file in the same
       order, from the finished buffers; only the encoding is concurrent. */
    PlaneJob planes[4];
    SaveJob planeJobs[4];
    for (int i = 0; i < 4; ++i) {
        planes[i].w = &w;
        planes[i].which = i;
        planeJobs[i].fn = encodePlane;
        planeJobs[i].arg = &planes[i];
    }
    runSaveJobs(planeJobs, 4);
    struct { const char* tag; const char* label; } const PLANE_SECTIONS[4] = {
        { "CMAT", "cell material" }, { "CMOI", "cell moisture" },
        { "TEMP", "temperature" },   { "BGND", "background" }
    };

    /* --- the three big planes ---------------------------------------- */
    for (int i = 0; i < 2; ++i) {
        SectionWriter s; s.begin(f, PLANE_SECTIONS[i].tag, PLANE_SECTIONS[i].label);
        fwrite(&planes[i].encoded[0], 1, planes[i].encoded.size(), f);
        s.end();
    }
    /* Tint is NOT saved, and flags are not either.

       Tint is per-cell colour jitter -- the speckle that stops a slab of stone
       being one flat colour. It is random per cell, so it is the one plane RLE
       cannot touch: stored it would be 12.6 MB, which is more than everything
       else in this file put together, to preserve noise nobody could identify
       in a screenshot. It is re-rolled on load instead. The world looks
       statistically identical and not bit-identical, which is the right trade
       for a twelve-megabyte saving.

       Flags carry the direction bit and a frame stamp -- scheduling state for
       the frame that was in progress. Zeroing them on load costs at most one
       frame of settling. */
    for (int i = 2; i < 4; ++i) {
        SectionWriter s; s.begin(f, PLANE_SECTIONS[i].tag, PLANE_SECTIONS[i].label);
        fwrite(&planes[i].encoded[0], 1, planes[i].encoded.size(), f);
        s.end();
    }
    {
        SectionWriter s; s.begin(f, "ZONE", "zones");
        fwrite(w.zone, 1, CHUNK_COUNT, f);
        s.end();
    }
    /* The height maps. Derived from generation, but the player digs, so they
       cannot simply be regenerated -- and at 32 KB they are not worth being
       clever about. */
    {
        SectionWriter s; s.begin(f, "HGHT", "height maps");
        fwrite(g_surfaceY, sizeof(int), SIM_W, f);
        fwrite(g_stoneY,   sizeof(int), SIM_W, f);
        s.end();
    }

    /* --- entities ---------------------------------------------------- */
    {
        SectionWriter s; s.begin(f, "PLYR", "character");
        fwrite(&g_player, sizeof(Player), 1, f);
        s.end();
    }
    {
        /* Session state kept out of Player so adding it does not invalidate
           every existing raw PLYR section. Optional framing gives old saves
           the natural fallback: no bed, therefore world spawn. */
        SectionWriter s; s.begin(f, "RSPN", "respawn checkpoint");
        const i32 checkpoint[3] = { g_playerSessions[0].respawnBedX,
                                    g_playerSessions[0].respawnBedY,
                                    g_playerSessions[0].healCooldown };
        fwrite(checkpoint, sizeof(checkpoint), 1, f);
        s.end();
    }
    {
        SectionWriter s; s.begin(f, "INVN", "inventory");
        fwrite(&g_inv, sizeof(Inventory), 1, f);
        s.end();
    }
    {
        /* Everyone the host has played with, and what they were carrying.

           A guest's pack used to exist only for the length of their
           connection, so every visit began from nothing. This is the part
           that makes it survive the HOST restarting as well as the guest
           leaving -- without it the roster would be a session cache and the
           first thing to evaporate on the evening you stop playing.

           Only occupied entries are written, so an empty roster costs four
           bytes. Adding a section does not move SAVE_VERSION -- see the note
           on it -- which is the whole reason this can be added to a format
           people already have worlds in. */
        SectionWriter s; s.begin(f, "PLRS", "remembered players");
        u32 n = 0;
        for (int i = 0; i < MAX_REMEMBERED; ++i) if (g_roster[i].used) ++n;
        fwrite(&n, sizeof(n), 1, f);
        for (int i = 0; i < MAX_REMEMBERED; ++i)
            if (g_roster[i].used) fwrite(&g_roster[i], sizeof(RememberedPlayer), 1, f);
        s.end();
    }
    {
        /* The tool instance pool. INVN already stores each stack's inst ID, so
           without this the save was internally inconsistent: it remembered
           WHICH instance a multitool owned and nothing about what was in it.
           Every module you had fitted and every payload you had loaded was
           dropped on the floor by a save, silently.

           Worse than losing them, it froze the tool. See toolInstReconcile. */
        SectionWriter s; s.begin(f, "TOOL", "tool loadouts");
        fwrite(g_toolInst, sizeof(ToolInst), MAX_TOOL_INST, f);
        s.end();
    }
    {
        /* Where you have been. 576 KB raw, which is nothing beside the world
           plane and not worth compressing -- an explored map is mostly
           non-repeating material ids, so RLE would buy little and cost a second
           encoder to get wrong. */
        SectionWriter s; s.begin(f, "MAPX", "explored map");
        fwrite(g_map, 1, sizeof(g_map), f);
        s.end();
    }
    {
        SectionWriter s; s.begin(f, "DEVS", "machines");
        fwrite(g_devices, sizeof(Device), MAX_DEVICES, f);
        /* A spark is an in-flight simulation front, not durable machine state.
           Its pulse-mark field is deliberately not saved, so restoring a front
           alone lets it revisit a wire as a new pulse and turn a clean reload
           into an overload. Devices and their stored contents persist; current
           simply dissipates at the save boundary. */
        s.end();
    }
    {
        /* Whether a rocket has left this world. Four bytes, in a section of
           its own rather than as a field on something -- a world made before
           the ending existed simply has no WON tag and loads with the flag
           false, which is exactly right and needed no version gate. See the
           note on SAVE_VERSION. */
        SectionWriter s; s.begin(f, "WON ", "victory");
        const u32 won = rocketVictory() ? 1u : 0u;
        fwrite(&won, sizeof(won), 1, f);
        s.end();
    }
    {
        SectionWriter s; s.begin(f, "TORC", "torch fixtures");
        const i32 count = torchCount();
        fwrite(&count, sizeof(count), 1, f);
        if (count > 0) fwrite(torchData(), sizeof(TorchFixture), count, f);
        s.end();
    }
    {
        /* Circuit topology is its own additive section rather than extra bytes
           in Device. Older saves can still load their machine structs exactly;
           they simply arrive with no wires and default signal settings. */
        SectionWriter s; s.begin(f, "CIRC", "circuit wires");
        fwrite(g_circuitConfig, sizeof(CircuitDeviceConfig), MAX_DEVICES, f);
        fwrite(g_circuitWires, sizeof(CircuitWire), MAX_CIRCUIT_WIRES, f);
        s.end();
    }
    {
        SectionWriter s; s.begin(f, "ROOM", "rooms");
        fwrite(g_rooms, sizeof(Room), MAX_ROOMS, f);
        s.end();
    }
    {
        SectionWriter s; s.begin(f, "TREE", "growing trees");
        fwrite(g_trees, sizeof(Tree), MAX_TREES, f);
        s.end();
    }
    {
        /* The time of day, and nothing else about creatures.

           Enemies are deliberately NOT written -- see the note at the top of
           entity.h. They respawn out of the dark, so preserving them would buy
           nothing and would cost a versioned section that could be wrong. The
           CLOCK is different: a world that reloads at noon every time does not
           have a day/night cycle, it has a lighting effect, and four bytes is
           the whole price of it being real. */
        SectionWriter s; s.begin(f, "TIME", "time of day");
        fwrite(&g_worldTime, sizeof(g_worldTime), 1, f);
        s.end();
    }
    {
        /* Which bosses have been beaten -- the ONLY thing about creatures that
           is written at all. Everything else respawns from the dark and is
           deliberately not saved (entity.h); this is the one fact that must
           not, because a boss you have to kill twice is a boss whose reward
           was never really a reward. Four bytes. */
        SectionWriter s; s.begin(f, "BOSS", "bosses beaten");
        fwrite(&g_bossesBeaten, sizeof(g_bossesBeaten), 1, f);
        s.end();
    }
    {
        /* Where you have been -- see seenAt in light.h. Somewhere you have lit
           is drawn bright forever, and "forever" has to survive quitting or the
           whole world goes black again on the next load.

           RLE like the world planes, and it is the best case that encoding has:
           a bitmap that is one long run of zeroes everywhere you have not been.
           288 KB raw, a few hundred bytes for a world you have just started. */
        SectionWriter s; s.begin(f, "SEEN", "explored");
        std::vector<u8> encoded;
        rleEncode(seenData(), SEEN_BYTES, encoded);
        fwrite(&encoded[0], 1, encoded.size(), f);
        s.end();
    }

    const u32 endTag = 0;
    fwrite(&endTag, sizeof(endTag), 1, f);
    statAdd("end", sizeof(endTag));

    const bool ok = ferror(f) == 0;
    fclose(f);
    if (!ok) { sprintf(g_err, "write failed part way through %s", path); return false; }
    statSort();
    return true;
}

/* ==========================================================================
   Reading
   ========================================================================== */

/* Remap a material plane through the name table. */
/* --- the speckle -----------------------------------------------------------
   Every cell carries a random tint byte that the colour LUT uses to break up
   flat material into texture. It is NOT saved: it is a byte of pure noise per
   cell, so it is the one plane run-length encoding cannot help with, and 12.6
   million incompressible bytes on every save to preserve something purely
   cosmetic is a bad trade. It is re-derived from the cell index instead, which
   also means a save reloaded twice looks the same both times.

   What it must be is a HASH, and the first version was not one. It was

       h = i * 2654435761 ^ ((i >> 13) * 40503);   tint = h >> 24

   which looks like mixing and is a straight line: multiplying by a constant
   makes the top byte an arithmetic progression in i, and the second term only
   changes every 8192 cells -- twice per row. Loaded ground came back as a
   smooth regular plaid instead of noise, which is exactly what it looked like.

   Measured over a 320x200 patch: real noise has two neighbouring cells share a
   tint 0.39% of the time, and the old formula managed 0.00% -- not "less
   random" but arithmetically impossible for noise, because a ramp never
   repeats a value. That zero is the tell, and it is what this is checked
   against.

   The finalizer below is the one worldgen and the tree shapes already use: a
   multiply, an xor-shift, another multiply, another shift. Every input bit
   reaches every output bit. */
u8 tintAt(u32 i) {
    u32 h = i * 374761393u + 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (u8)(h ^ (h >> 16));
}

/* The per-cell passes a load makes, each one split across threads. Every one
   reads shared tables and g_plane, and writes only its own cell. */
static void loadMaterialRange(void* ctx, u64 lo, u64 hi) {
    World& w = *(World*)ctx;
    for (u64 i = lo; i < hi; ++i) {
        const u8 m = g_remap[g_plane[i]];
        w.cells[i].mat   = m;
        w.cells[i].flags = 0;
        w.cells[i].tint  = tintAt((u32)i);
    }
}
static void loadTintRange(void* ctx, u64 lo, u64 hi) {
    World& w = *(World*)ctx;
    for (u64 i = lo; i < hi; ++i) w.cells[i].tint = tintAt((u32)i);
}
static void loadMoistureRange(void* ctx, u64 lo, u64 hi) {
    World& w = *(World*)ctx;
    for (u64 i = lo; i < hi; ++i) {
        /* A sieve's or reactive powder's moisture byte is a fluid material id.
           Remap it by name just like the foreground plane; ordinary moisture is
           a scalar and must remain untouched. */
        const u8 raw = g_plane[i];
        const u8 host = w.cells[i].mat;
        const bool sparseOccupant = host == MAT_SIEVE ||
                                    host == MAT_GAS_SIEVE ||
                                    (MATS[host].kind == KIND_POWDER &&
                                     g_matWetInto[host] != MAT_EMPTY &&
                                     MATS[g_matWetBy[host]].kind == KIND_GAS);
        if (sparseOccupant && raw) {
            const u8 volumeOnly = raw & GAS_VOLUME_ONLY;
            const u8 occupant = raw & GAS_EXCESS_MASK;
            w.cells[i].moisture = (u8)(g_remap[occupant] | volumeOnly);
        } else {
            w.cells[i].moisture = raw;
        }
    }
}
static void loadBackgroundRange(void* ctx, u64 lo, u64 hi) {
    World& w = *(World*)ctx;
    for (u64 i = lo; i < hi; ++i) {
        /* The background stores a material id beside a flag bit, so it needs
           the same remap the foreground got -- and it has to keep the flag
           while doing it. Missing this would repaint every wall you have ever
           built as whatever now sits at that index. */
        const u8 raw = w.bg[i];
        const u8 m   = g_remap[raw & BG_MAT_MASK];
        w.bg[i] = (u8)((m & BG_MAT_MASK) | (raw & BG_PLACED));
    }
}

bool savePeek(const char* path, SaveSlotInfo* out) {
    memset(out, 0, sizeof(*out));

    FILE* f = fopen(path, "rb");
    if (!f) return false;
    out->used = true;

    fseek(f, 0, SEEK_END);
    out->bytes = (u64)ftell(f);
    fseek(f, 0, SEEK_SET);

    u32 magic = 0, ver = 0; i32 dims[2] = { 0, 0 };
    if (fread(&magic, sizeof(magic), 1, f) != 1 ||
        fread(&ver,   sizeof(ver),   1, f) != 1 ||
        fread(dims,   sizeof(dims),  1, f) != 1) {
        sprintf(out->note, "too short to be a save");
        fclose(f); return true;
    }
    if (magic != fourcc("CRUC")) {
        sprintf(out->note, "not a cinderlift save");
        fclose(f); return true;
    }
    if (ver != SAVE_VERSION) {
        sprintf(out->note, "format %u, this build reads %u", ver, SAVE_VERSION);
        fclose(f); return true;
    }
    if (dims[0] != SIM_W || dims[1] != SIM_H) {
        sprintf(out->note, "%dx%d, this build is %dx%d", dims[0], dims[1], SIM_W, SIM_H);
        fclose(f); return true;
    }
    out->readable = true;

    /* Walk the framing, read the two small sections, seek past everything else.
       Stops as soon as both are in hand rather than reading to the end: they
       are written first, so the common case touches a few hundred bytes of a
       fifty-megabyte file. */
    for (;;) {
        u32 tag = 0; u64 len = 0;
        if (fread(&tag, sizeof(tag), 1, f) != 1) break;
        if (tag == 0) break;
        if (fread(&len, sizeof(len), 1, f) != 1) break;
        const long payload = ftell(f);

        if (tag == fourcc("META") && len >= sizeof(i64)) {
            i64 when = 0;
            if (fread(&when, sizeof(when), 1, f) == 1) out->when = when;
        } else if (tag == fourcc("SHOT")) {
            i32 tw = 0, th = 0;
            if (fread(&tw, sizeof(tw), 1, f) == 1 && fread(&th, sizeof(th), 1, f) == 1 &&
                tw == SAVE_THUMB_W && th == SAVE_THUMB_H &&
                len >= (u64)(8 + SAVE_THUMB_BYTES)) {
                if (fread(out->rgb, 1, SAVE_THUMB_BYTES, f) == (size_t)SAVE_THUMB_BYTES)
                    out->hasThumb = true;
            }
        }
        if (out->when && out->hasThumb) break;
        if (fseek(f, payload + (long)len, SEEK_SET) != 0) break;
    }
    fclose(f);
    return true;
}

/* ==========================================================================
   Shapes of files that already exist
   ==========================================================================

   Every struct here describes bytes on somebody's disk rather than anything
   this build uses, which is why they are at file scope beside each other:
   the roster embeds an Inventory too, so the same frozen shape is needed in
   two places and having two copies of it would be one more thing that can
   drift.
   ========================================================================== */

/* FORTY, spelled out, and not INV_SLOTS. Every one of these
   structs describes a file that already exists on somebody's disk,
   so its numbers have to be frozen at what they were when it was
   written -- and INV_SLOTS is exactly the kind of constant that
   looks stable until the day it is not. The pack went from four
   rows to six, and had these said INV_SLOTS they would have
   silently stopped describing any real file, which reads as every
   old save losing its inventory for no visible reason.

   EVERY number here is frozen now, for the same reason and because
   the rule was already broken once: InventoryV3 was written with
   `equip[EQ_COUNT]` and `droneModule[DRONE_BAY_COUNT]`, which was
   correct on the day it was written and stopped being correct the
   moment a fifth trinket slot was added -- at which point V3
   described no file anywhere and every four-row save would have
   quietly lost its equipment. A shape that describes a file on
   disk cannot be spelled with a constant that is still allowed to
   move. */
static const int INV_SLOTS_4ROW = 40;
static const int INV_SLOTS_6ROW = 60;
static const int EQ_SLOTS_V3 = 12;      /* before trinkets 5 and 6 */
static const int DRONE_BAYS_V3 = 4;
static const int DRONE_CHIPS_V3 = 3;
struct InventoryV1 {
    ItemStack slot[INV_SLOTS_4ROW];
    ItemStack equip[9];          /* before EQ_TRINKET_C/D */
    int       selected;
    ItemStack droneModule[3][DRONE_CHIPS_V3];
    u8        droneLevel[3];
};
struct InventoryV2 {
    ItemStack slot[INV_SLOTS_4ROW];
    ItemStack equip[11];         /* before Drone C */
    int       selected;
    ItemStack droneModule[3][DRONE_CHIPS_V3];
    u8        droneLevel[3];
};
/* Four-row pack, twelve equipment slots: the shape between Drone C
   and the sixth trinket. */
struct InventoryV3 {
    ItemStack slot[INV_SLOTS_4ROW];
    ItemStack equip[EQ_SLOTS_V3];
    int       selected;
    ItemStack droneModule[DRONE_BAYS_V3][DRONE_CHIPS_V3];
    u8        droneLevel[DRONE_BAYS_V3];
};
/* Six-row pack, twelve equipment slots. This is the one every save
   written between the pack growing and the trinkets growing will
   match, which today means most of them. */
struct InventoryV4 {
    ItemStack slot[INV_SLOTS_6ROW];
    ItemStack equip[EQ_SLOTS_V3];
    int       selected;
    ItemStack droneModule[DRONE_BAYS_V3][DRONE_CHIPS_V3];
    u8        droneLevel[DRONE_BAYS_V3];
};

/* The roster as it was written before the trinket slots grew, which is the
   only thing about it that changed: it embeds an Inventory by value, so its
   size moved when the pack did. Without this every LAN host's remembered
   guests -- their packs, their tools, where they logged out -- would be
   dropped on the first load after the update, silently and for a reason no
   player could possibly guess at. */
struct RememberedPlayerV1 {
    char        id[PLAYER_IDENTITY_CHARS + 1];
    InventoryV4 inventory;
    ToolInst    tools[REMEMBERED_TOOLS];
    u8          toolCount;
    float       x, y;
    u32         lastSeen;
    bool        used;
};

/* One old pack into a new one. The equipment array is the only part that
   changed length, and the two new slots are simply left empty -- a character
   who has never had a fifth trinket slot cannot have had anything in it. */
static void inventoryFromV4(const InventoryV4& old, Inventory& out) {
    out.clear();
    for (int i = 0; i < INV_SLOTS_6ROW && i < INV_SLOTS; ++i) out.slot[i] = old.slot[i];
    for (int i = 0; i < EQ_SLOTS_V3; ++i) out.equip[i] = old.equip[i];
    out.selected = old.selected;
    for (int d = 0; d < DRONE_BAYS_V3; ++d) {
        for (int i = 0; i < DRONE_CHIPS_V3; ++i)
            out.droneModule[d][i] = old.droneModule[d][i];
        out.droneLevel[d] = old.droneLevel[d];
    }
}

bool saveRead(const char* path, World& w) {
    g_nStats = 0; g_total = 0; g_err[0] = 0;
    /* Cleared before anything is read, so a world loaded on top of another does
       not inherit its explored map. Every other section overwrites what it owns
       outright; this one is a bitmap that is only ever OR-ed into, and a save
       written before the section existed supplies nothing at all -- so without
       this, loading an old world after playing a new one would light up
       wherever you had been in a completely different place. */
    seenReset();

    FILE* f = fopen(path, "rb");
    if (!f) { sprintf(g_err, "no save at %s", path); return false; }

    u32 magic = 0, ver = 0; i32 dims[2] = { 0, 0 };
    if (fread(&magic, sizeof(magic), 1, f) != 1 ||
        fread(&ver,   sizeof(ver),   1, f) != 1 ||
        fread(dims,   sizeof(dims),  1, f) != 1) {
        sprintf(g_err, "%s is too short to be a save", path); fclose(f); return false;
    }
    if (magic != fourcc("CRUC")) {
        sprintf(g_err, "%s is not a cinderlift save", path); fclose(f); return false;
    }
    /* Refused, not guessed at -- see the note on migration in save.h. */
    if (ver != SAVE_VERSION) {
        sprintf(g_err, "save is format %u, this build reads %u", ver, SAVE_VERSION);
        fclose(f); return false;
    }
    if (dims[0] != SIM_W || dims[1] != SIM_H) {
        sprintf(g_err, "save is %dx%d, this build is %dx%d", dims[0], dims[1], SIM_W, SIM_H);
        fclose(f); return false;
    }
    statAdd("header", sizeof(magic) + sizeof(ver) + sizeof(dims));

    /* Defaults for anything the file does not carry, so a save written by an
       older build simply arrives without the parts that did not exist. */
    /* Without the per-cell speckle: the cell material section re-rolls every
       tint from tintAt, so drawing 37.7 million random numbers here only to
       overwrite them was a third of the time a load took. A save with no cell
       section at all -- which should not exist -- still gets its speckle, from
       the check after the sections are read. */
    w.reset(false);
    bool haveCells = false;
    devClear(); sparkClear(); roomsClear(w); treesClear();
    g_inv.clear();
    rosterClear();
    g_playerSessions[0].respawnBedX = g_playerSessions[0].respawnBedY = -1;
    g_playerSessions[0].respawnFrames = 0;
    g_playerSessions[0].healCooldown = 0;
    /* A world that has not been won until its own WON section says so. Without
       this, loading a world you had not finished after playing one you had
       would carry the victory across -- the flag is a global, and every other
       default here exists for the same reason. */
    rocketSetVictory(false);

    bool haveMats = false;
    for (;;) {
        u32 tag = 0; u64 len = 0;
        if (fread(&tag, sizeof(tag), 1, f) != 1) break;
        if (tag == 0) { statAdd("end", 4); break; }
        if (fread(&len, sizeof(len), 1, f) != 1) break;
        const long payload = ftell(f);
        const long next    = payload + (long)len;

        if (tag == fourcc("MATS")) {
            if (!readMatTable(f, len)) { sprintf(g_err, "bad material table"); fclose(f); return false; }
            haveMats = true;
            statAdd("material names", len + 12);
        } else if (tag == fourcc("CMAT")) {
            if (!haveMats) { sprintf(g_err, "cells before the material table"); fclose(f); return false; }
            if (!rleReadSection(f, len, g_plane, (u64)SIM_W * SIM_H)) { sprintf(g_err, "bad cell data"); fclose(f); return false; }
            /* Remap, clear the flags and re-roll the tint in one pass per
               thread, rather than three passes on one. */
            parallelCells((u64)SIM_W * SIM_H, loadMaterialRange, &w);
            haveCells = true;
            statAdd("cell material", len + 12);
        } else if (tag == fourcc("CMOI")) {
            if (!rleReadSection(f, len, g_plane, (u64)SIM_W * SIM_H)) { sprintf(g_err, "bad moisture data"); fclose(f); return false; }
            parallelCells((u64)SIM_W * SIM_H, loadMoistureRange, &w);
            statAdd("cell moisture", len + 12);
        } else if (tag == fourcc("TEMP")) {
            if (!rleReadSection(f, len, w.temp, (u64)SIM_W * SIM_H)) { sprintf(g_err, "bad temperature data"); fclose(f); return false; }
            statAdd("temperature", len + 12);
        } else if (tag == fourcc("BGND")) {
            if (!rleReadSection(f, len, w.bg, (u64)SIM_W * SIM_H)) { sprintf(g_err, "bad background data"); fclose(f); return false; }
            parallelCells((u64)SIM_W * SIM_H, loadBackgroundRange, &w);
            statAdd("background", len + 12);
        } else if (tag == fourcc("ZONE")) {
            if (fread(w.zone, 1, CHUNK_COUNT, f) != (size_t)CHUNK_COUNT) {
                sprintf(g_err, "bad zone data"); fclose(f); return false;
            }
            statAdd("zones", len + 12);
        } else if (tag == fourcc("HGHT")) {
            fread(g_surfaceY, sizeof(int), SIM_W, f);
            fread(g_stoneY,   sizeof(int), SIM_W, f);
            statAdd("height maps", len + 12);
        } else if (tag == fourcc("PLYR")) {
            if (len == sizeof(Player)) fread(&g_player, sizeof(Player), 1, f);
            statAdd("character", len + 12);
        } else if (tag == fourcc("RSPN")) {
            if (len == sizeof(i32) * 2 || len == sizeof(i32) * 3) {
                i32 checkpoint[3] = { -1, -1, 0 };
                fread(checkpoint, 1, (size_t)len, f);
                g_playerSessions[0].respawnBedX = checkpoint[0];
                g_playerSessions[0].respawnBedY = checkpoint[1];
                g_playerSessions[0].healCooldown =
                    imin(HEAL_COOLDOWN_FRAMES, imax(0, checkpoint[2]));
            }
            statAdd("respawn checkpoint", len + 12);
        } else if (tag == fourcc("PLRS")) {
            u32 n = 0;
            if (len >= sizeof(n)) {
                fread(&n, sizeof(n), 1, f);
                /* The count and the length have to agree. A file claiming a
                   thousand entries in twelve bytes is corruption, and
                   trusting it would read the rest of the save as players. */
                const u64 want   = sizeof(n) + (u64)n * sizeof(RememberedPlayer);
                const u64 wantV1 = sizeof(n) + (u64)n * sizeof(RememberedPlayerV1);
                if (n <= (u32)MAX_REMEMBERED && len == want) {
                    for (u32 i = 0; i < n; ++i) {
                        if (fread(&g_roster[i], sizeof(RememberedPlayer), 1, f) != 1) break;
                        g_roster[i].used = true;
                        g_roster[i].id[PLAYER_IDENTITY_CHARS] = 0;
                        if (g_roster[i].toolCount > REMEMBERED_TOOLS)
                            g_roster[i].toolCount = REMEMBERED_TOOLS;
                        remapStacksIn(g_roster[i].inventory);
                    }
                } else if (n <= (u32)MAX_REMEMBERED && len == wantV1) {
                    /* Written before the trinket slots grew. Converted rather
                       than skipped -- see RememberedPlayerV1. */
                    for (u32 i = 0; i < n; ++i) {
                        RememberedPlayerV1 old;
                        if (fread(&old, sizeof(old), 1, f) != 1) break;
                        memcpy(g_roster[i].id, old.id, sizeof(old.id));
                        g_roster[i].id[PLAYER_IDENTITY_CHARS] = 0;
                        inventoryFromV4(old.inventory, g_roster[i].inventory);
                        memcpy(g_roster[i].tools, old.tools, sizeof(old.tools));
                        g_roster[i].toolCount = old.toolCount > REMEMBERED_TOOLS
                                              ? REMEMBERED_TOOLS : old.toolCount;
                        g_roster[i].x = old.x; g_roster[i].y = old.y;
                        g_roster[i].lastSeen = old.lastSeen;
                        g_roster[i].used = true;
                        remapStacksIn(g_roster[i].inventory);
                    }
                }
            }
            statAdd("remembered players", len + 12);
        } else if (tag == fourcc("INVN")) {
            /* Inventory grew by APPENDING durable state (such as drone chip
               bays) for as long as every change was at the end, and a short
               read was then a valid prefix. That stopped being true the moment
               equip[] itself got longer: the new trinket slots sit in the
               middle of the struct, so an older file read as a prefix lands
               `selected` inside equip[9] and every byte after it is one slot
               out. It would not error -- it would hand you a character wearing
               a garbled stack, which is the worst kind of load failure.

               So the previous shape is spelled out and converted. The old
               struct is written here rather than derived from an offset
               calculation because this is the same compiler with the same
               padding rules, and copying field by field cannot be off by a
               pad byte the way arithmetic over `len` can. */
            if (len == sizeof(Inventory)) {
                fread(&g_inv, 1, (size_t)len, f);
                remapInventoryItems();
            } else if (len == sizeof(InventoryV4)) {
                InventoryV4 old;
                fread(&old, 1, sizeof(old), f);
                inventoryFromV4(old, g_inv);
                remapInventoryItems();
            } else if (len == sizeof(InventoryV3)) {
                InventoryV3 old;
                fread(&old, 1, sizeof(old), f);
                g_inv.clear();
                for (int i = 0; i < INV_SLOTS_4ROW; ++i) g_inv.slot[i] = old.slot[i];
                for (int i = 0; i < EQ_SLOTS_V3; ++i)    g_inv.equip[i] = old.equip[i];
                g_inv.selected = old.selected;
                for (int d = 0; d < DRONE_BAYS_V3; ++d) {
                    for (int i = 0; i < DRONE_CHIPS_V3; ++i)
                        g_inv.droneModule[d][i] = old.droneModule[d][i];
                    g_inv.droneLevel[d] = old.droneLevel[d];
                }
                remapInventoryItems();
            } else if (len == sizeof(InventoryV2)) {
                InventoryV2 old;
                fread(&old, 1, sizeof(old), f);
                g_inv.clear();
                for (int i = 0; i < INV_SLOTS_4ROW; ++i) g_inv.slot[i] = old.slot[i];
                for (int i = 0; i < 11; ++i)        g_inv.equip[i] = old.equip[i];
                g_inv.selected = old.selected;
                for (int d = 0; d < 3; ++d) {
                    for (int i = 0; i < DRONE_CHIPS_V3; ++i)
                        g_inv.droneModule[d][i] = old.droneModule[d][i];
                    g_inv.droneLevel[d] = old.droneLevel[d];
                }
                remapInventoryItems();
            } else if (len <= sizeof(InventoryV1)) {
                InventoryV1 old;
                memset(&old, 0, sizeof(old));
                fread(&old, 1, (size_t)len, f);
                g_inv.clear();
                for (int i = 0; i < INV_SLOTS_4ROW; ++i) g_inv.slot[i] = old.slot[i];
                for (int i = 0; i < 9; ++i)         g_inv.equip[i] = old.equip[i];
                g_inv.selected = old.selected;
                for (int d = 0; d < 3; ++d) {
                    for (int i = 0; i < DRONE_CHIPS_V3; ++i)
                        g_inv.droneModule[d][i] = old.droneModule[d][i];
                    g_inv.droneLevel[d] = old.droneLevel[d];
                }
                remapInventoryItems();
            }
            statAdd("inventory", len + 12);
        } else if (tag == fourcc("MAPX")) {
            if (len == (u64)sizeof(g_map)) {
                fread(g_map, 1, sizeof(g_map), f);
                /* Remapped like every other stored MatId -- the map is made of
                   them, so a save written before a material moved would draw
                   the wrong colours. MAP_UNSEEN and MAP_AIR are NOT material
                   ids and must survive untouched: remapping MAP_AIR (255)
                   through the table would land on MAT_EMPTY, which is
                   MAP_UNSEEN, and every tunnel you had walked would go back to
                   being unexplored. */
                for (int i = 0; i < MAP_W * MAP_H; ++i) {
                    const u8 v = g_map[i];
                    if (v == MAP_UNSEEN || v == MAP_AIR) continue;
                    g_map[i] = g_remap[v] ? g_remap[v] : MAP_AIR;
                }
            }
            statAdd("explored map", len + 12);
        } else if (tag == fourcc("TOOL")) {
            if (len == sizeof(ToolInst) * MAX_TOOL_INST) {
                fread(g_toolInst, sizeof(ToolInst), MAX_TOOL_INST, f);
                remapToolItems();
            } else {
                /* ToolInst before batteries and shot sequencing. Preserve the
                   loadout/payload, then toolInstReconcile() below adopts each
                   referenced chassis with a full current-capacity battery. */
                struct ToolInstV1 {
                    ItemId slot[TOOL_SLOTS_MAX];
                    int cooldown;
                    bool used;
                    ItemStack payload;
                };
                if (len == sizeof(ToolInstV1) * MAX_TOOL_INST) {
                    ToolInstV1 old[MAX_TOOL_INST];
                    fread(old, sizeof(ToolInstV1), MAX_TOOL_INST, f);
                    memset(g_toolInst, 0, sizeof(g_toolInst));
                    for (int i = 0; i < MAX_TOOL_INST; ++i) {
                        memcpy(g_toolInst[i].slot, old[i].slot, sizeof(old[i].slot));
                        g_toolInst[i].cooldown = old[i].cooldown;
                        g_toolInst[i].used = old[i].used;
                        g_toolInst[i].payload = old[i].payload;
                    }
                    remapToolItems();
                }
            }
            statAdd("tool loadouts", len + 12);
        } else if (tag == fourcc("DEVS")) {
            if (len == sizeof(Device) * MAX_DEVICES ||
                len == sizeof(Device) * MAX_DEVICES + sizeof(Spark) * MAX_SPARKS) {
                fread(g_devices, sizeof(Device), MAX_DEVICES, f);
                for (int i = 0; i < MAX_DEVICES; ++i)
                    if (g_devices[i].used) g_devices[i].mat = g_remap[g_devices[i].mat];
            }
            statAdd("machines", len + 12);
        } else if (tag == fourcc("WON ")) {
            u32 won = 0;
            if (len == sizeof(won)) fread(&won, sizeof(won), 1, f);
            rocketSetVictory(won != 0);
            statAdd("victory", len + 12);
        } else if (tag == fourcc("TORC")) {
            i32 count = 0;
            if (len >= sizeof(count)) {
                fread(&count, sizeof(count), 1, f);
                const int available = (int)((len - sizeof(count)) / sizeof(TorchFixture));
                if (count >= 0 && count <= available) {
                    TorchFixture* fixtures = count ? new TorchFixture[count] : 0;
                    if (count) fread(fixtures, sizeof(TorchFixture), count, f);
                    torchLoad(fixtures, count);
                    delete[] fixtures;
                }
            }
            statAdd("torch fixtures", len + 12);
        } else if (tag == fourcc("CIRC")) {
            if (len == sizeof(CircuitDeviceConfig) * MAX_DEVICES +
                       sizeof(CircuitWire) * MAX_CIRCUIT_WIRES) {
                fread(g_circuitConfig, sizeof(CircuitDeviceConfig), MAX_DEVICES, f);
                fread(g_circuitWires, sizeof(CircuitWire), MAX_CIRCUIT_WIRES, f);
                circuitRemapMaterials(g_remap, g_savedMatCount);
            } else {
                /* The first circuit build had endpoint-only wires. Upgrade
                   them to port 0 links so a save made yesterday keeps every
                   network; only newly placed arithmetic/decider cables can
                   choose the new right-side output terminal. */
                struct OldCircuitWire { u8 a, b; bool used; };
                if (len == sizeof(CircuitDeviceConfig) * MAX_DEVICES +
                           sizeof(OldCircuitWire) * MAX_CIRCUIT_WIRES) {
                    OldCircuitWire old[MAX_CIRCUIT_WIRES];
                    fread(g_circuitConfig, sizeof(CircuitDeviceConfig), MAX_DEVICES, f);
                    fread(old, sizeof(OldCircuitWire), MAX_CIRCUIT_WIRES, f);
                    memset(g_circuitWires, 0, sizeof(g_circuitWires));
                    for (int i = 0; i < MAX_CIRCUIT_WIRES; ++i) {
                        g_circuitWires[i].a = old[i].a; g_circuitWires[i].b = old[i].b;
                        g_circuitWires[i].used = old[i].used;
                    }
                    circuitRemapMaterials(g_remap, g_savedMatCount);
                }
            }
            statAdd("circuit wires", len + 12);
        } else if (tag == fourcc("ROOM")) {
            if (len == sizeof(Room) * MAX_ROOMS) fread(g_rooms, sizeof(Room), MAX_ROOMS, f);
            statAdd("rooms", len + 12);
        } else if (tag == fourcc("TREE")) {
            if (len == sizeof(Tree) * MAX_TREES) fread(g_trees, sizeof(Tree), MAX_TREES, f);
            statAdd("growing trees", len + 12);
        } else if (tag == fourcc("SEEN")) {
            /* A save written before this section existed simply keeps the empty
               map seenReset() left, so an old world starts undiscovered and
               lights up again as you walk it -- the same graceful degradation
               every other optional section gets. */
            if (!rleReadSection(f, len, seenData(), SEEN_BYTES)) {
                sprintf(g_err, "bad explored data"); fclose(f); return false;
            }
            statAdd("explored", len + 12);
        } else if (tag == fourcc("BOSS")) {
            if (len == sizeof(g_bossesBeaten))
                fread(&g_bossesBeaten, sizeof(g_bossesBeaten), 1, f);
            statAdd("bosses beaten", len + 12);
        } else if (tag == fourcc("TIME")) {
            if (len == sizeof(g_worldTime)) {
                fread(&g_worldTime, sizeof(g_worldTime), 1, f);
                /* A save from before this section existed simply leaves the
                   clock where it was, which is dawn on a fresh run -- the same
                   graceful degradation every other optional section gets. */
                g_worldTime %= DAY_LENGTH;
            }
            statAdd("time of day", len + 12);
        } else {
            /* Unknown tag: skipped, which is the whole point of the framing.
               A save written by a later build loads here without whatever this
               was, rather than being refused. */
            statAdd("skipped", len + 12);
        }
        fseek(f, next, SEEK_SET);
    }

    fclose(f);
    /* Torch fixtures used to occupy machine slots. Migrate old saves after all
       sections have been read; current saves supply TORC and therefore contain
       no such records. */
    for (int i = 0; i < MAX_DEVICES; ++i) {
        if (g_devices[i].used && g_devices[i].type == DEV_TORCH) {
            torchAdd(g_devices[i].x, g_devices[i].y);
            g_devices[i].used = false;
        }
    }
    if (!haveCells) parallelCells((u64)SIM_W * SIM_H, loadTintRange, &w);
    circuitInitMissingConfigs();
    /* Put the inventory and the tool pool back in agreement. Needed for every
       save written before the TOOL section existed, where the pack names
       instances that were never restored -- and cheap insurance for every save
       written after it. See toolInstReconcile: the failure it prevents is a
       tool that fires exactly once and then never again, which is about as
       far from its cause as a symptom can get. */
    toolInstReconcile(g_inv);
    /* Older saves included sparks in DEVS. Ignore them too: a reload begins
       with passive wires and devices, never a half-restored electrical front. */
    sparkClear();
    /* Whatever was chasing you is gone. Creatures are not saved (entity.h), so
       the ones still in the array are from the world that was open a moment
       ago -- leaving them would strand them inside whatever terrain now
       occupies the cells they were standing in. */
    entReset();

    /* Everything must be simulated once, and the room flags rebuilt from the
       rooms that were loaded. Dirtying the whole world is a one-off cost on
       load and it is what stops sand hanging in mid-air where the save caught
       it between frames. */
    w.dirtyArea(0, 0, SIM_W - 1, SIM_H - 1);
    /* The line above would force a recut on its own, since a whole-world dirty
       box is far past the size worth patching. Saying so outright is cheaper
       than deducing it, and it does not depend on that reasoning staying true
       if the dirtying above is ever narrowed. */
    lightInvalidate();
    statSort();
    if (g_lostMats) sprintf(g_err, "%d material(s) in the save no longer exist", g_lostMats);
    return true;
}
