// CHORUS effect module (id CHOR): master insert, project data and a settings
// page (modules/module.h, modules/module_ui.h).
#include <cstdio>
#include <cstring>

#include "chorus.h"
#include "modules/module.h"
#include "modules/module_ui.h"

namespace ot {
namespace mod {

namespace {

const ModuleInfo kInfo = {"CHOR", kModuleApi, 100, "Chorus", kCapMasterInsert | kCapSection | kCapPage, 1024, 0, 0, 12000, 1};

// Project data v1: on, rate, depth, width, mix, 3 reserved (8 B). Nothing is
// saved while the effect is Off at the default settings.
class Module final : public IModule, public IProjectSection, public IModulePage {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::IMasterInsert* masterInsert() override { return &fx_; }
    IProjectSection* section() override { return this; }
    IModulePage* page() override { return this; }

    uint16_t tag() const override { return 0x4002; }
    uint16_t version() const override { return 1; }
    bool dirty() const override { return dirty_; }
    uint32_t encode(uint8_t* buf, uint32_t cap) override {
        dirty_ = false;
        const audio::chorus::Params p = fx_.params();
        const audio::chorus::Params d;
        if (!p.on && p.rate == d.rate && p.depth == d.depth && p.width == d.width && p.mix == d.mix) return 0;
        if (cap < 8) return 0;
        const uint8_t b[8] = {p.on, p.rate, p.depth, p.width, p.mix, 0, 0, 0};
        std::memcpy(buf, b, 8);
        return 8;
    }
    bool decode(const uint8_t* b, uint32_t len, uint16_t) override {
        audio::chorus::Params p;
        auto take = [&](uint32_t i, uint8_t& v, uint8_t max) {
            if (len > i) v = b[i] > max ? max : b[i];
        };
        take(0, p.on, 1);
        take(1, p.rate, 100);
        take(2, p.depth, 100);
        take(3, p.width, 100);
        take(4, p.mix, 100);
        fx_.setParams(p);
        dirty_ = false;
        return true;
    }
    void reset() override {
        fx_.setParams(audio::chorus::Params{});
        dirty_ = false;
    }

    // Settings page: Chorus On, Rate, Depth, Width, Mix (Shift + Master).
    const char* title() const override { return "Chorus"; }
    int rows() const override { return 5; }
    void row(int i, ModuleRow& out) override {
        static const char* const kLabel[5] = {"Chorus", "Rate", "Depth", "Width", "Mix"};
        const audio::chorus::Params p = fx_.params();
        const uint8_t v[5] = {p.on, p.rate, p.depth, p.width, p.mix};
        std::snprintf(out.label, sizeof(out.label), "%s", kLabel[i]);
        if (i == 0) std::snprintf(out.value, sizeof(out.value), "%s", p.on ? "On" : "Off");
        else if (i == 1) std::snprintf(out.value, sizeof(out.value), "%.2f Hz", double(0.1f + 0.049f * float(p.rate)));
        else std::snprintf(out.value, sizeof(out.value), "%d", v[i]);
        out.frac = i == 0 ? float(p.on) : float(v[i]) * 0.01f;
        out.bipolar = false;
    }
    void adjust(int i, int d) override {
        audio::chorus::Params p = fx_.params();
        auto step = [d](uint8_t& x) {
            const int n = int(x) + d;
            x = uint8_t(n < 0 ? 0 : (n > 100 ? 100 : n));
        };
        switch (i) {
            case 0: p.on = d > 0 ? 1 : (d < 0 ? 0 : p.on); break;
            case 1: step(p.rate); break;
            case 2: step(p.depth); break;
            case 3: step(p.width); break;
            case 4: step(p.mix); break;
            default: return;
        }
        set(p);
    }

    void set(const audio::chorus::Params& p) {
        fx_.setParams(p);
        dirty_ = true;
    }
    audio::chorus::Params get() const { return fx_.params(); }

private:
    audio::chorus::Chorus fx_;
    bool dirty_ = false;
};

Module gModule;

}  // namespace

IModule& chorusModule() { return gModule; }

void chorusSet(const audio::chorus::Params& p) { gModule.set(p); }
audio::chorus::Params chorusGet() { return gModule.get(); }

}  // namespace mod
}  // namespace ot
