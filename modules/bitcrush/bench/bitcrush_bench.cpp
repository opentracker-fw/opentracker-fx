// CRSH bench (core tests/bench_api.h): one stereo block of the costliest
// setting (Soft, Crush 12, Down 0: shaped and quantised every frame, Mix 70).
#include <cstdio>

#include "audio/audio_tables.h"
#include "bench_api.h"
#include "bitcrush.h"

void bitcrushBench() {
    using namespace ot::audio;
    alignas(8) static uint8_t state[kEffectStateMax];
    static float l[kBlock], r[kBlock];
    const EffectSettings s = {{bitcrush::kSoft, 12, 0, 70}};
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
        bitcrush::gBitcrushEffect.process(state, nullptr, s, true, l, r, kBlock);
        sum += otbench::nowUnits() - t0;
    }
    std::printf("    crsh.unit %8.1f  (one stereo block, Soft, Crush 12, Down 0, Mix 70, limit 9500 M4 insn)\n", sum / n);
}
