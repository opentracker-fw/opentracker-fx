// DIRT bench (core tests/bench_api.h): one stereo block at the costliest
// settings (Asym with its DC block, Drive 100, Bits 4, Rate 1, Tone 60, Mix 70).
#include <cstdio>

#include "audio/audio_tables.h"
#include "bench_api.h"
#include "dirt.h"

void dirtBench() {
    using namespace ot::audio;
    static dirt::Dirt d;
    static float l[kBlock], r[kBlock];
    dirt::Params p;
    p.on = 1;
    p.type = dirt::kAsym;
    p.drive = 100;
    p.bits = 4;
    p.rate = 1;
    p.tone = 60;
    p.mix = 70;
    d.setParams(p);
    uint32_t x = 1;
    const int n = 400 / otbench::scale();
    double sum = 0.0;
    for (int b = 0; b < n; ++b) {
        for (uint32_t i = 0; i < kBlock; ++i) {
            x = x * 1664525u + 1013904223u;
            l[i] = float(int32_t(x)) * (0.5f / 2147483648.0f);
            r[i] = -l[i];
        }
        const double t0 = otbench::nowUnits();
        d.process(l, r, kBlock);
        sum += otbench::nowUnits() - t0;
    }
    std::printf("    dirt.unit %8.1f  (one stereo block, Asym, Drive 100, Bits 4, Tone 60, limit 9000 M4 insn)\n", sum / n);
}
