#include "SynthesisVerifier.h"
#include "llvm/Support/Errc.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

Error synthesisError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "link synthesis verification: " + Message);
}

bool fitsSigned(int64_t Value, uint32_t Width) {
  if (Width == 0 || Width >= 64)
    return true;
  const int64_t Minimum = -(INT64_C(1) << (Width - 1));
  const int64_t Maximum = (INT64_C(1) << (Width - 1)) - 1;
  return Value >= Minimum && Value <= Maximum;
}

const PluginLinkAtom *edgeTarget(const PluginLinkGraph &Graph,
                                 const PluginLinkEdge &Edge) {
  if (Edge.TargetAtomID != 0)
    return Graph.findAtom(Edge.TargetAtomID);
  const PluginLinkSymbol *Symbol =
      Graph.findSymbol(Edge.TargetSymbolID);
  return Symbol ? Graph.findAtom(Symbol->AtomID) : nullptr;
}

} // namespace

Error verifyLinkSynthetics(const PluginLinkGraph &Graph) {
  if (Graph.state() < NEVERC_LINK_STATE_SYNTHETICS_READY)
    return synthesisError("graph has no synthetic result");
  if (Error E = verifyPluginLinkGraph(Graph))
    return E;
  for (const PluginLinkSynthetic &Synthetic : Graph.synthetics()) {
    if (Synthetic.Role.empty())
      return synthesisError("synthetic role is empty");
    const PluginLinkSection *Section =
        Graph.findSection(Synthetic.SectionID);
    const PluginLinkAtom *Atom = Graph.findAtom(Synthetic.AtomID);
    if (!Section || !Atom || Atom->SectionID != Section->ID)
      return synthesisError("synthetic references invalid storage: " +
                            Synthetic.Role);
    if ((Atom->Flags & NEVERC_LINK_ATOM_SYNTHETIC) == 0 ||
        (Atom->Flags & NEVERC_LINK_ATOM_LIVE) == 0)
      return synthesisError("synthetic atom is not live and marked: " +
                            Synthetic.Role);
  }
  return Error::success();
}

Error verifyLinkRelaxation(const PluginLinkGraph &Graph) {
  if (Graph.state() < NEVERC_LINK_STATE_THUNKS_RELAXED)
    return synthesisError("graph has no relaxation result");
  if (Error E = verifyLinkSynthetics(Graph))
    return E;
  for (const PluginLinkEdge &Edge : Graph.edges()) {
    if (Edge.Width != 0 && Edge.Width != 8 && Edge.Width != 16 &&
        Edge.Width != 32 && Edge.Width != 64)
      return synthesisError("edge has an unsupported relaxed width");
    if (!Edge.IsPCRelative || !Edge.IsSigned || Edge.Width == 0)
      continue;
    const PluginLinkAtom *Source =
        Graph.findAtom(Edge.SourceAtomID);
    const PluginLinkAtom *Target = edgeTarget(Graph, Edge);
    if (!Source || !Target)
      continue;
    const int64_t Place = static_cast<int64_t>(
        Source->Address + Edge.Offset + Edge.Width / 8);
    const int64_t Value =
        static_cast<int64_t>(Target->Address) + Edge.Addend - Place;
    if (!fitsSigned(Value, Edge.Width))
      return synthesisError("PC-relative edge remains out of range");
  }
  return Error::success();
}

} // namespace neverc::plugin
