/* --- the wind holds on, and a knocked plume stands back up ---------------------

   Reported: "a clone producing plasma has the plasma climb upwards, but if you
   mess with it ... the plasma will now drift left, and it like never stops".
   And: all the air's state was dropped when a chunk went quiet, so a flow
   stopped dead if the gas in part of it thinned out for a moment.

   Four properties, on a plasma clone on the floor of a closed box the size of
   the sandbox's Normal scale:

     a gust through the plume blows over    (it straightens within seconds)
     a warm column beside it does too       (warm air is carried by the wind;
                                             it used to sit where it was
                                             warmed and hold the plume over
                                             for twenty seconds)
     the wind outlasts its gas              (five seconds of grace, not 1.5)
     the wind freezes with the live window  (and comes back as it was)

   A plume is a noisy thing to measure, and one run of either of the first two
   can land a couple of cells either way of the truth. So each is run from
   several seeds, the lean is measured against where that same plume stood
   before it was disturbed, and it is averaged over a second of frames.

   SEEDS=n in the environment runs more of them, for tuning.

   Compile with every src/*.cpp except main.cpp. Do not name the output
   *_test.exe -- build.bat deletes those. */

#include "world.h"
#include "materials.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static World g_w;
static const int X0 = 1024, Y0 = 4608, W = 512, H = 384;
static const int X1 = X0 + W - 1, Y1 = Y0 + H - 1, MID = X0 + W / 2;
static const int SRC_Y = Y1 - 6;

/* Mean x of the plasma, relative to the clone, over the rows 20 to 120 above
   it. The column's middle, not its tip, is what shows a lean. */
static float offset() {
    double sx = 0.0; int n = 0;
    for (int y = SRC_Y - 120; y <= SRC_Y - 20; ++y)
        for (int x = X0 + 2; x <= X1 - 2; ++x)
            if (g_w.at(x, y).mat == MAT_PLASMA) { sx += x - MID; ++n; }
    return n ? (float)(sx / n) : 0.0f;
}

/* The same, averaged over the next second. */
static float offsetOverASecond() {
    float s = 0.0f;
    for (int k = 0; k < 6; ++k) {
        for (int f = 0; f < 10; ++f) g_w.step();
        s += offset();
    }
    return s / 6.0f;
}

/* The fastest wind anywhere in the box. */
static float peakWind() {
    float peak = 0.0f;
    for (int ay = Y0 >> WIND_SHIFT; ay <= Y1 >> WIND_SHIFT; ++ay)
        for (int ax = X0 >> WIND_SHIFT; ax <= X1 >> WIND_SHIFT; ++ax) {
            const int w = ay * WIND_PITCH + ax;
            peak = fmaxf(peak, fabsf(g_w.windVX[w]) + fabsf(g_w.windVY[w]));
        }
    return peak;
}

static void steps(int n) { for (int f = 0; f < n; ++f) g_w.step(); }

/* A fresh box with the clone going, settled for ten seconds. Returns where
   the plume stands. */
static float settle(u32 seed) {
    g_rng = 0x9E3779B9u + seed * 0x632BE5ABu;
    g_w.reset();
    g_w.setLiveWindow(X0, Y0, X1, Y1);
    for (int y = Y0; y <= Y1; ++y)
        for (int x = X0; x <= X1; ++x)
            if (x < X0 + 2 || x > X1 - 2 || y < Y0 + 2 || y > Y1 - 2)
                g_w.setCell(x, y, MAT_WALL);
    g_w.setCell(MID, SRC_Y, MAT_CLONE);
    g_w.cells[SRC_Y * SIM_W + MID].moisture = MAT_PLASMA;
    steps(540);
    return offsetOverASecond();
}

/* The lean over the last half second of the disturbance, and five and ten
   seconds after, each against where the plume stood before it. */
struct Lean { float at, after5, after10; };

static void follow(float home, float during, Lean& out) {
    out.at = during - home;
    steps(240);
    out.after5 = offsetOverASecond() - home;
    steps(240);
    out.after10 = offsetOverASecond() - home;
}

/* A wind-brush stroke at full strength dragged right to left through the
   lower plume, four times over. */
static void gust(u32 seed, Lean& out) {
    const float home = settle(seed);
    float during = 0.0f;
    for (int f = 0; f < 120; ++f) {
        g_w.pushWind(MID + 60 - (f % 30) * 4, SRC_Y - 20 - (f / 30) * 20, 12, -0.6f, 0.0f);
        g_w.step();
        if (f >= 90) during += offset() / 30.0f;
    }
    follow(home, during, out);
}

/* Ten seconds of heat brush in a column to the left of the clone. */
static void warmColumn(u32 seed, Lean& out) {
    const float home = settle(seed);
    float during = 0.0f;
    for (int f = 0; f < 600; ++f) {
        g_w.heat(MID - 30, SRC_Y - 10 - (f % 60), 10, 6);
        g_w.step();
        if (f >= 570) during += offset() / 30.0f;
    }
    follow(home, during, out);
}

/* Mean over the seeds, of the signed lean and of its size. */
static void runSeeds(const char* name, void (*scene)(u32, Lean&), int seeds, Lean& mean, Lean& size) {
    mean.at = mean.after5 = mean.after10 = 0.0f;
    size = mean;
    for (int s = 0; s < seeds; ++s) {
        Lean l;
        scene((u32)s, l);
        if (getenv("SEEDS")) printf("  %s seed %d: %+6.1f  %+6.1f  %+6.1f\n", name, s, l.at, l.after5, l.after10);
        mean.at += l.at / seeds; mean.after5 += l.after5 / seeds; mean.after10 += l.after10 / seeds;
        size.at += fabsf(l.at) / seeds; size.after5 += fabsf(l.after5) / seeds; size.after10 += fabsf(l.after10) / seeds;
    }
    printf("%-9s lean %+6.1f when it stops, %+6.1f five seconds on, %+6.1f ten on (size %.1f / %.1f / %.1f)\n",
           name, mean.at, mean.after5, mean.after10, size.at, size.after5, size.after10);
}

int main() {
    initMaterials();
    simSetWorkers(1);
    int failures = 0;
    const int seeds = getenv("SEEDS") ? atoi(getenv("SEEDS")) : 3;

    /* --- a gust, and a warm column ------------------------------------------ */
    Lean m, s;
    runSeeds("gust:", gust, seeds, m, s);
    if (!(m.at < -3.0f)) { printf("FAIL: the gust never moved the plume -- the test is not testing\n"); ++failures; }
    if (!(s.after10 < 2.0f)) { printf("FAIL: a gust left the plume leaning\n"); ++failures; }
    runSeeds("warm air:", warmColumn, seeds, m, s);
    if (!(m.at < -3.0f)) { printf("FAIL: the warm column never drew the plume over -- the test is not testing\n"); ++failures; }
    if (!(s.after10 < 2.0f)) { printf("FAIL: the plume stayed in the warm column\n"); ++failures; }

    /* --- the wind outlasts its gas -----------------------------------------
       Clone and plasma gone at once. Two seconds later the draught they made
       is still there; well after the grace, and once it is still, it sleeps. */
    settle(0);
    for (int y = Y0 + 2; y <= Y1 - 2; ++y)
        for (int x = X0 + 2; x <= X1 - 2; ++x)
            if (g_w.at(x, y).mat == MAT_PLASMA || g_w.at(x, y).mat == MAT_CLONE) g_w.setCell(x, y, MAT_EMPTY);
    steps(120);
    const float graceWind = peakWind();
    int frames = 120;
    while (g_w.windChunks > 0 && frames < 3000) { g_w.step(); ++frames; }
    printf("grace:    peak wind %.3f two seconds after the gas, asleep after %d frames\n", graceWind, frames);
    if (!(graceWind > 0.02f)) { printf("FAIL: the wind died with its gas\n"); ++failures; }
    if (g_w.windChunks != 0 || peakWind() != 0.0f) { printf("FAIL: the wind never went to sleep\n"); ++failures; }

    /* --- frozen with the live window ---------------------------------------
       Look away, far enough that the box is outside the window, and wait out
       its grace: now the cells are frozen. Ten seconds more, and the wind
       should not have moved either.

       Twice the grace, because a lingering chunk can dirty a settled
       neighbour, and that neighbour's grace only starts counting then. A
       chunk still lingering is still simulated, wind and all, which is why
       this asks for nearly every block to hold rather than every one. It
       used to be none: the wind was zeroed a second and a half in. */
    settle(0);
    g_w.setLiveWindow(X0 + 2048, Y0 - 2048, X1 + 2048, Y1 - 2048);
    steps(LIVE_GRACE_STEPS * 2);
    float before[16 * 8];
    const int ax0 = (MID >> WIND_SHIFT) - 8, ay0 = ((SRC_Y - 60) >> WIND_SHIFT) - 4;
    for (int k = 0; k < 16 * 8; ++k) before[k] = g_w.windVY[(ay0 + k / 16) * WIND_PITCH + ax0 + k % 16];
    steps(600);
    int held = 0, moving = 0;
    for (int k = 0; k < 16 * 8; ++k) {
        const float now = g_w.windVY[(ay0 + k / 16) * WIND_PITCH + ax0 + k % 16];
        if (fabsf(before[k]) > 0.02f) { ++moving; if (now == before[k]) ++held; }
    }
    printf("frozen:   %d of %d moving blocks held their wind\n", held, moving);
    if (!(moving > 8 && held * 10 >= moving * 9)) { printf("FAIL: the wind drained away while the cells were frozen\n"); ++failures; }

    if (failures) return 1;
    printf("PASS: gusts and warm air blow over, wind outlasts its gas and freezes with the window\n");
    return 0;
}
