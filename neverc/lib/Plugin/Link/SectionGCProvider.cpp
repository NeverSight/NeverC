#include "SectionGCProvider.h"
#include "LivenessVerifier.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Errc.h"
#include <deque>
#include <map>
#include <set>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error gcError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "link section GC: " + Message);
}

StringRef edgeReason(NevercLinkEdgeKind Kind) {
  switch (Kind) {
  case NEVERC_LINK_EDGE_RELOCATION:
    return "relocation";
  case NEVERC_LINK_EDGE_ASSOCIATION:
    return "association";
  case NEVERC_LINK_EDGE_KEEP_ALIVE:
    return "keep-alive";
  case NEVERC_LINK_EDGE_UNWIND:
    return "unwind";
  default:
    return "format-extension";
  }
}

} // namespace

Expected<std::vector<LinkLivenessRecord>>
markLiveLinkAtoms(PluginLinkGraph &Graph) {
  if (Graph.state() < NEVERC_LINK_STATE_COMDAT_SELECTED)
    return gcError("COMDAT selection is not complete");
  if (Graph.state() > NEVERC_LINK_STATE_GC_COMPLETE)
    return gcError("later phases must be invalidated before GC");

  std::set<uint64_t> ExplicitLive;
  for (const PluginLinkAtom &Atom : Graph.atoms())
    if ((Atom.Flags & NEVERC_LINK_ATOM_LIVE) != 0)
      ExplicitLive.insert(Atom.ID);

  std::map<uint64_t, LinkLivenessRecord> Records;
  std::deque<uint64_t> Work;
  auto Mark = [&](uint64_t AtomID, StringRef Reason) {
    PluginLinkAtom *Atom = Graph.findAtom(AtomID);
    if (!Atom)
      return;
    LinkLivenessRecord &Record = Records[AtomID];
    Record.AtomID = AtomID;
    Record.Live = true;
    if (llvm::none_of(Record.KeepReasons, [&](const std::string &Value) {
          return Value == Reason;
        }))
      Record.KeepReasons.push_back(Reason.str());
    if ((Atom->Flags & NEVERC_LINK_ATOM_LIVE) == 0) {
      Atom->Flags |= NEVERC_LINK_ATOM_LIVE;
      Work.push_back(AtomID);
    }
  };

  for (PluginLinkAtom &Atom : Graph.atoms())
    Atom.Flags &= ~NEVERC_LINK_ATOM_LIVE;
  for (const PluginLinkAtom &Atom : Graph.atoms()) {
    if ((Atom.Flags & NEVERC_LINK_ATOM_ROOT) != 0)
      Mark(Atom.ID, "atom-root");
    if (ExplicitLive.count(Atom.ID) != 0)
      Mark(Atom.ID, "plugin-root");
  }
  for (const PluginLinkSymbol &Symbol : Graph.symbols()) {
    if (Symbol.IsRoot)
      Mark(Symbol.AtomID, "symbol-root");
    if (Symbol.IsExported)
      Mark(Symbol.AtomID, "exported-symbol");
  }
  for (const PluginLinkExport &Export : Graph.exports()) {
    const PluginLinkSymbol *Symbol =
        Graph.findSymbol(Export.SymbolID);
    if (Symbol)
      Mark(Symbol->AtomID, "export");
  }
  for (const PluginLinkSynthetic &Synthetic : Graph.synthetics())
    Mark(Synthetic.AtomID, "required-synthetic");

  while (!Work.empty()) {
    const uint64_t AtomID = Work.front();
    Work.pop_front();
    const PluginLinkAtom *Atom = Graph.findAtom(AtomID);
    if (!Atom)
      continue;

    if (Atom->ComdatID != 0)
      for (const PluginLinkAtom &Peer : Graph.atoms())
        if (Peer.ComdatID == Atom->ComdatID)
          Mark(Peer.ID, "comdat-association");

    for (const PluginLinkEdge &Edge : Graph.edges()) {
      if (Edge.SourceAtomID != AtomID)
        continue;
      uint64_t TargetID = Edge.TargetAtomID;
      if (TargetID == 0) {
        const PluginLinkSymbol *Target =
            Graph.findSymbol(Edge.TargetSymbolID);
        TargetID = Target ? Target->AtomID : 0;
      }
      Mark(TargetID, edgeReason(Edge.Kind));
    }
    for (const PluginLinkUnwindRecord &Unwind :
         Graph.unwindRecords()) {
      if (Unwind.AtomID != AtomID)
        continue;
      const PluginLinkSymbol *Personality =
          Graph.findSymbol(Unwind.PersonalitySymbolID);
      if (Personality)
        Mark(Personality->AtomID, "unwind-personality");
    }
  }

  std::vector<LinkLivenessRecord> Result;
  Result.reserve(Graph.atoms().size());
  for (const PluginLinkAtom &Atom : Graph.atoms()) {
    auto It = Records.find(Atom.ID);
    if (It != Records.end()) {
      Result.push_back(std::move(It->second));
      continue;
    }
    LinkLivenessRecord Record;
    Record.AtomID = Atom.ID;
    Result.push_back(std::move(Record));
  }
  Graph.advanceGeneration();
  Graph.setState(NEVERC_LINK_STATE_GC_COMPLETE);
  if (Error E = verifyLinkLiveness(Graph))
    return std::move(E);
  return Result;
}

} // namespace neverc::plugin
