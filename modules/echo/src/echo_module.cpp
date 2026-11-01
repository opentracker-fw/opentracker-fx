// ECHO effect module (id ECHO): master insert + project data + settings page
// (modules/module.h). The line comes from the host's SDRAM block
// (ModuleHost::sdram()); without it the module leaves itself out (the boot
// log names a refused module).
#include <cstdio>
#include <cstring>

#include "echo.h"
#include "modules/module.h"
#include "modules/module_ui.h"

namespace ot {
namespace mod {

namespace {

using audio::echo::Params;

const ModuleInfo kInfo = {"ECHO", kModuleApi, 100, "Echo", kCapMasterInsert | kCapSection | kCapPage, 0,
                          audio::echo::kLine * 2u, 0, 9000, 1};

audio::echo::Echo gEcho;
bool gDirty = false;

void set(const Params& p) {
    gEcho.setParams(p);
    gDirty = true;
}

// Project data v1: on, time, feedback, lowcut, tone, pingpong, duck, mix (8 B).
// Nothing is saved while the effect is Off at the default settings.
class Section final : public IProjectSection {
public:
    uint16_t tag() const override { return 0x4006; }
    uint16_t version() const override { return 1; }
    bool dirty() const override { return gDirty; }
    uint32_t encode(uint8_t* buf, uint32_t cap) override {
        gDirty = false;
        const Params p = gEcho.params();
        const uint8_t b[8] = {p.on, p.time, p.feedback, p.lowcut, p.tone, p.pingpong, p.duck, p.mix};
        const Params d;
        const uint8_t z[8] = {0, d.time, d.feedback, d.lowcut, d.tone, d.pingpong, d.duck, d.mix};
        if (std::memcmp(b, z, 8) == 0 || cap < 8) return 0;
        std::memcpy(buf, b, 8);
        return 8;
    }
    bool decode(const uint8_t* b, uint32_t len, uint16_t) override {
        Params p;
        uint8_t* const f[8] = {&p.on, &p.time, &p.feedback, &p.lowcut, &p.tone, &p.pingpong, &p.duck, &p.mix};
        for (uint32_t i = 0; i < 8 && i < len; ++i) *f[i] = b[i];
        gEcho.setParams(p);   // setParams clamps
        gDirty = false;
        return true;
    }
    void reset() override {
        gEcho.setParams(Params{});
        gDirty = false;
    }
};

// Settings page: Echo, Time, Feedback, Low Cut, Tone, Ping-Pong, Duck, Mix.
class Page final : public IModulePage {
public:
    const char* title() const override { return "Echo"; }
    int rows() const override { return 8; }
    void row(int i, ModuleRow& out) override {
        static const char* const kLabel[8] = {"Echo", "Time", "Feedback", "Low Cut", "Tone", "Ping-Pong", "Duck", "Mix"};
        static const uint8_t kHi[8] = {1, audio::echo::kTimes - 1, 100, 100, 100, 1, 100, 100};
        i = i < 0 || i > 7 ? 0 : i;
        const Params p = gEcho.params();
        const uint8_t v[8] = {p.on, p.time, p.feedback, p.lowcut, p.tone, p.pingpong, p.duck, p.mix};
        std::snprintf(out.label, sizeof(out.label), "%s", kLabel[i]);
        if (i == 0 || i == 5) {
            std::snprintf(out.value, sizeof(out.value), "%s", v[i] ? "On" : "Off");
        } else if (i == 1) {
            bool clamped = false;
            gEcho.seconds(p, gEcho.tempo(), &clamped);
            std::snprintf(out.value, sizeof(out.value), "%s%s", audio::echo::kTimeName[p.time], clamped ? " (max)" : "");
        } else {
            std::snprintf(out.value, sizeof(out.value), "%d", v[i]);
        }
        out.frac = float(v[i]) / float(kHi[i]);
        out.bipolar = false;
    }
    void adjust(int i, int d) override {
        Params p = gEcho.params();
        uint8_t* const f[8] = {&p.on, &p.time, &p.feedback, &p.lowcut, &p.tone, &p.pingpong, &p.duck, &p.mix};
        if (i < 0 || i > 7) return;
        if (i == 0 || i == 5) {
            *f[i] = d > 0 ? 1 : (d < 0 ? 0 : *f[i]);
        } else {
            const int n = int(*f[i]) + d;
            *f[i] = uint8_t(n < 0 ? 0 : (n > 255 ? 255 : n));   // setParams clamps to the range
        }
        set(p);
    }
};

Section gSection;
Page gPage;

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost& host) override {
        int16_t* line = static_cast<int16_t*>(host.sdram(audio::echo::kLine * sizeof(int16_t)));
        gEcho.setLine(line);   // zeroed .sdram_bss (host builds: .bss)
        return line != nullptr;
    }
    audio::IMasterInsert* masterInsert() override { return &gEcho; }
    IProjectSection* section() override { return &gSection; }
    IModulePage* page() override { return &gPage; }
};

Module gModule;

}  // namespace

IModule& echoModule() { return gModule; }

// Settings API (tests, the module page).
void echoSet(const audio::echo::Params& p) { set(p); }
audio::echo::Params echoGet() { return gEcho.params(); }

}  // namespace mod
}  // namespace ot
