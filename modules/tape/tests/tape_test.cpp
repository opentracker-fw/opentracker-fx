// TAPE master-bus effect (opentracker-fx modules/tape): Off passes the bus
// through bit for bit; drive saturates without clipping; wow / flutter move
// the signal in time; the project data round-trips; the module is the
// image's master insert.
#include "test.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "audio/synth_engine.h"
#include "modules/module.h"
#include "tape.h"

using namespace ot;
using namespace ot::audio;

namespace ot {
namespace mod {
void tapeSet(const audio::tape::Params& p);
audio::tape::Params tapeGet();
}  // namespace mod
}  // namespace ot

namespace {
constexpr uint32_t kN = kBlock;

void tone(std::vector<float>& l, std::vector<float>& r, float amp, int blocks, float hz = 220.0f) {
    l.resize(size_t(blocks) * kN);
    r.resize(l.size());
    for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = amp * std::sin(6.2831853f * hz * float(i) / 44117.6f);
}
void run(tape::Tape& t, std::vector<float>& l, std::vector<float>& r) {
    for (size_t o = 0; o < l.size(); o += kN) t.process(l.data() + o, r.data() + o, kN);
}
float peak(const std::vector<float>& x) {
    float p = 0.0f;
    for (float v : x) p = std::fmax(p, std::fabs(v));
    return p;
}
}  // namespace

TEST(tape_off_is_bit_exact) {
    tape::Tape t;
    std::vector<float> l, r;
    tone(l, r, 0.5f, 10);
    const std::vector<float> l0 = l;
    run(t, l, r);
    CHECK(l == l0);
    float a[kN] = {}, b[kN] = {};
    CHECK(!t.process(a, b, kN));
}

TEST(tape_drive_saturates_without_clipping) {
    tape::Tape t;
    tape::Params p;
    p.on = 1;
    p.drive = 100;
    p.wow = p.flutter = 0;
    t.setParams(p);
    std::vector<float> l, r;
    tone(l, r, 0.95f, 40);
    run(t, l, r);
    CHECK(peak(l) < 1.0f);
    CHECK(peak(l) > 0.3f);
    // A clean tone gains odd harmonics: the waveform is flatter than a sine.
    double s2 = 0, s4 = 0;
    for (size_t i = 20 * kN; i < l.size(); ++i) {
        s2 += double(l[i]) * l[i];
        s4 += double(l[i]) * l[i] * l[i] * l[i];
    }
    const double crest = s4 / (s2 * s2) * double(l.size() - 20 * kN);
    CHECK(crest < 1.45);   // a sine has 1.5, a square 1.0
}

TEST(tape_wow_and_flutter_move_the_signal) {
    std::vector<float> a, ar, b, br;
    tone(a, ar, 0.3f, 400, 1000.0f);
    b = a;
    br = ar;
    tape::Tape x, y;
    tape::Params p;
    p.on = 1;
    p.drive = 0;
    p.wow = 0;
    p.flutter = 0;
    x.setParams(p);
    p.wow = 100;
    p.flutter = 100;
    y.setParams(p);
    run(x, a, ar);
    run(y, b, br);
    double d = 0;
    for (size_t i = 100 * kN; i < a.size(); ++i) d += std::fabs(a[i] - b[i]);
    CHECK(d / double(a.size() - 100 * kN) > 0.01);
    for (float v : b) CHECK(std::isfinite(v));
}

TEST(tape_mix_fades_and_off_returns_to_dry) {
    tape::Tape t;
    tape::Params p;
    p.on = 1;
    p.drive = 80;
    t.setParams(p);
    std::vector<float> l, r;
    tone(l, r, 0.5f, 20);
    run(t, l, r);
    p.on = 0;
    t.setParams(p);
    std::vector<float> l2, r2;
    tone(l2, r2, 0.5f, 3);
    const std::vector<float> dry = l2;
    t.process(l2.data(), r2.data(), kN);                    // fades out over this block
    t.process(l2.data() + kN, r2.data() + kN, kN);          // dry again
    CHECK(std::memcmp(l2.data() + kN, dry.data() + kN, kN * sizeof(float)) == 0);
}

TEST(tape_module_project_data_round_trip) {
    mod::ModuleHost& h = mod::ModuleHost::instance();
    h.init();
    const mod::IModule* m = h.find("TAPE");
    CHECK(m != nullptr);
    if (!m) return;
    mod::IProjectSection* sec = const_cast<mod::IModule*>(m)->section();
    CHECK(sec != nullptr && sec->tag() == 0x4001);
    CHECK(h.masterInsert() != nullptr);
    sec->reset();
    uint8_t b[16];
    CHECK_EQ(sec->encode(b, sizeof b), 0u);   // defaults, Off: nothing saved
    tape::Params p;
    p.on = 1;
    p.drive = 64;
    p.wow = 5;
    p.flutter = 7;
    p.mix = 90;
    mod::tapeSet(p);
    CHECK(sec->dirty());
    CHECK_EQ(sec->encode(b, sizeof b), 8u);
    CHECK(!sec->dirty());
    sec->reset();
    CHECK_EQ(int(mod::tapeGet().on), 0);
    CHECK(sec->decode(b, 8, 1));
    const tape::Params q = mod::tapeGet();
    CHECK(q.on == 1 && q.drive == 64 && q.wow == 5 && q.flutter == 7 && q.mix == 90);
    // A short payload (older writer) keeps the defaults for the rest; values clamp.
    const uint8_t s[2] = {1, 250};
    CHECK(sec->decode(s, 2, 1));
    CHECK(mod::tapeGet().drive == 100 && mod::tapeGet().mix == 100);
    sec->reset();
}
