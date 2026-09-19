/* ============================================================================
   powderbench.cpp -- powderlike's Half scale, without the window.

   Reproduces the complaint: at Half (1024x768 cells, the whole play area live)
   pouring water with the brush lags. The frame is split into the phases the
   front-end actually runs -- step, the device/tree ticks, and renderView over
   the full view -- and reported as a distribution, because a stroke is a
   transient and an average hides one.

     powderbench [threads] [scene] [frames] [ppm-out or -] [radius] [speed]

       POWDERBENCH_SAMPLES=file  sample the main thread (use threads=1)
       POWDERBENCH_ALLAWAKE=1    wake every chunk every frame (diagnostic)
       scene: pour (default) -- a brush of water dragged back and forth along
                                the top, the way a hand does it
              flood           -- the bottom third of the box filled at once
              sand            -- pour, with sand

   Found, with this: dirtyArea was 31% of all samples, called three times per
   cell update and never inlined; and the renderer was a quarter of the rest,
   on one thread while the sim's pool sat idle. Both fixed without changing
   the hash below. Also found, and fixed separately because it is physics
   rather than speed: poured water stood as a 300-cell mesa with walls on the
   chunk grid. The packed-fluid skip in updateLiquid stopped interior water
   hopping through its own body to an open edge, and PRESSURE_MAX = 10 gave
   every deeper row the same reach. POWDERBENCH_ALLAWAKE is how the chunk
   system was ruled out; tests/liquid_slump.cpp holds the fix.

   Build like the other harnesses, all of src except main.cpp.
   ========================================================================== */
#include "world.h"
#include "materials.h"
#include "render.h"
#include "device.h"
#include "tree.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const int CW = 1024, CH = 768;         /* Half */
static const int OX = 1024, OY = 3072;        /* matches powderlike */
static u32 g_px[CW * CH];

/* --- a sampling profiler --------------------------------------------------
   gprof was tried first and it lies about exactly this code: -pg puts an
   mcount call in every function, and the hottest functions here are tiny and
   called half a billion times a run, so the instrumentation IS the profile.
   Sampling the main thread's instruction pointer every millisecond costs the
   program nothing it would not otherwise spend, and at -O3 -g the addresses
   map to source lines, inlined code included. Set POWDERBENCH_SAMPLES to a
   file name to get the raw addresses; tools/ has no symboliser, addr2line is. */
static HANDLE g_mainThread;
static volatile LONG g_sampling = 0;
static unsigned g_samples[1 << 20];
static volatile LONG g_nSamples = 0;
static DWORD WINAPI samplerMain(LPVOID) {
    while (g_sampling) {
        Sleep(1);
        if (SuspendThread(g_mainThread) == (DWORD)-1) continue;
        CONTEXT ctx; ctx.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(g_mainThread, &ctx) && g_nSamples < (1 << 20))
#ifdef _WIN64
            g_samples[g_nSamples++] = (unsigned)ctx.Rip;
#else
            g_samples[g_nSamples++] = (unsigned)ctx.Eip;
#endif
        ResumeThread(g_mainThread);
    }
    return 0;
}

static double g_freq;
static double now(void) {
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (double)t.QuadPart / g_freq;
}

static void buildWorld() {
    devClear();
    g_world.reset();
    const int x1 = OX + CW - 1, y1 = OY + CH - 1;
    g_world.setZoneRect(OX - CHUNK, OY - CHUNK, x1 + CHUNK, y1 + CHUNK, ZONE_LAYER1);
    for (int y = OY - 6; y <= y1 + 6; ++y)
        for (int x = OX - 6; x <= x1 + 6; ++x) {
            const bool inside = x >= OX + 2 && x <= x1 - 2 && y >= OY + 2 && y <= y1 - 2;
            if (!inside) g_world.cells[y * SIM_W + x].mat = MAT_WALL;
        }
    g_world.setLiveWindow(OX, OY, x1, y1);
}

struct Sample { double sim, aux, render; int chunks; };

static int cmpd(const void* a, const void* b) {
    const double x = *(const double*)a, y = *(const double*)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void report(const char* label, const Sample* s, int n) {
    double* v = (double*)malloc(sizeof(double) * n);
    double sim = 0, aux = 0, ren = 0; int cmax = 0; long csum = 0;
    for (int i = 0; i < n; ++i) {
        v[i] = s[i].sim; sim += s[i].sim; aux += s[i].aux; ren += s[i].render;
        if (s[i].chunks > cmax) cmax = s[i].chunks;
        csum += s[i].chunks;
    }
    qsort(v, n, sizeof(double), cmpd);
    printf("  %-8s sim mean %6.2f p50 %6.2f p90 %6.2f p99 %6.2f max %6.2f | "
           "aux %5.2f render %5.2f | chunks mean %ld max %d\n",
           label, sim / n, v[n / 2], v[(int)(n * 0.9)], v[(int)(n * 0.99)], v[n - 1],
           aux / n, ren / n, csum / n, cmax);
    free(v);
}

int main(int argc, char** argv) {
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = (double)f.QuadPart;
    const int threads = argc > 1 ? atoi(argv[1]) : 1;
    const char* scene = argc > 2 ? argv[2] : "pour";
    const int frames = argc > 3 ? atoi(argv[3]) : 900;
    const int radius = argc > 5 ? atoi(argv[5]) : 30;
    /* Sim steps per frame -- powderlike's speed control, which is what a user
       reaches for once they notice material crosses a Half screen slowly. */
    const int speed = argc > 6 ? atoi(argv[6]) : 1;

    simSetWorkers(threads);
    initMaterials();
    buildWorld();

    const bool sand = !strcmp(scene, "sand");
    const u8 mat = sand ? (u8)MAT_SAND : (u8)MAT_WATER;
    if (!strcmp(scene, "flood")) {
        for (int y = OY + CH * 2 / 3; y < OY + CH - 2; ++y)
            for (int x = OX + 2; x < OX + CW - 2; ++x) g_world.setCell(x, y, MAT_WATER);
    }

    const bool allAwake = getenv("POWDERBENCH_ALLAWAKE") != 0;
    const char* sampleOut = getenv("POWDERBENCH_SAMPLES");
    if (sampleOut) {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                        &g_mainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
        timeBeginPeriod(1);
        g_sampling = 1;
        CreateThread(NULL, 0, samplerMain, NULL, 0, NULL);
    }

    Sample* s = (Sample*)calloc(frames, sizeof(Sample));
    const int pourFrames = frames / 2;
    for (int fr = 0; fr < frames; ++fr) {
        if (strcmp(scene, "flood") && fr < pourFrames) {
            /* A hand sweeping across the top, about 1200 cells a second -- the
               drag that was measured in the real window. */
            const int span = CW - 200;
            const int t = (fr * 20) % (2 * span);
            const int bx = OX + 100 + (t < span ? t : 2 * span - t);
            for (int k = 0; k < 20; ++k)
                g_world.paint(bx + (t < span ? -k : k), OY + 120, radius, mat, true);
        }
        const double t0 = now();
        for (int k = 0; k < speed; ++k) {
            /* Diagnostic: every chunk awake, every frame. Separates "the rule
               cannot move this water" from "nothing woke it to try". */
            if (allAwake) g_world.dirtyArea(OX, OY, OX + CW - 1, OY + CH - 1);
            g_world.step();
        }
        const double t1 = now();
        for (int k = 0; k < speed; ++k) { devTick(g_world); treesTick(g_world); }
        const double t2 = now();
        renderView(g_world, g_px, VIEW_NORMAL, OX, OY, false, CW, CH);
        const double t3 = now();
        s[fr].sim = (t1 - t0) * 1000.0;
        s[fr].aux = (t2 - t1) * 1000.0;
        s[fr].render = (t3 - t2) * 1000.0;
        s[fr].chunks = g_world.activeChunks;
    }

    if (sampleOut) {
        g_sampling = 0; Sleep(20);
        FILE* sf = fopen(sampleOut, "w");
        if (sf) {
            for (LONG i = 0; i < g_nSamples; ++i) fprintf(sf, "0x%x\n", g_samples[i]);
            fclose(sf);
        }
    }
    printf("powderbench  scene=%s  threads=%d  frames=%d  radius=%d  speed=%d\n",
           scene, threads, frames, radius, speed);
    if (strcmp(scene, "flood")) {
        report("pouring", s, pourFrames);
        report("after", s + pourFrames, frames - pourFrames);
    } else {
        report("flood", s, frames);
    }

    /* The control: how much is in the box, so a faster variant cannot win by
       losing water. */
    long count = 0;
    for (int y = OY; y < OY + CH; ++y)
        for (int x = OX; x < OX + CW; ++x)
            if (g_world.cells[y * SIM_W + x].mat == mat) ++count;
    /* And a hash of every cell and temperature in the box. The census catches a
       variant that loses water; this catches one that moves it differently. An
       optimisation that is meant to change nothing has to leave this alone at
       one thread, where the scan order is fixed. */
    u32 h = 2166136261u;
    for (int y = OY; y < OY + CH; ++y)
        for (int x = OX; x < OX + CW; ++x) {
            const int i = y * SIM_W + x;
            const Cell& c = g_world.cells[i];
            const u8 b[5] = { c.mat, c.moisture, c.tint, c.flags, g_world.temp[i] };
            for (int k = 0; k < 5; ++k) { h ^= b[k]; h *= 16777619u; }
        }
    printf("  census   %ld cells of %s   hash %08x\n", count, MATS[mat].name, h);

    /* A picture of the last frame, for looking at what the water does. */
    if (argc > 4 && strcmp(argv[4], "-")) {
        FILE* fp = fopen(argv[4], "wb");
        if (fp) {
            fprintf(fp, "P6\n%d %d\n255\n", CW, CH);
            for (int i = 0; i < CW * CH; ++i) {
                const u8 rgb[3] = { (u8)(g_px[i] >> 16), (u8)(g_px[i] >> 8), (u8)g_px[i] };
                fwrite(rgb, 1, 3, fp);
            }
            fclose(fp);
        }
    }
    free(s);
    return 0;
}
