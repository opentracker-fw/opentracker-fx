// CHORUS bench (core tests/bench_api.h): one stereo block, Depth / Width /
// Mix at 100, the fastest rate.
#include <cstdio>

#include "audio/audio_tables.h"
#include "bench_api.h"
#include "chorus.h"

void chorusBench() {
    using namespace ot::audio;
    static chorus::Chorus c;
    static float l[kBlock], r[kBlock];
    chorus::Params p;
    p.on = 1;
    p.rate = p.depth = p.width = p.mix = 100;
    c.setParams(p);
    uint32_t x = 7;
    const int n = 400 / otbench::scale();
    double sum = 0.0;
    for (int b = 0; b < n; ++b) {
        for (uint32_t i = 0; i < kBlock; ++i) {
            x = x * 1664525u + 1013904223u;
            l[i] = float(int32_t(x)) * (0.5f / 2147483648.0f);
            r[i] = 0.5f * l[i];
        }
        const double t0 = otbench::nowUnits();
        c.process(l, r, kBlock);
        sum += otbench::nowUnits() - t0;
    }
    std::printf("    chor.unit %8.1f  (one stereo block, depth / width / mix 100, limit 12000 M4 insn)\n", sum / n);
}
