// BBDC master-bus effect (opentracker-fx modules/bbd), both modes: Off
// passes the bus through bit for bit; silence in gives silence out; the LFO
// runs at the mode's rate (I 0.5 Hz, II 0.8 Hz); the taps stay in the mode's
// delay range (I 2.0..5.0 ms, II 1.5..5.5 ms, II deeper and faster) on
// opposite phases and decorrelate the sides; Width 0 keeps a mono input
// mono; no NaN or denormals; levels stay near unity; a mode switch does not
// jump the delay; the project data round-trips and the page rows adjust.
#include "test.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "audio/synth_engine.h"
#include "bbdc.h"
#include "modules/module.h"
#include "modules/module_ui.h"

using namespace ot;
using namespace ot::audio;

namespace ot {
namespace mod {
void bbdcSet(const audio::bbdc::Params& p);
audio::bbdc::Params bbdcGet();
}  // namespace mod
}  // namespace ot

namespace {
constexpr uint32_t kN = kBlock;
constexpr float kFs = 44117.647f;

// The reference values per mode (index = Params::mode - 1).
struct Ref {
    float hz, minMs, maxMs;
    const char* name;
};
constexpr Ref kRef[2] = {{0.5f, 2.0f, 5.0f, "I"}, {0.8f, 1.5f, 5.5f, "II"}};

struct Rig {
    int16_t line[bbdc::kLine] = {};
    bbdc::BbdChorus fx{line};
    explicit Rig(uint8_t mode, uint8_t mix = 100, uint8_t width = 100) {
        bbdc::Params p;
        p.mode = mode;
        p.mix = mix;
        p.width = width;
        fx.setParams(p);
    }
};

void tone(std::vector<float>& l, std::vector<float>& r, int blocks, float hz = 440.0f, float amp = 0.4f) {
    l.resize(size_t(blocks) * kN);
    r.resize(l.size());
    for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = amp * std::sin(6.2831853f * hz * float(i) / kFs);
}
// White noise, or with soft = true low-passed (~3 kHz) to stay inside the BBD band.
void noise(std::vector<float>& l, std::vector<float>& r, int blocks, float amp = 0.4f, bool soft = false) {
    l.resize(size_t(blocks) * kN);
    r.resize(l.size());
    uint32_t x = 12345;
    float s0 = 0.0f, s1 = 0.0f;
    for (size_t i = 0; i < l.size(); ++i) {
        x = x * 1664525u + 1013904223u;
        float v = amp * float(int32_t(x)) * (1.0f / 2147483648.0f);
        if (soft) {
            s0 += 0.35f * (v - s0);
            s1 += 0.35f * (s0 - s1);
            v = 2.0f * s1;
        }
        l[i] = r[i] = v;
    }
}
void run(bbdc::BbdChorus& j, std::vector<float>& l, std::vector<float>& r) {
    for (size_t o = 0; o < l.size(); o += kN) j.process(l.data() + o, r.data() + o, kN);
}
double rms(const std::vector<float>& x, size_t from) {
    double s = 0;
    for (size_t i = from; i < x.size(); ++i) s += double(x[i]) * x[i];
    return std::sqrt(s / double(x.size() - from));
}
double corr(const std::vector<float>& a, const std::vector<float>& b, size_t from) {
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = from; i < a.size(); ++i) {
        ab += double(a[i]) * b[i];
        aa += double(a[i]) * a[i];
        bb += double(b[i]) * b[i];
    }
    return ab / std::sqrt(aa * bb);
}
bool clean(const std::vector<float>& x) {
    for (float v : x)
        if (!std::isfinite(v) || std::fpclassify(v) == FP_SUBNORMAL) return false;
    return true;
}
}  // namespace

TEST(bbdc_off_is_bit_exact) {
    int16_t line[bbdc::kLine] = {};
    bbdc::BbdChorus j(line);
    std::vector<float> l, r;
    tone(l, r, 10);
    const std::vector<float> l0 = l;
    run(j, l, r);
    CHECK(l == l0);
    float a[kN] = {}, b[kN] = {};
    CHECK(!j.process(a, b, kN));
}

TEST(bbdc_silence_in_silence_out) {
    for (uint8_t mode = 1; mode <= 2; ++mode) {
        Rig g(mode);
        std::vector<float> l(200 * kN, 0.0f), r(l.size(), 0.0f);
        run(g.fx, l, r);
        for (size_t i = 0; i < l.size(); ++i) CHECK(l[i] == 0.0f && r[i] == 0.0f);
        // After sound the line empties to exact zeros.
        std::vector<float> nl, nr;
        noise(nl, nr, 50);
        run(g.fx, nl, nr);
        std::vector<float> sl(100 * kN, 0.0f), sr(sl.size(), 0.0f);
        run(g.fx, sl, sr);
        for (size_t i = 20 * kN; i < sl.size(); ++i) CHECK(sl[i] == 0.0f && sr[i] == 0.0f);
    }
}

TEST(bbdc_lfo_runs_at_the_mode_rate) {
    for (uint8_t mode = 1; mode <= 2; ++mode) {
        const Ref& ref = kRef[mode - 1];
        const bbdc::Mode& md = bbdc::kModes[mode - 1];
        Rig g(mode);
        float a[kN], b[kN];
        // Upward crossings of the centre by the left tap delay, 12 s of blocks.
        const int blocks = int(12.0f * kFs / float(kN));
        std::vector<double> ups;
        float prev = -1.0f;
        for (int k = 1; k <= blocks; ++k) {
            for (uint32_t i = 0; i < kN; ++i) a[i] = b[i] = 0.0f;
            g.fx.process(a, b, kN);
            const float d = g.fx.delay(0);
            if (prev >= 0.0f && prev < md.centre && d >= md.centre) ups.push_back(double(k));
            prev = d;
        }
        CHECK(ups.size() >= 4);
        if (ups.size() < 2) continue;
        const double period = (ups.back() - ups.front()) / double(ups.size() - 1) * double(kN) / double(kFs);
        CHECK(std::fabs(1.0 / period - double(ref.hz)) < 0.01 * double(ref.hz));
        std::printf("  Chorus %s: LFO %.3f Hz\n", ref.name, 1.0 / period);
    }
    CHECK(bbdc::kModes[1].hz > bbdc::kModes[0].hz);   // II faster ...
}

TEST(bbdc_delay_stays_in_range_on_opposite_phases) {
    for (uint8_t mode = 1; mode <= 2; ++mode) {
        const Ref& ref = kRef[mode - 1];
        const bbdc::Mode& md = bbdc::kModes[mode - 1];
        Rig g(mode);
        float a[kN], b[kN];
        float lo[2] = {1e9f, 1e9f}, hi[2] = {-1e9f, -1e9f};
        const int blocks = int(3.0f / ref.hz * kFs / float(kN));   // three LFO cycles
        for (int k = 0; k < blocks; ++k) {
            for (uint32_t i = 0; i < kN; ++i) a[i] = b[i] = 0.1f;
            g.fx.process(a, b, kN);
            const float d[2] = {g.fx.delay(0), g.fx.delay(1)};
            CHECK(std::fabs(d[0] + d[1] - 2.0f * md.centre) < 1e-3f);   // opposite phase
            for (int c = 0; c < 2; ++c) {
                lo[c] = std::fmin(lo[c], d[c]);
                hi[c] = std::fmax(hi[c], d[c]);
            }
        }
        // Each tap sweeps the whole range (to within a block's travel).
        const float ms = kFs / 1000.0f;
        for (int c = 0; c < 2; ++c) {
            CHECK(lo[c] >= ref.minMs * ms - 0.01f && lo[c] < ref.minMs * ms + 2.0f);
            CHECK(hi[c] <= ref.maxMs * ms + 0.01f && hi[c] > ref.maxMs * ms - 2.0f);
            CHECK(hi[c] + 2.0f < float(bbdc::kLine));
        }
    }
    CHECK(bbdc::kModes[1].depth > bbdc::kModes[0].depth);   // ... and deeper than I
}

TEST(bbdc_decorrelates_the_sides) {
    std::vector<float> l, r;
    noise(l, r, 400, 0.4f, true);
    const size_t from = 20 * kN;
    for (uint8_t mode = 1; mode <= 2; ++mode) {
        Rig wide(mode);
        std::vector<float> l1 = l, r1 = r;
        run(wide.fx, l1, r1);
        // Mono in: the dry part (0.7 at Mix 100) is common, the wet parts move apart.
        std::vector<float> wl(l1.size()), wr(l1.size());
        for (size_t i = 0; i < l1.size(); ++i) {
            wl[i] = l1[i] - 0.7f * l[i];
            wr[i] = r1[i] - 0.7f * r[i];
        }
        CHECK(corr(wl, wr, from) < 0.5);
        CHECK(corr(l1, r1, from) < 0.85);
        CHECK(corr(l1, r1, from) > 0.2);
        std::printf("  Chorus %s: L/R correlation %.2f (wet parts %.2f)\n", kRef[mode - 1].name, corr(l1, r1, from),
                    corr(wl, wr, from));
        Rig mono(mode, 100, 0);
        std::vector<float> l0 = l, r0 = r;
        run(mono.fx, l0, r0);
        CHECK(l0 == r0);   // Width 0: mono in, mono out
        // The wet signal moves: the output is not a fixed filter of the input.
        Rig g(mode);
        std::vector<float> lt, rt;
        tone(lt, rt, 400, 1000.0f);
        run(g.fx, lt, rt);
        double env0 = 1e9, env1 = 0;
        for (size_t o = from; o + kN <= lt.size(); o += kN) {
            double s = 0;
            for (size_t i = o; i < o + kN; ++i) s += double(lt[i]) * lt[i];
            env0 = std::fmin(env0, s);
            env1 = std::fmax(env1, s);
        }
        CHECK(env1 > 1.2 * env0);
    }
}

TEST(bbdc_keeps_the_level_and_stays_finite) {
    for (uint8_t mode = 1; mode <= 2; ++mode) {
        std::vector<float> dl, dr;
        tone(dl, dr, 300, 220.0f);
        std::vector<float> l = dl, r = dr;
        Rig g(mode);
        run(g.fx, l, r);
        const double ratio = rms(l, 20 * kN) / rms(dl, 20 * kN);
        CHECK(ratio > 0.7 && ratio < 1.5);
        std::printf("  Chorus %s: level x%.2f\n", kRef[mode - 1].name, ratio);
        // Hot noise, then extremes, then silence: finite, no denormals.
        std::vector<float> nl, nr;
        noise(nl, nr, 100, 8.0f);
        for (size_t i = 0; i < nl.size(); i += 7) nl[i] = nr[i] = (i & 8) ? 1e6f : -1e6f;
        run(g.fx, nl, nr);
        CHECK(clean(nl) && clean(nr));
        std::vector<float> tl(2000 * kN, 0.0f), tr(tl.size(), 0.0f);
        tl[5] = 1e-30f;   // a tiny input decays to exact zeros
        run(g.fx, tl, tr);
        CHECK(clean(tl) && clean(tr));
        CHECK(tl.back() == 0.0f && tr.back() == 0.0f);
    }
}

TEST(bbdc_mode_switch_ramps_the_delay) {
    // A 100 Hz tone through Chorus I, then II: the taps glide to the new
    // mode's delay (a jump would step the wet signal, a click).
    std::vector<float> l, r;
    tone(l, r, 200, 100.0f);
    Rig g(1);
    float maxStep = 0.0f, switchStep = 0.0f;
    float prev = 0.0f;
    for (int k = 0; k < 200; ++k) {
        if (k == 100) {
            bbdc::Params p = g.fx.params();
            p.mode = 2;
            g.fx.setParams(p);
        }
        float* bl = l.data() + size_t(k) * kN;
        g.fx.process(bl, r.data() + size_t(k) * kN, kN);
        for (uint32_t i = 0; i < kN; ++i) {
            const float d = std::fabs(bl[i] - prev);
            prev = bl[i];
            if (k >= 20 && k < 100) maxStep = std::fmax(maxStep, d);
            if (k == 100 || k == 101) switchStep = std::fmax(switchStep, d);
        }
    }
    CHECK(switchStep < 1.5f * maxStep);
    CHECK(std::fabs(g.fx.delay(0) + g.fx.delay(1) - 2.0f * bbdc::kModes[1].centre) < 1e-3f);
}

// Switching on (Off -> I, II or I+II; also Mix up from 0) does not click:
// the taps read an empty line for their first 2-5.5 ms, so the wet came in
// with a step when they reached the signal (a user heard it on a synth
// insert). A low sine shows it best: the largest step around the switch stays
// within the steady signal's.
TEST(bbdc_switching_on_does_not_click) {
    for (uint8_t mode : {1, 2, 3})
        for (float hz : {55.0f, 110.0f}) {
            std::vector<float> l, r;
            tone(l, r, 120, hz);
            Rig g(0);
            for (int k = 0; k < 120; ++k) {
                if (k == 40) {
                    bbdc::Params p = g.fx.params();
                    p.mode = mode;
                    g.fx.setParams(p);
                }
                g.fx.process(l.data() + size_t(k) * kN, r.data() + size_t(k) * kN, kN);
            }
            float steady = 0.0f, sw = 0.0f;
            for (size_t i = 1; i < l.size(); ++i) {
                const float d = std::fabs(l[i] - l[i - 1]);
                if (i < 39 * kN || i >= 60 * kN) steady = std::fmax(steady, d);
                else sw = std::fmax(sw, d);
            }
            if (!(sw < 1.5f * steady)) std::printf("       mode %d %g Hz: step %.5f, steady %.5f\n", mode, double(hz), double(sw), double(steady));
            CHECK(sw < 1.5f * steady);
        }
}

TEST(bbdc_off_after_on_returns_to_dry) {
    Rig g(2);
    std::vector<float> l, r;
    tone(l, r, 20);
    run(g.fx, l, r);
    bbdc::Params p = g.fx.params();
    p.mode = 0;
    g.fx.setParams(p);
    std::vector<float> l2, r2;
    tone(l2, r2, 3);
    const std::vector<float> dry = l2;
    g.fx.process(l2.data(), r2.data(), kN);                 // fades out
    CHECK(!g.fx.process(l2.data() + kN, r2.data() + kN, kN));
    CHECK(std::memcmp(l2.data() + kN, dry.data() + kN, kN * sizeof(float)) == 0);
}

TEST(bbdc_module_data_and_page) {
    mod::ModuleHost& h = mod::ModuleHost::instance();
    h.init();
    mod::IModule* m = const_cast<mod::IModule*>(h.find("BBDC"));
    CHECK(m != nullptr);
    if (!m) return;
    mod::IProjectSection* sec = m->section();
    mod::IModulePage* pg = m->page();
    CHECK(m->masterInsert() != nullptr);
    CHECK(sec && sec->tag() == 0x4003 && pg && pg->rows() == 4);
    if (!sec || !pg) return;
    sec->reset();
    uint8_t b[16];
    CHECK_EQ(sec->encode(b, sizeof b), 0u);   // Off at the defaults: nothing saved
    mod::ModuleRow row{};
    pg->adjust(0, 1);     // Chorus I
    pg->row(0, row);
    CHECK_STR(row.value, "I");
    pg->adjust(0, 5);     // Chorus II (the last)
    pg->row(0, row);
    CHECK_STR(row.value, "II");
    pg->adjust(1, -40);   // Mix 100 -> 60
    pg->adjust(3, 30);    // Tone 50 -> 80
    CHECK(sec->dirty());
    CHECK(mod::bbdcGet().mode == 2 && mod::bbdcGet().mix == 60 && mod::bbdcGet().tone == 80);
    pg->row(3, row);
    CHECK(std::strstr(row.value, "kHz") != nullptr);
    CHECK_EQ(sec->encode(b, sizeof b), 8u);
    CHECK(!sec->dirty());
    sec->reset();
    CHECK_EQ(int(mod::bbdcGet().mode), 0);
    CHECK(sec->decode(b, 8, 1));
    const bbdc::Params q = mod::bbdcGet();
    CHECK(q.mode == 2 && q.mix == 60 && q.width == 100 && q.tone == 80);
    // A short payload keeps the defaults for the rest; values clamp.
    const uint8_t s[2] = {7, 250};
    CHECK(sec->decode(s, 2, 1));
    CHECK(mod::bbdcGet().mode == 2 && mod::bbdcGet().mix == 100 && mod::bbdcGet().tone == 50);
    // A mode past II from code (bbdcSet) shows and runs as II.
    bbdc::Params bad;
    bad.mode = 9;
    mod::bbdcSet(bad);
    pg->row(0, row);
    CHECK_STR(row.value, "II");
    sec->reset();
}

// The module was called JUNO before 3.0: projects saved with that id (its
// project section and the module list of the sidecar) still find it.
TEST(bbdc_answers_to_its_old_id) {
    mod::ModuleHost& h = mod::ModuleHost::instance();
    h.init();
    const mod::IModule* m = h.find("BBDC");
    CHECK(m != nullptr);
    CHECK(h.find("JUNO") == m);
    CHECK(h.find("JUNX") == nullptr);
}

// The DSP is shared with the JU6 synth's chorus (core audio/bbd_chorus.h):
// the insert's output is the one recorded before the move, bit for bit
// (both modes, a Tone / Width / Mix change, a mode switch, Off and its fade;
// host build, -ffp-contract=off).
TEST(bbdc_output_is_unchanged_by_the_shared_dsp) {
    int16_t line[bbdc::kLine] = {};
    bbdc::BbdChorus j(line);
    std::vector<float> l, r;
    noise(l, r, 400, 0.5f);
    for (size_t i = 0; i < r.size(); ++i) r[i] = 0.7f * r[i] + 0.2f * std::sin(0.01f * float(i));
    const bbdc::Params steps[5] = {{1, 100, 100, 50}, {2, 80, 60, 20}, {2, 100, 0, 90}, {1, 40, 100, 100}, {0, 100, 100, 50}};
    for (size_t o = 0, b = 0; o < l.size(); o += kN, ++b) {
        if (b % 80 == 0) j.setParams(steps[b / 80]);
        j.process(l.data() + o, r.data() + o, kN);
    }
    uint64_t h = 1469598103934665603ull;
    for (const std::vector<float>* x : {&l, &r})
        for (float s : *x) {
            uint32_t v;
            std::memcpy(&v, &s, 4);
            for (int k = 0; k < 4; ++k) {
                h ^= (v >> (8 * k)) & 0xFFu;
                h *= 1099511628211ull;
            }
        }
    std::printf("  BBDC golden %016llx\n", (unsigned long long)h);
    CHECK_EQ(h, uint64_t(0x717a39f30c9ed601ull));   // 2 blocks dry at the start: the line fills first
}

// The chorus as a per-instance effect (IEffect): called the way a synth slot calls
// it (caller-owned state and line, settings per block), the output is the master
// insert's above, bit for bit; Mode offers I+II too, the master page does not.
TEST(bbdc_effect_equals_the_master_insert) {
    IEffect& fx = bbdc::gChorusEffect;
    CHECK_STR(fx.id(), "BBDC");
    CHECK_EQ(fx.paramCount(), 4);
    CHECK_EQ(int(fx.param(0).max), 2);
    char t[16];
    fx.valueText(0, 2, t, sizeof(t));
    CHECK_STR(t, "I+II");
    fx.valueText(3, 50, t, sizeof(t));
    CHECK_STR(t, "8.5 kHz");
    fx.valueText(1, 80, t, sizeof(t));
    CHECK_STR(t, "80");
    const EffectSettings d = effectDefaults(fx);
    CHECK(d.v[0] == 0 && d.v[1] == 100 && d.v[2] == 100 && d.v[3] == 50);
    alignas(8) uint8_t state[kEffectStateMax] = {};
    int16_t line[bbdc::kLine] = {};
    CHECK(fx.stateBytes() <= sizeof(state));
    std::vector<float> l, r;
    noise(l, r, 400, 0.5f);
    for (size_t i = 0; i < r.size(); ++i) r[i] = 0.7f * r[i] + 0.2f * std::sin(0.01f * float(i));
    // The master test's steps as effect settings (mode 0 = off).
    const uint8_t steps[5][4] = {{1, 100, 100, 50}, {2, 80, 60, 20}, {2, 100, 0, 90}, {1, 40, 100, 100}, {0, 100, 100, 50}};
    EffectSettings s = {};
    bool on = false;
    for (size_t o = 0, b = 0; o < l.size(); o += kN, ++b) {
        if (b % 80 == 0) {
            const uint8_t* p = steps[b / 80];
            on = p[0] != 0;
            s = EffectSettings{{uint8_t(on ? p[0] - 1 : 0), p[1], p[2], p[3], 0, 0}};
        }
        fx.process(state, line, s, on, l.data() + o, r.data() + o, kN);
    }
    uint64_t h = 1469598103934665603ull;
    for (const std::vector<float>* x : {&l, &r})
        for (float v : *x) {
            uint32_t u;
            std::memcpy(&u, &v, 4);
            for (int k = 0; k < 4; ++k) {
                h ^= (u >> (8 * k)) & 0xFFu;
                h *= 1099511628211ull;
            }
        }
    CHECK_EQ(h, uint64_t(0x717a39f30c9ed601ull));   // 2 blocks dry at the start: the line fills first
    // The module serves it, and the master insert starts at the old defaults.
    CHECK(mod::ModuleHost::instance().effectById("BBDC") == &fx);
    int16_t line2[bbdc::kLine] = {};
    const bbdc::BbdChorus m(line2);
    const bbdc::Params p = m.params();
    CHECK(p.mode == 0 && p.mix == 100 && p.width == 100 && p.tone == 50);
}
