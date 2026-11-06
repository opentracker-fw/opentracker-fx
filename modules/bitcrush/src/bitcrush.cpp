// CRSH bitcrusher (see bitcrush.h).
#include "bitcrush.h"

#include "audio/va_dsp.h"

namespace ot {
namespace audio {
namespace bitcrush {

const char* const kShapeName[kShapes] = {"Hard", "Soft"};

namespace {

// One channel of a block; returns the hold counter at its end (both channels
// run the same count from the same start).
template <bool Soft, bool Crush>
uint32_t channel(float* x, uint32_t n, float& hold, float w, float dw, uint32_t cnt, uint32_t down, float scale,
                 float inv) {
    float h = hold;
    for (uint32_t i = 0; i < n; ++i) {
        w += dw;
        const float in = x[i];
        if (cnt == 0) {
            float y = Soft ? va::tanhR(in) : (in > 1.0f ? 1.0f : (in < -1.0f ? -1.0f : in));
            if (Crush) {   // round to the grid (one conversion, |y * scale| < 2^15)
                const float q = y * scale;
                y = float(int32_t(q + (q >= 0.0f ? 0.5f : -0.5f))) * inv;
            }
            h = y;
            cnt = down;
        } else {
            --cnt;
        }
        x[i] = (1.0f - w) * in + w * h;
    }
    hold = h;
    return cnt;
}

}  // namespace

const EffectParam& BitcrushEffect::param(int i) const {
    static const EffectParam kParams[4] = {
        {"Shape", kShapes - 1, kHard, kShapeName},
        {"Crush", 12, 8, nullptr},
        {"Down", 31, 3, nullptr},
        {"Mix", 100, 100, nullptr},
    };
    return kParams[i < 0 || i > 3 ? 0 : i];
}

bool BitcrushEffect::process(void* state, void* work, const EffectSettings& s, bool on, float* l, float* r,
                             uint32_t n) {
    (void)work;
    Inst& st = *static_cast<Inst*>(state);
    const float wetT = on ? float(s.v[3] > 100 ? 100 : s.v[3]) * 0.01f : 0.0f;
    if (!on && st.wet <= 0.0f) return false;   // faded out (or fresh): the bus passes untouched
    const bool soft = s.v[0] == kSoft;
    const uint8_t crush = s.v[1] > 12 ? 12 : s.v[1];
    const uint32_t down = s.v[2] > 31 ? 31 : s.v[2];
    const float scale = float(1u << (15 - crush)), inv = 1.0f / scale;
    const float dw = (wetT - st.wet) / float(n);
    const uint32_t cnt0 = st.cnt;
    auto run = soft ? (crush ? channel<true, true> : channel<true, false>)
                    : (crush ? channel<false, true> : channel<false, false>);
    st.cnt = run(l, n, st.hold[0], st.wet, dw, cnt0, down, scale, inv);
    run(r, n, st.hold[1], st.wet, dw, cnt0, down, scale, inv);
    st.wet = wetT;
    return true;
}

BitcrushEffect gBitcrushEffect;

}  // namespace bitcrush
}  // namespace audio
}  // namespace ot
