// Standalone browser-shim regression test, without SDL or Emscripten:
// g++ -std=c++11 -O2 -U_WIN32 tests/web/blit_test.cpp -o build/blit_test.exe
// build/blit_test.exe
// Kept below tests/web because the ordinary native suite links real Win32 GDI.
#include <algorithm>
#include <cstdio>
#include <vector>
#include "../../src/web/gdi.cpp"

// The pre-optimization algorithm is the oracle, including source clamping and
// sequential reads/writes when the source and destination overlap.
static void originalBlit(WDC* dst, int dx, int dy, int dw, int dh,
                         const uint32_t* src, int stride, int srcW, int srcH,
                         int sx, int sy, int sw, int sh, bool keyed, uint32_t key) {
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    for (int y = 0; y < dh; ++y) {
        const int ty = dy + y;
        if (ty < 0 || ty >= dst->h) continue;
        const int srcY = iclamp(sy + (int)((long long)y * sh / dh), 0, srcH - 1);
        const uint32_t* srow = src + (size_t)srcY * stride;
        uint32_t* drow = dst->px + (size_t)ty * dst->w;
        for (int x = 0; x < dw; ++x) {
            const int tx = dx + x;
            if (tx < 0 || tx >= dst->w) continue;
            const int srcX = iclamp(sx + (int)((long long)x * sw / dw), 0, srcW - 1);
            const uint32_t c = srow[srcX];
            if (keyed && (c & 0x00FFFFFF) == (key & 0x00FFFFFF)) continue;
            drow[tx] = c;
        }
    }
}

static uint32_t rng = 0x312B1479u;
static uint32_t randomValue() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static int randomInt(int lo, int hi) {
    return lo + (int)(randomValue() % (unsigned)(hi - lo + 1));
}

int main() {
    const uint32_t key = 0x7FAB1256u;
    const int cases = 40000;
    for (int n = 0; n < cases; ++n) {
        const bool aliased = n % 4 == 0;
        const int dstW = randomInt(1, 80), dstH = randomInt(1, 65);
        const int srcW = aliased ? dstW : randomInt(1, 71);
        const int srcH = aliased ? dstH : randomInt(1, 57);
        const int stride = aliased ? srcW : srcW + randomInt(0, 6);
        const int guard = 16;
        const int sourceOffset = aliased ? randomInt(0, guard * 2) : 0;
        std::vector<uint32_t> expected((size_t)dstW * dstH + guard * 3);
        for (size_t i = 0; i < expected.size(); ++i)
            expected[i] = i % 5 == 0 ? key ^ (randomValue() & 0xFF000000u) : randomValue();
        std::vector<uint32_t> actual = expected;
        std::vector<uint32_t> source((size_t)stride * srcH);
        for (size_t i = 0; i < source.size(); ++i)
            source[i] = i % 3 == 0 ? key ^ (randomValue() & 0xFF000000u) : randomValue();
        const uint32_t* expectedSrc = aliased ? expected.data() + sourceOffset : source.data();
        const uint32_t* actualSrc = aliased ? actual.data() + sourceOffset : source.data();
        WDC expectedDC = {}, actualDC = {};
        expectedDC.px = expected.data() + guard;
        actualDC.px = actual.data() + guard;
        expectedDC.w = actualDC.w = dstW;
        expectedDC.h = actualDC.h = dstH;
        const int dx = randomInt(-dstW, dstW + 10), dy = randomInt(-dstH, dstH + 10);
        const int sx = randomInt(-srcW, srcW + 5), sy = randomInt(-srcH, srcH + 5);
        const int sw = randomInt(-2, srcW * 2), sh = randomInt(-2, srcH * 2);
        const int dw = n % 3 == 0 ? sw : randomInt(-2, dstW * 3);
        const int dh = n % 3 == 0 ? sh : randomInt(-2, dstH * 3);
        const bool keyed = randomInt(0, 1) != 0;
        originalBlit(&expectedDC, dx, dy, dw, dh, expectedSrc, stride, srcW, srcH,
                     sx, sy, sw, sh, keyed, key);
        blitScaled(&actualDC, dx, dy, dw, dh, actualSrc, stride, srcW, srcH,
                   sx, sy, sw, sh, keyed, key);
        if (actual != expected) {
            std::fprintf(stderr, "Blit mismatch case %d: alias=%d key=%d dst=%dx%d at %d,%d size=%dx%d src=%dx%d stride=%d at %d,%d size=%dx%d\n",
                         n, aliased, keyed, dstW, dstH, dx, dy, dw, dh,
                         srcW, srcH, stride, sx, sy, sw, sh);
            return 1;
        }
    }
    std::printf("PASS: %d blits match the original, including clipping, clamping, scaling, transparency, stride, and overlap.\n", cases);
    return 0;
}
