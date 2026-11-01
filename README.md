# opentracker-fx

Effect modules for [OpenTracker](https://github.com/opentracker-fw/opentracker) 3.x, the open-source firmware for the
original Polyend Tracker.

| Module | Id | What it does |
|---|---|---|
| `modules/chorus` | CHOR | stereo master-bus chorus: rate, depth, width (L/R LFO offset), mix |
| `modules/tape` | TAPE | master-bus tape effect: drive (saturation), head-loss high cut, wow and flutter, mix |
| `modules/dirt` | DIRT | master-bus drive / bitcrush: Soft / Hard / Fold / Asym shapers, Bits 4..16, sample-rate hold 1..32, Tone, Mix |
| `modules/echo` | ECHO | tempo-synced dub echo: 1/32 .. 1/2 of the song tempo, feedback, low cut, tone, ping-pong, duck, mix; a 32 KB half-rate line in the SDRAM (`ModuleHost::sdram()`) |

A build profile in the core lists modules by path relative to `MODULES_DIR`, so a profile can
take synths from `opentracker-synths` and effects from here, for example
`{ id = "TAPE", path = "../opentracker-fx/modules/tape" }`.

Module rules (core `src/core/modules/module.h`, API major 1): no static constructors, no heap,
audio code follows `audio/inserts.h` (no allocation, I/O or locks in `process`). Tests in
`tests/` are built and run by the core's `make host-test`.

License: MIT (LICENSE). Contains no Polyend code.
