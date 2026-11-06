// TREM effect module: the Tremolo as a per-instance effect (modules/module.h).
#include "modules/module.h"
#include "tremolo.h"

namespace ot {
namespace mod {

namespace {

const ModuleInfo kInfo = {"TREM", kModuleApi, 100, "Tremolo", kCapEffect, 0, 0, 0, 3000, 3};

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::IEffect* effect(int i) override { return i == 0 ? &audio::tremolo::gTremoloEffect : nullptr; }
};

Module gModule;

}  // namespace

IModule& tremoloModule() { return gModule; }

}  // namespace mod
}  // namespace ot
