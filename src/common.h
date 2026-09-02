#pragma once
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
/* Added for the save timestamp. A unix time in 32 bits stops working in 2038,
   and a save format is exactly the kind of thing that outlives the assumption
   -- so the one field that stores wall-clock time is 64 bits from the start
   rather than after somebody notices. */
typedef int64_t  i64;

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

/* ------------------------------------------------------------------------
   Fast RNG. This gets called several times per active cell per frame, so
   rand() is far too slow (and on MSVCRT only has 15 bits of range anyway).
   xorshift32 is 3 shifts and 3 xors.
   ------------------------------------------------------------------------ */
/* THREAD-LOCAL, because the simulation runs its stripes on several threads
   and a shared stream would be both a race and a source of results that
   differ from one run to the next. Each stripe seeds its own copy from the
   world's master stream (see World::step), so the answer is the same however
   many cores are doing the work -- that property is worth more than the
   stream being globally sequential, which nothing ever depended on.

   Everything OUTSIDE the sim -- worldgen, entities, devices, projectiles, the
   brush -- runs on the main thread and so keeps using the main thread's copy,
   which is the one codec.cpp serialises and the one a save restores.

   This costs something on this toolchain: MinGW's __thread is emulated
   (__emutls_get_address, a real call per access, measured at +5.4% on the sim
   step). It is paid inside the parallel region, so the threads divide it
   along with everything else. A MinGW-w64 toolchain would have native TLS
   and not pay it at all. */
extern __thread u32 g_rng;

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
