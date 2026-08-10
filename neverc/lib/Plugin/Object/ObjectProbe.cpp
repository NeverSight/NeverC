#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Support/Errc.h"
#include <algorithm>
#include <limits>
#include <string>

using namespace llvm;

namespace neverc::plugin {
namespace {

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool nonzero(NevercInterfaceID ID) {
  return ID.High != 0 || ID.Low != 0;
}

bool supportsTarget(
    const PluginTargetSnapshot::ObjectFormatRecord &Format,
    NevercTargetID Target) {
  return Format.SupportedTargets.empty() ||
         std::any_of(Format.SupportedTargets.begin(),
                     Format.SupportedTargets.end(),
                     [&](NevercTargetID Supported) {
                       return sameID(Supported, Target);
                     });
}

bool validArtifactKind(NevercObjectArtifactKind Kind) {
  return Kind >= NEVERC_OBJECT_ARTIFACT_RELOCATABLE &&
         Kind <= NEVERC_OBJECT_ARTIFACT_UNIVERSAL_BINARY;
}

Expected<std::optional<ObjectProbeMatch>>
knownContainerKind(ArrayRef<uint8_t> Input) {
  if (Input.size() > std::numeric_limits<size_t>::max())
    return createStringError(errc::file_too_large,
                             "object probe input is too large");
  StringRef Bytes(reinterpret_cast<const char *>(Input.data()),
                  Input.size());
  ObjectProbeMatch Match;
  Match.Confidence = NEVERC_OBJECT_PROBE_MAX_CONFIDENCE;
  switch (identify_magic(Bytes)) {
  case file_magic::archive:
  case file_magic::coff_import_library:
    Match.ArtifactKind = NEVERC_OBJECT_ARTIFACT_ARCHIVE;
    Match.ConsumedMinimum = std::min<uint64_t>(Input.size(), 8);
    return std::optional<ObjectProbeMatch>(Match);
  case file_magic::elf_executable:
  case file_magic::macho_executable:
  case file_magic::macho_fixed_virtual_memory_shared_lib:
  case file_magic::macho_core:
  case file_magic::macho_preload_executable:
  case file_magic::macho_dynamic_linker:
  case file_magic::macho_bundle:
  case file_magic::macho_dsym_companion:
  case file_magic::macho_kext_bundle:
  case file_magic::macho_file_set:
  case file_magic::pecoff_executable:
    Match.ArtifactKind = NEVERC_OBJECT_ARTIFACT_EXECUTABLE_IMAGE;
    Match.ConsumedMinimum = std::min<uint64_t>(Input.size(), 16);
    return std::optional<ObjectProbeMatch>(Match);
  case file_magic::elf_shared_object:
  case file_magic::macho_dynamically_linked_shared_lib:
  case file_magic::macho_dynamically_linked_shared_lib_stub:
    Match.ArtifactKind = NEVERC_OBJECT_ARTIFACT_SHARED_IMAGE;
    Match.ConsumedMinimum = std::min<uint64_t>(Input.size(), 16);
    return std::optional<ObjectProbeMatch>(Match);
  case file_magic::macho_universal_binary:
    Match.ArtifactKind = NEVERC_OBJECT_ARTIFACT_UNIVERSAL_BINARY;
    Match.ConsumedMinimum = std::min<uint64_t>(Input.size(), 8);
    return std::optional<ObjectProbeMatch>(Match);
  default:
    return std::optional<ObjectProbeMatch>();
  }
}

Error callbackFailure(
    const PluginTargetSnapshot::ObjectFormatRecord &Format,
    NevercStatus Status) {
  return createStringError(
      errc::invalid_argument,
      "object probe callback for plugin '" + Format.PluginID +
          "', format '" + Format.CanonicalName +
          "' failed with status " + std::to_string(Status.Code));
}

} // namespace

Expected<ObjectProbeMatch> ObjectFormatRegistry::probe(
    PluginTaskContext &Task, ArrayRef<uint8_t> Input,
    StringRef LogicalPath, NevercTargetKey Target,
    std::optional<NevercObjectFormatID> RequiredFormat) const {
  auto Container = knownContainerKind(Input);
  if (!Container)
    return Container.takeError();
  if (*Container)
    return **Container;

  const PluginTargetSnapshot::ObjectFormatRecord *Required = nullptr;
  if (RequiredFormat) {
    Required = find(*RequiredFormat);
    if (!Required)
      return createStringError(errc::invalid_argument,
                               "required object Format is not registered");
    if (!supportsTarget(*Required, Target.TargetID))
      return createStringError(
          errc::not_supported,
          "required object Format '" + Required->CanonicalName +
              "' does not support the selected Target");
    if (!Required->Probe)
      return createStringError(
          errc::not_supported,
          "required object Format '" + Required->CanonicalName +
              "' has no probe capability");
  }

  NevercBufferView Buffer{};
  Buffer.Header = {sizeof(Buffer), NEVERC_SOURCE_API_MAJOR,
                   NEVERC_SOURCE_API_MINOR, 0};
  Buffer.Data = Input.data();
  Buffer.Length = Input.size();
  Buffer.NullTerminated = NEVERC_FALSE;

  NevercObjectProbeRequest Request{};
  Request.Header = {sizeof(Request), NEVERC_OBJECT_FORMAT_API_MAJOR, 0, 0};
  Request.Task = Task.handle();
  Request.Input = Buffer;
  Request.LogicalPath = {LogicalPath.data(), LogicalPath.size()};
  Request.Target = Target;

  const PluginTargetSnapshot::ObjectFormatRecord *Best = nullptr;
  NevercObjectProbeResult BestResult{};
  bool Ambiguous = false;
  uint64_t RequiredBytes = 0;

  for (const auto &Format : Formats) {
    if (Required && &Format != Required)
      continue;
    if (!Format.Probe || !supportsTarget(Format, Target.TargetID))
      continue;

    Request.Header.Minor = Format.APIMinor;
    NevercObjectProbeResult Result{};
    Result.Header = {sizeof(Result), NEVERC_OBJECT_FORMAT_API_MAJOR,
                     Format.APIMinor, 0};
    bool CallbackThrew = false;
    auto InvokeProbe = [&]() -> NevercStatus {
#if defined(__cpp_exceptions)
      try {
        return Format.Probe(Format.CallbackUserData, &Request, &Result);
      } catch (...) {
        CallbackThrew = true;
        NevercStatus Failure = neverc_status_ok();
        Failure.Code = NEVERC_STATUS_PLUGIN_FAILURE;
        return Failure;
      }
#else
      return Format.Probe(Format.CallbackUserData, &Request, &Result);
#endif
    };
    Expected<NevercStatus> Invoked =
        Format.Owner
            ? Task.invokeCallback(Format.PluginID,
                                  "object-probe:" + Format.CanonicalName,
                                  InvokeProbe)
            : Expected<NevercStatus>(InvokeProbe());
    if (!Invoked)
      return Invoked.takeError();
    if (CallbackThrew)
      return createStringError(
          errc::invalid_argument,
          "object probe callback for plugin '" + Format.PluginID +
              "', format '" + Format.CanonicalName + "' threw an exception");
    NevercStatus Status = *Invoked;
    if (!neverc_status_is_ok(Status))
      return callbackFailure(Format, Status);
    if (Result.Header.StructSize < sizeof(Result) ||
        Result.Header.Major != NEVERC_OBJECT_FORMAT_API_MAJOR ||
        Result.Header.Minor > Format.APIMinor || Result.Header.Flags != 0 ||
        Result.Confidence > NEVERC_OBJECT_PROBE_MAX_CONFIDENCE ||
        Result.ConsumedMinimum > NEVERC_OBJECT_PROBE_MAX_CONSUMED_MINIMUM)
      return createStringError(
          errc::invalid_argument,
          "object probe callback for plugin '" + Format.PluginID +
              "', format '" + Format.CanonicalName +
              "' returned an invalid result");

    if (Result.Confidence == 0) {
      RequiredBytes = std::max(RequiredBytes, Result.ConsumedMinimum);
      continue;
    }
    if (!validArtifactKind(Result.ArtifactKind) ||
        Result.ConsumedMinimum > Input.size())
      return createStringError(
          errc::invalid_argument,
          "object probe callback for plugin '" + Format.PluginID +
              "', format '" + Format.CanonicalName +
              "' claimed a malformed result");

    if (!Best || Result.Confidence > BestResult.Confidence) {
      Best = &Format;
      BestResult = Result;
      Ambiguous = false;
    } else if (Result.Confidence == BestResult.Confidence &&
               !sameID(Format.ID, Best->ID)) {
      Ambiguous = true;
    }
  }

  if (Ambiguous)
    return createStringError(
        errc::invalid_argument,
        "ambiguous object format probe: multiple formats returned confidence " +
            std::to_string(BestResult.Confidence));
  if (!Best) {
    if (RequiredBytes > Input.size())
      return createStringError(
          errc::invalid_argument,
          "truncated object input: format probe requires at least " +
              std::to_string(RequiredBytes) + " bytes");
    return createStringError(errc::invalid_argument,
                             "unrecognized object format");
  }

  ObjectProbeMatch Match;
  Match.Format = Best;
  Match.Confidence = BestResult.Confidence;
  Match.ArtifactKind = BestResult.ArtifactKind;
  Match.ConsumedMinimum = BestResult.ConsumedMinimum;
  return Match;
}

} // namespace neverc::plugin
