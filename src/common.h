#pragma once
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int16_t  i16;
typedef int32_t  i32;

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

/* ------------------------------------------------------------------------
   Fast RNG. This gets called several times per active cell per frame, so
   rand() is far too slow (and on MSVCRT only has 15 bits of range anyway).
   xorshift32 is 3 shifts and 3 xors.
   ------------------------------------------------------------------------ */
extern u32 g_rng;

static inline u32 rngNext() {
    u32 x = g_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng = x;
    return x;
}

/* Probability out of 255. Mask instead of modulo -- no division. */
static inline bool rngChance(u32 p) { return (rngNext() & 0xFF) < p; }

/* n random bits, taken from the high end where xorshift mixes best. */
static inline u32 rngBits(u32 n) { return rngNext() >> (32 - n); }
