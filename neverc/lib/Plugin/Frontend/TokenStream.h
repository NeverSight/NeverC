#ifndef NEVERC_LIB_PLUGIN_FRONTEND_TOKENSTREAM_H
#define NEVERC_LIB_PLUGIN_FRONTEND_TOKENSTREAM_H

#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Scan/Token.h"
#include <cstdint>
#include <vector>

namespace neverc {
class PrepEngine;
}

namespace neverc::plugin {

namespace prep_bridge_detail {

struct PrepTokenStreamArtifact {
  PrepEngine *Engine = nullptr;
  std::vector<Token> Tokens;
  std::vector<PluginDependencySnapshot> Dependencies;
  bool BuiltinLazy = false;
};

struct TokenStreamBuilderPayload {
  PrepEngine *Engine = nullptr;
  std::vector<std::vector<Token>> Chunks;
  uint64_t TokenCount = 0;
  bool Committed = false;
};

} // namespace prep_bridge_detail
} // namespace neverc::plugin

#endif
