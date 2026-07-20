#include "LinkMutation.h"

namespace neverc::plugin {

NevercLinkState earliestInvalidatedLinkState(LinkMutationKind Kind) {
  switch (Kind) {
  case LinkMutationKind::InputStructure:
    return NEVERC_LINK_STATE_INPUTS_READ;
  case LinkMutationKind::SymbolResolution:
    return NEVERC_LINK_STATE_SYMBOLS_RESOLVED;
  case LinkMutationKind::ResolutionOutcome:
    return NEVERC_LINK_STATE_COMDAT_SELECTED;
  case LinkMutationKind::Liveness:
    return NEVERC_LINK_STATE_GC_COMPLETE;
  case LinkMutationKind::LivenessOutcome:
    return NEVERC_LINK_STATE_ICF_COMPLETE;
  case LinkMutationKind::Folding:
    return NEVERC_LINK_STATE_SYNTHETICS_READY;
  case LinkMutationKind::Synthetic:
  case LinkMutationKind::AtomContent:
    return NEVERC_LINK_STATE_SYNTHETICS_READY;
  case LinkMutationKind::LayoutConstraint:
    return NEVERC_LINK_STATE_LAYOUT_COMPLETE;
  case LinkMutationKind::Relocation:
    return NEVERC_LINK_STATE_RELOCATIONS_APPLIED;
  case LinkMutationKind::Image:
    return NEVERC_LINK_STATE_IMAGE_EMITTED;
  }
  return NEVERC_LINK_STATE_INPUTS_READ;
}

NevercLinkState predecessorLinkState(NevercLinkState State) {
  if (State == NEVERC_LINK_STATE_INITIAL)
    return NEVERC_LINK_STATE_INITIAL;
  return State - 1;
}

} // namespace neverc::plugin
