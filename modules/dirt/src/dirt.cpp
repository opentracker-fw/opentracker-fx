// DIRT master-bus drive / bitcrush (see dirt.h).
#include "dirt.h"

#include <cstring>

#include "audio/audio_tables.h"
#include "audio/va_dsp.h"

namespace ot {
namespace audio {
namespace dirt {

const char* const kTypeName[kTypes] = {"Soft", "Hard", "Fold", "Asym"};

namespace {

constexpr float kDcA = 2.0f * 3.14159265f * 10.0f / kSampleRate;   // the Asym DC block (10 Hz)
constexpr float kBias = 0.3f;                                       // Asym: tanh(x + b) - tanh(b)
constexpr float kBiasY = kBias * (27.0f + kBias * kBias) / (27.0f + 9.0f * kBias * kBias);   // va::tanhR(kBias)

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float absf(float x) { return __builtin_fabsf(x); }
// Clamp to [-1, 1): on the M4 the saturating VCVT to Q31 and back.
inline float clamp1(float x) {
#if defined(__arm__) && defined(__ARM_FP)
    __asm__("vcvt.s32.f32 %0, %0, #31" : "+t"(x));
    __asm__("vcvt.f32.s32 %0, %0, #31" : "+t"(x));
    return x;
#else
    return clampf(x, -1.0f, 1.0f);
#endif
}
// va::tanhR (exact at 0 and +-3, clamped beyond) of x = 3 u, u clamped to +-1.
inline float tanhU(float u) {
    u = clamp1(u);
    const float u2 = u * u;
    return u * (3.0f + u2) / (1.0f + 3.0f * u2);
}
inline float tanhC(float x) { return tanhU(x * (1.0f / 3.0f)); }
// Mid-rise quantiser of y in [-1, 1) to 2^bits levels: y as Q31 (the
// saturating VCVT clamps), the low 32 - bits bits cleared (floor in two's
// complement) and set to half a step.
inline float quantQ31(float y, uint32_t mask, uint32_t half) {
#if defined(__arm__) && defined(__ARM_FP)
    uint32_t k;
    __asm__("vcvt.s32.f32 %0, %0, #31" : "+t"(y));
    __asm__("vmov %0, %1" : "=r"(k) : "t"(y));
    k = (k & mask) | half;
    __asm__("vmov %0, %1" : "=t"(y) : "r"(k));
    __asm__("vcvt.f32.s32 %0, %0, #31" : "+t"(y));
    return y;
#else
    const double v = double(y) * 2147483648.0;
    const int32_t k = v >= 2147483647.0 ? 2147483647 : (v <= -2147483648.0 ? int32_t(-2147483647 - 1) : int32_t(v));
    const uint32_t u = (uint32_t(k) & mask) | half;
    return float(int32_t(u)) * (1.0f / 2147483648.0f);
#endif
}

template <int T>
inline float shapeT(float x) {
    if (T == kSoft) return tanhC(x);
    if (T == kHard) return clampf(x, -1.0f, 1.0f);
    if (T == kFold) {   // triangle fold: period 4, +-1 at x = +-1
        float t = (x + 1.0f) * 0.25f;
        const int32_t k = int32_t(t);
        t -= float(k) - (t < float(k) ? 1.0f : 0.0f);
        return 1.0f - 4.0f * absf(t - 0.5f);
    }
    // Asym: a biased tanh (even harmonics), 0 at 0.
    return tanhC(x + kBias) - kBiasY;
}
struct Common {
    float pre, dPre, wet, dWet;   // pre-gain; wet (x make-up) and dry = 1 - Mix share the ramp
    float dry, dDry;
    float bias, biasY, dcA;       // the tanh loop: Asym's bias, its output at 0, the DC block (Soft: 0)
    uint32_t quant, mask, half;   // quantiser (quantQ31), 0: none
    uint32_t tone;                // tone low-pass on
    float g;                      // its one-pole G
    uint32_t type, rate, cnt;     // hold every `rate` frames, frames left of the held value
};
struct State {
    float hold[2], lp[2], dc[2];
};

// One sample of any shaper (Asym without its DC block).
__attribute__((noinline)) float shapeAny(uint32_t type, float x) {
    switch (type) {
        case kSoft: return shapeT<kSoft>(x);
        case kHard: return shapeT<kHard>(x);
        case kFold: return shapeT<kFold>(x);
        default: return shapeT<kAsym>(x);
    }
}

// Rate 1: every frame through the shaper F (kSoft: Soft and Asym, the
// pre-gain / 3; kHard; kFold).
template <int F>
void fastBlock(float* __restrict l, float* __restrict r, const Common& c, State& st, uint32_t n) {
    float pre = c.pre, wet = c.wet, dry = c.dry;
    const float dPre = c.dPre, dWet = c.dWet, dDry = c.dDry, b = c.bias, by = c.biasY, a = c.dcA, g = c.g;
    float lp0 = st.lp[0], lp1 = st.lp[1], dc0 = st.dc[0], dc1 = st.dc[1], y0 = 0.0f, y1 = 0.0f;
    const uint32_t quant = c.quant, mask = c.mask, half = c.half, tone = c.tone;
    for (uint32_t i = 0; i < n; ++i) {
        const float x0 = l[i], x1 = r[i];
        if (F == kSoft) {
            y0 = tanhU(pre * x0 + b) - by;
            y1 = tanhU(pre * x1 + b) - by;
            dc0 += a * (y0 - dc0);
            dc1 += a * (y1 - dc1);
            y0 -= dc0;
            y1 -= dc1;
        } else if (F == kHard) {
            y0 = clamp1(pre * x0);
            y1 = clamp1(pre * x1);
        } else {   // Fold: the shared shaper (flash; not the costliest type)
            y0 = shapeAny(kFold, pre * x0);
            y1 = shapeAny(kFold, pre * x1);
        }
        if (quant) {   // mid-rise: 2^bits levels across -1..1
            y0 = quantQ31(y0, mask, half);
            y1 = quantQ31(y1, mask, half);
        }
        if (tone) {   // TPT one-pole low-pass
            const float v0 = (y0 - lp0) * g, v1 = (y1 - lp1) * g;
            y0 = v0 + lp0;
            y1 = v1 + lp1;
            lp0 = y0 + v0;
            lp1 = y1 + v1;
        } else {   // bypassed: the state follows (no step when it comes in)
            lp0 = y0;
            lp1 = y1;
        }
        l[i] = y0 * wet + x0 * dry;   // exact at Mix 0 and 100 (and the make-up 1)
        r[i] = y1 * wet + x1 * dry;
        pre += dPre;
        wet += dWet;
        dry += dDry;
    }
    st.lp[0] = va::flush(lp0);
    st.lp[1] = va::flush(lp1);
    st.dc[0] = va::flush(dc0);
    st.dc[1] = va::flush(dc1);
}

// Rate > 1: the shaper only at the hold points; returns the hold counter.
uint32_t holdBlock(float* __restrict l, float* __restrict r, const Common& c, State& st, uint32_t n) {
    float pre = c.pre, wet = c.wet, dry = c.dry;
    const float dPre = c.dPre, dWet = c.dWet, dDry = c.dDry, a = c.dcA, g = c.g;
    float h[2] = {st.hold[0], st.hold[1]}, lp0 = st.lp[0], lp1 = st.lp[1];
    uint32_t cnt = c.cnt;
    for (uint32_t i = 0; i < n; ++i) {
        const float x0 = l[i], x1 = r[i];
        if (cnt == 0) {
            const float x[2] = {x0, x1};
            for (int k = 0; k < 2; ++k) {
                float y = shapeAny(c.type, pre * x[k]);
                st.dc[k] += a * (y - st.dc[k]);   // Asym (a = 0 otherwise)
                y -= st.dc[k];
                h[k] = c.quant ? quantQ31(y, c.mask, c.half) : y;
            }
            cnt = c.rate;
        }
        --cnt;
        float y0 = h[0], y1 = h[1];
        if (c.tone) {
            const float v0 = (y0 - lp0) * g, v1 = (y1 - lp1) * g;
            y0 = v0 + lp0;
            y1 = v1 + lp1;
            lp0 = y0 + v0;
            lp1 = y1 + v1;
        } else {
            lp0 = y0;
            lp1 = y1;
        }
        l[i] = y0 * wet + x0 * dry;
        r[i] = y1 * wet + x1 * dry;
        pre += dPre;
        wet += dWet;
        dry += dDry;
    }
    st.hold[0] = h[0];
    st.hold[1] = h[1];
    st.lp[0] = va::flush(lp0);
    st.lp[1] = va::flush(lp1);
    st.dc[0] = va::flush(st.dc[0]);
    st.dc[1] = va::flush(st.dc[1]);
    return cnt;
}

// One block of a type: its fields of c (the pre-gain / 3 for the tanh loop,
// Asym's bias and DC block; at Rate > 1 the DC corner x the rate, the block
// updating once per hold), then the loop. Returns the hold counter.
uint32_t runType(uint8_t type, float* l, float* r, Common c, State& st, uint32_t n) {
    const bool hold = c.rate > 1, tanh = type == kSoft || type == kAsym;
    if (!hold && tanh) {
        c.pre *= 1.0f / 3.0f;
        c.dPre *= 1.0f / 3.0f;
    }
    c.type = type;
    c.bias = type == kAsym ? kBias * (1.0f / 3.0f) : 0.0f;
    c.biasY = type == kAsym ? kBiasY : 0.0f;
    c.dcA = type == kAsym ? kDcA * float(c.rate) : 0.0f;
    if (hold) return holdBlock(l, r, c, st, n);
    if (tanh) fastBlock<kSoft>(l, r, c, st, n);
    else if (type == kHard) fastBlock<kHard>(l, r, c, st, n);
    else fastBlock<kFold>(l, r, c, st, n);
    return 0;
}

}  // namespace

float shape(uint8_t type, float x) { return shapeAny(type, x); }

float toneHz(uint8_t tone) { return 200.0f * va::fastExp2(6.64385619f * float(tone > 100 ? 100 : tone) * 0.01f); }

void Dirt::setParams(const Params& in) {
    // Every field into its range.
    Params c = in;
    const Params& p = in;
    c.on = p.on ? 1 : 0;
    c.type = p.type < kTypes ? p.type : uint8_t(kTypes - 1);
    c.drive = p.drive > 100 ? 100 : p.drive;
    c.bits = p.bits < 4 ? 4 : (p.bits > 16 ? 16 : p.bits);
    c.rate = p.rate < 1 ? 1 : (p.rate > 32 ? 32 : p.rate);
    c.tone = p.tone > 100 ? 100 : p.tone;
    c.mix = p.mix > 100 ? 100 : p.mix;
    const uint8_t next = uint8_t(cur_ ^ 1u);
    pub_[next] = c;
    __atomic_store_n(&cur_, next, __ATOMIC_RELEASE);
}

void Dirt::reset() {
    wet_ = 0.0f;
    pre_ = post_ = 1.0f;
    for (int c = 0; c < 2; ++c) hold_[c] = lp_[c] = dc_[c] = 0.0f;
    cnt_ = 0;
    type_ = 0xFF;
}

bool Dirt::process(float* l, float* r, uint32_t n) {
    const Params p = pub_[__atomic_load_n(&cur_, __ATOMIC_ACQUIRE)];
    const float wetEnd = p.on ? float(p.mix) * 0.01f : 0.0f;
    if (wetEnd <= 0.0f && wet_ <= 0.0f) return false;   // Off (or Mix 0) and faded out: untouched
    if (!n) return false;
    const float d = float(p.drive) * 0.01f;
    const float preEnd = 1.0f + 15.0f * d * d, postEnd = 1.0f / (1.0f + 0.5f * d);
    const float inv = 1.0f / float(n);
    Common c;
    c.pre = pre_;
    c.dPre = (preEnd - pre_) * inv;
    // The make-up gain rides on the wet ramp: wet x post (the shaper's output is
    // quantised before it, at its own full scale).
    const float w0 = wet_ * post_, w1 = wetEnd * postEnd;
    c.wet = w0;
    c.dWet = (w1 - w0) * inv;
    c.dry = 1.0f - wet_;
    c.dDry = (wet_ - wetEnd) * inv;
    c.quant = p.bits < 16;
    c.mask = ~((1u << (32 - p.bits)) - 1u);
    c.half = 1u << (31 - p.bits);
    c.tone = p.tone < 100;
    if (!c.tone) {
        c.g = 0.0f;
    } else {
        const float g = va::tanPade(va::kPi * toneHz(p.tone) * va::kInvFs);
        c.g = g / (1.0f + g);
    }
    c.rate = p.rate;
    c.cnt = cnt_ > p.rate ? p.rate : cnt_;
    State st{{hold_[0], hold_[1]}, {lp_[0], lp_[1]}, {dc_[0], dc_[1]}};
    const State keep = st;   // the old type's (a Type change crossfades from it)
    if (p.type != kAsym) st.dc[0] = st.dc[1] = 0.0f;   // the DC block is Asym's alone
    const uint8_t prev = type_;
    type_ = p.type;
    if (prev < kTypes && prev != p.type) {
        // A Type change: the old type on a copy of the block (and of the
        // state), the new one in place, crossfaded over the block.
        float ol[kBlock], orr[kBlock];
        const uint32_t m = n < kBlock ? n : kBlock;
        std::memcpy(ol, l, m * sizeof(float));
        std::memcpy(orr, r, m * sizeof(float));
        State so = keep;
        runType(prev, ol, orr, c, so, m);
        cnt_ = runType(p.type, l, r, c, st, m);
        const float df = 1.0f / float(m);
        float f = 0.0f;
        for (uint32_t i = 0; i < m; ++i) {
            f += df;
            l[i] = ol[i] + f * (l[i] - ol[i]);
            r[i] = orr[i] + f * (r[i] - orr[i]);
        }
    } else {
        cnt_ = runType(p.type, l, r, c, st, n);
    }
    for (int k = 0; k < 2; ++k) {
        hold_[k] = st.hold[k];
        lp_[k] = st.lp[k];
        dc_[k] = st.dc[k];
    }
    pre_ = preEnd;
    post_ = postEnd;
    wet_ = wetEnd;
    if (wetEnd <= 0.0f) reset();   // faded out: the next On starts clean
    return false;                  // no tail
}

}  // namespace dirt
}  // namespace audio
}  // namespace ot
