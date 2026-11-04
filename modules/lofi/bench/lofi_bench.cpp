// LOFI bench (core tests/bench_api.h): one stereo block at the costliest
// settings (stereo, Bits 8 with Ladder 100 every frame (Rate Off), Speaker Handheld with
// its bump, Mix 70).
#include <cstdio>

#include "audio/audio_tables.h"
#include "bench_api.h"
#include "lofi.h"

void lofiBench() {
    using namespace ot::audio;
    static lofi::Lofi d;
    static float l[kBlock], r[kBlock];
    lofi::Params p;
    p.on = 1;
    p.bits = 8;
    p.ladder = 100;
    p.rate = lofi::kRateOff;   // the DAC every frame (a hold runs it only at the hold points)
    p.speaker = lofi::kHandheld;
    p.mono = 0;   // stereo: both channels through every stage (Mono does one)
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
    std::printf("    lofi.unit %8.1f  (one stereo block, Bits 8, Ladder 100, Rate Off, Speaker Handheld, Mix 70, limit 12500 M4 insn)\n",
                sum / n);
}
