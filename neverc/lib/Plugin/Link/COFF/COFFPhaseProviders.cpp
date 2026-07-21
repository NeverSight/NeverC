#include "COFFLinkGraphAdapter.h"
#include "Link/LinkPhaseExecutor.h"
#include "Link/LinkPhaseRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Support/Errc.h"

using namespace llvm;
using namespace neverc::plugin;

namespace linker::coff {
namespace {

Error phaseError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "COFF link phase adapter: " + Message);
}

} // namespace

Error COFFLinkGraphAdapter::advanceTo(NevercLinkState State) {
  if (!Graph)
    return phaseError("canonical LinkGraph is unavailable");
  if (State < Graph->state())
    return phaseError("cannot move the native projection backwards");
  if (State > NEVERC_LINK_STATE_IMAGE_EMITTED)
    return phaseError("requested LinkGraph state is invalid");

  while (Graph->state() < State) {
    auto Pipeline = LinkPhasePipeline::create(Task);
    if (!Pipeline)
      return joinErrors(phaseError("could not create phase executor"),
                        Pipeline.takeError());

    const LinkTransitionDefinition *Transition = nullptr;
    for (const LinkTransitionDefinition &Candidate :
         (*Pipeline)->registry().transitions()) {
      if (Candidate.InputState == Graph->state()) {
        Transition = &Candidate;
        break;
      }
    }
    if (!Transition)
      return phaseError("current native state has no outgoing transition");
    if (Transition->OutputState > State)
      return phaseError("requested state is not a transition boundary");

    std::shared_ptr<PluginLinkGraph> LastNative;
    if (Error E = (*Pipeline)->setBuiltinGraphProvider(
            Transition->Phase,
            [&](const PluginLinkGraph &Input)
                -> Expected<std::shared_ptr<PluginLinkGraph>> {
              if (Error Delta = applyDelta(*Graph, Input, Input.state()))
                return std::move(Delta);
              auto Captured = capture(Input, Transition->OutputState);
              if (!Captured)
                return Captured.takeError();
              LastNative = *Captured;
              return *Captured;
            }))
      return joinErrors(
          phaseError("could not install native COFF phase provider"),
          std::move(E));

    auto Output = (*Pipeline)->execute(Graph, Transition->OutputState);
    if (!Output)
      return joinErrors(phaseError("plugin phase execution failed"),
                        Output.takeError());

    if (!LastNative) {
      auto Captured = capture(*Graph, Transition->OutputState);
      if (!Captured)
        return Captured.takeError();
      LastNative = std::move(*Captured);
    }
    if (Error E = applyDelta(*LastNative, **Output, Transition->OutputState))
      return joinErrors(phaseError("could not apply plugin graph delta"),
                        std::move(E));
    Graph = std::move(*Output);
  }
  return Error::success();
}

} // namespace linker::coff
