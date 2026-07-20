#include "neverc/Plugin/Host/AssemblyArtifacts.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"
#include <algorithm>
#include <limits>
#include <new>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error artifactError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

bool nonzero(NevercInterfaceID ID) {
  return ID.High != 0 || ID.Low != 0;
}

bool validDigest(StringRef Digest) {
  return Digest.size() == 64 &&
         llvm::all_of(Digest, [](char Character) {
           return (Character >= '0' && Character <= '9') ||
                  (Character >= 'a' && Character <= 'f');
         });
}

template <typename Payload>
Expected<std::shared_ptr<const PluginArtifactType>>
registerOwned(PluginArtifactRegistry &Registry, NevercInterfaceID ID,
              StringRef Name) {
  PluginArtifactTypeDescriptor Descriptor;
  Descriptor.ID = ID;
  Descriptor.Name = Name.str();
  Descriptor.Ownership = PluginArtifactOwnership::Owned;
  Descriptor.Clone = [](const void *Value) -> Expected<void *> {
    if (!Value)
      return artifactError("assembly artifact payload is null");
    auto *Copy =
        new (std::nothrow) Payload(*static_cast<const Payload *>(Value));
    if (!Copy)
      return artifactError("assembly artifact allocation failed");
    return static_cast<void *>(Copy);
  };
  Descriptor.Destroy = [](void *Value) {
    delete static_cast<Payload *>(Value);
  };
  Descriptor.Verify = [](const void *Value) -> Error {
    if (!Value)
      return artifactError("assembly artifact payload is null");
    return static_cast<const Payload *>(Value)->verify();
  };
  return Registry.registerType(std::move(Descriptor));
}

} // namespace

Error AssemblySourceArtifact::verify() const {
  if (Identifier.empty() || Identifier.find('\0') != std::string::npos ||
      !json::isUTF8(Identifier))
    return artifactError("assembly source identifier is invalid");
  if (Buffer.find('\0') != std::string::npos || !json::isUTF8(Buffer))
    return artifactError("assembly source buffer is not valid UTF-8");
  if (Generation == 0)
    return artifactError("assembly source generation is zero");
  if (Preprocessed && SourceMap.empty() && !Buffer.empty())
    return artifactError(
        "preprocessed assembly source has no source map");

  uint64_t PreviousEnd = 0;
  for (const AssemblySourceMapEntry &Entry : SourceMap) {
    if (Entry.OutputBegin >= Entry.OutputEnd ||
        Entry.OutputEnd > Buffer.size() ||
        Entry.OutputBegin < PreviousEnd || Entry.FileID == 0 ||
        Entry.Line == 0 || Entry.Column == 0)
      return artifactError("assembly source map entry is invalid");
    PreviousEnd = Entry.OutputEnd;
  }
  return Error::success();
}

Expected<AssemblySourceLocation>
AssemblySourceArtifact::locate(uint64_t Offset) const {
  if (Error E = verify())
    return std::move(E);
  if (Offset > Buffer.size())
    return artifactError("assembly source offset is out of range");

  for (const AssemblySourceMapEntry &Entry : SourceMap) {
    if (Offset < Entry.OutputBegin || Offset >= Entry.OutputEnd)
      continue;
    const uint64_t Delta = Offset - Entry.OutputBegin;
    if (Delta > std::numeric_limits<uint32_t>::max() - Entry.Column)
      return artifactError("assembly source column overflows");
    return AssemblySourceLocation{
        Entry.FileID, Entry.SourceBegin + Delta, Entry.Line,
        static_cast<uint32_t>(Entry.Column + Delta)};
  }

  uint32_t Line = 1;
  uint32_t Column = 1;
  for (uint64_t I = 0; I < Offset; ++I) {
    if (Buffer[static_cast<size_t>(I)] == '\n') {
      if (Line == std::numeric_limits<uint32_t>::max())
        return artifactError("assembly source line overflows");
      ++Line;
      Column = 1;
    } else {
      if (Column == std::numeric_limits<uint32_t>::max())
        return artifactError("assembly source column overflows");
      ++Column;
    }
  }
  return AssemblySourceLocation{1, Offset, Line, Column};
}

Error AssemblyOutputArtifact::verify() const {
  if (!Finished)
    return artifactError("assembly output was not finished");
  if (!nonzero(TargetID))
    return artifactError("assembly output has no target ID");
  if (!validDigest(TargetSchemaDigest))
    return artifactError("assembly output schema digest is invalid");
  if (UnitGeneration == 0)
    return artifactError("assembly output has no MC generation");
  if (Text.find('\0') != std::string::npos || !json::isUTF8(Text))
    return artifactError("assembly output is not valid UTF-8");
  if (Syntax.find('\0') != std::string::npos || !json::isUTF8(Syntax) ||
      Comment.find('\0') != std::string::npos || !json::isUTF8(Comment) ||
      Flags != 0)
    return artifactError("assembly output metadata is invalid");
  return Error::success();
}

NevercInterfaceID assemblySourceArtifactID() {
  return {NEVERC_PHASE_ASSEMBLY_PARSE_INPUT_HIGH,
          NEVERC_PHASE_ASSEMBLY_PARSE_INPUT_LOW};
}

NevercInterfaceID assemblyOutputArtifactID() {
  return {NEVERC_PHASE_ASSEMBLY_PRINT_OUTPUT_HIGH,
          NEVERC_PHASE_ASSEMBLY_PRINT_OUTPUT_LOW};
}

Expected<AssemblyArtifactTypes>
registerAssemblyArtifactTypes(PluginArtifactRegistry &Registry) {
  AssemblyArtifactTypes Types;
  auto Source = registerOwned<AssemblySourceArtifact>(
      Registry, assemblySourceArtifactID(), "assembly.source");
  if (!Source)
    return Source.takeError();
  Types.Source = std::move(*Source);

  auto Output = registerOwned<AssemblyOutputArtifact>(
      Registry, assemblyOutputArtifactID(), "assembly.output");
  if (!Output)
    return Output.takeError();
  Types.Output = std::move(*Output);
  return Types;
}

} // namespace neverc::plugin
