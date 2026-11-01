// TAPE effect module (id TAPE): master insert + project data (modules/module.h).
#include <cstdio>
#include <cstring>

#include "modules/module.h"
#include "modules/module_ui.h"
#include "tape.h"

namespace ot {
namespace mod {

namespace {

const ModuleInfo kInfo = {"TAPE", kModuleApi, 100, "Tape", kCapMasterInsert | kCapSection | kCapPage, 0, 0, 0, 15000, 1};

// Project data v1: on, drive, wow, flutter, mix, 3 reserved (8 B). Nothing is
// saved while the effect is Off at the default settings.
class Module final : public IModule, public IProjectSection, public IModulePage {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::IMasterInsert* masterInsert() override { return &tape_; }
    IProjectSection* section() override { return this; }
    IModulePage* page() override { return this; }

    // Settings page: On, Drive, Wow, Flutter, Mix (Master key cycle).
    const char* title() const override { return "Tape"; }
    int rows() const override { return 5; }
    void row(int i, ModuleRow& out) override {
        static const char* const kLabel[5] = {"Tape", "Drive", "Wow", "Flutter", "Mix"};
        const audio::tape::Params p = tape_.params();
        const uint8_t v[5] = {p.on, p.drive, p.wow, p.flutter, p.mix};
        std::snprintf(out.label, sizeof(out.label), "%s", kLabel[i]);
        if (i == 0) std::snprintf(out.value, sizeof(out.value), "%s", p.on ? "On" : "Off");
        else std::snprintf(out.value, sizeof(out.value), "%d", v[i]);
        out.frac = i == 0 ? float(p.on) : float(v[i]) * 0.01f;
        out.bipolar = false;
    }
    void adjust(int i, int d) override {
        audio::tape::Params p = tape_.params();
        auto step = [d](uint8_t& x) {
            const int n = int(x) + d;
            x = uint8_t(n < 0 ? 0 : (n > 100 ? 100 : n));
        };
        switch (i) {
            case 0: p.on = d > 0 ? 1 : (d < 0 ? 0 : p.on); break;
            case 1: step(p.drive); break;
            case 2: step(p.wow); break;
            case 3: step(p.flutter); break;
            case 4: step(p.mix); break;
            default: return;
        }
        set(p);
    }

    uint16_t tag() const override { return 0x4001; }
    uint16_t version() const override { return 1; }
    bool dirty() const override { return dirty_; }
    uint32_t encode(uint8_t* buf, uint32_t cap) override {
        dirty_ = false;
        const audio::tape::Params p = tape_.params();
        const audio::tape::Params d;
        if (!p.on && p.drive == d.drive && p.wow == d.wow && p.flutter == d.flutter && p.mix == d.mix) return 0;
        if (cap < 8) return 0;
        const uint8_t b[8] = {p.on, p.drive, p.wow, p.flutter, p.mix, 0, 0, 0};
        std::memcpy(buf, b, 8);
        return 8;
    }
    bool decode(const uint8_t* b, uint32_t len, uint16_t) override {
        audio::tape::Params p;
        auto take = [&](uint32_t i, uint8_t& v, uint8_t max) {
            if (len > i) v = b[i] > max ? max : b[i];
        };
        take(0, p.on, 1);
        take(1, p.drive, 100);
        take(2, p.wow, 100);
        take(3, p.flutter, 100);
        take(4, p.mix, 100);
        tape_.setParams(p);
        dirty_ = false;
        return true;
    }
    void reset() override {
        tape_.setParams(audio::tape::Params{});
        dirty_ = false;
    }

    void set(const audio::tape::Params& p) {
        tape_.setParams(p);
        dirty_ = true;
    }
    audio::tape::Params get() const { return tape_.params(); }

private:
    audio::tape::Tape tape_;
    bool dirty_ = false;
};

Module gModule;

}  // namespace

IModule& tapeModule() { return gModule; }

// Settings API (tests, the module page).
void tapeSet(const audio::tape::Params& p) { gModule.set(p); }
audio::tape::Params tapeGet() { return gModule.get(); }

}  // namespace mod
}  // namespace ot
