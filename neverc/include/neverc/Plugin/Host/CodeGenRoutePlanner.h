#ifndef NEVERC_PLUGIN_HOST_CODEGENROUTEPLANNER_H
#define NEVERC_PLUGIN_HOST_CODEGENROUTEPLANNER_H

#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <string>
#include <vector>

namespace neverc::plugin {

struct CodeGenRouteRequest {
  NevercTargetID TargetID{};
  NevercCodeGenProductKind InputKind = 0;
  NevercCodeGenProductKind OutputKind = 0;
  std::string CompatibilityKey;
  std::string ForcedProvider;
};

class PlannedCodeGenRoute {
public:
  llvm::ArrayRef<const PluginTargetSnapshot::CodeGenEdgeRecord *>
  edges() const {
    return Edges;
  }
  llvm::StringRef compatibilityKey() const {
    return CompatibilityKey;
  }

private:
  std::vector<const PluginTargetSnapshot::CodeGenEdgeRecord *> Edges;
  std::string CompatibilityKey;
  friend class CodeGenRoutePlanner;
};

class CodeGenRoutePlanner {
public:
  static llvm::Expected<PlannedCodeGenRoute>
  plan(llvm::ArrayRef<PluginTargetSnapshot::CodeGenEdgeRecord> Edges,
       const CodeGenRouteRequest &Request);
  static llvm::Expected<PlannedCodeGenRoute>
  plan(const PluginTargetSnapshot &Snapshot,
       const CodeGenRouteRequest &Request);
};

} // namespace neverc::plugin

#endif
