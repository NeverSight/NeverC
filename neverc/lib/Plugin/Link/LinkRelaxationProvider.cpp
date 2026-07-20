#include "LinkRelaxationProvider.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

Error relaxationError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "link relaxation provider: " + Message);
}

bool isThunk(const PluginLinkGraph &Graph, uint64_t AtomID) {
  for (const PluginLinkSynthetic &Synthetic : Graph.synthetics())
    if (Synthetic.AtomID == AtomID &&
        (Synthetic.Role == "thunk" || Synthetic.Role == "stub"))
      return true;
  return false;
}

const PluginLinkAtom *edgeTarget(const PluginLinkGraph &Graph,
                                 const PluginLinkEdge &Edge) {
  if (Edge.TargetAtomID != 0)
    return Graph.findAtom(Edge.TargetAtomID);
  const PluginLinkSymbol *Symbol =
      Graph.findSymbol(Edge.TargetSymbolID);
  return Symbol ? Graph.findAtom(Symbol->AtomID) : nullptr;
}

bool fitsSigned(int64_t Value, uint32_t Width) {
  if (Width == 0 || Width >= 64)
    return true;
  const int64_t Minimum = -(INT64_C(1) << (Width - 1));
  const int64_t Maximum = (INT64_C(1) << (Width - 1)) - 1;
  return Value >= Minimum && Value <= Maximum;
}

} // namespace

Error assignProvisionalLinkAddresses(PluginLinkGraph &Graph) {
  if (Graph.state() != NEVERC_LINK_STATE_SYNTHETICS_READY)
    return relaxationError("synthetics are not ready");
  uint64_t ImageCursor = 0;
  for (PluginLinkSection &Section : Graph.sections()) {
    if (!isPowerOf2_64(Section.Alignment))
      return relaxationError("section alignment is invalid");
    ImageCursor = alignTo(ImageCursor, Section.Alignment);
    Section.Address = ImageCursor;
    uint64_t SectionCursor = 0;
    for (PluginLinkAtom &Atom : Graph.atoms()) {
      if (Atom.SectionID != Section.ID || isThunk(Graph, Atom.ID))
        continue;
      if (!isPowerOf2_64(Atom.Alignment))
        return relaxationError("atom alignment is invalid");
      SectionCursor = alignTo(SectionCursor, Atom.Alignment);
      Atom.Address = Section.Address + SectionCursor;
      SectionCursor += Atom.Content.size() + Atom.ZeroFillSize;
    }
    Section.Size = std::max(Section.Size, SectionCursor);
    ImageCursor = Section.Address + Section.Size;
  }
  return Error::success();
}

Expected<std::vector<LinkRelaxationRecord>>
relaxLinkEdges(PluginLinkGraph &Graph) {
  if (Graph.state() != NEVERC_LINK_STATE_SYNTHETICS_READY)
    return relaxationError("synthetics are not ready");
  std::vector<LinkRelaxationRecord> Records;
  for (PluginLinkEdge &Edge : Graph.edges()) {
    if (!Edge.IsPCRelative || !Edge.IsSigned || Edge.Width <= 8)
      continue;
    const PluginLinkAtom *Source =
        Graph.findAtom(Edge.SourceAtomID);
    const PluginLinkAtom *Target = edgeTarget(Graph, Edge);
    if (!Source || !Target)
      continue;
    const int64_t Place = static_cast<int64_t>(
        Source->Address + Edge.Offset + 1);
    const int64_t Value =
        static_cast<int64_t>(Target->Address) + Edge.Addend - Place;
    uint32_t NewWidth = Edge.Width;
    if (fitsSigned(Value, 8))
      NewWidth = 8;
    else if (Edge.Width > 16 && fitsSigned(Value, 16))
      NewWidth = 16;
    if (NewWidth == Edge.Width)
      continue;
    const uint32_t OldWidth = Edge.Width;
    Edge.Width = NewWidth;
    Records.push_back(
        {Edge.ID, OldWidth, NewWidth,
         -static_cast<int64_t>((OldWidth - NewWidth) / 8),
         "pc-relative-range"});
  }
  return Records;
}

} // namespace neverc::plugin
