#include "neverc/Plugin/Host/MachineEmissionBridge.h"
#include "../MIR/MIRModuleArtifact.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Errc.h"

using namespace llvm;

namespace neverc::plugin {

MachineEmissionBridge::MachineEmissionBridge(
    MIRModuleArtifact &MIRValue, std::unique_ptr<PluginMCUnit> UnitValue,
    std::unique_ptr<MCPluginBridge> MCValue,
    const PluginTargetSnapshot::NamedRecord *SchemaValue)
    : MIR(&MIRValue), Unit(std::move(UnitValue)), MC(std::move(MCValue)),
      Schema(SchemaValue) {}

Expected<std::unique_ptr<MachineEmissionBridge>>
MachineEmissionBridge::create(
    PluginTaskContext &Task, MIRModuleArtifact &MIR,
    const PluginTargetSnapshot::NamedRecord *Schema) {
  if (!Schema || Schema->Digest.empty())
    return createStringError(errc::invalid_argument,
                             "machine emission requires a target schema");
  auto Unit = std::make_unique<PluginMCUnit>();
  auto MC = std::make_unique<MCPluginBridge>(Task, *Unit, Schema);
  return std::unique_ptr<MachineEmissionBridge>(
      new MachineEmissionBridge(MIR, std::move(Unit), std::move(MC),
                                Schema));
}

MachineEmissionBridge::~MachineEmissionBridge() = default;

Error MachineEmissionBridge::verify() {
  if (!MIR || !Unit || !MC || !Schema ||
      MIR->schemaDigest() != Schema->Digest)
    return createStringError(
        errc::invalid_argument,
        "MC product target schema does not match its MIR input");

  bool HasDefinition = false;
  for (const Function &Function : MIR->module())
    HasDefinition |= !Function.isDeclaration();
  if (HasDefinition && Unit->size() == 0)
    return createStringError(errc::invalid_argument,
                             "MIR-to-MC provider emitted no instructions");

  for (const std::unique_ptr<MCInst> &Instruction :
       Unit->instructions()) {
    if (!Instruction)
      return createStringError(errc::invalid_argument,
                               "MC product contains a null instruction");
    auto StableOpcode = MC->stableOpcode(Instruction->getOpcode());
    if (!StableOpcode)
      return StableOpcode.takeError();
    for (const MCOperand &Operand : *Instruction) {
      if (!Operand.isReg() || Operand.getReg() == 0)
        continue;
      auto StableRegister = MC->stableRegister(Operand.getReg());
      if (!StableRegister)
        return StableRegister.takeError();
    }
  }
  return Error::success();
}

std::unique_ptr<PluginMCUnit> MachineEmissionBridge::takeUnit() {
  MC.reset();
  return std::move(Unit);
}

} // namespace neverc::plugin
