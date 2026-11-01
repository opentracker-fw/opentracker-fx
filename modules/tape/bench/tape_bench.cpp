// TAPE bench (core tests/bench_api.h): one stereo block with every stage at
// full depth (drive 100, wow 100, flutter 100, mix 100).
#include <cstdio>

#include "audio/audio_tables.h"
#include "bench_api.h"
#include "tape.h"

void tapeBench() {
    using namespace ot::audio;
    static tape::Tape t;
    static float l[kBlock], r[kBlock];
    tape::Params p;
    p.on = 1;
    p.drive = p.wow = p.flutter = p.mix = 100;
    t.setParams(p);
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
        t.process(l, r, kBlock);
        sum += otbench::nowUnits() - t0;
    }
    std::printf("    tape.unit %8.1f  (one stereo block, every stage at full depth, limit 15000 M4 insn)\n", sum / n);
}
