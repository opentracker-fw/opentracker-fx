// CHORUS master-bus effect (see chorus.h).
#include "chorus.h"

#include "audio/audio_tables.h"

namespace ot {
namespace audio {
namespace chorus {

namespace {

constexpr float kBase = 0.007f * kSampleRate;        // centre delay: 7 ms (~309 frames)
constexpr float kDepthMax = 0.003f * kSampleRate;    // swing at Depth 100: 3 ms (~132 frames)
static_assert(kBase + kDepthMax + 2.0f < float(Chorus::kLine), "chorus line too short");

inline float absf(float x) { return __builtin_fabsf(x); }
// sin(2 pi x), x in [0, 1): refined parabola.
inline float sinCyc(float x) {
    const float t = 2.0f * x - 1.0f;
    const float y = 4.0f * t * (1.0f - absf(t));
    return -(y + 0.225f * (y * absf(y) - y));
}
inline float wrap1(float x) { return x - float(int(x)); }   // x >= 0
inline int16_t toQ15(float x) {
    x = x > 0.99997f ? 0.99997f : (x < -1.0f ? -1.0f : x);
    return int16_t(x * 32768.0f);
}

}  // namespace

void Chorus::setParams(const Params& p) {
    const uint8_t next = uint8_t(cur_ ^ 1u);
    pub_[next] = p;
    __atomic_store_n(&cur_, next, __ATOMIC_RELEASE);
}

void Chorus::reset() {
    for (int16_t& v : line_) v = 0;
    w_ = 0;
    ph_ = wet_ = 0.0f;
}

bool Chorus::process(float* l, float* r, uint32_t n) {
    const Params p = pub_[__atomic_load_n(&cur_, __ATOMIC_ACQUIRE)];
    const float wetEnd = p.on ? float(p.mix) * 0.01f : 0.0f;
    if (wetEnd <= 0.0f && wet_ <= 0.0f) return false;   // Off and faded out: untouched
    const float hz = 0.1f + 0.049f * float(p.rate);
    const float depth = kDepthMax * 0.01f * float(p.depth);
    const float off = 0.005f * float(p.width);          // right-tap phase offset, cycles
    // The LFO moves slowly: both tap delays at the block ends, ramped.
    const float ph1 = wrap1(ph_ + hz * float(n) / kSampleRate);
    const float dl0 = kBase + depth * sinCyc(ph_), dl1 = kBase + depth * sinCyc(ph1);
    const float dr0 = kBase + depth * sinCyc(wrap1(ph_ + off)), dr1 = kBase + depth * sinCyc(wrap1(ph1 + off));
    const float inv = 1.0f / float(n);
    float dl = dl0, dr = dr0;
    const float ddl = (dl1 - dl0) * inv, ddr = (dr1 - dr0) * inv;
    float wet = wet_;
    const float dw = (wetEnd - wet_) * inv;
    constexpr float kQ = 1.0f / 32768.0f;
    constexpr uint32_t kMask = uint32_t(kLine - 1);
    uint32_t w = w_;
    for (uint32_t i = 0; i < n; ++i) {
        line_[w] = toQ15(0.5f * (l[i] + r[i]));
        float pl = float(w) - dl, pr = float(w) - dr;
        if (pl < 0.0f) pl += float(kLine);
        if (pr < 0.0f) pr += float(kLine);
        const uint32_t il = uint32_t(pl), ir = uint32_t(pr);
        const float fl = pl - float(il), fr = pr - float(ir);
        const float a0 = float(line_[il & kMask]), a1 = float(line_[(il + 1u) & kMask]);
        const float b0 = float(line_[ir & kMask]), b1 = float(line_[(ir + 1u) & kMask]);
        const float tl = (a0 + fl * (a1 - a0)) * kQ, tr = (b0 + fr * (b1 - b0)) * kQ;
        // Dry ducks a little as the wet tap comes in (the sum stays near unity).
        const float dry = 1.0f - 0.35f * wet;
        l[i] = dry * l[i] + 0.7f * wet * tl;
        r[i] = dry * r[i] + 0.7f * wet * tr;
        w = (w + 1u) & kMask;
        dl += ddl;
        dr += ddr;
        wet += dw;
    }
    w_ = w;
    ph_ = ph1;
    wet_ = wetEnd;
    if (wetEnd <= 0.0f) {
        reset();
        return false;
    }
    return true;
}

}  // namespace chorus
}  // namespace audio
}  // namespace ot
