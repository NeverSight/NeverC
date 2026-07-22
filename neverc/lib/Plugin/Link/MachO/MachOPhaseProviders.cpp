#include "MachOLinkGraphAdapter.h"
#include "Link/LinkPhaseExecutor.h"
#include "Link/LinkPhaseRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Support/Errc.h"

using namespace llvm;
using namespace neverc::plugin;

namespace linker::macho {
namespace {

Error phaseError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "Mach-O link phase adapter: " + Message);
}

} // namespace

Error MachOLinkGraphAdapter::advanceTo(NevercLinkState State) {
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
              // Interceptors may mutate the input before invoking the native
              // continuation. Apply those deltas before taking the completed
              // native phase snapshot.
              if (Error Delta = applyDelta(*Graph, Input, Input.state()))
                return std::move(Delta);
              auto Captured = capture(Input, Transition->OutputState);
              if (!Captured)
                return Captured.takeError();
              LastNative = *Captured;
              return *Captured;
            }))
      return joinErrors(
          phaseError("could not install native Mach-O phase provider"),
          std::move(E));

    auto Output = (*Pipeline)->execute(Graph, Transition->OutputState);
    if (!Output)
      return joinErrors(phaseError("plugin phase execution failed"),
                        Output.takeError());

    // A replacement Provider may bypass the native continuation entirely.
    // Capture the native baseline now so its graph delta can still be applied
    // through the same projection path.
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

} // namespace linker::macho
