// CRSH bitcrusher effect (opentracker-fx modules/bitcrush): the settings table;
// a fresh Off instance leaves the bus untouched; Crush 0 / Down 0 / Hard is
// exact inside +-1; Crush gives 2^(16 - Crush) levels at most; Down holds
// every value Down + 1 frames; Soft stays inside +-1; switching on fades the
// wet in; Off fades out and then passes the bus bit for bit; random settings
// stay bounded; the module offers the effect.
#include "test.h"

#include <cmath>
#include <cstring>
#include <set>
#include <vector>

#include "audio/synth_engine.h"
#include "bitcrush.h"
#include "modules/module.h"

using namespace ot;
using namespace ot::audio;

namespace ot {
namespace mod {
IModule& bitcrushModule();
}  // namespace mod
}  // namespace ot

namespace {
constexpr uint32_t kN = kBlock;
IEffect& fx = bitcrush::gBitcrushEffect;

struct Inst {
    alignas(8) uint8_t state[kEffectStateMax] = {};
    bool run(const EffectSettings& s, bool on, float* l, float* r) { return fx.process(state, nullptr, s, on, l, r, kN); }
};

// Shape, Crush, Down, Mix.
EffectSettings set(uint8_t shape, uint8_t crush = 8, uint8_t down = 3, uint8_t mix = 100) {
    return EffectSettings{{shape, crush, down, mix, 0, 0}};
}

// A slow ramp -amp .. amp through `blocks` blocks (the wet fully in after the first).
void ramp(Inst& in, const EffectSettings& s, int blocks, std::vector<float>& L, std::vector<float>& R,
          float amp = 1.2f) {
    L.resize(size_t(blocks) * kN);
    for (size_t i = 0; i < L.size(); ++i) L[i] = -amp + 2.0f * amp * float(i) / float(L.size());
    R = L;
    for (int b = 0; b < blocks; ++b) in.run(s, true, L.data() + b * kN, R.data() + b * kN);
}
}  // namespace

TEST(bitcrush_settings_table) {
    CHECK(std::strcmp(fx.id(), "CRSH") == 0);
    CHECK(std::strcmp(fx.name(), "Bitcrush") == 0);
    CHECK_EQ(fx.paramCount(), 4);
    const char* labels[4] = {"Shape", "Crush", "Down", "Mix"};
    for (int i = 0; i < 4; ++i) CHECK(std::strcmp(fx.param(i).label, labels[i]) == 0);
    CHECK_EQ(int(fx.param(0).max), 1);
    CHECK(std::strcmp(fx.param(0).names[0], "Hard") == 0);
    CHECK(std::strcmp(fx.param(0).names[1], "Soft") == 0);
    CHECK_EQ(int(fx.param(1).max), 12);
    CHECK_EQ(int(fx.param(2).max), 31);
    CHECK_EQ(int(fx.param(3).max), 100);
    CHECK_EQ(int(fx.param(3).def), 100);
    CHECK(fx.stateBytes() <= kEffectStateMax);
    CHECK_EQ(fx.workBytes(), 0u);
}

TEST(bitcrush_fresh_off_leaves_the_bus_untouched) {
    Inst in;
    float l[kN], r[kN], l0[kN], r0[kN];
    for (uint32_t i = 0; i < kN; ++i) l[i] = l0[i] = std::sin(0.01f * float(i)), r[i] = r0[i] = -l[i];
    CHECK(!in.run(set(bitcrush::kHard), false, l, r));
    CHECK(std::memcmp(l, l0, sizeof l) == 0);
    CHECK(std::memcmp(r, r0, sizeof r) == 0);
}

TEST(bitcrush_neutral_settings_are_exact) {
    Inst in;
    std::vector<float> L, R;
    ramp(in, set(bitcrush::kHard, 0, 0), 40, L, R);
    for (size_t i = kN; i < L.size(); ++i) {
        const float x = -1.2f + 2.4f * float(i) / float(L.size());
        const float want = x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
        CHECK(L[i] == want);
    }
}

TEST(bitcrush_crush_limits_the_levels) {
    for (int crush : {6, 10, 12}) {
        Inst in;
        std::vector<float> L, R;
        ramp(in, set(bitcrush::kHard, uint8_t(crush), 0), 200, L, R);
        std::set<float> v(L.begin() + kN, L.end());
        const size_t levels = size_t(1) << (16 - crush);
        CHECK(v.size() <= levels + 1);
        CHECK(v.size() >= levels / 2);
    }
}

TEST(bitcrush_down_holds_the_value) {
    for (int down : {1, 7, 31}) {
        Inst in;
        std::vector<float> L, R;
        ramp(in, set(bitcrush::kHard, 0, uint8_t(down)), 100, L, R, 0.9f);
        int run = 1, shortest = 1 << 30, longest = 0;
        for (size_t i = 2 * kN + 1; i < L.size() - kN; ++i) {
            if (L[i] == L[i - 1]) {
                ++run;
            } else {
                if (i > 2 * kN + 64) shortest = std::min(shortest, run), longest = std::max(longest, run);
                run = 1;
            }
        }
        CHECK_EQ(shortest, down + 1);
        CHECK_EQ(longest, down + 1);
    }
}

TEST(bitcrush_soft_stays_inside) {
    Inst in;
    std::vector<float> L, R;
    ramp(in, set(bitcrush::kSoft, 4, 2), 60, L, R);
    for (size_t i = kN; i < L.size(); ++i) CHECK(std::fabs(L[i]) <= 1.0f);   // after the wet's fade-in
}

TEST(bitcrush_switching_on_fades_the_wet_in) {
    Inst in;
    float l[kN], r[kN];
    for (uint32_t i = 0; i < kN; ++i) l[i] = r[i] = 0.3f;
    CHECK(!in.run(set(bitcrush::kHard, 12, 0), false, l, r));
    for (uint32_t i = 0; i < kN; ++i) l[i] = r[i] = 0.3f;
    in.run(set(bitcrush::kHard, 12, 0), true, l, r);
    // 4 bits: 0.3 -> 0.25; the first frames stay near the dry value, the last reach the wet.
    CHECK(std::fabs(l[0] - 0.3f) < 0.002f);
    CHECK(std::fabs(l[kN - 1] - 0.25f) < 0.002f);
    for (uint32_t i = 1; i < kN; ++i) CHECK(std::fabs(l[i] - l[i - 1]) < 0.002f);
}

TEST(bitcrush_off_fades_out_then_passes_bit_for_bit) {
    Inst in;
    float l[kN], r[kN];
    for (int b = 0; b < 20; ++b) {
        for (uint32_t i = 0; i < kN; ++i) l[i] = r[i] = 0.3f * std::sin(0.03f * float(i));
        CHECK(in.run(set(bitcrush::kSoft, 10, 5), true, l, r));
    }
    int passed = -1;
    for (int b = 0; b < 4; ++b) {
        float l0[kN];
        for (uint32_t i = 0; i < kN; ++i) l[i] = r[i] = l0[i] = 0.3f * std::sin(0.03f * float(i));
        const bool live = in.run(set(bitcrush::kSoft, 10, 5), false, l, r);
        if (!live && passed < 0) {
            passed = b;
            CHECK(std::memcmp(l, l0, sizeof l) == 0);
        }
    }
    CHECK(passed >= 1 && passed <= 2);
}

TEST(bitcrush_random_settings_stay_bounded) {
    Inst in;
    uint32_t x = 5;
    float l[kN], r[kN];
    for (int b = 0; b < 3000; ++b) {
        if (b % 30 == 0) x = x * 1664525u + 1013904223u;
        const EffectSettings s = set(uint8_t((x >> 3) & 1), uint8_t((x >> 7) % 13), uint8_t((x >> 15) % 32),
                                     uint8_t((x >> 22) % 101));
        for (uint32_t i = 0; i < kN; ++i) {
            x = x * 1664525u + 1013904223u;
            l[i] = 1.5f * float(int32_t(x)) / 2147483648.0f;
            r[i] = -l[i];
        }
        in.run(s, (b / 100) % 4 != 3, l, r);
        for (uint32_t i = 0; i < kN; ++i) {
            CHECK(std::isfinite(l[i]) && std::isfinite(r[i]));
            CHECK(std::fabs(l[i]) <= 1.5f && std::fabs(r[i]) <= 1.5f);
        }
    }
}

TEST(bitcrush_module_offers_the_effect) {
    mod::IModule& m = mod::bitcrushModule();
    CHECK(m.info().caps & mod::kCapEffect);
    CHECK(m.effect(0) == &bitcrush::gBitcrushEffect);
    CHECK(m.effect(1) == nullptr);
    CHECK(m.masterInsert() == nullptr);
}
