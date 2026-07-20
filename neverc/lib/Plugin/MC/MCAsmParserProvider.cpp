#include "neverc/Plugin/Host/MCAsmParserProvider.h"
#include "neverc/Plugin/Host/MCPluginBridge.h"
#include "neverc/Plugin/Host/MCUnit.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Support/Errc.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

Error parserError(const Twine &Message) {
  return createStringError(errc::invalid_argument, Message);
}

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

} // namespace

Expected<std::unique_ptr<PluginMCUnit>>
MCAsmParserProviderRuntime::execute(
    const AssemblyParseExecutionRequest &Request,
    ReplacementProvider Replacement, BuiltinProvider Builtin) {
  if (!Request.Task || !Request.Snapshot || !Request.Source)
    return parserError("invalid assembly parse execution request");
  if (Error E = Request.Source->verify())
    return joinErrors(parserError("assembly source verification failed"),
                      std::move(E));

  const auto *Target = Request.Snapshot->findTarget(Request.TargetID);
  if (!Target)
    return parserError("assembly parser target is not registered");
  const auto *Schema =
      Request.Snapshot->findMCSchema(Target->MCSchemaID);
  if (!Schema || !sameID(Schema->TargetID, Target->ID))
    return parserError("assembly parser target has no compatible MC schema");

  if (!Replacement) {
    if (!Builtin)
      return parserError("assembly parse route has no provider");
    auto Product = Builtin();
    if (!Product)
      return Product.takeError();
    if (!*Product)
      return parserError("builtin assembly parser returned no MC unit");
    if (!sameID((*Product)->targetID(), Target->ID) ||
        (*Product)->targetSchemaDigest() != Schema->Digest)
      return parserError(
          "builtin assembly parser returned a foreign MC unit");
    if (Error E = verifyPluginMCUnit(**Product, Schema))
      return joinErrors(
          parserError("builtin assembly parse verification failed"),
          std::move(E));
    return Product;
  }

  auto Product = std::make_unique<PluginMCUnit>();
  {
    MCPluginBridge Bridge(*Request.Task, *Product, Schema);
    if (Error E = Replacement(*Request.Source, Bridge))
      return joinErrors(
          parserError("replacement assembly parser failed"),
          std::move(E));
    if (Bridge.hasActiveMutation())
      return parserError(
          "replacement assembly parser left an MC mutation active");
  }
  if (Error E = verifyPluginMCUnit(*Product, Schema))
    return joinErrors(
        parserError("replacement assembly parse verification failed"),
        std::move(E));
  return Product;
}

} // namespace neverc::plugin
