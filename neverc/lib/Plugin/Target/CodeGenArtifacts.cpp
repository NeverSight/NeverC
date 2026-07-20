#include "neverc/Plugin/Host/CodeGenArtifacts.h"
#include "llvm/Support/Error.h"
#include <new>

using namespace llvm;

namespace neverc::plugin {
namespace {

constexpr uint64_t ArtifactIDHigh = UINT64_C(0x4e43434741525401);

bool nonzero(NevercInterfaceID ID) {
  return ID.High != 0 || ID.Low != 0;
}

bool nonnull(NevercHandle Handle) {
  return Handle.Owner != 0 && Handle.Value != 0;
}

template <typename Payload>
Expected<std::shared_ptr<const PluginArtifactType>>
registerOwned(PluginArtifactRegistry &Registry, NevercInterfaceID ID,
              StringRef Name,
              std::function<Error(const Payload &)> Verify) {
  PluginArtifactTypeDescriptor Descriptor;
  Descriptor.ID = ID;
  Descriptor.Name = Name.str();
  Descriptor.Ownership = PluginArtifactOwnership::Owned;
  Descriptor.Clone = [](const void *Value) -> Expected<void *> {
    if (!Value)
      return createStringError(inconvertibleErrorCode(),
                               "codegen artifact payload is null");
    auto *Copy =
        new (std::nothrow) Payload(*static_cast<const Payload *>(Value));
    if (!Copy)
      return createStringError(inconvertibleErrorCode(),
                               "codegen artifact allocation failed");
    return static_cast<void *>(Copy);
  };
  Descriptor.Destroy = [](void *Value) {
    delete static_cast<Payload *>(Value);
  };
  Descriptor.Verify =
      [Verify = std::move(Verify)](const void *Value) -> Error {
    if (!Value)
      return createStringError(inconvertibleErrorCode(),
                               "codegen artifact payload is null");
    return Verify(*static_cast<const Payload *>(Value));
  };
  return Registry.registerType(std::move(Descriptor));
}

Error verifyCompatibilityKey(StringRef Key) {
  if (Key.empty() || Key.size() > 4096 || Key.contains('\0'))
    return createStringError(inconvertibleErrorCode(),
                             "codegen compatibility key is invalid");
  return Error::success();
}

Expected<std::shared_ptr<const PluginArtifactType>>
registerProduct(PluginArtifactRegistry &Registry, NevercInterfaceID ID,
                StringRef Name, NevercCodeGenProductKind Kind) {
  return registerOwned<CodeGenProductArtifact>(
      Registry, ID, Name,
      [Kind](const CodeGenProductArtifact &Artifact) -> Error {
        if (Artifact.Kind != Kind || !nonzero(Artifact.ProductID) ||
            !nonnull(Artifact.Payload))
          return createStringError(
              inconvertibleErrorCode(),
              "codegen product artifact identity is invalid");
        if (!Artifact.HostVerified)
          return createStringError(
              inconvertibleErrorCode(),
              "codegen product artifact bypassed its mandatory verifier");
        return verifyCompatibilityKey(Artifact.CompatibilityKey);
      });
}

} // namespace

NevercInterfaceID targetSelectionArtifactID() {
  return {ArtifactIDHigh, UINT64_C(1)};
}

NevercInterfaceID codeGenRequestArtifactID() {
  return {ArtifactIDHigh, UINT64_C(2)};
}

NevercInterfaceID codeGenIRModuleArtifactID() {
  return {ArtifactIDHigh, UINT64_C(3)};
}

NevercInterfaceID codeGenMIRModuleArtifactID() {
  return {ArtifactIDHigh, UINT64_C(4)};
}

NevercInterfaceID codeGenMCUnitArtifactID() {
  return {ArtifactIDHigh, UINT64_C(5)};
}

NevercInterfaceID codeGenObjectGraphArtifactID() {
  return {ArtifactIDHigh, UINT64_C(6)};
}

NevercInterfaceID codeGenObjectImageArtifactID() {
  return {ArtifactIDHigh, UINT64_C(7)};
}

Expected<CodeGenArtifactTypes>
registerCodeGenArtifactTypes(PluginArtifactRegistry &Registry) {
  CodeGenArtifactTypes Types;
  auto TargetSelection = registerOwned<TargetSelectionArtifact>(
      Registry, targetSelectionArtifactID(), "target.selection",
      [](const TargetSelectionArtifact &Artifact) -> Error {
        if (!nonzero(Artifact.TargetID))
          return createStringError(
              inconvertibleErrorCode(),
              "target selection artifact has no target ID");
        return verifyCompatibilityKey(Artifact.CompatibilityKey);
      });
  if (!TargetSelection)
    return TargetSelection.takeError();
  Types.TargetSelection = std::move(*TargetSelection);

  auto Request = registerOwned<CodeGenRequestArtifact>(
      Registry, codeGenRequestArtifactID(), "codegen.request",
      [](const CodeGenRequestArtifact &Artifact) -> Error {
        if (Artifact.InputKind == 0 || Artifact.OutputKind == 0 ||
            Artifact.InputKind == Artifact.OutputKind)
          return createStringError(inconvertibleErrorCode(),
                                   "codegen request kinds are invalid");
        return verifyCompatibilityKey(Artifact.CompatibilityKey);
      });
  if (!Request)
    return Request.takeError();
  Types.Request = std::move(*Request);

  auto IR = registerProduct(Registry, codeGenIRModuleArtifactID(),
                            "ir.module", NEVERC_CODEGEN_PRODUCT_IR);
  if (!IR)
    return IR.takeError();
  Types.IRModule = std::move(*IR);
  auto MIR = registerProduct(Registry, codeGenMIRModuleArtifactID(),
                             "mir.module", NEVERC_CODEGEN_PRODUCT_MIR);
  if (!MIR)
    return MIR.takeError();
  Types.MIRModule = std::move(*MIR);
  auto MC = registerProduct(Registry, codeGenMCUnitArtifactID(), "mc.unit",
                            NEVERC_CODEGEN_PRODUCT_MC);
  if (!MC)
    return MC.takeError();
  Types.MCUnit = std::move(*MC);
  auto ObjectGraph =
      registerProduct(Registry, codeGenObjectGraphArtifactID(),
                      "object.graph",
                      NEVERC_CODEGEN_PRODUCT_OBJECT_GRAPH);
  if (!ObjectGraph)
    return ObjectGraph.takeError();
  Types.ObjectGraph = std::move(*ObjectGraph);
  auto ObjectImage =
      registerProduct(Registry, codeGenObjectImageArtifactID(),
                      "object.image",
                      NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE);
  if (!ObjectImage)
    return ObjectImage.takeError();
  Types.ObjectImage = std::move(*ObjectImage);
  return Types;
}

} // namespace neverc::plugin
