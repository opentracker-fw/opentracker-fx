// ECHO tempo-synced dub echo (see echo.h).
//
// One block (kBlock frames = kHalf line samples):
//   - the taps of the line (t, and 2t for ping-pong; also the new taps when
//     the time changed) come in as contiguous spans (spanIn, split at the
//     line's end);
//   - per line sample: the wet sides (stereo: both the t tap; ping-pong: the
//     t tap left, the 2t tap x feedback right) and the loop input (the
//     decimated input + feedback x the t tap, or feedback^2 x the 2t tap)
//     through the tone low-pass, the two low-cut high-passes and the soft
//     clip, into a span written back once (spanOut);
//   - the wet sides, upsampled (linear), times the mix and the duck gain,
//     are added into the bus; the dry bus is untouched.
// Ping-pong with taps t and 2t of the one line w(n) = x(n) + fb^2 w(n - 2t):
// left w(n - t) holds the repeats 1, 3, 5, .., right fb w(n - 2t) the
// repeats 2, 4, .. (each one the one before x fb).
#include "echo.h"

#include "audio/audio_tables.h"
#include "audio/ring_io.h"
#include "audio/va_dsp.h"

namespace ot {
namespace audio {
namespace echo {

const char* const kTimeName[kTimes] = {"1/32", "1/16", "1/16.", "1/8T", "1/8", "1/8.", "1/4T", "1/4", "1/4.", "1/2"};

namespace {

constexpr uint32_t kHalf = kBlock / 2;         // line samples per block
constexpr float kHalfFs = kSampleRate * 0.5f;
constexpr float kToLine = 32767.0f * 0.5f;     // the line holds y / 2 (soft clip ceiling 2)
constexpr float kFromLine = 2.0f / 32767.0f;
constexpr float kQuiet = 2.5e-4f;              // repeats below this are over
constexpr uint32_t kClearStep = 256;           // line samples cleared per block after Off (the normal span traffic)
// The write span (kHalf at a multiple of kHalf) never straddles the line's end.
static_assert(kBlock % 2 == 0 && kLine % kClearStep == 0 && kLine % kHalf == 0, "line layout");

const float kBeats[kTimes] = {0.125f, 0.25f, 0.375f, 1.0f / 3.0f, 0.5f, 0.75f, 2.0f / 3.0f, 1.0f, 1.5f, 2.0f};

inline float absf(float x) { return __builtin_fabsf(x); }
// The line position `back` samples before `w` (0 <= back <= 2 kLine: a 2t
// tap of a long stereo time, read in the block that switches to ping-pong).
inline uint32_t before(uint32_t w, uint32_t back) {
    uint32_t p = w + 2u * kLine - back;
    if (p >= kLine) p -= kLine;
    if (p >= kLine) p -= kLine;
    return p;
}
// One tap span in, split at the line's end (out of line: four call sites, flash).
__attribute__((noinline)) void tapIn(const int16_t* line, uint32_t pos, int16_t* dst) {
    uint32_t n = kHalf;
    while (n) {
        const uint32_t seg = kLine - pos < n ? kLine - pos : n;
        OT_SDRAM_ACCESS((seg + 2u) / 2u);
        spanIn(line + pos, dst, seg);
        dst += seg;
        n -= seg;
        pos = 0;
    }
}

// Clamp to [-1, 1): on the M4 the saturating VCVT to Q31 and back.
inline float clamp1(float x) {
#if defined(__arm__) && defined(__ARM_FP)
    __asm__("vcvt.s32.f32 %0, %0, #31" : "+t"(x));
    __asm__("vcvt.f32.s32 %0, %0, #31" : "+t"(x));
    return x;
#else
    return va::clampf(x, -1.0f, 1.0f);
#endif
}
// Soft clip with a ceiling of 2: a cubic, slope 1 at 0, flat from |x| = 3.
inline float clip2(float x) {
    const float v = clamp1(x * (1.0f / 3.0f));
    return v * (3.0f - v * v);
}

inline float lp1(float& s, float x, float G) {
    const float v = (x - s) * G;
    const float y = v + s;
    s = y + v;
    return y;
}

// One block of the line (kHalf samples): the wet sides and the feedback are
// the taps A (t) and B (2t) times per-sample ramped coefficients (stereo:
// wl = A, wr = A, back = fb A; ping-pong: wl = A, wr = fb B, back = fb^2 B; a
// change of the mode ramps one set into the other). The taps cross into the
// new time's (A1, B1) over the block (the same spans when the time stays:
// exact, one loop for both: flash).
struct Loop {
    const int16_t* a0;
    const int16_t* b0;
    const int16_t* a1;
    const int16_t* b1;
    int16_t* wr;
    float* l;
    float* r;
    float cr[2], dcr[2];   // wr = cr[0] A + cr[1] B
    float cb[2], dcb[2];   // back = cb[0] A + cb[1] B
    float in, din, g, dg;  // input gain; wet gain (mix x duck)
    float gLp, gHp;
    bool lp;
    float hp1, hp2, lps, upA, upB;
    float in2, in1;        // the input's last two mono frames (the decimator)
    float bw;              // 1: B is a tap of the mode (ping-pong before or after), else ignored
    float energy;          // sum of the taps' squares (the tail)
};
void lineBlock(Loop& o) {
    const int16_t* __restrict a0 = o.a0;
    const int16_t* __restrict b0 = o.b0;
    const int16_t* __restrict a1 = o.a1;
    const int16_t* __restrict b1 = o.b1;
    int16_t* __restrict wr = o.wr;
    float* __restrict l = o.l;
    float* __restrict r = o.r;
    float c0 = o.cr[0], c1 = o.cr[1], k0 = o.cb[0], k1 = o.cb[1], in = o.in, g = o.g;
    const float dc0 = o.dcr[0], dc1 = o.dcr[1], dk0 = o.dcb[0], dk1 = o.dcb[1], din = o.din, dg = o.dg;
    const float gLp = o.gLp, gHp = o.gHp, bw = o.bw;
    const bool lp = o.lp;
    float hp1 = o.hp1, hp2 = o.hp2, lps = o.lps, upA = o.upA, upB = o.upB, e = 0.0f, p2 = o.in2, p1 = o.in1;
    float f = 0.5f / float(kHalf);
    const float df = 1.0f / float(kHalf);
    for (uint32_t j = 0; j < kHalf; ++j) {
        float A = float(a0[j]) * kFromLine, B = float(b0[j]) * kFromLine;
        A += f * (float(a1[j]) * kFromLine - A);
        B += f * (float(b1[j]) * kFromLine - B);
        f += df;
        e += A * A + bw * (B * B);
        const float wl = A, wrt = c0 * A + c1 * B, back = k0 * A + k1 * B;
        // Into the line: the input (decimated 2:1) and the feedback, through
        // the tone, the low cut and the soft clip.
        const uint32_t i = 2 * j;
        // 2:1 decimation, [1 3 3 1] / 8 over the mono frames 2j-2 .. 2j+1 (a
        // zero at fs / 2, -18 dB at 16.5 kHz instead of the pair average's -8 dB).
        const float m0 = 0.5f * (l[i] + r[i]), m1 = 0.5f * (l[i + 1] + r[i + 1]);
        float y = 0.125f * (p2 + 3.0f * (p1 + m0) + m1) * in + back;
        p2 = m0;
        p1 = m1;
        if (lp) y = lp1(lps, y, gLp);
        else lps = y;   // bypassed: the state follows (no step when it comes in)
        y -= lp1(hp1, y, gHp);
        y -= lp1(hp2, y, gHp);
        wr[j] = int16_t(clip2(y) * kToLine);   // truncates towards 0: the repeats die out
        // Out: the wet sides, upsampled, added to the dry bus.
        l[i] += g * 0.5f * (upA + wl);
        r[i] += g * 0.5f * (upB + wrt);
        l[i + 1] += g * wl;
        r[i + 1] += g * wrt;
        upA = wl;
        upB = wrt;
        c0 += dc0;
        c1 += dc1;
        k0 += dk0;
        k1 += dk1;
        in += din;
        g += dg;
    }
    o.hp1 = hp1;
    o.hp2 = hp2;
    o.lps = lps;
    o.upA = upA;
    o.upB = upB;
    o.in2 = p2;
    o.in1 = p1;
    o.energy = e;
}
inline float onePoleG(float hz, float fs) {
    const float g = va::tanPade(va::kPi * va::clampf(hz, 5.0f, 0.45f * fs) / fs);
    return g / (1.0f + g);
}
// TPT one-pole: returns the low-pass, moves the state.

}  // namespace

float timeBeats(uint8_t time) { return kBeats[time < kTimes ? time : kTimes - 1]; }
float lowcutHz(uint8_t v) { return 20.0f * va::fastExp2(4.64385619f * float(v > 100 ? 100 : v) * 0.01f); }   // 20 x 25^(v/100)
float toneHz(uint8_t v) { return 500.0f * va::fastExp2(4.169925f * float(v > 100 ? 100 : v) * 0.01f); }     // 500 x 18^(v/100)

Params clamped(const Params& p) {
    Params c = p;
    c.on = p.on ? 1 : 0;
    c.time = p.time < kTimes ? p.time : uint8_t(kTimes - 1);
    c.feedback = p.feedback > 100 ? 100 : p.feedback;
    c.lowcut = p.lowcut > 100 ? 100 : p.lowcut;
    c.tone = p.tone > 100 ? 100 : p.tone;
    c.pingpong = p.pingpong ? 1 : 0;
    c.duck = p.duck > 100 ? 100 : p.duck;
    c.mix = p.mix > 100 ? 100 : p.mix;
    return c;
}

void Echo::setParams(const Params& p) {
    const uint8_t next = uint8_t(cur_ ^ 1u);
    pub_[next] = clamped(p);
    __atomic_store_n(&cur_, next, __ATOMIC_RELEASE);
}

void Echo::setTempo(float bpm) { bpm_ = va::clampf(bpm, 10.0f, 999.0f); }

float Echo::seconds(const Params& p, float bpm, bool* clamped) const {
    const float want = timeBeats(p.time) * 60.0f / va::clampf(bpm, 10.0f, 999.0f);
    const float max = float(p.pingpong ? kLine / 2 : kLine - 1) / kHalfFs;
    if (clamped) *clamped = want > max;
    return want > max ? max : want;
}

uint32_t Echo::tapOf(const Params& p) const {
    // Half-rate samples; one frame less: the decimator's and the upsampler's lag
    // (~2 frames, once) against the tap's own (every pass): the first and the
    // later repeats then land within 2 frames of the beat.
    const float s = seconds(p, bpm_) * kSampleRate - 1.0f;
    uint32_t t = uint32_t(s * 0.5f + 0.5f);
    const uint32_t max = p.pingpong ? kLine / 2 : kLine - 1;
    return t < kHalf ? kHalf : (t > max ? max : t);
}

void Echo::reset() {
    w_ = t_ = 0;
    pp_ = 0;
    hp1_ = hp2_ = lp_ = inPrev_[0] = inPrev_[1] = upA_ = upB_ = 0.0f;
    wet_ = fb_ = env_ = 0.0f;
    duckG_ = 1.0f;
    inOn_ = 0;
    quiet_ = clear_ = 0;
    running_ = false;
}

bool Echo::process(float* l, float* r, uint32_t n) {
    if (!line_ || n != kBlock) return false;
    const Params p = pub_[__atomic_load_n(&cur_, __ATOMIC_ACQUIRE)];
    if (clear_ && p.on) {   // On again while clearing: what is left is below kQuiet, start afresh
        clear_ = 0;
        reset();
    }
    if (clear_) {   // after Off and the repeats: clear the line, a span per block
        const uint32_t k = clear_ < kClearStep ? clear_ : kClearStep;
        OT_SDRAM_ACCESS((k + 2u) / 2u);
        spanZero(line_ + (kLine - clear_), k);
        clear_ -= k;
        if (clear_) return true;
        reset();
        return false;
    }
    if (!p.on && !running_) return false;   // Off and silent: untouched
    running_ = true;

    // Taps: the last block's and, after a change of the time / mode, the new ones.
    const uint32_t t1 = tapOf(p), t0 = t_ ? t_ : t1;
    const uint8_t pp1 = p.pingpong, pp0 = t_ ? pp_ : pp1;
    const bool xf = t1 != t0;
    alignas(4) int16_t a0[kHalf], b0[kHalf], a1[kHalf], b1[kHalf], wr[kHalf];
    // The 2t taps only for a ping-pong side of the block (stereo has none: a
    // long stereo time's 2t would be a wrapped, meaningless read).
    tapIn(line_, before(w_, t0), a0);
    if (pp0) tapIn(line_, before(w_, 2u * t0), b0);
    if (xf) tapIn(line_, before(w_, t1), a1);
    if (pp1 && (xf || !pp0)) tapIn(line_, before(w_, 2u * t1), b1);

    // Block-rate values, ramped from the last block.
    const float fbEnd = 0.98f * float(p.feedback) * 0.01f, wetEnd = float(p.mix) * 0.01f;
    float inPk = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        const float a = absf(0.5f * (l[i] + r[i]));
        inPk = a > inPk ? a : inPk;
    }
    // Duck follower at block rate (5 ms attack, 150 ms release).
    const float aAtt = 1.0f - va::fastExp2(-1.4427f * float(kBlock) / (0.005f * kSampleRate));
    const float aRel = va::fastExp2(-1.4427f * float(kBlock) / (0.150f * kSampleRate));
    env_ = inPk > env_ ? env_ + aAtt * (inPk - env_) : env_ * aRel;
    const float duckEnd = 1.0f - float(p.duck) * 0.01f * va::clampf(2.0f * env_, 0.0f, 1.0f);
    const float inv = 1.0f / float(kHalf);
    // The mode's coefficients at the block's ends (feedback fb_ -> fbEnd).
    auto coef = [](uint8_t pp, float fb, float* cr, float* cb) {
        cr[0] = pp ? 0.0f : 1.0f;
        cr[1] = pp ? fb : 0.0f;
        cb[0] = pp ? 0.0f : fb;
        cb[1] = pp ? fb * fb : 0.0f;
    };
    float cr0[2], cb0[2], cr1[2], cb1[2];
    coef(pp0, fb_, cr0, cb0);
    coef(pp1, fbEnd, cr1, cb1);
    Loop o;
    o.a0 = a0;
    o.a1 = xf ? a1 : a0;
    // B per side: its own ping-pong tap, else the other side's (or A when
    // neither has one: its coefficients are 0 then).
    const int16_t* const bNew = pp1 ? (xf || !pp0 ? b1 : b0) : nullptr;
    const int16_t* const bOld = pp0 ? b0 : nullptr;
    o.b0 = bOld ? bOld : (bNew ? bNew : a0);
    o.b1 = bNew ? bNew : o.b0;
    o.wr = wr;
    o.l = l;
    o.r = r;
    for (int k = 0; k < 2; ++k) {
        o.cr[k] = cr0[k];
        o.dcr[k] = (cr1[k] - cr0[k]) * inv;
        o.cb[k] = cb0[k];
        o.dcb[k] = (cb1[k] - cb0[k]) * inv;
    }
    // The input into the line ramps on a change of On (no step into the line).
    o.in = inOn_ ? 1.0f : 0.0f;
    o.din = ((p.on ? 1.0f : 0.0f) - o.in) * inv;
    o.g = wet_ * duckG_;
    o.dg = (wetEnd * duckEnd - o.g) * inv;
    o.bw = pp0 || pp1 ? 1.0f : 0.0f;   // (B is a real tap then)
    o.lp = p.tone < 100;
    o.gLp = o.lp ? onePoleG(toneHz(p.tone), kHalfFs) : 1.0f;
    o.gHp = onePoleG(lowcutHz(p.lowcut), kHalfFs);
    o.hp1 = hp1_;
    o.hp2 = hp2_;
    o.lps = lp_;
    o.upA = upA_;
    o.upB = upB_;
    o.in2 = inPrev_[0];
    o.in1 = inPrev_[1];
    lineBlock(o);
    inPrev_[0] = o.in2;
    inPrev_[1] = o.in1;
    OT_SDRAM_ACCESS((kHalf + 2u) / 2u);
    spanOut(line_ + w_, wr, kHalf);
    w_ = w_ + kHalf >= kLine ? 0u : w_ + kHalf;
    t_ = t1;
    pp_ = pp1;
    fb_ = fbEnd;
    wet_ = wetEnd;
    duckG_ = duckEnd;
    inOn_ = p.on;
    hp1_ = va::flush(o.hp1);
    hp2_ = va::flush(o.hp2);
    lp_ = va::flush(o.lps);
    upA_ = o.upA;
    upB_ = o.upB;
    const float tap = o.energy * inv;   // mean square of the taps
    // Tail: while the repeats sound (or the input feeds the line); after Off,
    // once they have been quiet for a whole pass, clear the line.
    const uint32_t pass = (pp1 ? 2u : 1u) * t1 + kHalf;
    quiet_ = tap < kQuiet * kQuiet && (!p.on || inPk < kQuiet) ? quiet_ + kHalf : 0;
    if (quiet_ >= pass) {
        if (!p.on) {
            clear_ = kLine;
            return true;
        }
        running_ = false;   // On, but nothing in the line: no tail
        return false;
    }
    return true;
}

}  // namespace echo
}  // namespace audio
}  // namespace ot
