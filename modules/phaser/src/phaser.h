// PHSR: a phaser as a per-instance effect (OpenTracker 3.x effect module, effect
// id PHAS, "Phaser"), for the synth slot inserts.
//
// Per channel: the input plus Feedback times the last wet sample runs through a
// chain of first-order all-pass stages that share one corner frequency; the
// wet chain output mixed with the dry input cancels where the chain turns the
// phase by 180 degrees (the notches). An LFO sweeps the corner on a log scale
// between the mode's lowest and highest frequency (Depth: how far up); Spread
// offsets the right channel's LFO by up to half a cycle.
//   Classic: 4 stages, 140 Hz .. 2.4 kHz, a sweep that lingers low and rises
//            quickly (two notches, the warm small-pedal sound); Feedback up
//            to 0.6.
//   80s:     8 stages, 90 Hz .. 4.2 kHz, a smooth (raised-cosine) sweep, four
//            notches; Feedback up to 0.8 (the deep rack sound).
// The corner is computed every 64 frames and ramped in between; Depth,
// Spread, Feedback, Mix and the on / off fade ramp across the block, and a
// Mode change crossfades the 4-stage and 8-stage outputs over one block (no
// click). Off fades the wet out over one block and then passes the bus through
// untouched. The stage memory is the work memory (80 bytes). Our own design;
// textbook techniques.
#pragma once

#include <cstdint>

#include "audio/inserts.h"

namespace ot {
namespace audio {
namespace phaser {

enum Mode : uint8_t { kClassic = 0, k80s, kModes };
extern const char* const kModeName[kModes];

constexpr int kStages = 8;
constexpr uint32_t kStep = 64;   // frames per corner update

// One instance's state (all zero = fresh).
struct Inst {
    uint32_t phase;            // LFO phase (one cycle = 2^32)
    float gain;                // wet amount (Mix x on / off) the last block ended on
    float fb, depth, spread;   // ramped settings the last block ended on
    float tap;                 // 0: the 4-stage output, 1: the 8-stage output
    float a[2];                // all-pass coefficient per channel the last block ended on
    uint8_t mode1;             // the mode the last block ran + 1 (0: fresh)
    uint8_t upper;             // stages 5..8 ran in the last block
};
// The work memory per channel: p[0] the last input of the chain, p[k] the
// last output of stage k (one multiply per stage: y = p[k-1] + a (x - p[k]);
// a stage's last input is the stage before's last output), and the last wet
// sample.
struct Work {
    float p[2][kStages + 1];
    float last[2];
};

// The all-pass coefficient of a corner frequency (Hz).
float coef(float hz);
// The LFO rate (Hz) of a Rate value 0..100.
float rateHz(uint8_t rate);

class PhaserEffect final : public IEffect {
public:
    const char* id() const override { return "PHAS"; }
    const char* name() const override { return "Phaser"; }
    uint32_t stateBytes() const override { return sizeof(Inst); }
    uint32_t workBytes() const override { return sizeof(Work); }
    int paramCount() const override { return 6; }
    const EffectParam& param(int i) const override;
    uint32_t tailFrames() const override;   // the feedback ring dies away
    bool process(void* state, void* work, const EffectSettings& s, bool on, float* l, float* r,
                 uint32_t n) override;
};
extern PhaserEffect gPhaserEffect;

}  // namespace phaser
}  // namespace audio
}  // namespace ot
