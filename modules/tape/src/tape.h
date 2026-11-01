// TAPE: a master-bus tape effect (OpenTracker 3.x demo effect module, id TAPE).
//
// Per channel: drive into a soft clip (tape saturation), a gentle high cut
// (tape head loss), wow (0.6 Hz) and flutter (7 Hz) as a modulated delay of
// up to ~2.3 ms, then a dry / wet mix. Parameters 0..100; Off passes the bus
// through untouched. Our own design; textbook techniques.
#pragma once

#include <cstdint>

#include "audio/inserts.h"

namespace ot {
namespace audio {
namespace tape {

struct Params {
    uint8_t on = 0;        // 0 / 1
    uint8_t drive = 30;    // 0..100
    uint8_t wow = 20;      // 0..100
    uint8_t flutter = 10;  // 0..100
    uint8_t mix = 100;     // 0..100 wet
};

class Tape final : public IMasterInsert {
public:
    static constexpr int kLine = 256;   // delay line per channel (frames, int16: 512 B)
    // Main loop: takes effect at the next block (one-block hand-over).
    void setParams(const Params& p);
    Params params() const { return pub_[cur_]; }
    void reset();
    bool process(float* l, float* r, uint32_t n) override;

private:
    struct Chan {
        float lp = 0.0f;
        int16_t line[kLine] = {};
    };
    Params pub_[2];
    volatile uint8_t cur_ = 0;
    Chan ch_[2];
    uint32_t w_ = 0;              // write position
    float phW_ = 0.0f, phF_ = 0.0f;
    float wet_ = 0.0f;            // the wet amount the last block ended on (ramps)
    int tail_ = 0;                // blocks left to play out after Off
};

}  // namespace tape
}  // namespace audio
}  // namespace ot
