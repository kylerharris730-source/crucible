#include "identity.h"
#include "save.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#else
#include <stdlib.h>
#include <unistd.h>
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

/* Sixteen bytes from the operating system's cryptographic generator.

   This used to be a clock, the process id and a stack address stirred
   together, on the grounds that the value only names a character. It does
   more than that: a host hands a remembered pack to whoever presents the
   identity, so an identity that can be guessed is a pack that can be taken.
   CryptGenRandom is in advapi32, which every Windows program already links;
   the browser and Linux get getentropy(), which in a tab is
   crypto.getRandomValues. */
static bool systemRandom(u8* out, size_t n) {
#ifdef _WIN32
    HCRYPTPROV provider = 0;
    if (!CryptAcquireContextA(&provider, 0, 0, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
        return false;
    const bool ok = CryptGenRandom(provider, (DWORD)n, out) != 0;
    CryptReleaseContext(provider, 0);
    return ok;
#else
    return getentropy(out, n) == 0;
#endif
}

/* Only if the system generator is unavailable, which it should never be.
   Guessable, but a player with no identity cannot join at all. */
static void weakRandom(u8* out, size_t n, const void* salt) {
    u32 x = (u32)time(0) ^ 0x9E3779B9u;
    u32 y = (u32)clock(); y = y ? y : 0x85EBCA6Bu;
    u32 z = (u32)(size_t)salt; z = z ? z : 0xC2B2AE35u;
#ifdef _WIN32
    y ^= (u32)GetCurrentProcessId() ^ (u32)GetTickCount();
#endif
    for (size_t i = 0; i < n; ++i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        y ^= y << 11; y ^= y >> 8;  y ^= x;
        z += 0x9E3779B9u; z ^= z >> 15;
        out[i] = (u8)(x ^ y ^ z);
    }
}

void identityGenerate(char* out) {
    u8 bytes[PLAYER_IDENTITY_CHARS / 2];
    if (!systemRandom(bytes, sizeof(bytes))) weakRandom(bytes, sizeof(bytes), out);
    static const char HEX[] = "0123456789abcdef";
    for (int i = 0; i < PLAYER_IDENTITY_CHARS / 2; ++i) {
        out[i * 2]     = HEX[bytes[i] >> 4];
        out[i * 2 + 1] = HEX[bytes[i] & 0xF];
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
        identityGenerate(id);
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
    identityGenerate(id);
    return id;
}
