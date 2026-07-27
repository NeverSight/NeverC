#include "LinkPhaseRegistry.h"
#include "neverc/Plugin/Host/LinkPluginInterfaces.h"
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"
#include "llvm/Support/Errc.h"
#include <iterator>

using namespace llvm;

namespace neverc::plugin {
namespace {

constexpr NevercPhasePolicy TransitionPolicy =
    NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_INTERCEPTABLE |
    NEVERC_PHASE_REPLACEABLE | NEVERC_PHASE_SKIPPABLE_WITH_PROOF;

#define NEVERC_LINK_TRANSITION(Symbol, InputState, OutputState, Reruns)        \
  LinkTransitionDefinition {                                                 \
    {NEVERC_PHASE_LINK_##Symbol##_HIGH,                                      \
     NEVERC_PHASE_LINK_##Symbol##_LOW},                                      \
        InputState, OutputState, Reruns                                      \
  }

constexpr LinkTransitionDefinition BuiltinTransitions[] = {
    NEVERC_LINK_TRANSITION(INPUT_PROBE, NEVERC_LINK_STATE_INITIAL,
                           NEVERC_LINK_STATE_INPUT_PROBED, 4),
    NEVERC_LINK_TRANSITION(READ_INPUTS, NEVERC_LINK_STATE_INPUT_PROBED,
                           NEVERC_LINK_STATE_INPUTS_READ, 4),
    NEVERC_LINK_TRANSITION(LTO_RESOLVE, NEVERC_LINK_STATE_INPUTS_READ,
                           NEVERC_LINK_STATE_LTO_RESOLUTION_READY, 8),
    NEVERC_LINK_TRANSITION(LTO_GENERATE,
                           NEVERC_LINK_STATE_LTO_RESOLUTION_READY,
                           NEVERC_LINK_STATE_LTO_GENERATED, 8),
    NEVERC_LINK_TRANSITION(RESOLVE_SYMBOLS,
                           NEVERC_LINK_STATE_LTO_GENERATED,
                           NEVERC_LINK_STATE_SYMBOLS_RESOLVED, 8),
    NEVERC_LINK_TRANSITION(SELECT_COMDAT,
                           NEVERC_LINK_STATE_SYMBOLS_RESOLVED,
                           NEVERC_LINK_STATE_COMDAT_SELECTED, 4),
    NEVERC_LINK_TRANSITION(GC, NEVERC_LINK_STATE_COMDAT_SELECTED,
                           NEVERC_LINK_STATE_GC_COMPLETE, 8),
    NEVERC_LINK_TRANSITION(ICF, NEVERC_LINK_STATE_GC_COMPLETE,
                           NEVERC_LINK_STATE_ICF_COMPLETE, 8),
    NEVERC_LINK_TRANSITION(SYNTHESIZE, NEVERC_LINK_STATE_ICF_COMPLETE,
                           NEVERC_LINK_STATE_SYNTHETICS_READY, 8),
    NEVERC_LINK_TRANSITION(RELAX_THUNKS,
                           NEVERC_LINK_STATE_SYNTHETICS_READY,
                           NEVERC_LINK_STATE_THUNKS_RELAXED, 16),
    NEVERC_LINK_TRANSITION(LAYOUT, NEVERC_LINK_STATE_THUNKS_RELAXED,
                           NEVERC_LINK_STATE_LAYOUT_COMPLETE, 16),
    NEVERC_LINK_TRANSITION(RELOCATE, NEVERC_LINK_STATE_LAYOUT_COMPLETE,
                           NEVERC_LINK_STATE_RELOCATIONS_APPLIED, 8),
    NEVERC_LINK_TRANSITION(EMIT_IMAGE,
                           NEVERC_LINK_STATE_RELOCATIONS_APPLIED,
                           NEVERC_LINK_STATE_IMAGE_EMITTED, 4),
};

#undef NEVERC_LINK_TRANSITION

static_assert(std::size(BuiltinTransitions) == 13);
static_assert(NEVERC_BUILTIN_LINK_PHASE_COUNT == 20);

} // namespace

Expected<LinkPhaseRegistry> LinkPhaseRegistry::create() {
  auto Graph = PluginPhaseGraph::createBuiltinLinkGraph();
  if (!Graph)
    return Graph.takeError();
  if (Graph->size() != NEVERC_BUILTIN_LINK_PHASE_COUNT)
    return createStringError(
        errc::invalid_argument,
        "Link phase graph disagrees with generated schema count");
  for (NevercInterfaceID ID : builtInLinkPhaseIDs())
    if (!Graph->find(ID))
      return createStringError(
          errc::invalid_argument,
          "Link phase graph omits a generated stable phase");
  for (const LinkTransitionDefinition &Transition :
       BuiltinTransitions) {
    const PluginPhaseDefinition *Phase =
        Graph->find(Transition.Phase);
    if (!Phase || Phase->Domain != "link" ||
        Phase->Policy != TransitionPolicy ||
        Phase->Gate != PluginPhaseGateKind::Transition)
      return createStringError(
          errc::invalid_argument,
          "Link transition registry disagrees with generated policy");
  }

  LinkPhaseRegistry Registry;
  Registry.Graph = std::move(*Graph);
  Registry.Transitions.assign(std::begin(BuiltinTransitions),
                              std::end(BuiltinTransitions));
  return Registry;
}

const LinkTransitionDefinition *
LinkPhaseRegistry::findTransition(NevercInterfaceID Phase) const {
  for (const LinkTransitionDefinition &Transition : Transitions)
    if (samePluginInterfaceID(Transition.Phase, Phase))
      return &Transition;
  return nullptr;
}

} // namespace neverc::plugin
