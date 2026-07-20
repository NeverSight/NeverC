#include "ThunkStubProvider.h"
#include "llvm/Support/Errc.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

Error thunkError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "link thunk provider: " + Message);
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

bool isThunk(const PluginLinkGraph &Graph, uint64_t AtomID) {
  for (const PluginLinkSynthetic &Synthetic : Graph.synthetics())
    if (Synthetic.AtomID == AtomID &&
        (Synthetic.Role == "thunk" || Synthetic.Role == "stub"))
      return true;
  return false;
}

} // namespace

Expected<std::vector<LinkThunkRecord>>
insertRequiredLinkThunks(PluginLinkGraph &Graph) {
  if (Graph.state() != NEVERC_LINK_STATE_SYNTHETICS_READY)
    return thunkError("synthetics are not ready");

  std::vector<uint64_t> EdgeIDs;
  EdgeIDs.reserve(Graph.edges().size());
  for (const PluginLinkEdge &Edge : Graph.edges())
    EdgeIDs.push_back(Edge.ID);

  std::vector<LinkThunkRecord> Records;
  for (uint64_t EdgeID : EdgeIDs) {
    PluginLinkEdge *Edge = Graph.findEdge(EdgeID);
    if (!Edge || !Edge->IsPCRelative || !Edge->IsSigned ||
        Edge->Width == 0)
      continue;
    const PluginLinkAtom *Source =
        Graph.findAtom(Edge->SourceAtomID);
    const PluginLinkAtom *Target = edgeTarget(Graph, *Edge);
    if (!Source || !Target)
      continue;
    const int64_t Place = static_cast<int64_t>(
        Source->Address + Edge->Offset + Edge->Width / 8);
    const int64_t Value =
        static_cast<int64_t>(Target->Address) + Edge->Addend - Place;
    if (fitsSigned(Value, Edge->Width))
      continue;
    if (isThunk(Graph, Target->ID))
      return thunkError("existing thunk remains out of range");

    const uint64_t OriginalTargetAtom = Edge->TargetAtomID;
    const uint64_t OriginalTargetSymbol = Edge->TargetSymbolID;
    PluginLinkAtom Thunk;
    Thunk.SectionID = Source->SectionID;
    Thunk.Name = "__neverc_thunk_" + std::to_string(Edge->ID);
    Thunk.Flags = NEVERC_LINK_ATOM_LIVE |
                  NEVERC_LINK_ATOM_SYNTHETIC;
    Thunk.Alignment = 1;
    Thunk.Address = Source->Address + Source->Content.size() +
                    Source->ZeroFillSize;
    Thunk.Content.assign(4, 0);
    Thunk.Origin = Edge->Origin;
    const uint64_t ThunkAtomID =
        Graph.addAtom(std::move(Thunk)).ID;

    PluginLinkSynthetic Synthetic;
    Synthetic.Role = "thunk";
    Synthetic.SectionID = Source->SectionID;
    Synthetic.AtomID = ThunkAtomID;
    Synthetic.Origin = Edge->Origin;
    const uint64_t SyntheticID =
        Graph.addSynthetic(std::move(Synthetic)).ID;

    PluginLinkEdge ThunkEdge;
    ThunkEdge.Kind = NEVERC_LINK_EDGE_ASSOCIATION;
    ThunkEdge.SourceAtomID = ThunkAtomID;
    ThunkEdge.Width = 32;
    ThunkEdge.TargetAtomID = OriginalTargetAtom;
    ThunkEdge.TargetSymbolID = OriginalTargetSymbol;
    ThunkEdge.Origin = Edge->Origin;
    Graph.addEdge(std::move(ThunkEdge));

    Edge = Graph.findEdge(EdgeID);
    Edge->TargetAtomID = ThunkAtomID;
    Edge->TargetSymbolID = 0;
    if (PluginLinkSection *Section =
            Graph.findSection(Source->SectionID))
      Section->Size += 4;
    Records.push_back({EdgeID, Target->ID, ThunkAtomID,
                       SyntheticID, "branch-range"});
  }
  return Records;
}

} // namespace neverc::plugin
