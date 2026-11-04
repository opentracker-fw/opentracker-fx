// LOFI: a chip DAC / speaker master insert (OpenTracker 3.x effect module,
// id LOFI, docs/PLAN_RETRO.md §10).
//
// The bus through the sound of a small chip's output stage: Mono (L = R =
// their mean), a DAC of Bits bits (4..16; 16 = none) with an optional Ladder
// character (uneven top steps: the codes' top bits carry weight errors,
// audio/chip_dsp.h), a sample-and-hold at Rate (32 kHz .. 4 kHz; a
// fractional period, one counter for both channels, no interpolation:
// aliasing is the point), a Speaker shape (TV: high-pass 80 Hz, low-pass
// 6 kHz, +2 dB at 3 kHz; Handheld: 300 Hz, 4.5 kHz, +4 dB at 1.5 kHz;
// Monitor: 40 Hz, 12 kHz; the shapes are not level-neutral on edges: a full
// square through TV / Handheld peaks near +-2.2 before the limiter) and a dry
// / wet Mix. Gains ramp from the previous
// block; a Rate, Speaker or Mono change crossfades over one block; Off ramps
// the wet out over one block and then passes the bus through untouched. No
// buffers. Our own design; textbook techniques.
#pragma once

#include <cstdint>

#include "audio/inserts.h"

namespace ot {
namespace audio {
namespace lofi {

enum Rate : uint8_t { kRateOff = 0, k32k, k22k, k16k, k11k, k8k, k6k, k4k, kRates };
enum Speaker : uint8_t { kSpeakerOff = 0, kTv, kHandheld, kMonitor, kSpeakers };

struct Params {
    uint8_t on = 0;            // 0 / 1
    uint8_t bits = 8;          // 4..16 (16: none)
    uint8_t rate = k11k;       // Rate
    uint8_t ladder = 0;        // 0..100
    uint8_t speaker = kTv;     // Speaker
    uint8_t mono = 0;          // 0 / 1
    uint8_t mix = 100;         // 0..100 wet
};
// The hold rate (Hz) of a Rate (0: off).
float rateHz(uint8_t rate);
// A Speaker's high-pass and low-pass corners and its bump (Hz, dB; 0 Hz: none).
struct Shape {
    float hp, lp, bumpHz, bumpDb;
};
Shape speakerShape(uint8_t speaker);
// Names (the page).
extern const char* const kRateName[kRates];
extern const char* const kSpeakerName[kSpeakers];

class Lofi final : public IMasterInsert {
public:
    // Main loop: takes effect at the next block (one-block hand-over).
    void setParams(const Params& p);
    Params params() const { return pub_[cur_]; }
    void reset();
    bool process(float* l, float* r, uint32_t n) override;

    // The state of one configuration (the speaker filters, the hold).
    struct Chan {
        float hp, lp, s1, s2, held;
    };

private:
    Params pub_[2];
    volatile uint8_t cur_ = 0;
    float wet_ = 0.0f;           // the wet amount the last block ended on
    float holdPh_ = 0.0f;        // frames since the last hold
    Chan ch_[2] = {};
    uint8_t rate_ = 0xFF, speaker_ = 0xFF, mono_ = 0xFF;   // what the last block ran (a change crossfades)
};

}  // namespace lofi
}  // namespace audio
}  // namespace ot
