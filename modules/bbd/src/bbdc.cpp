// BBDC master-bus effect (see bbdc.h): the shared BBD chorus on the master bus.
#include "bbdc.h"

namespace ot {
namespace audio {
namespace bbdc {

float toneHz(uint8_t t) { return bbd::toneHz(t); }

void BbdChorus::setParams(const Params& p) {
    const uint8_t next = uint8_t(cur_ ^ 1u);
    pub_[next] = p;
    __atomic_store_n(&cur_, next, __ATOMIC_RELEASE);
}

void BbdChorus::reset() { bbd::clear(st_, line_); }

bool BbdChorus::process(float* l, float* r, uint32_t n) {
    const Params p = pub_[__atomic_load_n(&cur_, __ATOMIC_ACQUIRE)];
    // As on the classic BBD chorus, Chorus I or II (the page offers no more).
    const uint8_t mode = p.mode > 2 ? 2 : p.mode;
    return bbd::process(st_, line_, mode, float(p.mix) * 0.01f, 0.01f * float(p.width), p.tone, l, r, n);
}

}  // namespace bbdc
}  // namespace audio
}  // namespace ot
