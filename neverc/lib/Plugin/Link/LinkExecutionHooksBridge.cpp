#include "neverc/Plugin/Host/LinkExecutionHooksBridge.h"
#include "AndroidKernelModuleFinalizer.h"
#include "AndroidKernelProfileContractVerifier.h"
#include "AndroidKernelReleaseIdentitySeal.h"
#include "AndroidKernelReleaseInputVerifier.h"
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
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Triple.h"
#include <limits>
#include <memory>
#include <optional>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

bool nonzero(linker::LinkExecutionID ID) { return ID.High != 0 || ID.Low != 0; }

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

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
  if (Config.finalizeAndroidKernelModule && !Config.androidKernelModule)
    return bridgeError(
        "Android module finalization requires Android module merge semantics");
  if (Config.finalizeAndroidKernelModule &&
      (!Config.relocatable ||
       Request.OutputKind != linker::LinkExecutionOutputKind::Relocatable))
    return bridgeError(
        "finalized Android module release requires a relocatable output "
        "request and driver configuration");
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

  // The release gate and the eventual ObjectPhasePipeline must reason about
  // one format identity. The pipeline dispatches from the authoritative
  // ObjectGraph target, so accepting a different request output format here
  // would let a provider hide from the pre-write gate and match during write.
  // Keep this restriction release-specific: non-release plugin routes may
  // intentionally model format conversion.
  if (Config.finalizeAndroidKernelModule &&
      (!sameID(FrozenInputFormat, FrozenTarget.ObjectFormatID) ||
       !sameID(FrozenOutputFormat, FrozenTarget.ObjectFormatID)))
    return bridgeError(
        "finalized Android relocatable release requires frozen input, target, "
        "and output object formats to share one identity");

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
        findPluginTargetSnapshot(Session->processServices(), Session->handle());
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

    std::vector<PluginObjectGraph *> Objects = (*Inputs)->objectGraphs();
    std::vector<ArrayRef<uint8_t>> InputImages =
        (*Inputs)->objectGraphSourceBytes();

    // Backing storage for LTO-lowered objects; must outlive the merge below.
    std::vector<SmallString<0>> LTOObjectImages;
    std::vector<std::unique_ptr<PluginObjectGraph>> LTOObjectGraphs;

    // LTO/bitcode inputs (e.g. `-fandroid-kernel-driver-mode`, which implies
    // `-flto=full`) arrive as bitcode modules, not native ObjectGraphs.  The
    // native `-r` path lowers them to native objects (`compileBitcodeFiles`)
    // before the byte merge; the plugin builtin merge only understands native
    // objects.
    if (!(*Inputs)->graph().bitcodeModules().empty()) {
      // Does a plugin actually need to process this relocatable output?  Only
      // then must the bitcode be lowered here.  Otherwise defer the whole link
      // to the native driver, which performs the identical LTO + merge (still
      // running the plugin's LTO codegen hooks via the shared LTO config),
      // keeping the output byte-identical to a native `-r` link.
      auto GatePipeline =
          ObjectPhasePipeline::create(*Config.pluginTask, TargetSnapshot);
      if (!GatePipeline)
        return GatePipeline.takeError();
      const bool PluginMustHandleOutput =
          !Plan->objectMergeProvider()->Builtin ||
          (*GatePipeline)->hasPluginBindings();
      if (!PluginMustHandleOutput)
        return linker::LinkHookResult{
            linker::LinkHookDisposition::ContinueBuiltin, 0};
      if (!Config.compileRelocatableLTO) {
        // No relocatable LTO backend wired (e.g. a unit-test harness): defer to
        // the native driver when possible.
        if (Plan->objectMergeProvider()->Builtin)
          return linker::LinkHookResult{
              linker::LinkHookDisposition::ContinueBuiltin, 0};
        return bridgeError(
            "plugin ObjectMergeProvider cannot merge LTO/bitcode inputs "
            "without a relocatable LTO backend");
      }

      // Lower every bitcode module to native relocatable objects.
      std::vector<MemoryBufferRef> BitcodeBuffers;
      for (PluginLinkBitcodeModule &Module :
           (*Inputs)->graph().bitcodeModules()) {
        auto Buffer = (*Inputs)->bitcodeBufferForModule(Module.ID);
        if (!Buffer)
          return Buffer.takeError();
        BitcodeBuffers.push_back(*Buffer);
      }
      StringRef BackendTag;
      bool EmitAddrsig = true;
      switch (Triple(Request.TargetTriple).getObjectFormat()) {
      case Triple::ELF:
        BackendTag = "elf";
        break;
      case Triple::COFF:
        BackendTag = "coff";
        break;
      case Triple::MachO:
        BackendTag = "macho";
        EmitAddrsig = false;
        break;
      default:
        return bridgeError(
            "relocatable LTO is unsupported for this object format");
      }
      auto Compiled = Config.compileRelocatableLTO(Config, BitcodeBuffers,
                                                   BackendTag, EmitAddrsig);
      if (!Compiled)
        return Compiled.takeError();
      LTOObjectImages = std::move(*Compiled);

      // Parse the lowered objects into ObjectGraphs and append them after the
      // native object inputs, matching the native driver's object order.
      for (size_t Index = 0; Index != LTOObjectImages.size(); ++Index) {
        const ArrayRef<uint8_t> Bytes(
            reinterpret_cast<const uint8_t *>(LTOObjectImages[Index].data()),
            LTOObjectImages[Index].size());
        auto Graph =
            (*ObjectReader)
                ->read(*Config.pluginTask, Bytes,
                       ("<lto>/relocatable-" + Twine(Index)).str(),
                       (*FrozenRequest)->ownedTarget(), FrozenInputFormat);
        if (!Graph)
          return Graph.takeError();
        LTOObjectGraphs.push_back(std::move(*Graph));
      }
      for (size_t Index = 0; Index != LTOObjectGraphs.size(); ++Index) {
        Objects.push_back(LTOObjectGraphs[Index].get());
        InputImages.push_back(ArrayRef<uint8_t>(
            reinterpret_cast<const uint8_t *>(LTOObjectImages[Index].data()),
            LTOObjectImages[Index].size()));
      }
    }

    if (Objects.empty())
      return bridgeError(
          "ObjectMergeProvider received no materialized ObjectGraph inputs");

    const auto &Provider = *Plan->objectMergeProvider();
    AndroidKernelModuleFinalizationPolicy FinalizationPolicy;
    FinalizationPolicy.DropDebugInfo =
        Config.finalizeAndroidKernelModule && Config.stripsDebugInfo();
    FinalizationPolicy.StripUnneededSymbols =
        Config.finalizeAndroidKernelModule && Config.stripsSymbols();
    if (Provider.Builtin && FinalizationPolicy.StripUnneededSymbols)
      FinalizationPolicy.SymbolNameState =
          AndroidKernelSymbolNameState::CanonicalRelease;

    // ObjectGraph normalizes several ELF-only facts (reserved section indices,
    // relocation-section flags and ELF ABI headers). Audit every immutable
    // source image before choosing built-in versus third-party execution so no
    // provider can observe a release input that the host cannot later recover
    // from the graph. LTO-lowered objects are appended to both parallel arrays
    // above and are covered by the same exact-count/non-empty contract.
    std::optional<AndroidKernelReleaseInputContract> ReleaseInputContract;
    if (Config.finalizeAndroidKernelModule) {
      auto Contract = verifyAndroidKernelReleaseObjectMergeInputs(
          Objects, InputImages, FrozenTarget,
          "Android kernel ObjectMergeProvider boundary");
      if (!Contract)
        return Contract.takeError();
      ReleaseInputContract = *Contract;
      if (!Provider.Builtin && Contract->requiresNativeImagePassthrough())
        return bridgeError(
            "third-party ObjectMergeProvider cannot preserve native-only "
            "Android release ABI or anonymous symbol/section provenance");
    }

    std::optional<uint64_t> AndroidKernelContract;
    if (Config.androidKernelModule) {
      auto Contract = requireMatchingAndroidKernelProfileContracts(
          Objects, "Android kernel ObjectMergeProvider boundary");
      if (!Contract)
        return Contract.takeError();
      AndroidKernelContract = *Contract;
    }

    auto OutputPipeline =
        ObjectPhasePipeline::create(*Config.pluginTask, TargetSnapshot);
    if (!OutputPipeline)
      return OutputPipeline.takeError();
    if (Config.finalizeAndroidKernelModule &&
        (*OutputPipeline)
            ->mayReplaceWriteArtifact(FrozenTarget, FrozenOutputFormat))
      return bridgeError(
          "finalized Android release forbids replaceable object write phase "
          "providers or interceptors before the trusted image baseline");
    if (Config.finalizeAndroidKernelModule &&
        (*OutputPipeline)->hasPluginOwnedGraphWriter(FrozenOutputFormat))
      return bridgeError(
          "finalized Android release requires a host-owned graph writer "
          "before the trusted image baseline");
    if (ReleaseInputContract &&
        ReleaseInputContract->requiresNativeImagePassthrough() &&
        (*OutputPipeline)->mayReplaceArtifact(FrozenTarget, FrozenOutputFormat))
      return bridgeError(
          "Android release input requires native-image passthrough, which "
          "is incompatible with registered ObjectGraph/output phase "
          "bindings");

    BuiltinObjectMergeConfig MergeConfig;
    MergeConfig.AndroidKernelModule = Config.androidKernelModule;
    MergeConfig.FinalizeAndroidKernelModule =
        Config.finalizeAndroidKernelModule;
    MergeConfig.DropDebugInfo = FinalizationPolicy.DropDebugInfo;
    MergeConfig.StripUnneededSymbols = FinalizationPolicy.StripUnneededSymbols;
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

    // Only the direct built-in adapter may attest its trusted native merger
    // bytes. Keep the exact shared token it returned; this bridge consumes the
    // attestation but never manufactures one from an arbitrary MergedImage.
    std::shared_ptr<const AndroidKernelReleaseBoundOutputContract>
        BoundReleaseOutput;
    if (Provider.Builtin && Config.finalizeAndroidKernelModule) {
      if (!ReleaseInputContract)
        return bridgeError("built-in Android final merge has no audited "
                           "release input contract");
      auto Consumed = consumeAndroidKernelReleaseBoundOutput(
          *Merged, *ReleaseInputContract,
          "Android kernel ObjectMergeProvider boundary");
      if (!Consumed)
        return Consumed.takeError();
      BoundReleaseOutput = std::move(*Consumed);
    }

    ObjectPhaseSemanticValidators Validators;
    // Prefer the merger's concrete image for a lossless write. Finalization may
    // still invalidate it when a typed provider leaves profile/debug/unneeded
    // data in the graph; the host then serializes the finalized graph instead
    // of allowing stale MergedImage bytes to bypass `--strip`.
    ArrayRef<uint8_t> OutputImage(
        reinterpret_cast<const uint8_t *>(Merged->MergedImage.data()),
        Merged->MergedImage.size());
    if (Config.finalizeAndroidKernelModule) {
      if (!ReleaseInputContract)
        return bridgeError("final Android kernel ObjectMergeProvider output "
                           "has no audited release input contract");
      const AndroidKernelReleaseInputContract &FinalReleaseContract =
          *ReleaseInputContract;
      const uint64_t GenerationBeforeStrip = Merged->Object->generation();
      if (Error Err = finalizeAndroidKernelModuleObjectGraph(
              *Merged->Object, FinalizationPolicy,
              "final Android kernel ObjectMergeProvider output"))
        return std::move(Err);
      if (Merged->Object->generation() != GenerationBeforeStrip) {
        OutputImage = {};
        BoundReleaseOutput.reset();
      }
      // A third-party merge provider cannot attest that its independently
      // supplied bytes are the serialization of the finalized graph. Make the
      // audited graph authoritative and let the host-owned writer establish
      // the only baseline that post-write identity validation may trust.
      if (!Provider.Builtin)
        OutputImage = {};
      if (FinalReleaseContract.requiresNativeImagePassthrough() &&
          OutputImage.empty())
        return bridgeError(
            "Android release finalization made a native-only input contract "
            "graph-authoritative");
      if (Error E = verifyFinalAndroidKernelModuleObjectGraph(
              *Merged->Object, FinalizationPolicy,
              "final Android kernel authoritative pre-hook graph"))
        return std::move(E);
      auto GraphIdentitySeal = captureAndroidKernelReleaseGraphIdentitySeal(
          *Merged->Object, FinalizationPolicy.SymbolNameState,
          "final Android kernel authoritative pre-hook graph");
      if (!GraphIdentitySeal)
        return GraphIdentitySeal.takeError();
      Validators.Graph = [Policy = FinalizationPolicy,
                          Seal = std::move(*GraphIdentitySeal)](
                             const PluginObjectGraph &Object) {
        if (Error E = verifyFinalAndroidKernelModuleObjectGraph(
                Object, Policy, "final Android kernel pre-write output"))
          return E;
        return verifyAndroidKernelReleaseGraphIdentitySeal(
            Object, Policy.SymbolNameState, Seal,
            "final Android kernel graph immutable identity contract");
      };
      Validators.BindPrePostWriteImage =
          [Policy = FinalizationPolicy](ArrayRef<uint8_t> Image)
          -> Expected<ObjectImageSemanticValidator> {
        if (Error E = verifyFinalAndroidKernelModuleImage(
                Image, Policy,
                "final Android kernel trusted pre-post-write image"))
          return std::move(E);
        auto Seal = captureAndroidKernelReleaseImageIdentitySeal(
            Image, "final Android kernel trusted pre-post-write image");
        if (!Seal)
          return Seal.takeError();
        return ObjectImageSemanticValidator(
            [Policy, Seal = std::move(*Seal)](
                ArrayRef<uint8_t> PostWriteImage) -> Error {
              if (Error E = verifyFinalAndroidKernelModuleImage(
                      PostWriteImage, Policy,
                      "final Android kernel post-write output"))
                return E;
              return verifyAndroidKernelReleaseImageIdentitySeal(
                  PostWriteImage, Seal,
                  "final Android kernel image immutable identity contract");
            });
      };
      Validators.Image = [Contract = FinalReleaseContract,
                          BoundContract = BoundReleaseOutput](
                             ArrayRef<uint8_t> Image) -> Error {
        // A bound image digest is authoritative only when native-only ELF facts
        // make the merger image itself the release contract.  Ordinary release
        // contracts deliberately allow authorized output phases to mutate bytes
        // outside the structurally verified ABI surface.
        if (BoundContract && BoundContract->requiresNativeImagePassthrough())
          return verifyAndroidKernelReleaseOutputContract(
              Image, *BoundContract,
              "final Android kernel bound native output contract");
        return verifyAndroidKernelReleaseOutputContract(
            Image, Contract, "final Android kernel release input contract");
      };
    } else if (AndroidKernelContract) {
      if (Error Err = requireAndroidKernelProfileContract(
              *Merged->Object, *AndroidKernelContract,
              "partial Android kernel ObjectMergeProvider output"))
        return std::move(Err);
      if (Provider.Builtin && !Merged->MergedImage.empty()) {
        if (Error Err = requireAndroidKernelProfileContract(
                ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(
                                      Merged->MergedImage.data()),
                                  Merged->MergedImage.size()),
                *AndroidKernelContract,
                "partial Android kernel ObjectMergeProvider output image"))
          return std::move(Err);
      }
      Validators.Graph =
          [Expected = *AndroidKernelContract](const PluginObjectGraph &Object) {
            return requireAndroidKernelProfileContract(
                Object, Expected, "partial Android kernel object phase graph");
          };
      Validators.Image = [Expected =
                              *AndroidKernelContract](ArrayRef<uint8_t> Image) {
        return requireAndroidKernelProfileContract(
            Image, Expected, "partial Android kernel object phase image");
      };
    }

    // Write the merged object through the native-image passthrough: the merged
    // graph was just parsed from these exact bytes, so unless a plugin output
    // phase mutates it (or finalization had to strip a divergent graph), the
    // file is written verbatim (byte-identical to the native `-r` link) instead
    // of via the lossy graph->assembly->object Writer.
    ObjectOutputDestination Destination = ObjectOutputDestination::file(
        (*FrozenRequest)->outputURI(), std::numeric_limits<uint64_t>::max());
    // A native merger image already owns final ELF semantics and bypasses the
    // graph Writer. If finalization or an output plugin makes the graph
    // authoritative instead, tag only that Writer request: canonical-only
    // output repairs table ownership, while release output also removes
    // Writer-synthesized symbols and replays names at the serialized boundary.
    if (Config.finalizeAndroidKernelModule &&
        Triple(Request.TargetTriple).isOSBinFormatELF()) {
      Destination.WritePolicy = FinalizationPolicy.StripUnneededSymbols
                                    ? ObjectWritePolicy::AndroidKernelRelease
                                    : ObjectWritePolicy::CanonicalELFTables;
      Destination.DropDebugInfo = FinalizationPolicy.DropDebugInfo;
    }
    auto Output = (*OutputPipeline)
                      ->executeNative(*Merged->Object, OutputImage, Destination,
                                      std::move(Validators));
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
