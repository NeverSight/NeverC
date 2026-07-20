#include "SyntheticSectionProvider.h"
#include "SynthesisVerifier.h"
#include "llvm/Support/Errc.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

Error syntheticError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "link synthetic provider: " + Message);
}

} // namespace

Expected<std::vector<LinkSyntheticRecord>>
materializeLinkSynthetics(PluginLinkGraph &Graph) {
  if (Graph.state() < NEVERC_LINK_STATE_ICF_COMPLETE)
    return syntheticError("ICF is not complete");
  if (Graph.state() > NEVERC_LINK_STATE_SYNTHETICS_READY)
    return syntheticError(
        "later phases must be invalidated before synthesis");

  std::vector<LinkSyntheticRecord> Records;
  Records.reserve(Graph.synthetics().size());
  for (const PluginLinkSynthetic &Synthetic : Graph.synthetics()) {
    PluginLinkSection *Section =
        Graph.findSection(Synthetic.SectionID);
    PluginLinkAtom *Atom = Graph.findAtom(Synthetic.AtomID);
    if (Synthetic.Role.empty() || !Section || !Atom ||
        Atom->SectionID != Section->ID)
      return syntheticError(
          "synthetic descriptor has invalid storage");
    Atom->Flags |=
        NEVERC_LINK_ATOM_SYNTHETIC | NEVERC_LINK_ATOM_LIVE;
    Records.push_back({Synthetic.ID, Synthetic.SectionID,
                       Synthetic.AtomID, Synthetic.Role});
  }
  Graph.advanceGeneration();
  Graph.setState(NEVERC_LINK_STATE_SYNTHETICS_READY);
  if (Error E = verifyLinkSynthetics(Graph))
    return std::move(E);
  return Records;
}

} // namespace neverc::plugin
