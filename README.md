# opentracker-fx

Effect modules for [OpenTracker](https://github.com/sandlbn/polyend-tracker-re) 3.x, the open-source firmware for the
original Polyend Tracker.

| Module | Id | What it does |
|---|---|---|
| `modules/chorus` | CHOR | stereo master-bus chorus: rate, depth, width (L/R LFO offset), mix |
| `modules/tape` | TAPE | master-bus tape effect: drive (saturation), head-loss high cut, wow and flutter, mix |
| `modules/dirt` | DIRT | master-bus drive / bitcrush: Soft / Hard / Fold / Asym shapers, Bits 4..16, sample-rate hold 1..32, Tone, Mix |
| `modules/lofi` | LOFI | chip DAC / speaker master insert: Bits 4..16 with a Ladder character, a sample-and-hold Rate 32 .. 4 kHz, Speaker shapes (TV, Handheld, Monitor), Mono, Mix |
| `modules/echo` | ECHO | tempo-synced dub echo: 1/32 .. 1/2 of the song tempo, feedback, low cut, tone, ping-pong, duck, mix; a 32 KB half-rate line in the SDRAM (`ModuleHost::sdram()`) |
| `modules/bbd` | BBDC | classic BBD chorus: Chorus Off / I (0.5 Hz, 2.0..5.0 ms) / II (0.8 Hz, 1.5..5.5 ms), mix, width, tone |
| `modules/phaser` | PHSR | synth slot insert (effect PHAS): Mode Classic (4 stages, 140 Hz .. 2.4 kHz, a sweep that lingers low) / 80s (8 stages, 90 Hz .. 4.2 kHz, smooth sweep), Rate 0.05..8 Hz, Depth, Feedback, Spread, Mix |
| `modules/tremolo` | TREM | synth slot insert (effect TREM): Mode Level / Pan (constant power) / Both, Rate 0.1..20 Hz, Depth, Shape Sine / Tri / Square (2 ms edges) |
| `modules/bitcrush` | CRSH | synth slot insert (effect CRSH): Shape Hard / Soft, Crush (16 .. 4 bits), Down (hold 1..32 frames), Mix |

A build profile in the core lists modules by path relative to `MODULES_DIR`, so a profile can
take synths from `opentracker-synths` and effects from here, for example
`{ id = "TAPE", path = "../opentracker-fx/modules/tape" }`.

Module rules (core `src/core/modules/module.h`, API major 1): no static constructors, no heap,
audio code follows `audio/inserts.h` (no allocation, I/O or locks in `process`). Tests in
`tests/` are built and run by the core's `make host-test`.

License: MIT (LICENSE). Contains no Polyend code.
