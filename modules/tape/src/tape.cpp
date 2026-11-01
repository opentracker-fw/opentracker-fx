// TAPE master-bus effect (see tape.h).
#include "tape.h"

#include <cstring>

#include "audio/audio_tables.h"

namespace ot {
namespace audio {
namespace tape {

namespace {

constexpr float kInvFs = 1.0f / kSampleRate;
constexpr float kWowHz = 0.6f, kFlutterHz = 7.0f;
constexpr float kWowMax = 88.0f;      // frames (~2 ms) at 100
constexpr float kFlutterMax = 13.0f;  // frames (~0.3 ms) at 100
constexpr float kBase = 120.0f;       // centre delay (frames): room for both depths

inline float absf(float x) { return __builtin_fabsf(x); }
// sin(2 pi x), x in [0, 1): parabola with one refinement (error < 0.1 %).
inline float sinCyc(float x) {
    const float t = 2.0f * x - 1.0f;
    const float y = 4.0f * t * (1.0f - absf(t));
    return -(y + 0.225f * (y * absf(y) - y));
}
// Soft clip: unity slope at 0, flat at +-1.5 -> +-1.
inline float clip(float x) {
    x = 0.5f * (absf(x + 1.5f) - absf(x - 1.5f));
    return x - (4.0f / 27.0f) * x * x * x;
}

}  // namespace

void Tape::setParams(const Params& p) {
    const uint8_t next = uint8_t(cur_ ^ 1u);
    pub_[next] = p;
    __atomic_store_n(&cur_, next, __ATOMIC_RELEASE);
}

void Tape::reset() {
    for (Chan& c : ch_) {
        c.lp = 0.0f;
        for (int16_t& v : c.line) v = 0;
    }
    w_ = 0;
    phW_ = phF_ = wet_ = 0.0f;
    tail_ = 0;
}

bool Tape::process(float* l, float* r, uint32_t n) {
    const Params p = pub_[__atomic_load_n(&cur_, __ATOMIC_ACQUIRE)];
    const float wetEnd = p.on ? float(p.mix) * 0.01f : 0.0f;
    if (wetEnd <= 0.0f && wet_ <= 0.0f) return false;   // Off and faded out: untouched
    const float drive = 1.0f + 0.05f * float(p.drive);      // x1 .. x6
    const float makeup = 1.0f / (1.0f + 0.012f * float(p.drive));
    const float dW = kWowMax * 0.01f * float(p.wow), dF = kFlutterMax * 0.01f * float(p.flutter);
    const float incW = kWowHz * kInvFs, incF = kFlutterHz * kInvFs;
    constexpr float kLp = 0.55f;   // one-pole high cut (~8 kHz)
    float wet = wet_;
    const float dWet = (wetEnd - wet_) / float(n);
    float* io[2] = {l, r};
    uint32_t w = w_;
    // Wow and flutter move slowly (7 Hz = ~50 blocks per cycle): the delay is
    // evaluated at both block ends and ramped across the block.
    float phW = phW_ + incW * float(n), phF = phF_ + incF * float(n);
    phW -= float(int(phW));
    phF -= float(int(phF));
    const float d0 = kBase + dW * sinCyc(phW_) + dF * sinCyc(phF_);
    const float d1 = kBase + dW * sinCyc(phW) + dF * sinCyc(phF);
    float d = d0;
    const float dd = (d1 - d0) / float(n);
    for (uint32_t i = 0; i < n; ++i) {
        float rp = float(w) - d;
        d += dd;
        if (rp < 0.0f) rp += float(kLine);
        const uint32_t i0 = uint32_t(rp);
        const float fr = rp - float(i0);
        const uint32_t i1 = (i0 + 1u) & uint32_t(kLine - 1);
        for (int c = 0; c < 2; ++c) {
            Chan& ch = ch_[c];
            const float x = io[c][i];
            ch.lp += kLp * (clip(drive * x) * makeup - ch.lp);
            // int16 line: |lp| <= 1 (the clip's ceiling x a makeup <= 1), no clamp needed.
            ch.line[w] = int16_t(ch.lp * 32767.0f);
            const float y0 = float(ch.line[i0 & uint32_t(kLine - 1)]);
            const float y = (y0 + fr * (float(ch.line[i1]) - y0)) * (1.0f / 32767.0f);
            io[c][i] = x + wet * (y - x);
        }
        w = (w + 1u) & uint32_t(kLine - 1);
        wet += dWet;
    }
    w_ = w;
    phW_ = phW;
    phF_ = phF;
    wet_ = wetEnd;
    // After Off the line still holds sound for one more pass: report a tail
    // while fading, then stop.
    if (wetEnd <= 0.0f) {
        reset();
        return false;
    }
    return true;
}

}  // namespace tape
}  // namespace audio
}  // namespace ot
