#ifndef NEVERC_PLUGIN_LINK_SYMBOLRESOLUTIONPROVIDER_H
#define NEVERC_PLUGIN_LINK_SYMBOLRESOLUTIONPROVIDER_H

#include "LinkGraph.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include <string>
#include <vector>

namespace neverc::plugin {

enum class LinkSymbolResolutionReason : uint8_t {
  StrongDefinition,
  WeakDefinition,
  CommonDefinition,
  SharedDefinition,
  Undefined,
  ComdatCandidate,
};

struct LinkSymbolCandidateRecord {
  uint64_t SymbolID = 0;
  LinkSymbolResolutionReason Reason =
      LinkSymbolResolutionReason::Undefined;
  bool Selected = false;
};

struct LinkSymbolResolutionRecord {
  std::string Name;
  std::string Version;
  uint64_t SelectedSymbolID = 0;
  LinkSymbolResolutionReason Reason =
      LinkSymbolResolutionReason::Undefined;
  std::vector<LinkSymbolCandidateRecord> Candidates;
};

struct SymbolResolutionOptions {
  bool AllowUndefined = false;
  bool Relocatable = false;
  std::string EntrySymbol;
  std::vector<std::string> ExportedSymbols;
};

llvm::Expected<std::vector<LinkSymbolResolutionRecord>>
resolveLinkSymbols(PluginLinkGraph &Graph,
                   const SymbolResolutionOptions &Options = {});

} // namespace neverc::plugin

#endif
