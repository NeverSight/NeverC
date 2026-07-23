#ifndef NEVERC_LIB_DYNCODE_PLUGIN_DYNCODEPHASEREGISTRY_H
#define NEVERC_LIB_DYNCODE_PLUGIN_DYNCODEPHASEREGISTRY_H

// Session/task-scoped dyncode phase registry.
//
// Wraps the generated 34-phase dyncode graph (section 2.4) and records, for
// every phase, the linear input/output artifact edge, its policy gate and the
// per-phase rerun budget.  There is no process-global registry: each dyncode
// task builds one from the frozen schema.

#include "neverc/Plugin/Host/PluginPhaseGraph.h"
#include "neverc/Plugin/PluginDynCode.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include <vector>

namespace neverc {
namespace dyncode {

struct DynCodePhaseDefinition {
  NevercInterfaceID Phase{};
  NevercInterfaceID InputArtifact{};
  NevercInterfaceID OutputArtifact{};
  plugin::PluginPhaseGateKind Gate = plugin::PluginPhaseGateKind::Transition;
  uint32_t MaximumReruns = 1;

  bool isSealedGate() const {
    return Gate != plugin::PluginPhaseGateKind::Transition;
  }
};

class DynCodePhaseRegistry {
public:
  static llvm::Expected<DynCodePhaseRegistry> create();

  const plugin::PluginPhaseGraph &graph() const { return Graph; }
  /// All 34 dyncode phases in canonical pipeline order (1..34), each carrying
  /// its policy gate.  The four sealed gates keep their position in the chain.
  llvm::ArrayRef<DynCodePhaseDefinition> phases() const { return Phases; }
  const DynCodePhaseDefinition *find(NevercInterfaceID Phase) const;

private:
  plugin::PluginPhaseGraph Graph;
  std::vector<DynCodePhaseDefinition> Phases;
};

} // namespace dyncode
} // namespace neverc

#endif
