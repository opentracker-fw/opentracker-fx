// BBDC: a classic BBD stereo chorus (two fixed modes) (OpenTracker 3.x effect module,
// id BBDC), with the its two classic modes as its two effects: Chorus I and II.
//
// The DSP is the core's shared BBD chorus (audio/bbd_chorus.h; the JU6
// synth's built-in chorus runs the same code once per slot): the mono sum
// through the BBD filters and a soft clip into one int16 line, read by two
// taps on opposite triangle-LFO phases, mixed over the dry signal. As on the
// classic BBD chorus, one mode at a time; the page sets Chorus Off / I / II, Mix, Width
// (0 = both taps on both sides, 100 = one tap per side) and Tone. Our own
// design; textbook technique.
#pragma once

#include <cstdint>

#include "audio/audio_tables.h"
#include "audio/bbd_chorus.h"
#include "audio/inserts.h"

namespace ot {
namespace audio {
namespace bbdc {

constexpr int kLine = bbd::kLine;   // frames (5.8 ms), int16 (512 B of SRAM)

// What a chorus mode button fixes (bbd::kModes): LFO rate and the tap delay swing.
using Mode = bbd::Mode;
constexpr float kMs = bbd::kMs;
constexpr const Mode* kModes = bbd::kModes;

struct Params {
    uint8_t mode = 0;     // 0 Off, 1 Chorus I, 2 Chorus II
    uint8_t mix = 100;    // 0..100 (100: dry and wet equal, as on the classic BBD chorus)
    uint8_t width = 100;  // 0..100
    uint8_t tone = 50;    // 0..100 -> 6 .. 12 kHz (50: ~8.5 kHz)
};

// The BBD filters' cutoff for Tone t, Hz.
float toneHz(uint8_t t);

// The chorus as a per-instance effect (id BBDC, "Chorus"): Mode I / II / I+II,
// Mix, Width, Tone; state bbd::State, work the kLine int16 line.
class ChorusEffect final : public IEffect {
public:
    const char* id() const override { return "BBDC"; }
    const char* name() const override { return "Chorus"; }
    uint32_t stateBytes() const override { return sizeof(bbd::State); }
    uint32_t workBytes() const override { return uint32_t(kLine) * sizeof(int16_t); }
    int paramCount() const override { return 4; }
    const EffectParam& param(int i) const override;
    void valueText(int i, uint8_t v, char* out, int cap) const override;
    uint32_t tailFrames() const override { return bbd::kTailFrames; }
    bool process(void* state, void* work, const EffectSettings& s, bool on, float* l, float* r,
                 uint32_t n) override;
    void reset(void* state, void* work) override;
};
extern ChorusEffect gChorusEffect;

// The master insert: one ChorusEffect instance with Params (mode 0 = off).
class BbdChorus final : public EffectInsert {
public:
    // line: kLine zeroed frames the effect owns (static storage, .bss: a
    // buffer inside this object would also cost its size in flash, .data).
    constexpr explicit BbdChorus(int16_t* line)
        : EffectInsert(gChorusEffect, &st_, line, EffectSettings{{0, Params{}.mix, Params{}.width, Params{}.tone, 0, 0}}) {}
    // Main loop: takes effect at the next block (one-block hand-over).
    void setParams(const Params& p);
    Params params() const;
    // Tap delay of side c (0 = L, 1 = R) at the end of the last block, frames.
    float delay(int c) const { return c ? st_.dr : st_.dl; }

private:
    bbd::State st_ = {};
};

}  // namespace bbdc
}  // namespace audio
}  // namespace ot
