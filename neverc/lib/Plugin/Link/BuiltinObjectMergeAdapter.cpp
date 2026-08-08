#include "BuiltinObjectMergeAdapter.h"
#include "AndroidKernelProfileContractVerifier.h"
#include "neverc/Merge/Merger.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/ObjectPhaseHooks.h"
#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include <limits>
#include <optional>
#include <utility>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

constexpr NevercInterfaceID BuiltinObjectMergeProductID{
    UINT64_C(0x4e434f424a4d5247), UINT64_C(1)};

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

Expected<neverc::merge::Format>
getMergeFormat(NevercObjectFormatID FormatID) {
  std::optional<BuiltinObjectFormat> Selected;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    if (!sameID(Route.ObjectFormatID, FormatID))
      continue;
    if (Selected && *Selected != Route.ObjectFormat)
      return createStringError(
          errc::invalid_argument,
          "built-in object routes disagree on the merge format");
    Selected = Route.ObjectFormat;
  }
  if (!Selected)
    return createStringError(
        errc::not_supported,
        "the selected object format has no built-in relocatable merger");

  switch (*Selected) {
  case BuiltinObjectFormat::ELF:
    return neverc::merge::Format::ELF64LE;
  case BuiltinObjectFormat::COFF:
    return neverc::merge::Format::COFF;
  case BuiltinObjectFormat::MachO:
    return neverc::merge::Format::MachO64;
  }
  return createStringError(errc::not_supported,
                           "unsupported built-in object merge format");
}

Error verifyInputTargets(ArrayRef<PluginObjectGraph *> Objects,
                         NevercTargetKey Target) {
  if (Objects.empty())
    return createStringError(errc::invalid_argument,
                             "built-in object merge requires input objects");
  for (const PluginObjectGraph *Object : Objects) {
    if (!Object)
      return createStringError(errc::invalid_argument,
                               "built-in object merge input is null");
    const NevercTargetKey InputTarget = Object->targetKey();
    if (!sameID(InputTarget.TargetID, Target.TargetID) ||
        !sameID(InputTarget.ObjectFormatID, Target.ObjectFormatID))
      return createStringError(
          errc::invalid_argument,
          "built-in object merge input target or format does not match");
    if (Error E = verifyPluginObjectGraph(*Object))
      return joinErrors(
          createStringError(errc::invalid_argument,
                            "built-in object merge input graph is invalid"),
          std::move(E));
  }
  return Error::success();
}

} // namespace

Expected<ObjectMergeResult>
executeBuiltinObjectMergeAdapter(
    PluginTaskContext &Task,
    std::shared_ptr<const PluginTargetSnapshot> Snapshot,
    OwnedTargetKey Target, ArrayRef<PluginObjectGraph *> Objects,
    ArrayRef<ArrayRef<uint8_t>> InputImages,
    NevercLinkOptionFlags Flags, BuiltinObjectMergeConfig Config) {
  if (Config.FinalizeAndroidKernelModule && !Config.AndroidKernelModule)
    return createStringError(
        errc::invalid_argument,
        "Android module finalization requires Android module merge semantics");
  if ((Config.DropDebugInfo || Config.StripUnneededSymbols) &&
      !Config.FinalizeAndroidKernelModule)
    return createStringError(
        errc::invalid_argument,
        "Android module strip policy requires final module semantics");
  if (!Snapshot)
    return createStringError(
        errc::invalid_argument,
        "built-in object merge has no frozen Target snapshot");
  const NevercTargetKey TargetView = Target.view();
  if (Error E = verifyInputTargets(Objects, TargetView))
    return std::move(E);
  auto Format = getMergeFormat(TargetView.ObjectFormatID);
  if (!Format)
    return Format.takeError();

  auto SerializationToken = Task.handles().create(
      PluginObjectMergeSerializationHandleKind, &Task);
  if (!SerializationToken)
    return SerializationToken.takeError();
  auto ReleaseToken = make_scope_exit([&] {
    (void)Task.handles().release(
        *SerializationToken, PluginObjectMergeSerializationHandleKind);
  });
  const std::string Prefix =
      formatv("__neverc.object-merge.{0:x16}.{1:x16}",
              SerializationToken->Owner, SerializationToken->Value)
          .str();

  auto Pipeline = ObjectPhasePipeline::create(Task, Snapshot);
  if (!Pipeline)
    return Pipeline.takeError();

  std::vector<PluginMemoryOutputSnapshot> Serialized;
  Serialized.reserve(Objects.size());
  for (size_t Index = 0; Index != Objects.size(); ++Index) {
    const std::string Name =
        (Twine(Prefix) + ".input." + Twine(Index)).str();
    // Serialize the (possibly plugin-transformed) input graph back to bytes for
    // the byte merger.  When the caller supplied the original on-disk image and
    // no plugin phase mutated the graph, executeNative streams those exact bytes
    // through (beginImage), so the merge input matches the native link; only a
    // genuinely mutated graph falls back to the graph->assembly->object Writer.
    const ArrayRef<uint8_t> NativeBytes =
        Index < InputImages.size() ? InputImages[Index]
                                   : ArrayRef<uint8_t>{};
    auto Destination = ObjectOutputDestination::memory(
        Name, std::numeric_limits<uint64_t>::max());
    auto Image = NativeBytes.empty()
                     ? (*Pipeline)->execute(*Objects[Index], Destination)
                     : (*Pipeline)->executeNative(*Objects[Index],
                                                  NativeBytes, Destination);
    if (!Image)
      return joinErrors(
          createStringError(errc::invalid_argument,
                            "built-in object merge could not serialize "
                            "input ObjectGraph " +
                                Twine(Index)),
          Image.takeError());
    auto Bytes = findPluginMemoryOutput(Task, Name);
    if (!Bytes)
      return createStringError(
          errc::io_error,
          "built-in object merge Writer committed no memory image");
    Serialized.push_back(std::move(*Bytes));
  }

  SmallVector<StringRef, 8> InputBytes;
  InputBytes.reserve(Serialized.size());
  for (const PluginMemoryOutputSnapshot &Input : Serialized)
    InputBytes.emplace_back(
        reinterpret_cast<const char *>(Input.Bytes.data()),
        Input.Bytes.size());

  if (*Format == neverc::merge::Format::ELF64LE && Config.AndroidKernelModule) {
    auto Contract = requireMatchingAndroidKernelProfileContracts(
        InputBytes, "built-in Android kernel object merge");
    if (!Contract)
      return Contract.takeError();
  }

  SmallVector<char, 0> MergedBytes;
  raw_svector_ostream Output(MergedBytes);
  neverc::merge::Options MergeOptions;
  MergeOptions.pureC = true;
  MergeOptions.verify = true;
  // Mirror the native relocatable link's merge knobs so the plugin `-r` path is
  // byte-identical. Ordinary partial links keep strip disabled; the delivered
  // Android `.ko` is the sole ET_REL exception and uses the same relocation-
  // safe merger policy as the native backend.
  if (*Format == neverc::merge::Format::ELF64LE &&
      Config.AndroidKernelModule) {
    MergeOptions.androidKernelModule = true;
    MergeOptions.finalizeAndroidKernelModule =
        Config.FinalizeAndroidKernelModule;
    MergeOptions.dropDebugInfo = Config.DropDebugInfo;
    MergeOptions.stripUnneededSymbols = Config.StripUnneededSymbols;
    MergeOptions.mergeSections = true;
    MergeOptions.preservedSections = {
        ".modinfo",
        "__versions",
        ".codetag.alloc_tags",
        ".gnu.linkonce.this_module",
        ".plt",
        ".init.plt",
        ".text.ftrace_trampoline",
    };
  }
  (void)Flags;
  if (!neverc::merge::mergeObjects(
          InputBytes, Output, *Format, MergeOptions))
    return createStringError(
        errc::invalid_argument,
        "built-in relocatable object merge or self-verification failed");
  if (MergedBytes.empty())
    return createStringError(errc::invalid_argument,
                             "built-in object merge produced an empty image");
  auto Reader = ObjectReaderProvider::create(Snapshot);
  if (!Reader)
    return Reader.takeError();
  auto MergedGraph = (*Reader)->read(
      Task,
      ArrayRef<uint8_t>(
          reinterpret_cast<const uint8_t *>(MergedBytes.data()),
          MergedBytes.size()),
      Prefix + ".merged", Target, TargetView.ObjectFormatID);
  if (!MergedGraph)
    return joinErrors(
        createStringError(
            errc::invalid_argument,
            "built-in object merge output failed independent Reader import"),
        MergedGraph.takeError());
  if (Error E = verifyPluginObjectGraph(**MergedGraph))
    return joinErrors(
        createStringError(
            errc::invalid_argument,
            "built-in object merge output graph failed verification"),
        std::move(E));

  ObjectMergeResult Result;
  Result.Object = std::move(*MergedGraph);
  Result.ProductID = BuiltinObjectMergeProductID;
  Result.ProducerRouteDigest = SHA256::hash(ArrayRef<uint8_t>(
      reinterpret_cast<const uint8_t *>(MergedBytes.data()),
      MergedBytes.size()));
  Result.PluginID = "neverc.builtin";
  Result.ProviderID = "neverc.builtin.object-merge";
  Result.MergedImage = std::move(MergedBytes);
  return Result;
}

} // namespace neverc::plugin
