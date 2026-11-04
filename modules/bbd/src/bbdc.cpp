// BBDC master-bus effect (see bbdc.h): the shared BBD chorus on the master bus.
#include "bbdc.h"

#include <cstdio>

namespace ot {
namespace audio {
namespace bbdc {

float toneHz(uint8_t t) { return bbd::toneHz(t); }

ChorusEffect gChorusEffect;

const EffectParam& ChorusEffect::param(int i) const {
    static const char* const kModeNames[3] = {"I", "II", "I+II"};
    static const EffectParam kParams[4] = {
        {"Mode", 2, 0, kModeNames},
        {"Mix", 100, 100, nullptr},
        {"Width", 100, 100, nullptr},
        {"Tone", 100, 50, nullptr},
    };
    return kParams[i < 0 ? 0 : (i > 3 ? 3 : i)];
}

void ChorusEffect::valueText(int i, uint8_t v, char* out, int cap) const {
    if (i == 3 && out && cap > 0) {
        std::snprintf(out, size_t(cap), "%.1f kHz", double(toneHz(v)) * 0.001);
        return;
    }
    IEffect::valueText(i, v, out, cap);
}

bool ChorusEffect::process(void* state, void* work, const EffectSettings& s, bool on, float* l, float* r,
                           uint32_t n) {
    const uint8_t mode = on ? uint8_t((s.v[0] > 2 ? 2 : s.v[0]) + 1) : uint8_t(0);
    return bbd::process(*static_cast<bbd::State*>(state), static_cast<int16_t*>(work), mode, float(s.v[1]) * 0.01f,
                        0.01f * float(s.v[2]), s.v[3], l, r, n);
}

void ChorusEffect::reset(void* state, void* work) {
    bbd::clear(*static_cast<bbd::State*>(state), static_cast<int16_t*>(work));
}

void BbdChorus::setParams(const Params& p) {
    // As on the classic BBD chorus, Chorus I or II (the page offers no more).
    const uint8_t mode = p.mode > 2 ? 2 : p.mode;
    set(EffectSettings{{uint8_t(mode ? mode - 1 : 0), p.mix, p.width, p.tone, 0, 0}}, mode != 0);
}

Params BbdChorus::params() const {
    const EffectSettings s = settings();
    return Params{uint8_t(on() ? s.v[0] + 1 : 0), s.v[1], s.v[2], s.v[3]};
}

}  // namespace bbdc
}  // namespace audio
}  // namespace ot
