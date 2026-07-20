#ifndef NEVERC_PLUGIN_LINK_LINKLAYOUTPROVIDER_H
#define NEVERC_PLUGIN_LINK_LINKLAYOUTPROVIDER_H

#include "LinkGraph.h"
#include "llvm/Support/Error.h"
#include <array>

namespace neverc::plugin {

struct LinkLayoutOptions {
  uint64_t ImageBase = UINT64_C(0x10000);
  uint64_t FileBase = UINT64_C(0x1000);
  uint64_t PageSize = UINT64_C(0x1000);
  bool EnforceWritableXorExecutable = true;
};

struct LinkLayoutResult {
  uint64_t GraphGeneration = 0;
  uint64_t ImageBase = 0;
  uint64_t EntryAddress = 0;
  std::array<uint8_t, 32> RangeDigest{};
};

llvm::Expected<LinkLayoutResult>
layoutLinkGraph(PluginLinkGraph &Graph,
                const LinkLayoutOptions &Options = {});

} // namespace neverc::plugin

#endif
