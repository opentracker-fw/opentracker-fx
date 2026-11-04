// LOFI effect module (id LOFI): master insert + project data + settings page
// (modules/module.h).
#include <cstdio>
#include <cstring>

#include "lofi.h"
#include "modules/module.h"
#include "modules/module_ui.h"

namespace ot {
namespace mod {

namespace {

using audio::lofi::Params;

const ModuleInfo kInfo = {"LOFI", kModuleApi, 100, "LoFi", kCapMasterInsert | kCapSection | kCapPage, 0, 0, 0, 12500, 1};

audio::lofi::Lofi gLofi;
bool gDirty = false;

void set(const Params& p) {
    gLofi.setParams(p);
    gDirty = true;
}

// Project data v1: on, bits, rate, ladder, speaker, mono, mix, 1 reserved (8 B).
// Nothing is saved while the effect is Off at the default settings.
class Section final : public IProjectSection {
public:
    uint16_t tag() const override { return 0x4007; }
    uint16_t version() const override { return 1; }
    bool dirty() const override { return gDirty; }
    uint32_t encode(uint8_t* buf, uint32_t cap) override {
        gDirty = false;
        const Params p = gLofi.params();
        const uint8_t b[8] = {p.on, p.bits, p.rate, p.ladder, p.speaker, p.mono, p.mix, 0};
        const Params d;
        const uint8_t z[8] = {0, d.bits, d.rate, d.ladder, d.speaker, d.mono, d.mix, 0};
        if (std::memcmp(b, z, 8) == 0 || cap < 8) return 0;
        std::memcpy(buf, b, 8);
        return 8;
    }
    bool decode(const uint8_t* b, uint32_t len, uint16_t) override {
        Params p;
        uint8_t* const f[7] = {&p.on, &p.bits, &p.rate, &p.ladder, &p.speaker, &p.mono, &p.mix};
        for (uint32_t i = 0; i < 7 && i < len; ++i) *f[i] = b[i];
        gLofi.setParams(p);   // setParams clamps
        gDirty = false;
        return true;
    }
    void reset() override {
        gLofi.setParams(Params{});
        gDirty = false;
    }
};

// Settings page: LoFi, Bits, Rate, Ladder, Speaker, Mono, Mix (Master key cycle).
class Page final : public IModulePage {
public:
    const char* title() const override { return "LoFi"; }
    int rows() const override { return 7; }
    void row(int i, ModuleRow& out) override {
        static const char* const kLabel[7] = {"LoFi", "Bits", "Rate", "Ladder", "Speaker", "Mono", "Mix"};
        static const uint8_t kLo[7] = {0, 4, 0, 0, 0, 0, 0}, kHi[7] = {1, 16, 7, 100, 3, 1, 100};
        i = i < 0 || i > 6 ? 0 : i;
        const Params p = gLofi.params();
        const uint8_t v[7] = {p.on, p.bits, p.rate, p.ladder, p.speaker, p.mono, p.mix};
        std::snprintf(out.label, sizeof(out.label), "%s", kLabel[i]);
        if (i == 0 || i == 5) std::snprintf(out.value, sizeof(out.value), "%s", v[i] ? "On" : "Off");
        else if (i == 1 && p.bits >= 16) std::snprintf(out.value, sizeof(out.value), "%s", "Off");
        else if (i == 2) std::snprintf(out.value, sizeof(out.value), "%s", audio::lofi::kRateName[p.rate]);
        else if (i == 4) std::snprintf(out.value, sizeof(out.value), "%s", audio::lofi::kSpeakerName[p.speaker]);
        else std::snprintf(out.value, sizeof(out.value), "%d", v[i]);
        out.frac = float(v[i] - kLo[i]) / float(kHi[i] - kLo[i]);
        out.bipolar = false;
    }
    void adjust(int i, int d) override {
        Params p = gLofi.params();
        uint8_t* const f[7] = {&p.on, &p.bits, &p.rate, &p.ladder, &p.speaker, &p.mono, &p.mix};
        if (i < 0 || i > 6) return;
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
    bool attach(ModuleHost&) override { return true; }
    audio::IMasterInsert* masterInsert() override { return &gLofi; }
    IProjectSection* section() override { return &gSection; }
    IModulePage* page() override { return &gPage; }
};

Module gModule;

}  // namespace

IModule& lofiModule() { return gModule; }

// Settings API (tests, the module page).
void lofiSet(const audio::lofi::Params& p) { set(p); }
audio::lofi::Params lofiGet() { return gLofi.params(); }

}  // namespace mod
}  // namespace ot
