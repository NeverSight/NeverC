#ifndef NEVERC_PLUGIN_LINK_OBJECTGRAPHIMPORTER_H
#define NEVERC_PLUGIN_LINK_OBJECTGRAPHIMPORTER_H

#include "LinkGraph.h"
#include "neverc/Plugin/Host/ObjectGraph.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <map>

namespace neverc::plugin {

struct ObjectGraphImportOptions {
  uint64_t InputID = 0;
  uint64_t ArchiveMemberID = 0;
  NevercObjectGraphHandle ObjectGraph{};
};

struct ObjectGraphImportResult {
  std::map<uint64_t, uint64_t> Sections;
  std::map<uint64_t, uint64_t> Atoms;
  std::map<uint64_t, uint64_t> Symbols;
  std::map<uint64_t, uint64_t> Relocations;
  std::map<uint64_t, uint64_t> Comdats;
};

/// Copies one verified ObjectGraph into a normalized LinkGraph while retaining
/// a stable origin map back to every source object entity.
llvm::Expected<ObjectGraphImportResult>
importObjectGraph(PluginLinkGraph &Destination,
                  const PluginObjectGraph &Source,
                  const ObjectGraphImportOptions &Options = {});

} // namespace neverc::plugin

#endif
