// CHORUS: a stereo master-bus chorus (OpenTracker 3.x effect module, id CHOR).
//
// The mono sum feeds one delay line; two taps read it at 7 ms +- Depth,
// moved by one LFO (Rate) whose right-channel phase is offset by Width
// (0 = the same on both sides, 100 = opposite: the widest image). Each side
// mixes its tap over the dry signal (Mix). Parameters 0..100; Off passes the
// bus through untouched. Our own design; textbook technique.
#pragma once

#include <cstdint>

#include "audio/inserts.h"

namespace ot {
namespace audio {
namespace chorus {

struct Params {
    uint8_t on = 0;      // 0 / 1
    uint8_t rate = 20;   // 0..100 -> 0.1 .. 5 Hz
    uint8_t depth = 50;  // 0..100 -> 0 .. 3 ms swing
    uint8_t width = 70;  // 0..100 -> right LFO phase offset 0 .. 1/2 cycle
    uint8_t mix = 50;    // 0..100
};

class Chorus final : public IMasterInsert {
public:
    static constexpr int kLine = 512;   // frames (11.6 ms), int16 (1 KB of SRAM)
    void setParams(const Params& p);
    Params params() const { return pub_[cur_]; }
    void reset();
    bool process(float* l, float* r, uint32_t n) override;

private:
    Params pub_[2];
    volatile uint8_t cur_ = 0;
    int16_t line_[kLine] = {};
    uint32_t w_ = 0;
    float ph_ = 0.0f;
    float wet_ = 0.0f;
};

}  // namespace chorus
}  // namespace audio
}  // namespace ot
