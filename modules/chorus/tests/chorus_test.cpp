// CHORUS master-bus effect (opentracker-fx modules/chorus): Off passes the
// bus through bit for bit; On delays and modulates a copy (the output
// differs from the dry signal, more with Depth); Width 0 keeps a mono input
// mono, Width 100 makes the sides differ; levels stay near unity; the project
// data round-trips; the page rows adjust.
#include "test.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "audio/synth_engine.h"
#include "chorus.h"
#include "modules/module.h"
#include "modules/module_ui.h"

using namespace ot;
using namespace ot::audio;

namespace ot {
namespace mod {
void chorusSet(const audio::chorus::Params& p);
audio::chorus::Params chorusGet();
}  // namespace mod
}  // namespace ot

namespace {
constexpr uint32_t kN = kBlock;

void tone(std::vector<float>& l, std::vector<float>& r, int blocks, float hz = 440.0f) {
    l.resize(size_t(blocks) * kN);
    r.resize(l.size());
    for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 0.4f * std::sin(6.2831853f * hz * float(i) / 44117.6f);
}
void run(chorus::Chorus& c, std::vector<float>& l, std::vector<float>& r) {
    for (size_t o = 0; o < l.size(); o += kN) c.process(l.data() + o, r.data() + o, kN);
}
double rms(const std::vector<float>& x, size_t from) {
    double s = 0;
    for (size_t i = from; i < x.size(); ++i) s += double(x[i]) * x[i];
    return std::sqrt(s / double(x.size() - from));
}
double diff(const std::vector<float>& a, const std::vector<float>& b, size_t from) {
    double s = 0;
    for (size_t i = from; i < a.size(); ++i) s += std::fabs(a[i] - b[i]);
    return s / double(a.size() - from);
}
}  // namespace

TEST(chorus_off_is_bit_exact) {
    chorus::Chorus c;
    std::vector<float> l, r;
    tone(l, r, 10);
    const std::vector<float> l0 = l;
    run(c, l, r);
    CHECK(l == l0);
}

TEST(chorus_on_modulates_and_keeps_the_level) {
    std::vector<float> dryL, dryR;
    tone(dryL, dryR, 400);
    std::vector<float> l = dryL, r = dryR;
    chorus::Chorus c;
    chorus::Params p;
    p.on = 1;
    c.setParams(p);
    run(c, l, r);
    const size_t from = 20 * kN;
    CHECK(diff(l, dryL, from) > 0.02);
    const double ratio = rms(l, from) / rms(dryL, from);
    CHECK(ratio > 0.7 && ratio < 1.25);
    for (float v : l) CHECK(std::isfinite(v));
    // More depth, more change.
    std::vector<float> l2 = dryL, r2 = dryR;
    chorus::Chorus c2;
    p.depth = 100;
    c2.setParams(p);
    run(c2, l2, r2);
    std::vector<float> l3 = dryL, r3 = dryR;
    chorus::Chorus c3;
    p.depth = 5;
    c3.setParams(p);
    run(c3, l3, r3);
    CHECK(diff(l2, dryL, from) > diff(l3, dryL, from));
}

TEST(chorus_width_spreads_the_sides) {
    std::vector<float> l, r;
    tone(l, r, 300);
    chorus::Chorus c;
    chorus::Params p;
    p.on = 1;
    p.width = 0;
    c.setParams(p);
    std::vector<float> l0 = l, r0 = r;
    run(c, l0, r0);
    CHECK(diff(l0, r0, 0) < 1e-6);   // mono in, Width 0: mono out
    chorus::Chorus w;
    p.width = 100;
    w.setParams(p);
    std::vector<float> l1 = l, r1 = r;
    run(w, l1, r1);
    CHECK(diff(l1, r1, 20 * kN) > 0.01);
}

TEST(chorus_off_after_on_returns_to_dry) {
    chorus::Chorus c;
    chorus::Params p;
    p.on = 1;
    c.setParams(p);
    std::vector<float> l, r;
    tone(l, r, 20);
    run(c, l, r);
    p.on = 0;
    c.setParams(p);
    std::vector<float> l2, r2;
    tone(l2, r2, 3);
    const std::vector<float> dry = l2;
    c.process(l2.data(), r2.data(), kN);                 // fades out
    CHECK(!c.process(l2.data() + kN, r2.data() + kN, kN));
    CHECK(std::memcmp(l2.data() + kN, dry.data() + kN, kN * sizeof(float)) == 0);
}

TEST(chorus_module_data_and_page) {
    mod::ModuleHost& h = mod::ModuleHost::instance();
    h.init();
    mod::IModule* m = const_cast<mod::IModule*>(h.find("CHOR"));
    CHECK(m != nullptr);
    if (!m) return;
    mod::IProjectSection* sec = m->section();
    mod::IModulePage* pg = m->page();
    CHECK(sec && sec->tag() == 0x4002 && pg && pg->rows() == 5);
    sec->reset();
    uint8_t b[16];
    CHECK_EQ(sec->encode(b, sizeof b), 0u);
    pg->adjust(0, 1);    // On
    pg->adjust(2, 30);   // Depth 50 -> 80
    CHECK(sec->dirty());
    CHECK(mod::chorusGet().on == 1 && mod::chorusGet().depth == 80);
    mod::ModuleRow row{};
    pg->row(1, row);
    CHECK(std::strstr(row.value, "Hz") != nullptr);
    CHECK_EQ(sec->encode(b, sizeof b), 8u);
    sec->reset();
    CHECK(sec->decode(b, 8, 1));
    CHECK(mod::chorusGet().on == 1 && mod::chorusGet().depth == 80);
    sec->reset();
}
