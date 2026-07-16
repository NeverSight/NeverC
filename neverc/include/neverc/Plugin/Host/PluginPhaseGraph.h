#ifndef NEVERC_PLUGIN_HOST_PLUGINPHASEGRAPH_H
#define NEVERC_PLUGIN_HOST_PLUGINPHASEGRAPH_H

#include "neverc/Plugin/PluginCore.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace neverc::plugin {

enum class PluginPhaseGateKind : uint8_t {
  Transition,
  SealedVerifier,
  SealedCommit,
};

enum class PluginPhaseStability : uint8_t { Stable, Experimental };

struct PluginPhaseDefinition {
  NevercInterfaceID ID{};
  std::string CanonicalName;
  std::string Domain;
  std::string Verifier;
  NevercInterfaceID InputArtifact{};
  NevercInterfaceID OutputArtifact{};
  NevercPhasePolicy Policy = 0;
  NevercObserverPoint ObserverPoints = 0;
  PluginPhaseGateKind Gate = PluginPhaseGateKind::Transition;
  PluginPhaseStability Stability = PluginPhaseStability::Stable;
  bool HasBuiltinFallback = false;
};

class PluginPhaseGraph {
public:
  llvm::Error addPhase(PluginPhaseDefinition Phase);
  llvm::Error addEdge(NevercInterfaceID Before, NevercInterfaceID After,
                      bool RequireCompatibleArtifacts = false);
  llvm::Error finalize();

  const PluginPhaseDefinition *find(NevercInterfaceID ID) const;
  const PluginPhaseDefinition *find(llvm::StringRef CanonicalName) const;
  const PluginPhaseDefinition &phaseAt(size_t Index) const {
    return Phases[Index];
  }
  llvm::ArrayRef<size_t> order() const { return Order; }
  size_t size() const { return Phases.size(); }
  bool isFinalized() const { return Finalized; }

  static llvm::Expected<PluginPhaseGraph> createBuiltinDriverGraph();
  static llvm::Expected<PluginPhaseGraph> createBuiltinSourceGraph();

private:
  struct Edge {
    NevercInterfaceID Before{};
    NevercInterfaceID After{};
    bool RequireCompatibleArtifacts = false;
  };

  std::vector<PluginPhaseDefinition> Phases;
  std::vector<Edge> Edges;
  std::vector<size_t> Order;
  bool Finalized = false;
};

bool samePluginInterfaceID(NevercInterfaceID Left,
                           NevercInterfaceID Right);

} // namespace neverc::plugin

#endif
