#include "neverc/Plugin/Host/LinkExecutionHooksBridge.h"
#include "BuiltinObjectMergeAdapter.h"
#include "LinkInputReader.h"
#include "LinkRequest.h"
#include "ObjectMergeProvider.h"
#include "PluginLinkRegistry.h"
#include "neverc/Foundation/Core/OutputCoordinator.h"
#include "neverc/Linker/Core/Driver/Dispatcher.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/LinkPluginInterfaces.h"
#include "neverc/Plugin/Host/ObjectPhaseHooks.h"
#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <limits>

using namespace llvm;

namespace neverc::plugin {
namespace {

bool nonzero(linker::LinkExecutionID ID) { return ID.High != 0 || ID.Low != 0; }

NevercInterfaceID interfaceID(linker::LinkExecutionID ID) {
  return {ID.High, ID.Low};
}

NevercLinkOutputKind outputKind(linker::LinkExecutionOutputKind Kind) {
  return static_cast<NevercLinkOutputKind>(Kind);
}

Error bridgeError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

Expected<OwnedTargetKey>
resolveTargetKey(PluginSession &Session,
                 const linker::LinkExecutionRequest &Request,
                 const linker::LinkerDriverConfig &Config) {
  if (const BuiltinTargetRoute *Route =
          findBuiltinTargetRoute(Request.TargetTriple))
    return createBuiltinTargetKey(*Route, Request.TargetTriple, Config.cpu,
                                  Config.pie || Config.shared
                                      ? NEVERC_TARGET_RELOCATION_PIC
                                      : NEVERC_TARGET_RELOCATION_STATIC);

  PluginTargetRequest TargetRequest;
  TargetRequest.Triple = Request.TargetTriple;
  auto Snapshot =
      PluginTargetRegistry::freeze(Session.plugins(), TargetRequest);
  if (!Snapshot)
    return Snapshot.takeError();
  if (!(*Snapshot)->targetKey())
    return bridgeError("link request target has no frozen TargetKey");
  return *(*Snapshot)->targetKey();
}

LinkRequestOptions options(const linker::LinkerDriverConfig &Config) {
  LinkRequestOptions Result;
  if (Config.pie)
    Result.Flags |= NEVERC_LINK_OPTION_PIE;
  if (Config.staticLink)
    Result.Flags |= NEVERC_LINK_OPTION_STATIC;
  if (Config.gcSections)
    Result.Flags |= NEVERC_LINK_OPTION_GC_SECTIONS;
  if (Config.icfLevel != 0)
    Result.Flags |= NEVERC_LINK_OPTION_ICF;
  if (Config.exportDynamic)
    Result.Flags |= NEVERC_LINK_OPTION_EXPORT_DYNAMIC;
  Result.Flags |= NEVERC_LINK_OPTION_DETERMINISTIC;
  Result.ThreadBudget = Config.threadCount;
  return Result;
}

} // namespace

LinkExecutionHooksBridge::LinkExecutionHooksBridge(
    std::shared_ptr<PluginSession> SessionValue,
    OutputCoordinator &OutputsValue)
    : Session(std::move(SessionValue)), Outputs(OutputsValue) {}

LinkExecutionHooksBridge::~LinkExecutionHooksBridge() = default;

Expected<linker::LinkHookResult>
LinkExecutionHooksBridge::execute(const linker::LinkExecutionRequest &Request,
                                  const linker::LinkerDriverConfig &Config,
                                  raw_ostream &, raw_ostream &) {
  if (Active || Completed)
    return bridgeError("Link execution hooks cannot be re-entered");
  Active = true;
  (void)Outputs;

  std::shared_ptr<const PluginLinkSnapshot> Snapshot =
      findPluginLinkSnapshot(Session->processServices(), Session->handle());
  if (!Snapshot)
    return linker::LinkHookResult{linker::LinkHookDisposition::ContinueBuiltin,
                                  0};
  if (Request.OutputKind != linker::LinkExecutionOutputKind::Relocatable &&
      Snapshot->linkerProviders().empty() &&
      Snapshot->objectMergeProviders().empty())
    return linker::LinkHookResult{linker::LinkHookDisposition::ContinueBuiltin,
                                  0};

  auto Target = resolveTargetKey(*Session, Request, Config);
  if (!Target)
    return Target.takeError();
  const NevercTargetKey TargetView = Target->view();

  LinkRequestData Data;
  if (Config.pluginTask)
    Data.Task = Config.pluginTask->handle();
  Data.Target = std::move(*Target);
  Data.InputFormat = nonzero(Request.InputFormat)
                         ? interfaceID(Request.InputFormat)
                         : TargetView.ObjectFormatID;
  Data.OutputFormat = nonzero(Request.OutputFormat)
                          ? interfaceID(Request.OutputFormat)
                          : TargetView.ObjectFormatID;
  Data.OutputKind = outputKind(Request.OutputKind);
  Data.OutputURI = Request.OutputURI;
  Data.Options = options(Config);
  Data.Inputs.reserve(Request.Inputs.size());
  for (const linker::LinkExecutionInput &Input : Request.Inputs) {
    OwnedRawLinkInput Owned;
    Owned.Kind = static_cast<NevercLinkInputKind>(Input.Kind);
    Owned.Flags = Input.Flags;
    Owned.Ordinal = Input.Ordinal;
    Owned.LogicalURI = Input.LogicalURI;
    Owned.AuthorizedBlob = Input.AuthorizedBlob;
    Owned.Artifact = {Input.Artifact.High, Input.Artifact.Low};
    Data.Inputs.push_back(std::move(Owned));
  }
  auto FrozenRequest = LinkRequest::create(std::move(Data));
  if (!FrozenRequest)
    return FrozenRequest.takeError();
  const NevercTargetKey FrozenTarget = (*FrozenRequest)->target();
  const NevercObjectFormatID FrozenInputFormat =
      (*FrozenRequest)->inputFormat();
  const NevercObjectFormatID FrozenOutputFormat =
      (*FrozenRequest)->outputFormat();
  const NevercLinkOutputKind FrozenOutputKind = (*FrozenRequest)->outputKind();

  std::vector<PluginLinkSnapshot::LinkerProviderRecord> Linkers(
      Snapshot->linkerProviders().begin(), Snapshot->linkerProviders().end());
  std::vector<PluginLinkSnapshot::ObjectMergeProviderRecord> Mergers(
      Snapshot->objectMergeProviders().begin(),
      Snapshot->objectMergeProviders().end());

  if (Request.OutputKind == linker::LinkExecutionOutputKind::Relocatable) {
    PluginLinkSnapshot::ObjectMergeProviderRecord Builtin;
    Builtin.PluginID = "neverc.builtin";
    Builtin.ProviderID = "neverc.builtin.object-merge";
    Builtin.TargetID = FrozenTarget.TargetID;
    Builtin.FormatID = FrozenOutputFormat;
    Builtin.Builtin = true;
    Mergers.push_back(std::move(Builtin));
  } else {
    PluginLinkSnapshot::LinkerProviderRecord Builtin;
    Builtin.PluginID = "neverc.builtin";
    Builtin.ProviderID = "neverc.builtin.linker";
    Builtin.TargetID = FrozenTarget.TargetID;
    Builtin.InputFormat = FrozenInputFormat;
    Builtin.OutputFormat = FrozenOutputFormat;
    Builtin.OutputKind = FrozenOutputKind;
    Builtin.Builtin = true;
    Linkers.push_back(std::move(Builtin));
  }

  LinkRouteRequest RouteRequest;
  RouteRequest.TargetID = FrozenTarget.TargetID;
  RouteRequest.InputFormat = FrozenInputFormat;
  RouteRequest.OutputFormat = FrozenOutputFormat;
  RouteRequest.OutputKind = FrozenOutputKind;
  RouteRequest.CompatibilityKey = std::string(
      FrozenTarget.SchemaDigest.Data ? FrozenTarget.SchemaDigest.Data : "",
      static_cast<size_t>(FrozenTarget.SchemaDigest.Length));
  auto Plan = LinkRoutePlanner::plan(Linkers, Mergers, RouteRequest);
  if (!Plan)
    return Plan.takeError();

  if (Plan->kind() == PlannedLinkRoute::Kind::ObjectMerge) {
    if (!Config.pluginTask)
      return bridgeError("ObjectMergeProvider has no LinkTask");

    // Reuse the session's frozen target snapshot instead of re-freezing by
    // triple: the object-merge route also serves built-in targets, whose
    // triples are not registered as plugin targets (freezing by triple then
    // fails with "unknown target"). The snapshot's object-format registry
    // always carries the built-in ELF/COFF/Mach-O readers and writers, which
    // is all the merge path needs.
    std::shared_ptr<const PluginTargetSnapshot> TargetSnapshot =
        findPluginTargetSnapshot(Session->processServices(),
                                 Session->handle());
    if (!TargetSnapshot)
      return bridgeError("object-merge route has no frozen target snapshot");
    auto ObjectReader = ObjectReaderProvider::create(TargetSnapshot);
    if (!ObjectReader)
      return ObjectReader.takeError();
    auto FileSystem =
        createPluginFileSystem(*Config.pluginTask, vfs::getRealFileSystem());
    if (!FileSystem)
      return FileSystem.takeError();

    LinkInputReader InputReader(*Config.pluginTask, **FileSystem,
                                **ObjectReader);
    auto Inputs = InputReader.read(**FrozenRequest);
    if (!Inputs)
      return Inputs.takeError();

    // LTO/bitcode inputs (e.g. `-fandroid-kernel-driver-mode`, which implies
    // `-flto=full`) arrive as bitcode modules, not native ObjectGraphs.  The
    // built-in object merge only merges native objects; the native backend
    // must first compile the bitcode to native objects -- running the loaded
    // plugin's IR/MIR/optimization LTO hooks through the shared LTO config --
    // before performing the identical relocatable merge.  Defer such links to
    // the native driver instead of failing with "no materialized ObjectGraph
    // inputs".  A plugin-supplied merge provider cannot consume bitcode either,
    // so surface a precise error for that (currently unsupported) combination.
    if (!(*Inputs)->graph().bitcodeModules().empty()) {
      if (Plan->objectMergeProvider()->Builtin)
        return linker::LinkHookResult{
            linker::LinkHookDisposition::ContinueBuiltin, 0};
      return bridgeError(
          "plugin ObjectMergeProvider cannot merge LTO/bitcode inputs; "
          "relocatable LTO links require native bitcode compilation first");
    }

    std::vector<PluginObjectGraph *> Objects = (*Inputs)->objectGraphs();
    if (Objects.empty())
      return bridgeError(
          "ObjectMergeProvider received no materialized ObjectGraph inputs");
    std::vector<ArrayRef<uint8_t>> InputImages =
        (*Inputs)->objectGraphSourceBytes();

    const auto &Provider = *Plan->objectMergeProvider();
    BuiltinObjectMergeConfig MergeConfig;
    MergeConfig.AndroidKernelModule = Config.androidKernelModule;
    Expected<ObjectMergeResult> Merged =
        Provider.Builtin
            ? executeBuiltinObjectMergeAdapter(
                  *Config.pluginTask, TargetSnapshot,
                  (*FrozenRequest)->ownedTarget(), Objects, InputImages,
                  (*FrozenRequest)->options().Flags, MergeConfig)
            : executeObjectMergeProvider(
                  *Config.pluginTask, Provider, (*FrozenRequest)->ownedTarget(),
                  Objects, (*FrozenRequest)->options().Flags);
    if (!Merged)
      return Merged.takeError();

    auto OutputPipeline =
        ObjectPhasePipeline::create(*Config.pluginTask, TargetSnapshot);
    if (!OutputPipeline)
      return OutputPipeline.takeError();
    // Write the merged object through the native-image passthrough: the merged
    // graph was just parsed from these exact bytes, so unless a plugin output
    // phase mutates it, the file is written verbatim (byte-identical to the
    // native `-r` link) instead of via the lossy graph->assembly->object Writer.
    const ArrayRef<uint8_t> MergedImage(
        reinterpret_cast<const uint8_t *>(Merged->MergedImage.data()),
        Merged->MergedImage.size());
    auto Output = (*OutputPipeline)
                      ->executeNative(*Merged->Object, MergedImage,
                                      ObjectOutputDestination::file(
                                          (*FrozenRequest)->outputURI(),
                                          std::numeric_limits<uint64_t>::max()));
    if (!Output)
      return Output.takeError();
    return linker::LinkHookResult{linker::LinkHookDisposition::Completed, 0};
  }

  if (Plan->linkerProvider()->Builtin)
    return linker::LinkHookResult{linker::LinkHookDisposition::ContinueBuiltin,
                                  0};

  if (!Config.pluginTask)
    return bridgeError("plugin LinkerProvider has no LinkTask");
  const auto &Provider = *Plan->linkerProvider();
  NevercLinkerProductCandidate Candidate{};
  Candidate.Header = {sizeof(Candidate), NEVERC_LINK_API_MAJOR,
                      NEVERC_LINK_API_MINOR, 0};
  auto Invoked = Config.pluginTask->invokeCallback(
      Provider.PluginID, "LinkerProvider", [&] {
        return Provider.Link(Provider.UserData, Config.pluginTask->handle(),
                             &(*FrozenRequest)->cView(),
                             &(*FrozenRequest)->cView().RawInputs, &Candidate);
      });
  if (!Invoked)
    return Invoked.takeError();
  if (Invoked->Code != NEVERC_STATUS_OK)
    return bridgeError("plugin LinkerProvider returned status " +
                       Twine(Invoked->Code));
  if (neverc_handle_is_null(Candidate.Image) ||
      Candidate.ProductID.High != Provider.ProductID.High ||
      Candidate.ProductID.Low != Provider.ProductID.Low)
    return bridgeError("plugin LinkerProvider returned an invalid product");

  auto Verified = Config.pluginTask->invokeCallback(
      Provider.PluginID, "BinaryImageVerifier", [&] {
        return Provider.VerifyImage(
            Provider.UserData, Config.pluginTask->handle(),
            &(*FrozenRequest)->cView(), Candidate.Image);
      });
  if (!Verified)
    return Verified.takeError();
  if (Verified->Code != NEVERC_STATUS_OK)
    return bridgeError("plugin BinaryImage verifier returned status " +
                       Twine(Verified->Code));

  return linker::LinkHookResult{linker::LinkHookDisposition::Completed, 0};
}

void LinkExecutionHooksBridge::complete(bool) noexcept {
  Completed = true;
  Active = false;
}

} // namespace neverc::plugin
