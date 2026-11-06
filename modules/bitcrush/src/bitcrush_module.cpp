// CRSH effect module: the Bitcrush as a per-instance effect (modules/module.h).
#include "bitcrush.h"
#include "modules/module.h"

namespace ot {
namespace mod {

namespace {

const ModuleInfo kInfo = {"CRSH", kModuleApi, 100, "Bitcrush", kCapEffect, 0, 0, 0, 9500, 3};

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::IEffect* effect(int i) override { return i == 0 ? &audio::bitcrush::gBitcrushEffect : nullptr; }
};

Module gModule;

}  // namespace

IModule& bitcrushModule() { return gModule; }

}  // namespace mod
}  // namespace ot
