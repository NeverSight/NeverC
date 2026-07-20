#include "ObjectMergeProvider.h"
#include "neverc/Plugin/Host/ObjectPluginBridge.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "llvm/Support/Errc.h"
#include <cstring>
#include <utility>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

bool equalID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool equalHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

Error statusError(StringRef Operation, NevercStatus Status,
                  const PluginLinkSnapshot::ObjectMergeProviderRecord
                      &Provider) {
  return createStringError(
      errc::invalid_argument,
      Operation + " for plugin '" + Provider.PluginID + "', provider '" +
          Provider.ProviderID + "' failed with status " +
          std::to_string(Status.Code) + " (detail " +
          std::to_string(Status.Detail) + ")");
}

} // namespace

Expected<ObjectMergeResult>
executeObjectMergeProvider(
    PluginTaskContext &Task,
    const PluginLinkSnapshot::ObjectMergeProviderRecord &Provider,
    OwnedTargetKey Target, ArrayRef<PluginObjectGraph *> Objects,
    NevercLinkOptionFlags Flags) {
  if (!Provider.Merge)
    return createStringError(errc::invalid_argument,
                             "object-merge provider has no callback");
  if (Objects.empty())
    return createStringError(errc::invalid_argument,
                             "object-merge provider requires input objects");

  const NevercTargetKey TargetView = Target.view();
  if (!equalID(Provider.TargetID, TargetView.TargetID) ||
      !equalID(Provider.FormatID, TargetView.ObjectFormatID))
    return createStringError(
        errc::invalid_argument,
        "object-merge provider route does not match the requested target");
  for (const PluginObjectGraph *Object : Objects) {
    if (!Object)
      return createStringError(errc::invalid_argument,
                               "object-merge input graph is null");
    const NevercTargetKey InputTarget = Object->targetKey();
    if (!equalID(InputTarget.TargetID, TargetView.TargetID) ||
        !equalID(InputTarget.ObjectFormatID, TargetView.ObjectFormatID))
      return createStringError(
          errc::invalid_argument,
          "object-merge input target or format does not match the route");
    if (Error E = verifyPluginObjectGraph(*Object))
      return joinErrors(
          createStringError(errc::invalid_argument,
                            "object-merge input ObjectGraph is invalid"),
          std::move(E));
  }

  NevercStatus Cancelled = Task.checkCancelled();
  if (!neverc_status_is_ok(Cancelled))
    return statusError("object-merge cancellation check", Cancelled,
                       Provider);

  std::vector<std::unique_ptr<ObjectPluginBridge>> InputBridges;
  std::vector<NevercObjectMergeInput> InputViews;
  InputBridges.reserve(Objects.size());
  InputViews.reserve(Objects.size());
  for (PluginObjectGraph *Object : Objects) {
    auto Bridge =
        std::make_unique<ObjectPluginBridge>(Task, *Object, false);
    auto Handle = Bridge->graph();
    if (!Handle)
      return Handle.takeError();
    NevercObjectMergeInput Input{};
    Input.Header = {sizeof(Input), NEVERC_LINK_API_MAJOR,
                    NEVERC_LINK_API_MINOR, 0};
    Input.Object = &Bridge->api();
    Input.Graph = *Handle;
    InputViews.push_back(Input);
    InputBridges.push_back(std::move(Bridge));
  }

  auto Output = std::make_unique<PluginObjectGraph>(std::move(Target));
  ObjectPluginBridge OutputBridge(Task, *Output);
  auto OutputHandle = OutputBridge.graph();
  if (!OutputHandle)
    return OutputHandle.takeError();
  auto Mutation = OutputBridge.beginMutation();
  if (!Mutation)
    return Mutation.takeError();

  NevercObjectMergeRequest Request{};
  Request.Header = {sizeof(Request), NEVERC_LINK_API_MAJOR,
                    NEVERC_LINK_API_MINOR, 0};
  Request.Task = Task.handle();
  Request.Target = Output->targetKey();
  Request.FormatID = Request.Target.ObjectFormatID;
  Request.Objects = {InputViews.data(), InputViews.size(),
                     sizeof(NevercObjectMergeInput)};
  Request.Flags = Flags;
  Request.OutputObject = &OutputBridge.api();
  Request.OutputGraph = *OutputHandle;
  Request.OutputMutation = *Mutation;

  NevercObjectMergeCandidate Candidate{};
  Candidate.Header = {sizeof(Candidate), NEVERC_LINK_API_MAJOR,
                      NEVERC_LINK_API_MINOR, 0};

  auto Invoke = [&]() {
    return Provider.Merge(Provider.UserData, Task.handle(), &Request,
                          &Candidate);
  };
  Expected<NevercStatus> Invoked = Provider.Builtin
                                       ? Expected<NevercStatus>(Invoke())
                                       : Task.session().invokeCallback(
                                             Provider.PluginID,
                                             "object-merge:" +
                                                 Provider.ProviderID,
                                             Invoke, true, &Task);
  if (!Invoked) {
    if (OutputBridge.hasActiveMutation())
      (void)OutputBridge.abandonMutation(*Mutation);
    return Invoked.takeError();
  }
  if (!neverc_status_is_ok(*Invoked)) {
    if (OutputBridge.hasActiveMutation())
      (void)OutputBridge.abandonMutation(*Mutation);
    return statusError("object-merge callback", *Invoked, Provider);
  }
  if (!OutputBridge.hasActiveMutation())
    return createStringError(
        errc::invalid_argument,
        "object-merge callback closed its host-owned output transaction");
  if (!equalHandle(Candidate.Object, *OutputHandle)) {
    (void)OutputBridge.abandonMutation(*Mutation);
    return createStringError(
        errc::invalid_argument,
        "object-merge callback returned a foreign output ObjectGraph");
  }
  if (!equalID(Candidate.ProductID, Provider.ProductID)) {
    (void)OutputBridge.abandonMutation(*Mutation);
    return createStringError(
        errc::invalid_argument,
        "object-merge callback returned an unexpected product ID");
  }

  NevercStatus Commit = OutputBridge.commitMutation(*Mutation);
  if (!neverc_status_is_ok(Commit)) {
    if (OutputBridge.hasActiveMutation())
      (void)OutputBridge.abandonMutation(*Mutation);
    return statusError("object-merge output transaction", Commit, Provider);
  }
  if (Error E = verifyPluginObjectGraph(*Output))
    return joinErrors(
        createStringError(errc::invalid_argument,
                          "object-merge provider produced an invalid "
                          "ObjectGraph"),
        std::move(E));

  ObjectMergeResult Result;
  Result.Object = std::move(Output);
  Result.ProductID = Candidate.ProductID;
  std::memcpy(Result.ProducerRouteDigest.data(),
              Candidate.ProducerRouteDigest,
              Result.ProducerRouteDigest.size());
  Result.PluginID = Provider.PluginID;
  Result.ProviderID = Provider.ProviderID;
  return Result;
}

} // namespace neverc::plugin
