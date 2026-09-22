#pragma once

/* A browser may call back at 60, 120, 144 Hz or after a suspended tab resumes.
   Run at most one game tick per callback, capped at 60 Hz. Missed ticks are
   discarded instead of creating a catch-up spiral on an overloaded machine. */
struct BrowserFramePacer {
    double nextMs;
    bool started;

    BrowserFramePacer() : nextMs(0.0), started(false) {}

    bool due(double nowMs) {
        const double interval = 1000.0 / 60.0;
        if (!started) { nextMs = nowMs; started = true; }
        // RAF timestamps can be rounded; do not miss a refresh for <0.1 ms.
        if (nowMs + 0.1 < nextMs) return false;
        nextMs += interval;
        if (nextMs <= nowMs) nextMs = nowMs + interval;
        return true;
    }
};
