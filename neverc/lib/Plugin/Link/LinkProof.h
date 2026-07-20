#ifndef NEVERC_PLUGIN_LINK_LINKPROOF_H
#define NEVERC_PLUGIN_LINK_LINKPROOF_H

#include "neverc/Plugin/PluginLink.h"
#include <array>

namespace neverc::plugin {

class PluginLinkGraph;

struct PluginLinkProof {
  const PluginLinkGraph *Graph = nullptr;
  uint64_t GraphGeneration = 0;
  NevercLinkState State = NEVERC_LINK_STATE_INITIAL;
  NevercTargetID TargetID{};
  NevercObjectFormatID FormatID{};
  NevercInterfaceID OutputArtifact{};
  std::array<uint8_t, 32> RouteDigest{};
  std::array<uint8_t, 32> SemanticDigest{};
  uint64_t ImageBase = 0;
  uint64_t EntryAddress = 0;
};

} // namespace neverc::plugin

#endif
