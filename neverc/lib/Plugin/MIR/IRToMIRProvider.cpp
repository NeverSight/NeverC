#include "neverc/Plugin/Host/IRToMIRProvider.h"
#include "MIRModuleArtifact.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Errc.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

bool nonzero(NevercInterfaceID ID) {
  return ID.High != 0 || ID.Low != 0;
}

Error providerError(const Twine &Message) {
  return createStringError(errc::invalid_argument, Message);
}

} // namespace

Expected<std::unique_ptr<MIRModuleArtifact>>
IRToMIRProviderRuntime::execute(
    const IRToMIRExecutionRequest &Request,
    ReplacementProvider Replacement, BuiltinProvider Builtin) {
  if (!Request.Module || !Request.TargetMachine ||
      !nonzero(Request.TargetID) || Request.CompatibilityKey.empty() ||
      Request.SchemaDigest.empty() || !Request.Coverage)
    return providerError("invalid IR-to-MIR execution request");
  if (!Request.HasFinalIRProof)
    return providerError(
        "IR-to-MIR provider requires a sealed final IR proof");

  std::unique_ptr<MIRModuleArtifact> Product;
  if (Replacement) {
    if (!Request.PipelineMMI)
      return providerError(
          "IR-to-MIR replacement requires pipeline-owned MachineModuleInfo");
    Product = MIRModuleArtifact::borrow(
        *Request.Module, *Request.PipelineMMI, Request.TargetID,
        Request.CompatibilityKey, Request.SchemaDigest);
    Product->setCoveragePolicy(*Request.Coverage);
    if (Error E = Replacement(*Product))
      return joinErrors(
          providerError("replacement IR-to-MIR provider failed"),
          std::move(E));
  } else {
    if (!Builtin)
      return providerError("IR-to-MIR route has no provider");
    auto Lowered = Builtin();
    if (!Lowered)
      return Lowered.takeError();
    Product = std::move(*Lowered);
    if (!Product)
      return providerError("builtin IR-to-MIR provider returned no product");
  }

  if (Error E = Product->verify(Request.RunMachineVerifier))
    return joinErrors(providerError("final MIR boundary rejected product"),
                      std::move(E));
  return Product;
}

} // namespace neverc::plugin
