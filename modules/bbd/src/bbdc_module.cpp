// BBDC effect module (id BBDC): master insert, project data and a settings
// page (modules/module.h, modules/module_ui.h).
#include <cstdio>
#include <cstring>

#include "bbdc.h"
#include "modules/module.h"
#include "modules/module_ui.h"

namespace ot {
namespace mod {

namespace {

const ModuleInfo kInfo = {"BBDC", kModuleApi, 100, "BBD chorus", kCapMasterInsert | kCapSection | kCapPage, 512, 0, 0, 12000, 1};

int16_t gLine[audio::bbdc::kLine];   // the delay line (.bss)

// Project data v1: mode, mix, width, tone, 4 reserved (8 B). Nothing is saved
// while the effect is Off at the default settings.
class Module final : public IModule, public IProjectSection, public IModulePage {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::IMasterInsert* masterInsert() override { return &fx_; }
    IProjectSection* section() override { return this; }
    IModulePage* page() override { return this; }

    uint16_t tag() const override { return 0x4003; }
    uint16_t version() const override { return 1; }
    bool dirty() const override { return dirty_; }
    uint32_t encode(uint8_t* buf, uint32_t cap) override {
        dirty_ = false;
        const audio::bbdc::Params p = fx_.params();
        const audio::bbdc::Params d;
        if (!p.mode && p.mix == d.mix && p.width == d.width && p.tone == d.tone) return 0;
        if (cap < 8) return 0;
        const uint8_t b[8] = {p.mode, p.mix, p.width, p.tone, 0, 0, 0, 0};
        std::memcpy(buf, b, 8);
        return 8;
    }
    bool decode(const uint8_t* b, uint32_t len, uint16_t) override {
        audio::bbdc::Params p;
        auto take = [&](uint32_t i, uint8_t& v, uint8_t max) {
            if (len > i) v = b[i] > max ? max : b[i];
        };
        take(0, p.mode, 2);
        take(1, p.mix, 100);
        take(2, p.width, 100);
        take(3, p.tone, 100);
        fx_.setParams(p);
        dirty_ = false;
        return true;
    }
    void reset() override {
        fx_.setParams(audio::bbdc::Params{});
        dirty_ = false;
    }

    // Settings page: Chorus Off / I / II, Mix, Width, Tone (Shift + Master).
    const char* title() const override { return "BBD"; }
    int rows() const override { return 4; }
    void row(int i, ModuleRow& out) override {
        static const char* const kLabel[4] = {"Chorus", "Mix", "Width", "Tone"};
        static const char* const kMode[3] = {"Off", "I", "II"};
        const audio::bbdc::Params p = fx_.params();
        const uint8_t v[4] = {p.mode, p.mix, p.width, p.tone};
        std::snprintf(out.label, sizeof(out.label), "%s", kLabel[i]);
        if (i == 0) std::snprintf(out.value, sizeof(out.value), "%s", kMode[p.mode]);
        else if (i == 3) std::snprintf(out.value, sizeof(out.value), "%.1f kHz", double(audio::bbdc::toneHz(p.tone)) * 0.001);
        else std::snprintf(out.value, sizeof(out.value), "%d", v[i]);
        out.frac = i == 0 ? 0.5f * float(p.mode) : float(v[i]) * 0.01f;
        out.bipolar = false;
    }
    void adjust(int i, int d) override {
        audio::bbdc::Params p = fx_.params();
        auto step = [d](uint8_t& x, int max) {
            const int n = int(x) + d;
            x = uint8_t(n < 0 ? 0 : (n > max ? max : n));
        };
        switch (i) {
            case 0: step(p.mode, 2); break;
            case 1: step(p.mix, 100); break;
            case 2: step(p.width, 100); break;
            case 3: step(p.tone, 100); break;
            default: return;
        }
        set(p);
    }

    void set(audio::bbdc::Params p) {
        if (p.mode > 2) p.mode = 2;
        fx_.setParams(p);
        dirty_ = true;
    }
    audio::bbdc::Params get() const { return fx_.params(); }

private:
    audio::bbdc::BbdChorus fx_{gLine};
    bool dirty_ = false;
};

Module gModule;

}  // namespace

IModule& bbdcModule() { return gModule; }

void bbdcSet(const audio::bbdc::Params& p) { gModule.set(p); }
audio::bbdc::Params bbdcGet() { return gModule.get(); }

}  // namespace mod
}  // namespace ot
