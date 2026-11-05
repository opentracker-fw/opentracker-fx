// DIRT master-bus drive / bitcrush (opentracker-fx modules/dirt): Off passes
// the bus through bit for bit; the neutral stages (Bits 16, Rate 1, Tone 100)
// are exact; every type stays bounded; Bits and Rate give the expected value
// counts and holds; the Tone corner is -3 dB; Mix blends dry and wet; slammed
// settings do not click; random settings stay finite; the project data
// round-trips; the settings page; the module is a master insert.
#include "test.h"

#include <cmath>
#include <cstring>
#include <set>
#include <vector>

#include "audio/synth_engine.h"
#include "dirt.h"
#include "modules/module.h"
#include "modules/module_ui.h"

using namespace ot;
using namespace ot::audio;

namespace ot {
namespace mod {
void dirtSet(const audio::dirt::Params& p);
audio::dirt::Params dirtGet();
}  // namespace mod
}  // namespace ot

namespace {
constexpr uint32_t kN = kBlock;
constexpr float kFs = ot::audio::kSampleRate;
using Buf = std::vector<float>;

void tone(Buf& l, Buf& r, float amp, int blocks, float hz = 220.0f) {
    l.resize(size_t(blocks) * kN);
    r.resize(l.size());
    for (size_t i = 0; i < l.size(); ++i) {
        l[i] = amp * std::sin(6.2831853f * hz * float(i) / kFs);
        r[i] = amp * std::cos(6.2831853f * hz * float(i) / kFs);
    }
}
void run(dirt::Dirt& d, Buf& l, Buf& r) {
    for (size_t o = 0; o < l.size(); o += kN) d.process(l.data() + o, r.data() + o, kN);
}
dirt::Params on(uint8_t type = dirt::kSoft, uint8_t drive = 30) {
    dirt::Params p;
    p.on = 1;
    p.type = type;
    p.drive = drive;
    return p;
}
double rms(const Buf& x, size_t from) {
    double s = 0;
    for (size_t i = from; i < x.size(); ++i) s += double(x[i]) * x[i];
    return std::sqrt(s / double(x.size() - from));
}
}  // namespace

TEST(dirt_off_is_bit_exact) {
    dirt::Dirt d;
    Buf l, r;
    tone(l, r, 0.7f, 10);
    const Buf l0 = l, r0 = r;
    run(d, l, r);
    CHECK(l == l0 && r == r0);
    float a[kN] = {}, b[kN] = {};
    CHECK(!d.process(a, b, kN));
    // Off after On: one block fades the wet out, then the bus is untouched again.
    d.setParams(on(dirt::kHard, 90));
    tone(l, r, 0.7f, 6);
    run(d, l, r);
    dirt::Params p = on(dirt::kHard, 90);
    p.on = 0;
    d.setParams(p);
    tone(l, r, 0.7f, 3);
    const Buf dry = l;
    run(d, l, r);
    CHECK(std::memcmp(l.data() + kN, dry.data() + kN, 2 * kN * sizeof(float)) == 0);
    CHECK(std::memcmp(l.data(), dry.data(), kN * sizeof(float)) != 0);
}

// Bits 16, Rate 1, Tone 100, Mix 100: the shaper with its gains, nothing else.
TEST(dirt_neutral_stages_are_exact) {
    for (uint8_t t : {dirt::kSoft, dirt::kHard, dirt::kFold}) {
        dirt::Dirt d;
        d.setParams(on(t, 60));
        Buf l, r;
        tone(l, r, 0.8f, 4);
        const Buf x = l;
        d.process(l.data(), r.data(), kN);   // the gains ramp from 1 in the first block
        d.process(l.data() + kN, r.data() + kN, kN);
        const float dd = 0.6f, pre = 1.0f + 15.0f * dd * dd, post = 1.0f / (1.0f + 0.5f * dd);
        for (uint32_t i = kN; i < 2 * kN; ++i) CHECK_NEAR(l[i], dirt::shape(t, pre * x[i]) * post, 1e-6);
    }
}

TEST(dirt_types_stay_bounded) {
    for (uint8_t t = 0; t < dirt::kTypes; ++t) {
        dirt::Dirt d;
        d.setParams(on(t, 100));
        Buf l(kN * 40), r(kN * 40);
        uint32_t s = 7;
        for (size_t i = 0; i < l.size(); ++i) {
            s = s * 1664525u + 1013904223u;
            l[i] = 4.0f * float(int32_t(s)) / 2147483648.0f;
            r[i] = -l[i];
        }
        run(d, l, r);
        for (size_t i = kN; i < l.size(); ++i) {   // after the first block's fade-in from the dry bus
            CHECK(std::isfinite(l[i]) && std::isfinite(r[i]));
            CHECK(std::fabs(l[i]) <= 1.5f && std::fabs(r[i]) <= 1.5f);
        }
    }
    // The shapers: odd ones are odd, Hard / Fold within +-1 for any input.
    for (float x : {0.1f, 0.5f, 1.0f, 2.0f, 7.3f, 15.9f}) {
        CHECK_NEAR(dirt::shape(dirt::kSoft, -x), -dirt::shape(dirt::kSoft, x), 1e-7);
        CHECK(std::fabs(dirt::shape(dirt::kHard, x)) <= 1.0f && std::fabs(dirt::shape(dirt::kFold, x)) <= 1.0f);
        CHECK(std::fabs(dirt::shape(dirt::kAsym, x) + dirt::shape(dirt::kAsym, -x)) > 1e-3);
    }
    CHECK_NEAR(dirt::shape(dirt::kFold, 2.0f), 0.0f, 1e-6);   // folds back
    CHECK_NEAR(dirt::shape(dirt::kAsym, 0.0f), 0.0f, 1e-7);
}

TEST(dirt_bits_value_counts) {
    for (uint8_t bits : {4, 8}) {
        dirt::Dirt d;
        dirt::Params p = on(dirt::kHard, 0);   // Hard at Drive 0: the identity inside +-1
        p.bits = bits;
        d.setParams(p);
        Buf l(kN * 64), r(kN * 64);
        for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = -0.95f + 1.9f * float(i) / float(l.size());
        run(d, l, r);
        std::set<float> vals(l.begin() + kN, l.end());
        const size_t levels = size_t(1) << bits;
        CHECK(vals.size() <= levels);
        CHECK(vals.size() >= levels / 2);
        for (size_t i = kN; i < l.size(); i += 97) CHECK(std::fabs(l[i] - (-0.95f + 1.9f * float(i) / float(l.size()))) <= 1.0f / float(levels) + 1e-6f);
    }
}

TEST(dirt_rate_holds_both_channels) {
    dirt::Dirt d;
    dirt::Params p = on(dirt::kSoft, 0);
    p.rate = 8;
    d.setParams(p);
    Buf l, r;
    tone(l, r, 0.5f, 20, 330.0f);
    run(d, l, r);
    int changes = 0;
    long first = -1;
    for (size_t i = kN + 1; i < l.size(); ++i) {
        const bool cl = l[i] != l[i - 1], cr = r[i] != r[i - 1];
        if (cl || cr) {
            ++changes;
            if (first < 0) first = long(i);
            CHECK_EQ((long(i) - first) % 8, 0L);   // one counter: both channels change together, every 8 frames
            CHECK(cl && cr);
        }
    }
    const int frames = int(l.size() - kN);
    CHECK(changes <= frames / 8 + 1);
    CHECK(changes >= frames / 8 - 2);
    // Rate 1: every frame is new.
    dirt::Dirt e;
    e.setParams(on(dirt::kSoft, 0));
    tone(l, r, 0.5f, 4, 330.0f);
    run(e, l, r);
    int same = 0;
    for (size_t i = kN + 1; i < l.size(); ++i) same += l[i] == l[i - 1];
    CHECK(same < 4);
}

TEST(dirt_tone_corner_is_minus_3_db) {
    const float fc = dirt::toneHz(50);
    CHECK_NEAR(fc, 2000.0f, 1.0f);
    CHECK_NEAR(dirt::toneHz(0), 200.0f, 0.5f);
    auto level = [](uint8_t toneV, float hz) {
        dirt::Dirt d;
        dirt::Params p = on(dirt::kSoft, 0);
        p.tone = toneV;
        d.setParams(p);
        Buf l, r;
        tone(l, r, 0.01f, 80, hz);
        run(d, l, r);
        return rms(l, 20 * kN);
    };
    const double db = 20.0 * std::log10(level(50, fc) / level(100, fc));
    CHECK_NEAR(db, -3.0, 0.3);
    CHECK_NEAR(20.0 * std::log10(level(50, fc / 10.0f) / level(100, fc / 10.0f)), 0.0, 0.2);
    CHECK(20.0 * std::log10(level(50, fc * 5.0f) / level(100, fc * 5.0f)) < -12.0);
}

TEST(dirt_mix_blends_dry_and_wet) {
    // Mix 0 while On: the dry bus, exactly.
    {
        dirt::Dirt d;
        dirt::Params p = on(dirt::kFold, 100);
        p.mix = 0;
        d.setParams(p);
        Buf l, r;
        tone(l, r, 0.6f, 5);
        const Buf x = l;
        run(d, l, r);
        CHECK(l == x);
    }
    // Mix 50: half the dry and half the wet (Mix 100).
    dirt::Dirt w, h;
    dirt::Params p = on(dirt::kHard, 80);
    w.setParams(p);
    p.mix = 50;
    h.setParams(p);
    Buf l1, r1, l2, r2;
    tone(l1, r1, 0.6f, 6);
    tone(l2, r2, 0.6f, 6);
    const Buf x = l1;
    run(w, l1, r1);
    run(h, l2, r2);
    for (size_t i = 2 * kN; i < x.size(); ++i) CHECK_NEAR(l2[i], 0.5f * (x[i] + l1[i]), 1e-5);
}

TEST(dirt_slammed_settings_do_not_click) {
    dirt::Dirt d;
    Buf l, r;
    tone(l, r, 0.5f, 200, 100.0f);
    float worst = 0.0f;
    for (int b = 0; b < 200; ++b) {
        dirt::Params p = on(dirt::kSoft, (b / 10) % 2 ? 100 : 0);
        p.mix = (b / 20) % 2 ? 100 : 20;
        p.on = (b / 40) % 2 ? 0 : 1;
        d.setParams(p);
        d.process(l.data() + size_t(b) * kN, r.data() + size_t(b) * kN, kN);
    }
    for (size_t i = 1; i < l.size(); ++i) worst = std::fmax(worst, std::fabs(l[i] - l[i - 1]));
    CHECK(worst < 0.12f);
}

// Leaving Asym takes its DC block with it: Soft on a symmetric sine has no offset.
TEST(dirt_leaving_asym_leaves_no_dc) {
    for (uint8_t rate : {1, 4}) {
        dirt::Dirt d;
        dirt::Params p = on(dirt::kAsym, 80);
        p.rate = rate;
        d.setParams(p);
        Buf l, r;
        const float hz = kFs / 100.0f;   // a period of exactly 100 frames
        tone(l, r, 0.8f, 200, hz);
        run(d, l, r);
        p.type = dirt::kSoft;
        d.setParams(p);
        tone(l, r, 0.8f, 100, hz);
        run(d, l, r);
        double m = 0.0;
        const size_t from = 20 * kN, len = 10200;   // whole periods
        for (size_t i = from; i < from + len; ++i) m += l[i];
        CHECK(std::fabs(m / double(len)) < 1e-4);
    }
}

// Entering Asym brings no DC step: its DC block starts from the offset of
// the shaper on the incoming block, not from zero (a user heard a loud click
// on a Type change at a high Drive).
TEST(dirt_entering_asym_leaves_no_dc_step) {
    for (uint8_t from : {uint8_t(dirt::kSoft), uint8_t(dirt::kHard), uint8_t(dirt::kFold)})
        for (uint8_t drive : {40, 100})
            for (float amp : {0.15f, 0.5f}) {
                dirt::Dirt d;
                dirt::Params p = on(from, drive);
                d.setParams(p);
                Buf l, r;
                const float hz = kFs / 100.0f;   // a period of exactly 100 frames
                tone(l, r, amp, 200, hz);
                for (int b = 0; b < 200; ++b) {
                    if (b == 100) {
                        p.type = dirt::kAsym;
                        d.setParams(p);
                    }
                    d.process(l.data() + size_t(b) * kN, r.data() + size_t(b) * kN, kN);
                }
                double worst = 0.0;
                for (size_t f = 101 * kN; f + 100 <= 160 * kN; f += 20) {   // whole periods after the switch
                    double m = 0.0;
                    for (size_t i = f; i < f + 100; ++i) m += l[i];
                    worst = std::fmax(worst, std::fabs(m / 100.0));
                }
                if (!(worst < 0.01))
                    std::printf("       from %d drive %d amp %.2f: DC %.4f\n", from, drive, double(amp), worst);
                CHECK(worst < 0.01);
            }
}

// Type and Tone (100 <-> 99: the low-pass in and out) change without a click.
TEST(dirt_type_and_tone_changes_do_not_click) {
    dirt::Dirt d;
    Buf l, r;
    tone(l, r, 0.5f, 120, 100.0f);
    float worst = 0.0f, steady = 0.0f;
    for (int b = 0; b < 120; ++b) {
        dirt::Params p = on(uint8_t((b / 10) % dirt::kTypes), 100);
        p.tone = (b / 5) % 2 ? 100 : 99;
        d.setParams(p);
        d.process(l.data() + size_t(b) * kN, r.data() + size_t(b) * kN, kN);
    }
    size_t at = 0;
    for (size_t i = 1; i < l.size(); ++i)
        if (std::fabs(l[i] - l[i - 1]) > worst) {
            worst = std::fabs(l[i] - l[i - 1]);
            at = i;
        }
    // The steepest steady slope of the types at the same Drive.
    for (uint8_t t = 0; t < dirt::kTypes; ++t) {
        dirt::Dirt e;
        e.setParams(on(t, 100));
        Buf a, b2;
        tone(a, b2, 0.5f, 20, 100.0f);
        run(e, a, b2);
        for (size_t i = kN + 1; i < a.size(); ++i) steady = std::fmax(steady, std::fabs(a[i] - a[i - 1]));
    }
    if (!(worst < 1.5f * steady))
        std::printf("       worst %.4f at frame %zu (block %zu +%zu), steady %.4f\n", double(worst), at, at / kN, at % kN,
                    double(steady));
    CHECK(worst < 1.5f * steady);
}

TEST(dirt_random_settings_stay_finite) {
    dirt::Dirt d;
    uint32_t s = 99;
    auto rnd = [&s](int n) {
        s = s * 1664525u + 1013904223u;
        return int((s >> 8) % uint32_t(n));
    };
    float l[kN], r[kN];
    for (int b = 0; b < 600; ++b) {
        if (b % 5 == 0) {
            dirt::Params p;
            p.on = uint8_t(rnd(4) != 0);
            p.type = uint8_t(rnd(4));
            p.drive = uint8_t(rnd(101));
            p.bits = uint8_t(4 + rnd(13));
            p.rate = uint8_t(1 + rnd(32));
            p.tone = uint8_t(rnd(101));
            p.mix = uint8_t(rnd(101));
            d.setParams(p);
        }
        for (uint32_t i = 0; i < kN; ++i) {
            l[i] = 1.5f * float(rnd(20001) - 10000) / 10000.0f;
            r[i] = 0.3f * l[i];
        }
        d.process(l, r, kN);
        for (uint32_t i = 0; i < kN; ++i) {
            CHECK(std::isfinite(l[i]) && std::isfinite(r[i]));
            CHECK(std::fabs(l[i]) <= 2.0f && std::fabs(r[i]) <= 2.0f);
        }
    }
}

TEST(dirt_module_project_data_round_trip) {
    mod::ModuleHost& h = mod::ModuleHost::instance();
    h.init();
    const mod::IModule* m = h.find("DIRT");
    CHECK(m != nullptr);
    if (!m) return;
    CHECK(const_cast<mod::IModule*>(m)->masterInsert() != nullptr);
    mod::IProjectSection* sec = const_cast<mod::IModule*>(m)->section();
    CHECK(sec != nullptr && sec->tag() == 0x4004);
    sec->reset();
    uint8_t b[16];
    CHECK_EQ(sec->encode(b, sizeof b), 0u);   // defaults, Off: nothing saved
    dirt::Params p;
    p.on = 1;
    p.type = dirt::kFold;
    p.drive = 77;
    p.bits = 6;
    p.rate = 12;
    p.tone = 40;
    p.mix = 65;
    mod::dirtSet(p);
    CHECK(sec->dirty());
    CHECK_EQ(sec->encode(b, sizeof b), 8u);
    CHECK(!sec->dirty());
    sec->reset();
    CHECK_EQ(int(mod::dirtGet().on), 0);
    CHECK(sec->decode(b, 8, 1));
    const dirt::Params q = mod::dirtGet();
    CHECK(q.on == 1 && q.type == dirt::kFold && q.drive == 77 && q.bits == 6 && q.rate == 12 && q.tone == 40 &&
          q.mix == 65);
    // A short payload keeps the defaults for the rest; values clamp into their ranges.
    const uint8_t s[5] = {1, 9, 250, 2, 0};
    CHECK(sec->decode(s, 5, 1));
    const dirt::Params c = mod::dirtGet();
    CHECK(c.type == dirt::kAsym && c.drive == 100 && c.bits == 4 && c.rate == 1 && c.tone == 100 && c.mix == 100);
    const uint8_t s2[4] = {1, 0, 0, 40};
    CHECK(sec->decode(s2, 4, 1));
    CHECK_EQ(int(mod::dirtGet().bits), 16);
    sec->reset();
}

TEST(dirt_module_page) {
    mod::ModuleHost& h = mod::ModuleHost::instance();
    h.init();
    mod::IModule* m = const_cast<mod::IModule*>(h.find("DIRT"));
    CHECK(m != nullptr);
    if (!m) return;
    mod::IModulePage* pg = m->page();
    CHECK(pg != nullptr);
    if (!pg) return;
    m->section()->reset();
    CHECK(std::strcmp(pg->title(), "Dirt") == 0);
    CHECK_EQ(pg->rows(), 7);
    mod::ModuleRow row;
    const char* const labels[7] = {"Dirt", "Type", "Drive", "Bits", "Rate", "Tone", "Mix"};
    for (int i = 0; i < 7; ++i) {
        pg->row(i, row);
        CHECK(std::strcmp(row.label, labels[i]) == 0);
        CHECK(row.frac >= 0.0f && row.frac <= 1.0f);
    }
    pg->row(0, row);
    CHECK(std::strcmp(row.value, "Off") == 0);
    pg->adjust(0, 1);
    CHECK(m->section()->dirty());
    pg->row(0, row);
    CHECK(std::strcmp(row.value, "On") == 0);
    pg->row(1, row);
    CHECK(std::strcmp(row.value, "Soft") == 0);
    pg->adjust(1, 2);
    pg->row(1, row);
    CHECK(std::strcmp(row.value, "Fold") == 0);
    pg->adjust(1, 9);
    CHECK_EQ(int(mod::dirtGet().type), int(dirt::kAsym));
    pg->adjust(3, -40);
    CHECK_EQ(int(mod::dirtGet().bits), 4);
    pg->row(3, row);
    CHECK(std::strcmp(row.value, "4") == 0);
    CHECK_NEAR(row.frac, 0.0f, 1e-6);
    pg->adjust(4, 100);
    CHECK_EQ(int(mod::dirtGet().rate), 32);
    pg->adjust(2, 5);
    CHECK_EQ(int(mod::dirtGet().drive), 35);
    m->section()->reset();
}

// Golden hash (host build, -ffp-contract=off): every type, Bits, Rate, Tone and
// Mix moving over a tone pair. Update only for an intended change of the sound.
TEST(dirt_golden_hash) {
    dirt::Dirt d;
    Buf l, r;
    tone(l, r, 0.6f, 160, 330.0f);
    for (int b = 0; b < 160; ++b) {
        dirt::Params p = on(uint8_t((b / 20) % dirt::kTypes), uint8_t(20 + (b * 7) % 80));
        p.bits = uint8_t(4 + (b / 16) % 13);
        p.rate = uint8_t(1 + (b / 40) * 5);
        p.tone = uint8_t((b / 10) % 2 ? 100 : 60);
        p.mix = uint8_t(b < 150 ? 100 : 50);
        d.setParams(p);
        d.process(l.data() + size_t(b) * kN, r.data() + size_t(b) * kN, kN);
    }
    uint64_t h = 1469598103934665603ull;
    for (const Buf* x : {&l, &r})
        for (float s : *x) {
            uint32_t w;
            std::memcpy(&w, &s, 4);
            for (int k = 0; k < 4; ++k) {
                h ^= (w >> (8 * k)) & 0xFFu;
                h *= 1099511628211ull;
            }
        }
    std::printf("  DIRT golden: %016llx\n", (unsigned long long)h);
    CHECK_EQ(h, uint64_t(0x56c3a12883aee75full));   // entering Asym at blocks 60 and 140: no DC step
}

// Drive as a per-instance effect (IEffect, id DRIV): Type, Drive, Tone, Mix, with
// Bits 16 and Rate 1. Called the way a synth slot calls it (caller-owned zeroed
// state, settings per block), it equals the master insert with those settings,
// bit for bit; Off fades over one block and then leaves the bus untouched.
TEST(dirt_drive_effect_equals_the_master_insert) {
    IEffect& fx = dirt::gDriveEffect;
    CHECK_STR(fx.id(), "DRIV");
    CHECK_STR(fx.name(), "Drive");
    CHECK_EQ(fx.paramCount(), 4);
    CHECK(fx.stateBytes() <= kEffectStateMax);
    CHECK_EQ(fx.workBytes(), 0u);
    char t[16];
    fx.valueText(0, 3, t, sizeof(t));
    CHECK_STR(t, "Asym");
    const EffectSettings d = effectDefaults(fx);
    CHECK(d.v[0] == dirt::kSoft && d.v[1] == 30 && d.v[2] == 100 && d.v[3] == 100);
    dirt::Dirt m;
    alignas(8) uint8_t state[kEffectStateMax] = {};
    Buf l1, r1, l2, r2;
    tone(l1, r1, 0.6f, 120, 330.0f);
    l2 = l1;
    r2 = r1;
    bool same = true;
    for (int b = 0; b < 120; ++b) {
        const bool onb = b < 100;
        const uint8_t type = uint8_t((b / 20) % dirt::kTypes), drive = uint8_t(20 + (b * 7) % 80);
        const uint8_t tn = uint8_t((b / 10) % 2 ? 100 : 60), mix = uint8_t(b < 90 ? 100 : 50);
        dirt::Params p = on(type, drive);
        p.on = onb;
        p.tone = tn;
        p.mix = mix;
        m.setParams(p);
        const size_t o = size_t(b) * kN;
        m.process(l1.data() + o, r1.data() + o, kN);
        const bool live = fx.process(state, nullptr, EffectSettings{{type, drive, tn, mix, 0, 0}}, onb, l2.data() + o,
                                     r2.data() + o, kN);
        same = same && live == onb;   // the fade block ends at wet 0: nothing left to play
    }
    CHECK(same);
    CHECK(std::memcmp(l1.data(), l2.data(), l1.size() * sizeof(float)) == 0);
    CHECK(std::memcmp(r1.data(), r2.data(), r1.size() * sizeof(float)) == 0);
    CHECK(mod::ModuleHost::instance().effectById("DRIV") == &fx);
}
