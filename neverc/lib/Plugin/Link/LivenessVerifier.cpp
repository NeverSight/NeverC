#include "LivenessVerifier.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Errc.h"
#include <tuple>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error livenessError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "link liveness verification: " + Message);
}

bool isLive(const PluginLinkAtom &Atom) {
  return (Atom.Flags & NEVERC_LINK_ATOM_LIVE) != 0;
}

const PluginLinkAtom *symbolAtom(const PluginLinkGraph &Graph,
                                 uint64_t SymbolID) {
  const PluginLinkSymbol *Symbol = Graph.findSymbol(SymbolID);
  return Symbol ? Graph.findAtom(Symbol->AtomID) : nullptr;
}

std::vector<std::tuple<uint64_t, uint64_t, uint64_t, int64_t>>
edgeSignature(const PluginLinkGraph &Graph, uint64_t AtomID) {
  std::vector<std::tuple<uint64_t, uint64_t, uint64_t, int64_t>> Result;
  for (const PluginLinkEdge &Edge : Graph.edges()) {
    if (Edge.SourceAtomID != AtomID)
      continue;
    uint64_t Target = Edge.TargetAtomID;
    if (Target == 0) {
      const PluginLinkSymbol *Symbol =
          Graph.findSymbol(Edge.TargetSymbolID);
      Target = Symbol ? Symbol->AtomID : 0;
    }
    Result.emplace_back(Edge.Kind, Edge.Offset, Target, Edge.Addend);
  }
  llvm::sort(Result);
  return Result;
}

} // namespace

Error verifyLinkLiveness(const PluginLinkGraph &Graph) {
  if (Graph.state() < NEVERC_LINK_STATE_GC_COMPLETE)
    return livenessError("graph has no GC result");
  if (Error E = verifyPluginLinkGraph(Graph))
    return E;

  for (const PluginLinkAtom &Atom : Graph.atoms())
    if ((Atom.Flags & NEVERC_LINK_ATOM_ROOT) != 0 && !isLive(Atom))
      return livenessError("required root atom is dead: " + Atom.Name);

  for (const PluginLinkSymbol &Symbol : Graph.symbols())
    if ((Symbol.IsRoot || Symbol.IsExported) && Symbol.AtomID != 0) {
      const PluginLinkAtom *Atom = Graph.findAtom(Symbol.AtomID);
      if (!Atom || !isLive(*Atom))
        return livenessError("required symbol is dead: " + Symbol.Name);
    }

  for (const PluginLinkExport &Export : Graph.exports()) {
    const PluginLinkAtom *Atom = symbolAtom(Graph, Export.SymbolID);
    if (Atom && !isLive(*Atom))
      return livenessError("export target is dead: " + Export.Name);
  }
  for (const PluginLinkSynthetic &Synthetic : Graph.synthetics()) {
    const PluginLinkAtom *Atom = Graph.findAtom(Synthetic.AtomID);
    if (Atom && !isLive(*Atom))
      return livenessError("required synthetic atom is dead: " +
                           Synthetic.Role);
  }

  for (const PluginLinkEdge &Edge : Graph.edges()) {
    const PluginLinkAtom *Source = Graph.findAtom(Edge.SourceAtomID);
    if (!Source || !isLive(*Source))
      continue;
    const PluginLinkAtom *Target =
        Edge.TargetAtomID != 0
            ? Graph.findAtom(Edge.TargetAtomID)
            : symbolAtom(Graph, Edge.TargetSymbolID);
    if (Target && !isLive(*Target))
      return livenessError("live edge targets a dead atom");
  }

  for (const PluginLinkUnwindRecord &Unwind :
       Graph.unwindRecords()) {
    const PluginLinkAtom *Code = Graph.findAtom(Unwind.AtomID);
    const PluginLinkAtom *Personality =
        symbolAtom(Graph, Unwind.PersonalitySymbolID);
    if (Code && isLive(*Code) && Personality && !isLive(*Personality))
      return livenessError(
          "live unwind record has a dead personality");
  }
  return Error::success();
}

Error verifyLinkFolding(const PluginLinkGraph &Graph) {
  if (Graph.state() < NEVERC_LINK_STATE_ICF_COMPLETE)
    return livenessError("graph has no ICF result");
  if (Error E = verifyLinkLiveness(Graph))
    return E;

  for (const PluginLinkAtom &Atom : Graph.atoms()) {
    const bool Folded =
        (Atom.Flags & NEVERC_LINK_ATOM_FOLDED) != 0;
    if (Folded != (Atom.FoldLeaderID != 0))
      return livenessError("fold flag and leader disagree: " +
                           Atom.Name);
    if (!Folded)
      continue;
    const PluginLinkAtom *Leader =
        Graph.findAtom(Atom.FoldLeaderID);
    if (!Leader || Leader->ID == Atom.ID ||
        !isLive(Atom) || !isLive(*Leader))
      return livenessError("fold leader is invalid: " + Atom.Name);
    if (Leader->FoldLeaderID != 0)
      return livenessError("fold leader is itself folded: " +
                           Atom.Name);
    if ((Atom.Flags & (NEVERC_LINK_ATOM_ADDRESS_SIGNIFICANT |
                       NEVERC_LINK_ATOM_TLS |
                       NEVERC_LINK_ATOM_UNWIND)) != 0)
      return livenessError("ineligible atom was folded: " + Atom.Name);
    if (Atom.Content != Leader->Content ||
        Atom.ZeroFillSize != Leader->ZeroFillSize ||
        Atom.Alignment != Leader->Alignment ||
        edgeSignature(Graph, Atom.ID) !=
            edgeSignature(Graph, Leader->ID))
      return livenessError("folded atoms are not equivalent: " +
                           Atom.Name);
  }
  return Error::success();
}

} // namespace neverc::plugin
