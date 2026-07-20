#ifndef NEVERC_PLUGIN_HOST_MACHINEEMISSIONBRIDGE_H
#define NEVERC_PLUGIN_HOST_MACHINEEMISSIONBRIDGE_H

#include "neverc/Plugin/Host/MCPluginBridge.h"
#include "llvm/Support/Error.h"
#include <memory>

namespace neverc::plugin {

class MIRModuleArtifact;

class MachineEmissionBridge {
public:
  static llvm::Expected<std::unique_ptr<MachineEmissionBridge>>
  create(PluginTaskContext &Task, MIRModuleArtifact &MIR,
         const PluginTargetSnapshot::NamedRecord *Schema);
  ~MachineEmissionBridge();

  MachineEmissionBridge(const MachineEmissionBridge &) = delete;
  MachineEmissionBridge &operator=(const MachineEmissionBridge &) = delete;

  MIRModuleArtifact &mir() const { return *MIR; }
  PluginMCUnit &unit() const { return *Unit; }
  MCPluginBridge &mc() const { return *MC; }
  const NevercMCAPI &api() const { return MC->api(); }
  llvm::Expected<NevercMCUnitHandle> unitHandle() { return MC->unit(); }

  llvm::Error verify();
  std::unique_ptr<PluginMCUnit> takeUnit();

private:
  MachineEmissionBridge(MIRModuleArtifact &MIR,
                        std::unique_ptr<PluginMCUnit> Unit,
                        std::unique_ptr<MCPluginBridge> MC,
                        const PluginTargetSnapshot::NamedRecord *Schema);

  MIRModuleArtifact *MIR;
  std::unique_ptr<PluginMCUnit> Unit;
  std::unique_ptr<MCPluginBridge> MC;
  const PluginTargetSnapshot::NamedRecord *Schema;
};

} // namespace neverc::plugin

#endif
