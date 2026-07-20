#include "RelaxationExecutor.h"
#include "SynthesisVerifier.h"
#include "llvm/Support/Errc.h"

using namespace llvm;

namespace neverc::plugin {

Expected<LinkRelaxationResult>
executeLinkRelaxation(PluginLinkGraph &Graph,
                      uint32_t MaximumRounds) {
  if (Graph.state() != NEVERC_LINK_STATE_SYNTHETICS_READY ||
      MaximumRounds == 0)
    return createStringError(
        errc::invalid_argument,
        "link relaxation bounds or input state are invalid");

  LinkRelaxationResult Result;
  bool Converged = false;
  for (uint32_t Round = 1; Round <= MaximumRounds; ++Round) {
    const std::array<uint8_t, 32> Before = Graph.semanticDigest();
    if (Error E = assignProvisionalLinkAddresses(Graph))
      return std::move(E);
    auto Thunks = insertRequiredLinkThunks(Graph);
    if (!Thunks)
      return Thunks.takeError();
    auto Relaxations = relaxLinkEdges(Graph);
    if (!Relaxations)
      return Relaxations.takeError();

    LinkRelaxationRound Record;
    Record.Round = Round;
    Record.Thunks = std::move(*Thunks);
    Record.Relaxations = std::move(*Relaxations);
    Result.Rounds.push_back(std::move(Record));
    Result.RoundCount = Round;
    if (Graph.semanticDigest() == Before) {
      Converged = true;
      break;
    }
  }
  if (!Converged) {
    const LinkRelaxationRound &Last = Result.Rounds.back();
    return createStringError(
        errc::invalid_argument,
        "link relaxation did not converge after %u rounds; "
        "last round changed %zu thunks and %zu encodings",
        MaximumRounds, Last.Thunks.size(),
        Last.Relaxations.size());
  }
  Graph.advanceGeneration();
  Graph.setState(NEVERC_LINK_STATE_THUNKS_RELAXED);
  if (Error E = verifyLinkRelaxation(Graph))
    return std::move(E);
  return Result;
}

} // namespace neverc::plugin
