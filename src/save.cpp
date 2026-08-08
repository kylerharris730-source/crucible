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
#include <stdio.h>
#include <string.h>

static char g_err[256] = "";
const char* saveError() { return g_err; }

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
static void rleWrite(FILE* f, const u8* src, u64 n, u64* outBytes) {
    u64 written = 0;
    u64 i = 0;
    while (i < n) {
        const u8 v = src[i];
        u64 run = 1;
        while (i + run < n && src[i + run] == v && run < 65535) ++run;
        const u16 c = (u16)run;
        fwrite(&c, sizeof(c), 1, f);
        fwrite(&v, 1, 1, f);
        written += 3;
        i += run;
    }
    *outBytes = written;
}

static bool rleRead(FILE* f, u8* dst, u64 n) {
    u64 i = 0;
    while (i < n) {
        u16 c; u8 v;
        if (fread(&c, sizeof(c), 1, f) != 1) return false;
        if (fread(&v, 1, 1, f) != 1) return false;
        if (c == 0 || (u64)c > n - i) return false;
        memset(dst + i, v, c);
        i += c;
    }
    return true;
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
    return (now >= MAT_COUNT && now < ITEM_COUNT) ? (ItemId)now : ITEM_NONE;
}

static void remapInventoryItems() {
    for (int i = 0; i < INV_SLOTS; ++i) g_inv.slot[i].item = remapSavedItem(g_inv.slot[i].item);
    for (int i = 0; i < EQ_COUNT; ++i) g_inv.equip[i].item = remapSavedItem(g_inv.equip[i].item);
    for (int d = 0; d < 3; ++d)
        for (int i = 0; i < Inventory::DRONE_MODULE_SLOTS_MAX; ++i)
            g_inv.droneModule[d][i].item = remapSavedItem(g_inv.droneModule[d][i].item);
}

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

bool saveWrite(const char* path, const World& w) {
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

    writeMatTable(f);

    /* --- the three big planes ---------------------------------------- */
    {
        SectionWriter s; s.begin(f, "CMAT", "cell material");
        for (int i = 0; i < SIM_W * SIM_H; ++i) g_plane[i] = w.cells[i].mat;
        u64 b; rleWrite(f, g_plane, SIM_W * SIM_H, &b);
        s.end();
    }
    {
        SectionWriter s; s.begin(f, "CMOI", "cell moisture");
        for (int i = 0; i < SIM_W * SIM_H; ++i) g_plane[i] = w.cells[i].moisture;
        u64 b; rleWrite(f, g_plane, SIM_W * SIM_H, &b);
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
    {
        SectionWriter s; s.begin(f, "TEMP", "temperature");
        u64 b; rleWrite(f, w.temp, SIM_W * SIM_H, &b);
        s.end();
    }
    {
        SectionWriter s; s.begin(f, "BGND", "background");
        u64 b; rleWrite(f, w.bg, SIM_W * SIM_H, &b);
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
        SectionWriter s; s.begin(f, "INVN", "inventory");
        fwrite(&g_inv, sizeof(Inventory), 1, f);
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

static void remapPlane(u8* p, u64 n) {
    for (u64 i = 0; i < n; ++i) p[i] = g_remap[p[i]];
}

bool saveRead(const char* path, World& w) {
    g_nStats = 0; g_total = 0; g_err[0] = 0;

    FILE* f = fopen(path, "rb");
    if (!f) { sprintf(g_err, "no save at %s", path); return false; }

    u32 magic = 0, ver = 0; i32 dims[2] = { 0, 0 };
    if (fread(&magic, sizeof(magic), 1, f) != 1 ||
        fread(&ver,   sizeof(ver),   1, f) != 1 ||
        fread(dims,   sizeof(dims),  1, f) != 1) {
        sprintf(g_err, "%s is too short to be a save", path); fclose(f); return false;
    }
    if (magic != fourcc("CRUC")) {
        sprintf(g_err, "%s is not a crucible save", path); fclose(f); return false;
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
    w.reset();
    devClear(); sparkClear(); roomsClear(w); treesClear();
    g_inv.clear();

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
            if (!rleRead(f, g_plane, SIM_W * SIM_H)) { sprintf(g_err, "bad cell data"); fclose(f); return false; }
            remapPlane(g_plane, SIM_W * SIM_H);
            for (int i = 0; i < SIM_W * SIM_H; ++i) {
                w.cells[i].mat   = g_plane[i];
                w.cells[i].flags = 0;
                w.cells[i].tint = tintAt((u32)i);
            }
            statAdd("cell material", len + 12);
        } else if (tag == fourcc("CMOI")) {
            if (!rleRead(f, g_plane, SIM_W * SIM_H)) { sprintf(g_err, "bad moisture data"); fclose(f); return false; }
            for (int i = 0; i < SIM_W * SIM_H; ++i) {
                /* A sieve's or reactive powder's moisture byte is a fluid
                   material id. Remap it by name just like the foreground
                   plane; ordinary moisture is a scalar and must remain
                   untouched. */
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
            statAdd("cell moisture", len + 12);
        } else if (tag == fourcc("TEMP")) {
            if (!rleRead(f, w.temp, SIM_W * SIM_H)) { sprintf(g_err, "bad temperature data"); fclose(f); return false; }
            statAdd("temperature", len + 12);
        } else if (tag == fourcc("BGND")) {
            if (!rleRead(f, w.bg, SIM_W * SIM_H)) { sprintf(g_err, "bad background data"); fclose(f); return false; }
            /* The background stores a material id beside a flag bit, so it
               needs the same remap the foreground got -- and it has to keep the
               flag while doing it. Missing this would repaint every wall you
               have ever built as whatever now sits at that index. */
            for (int i = 0; i < SIM_W * SIM_H; ++i) {
                const u8 raw = w.bg[i];
                const u8 m   = g_remap[raw & BG_MAT_MASK];
                w.bg[i] = (u8)((m & BG_MAT_MASK) | (raw & BG_PLACED));
            }
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
        } else if (tag == fourcc("INVN")) {
            /* Inventory grows by appending durable state (such as drone chip
               bays). Older saves are a valid prefix, so retain their pack and
               equipment rather than discarding all of it on a size change. */
            if (len <= sizeof(Inventory)) { fread(&g_inv, 1, (size_t)len, f); remapInventoryItems(); }
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
