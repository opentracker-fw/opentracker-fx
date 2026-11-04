// ECHO tempo-synced dub echo (opentracker-fx modules/echo): Off and no line
// pass the bus through; the repeats land on the note division of the tempo
// (120 and 130 BPM) and fall by the feedback; ping-pong alternates the
// sides; a tempo change moves the repeats without a click; the low cut and
// the tone shape the repeats; duck turns them down under a loud input; the
// dry bus is untouched; the tail plays out (also after Off) and ends; no
// clicks; random settings stay finite; the project data and the page; the
// line comes from ModuleHost::sdram().
#include "test.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "audio/synth_engine.h"
#include "echo.h"
#include "modules/module.h"
#include "modules/module_ui.h"

using namespace ot;
using namespace ot::audio;

namespace ot {
namespace mod {
void echoSet(const audio::echo::Params& p);
audio::echo::Params echoGet();
}  // namespace mod
}  // namespace ot

namespace {
constexpr uint32_t kN = kBlock;
constexpr float kFs = ot::audio::kSampleRate;
using Buf = std::vector<float>;

// An Echo with its own line.
struct Rig {
    std::vector<int16_t> line = std::vector<int16_t>(echo::kLine);
    echo::Echo e;
    Buf L, R;
    std::vector<bool> tail;
    explicit Rig(const echo::Params& p, float bpm = 120.0f) {
        e.setLine(line.data());
        e.setTempo(bpm);
        e.setParams(p);
    }
    // n blocks of input (in() per frame: left, right).
    template <class F>
    void run(int blocks, F in) {
        for (int b = 0; b < blocks; ++b) {
            float l[kN], r[kN];
            for (uint32_t i = 0; i < kN; ++i) {
                const size_t k = L.size() + i;
                in(k, l[i], r[i]);
            }
            tail.push_back(e.process(l, r, kN));
            L.insert(L.end(), l, l + kN);
            R.insert(R.end(), r, r + kN);
        }
    }
    void silence(int blocks) {
        run(blocks, [](size_t, float& l, float& r) { l = r = 0.0f; });
    }
};
echo::Params open(uint8_t time = 4, uint8_t fb = 50, bool pp = false) {
    echo::Params p;
    p.on = 1;
    p.time = time;
    p.feedback = fb;
    p.lowcut = 0;
    p.tone = 100;
    p.pingpong = pp ? 1 : 0;
    p.duck = 0;
    p.mix = 100;
    return p;
}
// The frame of the largest |x| in [a, b).
size_t peakAt(const Buf& x, size_t a, size_t b) {
    size_t at = a;
    for (size_t i = a; i < b && i < x.size(); ++i)
        if (std::fabs(x[i]) > std::fabs(x[at])) at = i;
    return at;
}
float peak(const Buf& x, size_t a = 0, size_t b = SIZE_MAX) {
    float m = 0.0f;
    for (size_t i = a; i < b && i < x.size(); ++i) m = std::fmax(m, std::fabs(x[i]));
    return m;
}
double rms(const Buf& x, size_t a, size_t b) {
    double s = 0.0;
    b = std::min(b, x.size());
    for (size_t i = a; i < b; ++i) s += double(x[i]) * x[i];
    return std::sqrt(s / double(b > a ? b - a : 1));
}
// An impulse in both channels at frame kImp (after the first block, in which
// the input into the line fades in from Off).
constexpr size_t kImp = 200;
void impulse(size_t k, float& l, float& r) { l = r = k == kImp ? 1.0f : 0.0f; }
}  // namespace

TEST(echo_off_and_without_a_line_pass_through) {
    Rig r(echo::Params{});   // Off
    r.run(20, [](size_t k, float& l, float& rr) { l = rr = 0.5f * std::sin(0.01f * float(k)); });
    for (size_t i = 0; i < r.L.size(); ++i) CHECK_EQ(r.L[i], 0.5f * std::sin(0.01f * float(i)));
    for (bool t : r.tail) CHECK(!t);
    echo::Echo e;   // no line: On does nothing
    e.setParams(open());
    float l[kN], rr[kN];
    for (uint32_t i = 0; i < kN; ++i) l[i] = rr[i] = 0.3f;
    CHECK(!e.process(l, rr, kN));
    for (uint32_t i = 0; i < kN; ++i) CHECK_EQ(l[i], 0.3f);
}

TEST(echo_divisions_and_clamp) {
    CHECK_NEAR(echo::timeBeats(0), 0.125f, 1e-6);
    CHECK_NEAR(echo::timeBeats(3), 1.0f / 3.0f, 1e-6);
    CHECK_NEAR(echo::timeBeats(5), 0.75f, 1e-6);
    CHECK_NEAR(echo::timeBeats(7), 1.0f, 1e-6);
    CHECK_NEAR(echo::timeBeats(9), 2.0f, 1e-6);
    CHECK(std::strcmp(echo::kTimeName[5], "1/8.") == 0);
    CHECK_EQ(int(echo::Params{}.time), 5);   // 1/8.: ping-pong fits from 81 BPM with the 48 KB line
    CHECK_EQ(echo::kLine, 24576u);           // 48 KB of int16 at half rate
    echo::Echo e;
    bool c = true;
    CHECK_NEAR(e.seconds(open(4), 120.0f, &c), 0.25f, 1e-4);
    CHECK(!c);
    // The line holds 1.114 s (ping-pong: 557 ms): 1/2 at 120 BPM (1 s) fits, at 100 BPM (1.2 s) not.
    e.seconds(open(9), 120.0f, &c);
    CHECK(!c);
    e.seconds(open(9), 100.0f, &c);
    CHECK(c);
    CHECK(e.seconds(open(9), 100.0f) < 1.12f);
    e.seconds(open(7, 50, true), 120.0f, &c);   // 1/4 ping-pong: 500 ms of 557 ms
    CHECK(!c);
    e.seconds(open(7, 50, true), 100.0f, &c);   // 600 ms
    CHECK(c);
    e.seconds(open(5, 50, true), 81.0f, &c);    // 1/8. ping-pong at 81 BPM: 555.6 ms
    CHECK(!c);
}

TEST(echo_repeats_on_the_tempo) {
    for (float bpm : {120.0f, 130.0f}) {
        Rig r(open(4, 50), bpm);   // 1/8
        r.run(int(1.2f * kFs / kN), impulse);
        const double t = 0.5 * 60.0 / bpm * kFs;
        const size_t p1 = peakAt(r.L, kImp + size_t(t * 0.5), kImp + size_t(t * 1.5));
        const size_t p2 = peakAt(r.L, kImp + size_t(t * 1.5), kImp + size_t(t * 2.5));
        CHECK(std::fabs(double(p1) - double(kImp) - t) <= 2.0);
        CHECK(std::fabs(double(p2) - double(kImp) - 2.0 * t) <= 2.0);
        // Each repeat is the one before x the feedback (0.98 x 50 %).
        const double ratio = double(peak(r.L, p2 - 8, p2 + 8)) / double(peak(r.L, p1 - 8, p1 + 8));
        CHECK_NEAR(ratio, 0.49, 0.03);
        // Stereo: both sides the same repeats.
        CHECK(std::fabs(r.R[p1] - r.L[p1]) < 1e-6f);
    }
}

// The longest stereo repeat (1/2 at 120 BPM: 1 s, near the end of the line):
// on time, and again after a wrap of the ring.
TEST(echo_long_repeat_across_the_wrap) {
    Rig r(open(9, 60), 120.0f);
    r.run(int(2.3f * kFs / kN), impulse);
    const double t = 1.0 * kFs;
    const size_t p1 = peakAt(r.L, kImp + size_t(t * 0.8), kImp + size_t(t * 1.2));
    const size_t p2 = peakAt(r.L, kImp + size_t(t * 1.8), kImp + size_t(t * 2.2));
    CHECK(std::fabs(double(p1) - double(kImp) - t) <= 2.0);
    CHECK(std::fabs(double(p2) - double(kImp) - 2.0 * t) <= 3.0);
    CHECK(peak(r.L, p1 - 4, p1 + 4) > 0.1f);
}

// Stereo at a long time, then ping-pong: the stereo time's 2t "tap" (past the
// line, wrapped) must not leak into the switching block.
TEST(echo_mode_switch_reads_no_wrapped_tap) {
    Rig r(open(9, 60), 120.0f);   // 1/2: t = 1 s, 2t wraps the 1.114 s line
    // The wrapped 2t of the stereo tap: 2 t - kLine half-rate samples = ~0.886 s.
    const size_t t = size_t((kFs - 1.0f) * 0.5f + 0.5f);
    const size_t bogus = 2 * (2 * t - echo::kLine);   // frames
    const int switchBlock = int((kImp + bogus) / kN);
    r.run(switchBlock, impulse);
    echo::Params p = open(9, 60, true);
    r.e.setParams(p);
    r.run(1, impulse);
    CHECK(peak(r.R, size_t(switchBlock) * kN, size_t(switchBlock + 1) * kN) < 1e-3f);
}

TEST(echo_ping_pong_alternates) {
    Rig r(open(4, 60, true), 120.0f);   // 1/8 = 250 ms; ping-pong taps 250 / 500 ms fit
    r.run(int(1.4f * kFs / kN), impulse);
    const double t = 0.25 * kFs;
    for (int k = 1; k <= 4; ++k) {
        const size_t a = kImp + size_t(t * (double(k) - 0.3)), b = kImp + size_t(t * (double(k) + 0.3));
        const float pl = peak(r.L, a, b), pr = peak(r.R, a, b);
        if (k % 2) CHECK(pl > 0.05f && pr < 0.01f * pl);
        else CHECK(pr > 0.05f && pl < 0.01f * pr);
    }
    // Every bounce is the one before x the feedback.
    const float r1 = peak(r.L, kImp + size_t(0.7 * t), kImp + size_t(1.3 * t));
    const float r2 = peak(r.R, kImp + size_t(1.7 * t), kImp + size_t(2.3 * t));
    CHECK_NEAR(r2 / r1, 0.98f * 0.6f, 0.04f);
}

TEST(echo_tempo_change_moves_the_repeats_smoothly) {
    Rig r(open(4, 70), 120.0f);
    r.run(int(0.6f * kFs / kN), [](size_t k, float& l, float& rr) { l = rr = 0.3f * std::sin(0.03f * float(k)); });
    float worst = 0.0f;
    const size_t from = r.L.size();
    r.e.setTempo(130.0f);
    r.run(40, [](size_t k, float& l, float& rr) { l = rr = 0.3f * std::sin(0.03f * float(k)); });
    for (size_t i = from; i < r.L.size(); ++i) worst = std::fmax(worst, std::fabs(r.L[i] - r.L[i - 1]));
    CHECK(worst < 0.1f);   // the sine moves <= 0.03 x (0.3 + repeats) per frame
    // Then the repeats are at 130 BPM.
    Rig q(open(4, 0), 120.0f);
    q.e.setTempo(130.0f);
    q.run(int(0.5f * kFs / kN), impulse);
    const double t = 0.5 * 60.0 / 130.0 * kFs;
    CHECK(std::fabs(double(peakAt(q.L, kImp + size_t(0.5 * t), kImp + size_t(1.5 * t))) - double(kImp) - t) <= 2.0);
}

TEST(echo_low_cut_and_tone_shape_the_repeats) {
    auto wet = [](uint8_t lowcut, uint8_t tone, float hz) {
        echo::Params p = open(4, 0);
        p.lowcut = lowcut;
        p.tone = tone;
        Rig a(p), dry(echo::Params{});
        auto in = [hz](size_t k, float& l, float& r) { l = r = 0.2f * std::sin(6.2831853f * hz * float(k) / kFs); };
        a.run(int(1.0f * kFs / kN), in);
        dry.run(int(1.0f * kFs / kN), in);
        Buf w(a.L.size());
        for (size_t i = 0; i < w.size(); ++i) w[i] = a.L[i] - dry.L[i];
        return rms(w, size_t(0.5f * kFs), w.size());
    };
    const double lc = 20.0 * std::log10(wet(100, 100, 50.0f) / wet(0, 100, 50.0f));
    CHECK(lc <= -20.0);
    CHECK(20.0 * std::log10(wet(100, 100, 3000.0f) / wet(0, 100, 3000.0f)) > -1.0);
    const double tn = 20.0 * std::log10(wet(0, 0, 5000.0f) / wet(0, 100, 5000.0f));
    CHECK(tn <= -12.0);
    CHECK(20.0 * std::log10(wet(0, 0, 150.0f) / wet(0, 100, 150.0f)) > -1.0);
    CHECK_NEAR(echo::lowcutHz(0), 20.0f, 0.1f);
    CHECK_NEAR(echo::lowcutHz(100), 500.0f, 1.0f);
    CHECK_NEAR(echo::toneHz(0), 500.0f, 1.0f);
}

TEST(echo_duck_turns_the_repeats_down_under_the_input) {
    auto run = [](uint8_t duck) {
        echo::Params p = open(4, 60);
        p.duck = duck;
        Rig r(p);
        // A loud tone for 0.6 s, then silence: the repeats while it plays and after.
        r.run(int(1.2f * kFs / kN), [](size_t k, float& l, float& rr) {
            l = rr = k < size_t(0.6f * kFs) ? 0.5f * std::sin(0.05f * float(k)) : 0.0f;
        });
        Rig dry(echo::Params{});
        dry.run(int(1.2f * kFs / kN), [](size_t k, float& l, float& rr) {
            l = rr = k < size_t(0.6f * kFs) ? 0.5f * std::sin(0.05f * float(k)) : 0.0f;
        });
        Buf w(r.L.size());
        for (size_t i = 0; i < w.size(); ++i) w[i] = r.L[i] - dry.L[i];
        return std::make_pair(rms(w, size_t(0.3f * kFs), size_t(0.6f * kFs)), rms(w, size_t(0.8f * kFs), size_t(1.1f * kFs)));
    };
    const auto off = run(0), on = run(100);
    CHECK(on.first < 0.3 * off.first);    // ducked while the input plays
    CHECK(on.second > 0.7 * off.second);  // back once it stops
}

TEST(echo_dry_is_untouched_and_the_tail_plays_out) {
    echo::Params p = open(4, 60);
    p.tone = 50;
    p.lowcut = 40;
    p.mix = 35;
    Rig r(p);
    r.run(4, [](size_t k, float& l, float& rr) {
        l = 0.4f * std::sin(0.02f * float(k));
        rr = -0.5f * l;   // (an exact inverse would cancel in the mono sum)
    });
    // Before the first repeat (250 ms) the output is the input, bit for bit.
    for (size_t i = 0; i < r.L.size(); ++i) {
        CHECK_EQ(r.L[i], 0.4f * std::sin(0.02f * float(i)));
        CHECK_EQ(r.R[i], -0.5f * (0.4f * std::sin(0.02f * float(i))));
    }
    // The repeats then play out on a silent bus, with a tail, and end.
    r.silence(int(0.4f * kFs / kN));
    CHECK(peak(r.L, size_t(0.25f * kFs)) > 0.01f);
    CHECK(r.tail.back());
    r.silence(int(12.0f * kFs / kN));
    CHECK(!r.tail.back());
    CHECK(peak(r.L, r.L.size() - 4 * kN) == 0.0f);
}

TEST(echo_off_plays_the_repeats_out_then_clears) {
    Rig r(open(4, 80));
    r.run(int(0.3f * kFs / kN), [](size_t k, float& l, float& rr) { l = rr = k < 2000 ? 0.5f : 0.0f; });
    echo::Params p = open(4, 80);
    p.on = 0;
    r.e.setParams(p);
    const size_t at = r.L.size();
    r.silence(int(0.6f * kFs / kN));
    CHECK(peak(r.L, at) > 0.05f);   // the repeats go on after Off
    r.silence(int(15.0f * kFs / kN));
    CHECK(!r.tail.back());
    for (int16_t s : r.line) CHECK_EQ(int(s), 0);   // cleared
    // On again: nothing old comes back.
    r.e.setParams(open(4, 80));
    const size_t on = r.L.size();
    r.silence(int(0.6f * kFs / kN));
    CHECK_EQ(peak(r.L, on), 0.0f);
}

TEST(echo_no_clicks_on_settings) {
    Rig r(open(5, 60, true), 125.0f);
    float worst = 0.0f;
    for (int b = 0; b < 600; ++b) {
        if (b % 25 == 0) {
            echo::Params p = open(uint8_t((b / 25) % echo::kTimes), uint8_t(30 + (b / 25) % 60), (b / 50) % 2);
            p.mix = uint8_t((b / 75) % 2 ? 100 : 20);
            p.on = uint8_t((b / 100) % 3 != 2);
            r.e.setParams(p);
        }
        if (b == 300) r.e.setTempo(140.0f);
        r.run(1, [](size_t k, float& l, float& rr) { l = rr = 0.25f * std::sin(0.02f * float(k)); });
    }
    for (size_t i = 1; i < r.L.size(); ++i) {
        worst = std::fmax(worst, std::fabs(r.L[i] - r.L[i - 1]));
        worst = std::fmax(worst, std::fabs(r.R[i] - r.R[i - 1]));
    }
    std::printf("  ECHO settings: worst step %.4f\n", double(worst));
    CHECK(worst < 0.08f);
}

// Tone 100 <-> 99 (the loop's low-pass in and out) writes no step into the line.
TEST(echo_tone_toggle_does_not_click) {
    echo::Params p = open(4, 80);
    p.mix = 100;
    Rig r(p);
    for (int b = 0; b < 400; ++b) {
        p.tone = (b / 7) % 2 ? 100 : 99;
        r.e.setParams(p);
        r.run(1, [](size_t k, float& l, float& rr) { l = rr = 0.3f * std::sin(0.02f * float(k)); });
    }
    float worst = 0.0f;
    for (size_t i = 1; i < r.L.size(); ++i) worst = std::fmax(worst, std::fabs(r.L[i] - r.L[i - 1]));
    CHECK(worst < 0.05f);
}

TEST(echo_random_settings_stay_finite) {
    Rig r(open());
    uint32_t s = 4242;
    auto rnd = [&s](int n) {
        s = s * 1664525u + 1013904223u;
        return int((s >> 8) % uint32_t(n));
    };
    for (int b = 0; b < 1500; ++b) {
        if (b % 9 == 0) {
            echo::Params p;
            p.on = uint8_t(rnd(5) != 0);
            p.time = uint8_t(rnd(12));
            p.feedback = uint8_t(rnd(140));
            p.lowcut = uint8_t(rnd(101));
            p.tone = uint8_t(rnd(101));
            p.pingpong = uint8_t(rnd(2));
            p.duck = uint8_t(rnd(101));
            p.mix = uint8_t(rnd(101));
            r.e.setParams(p);
        }
        if (b % 40 == 0) r.e.setTempo(float(30 + rnd(270)));
        r.run(1, [&](size_t, float& l, float& rr) {
            l = float(rnd(20001) - 10000) * 1e-4f;
            rr = -l;
        });
    }
    for (size_t i = 0; i < r.L.size(); ++i) {
        CHECK(std::isfinite(r.L[i]) && std::isfinite(r.R[i]));
        CHECK(std::fabs(r.L[i]) < 3.0f && std::fabs(r.R[i]) < 3.0f);
    }
}

TEST(echo_module_line_from_the_host_and_project_data) {
    mod::ModuleHost& h = mod::ModuleHost::instance();
    h.init();
    mod::IModule* m = const_cast<mod::IModule*>(h.find("ECHO"));
    CHECK(m != nullptr);
    if (!m) return;
    CHECK(h.sdramUsed() >= echo::kLine * 2u);   // the line is the host's
    CHECK(m->masterInsert() != nullptr);
    // DIRT before ECHO (a chorus may sit between them), then TAPE (profile order).
    audio::IMasterInsert* chain[8] = {};
    const int n = h.masterInserts(chain, 8);
    int at = -1;
    for (int i = 0; i < n; ++i)
        if (chain[i] == m->masterInsert()) at = i;
    CHECK(at >= 0);
    const mod::IModule* dirt = h.find("DIRT");
    const mod::IModule* tape = h.find("TAPE");
    if (dirt) {
        int d = -1;
        for (int i = 0; i < n; ++i)
            if (chain[i] == const_cast<mod::IModule*>(dirt)->masterInsert()) d = i;
        CHECK(d >= 0 && d < at);
    }
    if (tape) CHECK(at + 1 < n && chain[at + 1] == const_cast<mod::IModule*>(tape)->masterInsert());
    mod::IProjectSection* sec = m->section();
    CHECK(sec != nullptr && sec->tag() == 0x4006);
    sec->reset();
    uint8_t b[16];
    CHECK_EQ(sec->encode(b, sizeof b), 0u);
    echo::Params p;
    p.on = 1;
    p.time = 7;
    p.feedback = 66;
    p.lowcut = 12;
    p.tone = 80;
    p.pingpong = 0;
    p.duck = 10;
    p.mix = 44;
    mod::echoSet(p);
    CHECK(sec->dirty());
    CHECK_EQ(sec->encode(b, sizeof b), 8u);
    CHECK(!sec->dirty());
    sec->reset();
    CHECK_EQ(int(mod::echoGet().on), 0);
    CHECK(sec->decode(b, 8, 1));
    const echo::Params q = mod::echoGet();
    CHECK(q.on == 1 && q.time == 7 && q.feedback == 66 && q.lowcut == 12 && q.tone == 80 && q.pingpong == 0 &&
          q.duck == 10 && q.mix == 44);
    const uint8_t s[3] = {1, 40, 250};   // short and out of range: clamped, the rest default
    CHECK(sec->decode(s, 3, 1));
    const echo::Params c = mod::echoGet();
    CHECK(c.time == echo::kTimes - 1 && c.feedback == 100 && c.lowcut == 40 && c.mix == 35);
    sec->reset();
}

TEST(echo_module_page) {
    mod::ModuleHost& h = mod::ModuleHost::instance();
    h.init();
    mod::IModule* m = const_cast<mod::IModule*>(h.find("ECHO"));
    CHECK(m != nullptr);
    if (!m) return;
    mod::IModulePage* pg = m->page();
    CHECK(pg != nullptr);
    if (!pg) return;
    m->section()->reset();
    CHECK(std::strcmp(pg->title(), "Echo") == 0);
    CHECK_EQ(pg->rows(), 8);
    const char* const labels[8] = {"Echo", "Time", "Feedback", "Low Cut", "Tone", "Ping-Pong", "Duck", "Mix"};
    mod::ModuleRow row;
    for (int i = 0; i < 8; ++i) {
        pg->row(i, row);
        CHECK(std::strcmp(row.label, labels[i]) == 0);
        CHECK(row.frac >= 0.0f && row.frac <= 1.0f);
    }
    pg->row(1, row);
    CHECK(std::strcmp(row.value, "1/8.") == 0);
    pg->adjust(1, 4);   // 1/2 ping-pong at the default 130 BPM: longer than the line
    pg->row(1, row);
    CHECK(std::strstr(row.value, "1/2") != nullptr);
    CHECK(std::strstr(row.value, "max") != nullptr);
    pg->adjust(1, 5);
    CHECK_EQ(int(mod::echoGet().time), echo::kTimes - 1);
    pg->adjust(5, -1);
    pg->row(5, row);
    CHECK(std::strcmp(row.value, "Off") == 0);
    pg->adjust(2, 100);
    CHECK_EQ(int(mod::echoGet().feedback), 100);
    pg->adjust(0, 1);
    CHECK(m->section()->dirty());
    CHECK_EQ(int(mod::echoGet().on), 1);
    m->section()->reset();
}

// Golden hash (host build, -ffp-contract=off): a tone burst through ping-pong
// then stereo, a tempo change, Tone / Low Cut / Duck moving, then Off and the
// tail. Update only for an intended change of the sound.
TEST(echo_golden_hash) {
    echo::Params p = open(4, 70, true);
    p.tone = 60;
    p.lowcut = 30;
    p.duck = 40;
    p.mix = 50;
    Rig r(p, 125.0f);
    for (int b = 0; b < 700; ++b) {
        if (b == 200) {
            p.pingpong = 0;
            p.time = 6;
            r.e.setParams(p);
        }
        if (b == 300) r.e.setTempo(131.0f);
        if (b == 400) {
            p.tone = 100;
            p.lowcut = 0;
            r.e.setParams(p);
        }
        if (b == 500) {
            p.on = 0;
            r.e.setParams(p);
        }
        r.run(1, [b](size_t k, float& l, float& rr) {
            const float s = b < 60 || (b >= 220 && b < 240) ? 0.4f * std::sin(0.031f * float(k)) : 0.0f;
            l = s;
            rr = 0.7f * s;
        });
    }
    uint64_t h = 1469598103934665603ull;
    for (const Buf* x : {&r.L, &r.R})
        for (float s : *x) {
            uint32_t w;
            std::memcpy(&w, &s, 4);
            for (int k = 0; k < 4; ++k) {
                h ^= (w >> (8 * k)) & 0xFFu;
                h *= 1099511628211ull;
            }
        }
    std::printf("  ECHO golden: %016llx\n", (unsigned long long)h);
    CHECK_EQ(h, uint64_t(0xadab5819727f4502ull));
}
