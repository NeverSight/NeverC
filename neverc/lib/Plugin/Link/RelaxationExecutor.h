#ifndef NEVERC_PLUGIN_LINK_RELAXATIONEXECUTOR_H
#define NEVERC_PLUGIN_LINK_RELAXATIONEXECUTOR_H

#include "LinkRelaxationProvider.h"
#include "ThunkStubProvider.h"
#include "llvm/Support/Error.h"
#include <vector>

namespace neverc::plugin {

struct LinkRelaxationRound {
  uint32_t Round = 0;
  std::vector<LinkThunkRecord> Thunks;
  std::vector<LinkRelaxationRecord> Relaxations;
};

struct LinkRelaxationResult {
  uint32_t RoundCount = 0;
  std::vector<LinkRelaxationRound> Rounds;
};

llvm::Expected<LinkRelaxationResult>
executeLinkRelaxation(PluginLinkGraph &Graph,
                      uint32_t MaximumRounds = 16);

} // namespace neverc::plugin

#endif
