#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Error.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

Error mismatch(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(),
                           "built-in target route mismatch: " + Message);
}

} // namespace

Expected<const Target *>
lookupBuiltinLLVMTarget(const BuiltinTargetRoute &Route) {
  std::string Message;
  const Target *Result =
      TargetRegistry::lookupTarget(Route.CanonicalTriple, Message);
  if (!Result)
    return createStringError(
        inconvertibleErrorCode(),
        "built-in LLVM target '" + Route.CanonicalName +
            "' is unavailable: " + Message);
  return Result;
}

Error validateBuiltinTargetPipeline(
    const BuiltinTargetRoute &Route, const Module &ModuleValue,
    const TargetMachine &Machine, StringRef RequestedCPU,
    StringRef RequestedFeatures) {
  const BuiltinTargetRoute *ModuleRoute =
      findBuiltinTargetRoute(ModuleValue.getTargetTriple());
  if (!ModuleRoute || !sameID(ModuleRoute->TargetID, Route.TargetID))
    return mismatch("IR module triple '" +
                    ModuleValue.getTargetTriple() +
                    "' does not select '" + Route.CanonicalName + "'");

  const BuiltinTargetRoute *MachineRoute =
      findBuiltinTargetRoute(Machine.getTargetTriple().str());
  if (!MachineRoute || !sameID(MachineRoute->TargetID, Route.TargetID))
    return mismatch("LLVM TargetMachine triple '" +
                    Machine.getTargetTriple().str() +
                    "' does not select '" + Route.CanonicalName + "'");
  if (MachineRoute->ObjectFormat != Route.ObjectFormat ||
      !sameID(MachineRoute->ObjectFormatID, Route.ObjectFormatID))
    return mismatch("object format changed between route and TargetMachine");

  const DataLayout MachineLayout = Machine.createDataLayout();
  if (ModuleValue.getDataLayout() != MachineLayout)
    return mismatch("frontend/module DataLayout '" +
                    ModuleValue.getDataLayout().getStringRepresentation() +
                    "' differs from TargetMachine DataLayout '" +
                    MachineLayout.getStringRepresentation() + "'");

  if (Machine.getTargetCPU() != RequestedCPU)
    return mismatch("CPU '" + Machine.getTargetCPU() +
                    "' differs from requested CPU '" + RequestedCPU + "'");
  if (Machine.getTargetFeatureString() != RequestedFeatures)
    return mismatch("features '" + Machine.getTargetFeatureString() +
                    "' differ from requested features '" +
                    RequestedFeatures + "'");
  return Error::success();
}

} // namespace neverc::plugin
