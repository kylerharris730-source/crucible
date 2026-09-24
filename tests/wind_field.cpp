/* --- the wind field: a plume makes a draught, and the draught goes away -------

   Asked for: "an air system, like a vector field ... more realistic gas
   behaviour with less square artifacting for a somewhat low computational
   cost". See the Wind section of world.cpp.

   Four properties:

     a rising plume drags air up its middle      (buoyancy reaches the field)
     air comes in along the floor to replace it  (the projection is working:
                                                  without it there is only
                                                  updraft and no inflow)
     the world steps the same at 1 and 4 threads (the field is read-only in
                                                  the scan)
     once everything settles the wind sleeps     (no whole-world cost, and no
                                                  draught left in a quiet cave)

   Compile with every src/*.cpp except main.cpp. No socket, no window. Do not
   name the output *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include <stdio.h>
#include <math.h>
#include <windows.h>

static World g_testWorld;
static const int X0 = 1024, Y0 = 4800, W = 256, H = 256;
static const int X1 = X0 + W - 1, Y1 = Y0 + H - 1, MID = X0 + W / 2;

static void buildBox(World& w) {
    w.reset();
    w.setLiveWindow(X0, Y0, X1, Y1);
    for (int y = Y0; y <= Y1; ++y)
        for (int x = X0; x <= X1; ++x)
            if (x < X0 + 2 || x > X1 - 2 || y < Y0 + 2 || y > Y1 - 2)
                w.setCell(x, y, MAT_WALL);
}

/* A steam vent on the floor: a few hot parcels a frame, the way a boiler
   lets them go. */
static void vent(World& w) {
    for (int x = MID - 6; x <= MID + 6; x += 3) {
        const int y = Y1 - 4;
        if (w.at(x, y).mat == MAT_EMPTY) {
            w.setCell(x, y, MAT_STEAM);
            w.temp[y * SIM_W + x] = degC(115);
        }
    }
}

static u64 worldHash(const World& w) {
    u64 h = 1469598103934665603ull;
    for (int y = Y0; y <= Y1; ++y)
        for (int x = X0; x <= X1; ++x) {
            h ^= (u64)w.at(x, y).mat | ((u64)w.temp[y * SIM_W + x] << 8);
            h *= 1099511628211ull;
        }
    return h;
}

static u64 runScene(int threads, int frames) {
    simSetWorkers(threads);
    g_rng = 0x9E3779B9u;   /* reset() leaves the stream where it was */
    buildBox(g_testWorld);
    for (int f = 0; f < frames; ++f) {
        vent(g_testWorld);
        g_testWorld.step();
    }
    return worldHash(g_testWorld);
}

int main() {
    initMaterials();
    int failures = 0;

    /* --- the plume ---------------------------------------------------------- */
    LARGE_INTEGER f0, f1, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&f0);
    runScene(1, 240);
    QueryPerformanceCounter(&f1);
    const double ms = 1000.0 * (double)(f1.QuadPart - f0.QuadPart) / (double)freq.QuadPart / 240.0;

    float vx, vy;
    /* Up the middle, a third of the way up the box. */
    float up = 0.0f;
    for (int y = Y1 - 100; y <= Y1 - 40; y += 8) { g_testWorld.windAt(MID, y, vx, vy); up += vy; }
    up /= 8.0f;
    /* Along the floor either side, well clear of the vent: inflow means the
       left side blows right (+) and the right side blows left (-). */
    float inL = 0.0f, inR = 0.0f;
    for (int d = 24; d <= 72; d += 8) {
        g_testWorld.windAt(MID - d, Y1 - 10, vx, vy); inL += vx;
        g_testWorld.windAt(MID + d, Y1 - 10, vx, vy); inR += vx;
    }
    printf("plume: updraft %.3f cells/frame, floor inflow %+.3f / %+.3f, %d wind chunks, %.2f ms/frame\n",
           -up, inL / 7.0f, inR / 7.0f, g_testWorld.windChunks, ms);
    if (!(up < -0.05f)) { printf("FAIL: no updraft over the vent\n"); ++failures; }
    if (!(inL > 0.0f && inR < 0.0f)) { printf("FAIL: no inflow along the floor\n"); ++failures; }

    /* --- thread count ------------------------------------------------------- */
    const u64 h0 = runScene(1, 150), h1 = runScene(1, 150), h4 = runScene(4, 150);
    printf("hash 1 thread %016llx (again %016llx), 4 threads %016llx\n",
           (unsigned long long)h0, (unsigned long long)h1, (unsigned long long)h4);
    if (h0 != h1) { printf("FAIL: the same scene stepped differently twice\n"); ++failures; }
    if (h1 != h4) { printf("FAIL: wind made the world depend on thread count\n"); ++failures; }

    /* --- it goes to sleep --------------------------------------------------- */
    simSetWorkers(1);
    for (int y = Y0 + 2; y <= Y1 - 2; ++y)
        for (int x = X0 + 2; x <= X1 - 2; ++x) {
            g_testWorld.setCell(x, y, MAT_EMPTY);
            g_testWorld.temp[y * SIM_W + x] = AMBIENT_TEMP;
        }
    int frames = 0;
    while (g_testWorld.windChunks > 0 && frames < 2000) { g_testWorld.step(); ++frames; }
    float peak = 0.0f;
    for (int i = 0; i < WIND_N; ++i)
        peak = fmaxf(peak, fabsf(g_testWorld.windVX[i]) + fabsf(g_testWorld.windVY[i]));
    printf("settled: wind asleep after %d frames, peak %.4f\n", frames, peak);
    if (g_testWorld.windChunks != 0 || peak != 0.0f) {
        printf("FAIL: the wind never went to sleep\n"); ++failures;
    }

    if (failures) return 1;
    printf("PASS: plume draws air up and in, deterministic across threads, sleeps when still\n");
    return 0;
}
