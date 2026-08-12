#include "neverc/Plugin/Host/ObjectWriterProvider.h"
#include "../AndroidKernelReleaseWriterPolicy.h"
#include "BuiltinLLVMObjectWriter.h"
#include "BuiltinObjectWriterPreflight.h"
#include "neverc/Plugin/Host/MutableBinaryBuilder.h"
#include "neverc/Plugin/Host/ObjectPluginBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Errc.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

Error statusError(StringRef Operation, NevercStatus Status,
                  const PluginTargetSnapshot::ObjectFormatRecord &Format) {
  return createStringError(errc::invalid_argument,
                           Operation + " for plugin '" + Format.PluginID +
                               "', format '" + Format.CanonicalName +
                               "' failed with status " + Twine(Status.Code) +
                               " (detail " + Twine(Status.Detail) + ")");
}

Expected<const NevercIOAPI *> getOutputAPI(PluginTaskContext &Task) {
  auto Query = Task.processServices().interfaces().query(
      ioPluginInterfaceID(), NEVERC_IO_API_MAJOR, NEVERC_IO_API_MINOR);
  if (!Query)
    return Query.takeError();
  auto *API = static_cast<const NevercIOAPI *>(Query->Table);
  if (!API)
    return createStringError(errc::not_supported,
                             "plugin IO interface returned a null table");
  return API;
}

NevercObjectLayoutProofInfo
makeLayoutReport(const PluginObjectLayoutProof &Proof) {
  NevercObjectLayoutProofInfo Report{};
  Report.Header = {sizeof(Report), NEVERC_OBJECT_API_MAJOR,
                   NEVERC_OBJECT_API_MINOR, 0};
  Report.GraphGeneration = Proof.GraphGeneration;
  Report.TargetID = Proof.TargetID;
  Report.FormatID = Proof.FormatID;
  return Report;
}

} // namespace

ObjectOutputDestination ObjectOutputDestination::memory(StringRef LogicalName,
                                                        uint64_t SizeBudget) {
  return {ObjectOutputDestinationKind::Memory, LogicalName.str(), SizeBudget,
          ObjectWritePolicy::Default, false};
}

ObjectOutputDestination ObjectOutputDestination::file(StringRef FinalPath,
                                                      uint64_t SizeBudget) {
  return {ObjectOutputDestinationKind::File, FinalPath.str(), SizeBudget,
          ObjectWritePolicy::Default, false};
}

Expected<std::unique_ptr<ObjectWriterProvider>> ObjectWriterProvider::create(
    std::shared_ptr<const PluginTargetSnapshot> Snapshot) {
  auto Registry = ObjectFormatRegistry::create(std::move(Snapshot));
  if (!Registry)
    return Registry.takeError();
  return std::unique_ptr<ObjectWriterProvider>(
      new ObjectWriterProvider(std::move(*Registry)));
}

Expected<std::unique_ptr<PluginObjectImage>> ObjectWriterProvider::beginWrite(
    PluginTaskContext &Task, PluginObjectGraph &Graph,
    const ObjectOutputDestination &Destination) const {
  if (Destination.Name.empty() ||
      Destination.Name.find('\0') != std::string::npos ||
      Destination.SizeBudget == 0)
    return createStringError(
        errc::invalid_argument,
        "object output destination requires a name and nonzero budget");
  uint64_t RequestFlags = 0;
  switch (Destination.WritePolicy) {
  case ObjectWritePolicy::Default:
    if (Destination.DropDebugInfo)
      return createStringError(
          errc::invalid_argument,
          "debug stripping requires an explicit ELF object write policy");
    break;
  case ObjectWritePolicy::CanonicalELFTables:
    RequestFlags = NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES;
    break;
  case ObjectWritePolicy::AndroidKernelRelease:
    RequestFlags = NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES |
                   NEVERC_OBJECT_WRITE_ANDROID_KERNEL_RELEASE;
    break;
  default:
    return createStringError(errc::invalid_argument,
                             "object output destination has an unknown write "
                             "policy");
  }
  if (Destination.DropDebugInfo)
    RequestFlags |= NEVERC_OBJECT_WRITE_DROP_DEBUG_INFO;
  if (Error E = verifyPluginObjectGraph(Graph))
    return joinErrors(
        createStringError(errc::invalid_argument,
                          "object Writer received an invalid ObjectGraph"),
        std::move(E));

  const auto *Format = Registry->find(Graph.formatID());
  if (!Format || !Format->Writer ||
      (Format->Flags & NEVERC_OBJECT_FORMAT_CAN_WRITE) == 0)
    return createStringError(errc::not_supported,
                             "selected object Format has no Writer capability");
  if (RequestFlags != 0 &&
      Format->APIMinor < NEVERC_OBJECT_WRITE_REQUEST_FLAGS_API_MINOR)
    return createStringError(
        errc::not_supported,
        "explicit writer policies require object Format API minor " +
            Twine(NEVERC_OBJECT_WRITE_REQUEST_FLAGS_API_MINOR) + " or newer");

  if (Format->Writer == &writeBuiltinLLVMObject)
    if (Error E = verifyBuiltinObjectWriterGraphRepresentability(
            Graph, "built-in object Writer preflight"))
      return std::move(E);

  const PluginObjectLayoutProof *Proof = Graph.layoutProof();
  const NevercTargetKey Target = Graph.targetKey();
  if (!Proof || Proof->GraphGeneration != Graph.generation() ||
      !sameID(Proof->TargetID, Target.TargetID) ||
      !sameID(Proof->FormatID, Graph.formatID()))
    return createStringError(errc::operation_not_permitted,
                             "object Writer requires a current layout proof "
                             "for the selected target");

  if ((RequestFlags & NEVERC_OBJECT_WRITE_ANDROID_KERNEL_RELEASE) != 0 &&
      Format->Writer == &writeBuiltinLLVMObject)
    if (Error E = verifyPortableAndroidKernelReleaseWriterGraph(
            Graph, "built-in Android release object Writer preflight"))
      return std::move(E);

  auto OutputAPI = getOutputAPI(Task);
  if (!OutputAPI)
    return OutputAPI.takeError();

  NevercOutputSinkHandle Sink{};
  NevercStringView Name{Destination.Name.data(), Destination.Name.size()};
  NevercStatus Status{};
  switch (Destination.Kind) {
  case ObjectOutputDestinationKind::Memory:
    Status = (*OutputAPI)
                 ->BeginMemoryOutput((*OutputAPI)->Context, Task.handle(), Name,
                                     Destination.SizeBudget, &Sink);
    break;
  case ObjectOutputDestinationKind::File:
    Status = (*OutputAPI)
                 ->BeginFileOutput((*OutputAPI)->Context, Task.handle(), Name,
                                   Destination.SizeBudget, &Sink);
    break;
  }
  if (!neverc_status_is_ok(Status))
    return statusError("object output begin", Status, *Format);

  auto AbortOutput = make_scope_exit([&] {
    (void)(*OutputAPI)->OutputAbort((*OutputAPI)->Context, Task.handle(), Sink);
  });
  auto Binary = MutableBinaryBuilder::create(Task, **OutputAPI, Sink);
  if (!Binary)
    return Binary.takeError();

  ObjectPluginBridge Bridge(Task, Graph, false);
  auto GraphHandle = Bridge.graph();
  if (!GraphHandle)
    return GraphHandle.takeError();
  auto ProofHandle = Bridge.layoutProof();
  if (!ProofHandle)
    return ProofHandle.takeError();

  NevercObjectWriteRequest Request{};
  Request.Header = {sizeof(Request), NEVERC_OBJECT_FORMAT_API_MAJOR,
                    Format->APIMinor, RequestFlags};
  Request.Task = Task.handle();
  Request.Target = Target;
  Request.FormatID = Format->ID;
  Request.Object = &Bridge.api();
  Request.Graph = *GraphHandle;
  Request.LayoutProof = *ProofHandle;
  Request.Binary =
      Format->Owner ? &(*Binary)->readOnlyAPI() : &(*Binary)->api();
  Request.Builder = (*Binary)->handle();

  bool CallbackThrew = false;
  auto InvokeWriter = [&]() -> NevercStatus {
#if defined(__cpp_exceptions)
    try {
      return Format->Writer(Format->CallbackUserData, &Request);
    } catch (...) {
      CallbackThrew = true;
      NevercStatus Failure = neverc_status_ok();
      Failure.Code = NEVERC_STATUS_PLUGIN_FAILURE;
      return Failure;
    }
#else
    return Format->Writer(Format->CallbackUserData, &Request);
#endif
  };
  Expected<NevercStatus> Invoked = [&]() -> Expected<NevercStatus> {
    if (!Format->Owner)
      return InvokeWriter();
    return Task.invokeCallback(
        Format->PluginID, "object-writer:" + Format->CanonicalName,
        [&] {
          auto Capability = Task.currentArtifactMutationCapability(Format);
          if (!Capability) {
            NevercStatus Failure = neverc_status_ok();
            Failure.Code = NEVERC_STATUS_CAPABILITY_UNAVAILABLE;
            return Failure;
          }
          Request.Binary = &(*Binary)->capabilityAPI(Format, *Capability);
          return InvokeWriter();
        },
        true, nullptr, false, Format);
  }();
  if (!Invoked)
    return Invoked.takeError();
  Status = *Invoked;
  if (CallbackThrew)
    return createStringError(
        errc::invalid_argument,
        "object Writer callback for plugin '" + Format->PluginID +
            "', format '" + Format->CanonicalName + "' threw an exception");
  if (!neverc_status_is_ok(Status))
    return statusError("object Writer callback", Status, *Format);
  if (Graph.generation() != Proof->GraphGeneration ||
      Graph.layoutProof() != Proof)
    return createStringError(
        errc::invalid_argument,
        "object Writer invalidated its host-owned layout proof");

  const std::string Provenance =
      "writer:" + Format->PluginID + ":" + Format->CanonicalName;
  auto Image = PluginObjectImage::createPending(
      Task, Format->ID, Target.TargetID, Graph.generation(), std::move(*Binary),
      Provenance, makeLayoutReport(*Proof));
  if (!Image)
    return Image.takeError();
  AbortOutput.release();
  return Image;
}

Expected<std::unique_ptr<PluginObjectImage>> ObjectWriterProvider::beginImage(
    PluginTaskContext &Task, NevercObjectFormatID FormatID,
    NevercTargetID TargetID, uint64_t GraphGeneration, ArrayRef<uint8_t> Bytes,
    const ObjectOutputDestination &Destination) const {
  if (Bytes.empty() || Destination.Name.empty() ||
      Destination.Name.find('\0') != std::string::npos ||
      Destination.SizeBudget == 0)
    return createStringError(
        errc::invalid_argument,
        "native object image requires bytes and a valid destination");
  const auto *Format = Registry->find(FormatID);
  if (!Format)
    return createStringError(errc::not_supported,
                             "native object image has an unknown format");

  auto OutputAPI = getOutputAPI(Task);
  if (!OutputAPI)
    return OutputAPI.takeError();
  NevercOutputSinkHandle Sink{};
  NevercStringView Name{Destination.Name.data(), Destination.Name.size()};
  NevercStatus Status{};
  switch (Destination.Kind) {
  case ObjectOutputDestinationKind::Memory:
    Status = (*OutputAPI)
                 ->BeginMemoryOutput((*OutputAPI)->Context, Task.handle(), Name,
                                     Destination.SizeBudget, &Sink);
    break;
  case ObjectOutputDestinationKind::File:
    Status = (*OutputAPI)
                 ->BeginFileOutput((*OutputAPI)->Context, Task.handle(), Name,
                                   Destination.SizeBudget, &Sink);
    break;
  }
  if (!neverc_status_is_ok(Status))
    return statusError("native object output begin", Status, *Format);
  auto AbortOutput = make_scope_exit([&] {
    (void)(*OutputAPI)->OutputAbort((*OutputAPI)->Context, Task.handle(), Sink);
  });
  auto Binary = MutableBinaryBuilder::create(Task, **OutputAPI, Sink);
  if (!Binary)
    return Binary.takeError();
  Status =
      (*Binary)->api().Write((*Binary)->api().Context, Task.handle(),
                             (*Binary)->handle(), {Bytes.data(), Bytes.size()});
  if (!neverc_status_is_ok(Status))
    return statusError("native object staging", Status, *Format);

  NevercObjectLayoutProofInfo Report{};
  Report.Header = {sizeof(Report), NEVERC_OBJECT_API_MAJOR,
                   NEVERC_OBJECT_API_MINOR, 0};
  Report.GraphGeneration = GraphGeneration;
  Report.TargetID = TargetID;
  Report.FormatID = FormatID;
  const std::string Provenance =
      "native:" + Format->PluginID + ":" + Format->CanonicalName;
  auto Image = PluginObjectImage::createPending(
      Task, FormatID, TargetID, GraphGeneration, std::move(*Binary), Provenance,
      Report);
  if (!Image)
    return Image.takeError();
  AbortOutput.release();
  return Image;
}

Expected<std::unique_ptr<PluginObjectImage>>
ObjectWriterProvider::write(PluginTaskContext &Task, PluginObjectGraph &Graph,
                            const ObjectOutputDestination &Destination) const {
  auto Image = beginWrite(Task, Graph, Destination);
  if (!Image)
    return Image.takeError();
  if (Error E = (*Image)->finish())
    return std::move(E);
  return Image;
}

bool ObjectWriterProvider::hasPluginOwnedGraphWriter(
    NevercObjectFormatID FormatID) const {
  const PluginTargetSnapshot::ObjectFormatRecord *Format =
      Registry->find(FormatID);
  return Format && Format->Owner && Format->Writer;
}

} // namespace neverc::plugin
