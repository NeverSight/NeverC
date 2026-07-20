#include "RelocationVerifier.h"
#include "LayoutVerifier.h"
#include "RelocationProvider.h"
#include "llvm/Support/Errc.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

Error relocationError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "link relocation verification: " + Message);
}

} // namespace

Error verifyLinkRelocations(const PluginLinkGraph &Graph) {
  if (Graph.state() < NEVERC_LINK_STATE_RELOCATIONS_APPLIED)
    return relocationError("graph has no relocation result");
  if (Error E = verifyLinkLayout(Graph))
    return E;
  const bool LittleEndian =
      Graph.targetKey().Endianness == NEVERC_TARGET_ENDIAN_LITTLE;
  for (const PluginLinkEdge &Edge : Graph.edges()) {
    if (Edge.Kind != NEVERC_LINK_EDGE_RELOCATION)
      continue;
    auto Expected = evaluateLinkRelocation(Graph, Edge);
    if (!Expected)
      return Expected.takeError();
    if (Expected->Dynamic)
      continue;
    const PluginLinkAtom *Source =
        Graph.findAtom(Edge.SourceAtomID);
    const uint32_t ByteCount = Edge.Width / 8;
    uint64_t Encoded = 0;
    for (uint32_t Index = 0; Index != ByteCount; ++Index) {
      const uint32_t Shift =
          LittleEndian ? Index * 8 : (ByteCount - Index - 1) * 8;
      Encoded |=
          static_cast<uint64_t>(Source->Content[Edge.Offset + Index])
          << Shift;
    }
    if (Encoded != Expected->EncodedValue)
      return relocationError("applied relocation bytes do not match");
  }
  return Error::success();
}

} // namespace neverc::plugin
