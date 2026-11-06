// TREM tremolo / auto-pan (see tremolo.h).
#include "tremolo.h"

#include "audio/audio_tables.h"
#include "audio/va_dsp.h"

namespace ot {
namespace audio {
namespace tremolo {

const char* const kModeName[kModes] = {"Level", "Pan", "Both"};
const char* const kShapeName[kShapes] = {"Sine", "Tri", "Square"};

namespace {

constexpr float kSqrt2 = 1.41421356f;

// sin(x) for 0 <= x <= pi / 2 (Taylor to x^11: < 1e-6).
float sinQ(float x) {
    const float x2 = x * x;
    return x * (1.0f - x2 * (1.0f / 6 - x2 * (1.0f / 120 - x2 * (1.0f / 5040 - x2 * (1.0f / 362880 - x2 / 39916800)))));
}

struct Gains {
    float l, r;
};

// The gains of a setting at an LFO value v (-1..1).
__attribute__((optimize("Os"))) Gains gains(Mode m, float depth, float v) {
    Gains g = {1.0f, 1.0f};
    if (m != kLevel) {   // constant power, unity in the middle
        const float p = depth * v;
        if (p != 0.0f) {
            const float th = (p + 1.0f) * (va::kPi * 0.25f);
            g.l = kSqrt2 * sinQ(va::kPi * 0.5f - th);
            g.r = kSqrt2 * sinQ(th);
        }
    }
    if (m != kPan) {
        const float lv = 1.0f - depth * 0.5f * (1.0f - v);
        g.l *= lv;
        g.r *= lv;
    }
    return g;
}

}  // namespace

float rateHz(uint8_t rate) { return 0.1f * va::fastExp2(float(rate > 100 ? 100 : rate) * (7.643856f / 100.0f)); }

float lfo(uint32_t phase, Shape shape, float hz) {
    const float f = float(phase) * (1.0f / 4294967296.0f);
    const float t = f < 0.5f ? 2.0f * f : 2.0f - 2.0f * f;   // 0 .. 1 .. 0
    if (shape == kSine) return 2.0f * t * t * (3.0f - 2.0f * t) - 1.0f;
    const float b = 2.0f * t - 1.0f;
    if (shape == kTri) return b;
    // Square: the triangle steepened to edges of 2 ms.
    const float k = 250.0f / hz;
    const float q = b * (k > 1.0f ? k : 1.0f);
    return q > 1.0f ? 1.0f : (q < -1.0f ? -1.0f : q);
}

const EffectParam& TremoloEffect::param(int i) const {
    static const EffectParam kParams[4] = {
        {"Mode", kModes - 1, kLevel, kModeName},
        {"Rate", 100, 50, nullptr},
        {"Depth", 100, 60, nullptr},
        {"Shape", kShapes - 1, kSine, kShapeName},
    };
    return kParams[i < 0 || i > 3 ? 0 : i];
}

// Two multiplies per frame; the rest is per 32 frames: built for size.
__attribute__((optimize("Os"))) bool TremoloEffect::process(void* state, void* work, const EffectSettings& s, bool on, float* l, float* r,
                            uint32_t n) {
    (void)work;
    Inst& st = *static_cast<Inst*>(state);
    const Mode mode = s.v[0] < kModes ? Mode(s.v[0]) : Mode(kModes - 1);
    const Shape shape = s.v[3] < kShapes ? Shape(s.v[3]) : Shape(kShapes - 1);
    const float depth = float(s.v[2] > 100 ? 100 : s.v[2]) * 0.01f;
    if (!st.ran) {   // fresh: the old curve is the new one, the gains start at 1
        if (!on) return false;
        st.gl = st.gr = 1.0f;
        st.depth = depth;
        st.mode = mode;
        st.shape = shape;
        st.ran = 1;
    }
    if (!on && st.wet <= 0.0f) return false;   // faded out: the bus passes untouched
    const float hz = rateHz(s.v[1]);
    const uint32_t inc = uint32_t(hz / float(kSampleRate) * 4294967296.0f);
    const float wetT = on ? 1.0f : 0.0f;
    const uint32_t steps = n / kStep;
    for (uint32_t j = 0; j < steps; ++j) {
        const float f = float(j + 1) / float(steps);
        const uint32_t ph = st.phase + inc * (kStep * (j + 1));
        Gains g = gains(mode, depth, lfo(ph, shape, hz));
        if (mode != st.mode || shape != st.shape || depth != st.depth) {
            const Gains o = gains(Mode(st.mode), st.depth, lfo(ph, Shape(st.shape), hz));
            g.l = o.l + f * (g.l - o.l);
            g.r = o.r + f * (g.r - o.r);
        }
        const float w = st.wet + f * (wetT - st.wet);
        const float tl = 1.0f + w * (g.l - 1.0f), tr = 1.0f + w * (g.r - 1.0f);
        const float dl = (tl - st.gl) * (1.0f / float(kStep)), dr = (tr - st.gr) * (1.0f / float(kStep));
        float gl = st.gl, gr = st.gr;
        float* const x = l + j * kStep;
        float* const y = r + j * kStep;
        for (uint32_t i = 0; i < kStep; ++i) {
            gl += dl;
            gr += dr;
            x[i] *= gl;
            y[i] *= gr;
        }
        st.gl = tl;
        st.gr = tr;
    }
    st.phase += inc * n;
    st.wet = wetT;
    st.depth = depth;
    st.mode = mode;
    st.shape = shape;
    return true;
}

TremoloEffect gTremoloEffect;

}  // namespace tremolo
}  // namespace audio
}  // namespace ot
