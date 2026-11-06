// TREM: a tremolo / auto-pan as a per-instance effect (OpenTracker 3.x effect
// module, effect id TREM, "Tremolo"), for the synth slot inserts.
//
// One LFO (Rate 0.1 .. 20 Hz) of a Shape (Sine; Tri; Square with 2 ms edges, no
// click) moves the level (Level: 1 - Depth .. 1), the stereo position
// (Pan: constant power, unity in the middle) or both. The gains are computed
// every 32 frames and ramped in between; the on / off fade ramps across the
// block, and a change of Mode, Shape or Depth crossfades the old gain curve
// into the new one across the block (no click). Off fades out over one block
// and then passes the bus through untouched. No buffers. Our own design;
// textbook techniques.
#pragma once

#include <cstdint>

#include "audio/inserts.h"

namespace ot {
namespace audio {
namespace tremolo {

enum Mode : uint8_t { kLevel = 0, kPan, kBoth, kModes };
enum Shape : uint8_t { kSine = 0, kTri, kSquare, kShapes };
extern const char* const kModeName[kModes];
extern const char* const kShapeName[kShapes];

constexpr uint32_t kStep = 32;   // frames per gain update

// One instance's state (all zero = fresh).
struct Inst {
    uint32_t phase;           // LFO phase (one cycle = 2^32)
    float gl, gr;             // the gains the last block ended on (on / off fade included)
    float wet;                // the on / off amount the last block ended on
    float depth;              // the last block's Depth (a change crossfades the gain curves)
    uint8_t mode, shape;      // the last block's Mode / Shape
    uint8_t ran;              // 1 once a block ran
};

// The LFO rate (Hz) of a Rate value 0..100.
float rateHz(uint8_t rate);
// The LFO at a phase, -1..1.
float lfo(uint32_t phase, Shape shape, float hz);

class TremoloEffect final : public IEffect {
public:
    const char* id() const override { return "TREM"; }
    const char* name() const override { return "Tremolo"; }
    uint32_t stateBytes() const override { return sizeof(Inst); }
    int paramCount() const override { return 4; }
    const EffectParam& param(int i) const override;
    bool process(void* state, void* work, const EffectSettings& s, bool on, float* l, float* r,
                 uint32_t n) override;
};
extern TremoloEffect gTremoloEffect;

}  // namespace tremolo
}  // namespace audio
}  // namespace ot
