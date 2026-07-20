#ifndef NEVERC_PLUGIN_HOST_ASSEMBLYARTIFACTS_H
#define NEVERC_PLUGIN_HOST_ASSEMBLYARTIFACTS_H

#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/PluginMC.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace neverc::plugin {

struct AssemblySourceMapEntry {
  uint64_t OutputBegin = 0;
  uint64_t OutputEnd = 0;
  uint64_t FileID = 0;
  uint64_t SourceBegin = 0;
  uint32_t Line = 0;
  uint32_t Column = 0;
};

struct AssemblySourceLocation {
  uint64_t FileID = 0;
  uint64_t ByteOffset = 0;
  uint32_t Line = 0;
  uint32_t Column = 0;
};

struct AssemblySourceArtifact {
  std::string Identifier;
  std::string Buffer;
  std::vector<AssemblySourceMapEntry> SourceMap;
  bool Preprocessed = false;
  uint64_t Generation = 1;

  llvm::Error verify() const;
  llvm::Expected<AssemblySourceLocation> locate(uint64_t Offset) const;
};

struct AssemblyOutputArtifact {
  std::string Text;
  NevercTargetID TargetID{};
  std::string TargetSchemaDigest;
  std::string Syntax;
  std::string Comment;
  uint64_t UnitGeneration = 0;
  uint64_t Flags = 0;
  bool Finished = false;

  llvm::Error verify() const;
};

struct AssemblyArtifactTypes {
  std::shared_ptr<const PluginArtifactType> Source;
  std::shared_ptr<const PluginArtifactType> Output;
};

NevercInterfaceID assemblySourceArtifactID();
NevercInterfaceID assemblyOutputArtifactID();

llvm::Expected<AssemblyArtifactTypes>
registerAssemblyArtifactTypes(PluginArtifactRegistry &Registry);

} // namespace neverc::plugin

#endif
