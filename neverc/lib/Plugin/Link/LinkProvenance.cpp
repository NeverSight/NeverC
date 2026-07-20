#include "LinkGraph.h"
#include "llvm/Support/raw_ostream.h"

namespace neverc::plugin {

std::string canonicalizeLinkOrigin(const PluginLinkOriginData &Origin) {
  std::string Result;
  llvm::raw_string_ostream OS(Result);
  OS << Origin.InputID << ":" << Origin.ArchiveMemberID << ":"
     << Origin.ObjectGraph.Owner << ":" << Origin.ObjectGraph.Value << ":"
     << Origin.ObjectEntityID << ":" << Origin.CreatedByPhase.High << ":"
     << Origin.CreatedByPhase.Low << ":"
     << Origin.CreatedByProvider.size() << ":"
     << Origin.CreatedByProvider << ":" << Origin.LastMutationPhase.High
     << ":" << Origin.LastMutationPhase.Low << ":"
     << Origin.LastMutationPlugin.size() << ":"
     << Origin.LastMutationPlugin;
  OS.flush();
  return Result;
}

} // namespace neverc::plugin
