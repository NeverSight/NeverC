#include "MIRModuleArtifact.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Errc.h"
#include "llvm/Target/TargetMachine.h"
#include <new>

using namespace llvm;

namespace neverc::plugin {
namespace {

bool nonzero(NevercInterfaceID ID) {
  return ID.High != 0 || ID.Low != 0;
}

Error artifactError(const Twine &Message) {
  return createStringError(errc::invalid_argument, Message);
}

bool isConstructorList(const GlobalVariable &Global) {
  return Global.getName() == "llvm.global_ctors" ||
         Global.getName() == "llvm.global_dtors";
}

} // namespace

MIRModuleArtifact::MIRModuleArtifact(
    Module &Module, MachineModuleInfoWrapperPass &MMIValue,
    NevercTargetID TargetIDValue, StringRef CompatibilityKeyValue,
    StringRef SchemaDigestValue)
    : IRModule(&Module), MMI(&MMIValue), TargetID(TargetIDValue),
      CompatibilityKey(CompatibilityKeyValue.str()),
      SchemaDigest(SchemaDigestValue.str()) {}

Expected<std::unique_ptr<MIRModuleArtifact>>
MIRModuleArtifact::createOwned(Module &Module,
                               LLVMTargetMachine &TargetMachine,
                               NevercTargetID TargetID,
                               StringRef CompatibilityKey,
                               StringRef SchemaDigest) {
  auto Wrapper =
      std::make_unique<MachineModuleInfoWrapperPass>(&TargetMachine);
  Wrapper->doInitialization(Module);
  auto Artifact = std::unique_ptr<MIRModuleArtifact>(
      new (std::nothrow) MIRModuleArtifact(
          Module, *Wrapper, TargetID, CompatibilityKey, SchemaDigest));
  if (!Artifact) {
    Wrapper->doFinalization(Module);
    return createStringError(errc::not_enough_memory,
                             "MIR module artifact allocation failed");
  }
  Artifact->OwnedMMI = std::move(Wrapper);
  return Artifact;
}

std::unique_ptr<MIRModuleArtifact>
MIRModuleArtifact::borrow(Module &Module,
                          MachineModuleInfoWrapperPass &MMI,
                          NevercTargetID TargetID,
                          StringRef CompatibilityKey,
                          StringRef SchemaDigest) {
  return std::unique_ptr<MIRModuleArtifact>(
      new MIRModuleArtifact(Module, MMI, TargetID, CompatibilityKey,
                            SchemaDigest));
}

MIRModuleArtifact::~MIRModuleArtifact() {
  if (OwnedMMI && IRModule)
    OwnedMMI->doFinalization(*IRModule);
}

MachineModuleInfo &MIRModuleArtifact::machineModuleInfo() const {
  return MMI->getMMI();
}

MachineFunction &
MIRModuleArtifact::getOrCreateMachineFunction(Function &Function) {
  return machineModuleInfo().getOrCreateMachineFunction(Function);
}

MachineFunction *
MIRModuleArtifact::getMachineFunction(const Function &Function) const {
  return machineModuleInfo().getMachineFunction(Function);
}

Error MIRModuleArtifact::verify(bool RunMachineVerifier) const {
  if (!IRModule || !MMI || !nonzero(TargetID) ||
      CompatibilityKey.empty() || SchemaDigest.empty() ||
      Generation == 0)
    return artifactError("MIR module artifact identity is invalid");

  for (const GlobalVariable &Global : IRModule->globals()) {
    if (!Global.hasInitializer())
      continue;
    if (isConstructorList(Global)) {
      if (!Coverage.HandlesConstructors)
        return artifactError(
            "MIR provider did not declare constructor handling");
      continue;
    }
    if (!Coverage.HandlesGlobals)
      return artifactError("MIR provider did not declare global handling");
  }

  if (!IRModule->debug_compile_units().empty() &&
      !Coverage.HandlesDebugInfo)
    return artifactError("MIR provider did not declare debug-info handling");

  for (const Function &Function : *IRModule) {
    if (Function.isDeclaration())
      continue;
    if (Function.hasPersonalityFn() && !Coverage.HandlesUnwind)
      return artifactError("MIR provider did not declare unwind handling");
    MachineFunction *MF = getMachineFunction(Function);
    if (!MF)
      return artifactError("MIR provider omitted function '" +
                           Function.getName() + "'");
    if (&MF->getFunction() != &Function)
      return artifactError("MIR function has a foreign IR function");
    if (RunMachineVerifier &&
        !MF->verify(nullptr, "NeverC plugin final MIR verification",
                    /*AbortOnError=*/false))
      return artifactError("MIR module failed MachineVerifier");
  }
  return Error::success();
}

NevercInterfaceID mirModuleArtifactID() {
  return {NEVERC_PHASE_CODEGEN_IR_TO_MIR_OUTPUT_HIGH,
          NEVERC_PHASE_CODEGEN_IR_TO_MIR_OUTPUT_LOW};
}

Error registerMIRModuleArtifactType(PluginArtifactRegistry &Artifacts) {
  auto Registered = Artifacts.registerType(
      {mirModuleArtifactID(), "mir.module",
       PluginArtifactOwnership::Borrowed, {}, {},
       [](const void *Payload) -> Error {
         if (!Payload)
           return artifactError("MIR module artifact payload is null");
         return static_cast<const MIRModuleArtifact *>(Payload)->verify(false);
       }});
  if (!Registered)
    return Registered.takeError();
  return Error::success();
}

} // namespace neverc::plugin
