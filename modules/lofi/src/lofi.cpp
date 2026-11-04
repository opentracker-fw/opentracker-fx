// LOFI chip DAC / speaker master insert (see lofi.h).
#include "lofi.h"

#include <cstring>

#include "audio/audio_tables.h"
#include "audio/chip_dsp.h"
#include "audio/va_dsp.h"

namespace ot {
namespace audio {
namespace lofi {

const char* const kRateName[kRates] = {"Off", "32 kHz", "22 kHz", "16 kHz", "11 kHz", "8 kHz", "6 kHz", "4 kHz"};
const char* const kSpeakerName[kSpeakers] = {"Off", "TV", "Handheld", "Monitor"};

namespace {

const float kRateHz[kRates] = {0.0f, 32000.0f, 22050.0f, 16000.0f, 11025.0f, 8000.0f, 6000.0f, 4000.0f};
const Shape kShapes[kSpeakers] = {{0.0f, 0.0f, 0.0f, 0.0f}, {80.0f, 6000.0f, 3000.0f, 2.0f},
                                  {300.0f, 4500.0f, 1500.0f, 4.0f}, {40.0f, 12000.0f, 0.0f, 0.0f}};

// One configuration's per-block constants.
struct Cfg {
    uint8_t mono, quant, ladder, hold, speaker, bump, pad[2];
    int bits;
    float q, iq, amount;   // the DAC: 2^(bits - 1), its inverse, the ladder amount
    float period;          // frames a hold
    float ghp, glp;        // one-pole G of the high-pass and the low-pass
    va::SvfCoef bp;        // the bump's band-pass
    float kg;              // bp.k + bp.g
    float bumpGain;        // (A - 1) x k: the band-pass's peak scaled to the bump
};

inline float onePoleG(float hz) {
    const float g = va::tanPade(va::kPi * hz * va::kInvFs);
    return g / (1.0f + g);
}

Cfg cfgOf(const Params& p) {
    Cfg c;
    std::memset(&c, 0, sizeof(c));
    c.mono = p.mono;
    c.quant = p.bits < 16;             // 16: none (a Ladder at 16 bits would be -90 dB: not run)
    c.ladder = c.quant && p.ladder > 0;
    c.bits = p.bits;
    c.q = float(1 << (p.bits < 16 ? p.bits - 1 : 15));
    c.iq = 1.0f / c.q;
    c.amount = float(p.ladder) * 0.01f;
    c.hold = p.rate != kRateOff;
    c.period = c.hold ? chip::holdRate(kSampleRate, kRateHz[p.rate]) : 1.0f;
    c.speaker = p.speaker != kSpeakerOff;
    if (c.speaker) {
        const Shape& s = kShapes[p.speaker];
        c.ghp = onePoleG(s.hp);
        c.glp = onePoleG(s.lp);
        c.bump = s.bumpHz > 0.0f;
        if (c.bump) {
            constexpr float kK = 1.0f;   // the bump's damping (Q 1)
            const float g = va::svfG(s.bumpHz, 0.0f);
            c.bp = va::SvfCoef{g, kK, va::svfH(g, kK)};
            c.kg = g + kK;
            // The bump's peak scaled so the whole shape reaches its dB there (the
            // one-poles' own loss at the bump made up: an analog estimate).
            const float f = s.bumpHz, a = f / s.lp, b = s.hp / f;
            const float loss = __builtin_sqrtf((1.0f + a * a) * (1.0f + b * b));
            c.bumpGain = (va::fastExp2(s.bumpDb * (3.32192809f / 20.0f)) * loss - 1.0f) * kK;
        }
    }
    return c;
}

// The DAC on one sample: rounded to 2^bits levels (mid-tread, clamped), the
// ladder's weight errors on the code's top bits (chip::ladder's, inline,
// from a table scaled to the block's amount: no division).
// The DAC in Q31: x saturated to Q31 (toward zero), half a step added (the
// saturating QADD keeps the top code; a negative value adds one less, so an
// exact .5 rounds away from zero as chip::quantBits does), the low 32 - bits
// bits cleared: the mid-tread code x 2^(32 - bits). Its top 6 bits (offset
// binary: the sign flipped) index the ladder table for every Bits 4..16.
struct Dac {
    uint32_t half, mask;
    uint32_t ladder;
    float err[64];       // chip::kLadder.e x amount / q
};
inline int32_t satAdd(int32_t a, int32_t b) {
#if defined(__arm__) && defined(__ARM_FP)
    int32_t r;
    __asm__("qadd %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
    return r;
#else
    const int64_t s = int64_t(a) + b;
    return s > 2147483647LL ? 2147483647 : (s < -2147483648LL ? int32_t(-2147483647 - 1) : int32_t(s));
#endif
}
inline float dac(const Dac& d, float x) {
    int32_t k;
#if defined(__arm__) && defined(__ARM_FP)
    __asm__("vcvt.s32.f32 %0, %0, #31" : "+t"(x));
    __asm__("vmov %0, %1" : "=r"(k) : "t"(x));
#else
    const double v = double(x) * 2147483648.0;
    k = v >= 2147483647.0 ? 2147483647 : (v <= -2147483648.0 ? int32_t(-2147483647 - 1) : int32_t(v));
#endif
    k = satAdd(k, int32_t(d.half) + (k >> 31)) & int32_t(d.mask);
    float y;
#if defined(__arm__) && defined(__ARM_FP)
    __asm__("vmov %0, %1" : "=t"(y) : "r"(k));
    __asm__("vcvt.f32.s32 %0, %0, #31" : "+t"(y));
#else
    y = float(k) * (1.0f / 2147483648.0f);
#endif
    if (d.ladder) y += d.err[(uint32_t(k) ^ 0x80000000u) >> 26];
    return y;
}
void dacOf(const Cfg& c, Dac& d) {
    const int s = 32 - c.bits;   // bits 4..15 here (16: no DAC)
    d.half = 1u << (s - 1);
    d.mask = ~((1u << s) - 1u);
    d.ladder = c.ladder;
    if (c.ladder)
        for (int i = 0; i < 64; ++i) d.err[i] = c.amount * chip::kLadder.e[i] * c.iq;
}

// The speaker on one sample: high-pass, the bump, low-pass (TPT one-poles, the bump an SVF band-pass).
inline float speak(const Cfg& c, Lofi::Chan& s, float x) {
    float v = (x - s.hp) * c.ghp;
    const float lo = v + s.hp;
    s.hp = lo + v;
    float y = x - lo;
    if (c.bump) {   // va::svfStep with k + g precomputed
        const float hp = (y - c.kg * s.s1 - s.s2) * c.bp.h;
        const float v1 = c.bp.g * hp;
        const float bp = v1 + s.s1;
        s.s1 = bp + v1;
        const float v2 = c.bp.g * bp;
        s.s2 = (v2 + s.s2) + v2;
        y += c.bumpGain * bp;
    } else {
        s.s1 = s.s2 = 0.0f;   // no bump (Monitor): its band-pass starts clean when a bump comes in
    }
    v = (y - s.lp) * c.glp;
    const float out = v + s.lp;
    s.lp = out + v;
    return out;
}

// n frames of the bus through a configuration. kMix: mixed in place into l /
// r (the wet amount w ramping by dw); else the wet signal into wl / wr.
// Mono: both channels do the same work on the same state (L == R).
template <bool kSpeaker>
__attribute__((noinline)) void runT(const Cfg& cfg, float* __restrict l, float* __restrict r, uint32_t n, Lofi::Chan* st,
                                    float& ph, float w, float dw) {
    const Cfg c = cfg;   // a local copy: the stores cannot alias it
    Dac dc;
    dc.half = dc.mask = dc.ladder = 0;
    if (c.quant) dacOf(c, dc);
    Lofi::Chan s0 = st[0], s1 = st[1];
    float p = ph, dry = 1.0f - w;
    const float ddry = -dw;
    for (uint32_t i = 0; i < n; ++i) {
        const float d0 = l[i], d1 = r[i];
        float x0 = d0, x1 = d1;
        if (c.mono) x0 = x1 = 0.5f * (d0 + d1);
        if (c.hold) {
            // The hold of the DAC's output: the DAC only where the hold takes a value.
            p += 1.0f;
            if (p >= c.period) {   // one counter for both channels
                p -= c.period;
                s0.held = c.quant ? dac(dc, x0) : x0;
                s1.held = c.quant ? dac(dc, x1) : x1;
            }
            x0 = s0.held;
            x1 = s1.held;
        } else if (c.quant) {
            x0 = dac(dc, x0);
            x1 = dac(dc, x1);
        }
        if (kSpeaker) {
            x0 = speak(c, s0, x0);
            x1 = speak(c, s1, x1);
        }
        w += dw;
        dry += ddry;
        l[i] = x0 * w + d0 * dry;   // exact at Mix 0 and 100
        r[i] = x1 * w + d1 * dry;
    }
    st[0] = s0;
    st[1] = s1;
    ph = p;
    for (int k = 0; k < 2; ++k) {
        st[k].hp = va::flush(st[k].hp);
        st[k].lp = va::flush(st[k].lp);
        st[k].s1 = va::flush(st[k].s1);
        st[k].s2 = va::flush(st[k].s2);
    }
}
void run(const Cfg& c, float* l, float* r, uint32_t n, Lofi::Chan* st, float& ph, float w, float dw) {
    (c.speaker ? runT<true> : runT<false>)(c, l, r, n, st, ph, w, dw);
}

}  // namespace

float rateHz(uint8_t rate) { return kRateHz[rate < kRates ? rate : kRates - 1]; }
Shape speakerShape(uint8_t speaker) { return kShapes[speaker < kSpeakers ? speaker : kSpeakers - 1]; }

void Lofi::setParams(const Params& in) {
    Params c = in;
    c.on = in.on ? 1 : 0;
    c.bits = in.bits < 4 ? 4 : (in.bits > 16 ? 16 : in.bits);
    c.rate = in.rate < kRates ? in.rate : uint8_t(kRates - 1);
    c.ladder = in.ladder > 100 ? 100 : in.ladder;
    c.speaker = in.speaker < kSpeakers ? in.speaker : uint8_t(kSpeakers - 1);
    c.mono = in.mono ? 1 : 0;
    c.mix = in.mix > 100 ? 100 : in.mix;
    const uint8_t next = uint8_t(cur_ ^ 1u);
    pub_[next] = c;
    __atomic_store_n(&cur_, next, __ATOMIC_RELEASE);
}

void Lofi::reset() {
    wet_ = 0.0f;
    holdPh_ = 0.0f;
    std::memset(ch_, 0, sizeof(ch_));
    rate_ = speaker_ = mono_ = 0xFF;
}

bool Lofi::process(float* l, float* r, uint32_t n) {
    const Params p = pub_[__atomic_load_n(&cur_, __ATOMIC_ACQUIRE)];
    const float wetEnd = p.on ? float(p.mix) * 0.01f : 0.0f;
    if (wetEnd <= 0.0f && wet_ <= 0.0f) return false;   // Off (or Mix 0) and faded out: untouched
    if (!n) return false;
    if (n > kBlock) n = kBlock;
    const Cfg c = cfgOf(p);
    const bool change = rate_ != 0xFF && (p.rate != rate_ || p.speaker != speaker_ || p.mono != mono_);
    const float dw = (wetEnd - wet_) / float(n);
    if (change) {
        // The old Rate / Speaker / Mono on copies of the state, the new one on
        // the state, crossfaded over the block, then mixed.
        Params o = p;
        o.rate = rate_;
        o.speaker = speaker_;
        o.mono = mono_;
        const Cfg co = cfgOf(o);
        Chan so[2] = {ch_[0], ch_[1]};
        float pho = holdPh_;
        float ol[kBlock], orr[kBlock];
        std::memcpy(ol, l, n * sizeof(float));
        std::memcpy(orr, r, n * sizeof(float));
        run(co, ol, orr, n, so, pho, wet_, dw);   // the old configuration, mixed, on the copy
        if (p.mono) ch_[1] = ch_[0];              // Mono: the right channel's state follows the left (L == R exactly)
        if (p.rate != rate_) holdPh_ = c.period;  // a new Rate takes its first value at once (no stale or late hold)
        run(c, l, r, n, ch_, holdPh_, wet_, dw);   // the new one in place; the mix is linear, so the
        const float df = 1.0f / float(n);          // crossfade of the two mixed blocks is the mixed crossfade
        float f = 0.0f;
        for (uint32_t i = 0; i < n; ++i) {
            f += df;
            l[i] = ol[i] + f * (l[i] - ol[i]);
            r[i] = orr[i] + f * (r[i] - orr[i]);
        }
    } else {
        run(c, l, r, n, ch_, holdPh_, wet_, dw);
    }
    rate_ = p.rate;
    speaker_ = p.speaker;
    mono_ = p.mono;
    wet_ = wetEnd;
    if (wetEnd <= 0.0f) reset();   // faded out: the next On starts clean
    return false;                  // no tail
}

}  // namespace lofi
}  // namespace audio
}  // namespace ot
