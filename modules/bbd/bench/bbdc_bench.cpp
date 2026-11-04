// BBDC bench (core tests/bench_api.h): one stereo block, Chorus II (the
// faster, deeper mode; I costs the same), Mix and Width 100.
#include <cstdio>

#include "audio/audio_tables.h"
#include "bench_api.h"
#include "bbdc.h"

void bbdcBench() {
    using namespace ot::audio;
    static int16_t line[bbdc::kLine];
    static bbdc::BbdChorus j(line);
    static float l[kBlock], r[kBlock];
    bbdc::Params p;
    p.mode = 2;
    j.setParams(p);
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
        j.process(l, r, kBlock);
        sum += otbench::nowUnits() - t0;
    }
    std::printf("    bbdc.unit %8.1f  (one stereo block, Chorus II, mix / width 100, limit 12000 M4 insn)\n", sum / n);
}
