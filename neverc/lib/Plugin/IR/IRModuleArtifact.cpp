#include "IRModuleArtifact.h"
#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include <new>

using namespace llvm;

namespace neverc::plugin {
namespace {

bool nonnull(NevercInterfaceID ID) { return ID.High != 0 || ID.Low != 0; }

Expected<void *> cloneIRModuleArtifact(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "IR module artifact payload is null");
  auto *Clone = new (std::nothrow)
      IRModuleArtifact(*static_cast<const IRModuleArtifact *>(Payload));
  if (!Clone)
    return createStringError(inconvertibleErrorCode(),
                             "IR module artifact allocation failed");
  return static_cast<void *>(Clone);
}

Error verifyIRModuleArtifact(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "IR module artifact payload is null");
  const auto &Artifact = *static_cast<const IRModuleArtifact *>(Payload);
  if ((!Artifact.Bridge && !Artifact.BorrowedModule) ||
      (Artifact.Bridge && Artifact.BorrowedModule))
    return createStringError(inconvertibleErrorCode(),
                             "IR module artifact has invalid module ownership");
  if (!nonnull(Artifact.Product))
    return createStringError(inconvertibleErrorCode(),
                             "IR module artifact has no product ID");
  if (Artifact.Generation == 0 ||
      (Artifact.Bridge &&
       Artifact.Generation != Artifact.Bridge->mutationGeneration()))
    return createStringError(inconvertibleErrorCode(),
                             "IR module artifact generation is stale");
  if (!Artifact.HasDependencyDigest)
    return createStringError(inconvertibleErrorCode(),
                             "IR module artifact has no dependency digest");

  const Module &Module = *getIRModule(Artifact);
  if (Artifact.TargetTriple.empty() ||
      Module.getTargetTriple() != Artifact.TargetTriple)
    return createStringError(inconvertibleErrorCode(),
                             "IR module artifact target triple is invalid");
  if (Artifact.DataLayout.empty() ||
      Module.getDataLayoutStr() != Artifact.DataLayout)
    return createStringError(inconvertibleErrorCode(),
                             "IR module artifact data layout is invalid");
  if (verifyModule(Module))
    return createStringError(inconvertibleErrorCode(),
                             "IR module artifact failed LLVM verification");
  return Error::success();
}

} // namespace

NevercInterfaceID irGeneratePhaseID() {
  return {NEVERC_PHASE_IR_GENERATE_HIGH, NEVERC_PHASE_IR_GENERATE_LOW};
}

NevercInterfaceID irOptimizePhaseID() {
  return {NEVERC_PHASE_IR_OPTIMIZE_HIGH, NEVERC_PHASE_IR_OPTIMIZE_LOW};
}

NevercInterfaceID irModuleArtifactID() {
  return {NEVERC_PHASE_IR_GENERATE_OUTPUT_HIGH,
          NEVERC_PHASE_IR_GENERATE_OUTPUT_LOW};
}

NevercInterfaceID optimizedIRModuleArtifactID() {
  return {NEVERC_PHASE_IR_OPTIMIZE_OUTPUT_HIGH,
          NEVERC_PHASE_IR_OPTIMIZE_OUTPUT_LOW};
}

NevercInterfaceID standardIRModuleProductID() {
  return irModuleArtifactID();
}

Module *getIRModule(IRModuleArtifact &Artifact) {
  return Artifact.Bridge ? &Artifact.Bridge->module()
                         : Artifact.BorrowedModule;
}

const Module *getIRModule(const IRModuleArtifact &Artifact) {
  return Artifact.Bridge ? &Artifact.Bridge->module()
                         : Artifact.BorrowedModule;
}

Error registerIRModuleArtifactType(PluginArtifactRegistry &Artifacts) {
  auto Type = Artifacts.registerType(
      {irModuleArtifactID(), "ir.module", PluginArtifactOwnership::Owned,
       cloneIRModuleArtifact,
       [](void *Payload) {
         delete static_cast<IRModuleArtifact *>(Payload);
       },
       verifyIRModuleArtifact});
  if (!Type)
    return Type.takeError();
  return Error::success();
}

Error registerOptimizedIRModuleArtifactType(
    PluginArtifactRegistry &Artifacts) {
  auto Type = Artifacts.registerType(
      {optimizedIRModuleArtifactID(), "ir.optimized_module",
       PluginArtifactOwnership::Owned, cloneIRModuleArtifact,
       [](void *Payload) {
         delete static_cast<IRModuleArtifact *>(Payload);
       },
       verifyIRModuleArtifact});
  if (!Type)
    return Type.takeError();
  return Error::success();
}

} // namespace neverc::plugin
