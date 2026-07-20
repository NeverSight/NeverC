#include "neverc/Plugin/Host/MIRToMCProvider.h"
#include "../MIR/MIRModuleArtifact.h"
#include "neverc/Plugin/Host/MachineEmissionBridge.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/Support/Errc.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

Error providerError(const Twine &Message) {
  return createStringError(errc::invalid_argument, Message);
}

} // namespace

Expected<std::unique_ptr<PluginMCUnit>>
MIRToMCProviderRuntime::execute(
    const MIRToMCExecutionRequest &Request,
    ReplacementProvider Replacement, BuiltinProvider Builtin) {
  if (!Request.Task || !Request.MIR || !Request.Snapshot)
    return providerError("invalid MIR-to-MC execution request");
  if (!Request.HasFinalMIRProof)
    return providerError(
        "MIR-to-MC provider requires a sealed final MIR proof");
  if (Error E = Request.MIR->verify(Request.RunMachineVerifier))
    return joinErrors(providerError("MIR-to-MC input verification failed"),
                      std::move(E));

  if (!Replacement) {
    if (!Builtin)
      return providerError("MIR-to-MC route has no provider");
    auto Product = Builtin();
    if (!Product)
      return Product.takeError();
    if (!*Product)
      return providerError("builtin MIR-to-MC provider returned no product");
    return Product;
  }

  const auto *Target =
      Request.Snapshot->findTarget(Request.MIR->targetID());
  if (!Target)
    return providerError("MIR-to-MC input references an unknown target");
  const auto *Schema =
      Request.Snapshot->findMCSchema(Target->MCSchemaID);
  if (!Schema)
    return providerError("MIR-to-MC target has no MC schema");
  if (Request.MIR->schemaDigest() != Schema->Digest)
    return providerError(
        "MIR-to-MC input schema does not match the selected target");

  auto Bridge = MachineEmissionBridge::create(
      *Request.Task, *Request.MIR, Schema);
  if (!Bridge)
    return Bridge.takeError();
  if (Error E = Replacement(**Bridge))
    return joinErrors(
        providerError("replacement MIR-to-MC provider failed"),
        std::move(E));
  if (Error E = (*Bridge)->verify())
    return joinErrors(providerError("MC product verification failed"),
                      std::move(E));
  return (*Bridge)->takeUnit();
}

} // namespace neverc::plugin
