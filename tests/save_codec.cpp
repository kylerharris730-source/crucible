/* --- the fast save codec writes the same files and reads them back exactly ----

   Saving and loading were rewritten for speed -- an eight-bytes-at-a-time run
   scan, the four world layers encoded on their own threads, sections read in
   one call and the per-cell load passes split across threads. Measured, a save
   went from about 190 ms to under 40 and a load from about 186 to about 65,
   which is what makes the five-minute autosave a blip instead of a hitch.

   Speed is not what this file checks. It checks the two promises that made the
   rewrite safe to ship, on planes built to hit the encoder's awkward cases --
   which a real world does not, because a real world is mostly long runs:

     THE FORMAT DID NOT CHANGE. Every RLE section is byte-for-byte what a plain
       one-byte-at-a-time greedy encoder produces, so a save written now is the
       same file an old build would write, and an old build can read it.
       Awkward cases: random noise (every byte a new run), runs longer than the
       65535 cap, runs of exactly 65535 and 65536, and runs that end one byte
       either side of an eight-byte word.

     NOTHING IS LOST. Every plane loads back identical to what was saved.

   Plus one refusal: a truncated plane section fails the load cleanly rather
   than reading on into whatever section comes next.

   Compile with every src/*.cpp except main.cpp. Do not name the output
   *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "save.h"
#include "device.h"
#include "multiplayer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static World g_a, g_b;
static int failures = 0;

static void check(bool ok, const char* what) {
    printf("  %-64s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static const u64 N = (u64)SIM_W * SIM_H;

/* The reference: greedy maximal runs, capped at 65535, one byte at a time.
   Deliberately the slow, obvious version -- it is the definition of the format
   the fast one has to match, not a second clever implementation to agree
   with. */
static void referenceRle(const u8* src, u64 n, std::vector<u8>& out) {
    out.clear();
    u64 i = 0;
    while (i < n) {
        const u8 v = src[i];
        u64 run = 1;
        while (i + run < n && src[i + run] == v && run < 65535) ++run;
        const u16 c = (u16)run;
        u8 rec[3];
        memcpy(rec, &c, 2);
        rec[2] = v;
        out.insert(out.end(), rec, rec + 3);
        i += run;
    }
}

/* A save file's sections, by tag. */
struct Section { u32 tag; std::vector<u8> payload; };
static bool readSections(const char* path, std::vector<Section>& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    u8 header[16];
    if (fread(header, 1, 16, f) != 16) { fclose(f); return false; }
    for (;;) {
        u32 tag = 0; u64 len = 0;
        if (fread(&tag, 4, 1, f) != 1 || tag == 0) break;
        if (fread(&len, 8, 1, f) != 1) break;
        Section s; s.tag = tag; s.payload.resize((size_t)len);
        if (len && fread(&s.payload[0], 1, (size_t)len, f) != (size_t)len) break;
        out.push_back(s);
    }
    fclose(f);
    return true;
}
static u32 tag4(const char* t) {
    return (u32)t[0] | ((u32)t[1] << 8) | ((u32)t[2] << 16) | ((u32)t[3] << 24);
}
static const Section* find(const std::vector<Section>& v, const char* t) {
    for (size_t i = 0; i < v.size(); ++i) if (v[i].tag == tag4(t)) return &v[i];
    return 0;
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();

    World& a = g_a;
    a.reset();
    devClear();

    /* --- planes built to be difficult ------------------------------------- */
    srand(12345);
    const u8 solid[4] = { MAT_STONE, MAT_SAND, MAT_WATER, MAT_DIRT };
    for (u64 i = 0; i < N; ++i) {
        const u64 band = i / 100000;
        u8 m;
        switch (band % 6) {
        case 0:  m = (u8)MAT_EMPTY; break;                                /* long runs */
        case 1:  m = solid[rand() % 4]; break;                            /* noise */
        case 2:  m = solid[(i / 7) % 4]; break;                           /* runs of 7 */
        case 3:  m = solid[(i / 9) % 4]; break;                           /* runs of 9 */
        default: m = solid[(i / 65536) % 4]; break;                       /* at the cap */
        }
        a.cells[i].mat = m;
        a.cells[i].moisture = (u8)((band % 3 == 1) ? (rand() & 0x3F) : 0);
        a.temp[i] = (u8)((band % 4 == 2) ? rand() : (80 + (i >> 18) % 5));
        a.bg[i] = (u8)((band % 5 == 3) ? ((i / 3) % 2 ? MAT_STONE : 0) : 0);
    }
    /* Exact boundary runs somewhere in the middle: 65535, 65536 and 65537. */
    {
        u64 at = N / 2;
        const u64 lens[3] = { 65535, 65536, 65537 };
        for (int k = 0; k < 3; ++k) {
            const u8 v = (u8)(k % 2 ? MAT_STONE : MAT_SAND);
            for (u64 j = 0; j < lens[k] && at < N; ++j) a.temp[at++] = v;
            if (at < N) a.temp[at++] = (u8)(v + 1);
        }
    }
    /* And runs that end one byte either side of an eight-byte word. */
    for (u64 base = N / 3, k = 0; k < 64 && base + 64 < N; ++k) {
        const u64 len = 8 * (k + 1) + (k % 3) - 1;
        for (u64 j = 0; j < len && base < N; ++j) a.bg[base++] = (u8)(k & 1 ? MAT_STONE : 0);
    }
    /* The border is wall in every real world; keep it so, since the loader
       expects a material table that knows every id used. */

    const char* path = "save_codec.tmp";
    check(saveWrite(path, a, 0), "the difficult world saves");

    /* --- the format did not change ---------------------------------------- */
    std::vector<Section> sections;
    check(readSections(path, sections), "and its sections can be read back");
    std::vector<u8> plane(N), expect;
    struct { const char* tag; int which; } const PLANES[4] = {
        { "CMAT", 0 }, { "CMOI", 1 }, { "TEMP", 2 }, { "BGND", 3 }
    };
    int identical = 0;
    for (int p = 0; p < 4; ++p) {
        for (u64 i = 0; i < N; ++i) {
            plane[i] = PLANES[p].which == 0 ? a.cells[i].mat
                     : PLANES[p].which == 1 ? a.cells[i].moisture
                     : PLANES[p].which == 2 ? a.temp[i] : a.bg[i];
        }
        referenceRle(&plane[0], N, expect);
        const Section* s = find(sections, PLANES[p].tag);
        const bool same = s && s->payload.size() == expect.size() &&
                          memcmp(&s->payload[0], &expect[0], expect.size()) == 0;
        if (!same)
            printf("  %s: %u bytes written, reference encoder wants %u\n", PLANES[p].tag,
                   s ? (unsigned)s->payload.size() : 0u, (unsigned)expect.size());
        if (same) ++identical;
    }
    printf("  (the noisy material plane encodes to %.1f MB)\n",
           find(sections, "CMAT") ? find(sections, "CMAT")->payload.size() / 1048576.0 : 0.0);
    check(identical == 4, "all four layers are byte-identical to the reference encoder");

    /* --- nothing is lost --------------------------------------------------- */
    World& b = g_b;
    check(saveRead(path, b), "the save loads");
    u64 wrongMat = 0, wrongMoist = 0, wrongTemp = 0, wrongBg = 0, wrongTint = 0;
    for (u64 i = 0; i < N; ++i) {
        wrongMat   += a.cells[i].mat != b.cells[i].mat;
        wrongMoist += a.cells[i].moisture != b.cells[i].moisture;
        wrongTemp  += a.temp[i] != b.temp[i];
        wrongBg    += a.bg[i] != b.bg[i];
        wrongTint  += b.cells[i].tint != tintAt((u32)i);
    }
    printf("  cells differing after load: material %llu, moisture %llu, temperature %llu, "
           "backdrop %llu\n", (unsigned long long)wrongMat, (unsigned long long)wrongMoist,
           (unsigned long long)wrongTemp, (unsigned long long)wrongBg);
    check(wrongMat == 0 && wrongMoist == 0 && wrongTemp == 0 && wrongBg == 0,
          "every layer loads back exactly as it was saved");
    check(wrongTint == 0, "and every cell's speckle is re-rolled from its index");

    /* --- a truncated plane is refused ------------------------------------- */
    {
        /* Rewrite the file with the temperature section's payload cut short
           but its length field left honest about the shorter size, so the
           framing is valid and only the plane itself is incomplete. */
        std::vector<u8> file;
        FILE* f = fopen(path, "rb");
        fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
        file.resize((size_t)size);
        if (fread(&file[0], 1, (size_t)size, f) != (size_t)size) file.clear();
        fclose(f);

        const char* cut = "save_codec_cut.tmp";
        FILE* o = fopen(cut, "wb");
        size_t at = 16;
        fwrite(&file[0], 1, 16, o);
        bool truncated = false;
        while (at + 12 <= file.size()) {
            u32 tag; u64 len;
            memcpy(&tag, &file[at], 4);
            if (tag == 0) { fwrite(&file[at], 1, 4, o); break; }
            memcpy(&len, &file[at + 4], 8);
            u64 keep = len;
            if (tag == tag4("TEMP") && len > 30) { keep = len / 2 - (len / 2) % 3; truncated = true; }
            fwrite(&tag, 4, 1, o);
            fwrite(&keep, 8, 1, o);
            fwrite(&file[at + 12], 1, (size_t)keep, o);
            at += 12 + (size_t)len;
        }
        fclose(o);
        World& c = g_b;
        const bool loaded = saveRead(cut, c);
        remove(cut);
        check(truncated && !loaded, "a temperature section cut in half fails the load cleanly");
    }

    remove(path);
    if (failures) { fprintf(stderr, "\n%d save codec check(s) failed\n", failures); return 1; }
    printf("\nPASS\n");
    return 0;
}
