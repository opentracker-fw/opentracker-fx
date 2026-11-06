// PHSR phaser effect (opentracker-fx modules/phaser): the settings table; a
// fresh Off instance leaves the bus untouched; both modes cut notches into the
// spectrum (Classic two, 80s four); the LFO moves them; Spread splits the
// channels; switching on, off and between modes does not click; Off fades out
// and then passes the bus bit for bit; random settings stay bounded; the
// module offers the effect.
#include "test.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "audio/synth_engine.h"
#include "modules/module.h"
#include "phaser.h"

using namespace ot;
using namespace ot::audio;

namespace ot {
namespace mod {
IModule& phaserModule();
}  // namespace mod
}  // namespace ot

namespace {
constexpr uint32_t kN = kBlock;
constexpr float kFs = ot::audio::kSampleRate;
using Buf = std::vector<float>;
IEffect& fx = phaser::gPhaserEffect;

struct Inst {
    alignas(8) uint8_t state[kEffectStateMax] = {};
    alignas(8) uint8_t work[kEffectWorkMax] = {};
    bool run(const EffectSettings& s, bool on, float* l, float* r) { return fx.process(state, work, s, on, l, r, kN); }
};

// Mode, Rate, Depth, Feedback, Spread, Mix.
EffectSettings set(uint8_t mode, uint8_t rate = 40, uint8_t depth = 80, uint8_t fb = 30, uint8_t spread = 0,
                   uint8_t mix = 50) {
    return EffectSettings{{mode, rate, depth, fb, spread, mix}};
}

// Gain of the effect at a sine of hz (steady state, LFO frozen: Depth 0).
float gainAt(uint8_t mode, float hz, uint8_t fb = 0) {
    Inst in;
    const EffectSettings s = set(mode, 0, 0, fb);
    float l[kN], r[kN];
    double si = 0, so = 0;
    for (int b = 0; b < 120; ++b) {
        for (uint32_t i = 0; i < kN; ++i) l[i] = r[i] = 0.5f * std::sin(6.2831853f * hz * float(b * kN + i) / kFs);
        float x[kN];
        std::memcpy(x, l, sizeof x);
        in.run(s, true, l, r);
        if (b >= 60)
            for (uint32_t i = 0; i < kN; ++i) {
                si += double(x[i]) * x[i];
                so += double(l[i]) * l[i];
            }
    }
    return float(std::sqrt(so / si));
}

// Notches: local minima of the gain below 0.25 on a log sweep 12 Hz .. 16 kHz.
int notches(uint8_t mode) {
    std::vector<float> g;
    for (float hz = 12.0f; hz < 16000.0f; hz *= 1.03f) g.push_back(gainAt(mode, hz));
    int n = 0;
    for (size_t i = 1; i + 1 < g.size(); ++i)
        if (g[i] < 0.25f && g[i] <= g[i - 1] && g[i] < g[i + 1]) ++n;
    return n;
}

float maxStep(const Buf& x, size_t from, size_t to) {
    float m = 0;
    for (size_t i = from + 1; i < to; ++i) m = std::fmax(m, std::fabs(x[i] - x[i - 1]));
    return m;
}
}  // namespace

TEST(phaser_settings_table) {
    CHECK(std::strcmp(fx.id(), "PHAS") == 0);
    CHECK(std::strcmp(fx.name(), "Phaser") == 0);
    CHECK_EQ(fx.paramCount(), 6);
    const char* labels[6] = {"Mode", "Rate", "Depth", "Feedback", "Spread", "Mix"};
    for (int i = 0; i < 6; ++i) CHECK(std::strcmp(fx.param(i).label, labels[i]) == 0);
    CHECK_EQ(int(fx.param(0).max), 1);
    CHECK(fx.param(0).names != nullptr);
    CHECK(std::strcmp(fx.param(0).names[0], "Classic") == 0);
    CHECK(std::strcmp(fx.param(0).names[1], "80s") == 0);
    for (int i = 1; i < 6; ++i) CHECK_EQ(int(fx.param(i).max), 100);
    CHECK_EQ(int(fx.param(5).def), 50);
    CHECK(fx.stateBytes() <= kEffectStateMax);
    CHECK(fx.workBytes() > 0 && fx.workBytes() <= kEffectWorkMax);
}

TEST(phaser_fresh_off_leaves_the_bus_untouched) {
    Inst in;
    float l[kN], r[kN], l0[kN], r0[kN];
    for (uint32_t i = 0; i < kN; ++i) l[i] = l0[i] = std::sin(0.01f * float(i)), r[i] = r0[i] = -l[i];
    CHECK(!in.run(set(phaser::kClassic), false, l, r));
    CHECK(std::memcmp(l, l0, sizeof l) == 0);
    CHECK(std::memcmp(r, r0, sizeof r) == 0);
}

TEST(phaser_classic_cuts_two_notches_80s_four) {
    CHECK_EQ(notches(phaser::kClassic), 2);
    CHECK_EQ(notches(phaser::k80s), 4);
    // Between the notches the level comes back (chain in phase with the dry).
    float g = 0;
    for (float hz = 12.0f; hz < 16000.0f; hz *= 1.05f) g = std::fmax(g, gainAt(phaser::kClassic, hz));
    CHECK(g > 0.95f);
}

TEST(phaser_feedback_deepens_the_peaks) {
    float g0 = 0, g1 = 0;
    for (float hz = 12.0f; hz < 16000.0f; hz *= 1.05f) {
        g0 = std::fmax(g0, gainAt(phaser::kClassic, hz, 0));
        g1 = std::fmax(g1, gainAt(phaser::kClassic, hz, 100));
    }
    CHECK(g0 < 1.05f);
    CHECK(g1 > 1.2f);
    CHECK(g1 < 3.0f);   // compensated: no runaway level
}

TEST(phaser_lfo_moves_the_notch) {
    // A 500 Hz sine through a sweeping phaser: its level rises and falls.
    Inst in;
    const EffectSettings s = set(phaser::kClassic, 60, 100, 0);
    float l[kN], r[kN];
    float lo = 1e9f, hi = 0;
    for (int b = 0; b < 1400; ++b) {
        for (uint32_t i = 0; i < kN; ++i) l[i] = r[i] = 0.5f * std::sin(6.2831853f * 500.0f * float(b * kN + i) / kFs);
        in.run(s, true, l, r);
        if (b < 20) continue;
        float pk = 0;
        for (uint32_t i = 0; i < kN; ++i) pk = std::fmax(pk, std::fabs(l[i]));
        lo = std::fmin(lo, pk);
        hi = std::fmax(hi, pk);
    }
    CHECK(hi > 0.45f);
    CHECK(lo < 0.15f);
}

TEST(phaser_spread_splits_the_channels) {
    for (int spread = 0; spread <= 100; spread += 100) {
        Inst in;
        const EffectSettings s = set(phaser::k80s, 50, 100, 30, uint8_t(spread));
        float l[kN], r[kN];
        double d = 0;
        for (int b = 0; b < 300; ++b) {
            for (uint32_t i = 0; i < kN; ++i) l[i] = r[i] = 0.4f * std::sin(0.05f * float(b * kN + i));
            in.run(s, true, l, r);
            for (uint32_t i = 0; i < kN; ++i) d += std::fabs(l[i] - r[i]);
        }
        if (spread == 0) CHECK(d == 0.0);
        else CHECK(d > 100.0);
    }
}

TEST(phaser_off_fades_out_then_passes_bit_for_bit) {
    Inst in;
    float l[kN], r[kN];
    for (int b = 0; b < 20; ++b) {
        for (uint32_t i = 0; i < kN; ++i) l[i] = r[i] = 0.3f * std::sin(0.03f * float(b * kN + i));
        CHECK(in.run(set(phaser::k80s), true, l, r));
    }
    int passed = -1;
    for (int b = 0; b < 4; ++b) {
        float l0[kN];
        for (uint32_t i = 0; i < kN; ++i) l[i] = r[i] = l0[i] = 0.3f * std::sin(0.03f * float((20 + b) * kN + i));
        const bool live = in.run(set(phaser::k80s), false, l, r);
        if (!live && passed < 0) {
            passed = b;
            CHECK(std::memcmp(l, l0, sizeof l) == 0);
        }
    }
    CHECK(passed >= 1 && passed <= 2);
}

// The largest sample step across a switch, against the steady steps of the
// same signal: no click.
static void noClick(EffectSettings a, bool onA, EffectSettings b, bool onB) {
    Inst in;
    const int blocks = 60, at = 40;
    Buf out(size_t(blocks) * kN), outR(out.size());
    for (int k = 0; k < blocks; ++k) {
        float* l = out.data() + k * kN;
        float* r = outR.data() + k * kN;
        for (uint32_t i = 0; i < kN; ++i) l[i] = r[i] = 0.4f * std::sin(6.2831853f * 220.0f * float(k * kN + i) / kFs);
        if (k < at) in.run(a, onA, l, r);
        else in.run(b, onB, l, r);
    }
    const float steady = maxStep(out, size_t(at - 10) * kN, size_t(at) * kN);
    const float sw = maxStep(out, size_t(at) * kN - 1, size_t(at + 2) * kN);
    const float dry = 0.4f * 6.2831853f * 220.0f / kFs;   // the dry sine's largest step
    CHECK(sw < 1.5f * std::fmax(steady, dry));
}

TEST(phaser_switching_does_not_click) {
    noClick(set(phaser::kClassic), false, set(phaser::kClassic, 40, 100, 100), true);   // on
    noClick(set(phaser::k80s, 40, 100, 100), true, set(phaser::k80s), false);           // off
    noClick(set(phaser::kClassic, 40, 100, 60), true, set(phaser::k80s, 40, 100, 60), true);
    noClick(set(phaser::k80s, 40, 100, 60), true, set(phaser::kClassic, 40, 100, 60), true);
    noClick(set(phaser::k80s, 0, 0, 0, 0, 0), true, set(phaser::k80s, 100, 100, 100, 100, 100), true);
}

TEST(phaser_random_settings_stay_bounded) {
    Inst in;
    uint32_t x = 7;
    float l[kN], r[kN];
    float peak = 0;
    for (int b = 0; b < 3000; ++b) {
        if (b % 50 == 0) x = x * 1664525u + 1013904223u;
        const EffectSettings s = set(uint8_t((x >> 3) & 1), uint8_t((x >> 5) % 101), uint8_t((x >> 12) % 101),
                                     uint8_t((x >> 19) % 101), uint8_t((x >> 26) % 101), uint8_t((x >> 9) % 101));
        for (uint32_t i = 0; i < kN; ++i) {
            x = x * 1664525u + 1013904223u;
            l[i] = float(int32_t(x)) / 2147483648.0f;
            r[i] = -l[i];
        }
        in.run(s, (b / 200) % 4 != 3, l, r);
        for (uint32_t i = 0; i < kN; ++i) {
            CHECK(std::isfinite(l[i]) && std::isfinite(r[i]));
            peak = std::fmax(peak, std::fmax(std::fabs(l[i]), std::fabs(r[i])));
        }
    }
    CHECK(peak < 4.0f);
}

TEST(phaser_module_offers_the_effect) {
    mod::IModule& m = mod::phaserModule();
    CHECK(m.info().caps & mod::kCapEffect);
    CHECK(m.effect(0) == &phaser::gPhaserEffect);
    CHECK(m.effect(1) == nullptr);
    CHECK(m.masterInsert() == nullptr);
}
