// LOFI chip DAC / speaker master insert (opentracker-fx modules/lofi): Off
// passes the bus through bit for bit (and again one block after Off); the
// neutral settings (Bits 16, Rate Off, Ladder 0, Speaker Off, Mono Off) are
// exact; Bits gives its level count; the hold's mean period per Rate; the
// Ladder bounded, exact at 0, non-monotonic at 100; the speakers' corners and
// bumps; Mono; Mix; no click on any row's change; random settings finite; the
// project data round-trips and resets; the settings page; the module is a
// master insert; a golden hash.
#include "test.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <vector>

#include "audio/chip_dsp.h"
#include "audio/synth_engine.h"
#include "lofi.h"
#include "modules/module.h"
#include "modules/module_ui.h"

using namespace ot;
using namespace ot::audio;

namespace ot {
namespace mod {
void lofiSet(const audio::lofi::Params& p);
audio::lofi::Params lofiGet();
}  // namespace mod
}  // namespace ot

namespace {
constexpr uint32_t kN = kBlock;
constexpr float kFs = ot::audio::kSampleRate;
using Buf = std::vector<float>;

void sine(Buf& l, Buf& r, float amp, int blocks, float hz = 220.0f) {
    l.resize(size_t(blocks) * kN);
    r.resize(l.size());
    for (size_t i = 0; i < l.size(); ++i) {
        l[i] = amp * std::sin(6.2831853f * hz * float(i) / kFs);
        r[i] = amp * std::cos(6.2831853f * hz * float(i) / kFs);
    }
}
void noise(Buf& l, Buf& r, float amp, int blocks, uint32_t seed = 7) {
    l.resize(size_t(blocks) * kN);
    r.resize(l.size());
    for (size_t i = 0; i < l.size(); ++i) {
        seed = seed * 1664525u + 1013904223u;
        l[i] = amp * float(int32_t(seed)) * (1.0f / 2147483648.0f);
        seed = seed * 1664525u + 1013904223u;
        r[i] = amp * float(int32_t(seed)) * (1.0f / 2147483648.0f);
    }
}
void run(lofi::Lofi& d, Buf& l, Buf& r) {
    for (size_t o = 0; o < l.size(); o += kN) d.process(l.data() + o, r.data() + o, kN);
}
lofi::Params neutral() {
    lofi::Params p;
    p.on = 1;
    p.bits = 16;
    p.rate = lofi::kRateOff;
    p.ladder = 0;
    p.speaker = lofi::kSpeakerOff;
    p.mono = 0;
    p.mix = 100;
    return p;
}
double rms(const Buf& x, size_t from, size_t to = SIZE_MAX) {
    to = std::min(to, x.size());
    double s = 0;
    for (size_t i = from; i < to; ++i) s += double(x[i]) * x[i];
    return std::sqrt(s / double(to - from));
}
double db(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }
// The gain (dB) of a sine at hz through p, after it settles.
double gainAt(const lofi::Params& p, float hz) {
    lofi::Lofi d;
    d.setParams(p);
    Buf l, r;
    sine(l, r, 0.25f, 120, hz);
    const Buf in = l;
    run(d, l, r);
    return db(rms(l, 60 * kN) / rms(in, 60 * kN));
}
}  // namespace

TEST(lofi_off_is_bit_exact) {
    lofi::Lofi d;
    Buf l, r;
    sine(l, r, 0.7f, 10);
    const Buf l0 = l, r0 = r;
    run(d, l, r);
    CHECK(l == l0 && r == r0);
    // Off after On: one block fades the wet out, then the bus is untouched again.
    lofi::Params p;
    p.on = 1;
    p.bits = 4;
    d.setParams(p);
    sine(l, r, 0.7f, 6);
    run(d, l, r);
    p.on = 0;
    d.setParams(p);
    sine(l, r, 0.7f, 3);
    const Buf dry = l;
    run(d, l, r);
    CHECK(std::memcmp(l.data() + kN, dry.data() + kN, 2 * kN * sizeof(float)) == 0);
    CHECK(std::memcmp(l.data(), dry.data(), kN * sizeof(float)) != 0);
}

TEST(lofi_neutral_settings_are_exact) {
    lofi::Lofi d;
    d.setParams(neutral());
    Buf l, r;
    noise(l, r, 0.6f, 12);
    const Buf l0 = l, r0 = r;
    run(d, l, r);
    // (the first block ramps the wet in from 0: exact from the second)
    CHECK(std::memcmp(l.data() + kN, l0.data() + kN, 11 * kN * sizeof(float)) == 0);
    CHECK(std::memcmp(r.data() + kN, r0.data() + kN, 11 * kN * sizeof(float)) == 0);
}

TEST(lofi_bits_levels) {
    for (int bits : {4, 6, 8, 12}) {
        lofi::Params p = neutral();
        p.bits = uint8_t(bits);
        lofi::Lofi d;
        d.setParams(p);
        Buf l, r;
        noise(l, r, 1.0f, 300);
        run(d, l, r);
        std::set<float> lv(l.begin() + kN, l.end());
        std::printf("  bits %d: %zu levels\n", bits, lv.size());
        CHECK(lv.size() <= size_t(1u << bits));
        CHECK(lv.size() >= size_t(1u << bits) - 2);
    }
}

TEST(lofi_hold_period_per_rate) {
    for (int rate = lofi::k32k; rate < lofi::kRates; ++rate) {
        lofi::Params p = neutral();
        p.rate = uint8_t(rate);
        lofi::Lofi d;
        d.setParams(p);
        Buf l, r;
        noise(l, r, 0.8f, 400);
        run(d, l, r);
        long changes = 0;
        for (size_t i = kN + 1; i < l.size(); ++i) changes += l[i] != l[i - 1];
        const double mean = double(l.size() - kN - 1) / double(changes);
        const double want = kFs / lofi::rateHz(uint8_t(rate));
        std::printf("  rate %s: %.3f frames a hold (want %.3f)\n", lofi::kRateName[rate], mean, want);
        CHECK(std::fabs(mean / want - 1.0) < 0.01);
        // One counter for both channels: they change on the same frames.
        long both = 0;
        for (size_t i = kN + 1; i < l.size(); ++i) both += (l[i] != l[i - 1]) == (r[i] != r[i - 1]);
        CHECK(both > long(l.size() - kN) - 10);
    }
}

TEST(lofi_ladder) {
    // Ladder 0 = the quantiser; at 100 bounded (2 LSB) and non-monotonic.
    for (int bits : {4, 8, 12}) {
        lofi::Params p = neutral();
        p.bits = uint8_t(bits);
        lofi::Lofi a, b;
        a.setParams(p);
        p.ladder = 100;
        b.setParams(p);
        Buf l(512 * kN), r(512 * kN);
        for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = -1.0f + 2.0f * float(i) / float(l.size());
        Buf l2 = l, r2 = r;
        run(a, l, r);
        run(b, l2, r2);
        const float lsb = 1.0f / float(1 << (bits - 1));
        float worst = 0.0f;
        bool down = false;
        for (size_t i = kN + 1; i < l.size(); ++i) {
            worst = std::max(worst, std::fabs(l2[i] - l[i]) / lsb);
            if (l2[i] < l2[i - 1] - 1e-7f) down = true;
        }
        std::printf("  ladder %d bits: |error| <= %.2f LSB, non-monotonic %d\n", bits, worst, int(down));
        CHECK(worst <= 2.0f);
        CHECK(down);
    }
}

TEST(lofi_speakers) {
    // Monitor (no bump): -3 dB at 40 Hz and 12 kHz (+-10 %).
    lofi::Params p = neutral();
    p.speaker = lofi::kMonitor;
    const double mid = gainAt(p, 1000.0f);
    CHECK(std::fabs(mid) < 0.5);
    CHECK(std::fabs(gainAt(p, 40.0f) - mid + 3.0) < 1.0);
    CHECK(std::fabs(gainAt(p, 12000.0f) - mid + 3.0) < 1.0);
    CHECK(gainAt(p, 44.0f) - mid > -3.0 && gainAt(p, 36.0f) - mid < -3.0);
    CHECK(gainAt(p, 10800.0f) - mid > -3.0 && gainAt(p, 13200.0f) - mid < -3.0);
    // TV and Handheld: their bumps at 3 kHz / 1.5 kHz, their corners far lower / higher.
    for (int s : {lofi::kTv, lofi::kHandheld}) {
        p.speaker = uint8_t(s);
        const lofi::Shape sh = lofi::speakerShape(uint8_t(s));
        const double bump = gainAt(p, sh.bumpHz);
        const double lo = gainAt(p, sh.hp), hi = gainAt(p, sh.lp);
        std::printf("  speaker %s: bump %+.2f dB, %g Hz %+.2f dB, %g Hz %+.2f dB\n", lofi::kSpeakerName[s], bump, sh.hp, lo, sh.lp, hi);
        CHECK(std::fabs(bump - sh.bumpDb) < 1.0);
        CHECK(lo < -2.0 && lo > -5.0);
        CHECK(hi < bump - 2.0);
        CHECK(gainAt(p, sh.hp * 0.25f) < lo - 8.0);
    }
}

TEST(lofi_mono_and_mix) {
    lofi::Params p = neutral();
    p.mono = 1;
    lofi::Lofi d;
    d.setParams(p);
    Buf l, r;
    sine(l, r, 0.5f, 10);
    const Buf l0 = l, r0 = r;
    run(d, l, r);
    for (size_t i = kN; i < l.size(); ++i) {
        CHECK_EQ(l[i], r[i]);
        if (std::fabs(l[i] - 0.5f * (l0[i] + r0[i])) > 1e-6f) {
            CHECK(false);
            break;
        }
    }
    // Mix 50: half dry, half wet (Bits 4).
    p = neutral();
    p.bits = 4;
    p.mix = 50;
    lofi::Lofi m, w;
    m.setParams(p);
    p.mix = 100;
    w.setParams(p);
    Buf a, b, c, e;
    sine(a, b, 0.5f, 10);
    sine(c, e, 0.5f, 10);
    const Buf dry = a;
    run(m, a, b);
    run(w, c, e);
    double err = 0;
    for (size_t i = 2 * kN; i < a.size(); ++i) err = std::max(err, double(std::fabs(a[i] - 0.5f * (dry[i] + c[i]))));
    CHECK(err < 1e-5);
}

TEST(lofi_row_changes_do_not_click) {
    // Each row stepped mid-signal: the largest sample step stays near the steady one.
    const int rows = 6;
    for (int row = 1; row <= rows; ++row) {
        lofi::Params a;
        a.on = 1;
        a.bits = 10;
        a.speaker = lofi::kSpeakerOff;
        lofi::Params b = a;
        switch (row) {
            case 1: b.bits = 5; break;
            case 2: a.rate = lofi::kRateOff, b.rate = lofi::k4k; break;
            case 3: b.ladder = 100; break;
            case 4: b.speaker = lofi::kHandheld; break;
            case 5: b.mono = 1; break;
            default: b.mix = 0; break;
        }
        lofi::Lofi d;
        d.setParams(a);
        Buf l, r;
        sine(l, r, 0.5f, 40, 110.0f);
        size_t o = 0;
        for (; o < 20 * kN; o += kN) d.process(l.data() + o, r.data() + o, kN);
        double steady = 0;
        for (size_t i = 10 * kN + 1; i < 20 * kN; ++i) steady = std::max(steady, double(std::fabs(l[i] - l[i - 1])));
        d.setParams(b);
        for (; o < l.size(); o += kN) d.process(l.data() + o, r.data() + o, kN);
        double jump = 0;
        for (size_t i = 20 * kN; i < 22 * kN; ++i) jump = std::max(jump, double(std::fabs(l[i] - l[i - 1])));
        double after = 0;
        for (size_t i = 30 * kN; i < 40 * kN; ++i) after = std::max(after, double(std::fabs(l[i] - l[i - 1])));
        const double lim = 1.5 * std::max(steady, after) + 0.02;
        if (!(jump < lim)) std::printf("       row %d: step %.4f (steady %.4f / %.4f)\n", row, jump, steady, after);
        CHECK(jump < lim);
    }
}

TEST(lofi_random_settings_stay_finite) {
    uint32_t seed = 4007;
    auto rnd = [&seed](int n) {
        seed = seed * 1664525u + 1013904223u;
        return int((seed >> 8) % uint32_t(n));
    };
    lofi::Lofi d;
    Buf l, r;
    noise(l, r, 1.0f, 600);
    for (size_t o = 0; o < l.size(); o += kN) {
        if (o % (8 * kN) == 0) {
            lofi::Params p;
            p.on = uint8_t(rnd(2));
            p.bits = uint8_t(4 + rnd(13));
            p.rate = uint8_t(rnd(lofi::kRates));
            p.ladder = uint8_t(rnd(101));
            p.speaker = uint8_t(rnd(lofi::kSpeakers));
            p.mono = uint8_t(rnd(2));
            p.mix = uint8_t(rnd(101));
            d.setParams(p);
        }
        d.process(l.data() + o, r.data() + o, kN);
    }
    bool ok = true;
    float pk = 0;
    long sub = 0;
    for (size_t i = 0; i < l.size(); ++i) {
        ok = ok && std::isfinite(l[i]) && std::isfinite(r[i]);
        pk = std::max(pk, std::max(std::fabs(l[i]), std::fabs(r[i])));
        sub += std::fpclassify(l[i]) == FP_SUBNORMAL;
    }
    CHECK(ok);
    CHECK(pk < 2.0f);
    CHECK_EQ(sub, 0L);
}

TEST(lofi_section_and_page) {
    mod::ModuleHost::instance().init();
    mod::IModule* m = const_cast<mod::IModule*>(mod::ModuleHost::instance().find("LOFI"));
    CHECK(m != nullptr);
    if (!m) return;
    CHECK(m->masterInsert() != nullptr);
    mod::IProjectSection* s = m->section();
    CHECK(s != nullptr);
    CHECK_EQ(int(s->tag()), 0x4007);
    CHECK_EQ(int(s->version()), 1);
    s->reset();
    uint8_t buf[16] = {};
    CHECK_EQ(s->encode(buf, sizeof buf), 0u);   // Off at the defaults: nothing saved
    lofi::Params p;
    p.on = 1;
    p.bits = 5;
    p.rate = lofi::k6k;
    p.ladder = 40;
    p.speaker = lofi::kHandheld;
    p.mono = 1;
    p.mix = 80;
    mod::lofiSet(p);
    CHECK(s->dirty());
    CHECK_EQ(s->encode(buf, sizeof buf), 8u);
    CHECK(!s->dirty());
    s->reset();
    CHECK_EQ(int(mod::lofiGet().on), 0);
    CHECK(s->decode(buf, 8, 1));
    const lofi::Params q = mod::lofiGet();
    CHECK(q.on == 1 && q.bits == 5 && q.rate == lofi::k6k && q.ladder == 40 && q.speaker == lofi::kHandheld && q.mono == 1 &&
          q.mix == 80);
    // Out of range values clamp.
    const uint8_t bad[8] = {7, 99, 99, 255, 9, 3, 255, 0};
    CHECK(s->decode(bad, 8, 1));
    const lofi::Params c = mod::lofiGet();
    CHECK(c.on == 1 && c.bits == 16 && c.rate == lofi::k4k && c.ladder == 100 && c.speaker == lofi::kMonitor && c.mono == 1 &&
          c.mix == 100);
    // The page: seven rows, values and adjust.
    mod::IModulePage* pg = m->page();
    CHECK(pg != nullptr);
    CHECK_STR(pg->title(), "LoFi");
    CHECK_EQ(pg->rows(), 7);
    s->reset();
    mod::ModuleRow row;
    pg->row(0, row);
    CHECK_STR(row.label, "LoFi");
    CHECK_STR(row.value, "Off");
    pg->adjust(0, 1);
    pg->row(0, row);
    CHECK_STR(row.value, "On");
    pg->row(2, row);
    CHECK_STR(row.label, "Rate");
    CHECK_STR(row.value, "11 kHz");
    pg->adjust(2, 1);
    pg->row(2, row);
    CHECK_STR(row.value, "8 kHz");
    pg->row(4, row);
    CHECK_STR(row.value, "TV");
    pg->adjust(4, 1);
    pg->row(4, row);
    CHECK_STR(row.value, "Handheld");
    pg->adjust(1, 20);
    pg->row(1, row);
    CHECK_STR(row.value, "Off");   // Bits 16: no DAC
    pg->adjust(6, -30);
    CHECK_EQ(int(mod::lofiGet().mix), 70);
    s->reset();
}

TEST(lofi_ladder_matches_the_core_for_every_bits) {
    for (int bits = 4; bits <= 15; ++bits)
        for (int amount : {37, 100}) {
            lofi::Params p = neutral();
            p.bits = uint8_t(bits);
            p.ladder = uint8_t(amount);
            lofi::Lofi d;
            d.setParams(p);
            Buf l(8 * kN), r(8 * kN);
            for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = -1.05f + 2.1f * float(i) / float(l.size());
            const Buf in = l;
            run(d, l, r);
            double worst = 0;
            for (size_t i = kN; i < l.size(); ++i)
                worst = std::max(worst, double(std::fabs(l[i] - chip::ladder(in[i], bits, float(amount) * 0.01f))));
            if (!(worst < 1e-6)) std::printf("       bits %d ladder %d: %.3g off the core\n", bits, amount, worst);
            CHECK(worst < 1e-6);
        }
    // Bits 16 is no DAC: a Ladder does nothing there (no clamp of the bus).
    lofi::Params p = neutral();
    p.ladder = 75;
    lofi::Lofi d;
    d.setParams(p);
    Buf l, r;
    sine(l, r, 0.9f, 8);
    for (float& x : l) x += 0.5f;
    const Buf l0 = l;
    run(d, l, r);
    CHECK(std::memcmp(l.data() + kN, l0.data() + kN, 7 * kN * sizeof(float)) == 0);
}

TEST(lofi_changes_between_settings_do_not_click) {
    struct Step {
        uint8_t row, a, b;
    };
    // row: 2 rate, 4 speaker, 5 mono, 6 mix
    const Step steps[] = {{4, lofi::kTv, lofi::kHandheld}, {4, lofi::kMonitor, lofi::kTv}, {2, lofi::k11k, lofi::k4k},
                          {2, lofi::k11k, lofi::k32k},     {5, 1, 0},                    {6, 100, 30}};
    for (const Step& st : steps) {
        lofi::Params a;
        a.on = 1;
        a.bits = 10;
        uint8_t* fa[7] = {&a.on, &a.bits, &a.rate, &a.ladder, &a.speaker, &a.mono, &a.mix};
        *fa[st.row] = st.a;
        lofi::Params b = a;
        uint8_t* fb[7] = {&b.on, &b.bits, &b.rate, &b.ladder, &b.speaker, &b.mono, &b.mix};
        *fb[st.row] = st.b;
        if (st.row == 2) b.speaker = lofi::kHandheld;   // and a second row at once
        lofi::Lofi d;
        d.setParams(a);
        Buf l, r;
        sine(l, r, 0.5f, 40, 110.0f);
        size_t o = 0;
        for (; o < 20 * kN; o += kN) d.process(l.data() + o, r.data() + o, kN);
        double steady = 0;
        for (size_t i = 10 * kN + 1; i < 20 * kN; ++i) steady = std::max(steady, double(std::fabs(l[i] - l[i - 1])));
        d.setParams(b);
        for (; o < l.size(); o += kN) d.process(l.data() + o, r.data() + o, kN);
        double jump = 0, after = 0;
        for (size_t i = 20 * kN; i < 22 * kN; ++i) jump = std::max(jump, double(std::fabs(l[i] - l[i - 1])));
        for (size_t i = 30 * kN; i < 40 * kN; ++i) after = std::max(after, double(std::fabs(l[i] - l[i - 1])));
        const double lim = 1.5 * std::max(steady, after) + 0.02;
        if (!(jump < lim)) std::printf("       row %d %d -> %d: step %.4f (steady %.4f / %.4f)\n", st.row, st.a, st.b, jump, steady, after);
        CHECK(jump < lim);
    }
    // A new Rate holds its first value at once: DC through Off -> 4 kHz stays flat.
    lofi::Params p = neutral();
    lofi::Lofi d;
    d.setParams(p);
    Buf l(8 * kN, 0.9f), r(8 * kN, 0.9f);
    for (size_t o = 0; o < 4 * kN; o += kN) d.process(l.data() + o, r.data() + o, kN);
    p.rate = lofi::k4k;
    d.setParams(p);
    for (size_t o = 4 * kN; o < l.size(); o += kN) d.process(l.data() + o, r.data() + o, kN);
    float lo = 1.0f;
    for (size_t i = 4 * kN; i < l.size(); ++i) lo = std::min(lo, l[i]);
    CHECK(lo > 0.899f);
}

TEST(lofi_short_blocks_short_data_and_golden_hash) {
    // A short section keeps the defaults of the fields it lacks.
    mod::ModuleHost::instance().init();
    mod::IModule* m = const_cast<mod::IModule*>(mod::ModuleHost::instance().find("LOFI"));
    CHECK(m != nullptr);
    if (!m) return;
    const uint8_t b[3] = {1, 6, lofi::k8k};
    CHECK(m->section()->decode(b, 3, 1));
    const lofi::Params q = mod::lofiGet();
    const lofi::Params def;
    CHECK(q.on == 1 && q.bits == 6 && q.rate == lofi::k8k && q.ladder == def.ladder && q.speaker == def.speaker && q.mix == def.mix);
    m->section()->reset();
    // Blocks shorter than kBlock; every row moving: the golden hash.
    lofi::Lofi d;
    Buf l, r;
    noise(l, r, 0.7f, 64, 99);
    uint64_t h = 1469598103934665603ull;
    size_t o = 0;
    for (int k = 0; o + 100 <= l.size(); ++k) {
        lofi::Params p;
        p.on = 1;
        p.bits = uint8_t(4 + k % 13);
        p.rate = uint8_t(k % lofi::kRates);
        p.ladder = uint8_t((k * 37) % 101);
        p.speaker = uint8_t(k % lofi::kSpeakers);
        p.mono = uint8_t((k / 3) & 1);
        p.mix = uint8_t(40 + (k * 13) % 61);
        if (k % 4 == 0) d.setParams(p);
        const uint32_t n = k % 2 ? 100u : kN;
        d.process(l.data() + o, r.data() + o, n);
        for (uint32_t i = 0; i < n; ++i) {
            for (float v : {l[o + i], r[o + i]}) {
                uint32_t bits;
                std::memcpy(&bits, &v, 4);
                for (int j = 0; j < 4; ++j) {
                    h ^= (bits >> (8 * j)) & 0xFFu;
                    h *= 1099511628211ull;
                }
            }
        }
        o += n;
    }
    std::printf("  LoFi golden: %016llx\n", (unsigned long long)h);
    CHECK_EQ(h, uint64_t(0xe38745cf39dffb1aull));
}
