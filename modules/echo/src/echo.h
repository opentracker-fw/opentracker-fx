// ECHO: a tempo-synced dub echo on the master bus (OpenTracker 3.x effect
// module, id ECHO, docs/PLAN_TECHNO.md §11).
//
// The bus's mono sum is decimated 2:1 into one int16 line in the SDRAM
// (kLine samples at half rate: 1.114 s; ModuleHost::sdram()), which the audio
// block reads and writes only as contiguous spans, once per block
// (audio/ring_io.h). The repeat time is a note division of the song tempo
// (setTempo); a time longer than the line holds is clamped ("(max)" on the
// page). Into the line go the input ([1 3 3 1] decimated) and the feedback
// (<= 0.98) through the loop's low cut (two one-pole high-passes, 20..500 Hz)
// and tone (a one-pole low-pass, 500 Hz..9 kHz; 100 = open) and a soft clip,
// so every repeat (in ping-pong: every pair of repeats) is darker and thinner
// than the one before. Ping-pong reads two taps of the
// one line (t and 2t: the odd repeats left, the even ones right; ping-pong
// times up to half the line); otherwise both sides play the t tap. Duck
// turns the repeats down while the input is loud (5 ms / 150 ms follower).
// Mix is the wet level added to the bus; the dry bus is untouched. A change
// of the time crossfades over one block. Off stops the input into the line
// and plays the repeats out; once they are quiet the line is cleared and the
// insert reports no tail. Our own design; textbook techniques.
#pragma once

#include <cstdint>

#include "audio/inserts.h"

namespace ot {
namespace audio {
namespace echo {

constexpr uint32_t kLine = 24576;   // half-rate samples (48 KB: 1.114 s; ping-pong 557 ms)
constexpr int kTimes = 10;
// Note divisions: 1/32, 1/16, 1/16., 1/8T, 1/8, 1/8., 1/4T, 1/4, 1/4., 1/2.
extern const char* const kTimeName[kTimes];
float timeBeats(uint8_t time);   // the division in quarter notes

struct Params {
    uint8_t on = 0;         // 0 / 1
    uint8_t time = 5;       // division index (1/8.: ping-pong fits from 81 BPM)
    uint8_t feedback = 45;  // 0..100 (x 0.98)
    uint8_t lowcut = 40;    // 0..100 = 20..500 Hz
    uint8_t tone = 50;      // 0..100 = 500 Hz..9 kHz (100: open)
    uint8_t pingpong = 1;   // 0 / 1
    uint8_t duck = 50;      // 0..100
    uint8_t mix = 35;       // 0..100 wet
};
Params clamped(const Params& p);
float lowcutHz(uint8_t v);
float toneHz(uint8_t v);

class Echo final : public IMasterInsert {
public:
    // The line (kLine int16, zeroed), or nullptr: the insert passes the bus through.
    void setLine(int16_t* line) { line_ = line; }
    bool hasLine() const { return line_ != nullptr; }
    // Main loop: takes effect at the next block (one-block hand-over).
    void setParams(const Params& p);
    Params params() const { return pub_[cur_]; }
    // The repeat time in seconds at the tempo, and whether the line clamps it.
    float seconds(const Params& p, float bpm, bool* clamped = nullptr) const;
    float tempo() const { return bpm_; }
    void reset();   // clears the state (not the line); audio context or before audio starts
    // setLine() too: before audio starts. process() needs n == kBlock (else it passes the bus through).
    void setTempo(float bpm) override;
    bool process(float* l, float* r, uint32_t n) override;

private:
    uint32_t tapOf(const Params& p) const;   // half-rate delay of the t tap
    int16_t* line_ = nullptr;
    Params pub_[2];
    volatile uint8_t cur_ = 0;
    float bpm_ = 130.0f;      // the engine's default tempo until setTempo
    uint32_t w_ = 0;          // line write position
    uint32_t t_ = 0;          // the tap the last block used (0: none yet)
    uint8_t pp_ = 0;          // ping-pong the last block used
    float hp1_ = 0.0f, hp2_ = 0.0f, lp_ = 0.0f;   // loop filters (half rate)
    float inPrev_[2] = {};    // the input decimator: the last block's last two mono frames
    float upA_ = 0.0f, upB_ = 0.0f;   // upsampling: the last half-rate wet samples
    float wet_ = 0.0f, fb_ = 0.0f;    // wet level / feedback the last block ended on
    float env_ = 0.0f, duckG_ = 1.0f; // duck follower, the duck gain the last block ended on
    uint8_t inOn_ = 0;        // the input fed the line in the last block
    uint32_t quiet_ = 0;      // half-rate samples of quiet repeats
    uint32_t clear_ = 0;      // line samples left to clear (after Off)
    bool running_ = false;    // the line holds sound
};

}  // namespace echo
}  // namespace audio
}  // namespace ot
