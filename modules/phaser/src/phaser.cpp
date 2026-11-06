// PHSR phaser (see phaser.h).
#include "phaser.h"

#include <cmath>
#include <cstring>

#include "audio/audio_tables.h"
#include "audio/va_dsp.h"

namespace ot {
namespace audio {
namespace phaser {

const char* const kModeName[kModes] = {"Classic", "80s"};

namespace {

struct Voice {
    int stages;
    float lo, span;   // corner range: lo .. lo * 2^span (Hz)
    float fbMax;
};
constexpr Voice kVoice[kModes] = {{4, 140.0f, 4.1000f, 0.6f},    // 140 Hz .. 2.4 kHz
                                  {8, 90.0f, 5.5443f, 0.8f}};    // 90 Hz .. 4.2 kHz

// The LFO at a phase as the sweep position 0..1: a triangle, shaped per mode
// (Classic: t^1.5, lingering low; 80s: a smoothstep, close to a raised cosine).
float sweep(uint32_t ph, Mode m) {
    const float f = float(ph) * (1.0f / 4294967296.0f);
    const float t = f < 0.5f ? 2.0f * f : 2.0f - 2.0f * f;
    return m == kClassic ? t * std::sqrt(t) : t * t * (3.0f - 2.0f * t);
}

float flush(float x) { return std::fabs(x) < 1e-15f ? 0.0f : x; }

// One all-pass stage: x its input, p0 / p1 the last input / output (one
// multiply: a fused multiply-add on the M4 / M7).
inline float stage(float x, float a, float& p0, float& p1) {
    const float y = __builtin_fmaf(a, x - p1, p0);
    p0 = x;
    return y;
}

// kStep frames of one channel with steady settings (the hot path); the stage
// memory stays in registers. Upper: the 8-stage output, else the 4-stage one.
template <bool Upper>
__attribute__((noinline)) void chain(float* x, float* pm, float& last, float a, float da, float g, float fb) {
    float p0 = pm[0], p1 = pm[1], p2 = pm[2], p3 = pm[3], p4 = pm[4];
    float p5 = 0, p6 = 0, p7 = 0, p8 = 0;
    if (Upper) p5 = pm[5], p6 = pm[6], p7 = pm[7], p8 = pm[8];
    float y = last;
    const float dry = 1.0f - g, wet = g * (1.0f - 0.4f * fb);
    for (uint32_t i = 0; i < kStep; ++i) {
        a += da;
        const float in = x[i];
        float v = stage(__builtin_fmaf(fb, y, in), a, p0, p1);
        v = stage(v, a, p1, p2);
        v = stage(v, a, p2, p3);
        v = stage(v, a, p3, p4);
        if (Upper) {
            v = stage(v, a, p4, p5);
            v = stage(v, a, p5, p6);
            v = stage(v, a, p6, p7);
            v = stage(v, a, p7, p8);
            p8 = v;
        } else {
            p4 = v;
        }
        y = v;
        x[i] = __builtin_fmaf(wet, v, dry * in);
    }
    last = y;
    pm[0] = p0, pm[1] = p1, pm[2] = p2, pm[3] = p3, pm[4] = p4;
    if (Upper) pm[5] = p5, pm[6] = p6, pm[7] = p7, pm[8] = p8;
}

// kStep frames while settings ramp (one block after a change: small, not fast):
// the wet, Feedback and the 4- / 8-stage crossfade move per frame.
struct Ramp {
    float a, da, g, dg, fb, dfb, tap, dtap;
};
__attribute__((noinline, optimize("Os"))) void chainMoving(float* x, float* pm, float& last, Ramp r,
                                                           bool upper) {
    const int n = upper ? kStages : 4;
    float y = last;
    for (uint32_t i = 0; i < kStep; ++i) {
        r.a += r.da;
        r.g += r.dg;
        r.fb += r.dfb;
        r.tap += r.dtap;
        const float in = x[i];
        float v = in + r.fb * y, v4 = 0.0f;
        for (int k = 0; k < n; ++k) {
            v = stage(v, r.a, pm[k], pm[k + 1]);
            if (k == 3) v4 = v;
        }
        pm[n] = v;
        if (upper) v = v4 + r.tap * (v - v4);
        y = v;
        x[i] = in + r.g * (v * (1.0f - 0.4f * r.fb) - in);
    }
    last = y;
}

}  // namespace

__attribute__((noinline)) float coef(float hz) {
    const float lim = 0.45f * float(kSampleRate);
    const float t = va::tanPade(va::kPi * va::kInvFs * (hz < lim ? hz : lim));
    return (t - 1.0f) / (t + 1.0f);
}

float rateHz(uint8_t rate) { return 0.05f * va::fastExp2(float(rate > 100 ? 100 : rate) * (7.321928f / 100.0f)); }

const EffectParam& PhaserEffect::param(int i) const {
    static const EffectParam kParams[6] = {
        {"Mode", kModes - 1, kClassic, kModeName}, {"Rate", 100, 40, nullptr},   {"Depth", 100, 80, nullptr},
        {"Feedback", 100, 30, nullptr},            {"Spread", 100, 25, nullptr}, {"Mix", 100, 50, nullptr},
    };
    return kParams[i < 0 || i > 5 ? 0 : i];
}

uint32_t PhaserEffect::tailFrames() const { return 2 * kBlock; }

// The per-block part is small next to the stage loops: built for size.
__attribute__((optimize("Os"))) bool PhaserEffect::process(void* state, void* work, const EffectSettings& s, bool on, float* l, float* r,
                           uint32_t n) {
    Inst& st = *static_cast<Inst*>(state);
    if (!work || n == 0) return false;
    Work& w = *static_cast<Work*>(work);
    const Mode mode = s.v[0] < kModes ? Mode(s.v[0]) : Mode(kModes - 1);
    const Voice& vc = kVoice[mode];
    auto pct = [&](int i) { return float(s.v[i] > 100 ? 100 : s.v[i]) * 0.01f; };
    const float depth = pct(2), fbT = pct(3) * vc.fbMax, spread = pct(4), gT = on ? pct(5) : 0.0f;
    const float tapT = vc.stages == kStages ? 1.0f : 0.0f;
    const bool fresh = st.mode1 == 0;
    if (fresh) {   // start at the settings, the wet fading in
        if (!on) return false;
        st.depth = depth;
        st.spread = spread;
        st.fb = fbT;
        st.tap = tapT;
        st.gain = 0.0f;
        std::memset(&w, 0, sizeof(Work));
    }
    if (!on && st.gain <= 0.0f) return false;   // faded out: the bus passes untouched
    // Stages 5..8 run while the 8-stage output is heard; restarting, they start empty.
    const bool upper = tapT > 0.0f || st.tap > 0.0f;
    if (upper && !st.upper)
        for (int c = 0; c < 2; ++c)
            for (int k = 5; k <= kStages; ++k) w.p[c][k] = w.p[c][4];

    const float logSpan = vc.span;
    const uint32_t inc = uint32_t(rateHz(s.v[1]) / float(kSampleRate) * 4294967296.0f);
    const uint32_t steps = n / kStep;
    const float inv = 1.0f / float(n);
    const float dg = (gT - st.gain) * inv, dfb = (fbT - st.fb) * inv, dtap = (tapT - st.tap) * inv;
    float g = st.gain, fb = st.fb, tap = st.tap;
    // Steady (no ramp, the 4- or 8-stage output alone): the cheaper loop.
    const bool moving = dg != 0.0f || dfb != 0.0f || dtap != 0.0f || (tap != 0.0f && tap != 1.0f);
    float* const io[2] = {l, r};
    for (uint32_t j = 0; j < steps; ++j) {
        const float f = float(j + 1) / float(steps);
        const float d = st.depth + (depth - st.depth) * f;
        const float sp = st.spread + (spread - st.spread) * f;
        const uint32_t ph = st.phase + inc * (kStep * (j + 1));
        const uint32_t off = uint32_t(sp * 2147483648.0f);
        float a1[2];
        a1[0] = coef(vc.lo * va::fastExp2(d * sweep(ph, mode) * logSpan));
        a1[1] = off ? coef(vc.lo * va::fastExp2(d * sweep(ph + off, mode) * logSpan)) : a1[0];
        if (fresh && j == 0) st.a[0] = a1[0], st.a[1] = a1[1];
        const uint32_t i0 = j * kStep;
        for (int c = 0; c < 2; ++c) {
            float* const x = io[c] + i0;
            const float da = (a1[c] - st.a[c]) * (1.0f / float(kStep));
            if (moving) chainMoving(x, w.p[c], w.last[c], Ramp{st.a[c], da, g, dg, fb, dfb, tap, dtap}, upper);
            else if (upper) chain<true>(x, w.p[c], w.last[c], st.a[c], da, g, fb);
            else chain<false>(x, w.p[c], w.last[c], st.a[c], da, g, fb);
            st.a[c] = a1[c];
        }
        g += dg * float(kStep);
        fb += dfb * float(kStep);
        tap += dtap * float(kStep);
    }
    for (int c = 0; c < 2; ++c) {
        w.last[c] = flush(w.last[c]);
        for (int k = 0; k <= kStages; ++k) w.p[c][k] = flush(w.p[c][k]);
    }
    st.phase += inc * n;
    st.depth = depth;
    st.spread = spread;
    st.gain = gT;
    st.fb = fbT;
    st.tap = tapT;
    st.upper = upper ? 1 : 0;
    st.mode1 = uint8_t(mode + 1);
    return true;
}

PhaserEffect gPhaserEffect;

}  // namespace phaser
}  // namespace audio
}  // namespace ot
