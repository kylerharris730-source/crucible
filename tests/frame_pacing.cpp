#include "frame_pacing.h"
#include <stdio.h>

int main() {
    const int rates[] = { 30, 60, 75, 120, 144, 240 };
    for (int r = 0; r < 6; ++r) {
        BrowserFramePacer pacer;
        int frames = 0;
        for (int i = 0; i < rates[r] * 10; ++i)
            if (pacer.due(i * 1000.0 / rates[r])) ++frames;
        const int expected = (rates[r] < 60 ? rates[r] : 60) * 10;
        if (frames != expected) {
            printf("%d Hz: %d frames, expected %d\n", rates[r], frames, expected);
            return 1;
        }
    }
    BrowserFramePacer pacer;
    if (!pacer.due(0.0) || pacer.due(1.0) || !pacer.due(16.6)) return 2;
    // A minute hidden gets one tick on return, with no burst of catch-up work.
    if (!pacer.due(60000.0) || pacer.due(60001.0) ||
        !pacer.due(60016.667)) return 3;
    puts("browser frame pacing passed");
    return 0;
}
