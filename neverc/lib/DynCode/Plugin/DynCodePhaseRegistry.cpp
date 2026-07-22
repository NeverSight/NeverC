#include "DynCodePhaseRegistry.h"
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"
#include "llvm/Support/Errc.h"

using namespace llvm;
using neverc::plugin::PluginPhaseDefinition;
using neverc::plugin::PluginPhaseGateKind;
using neverc::plugin::PluginPhaseGraph;
using neverc::plugin::samePluginInterfaceID;

namespace neverc {
namespace dyncode {

namespace {

constexpr NevercPhasePolicy TransitionPolicy = NEVERC_PHASE_OBSERVABLE |
                                               NEVERC_PHASE_INTERCEPTABLE |
                                               NEVERC_PHASE_REPLACEABLE;
constexpr NevercPhasePolicy SealedPolicy =
    NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_SEALED_HOST_GATE;

// Sealed gates get a small rerun budget of one; ordinary transforms may be
// re-run when a later mutation invalidates their precondition (IR/MIR/extraction
// each have independent caps in later tasks).
constexpr uint32_t TransitionReruns = 4;
constexpr uint32_t SealedReruns = 1;

} // namespace

Expected<DynCodePhaseRegistry> DynCodePhaseRegistry::create() {
  auto Graph = PluginPhaseGraph::createBuiltinDynCodeGraph();
  if (!Graph)
    return Graph.takeError();
  if (Graph->size() != NEVERC_BUILTIN_DYNCODE_PHASE_COUNT)
    return createStringError(
        errc::invalid_argument,
        "dyncode phase graph disagrees with generated schema count");

  DynCodePhaseRegistry Registry;
  Registry.Phases.reserve(Graph->size());
  for (size_t Index = 0; Index != Graph->size(); ++Index) {
    const PluginPhaseDefinition &Phase = Graph->phaseAt(Index);
    if (Phase.Domain != "dyncode")
      return createStringError(
          errc::invalid_argument,
          "dyncode phase graph contains a non-dyncode phase");
    const bool Sealed = Phase.Gate != PluginPhaseGateKind::Transition;
    const NevercPhasePolicy Expected =
        Sealed ? SealedPolicy : TransitionPolicy;
    if (Phase.Policy != Expected)
      return createStringError(
          errc::invalid_argument,
          "dyncode phase registry disagrees with generated policy");
    DynCodePhaseDefinition Definition;
    Definition.Phase = Phase.ID;
    Definition.InputArtifact = Phase.InputArtifact;
    Definition.OutputArtifact = Phase.OutputArtifact;
    Definition.Gate = Phase.Gate;
    Definition.MaximumReruns = Sealed ? SealedReruns : TransitionReruns;
    Registry.Phases.push_back(Definition);
  }

  // The chain is strictly linear: phase N produces the artifact phase N+1
  // consumes.  Reject a schema that ever breaks that invariant.
  for (size_t Index = 1; Index != Registry.Phases.size(); ++Index)
    if (!samePluginInterfaceID(Registry.Phases[Index - 1].OutputArtifact,
                               Registry.Phases[Index].InputArtifact))
      return createStringError(
          errc::invalid_argument,
          "dyncode phase chain is not contiguous");

  Registry.Graph = std::move(*Graph);
  return Registry;
}

const DynCodePhaseDefinition *
DynCodePhaseRegistry::find(NevercInterfaceID Phase) const {
  for (const DynCodePhaseDefinition &Definition : Phases)
    if (samePluginInterfaceID(Definition.Phase, Phase))
      return &Definition;
  return nullptr;
}

} // namespace dyncode
} // namespace neverc
