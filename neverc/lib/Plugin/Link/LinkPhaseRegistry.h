#ifndef NEVERC_PLUGIN_LINK_LINKPHASEREGISTRY_H
#define NEVERC_PLUGIN_LINK_LINKPHASEREGISTRY_H

#include "neverc/Plugin/Host/PluginPhaseGraph.h"
#include "neverc/Plugin/PluginLink.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include <vector>

namespace neverc::plugin {

struct LinkTransitionDefinition {
  NevercInterfaceID Phase{};
  NevercLinkState InputState = NEVERC_LINK_STATE_INITIAL;
  NevercLinkState OutputState = NEVERC_LINK_STATE_INITIAL;
  uint32_t MaximumReruns = 1;
};

class LinkPhaseRegistry {
public:
  static llvm::Expected<LinkPhaseRegistry> create();

  const PluginPhaseGraph &graph() const { return Graph; }
  llvm::ArrayRef<LinkTransitionDefinition> transitions() const {
    return Transitions;
  }
  const LinkTransitionDefinition *
  findTransition(NevercInterfaceID Phase) const;

private:
  PluginPhaseGraph Graph;
  std::vector<LinkTransitionDefinition> Transitions;
};

} // namespace neverc::plugin

#endif
