// DIRT: a master-bus drive / bitcrush effect (OpenTracker 3.x effect module,
// id DIRT, docs/PLAN_TECHNO.md §10).
//
// Per channel: a pre-gain (1 + 15 d^2, d = Drive / 100) into one of four
// shapers (Soft: rational tanh; Hard: clip at +-1; Fold: a triangle fold
// back into +-1; Asym: a biased tanh, even harmonics, with a 10 Hz DC block),
// a make-up gain 1 / (1 + 0.5 d), a quantiser to Bits (4..16; 16 = none), a
// sample-and-hold every Rate frames (1..32; 1 = none; one counter for both
// channels), a one-pole low-pass Tone (0..100 = 200 Hz .. 20 kHz; 100 = none)
// and a dry / wet Mix. Gains and the wet amount ramp from the previous
// block's values (no click on a slammed Drive or Mix), a Type change
// crossfades over one block, the bypassed Tone low-pass follows the signal
// (no step when it comes in); Off ramps the wet out
// over one block and then passes the bus through untouched. No buffers. Our
// own design; textbook techniques.
#pragma once

#include <cstdint>

#include "audio/inserts.h"

namespace ot {
namespace audio {
namespace dirt {

enum Type : uint8_t { kSoft = 0, kHard, kFold, kAsym, kTypes };

struct Params {
    uint8_t on = 0;        // 0 / 1
    uint8_t type = kSoft;  // Type
    uint8_t drive = 30;    // 0..100
    uint8_t bits = 16;     // 4..16
    uint8_t rate = 1;      // 1..32 (hold every n frames)
    uint8_t tone = 100;    // 0..100
    uint8_t mix = 100;     // 0..100 wet
};
// The Tone low-pass corner (Hz) of a Tone value below 100.
float toneHz(uint8_t tone);
// One sample of the shaper (before the make-up gain; Asym without its DC block).
float shape(uint8_t type, float x);
// Names of the types (the page).
extern const char* const kTypeName[kTypes];

// One instance's state (all zero = fresh: the gains start at 1, no type yet).
struct Inst {
    float wet;            // the wet amount the last block ended on
    float pre, post;      // pre-gain / make-up the last block ended on (fresh: 1)
    float hold[2], lp[2], dc[2];
    float adc[2];         // Asym's DC block while another type plays (entering Asym starts from it)
    uint32_t cnt;         // frames left of the held value
    uint8_t type1;        // the type the last block ran + 1 (0: none; a change crossfades)
};
// One block of an instance (p clamped); false (no tail).
bool run(Inst& s, const Params& p, float* l, float* r, uint32_t n);

class Dirt final : public IMasterInsert {
public:
    // Main loop: takes effect at the next block (one-block hand-over).
    void setParams(const Params& p);
    Params params() const { return pub_[cur_]; }
    void reset() { st_ = Inst{}; }
    bool process(float* l, float* r, uint32_t n) override;

private:
    Params pub_[2];
    volatile uint8_t cur_ = 0;
    Inst st_ = {};
};

// Drive as a per-instance effect (id DRIV, "Drive"): Type, Drive, Tone, Mix at
// Bits 16 and Rate 1 (no crushing); no work memory.
class DriveEffect final : public IEffect {
public:
    const char* id() const override { return "DRIV"; }
    const char* name() const override { return "Drive"; }
    uint32_t stateBytes() const override { return sizeof(Inst); }
    int paramCount() const override { return 4; }
    const EffectParam& param(int i) const override;
    uint32_t tailFrames() const override;   // one block: the Tone low-pass / DC block settle
    bool process(void* state, void* work, const EffectSettings& s, bool on, float* l, float* r,
                 uint32_t n) override;
};
extern DriveEffect gDriveEffect;

}  // namespace dirt
}  // namespace audio
}  // namespace ot
