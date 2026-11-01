// ECHO bench (core tests/bench_api.h): one stereo block at the costliest
// settings (ping-pong, the time changing every block: two more taps and the
// crossfade; tone and low cut on, duck, feedback 90).
#include <cstdio>
#include <vector>

#include "audio/audio_tables.h"
#include "bench_api.h"
#include "echo.h"

void echoBench() {
    using namespace ot::audio;
    static echo::Echo e;
    static int16_t line[echo::kLine];
    static float l[kBlock], r[kBlock];
    e.setLine(line);
    echo::Params p;
    p.on = 1;
    p.time = 4;
    p.feedback = 90;
    p.lowcut = 40;
    p.tone = 50;
    p.pingpong = 1;
    p.duck = 50;
    p.mix = 50;
    e.setParams(p);
    e.setTempo(125.0f);
    uint32_t x = 1;
    const int n = 400 / otbench::scale();
    double sum = 0.0;
    for (int b = 0; b < n; ++b) {
        for (uint32_t i = 0; i < kBlock; ++i) {
            x = x * 1664525u + 1013904223u;
            l[i] = float(int32_t(x)) * (0.5f / 2147483648.0f);
            r[i] = -l[i];
        }
        e.setTempo(b % 2 ? 125.0f : 126.0f);   // a crossfade in every block
        const double t0 = otbench::nowUnits();
        e.process(l, r, kBlock);
        sum += otbench::nowUnits() - t0;
    }
    std::printf("    echo.unit %8.1f  (one stereo block, ping-pong, a time crossfade, tone, low cut, duck, limit 9000 M4 insn)\n",
                sum / n);
}
