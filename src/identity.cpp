#include "identity.h"
#include "save.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <stdlib.h>
#endif

/* Where the file lives, per platform.

   Windows puts it under %LOCALAPPDATA%, which follows the user rather than the
   folder the game was launched from -- the whole point of storing it outside
   the install.

   The browser build writes it into the save directory instead, because that is
   the one place a tab has that survives being closed: it is mounted on IDBFS
   and therefore backed by IndexedDB. It needs the same explicit flush every
   other write there needs, so savePersist() is called after creating one. */
static bool identityPath(char* out, size_t cap) {
#if defined(__EMSCRIPTEN__)
    if (cap < 20) return false;
    strcpy(out, "/saves/player.id");
    return true;
#elif defined(_WIN32)
    char base[MAX_PATH];
    const DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    char dir[MAX_PATH];
    if ((size_t)_snprintf(dir, sizeof(dir), "%s\\Cinderlift", base) >= sizeof(dir)) return false;
    dir[sizeof(dir) - 1] = 0;
    /* Already existing is success, not failure. */
    CreateDirectoryA(dir, 0);
    if ((size_t)_snprintf(out, cap, "%s\\player.id", dir) >= cap) return false;
    out[cap - 1] = 0;
    return true;
#else
    const char* home = getenv("HOME");
    if (!home) return false;
    if ((size_t)snprintf(out, cap, "%s/.cinderlift-player.id", home) >= cap) return false;
    return true;
#endif
}

static bool hexOnly(const char* s, int n) {
    for (int i = 0; i < n; ++i) {
        const char c = s[i];
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok) return false;
    }
    return true;
}

/* Enough entropy to not collide between the handful of people who will ever
   share a world. Deliberately not a cryptographic source: this names a
   character, it does not protect anything, and pulling in a crypto API for it
   would be a dependency bought with nothing. */
static void generateIdentity(char* out) {
    u32 s0 = (u32)time(0);
    u32 s1 = (u32)clock();
    u32 s2 = (u32)(size_t)(void*)out;
#ifdef _WIN32
    s1 ^= (u32)GetCurrentProcessId();
    s1 ^= (u32)GetTickCount();
#endif
    u32 x = s0 ^ 0x9E3779B9u;
    u32 y = s1 ? s1 : 0x85EBCA6Bu;
    u32 z = s2 ? s2 : 0xC2B2AE35u;
    static const char HEX[] = "0123456789abcdef";
    for (int i = 0; i < PLAYER_IDENTITY_CHARS; ++i) {
        /* xorshift128-ish: three words stirred together so a coarse clock does
           not make two launches in the same second produce the same name. */
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        y ^= y << 11; y ^= y >> 8;  y ^= x;
        z += 0x9E3779B9u; z ^= z >> 15;
        out[i] = HEX[(x ^ y ^ z) & 0xF];
    }
    out[PLAYER_IDENTITY_CHARS] = 0;
}

const char* playerIdentity() {
    static char id[PLAYER_IDENTITY_CHARS + 1];
    if (id[0]) return id;

    char path[512];
    if (identityPath(path, sizeof(path))) {
        FILE* f = fopen(path, "rb");
        if (f) {
            char raw[PLAYER_IDENTITY_CHARS + 1];
            const size_t got = fread(raw, 1, PLAYER_IDENTITY_CHARS, f);
            fclose(f);
            if (got == (size_t)PLAYER_IDENTITY_CHARS && hexOnly(raw, PLAYER_IDENTITY_CHARS)) {
                memcpy(id, raw, PLAYER_IDENTITY_CHARS);
                id[PLAYER_IDENTITY_CHARS] = 0;
                return id;
            }
            /* A short or corrupt file is replaced rather than trusted: half an
               identity would be a different player every launch, which looks
               exactly like the bug this feature removes. */
        }
        generateIdentity(id);
        f = fopen(path, "wb");
        if (f) {
            fwrite(id, 1, PLAYER_IDENTITY_CHARS, f);
            fclose(f);
            savePersist();   /* no-op off the web; see save.h */
        }
        return id;
    }

    /* Nowhere durable to put it. Still return something valid so that joining
       works -- it just will not be remembered, which is the behaviour this
       whole file replaces rather than a new failure. */
    generateIdentity(id);
    return id;
}
