// CRSH: a bitcrusher as a per-instance effect (OpenTracker 3.x effect module,
// effect id CRSH, "Bitcrush"), for the synth slot inserts.
//
// Per channel: a shaper (Hard: clip at +-1; Soft: a rational tanh), a
// quantiser to 16 - Crush bits (Crush 0..12; 0 = none), a sample-and-hold
// every Down + 1 frames (Down 0..31; one counter for both channels) and a dry /
// wet Mix. The wet amount (Mix x on / off) ramps across the block (no click);
// Off fades it out over one block and then passes the bus through untouched.
// No buffers. Our own design; textbook techniques.
#pragma once

#include <cstdint>

#include "audio/inserts.h"

namespace ot {
namespace audio {
namespace bitcrush {

enum Shape : uint8_t { kHard = 0, kSoft, kShapes };
extern const char* const kShapeName[kShapes];

// One instance's state (all zero = fresh).
struct Inst {
    float hold[2];   // the held samples
    float wet;       // the wet amount the last block ended on
    uint32_t cnt;    // frames left of the held values
};

class BitcrushEffect final : public IEffect {
public:
    const char* id() const override { return "CRSH"; }
    const char* name() const override { return "Bitcrush"; }
    uint32_t stateBytes() const override { return sizeof(Inst); }
    int paramCount() const override { return 4; }
    const EffectParam& param(int i) const override;
    bool process(void* state, void* work, const EffectSettings& s, bool on, float* l, float* r,
                 uint32_t n) override;
};
extern BitcrushEffect gBitcrushEffect;

}  // namespace bitcrush
}  // namespace audio
}  // namespace ot
