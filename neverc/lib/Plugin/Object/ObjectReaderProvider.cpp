#include "neverc/Plugin/Host/ObjectPluginBridge.h"
#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Support/Errc.h"
#include <string>

using namespace llvm;

namespace neverc::plugin {
namespace {

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

const char *artifactName(NevercObjectArtifactKind Kind) {
  switch (Kind) {
  case NEVERC_OBJECT_ARTIFACT_RELOCATABLE:
    return "relocatable object";
  case NEVERC_OBJECT_ARTIFACT_ARCHIVE:
    return "archive";
  case NEVERC_OBJECT_ARTIFACT_EXECUTABLE_IMAGE:
    return "executable image";
  case NEVERC_OBJECT_ARTIFACT_SHARED_IMAGE:
    return "shared image";
  case NEVERC_OBJECT_ARTIFACT_UNIVERSAL_BINARY:
    return "universal binary";
  default:
    return "unknown artifact";
  }
}

Error statusError(StringRef Operation, NevercStatus Status,
                  const PluginTargetSnapshot::ObjectFormatRecord &Format) {
  return createStringError(
      errc::invalid_argument,
      Operation + " for plugin '" + Format.PluginID + "', format '" +
          Format.CanonicalName + "' failed with status " +
          std::to_string(Status.Code) + " (detail " +
          std::to_string(Status.Detail) + ")");
}

} // namespace

Expected<std::unique_ptr<ObjectReaderProvider>>
ObjectReaderProvider::create(
    std::shared_ptr<const PluginTargetSnapshot> Snapshot) {
  auto Registry = ObjectFormatRegistry::create(std::move(Snapshot));
  if (!Registry)
    return Registry.takeError();
  return std::unique_ptr<ObjectReaderProvider>(
      new ObjectReaderProvider(std::move(*Registry)));
}

Expected<std::unique_ptr<PluginObjectGraph>>
ObjectReaderProvider::read(
    PluginTaskContext &Task, ArrayRef<uint8_t> Input,
    StringRef LogicalPath, const OwnedTargetKey &Target,
    std::optional<NevercObjectFormatID> RequiredFormat) const {
  const NevercTargetKey TargetView = Target.view();
  NevercObjectFormatID SelectedFormat =
      RequiredFormat.value_or(TargetView.ObjectFormatID);
  if (!sameID(SelectedFormat, TargetView.ObjectFormatID))
    return createStringError(
        errc::invalid_argument,
        "object Reader format does not match the selected TargetKey");

  auto Match =
      Registry->probe(Task, Input, LogicalPath, TargetView, SelectedFormat);
  if (!Match)
    return Match.takeError();
  if (Match->ArtifactKind != NEVERC_OBJECT_ARTIFACT_RELOCATABLE)
    return createStringError(
        errc::invalid_argument,
        Twine("artifact-kind mismatch: object Reader cannot consume ") +
            artifactName(Match->ArtifactKind));
  if (!Match->Format || !Match->Format->Reader)
    return createStringError(
        errc::not_supported,
        "selected object Format has no Reader capability");

  auto Graph = std::make_unique<PluginObjectGraph>(Target);
  ObjectPluginBridge Bridge(Task, *Graph);
  auto GraphHandle = Bridge.graph();
  if (!GraphHandle)
    return GraphHandle.takeError();
  auto Mutation = Bridge.beginMutation();
  if (!Mutation)
    return Mutation.takeError();

  NevercBufferView Buffer{};
  Buffer.Header = {sizeof(Buffer), NEVERC_SOURCE_API_MAJOR,
                   NEVERC_SOURCE_API_MINOR, 0};
  Buffer.Data = Input.data();
  Buffer.Length = Input.size();
  Buffer.NullTerminated = NEVERC_FALSE;

  NevercObjectReadRequest Request{};
  Request.Header = {sizeof(Request), NEVERC_OBJECT_FORMAT_API_MAJOR,
                    Match->Format->APIMinor, 0};
  Request.Task = Task.handle();
  Request.Input = Buffer;
  Request.LogicalPath = {LogicalPath.data(), LogicalPath.size()};
  Request.Target = TargetView;
  Request.Object = Match->Format->Owner ? &Bridge.readOnlyAPI() : &Bridge.api();
  Request.Graph = *GraphHandle;
  Request.Mutation = *Mutation;

  bool CallbackThrew = false;
  auto InvokeReader = [&]() -> NevercStatus {
#if defined(__cpp_exceptions)
    try {
      return Match->Format->Reader(Match->Format->CallbackUserData, &Request);
    } catch (...) {
      CallbackThrew = true;
      NevercStatus Failure = neverc_status_ok();
      Failure.Code = NEVERC_STATUS_PLUGIN_FAILURE;
      return Failure;
    }
#else
    return Match->Format->Reader(Match->Format->CallbackUserData, &Request);
#endif
  };

  Expected<NevercStatus> Invoked = [&]() -> Expected<NevercStatus> {
    if (!Match->Format->Owner)
      return InvokeReader();
    return Task.invokeCallback(
        Match->Format->PluginID,
        "object-reader:" + Match->Format->CanonicalName,
        [&] {
          auto Capability =
              Task.currentArtifactMutationCapability(Match->Format);
          if (!Capability) {
            NevercStatus Failure = neverc_status_ok();
            Failure.Code = NEVERC_STATUS_CAPABILITY_UNAVAILABLE;
            return Failure;
          }
          Request.Object = &Bridge.capabilityAPI(Match->Format, *Capability);
          return InvokeReader();
        },
        true, nullptr, false, Match->Format);
  }();
  if (!Invoked) {
    if (Bridge.hasActiveMutation())
      (void)Bridge.abandonMutation(*Mutation);
    return Invoked.takeError();
  }
  NevercStatus Status = *Invoked;
  if (CallbackThrew) {
    if (Bridge.hasActiveMutation())
      (void)Bridge.abandonMutation(*Mutation);
    return createStringError(errc::invalid_argument,
                             "object Reader callback for plugin '" +
                                 Match->Format->PluginID + "', format '" +
                                 Match->Format->CanonicalName +
                                 "' threw an exception");
  }
  if (!neverc_status_is_ok(Status)) {
    if (Bridge.hasActiveMutation())
      (void)Bridge.abandonMutation(*Mutation);
    return statusError("object Reader callback", Status, *Match->Format);
  }
  if (!Bridge.hasActiveMutation())
    return createStringError(
        errc::invalid_argument,
        "object Reader callback closed its host-owned transaction");

  Status = Bridge.commitMutation(*Mutation);
  if (!neverc_status_is_ok(Status)) {
    if (Bridge.hasActiveMutation())
      (void)Bridge.abandonMutation(*Mutation);
    return statusError("object Reader transaction", Status, *Match->Format);
  }
  if (Error E = verifyPluginObjectGraph(*Graph))
    return joinErrors(
        createStringError(errc::invalid_argument,
                          "object Reader produced an invalid ObjectGraph"),
        std::move(E));
  return Graph;
}

} // namespace neverc::plugin
