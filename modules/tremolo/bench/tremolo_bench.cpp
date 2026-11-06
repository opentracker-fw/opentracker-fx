// TREM bench (core tests/bench_api.h): one stereo block of the costliest
// setting (Both, Square, Rate / Depth 100, a Mode change crossfading).
#include <cstdio>

#include "audio/audio_tables.h"
#include "bench_api.h"
#include "tremolo.h"

void tremoloBench() {
    using namespace ot::audio;
    alignas(8) static uint8_t state[kEffectStateMax];
    static float l[kBlock], r[kBlock];
    uint32_t x = 1;
    const int n = 400 / otbench::scale();
    double sum = 0.0;
    for (int b = 0; b < n; ++b) {
        const EffectSettings s = {{uint8_t(b & 1 ? tremolo::kBoth : tremolo::kPan), 100, 100, tremolo::kSquare}};
        for (uint32_t i = 0; i < kBlock; ++i) {
            x = x * 1664525u + 1013904223u;
            l[i] = float(int32_t(x)) * (0.5f / 2147483648.0f);
            r[i] = -l[i];
        }
        const double t0 = otbench::nowUnits();
        tremolo::gTremoloEffect.process(state, nullptr, s, true, l, r, kBlock);
        sum += otbench::nowUnits() - t0;
    }
    std::printf("    trem.unit %8.1f  (one stereo block, Both / Pan alternating, Square, limit 3000 M4 insn)\n", sum / n);
}
