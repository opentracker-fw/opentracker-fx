// TREM tremolo / auto-pan effect (opentracker-fx modules/tremolo): the settings
// table; a fresh Off instance leaves the bus untouched; Depth 0 leaves it as it
// is; Level swings between 1 - Depth and 1 at the Rate; Pan keeps the power and
// moves the channels against each other; Square has no step; Off fades out and
// then passes the bus bit for bit; switching does not click; random settings
// stay bounded; the module offers the effect.
#include "test.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "audio/synth_engine.h"
#include "modules/module.h"
#include "tremolo.h"

using namespace ot;
using namespace ot::audio;

namespace ot {
namespace mod {
IModule& tremoloModule();
}  // namespace mod
}  // namespace ot

namespace {
constexpr uint32_t kN = kBlock;
constexpr float kFs = ot::audio::kSampleRate;
IEffect& fx = tremolo::gTremoloEffect;

struct Inst {
    alignas(8) uint8_t state[kEffectStateMax] = {};
    bool run(const EffectSettings& s, bool on, float* l, float* r) { return fx.process(state, nullptr, s, on, l, r, kN); }
};

// Mode, Rate, Depth, Shape.
EffectSettings set(uint8_t mode, uint8_t rate = 50, uint8_t depth = 60, uint8_t shape = tremolo::kSine) {
    return EffectSettings{{mode, rate, depth, shape, 0, 0}};
}

// Runs a constant input (both channels `in`) for `blocks`; the outputs.
void runDc(Inst& in, const EffectSettings& s, int blocks, std::vector<float>& L, std::vector<float>& R, float v = 0.5f) {
    L.assign(size_t(blocks) * kN, v);
    R.assign(L.size(), v);
    for (int b = 0; b < blocks; ++b) in.run(s, true, L.data() + b * kN, R.data() + b * kN);
}
}  // namespace

TEST(tremolo_settings_table) {
    CHECK(std::strcmp(fx.id(), "TREM") == 0);
    CHECK(std::strcmp(fx.name(), "Tremolo") == 0);
    CHECK_EQ(fx.paramCount(), 4);
    const char* labels[4] = {"Mode", "Rate", "Depth", "Shape"};
    for (int i = 0; i < 4; ++i) CHECK(std::strcmp(fx.param(i).label, labels[i]) == 0);
    CHECK_EQ(int(fx.param(0).max), 2);
    CHECK(std::strcmp(fx.param(0).names[0], "Level") == 0);
    CHECK(std::strcmp(fx.param(0).names[1], "Pan") == 0);
    CHECK(std::strcmp(fx.param(0).names[2], "Both") == 0);
    CHECK_EQ(int(fx.param(3).max), 2);
    CHECK(std::strcmp(fx.param(3).names[0], "Sine") == 0);
    CHECK(std::strcmp(fx.param(3).names[1], "Tri") == 0);
    CHECK(std::strcmp(fx.param(3).names[2], "Square") == 0);
    CHECK_EQ(int(fx.param(1).max), 100);
    CHECK_EQ(int(fx.param(2).max), 100);
    CHECK(fx.stateBytes() <= kEffectStateMax);
    CHECK_EQ(fx.workBytes(), 0u);
}

TEST(tremolo_fresh_off_leaves_the_bus_untouched) {
    Inst in;
    float l[kN], r[kN], l0[kN], r0[kN];
    for (uint32_t i = 0; i < kN; ++i) l[i] = l0[i] = std::sin(0.01f * float(i)), r[i] = r0[i] = -l[i];
    CHECK(!in.run(set(tremolo::kLevel), false, l, r));
    CHECK(std::memcmp(l, l0, sizeof l) == 0);
    CHECK(std::memcmp(r, r0, sizeof r) == 0);
}

TEST(tremolo_depth_0_leaves_the_level) {
    for (int m = 0; m < tremolo::kModes; ++m) {
        Inst in;
        std::vector<float> L, R;
        runDc(in, set(uint8_t(m), 80, 0), 200, L, R);
        float d = 0;
        for (size_t i = kN; i < L.size(); ++i) d = std::fmax(d, std::fmax(std::fabs(L[i] - 0.5f), std::fabs(R[i] - 0.5f)));
        CHECK(d < 1e-5f);
    }
}

TEST(tremolo_level_swings_at_the_rate) {
    Inst in;
    std::vector<float> L, R;
    const uint8_t rate = 60;
    const float hz = tremolo::rateHz(rate);
    const int blocks = int(4.0f / hz * kFs / float(kN)) + 4;   // four cycles
    runDc(in, set(tremolo::kLevel, rate, 100), blocks, L, R);
    float lo = 1, hi = 0;
    int ups = 0;
    for (size_t i = 4 * kN; i < L.size(); ++i) {
        lo = std::fmin(lo, L[i]);
        hi = std::fmax(hi, L[i]);
        if (L[i - 1] < 0.25f && L[i] >= 0.25f) ++ups;
        CHECK(L[i] == R[i]);
    }
    CHECK(lo < 0.01f);
    CHECK(hi > 0.49f && hi <= 0.5f + 1e-6f);
    CHECK(ups >= 3 && ups <= 5);
    CHECK(hz > 1.0f && hz < 5.0f);
    CHECK(std::fabs(tremolo::rateHz(0) - 0.1f) < 0.001f);
    CHECK(std::fabs(tremolo::rateHz(100) - 20.0f) < 0.05f);
}

TEST(tremolo_pan_keeps_the_power) {
    Inst in;
    std::vector<float> L, R;
    runDc(in, set(tremolo::kPan, 70, 100), 600, L, R);
    float pmin = 1e9f, pmax = 0, lmin = 1, lmax = 0;
    for (size_t i = 4 * kN; i < L.size(); ++i) {
        const float p = L[i] * L[i] + R[i] * R[i];
        pmin = std::fmin(pmin, p);
        pmax = std::fmax(pmax, p);
        lmin = std::fmin(lmin, L[i]);
        lmax = std::fmax(lmax, L[i]);
    }
    CHECK(10.0f * std::log10(pmax / pmin) < 0.5f);
    CHECK(lmin < 0.05f);   // hard left / right at Depth 100
    CHECK(lmax > 0.65f);
}

TEST(tremolo_square_has_no_step) {
    Inst in;
    std::vector<float> L, R;
    runDc(in, set(tremolo::kLevel, 100, 100, tremolo::kSquare), 400, L, R, 1.0f);
    float step = 0, lo = 1, hi = 0;
    for (size_t i = 4 * kN; i < L.size(); ++i) {
        step = std::fmax(step, std::fabs(L[i] - L[i - 1]));
        lo = std::fmin(lo, L[i]);
        hi = std::fmax(hi, L[i]);
    }
    CHECK(step < 0.05f);
    CHECK(lo < 0.01f);
    CHECK(hi > 0.99f);
}

TEST(tremolo_off_fades_out_then_passes_bit_for_bit) {
    Inst in;
    float l[kN], r[kN];
    for (int b = 0; b < 20; ++b) {
        for (uint32_t i = 0; i < kN; ++i) l[i] = r[i] = 0.3f;
        CHECK(in.run(set(tremolo::kBoth, 50, 80), true, l, r));
    }
    int passed = -1;
    for (int b = 0; b < 4; ++b) {
        float l0[kN];
        for (uint32_t i = 0; i < kN; ++i) l[i] = r[i] = l0[i] = 0.3f * std::sin(0.03f * float(i));
        const bool live = in.run(set(tremolo::kBoth, 50, 80), false, l, r);
        if (!live && passed < 0) {
            passed = b;
            CHECK(std::memcmp(l, l0, sizeof l) == 0);
        }
    }
    CHECK(passed >= 1 && passed <= 2);
}

// The largest gain step (DC input 1) across a switch: no click.
static void noClick(EffectSettings a, bool onA, EffectSettings b, bool onB) {
    Inst in;
    std::vector<float> L(60 * kN, 1.0f), R(L.size(), 1.0f);
    for (int k = 0; k < 60; ++k) in.run(k < 40 ? a : b, k < 40 ? onA : onB, L.data() + k * kN, R.data() + k * kN);
    float step = 0;
    for (size_t i = 39 * kN; i < 42 * kN; ++i)
        step = std::fmax(step, std::fmax(std::fabs(L[i] - L[i - 1]), std::fabs(R[i] - R[i - 1])));
    CHECK(step < 0.03f);
}

TEST(tremolo_switching_does_not_click) {
    noClick(set(tremolo::kLevel), false, set(tremolo::kLevel, 30, 100), true);
    noClick(set(tremolo::kBoth, 30, 100), true, set(tremolo::kBoth), false);
    noClick(set(tremolo::kLevel, 30, 100), true, set(tremolo::kPan, 30, 100), true);
    noClick(set(tremolo::kPan, 30, 100, tremolo::kSine), true, set(tremolo::kPan, 30, 100, tremolo::kSquare), true);
    noClick(set(tremolo::kLevel, 30, 0), true, set(tremolo::kLevel, 30, 100), true);
}

TEST(tremolo_random_settings_stay_bounded) {
    Inst in;
    uint32_t x = 3;
    float l[kN], r[kN];
    for (int b = 0; b < 3000; ++b) {
        if (b % 40 == 0) x = x * 1664525u + 1013904223u;
        const EffectSettings s = set(uint8_t((x >> 3) % 3), uint8_t((x >> 7) % 101), uint8_t((x >> 15) % 101),
                                     uint8_t((x >> 24) % 3));
        for (uint32_t i = 0; i < kN; ++i) l[i] = r[i] = (i & 1) ? 1.0f : -1.0f;
        in.run(s, (b / 150) % 4 != 3, l, r);
        for (uint32_t i = 0; i < kN; ++i) {
            CHECK(std::isfinite(l[i]) && std::isfinite(r[i]));
            CHECK(std::fabs(l[i]) <= 1.4143f && std::fabs(r[i]) <= 1.4143f);
        }
    }
}

TEST(tremolo_module_offers_the_effect) {
    mod::IModule& m = mod::tremoloModule();
    CHECK(m.info().caps & mod::kCapEffect);
    CHECK(m.effect(0) == &tremolo::gTremoloEffect);
    CHECK(m.effect(1) == nullptr);
    CHECK(m.masterInsert() == nullptr);
}
