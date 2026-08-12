#include "AndroidKernelReleasePipeline.h"

#include "AndroidKernelReleaseBoundary.h"
#include "AndroidKernelReleaseIdentitySeal.h"
#include "neverc/Foundation/AndroidKernelBuildState.h"
#include "neverc/Foundation/AndroidKernelReleasePublisher.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"

#include <algorithm>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error pipelineError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

} // namespace

Expected<AndroidKernelReleasePipelineResult>
finalizeAndroidKernelReleasePipeline(
    ObjectMergeResult &Merged, bool ProviderBuiltin,
    const AndroidKernelReleaseInputContract &InputContract,
    AndroidKernelModuleFinalizationPolicy Policy) {
  if (!Merged.Object)
    return pipelineError("Android kernel release pipeline has no merged graph");

  AndroidKernelReleasePipelineResult Result;
  if (Merged.androidKernelReleaseSymbolMap()) {
    Result.SymbolMap = *Merged.androidKernelReleaseSymbolMap();
    Result.SymbolMapSource = AndroidKernelReleaseSymbolMapSource::NativeMerger;
  }

  if (ProviderBuiltin) {
    auto ConsumedBound = consumeAndroidKernelReleaseBoundOutput(
        Merged, InputContract,
        releaseBoundaryText(ReleaseBoundary::ObjectMergeProvider));
    if (!ConsumedBound)
      return ConsumedBound.takeError();
    Result.BoundNativeOutput = std::move(*ConsumedBound);
  }

  const uint64_t GenerationBeforeFinalization = Merged.Object->generation();
  AndroidKernelReleaseSymbolMap GraphReleaseSymbolMap;
  if (Error E = finalizeAndroidKernelModuleObjectGraph(
          *Merged.Object, Policy,
          releaseBoundaryText(ReleaseBoundary::FinalObjectMergeProviderOutput),
          Policy.StripUnneededSymbols ? &GraphReleaseSymbolMap : nullptr))
    return std::move(E);

  const bool GraphMutated =
      Merged.Object->generation() != GenerationBeforeFinalization;
  Result.Authority = ProviderBuiltin && !GraphMutated
                         ? AndroidKernelReleaseAuthority::NativeAttested
                         : AndroidKernelReleaseAuthority::GraphAuthoritative;

  if (!Result.usesNativeImage() &&
      InputContract.requiresNativeImagePassthrough())
    return pipelineError(
        "Android release finalization made a native-only input contract "
        "graph-authoritative");

  if (Result.usesNativeImage()) {
    if (Merged.MergedImage.empty())
      return pipelineError(
          "native-attested Android kernel release route has no native image");
    if (Policy.SymbolNameState !=
            AndroidKernelSymbolNameState::CanonicalRelease &&
        !GraphReleaseSymbolMap.Symbols.empty())
      return pipelineError(
          "native-attested Android kernel release route unexpectedly produced "
          "a graph symbol map");
  } else {
    Merged.MergedImage.clear();
    Result.BoundNativeOutput.reset();
    if (Policy.StripUnneededSymbols) {
      Result.SymbolMap = std::move(GraphReleaseSymbolMap);
      Result.SymbolMapSource =
          AndroidKernelReleaseSymbolMapSource::GraphFinalizer;
    } else {
      Result.SymbolMap.reset();
      Result.SymbolMapSource = AndroidKernelReleaseSymbolMapSource::None;
    }
  }

  if (Error E = verifyFinalAndroidKernelModuleObjectGraph(
          *Merged.Object, Policy,
          releaseBoundaryText(ReleaseBoundary::FinalAuthoritativePreHookGraph)))
    return std::move(E);

  auto GraphSeal = captureAndroidKernelReleaseGraphIdentitySeal(
      *Merged.Object, Policy.SymbolNameState,
      releaseBoundaryText(ReleaseBoundary::FinalAuthoritativePreHookGraph));
  if (!GraphSeal)
    return GraphSeal.takeError();

  Result.Validators.Graph = [Policy, Seal = std::move(*GraphSeal)](
                                const PluginObjectGraph &Graph) -> Error {
    if (Error E = verifyFinalAndroidKernelModuleObjectGraph(
            Graph, Policy,
            releaseBoundaryText(ReleaseBoundary::FinalPreWriteOutput)))
      return E;
    return verifyAndroidKernelReleaseGraphIdentitySeal(
        Graph, Policy.SymbolNameState, Seal,
        releaseBoundaryText(ReleaseBoundary::GraphImmutableIdentityContract));
  };
  Result.Validators.BindPrePostWriteImage =
      [Policy](
          ArrayRef<uint8_t> Image) -> Expected<ObjectImageSemanticValidator> {
    if (Error E = verifyFinalAndroidKernelModuleImage(
            Image, Policy,
            releaseBoundaryText(
                ReleaseBoundary::FinalTrustedPrePostWriteImage)))
      return std::move(E);
    auto Seal = captureAndroidKernelReleaseImageIdentitySeal(
        Image,
        releaseBoundaryText(ReleaseBoundary::FinalTrustedPrePostWriteImage));
    if (!Seal)
      return Seal.takeError();
    return ObjectImageSemanticValidator(
        [Policy,
         Seal = std::move(*Seal)](ArrayRef<uint8_t> PostWriteImage) -> Error {
          if (Error E = verifyFinalAndroidKernelModuleImage(
                  PostWriteImage, Policy,
                  releaseBoundaryText(ReleaseBoundary::FinalPostWriteOutput)))
            return E;
          return verifyAndroidKernelReleaseImageIdentitySeal(
              PostWriteImage, Seal,
              releaseBoundaryText(
                  ReleaseBoundary::ImageImmutableIdentityContract));
        });
  };
  Result.Validators.Image = [Contract = InputContract,
                             BoundOutput = Result.BoundNativeOutput](
                                ArrayRef<uint8_t> Image) -> Error {
    if (BoundOutput && BoundOutput->requiresNativeImagePassthrough())
      return verifyAndroidKernelReleaseOutputContract(
          Image, *BoundOutput,
          releaseBoundaryText(ReleaseBoundary::FinalBoundNativeOutputContract));
    return verifyAndroidKernelReleaseOutputContract(
        Image, Contract,
        releaseBoundaryText(ReleaseBoundary::FinalReleaseInputContract));
  };

  return Result;
}

ObjectPublicationHooks createAndroidKernelReleasePublicationHooks(
    OutputCoordinator &Coordinator, StringRef OutputPath,
    bool PublishesReleaseMap,
    std::optional<AndroidKernelReleaseSymbolMap> SymbolMap,
    OutputLeaseOwner LeaseOwner,
    OutputCoordinator::CancellationCheck IsCancelled) {
  ObjectPublicationHooks Hooks;
  Hooks.Commit =
      [&Coordinator, OutputPath = OutputPath.str(), PublishesReleaseMap,
       SymbolMap = std::move(SymbolMap), LeaseOwner = std::move(LeaseOwner),
       IsCancelled = std::move(IsCancelled)](
          ArrayRef<uint8_t> Image) mutable -> PluginObjectImageCommitResult {
    const auto makeSummary = [Image](const OutputBundleSummary &Bundle) {
      NevercOutputSummary Summary{};
      Summary.Header = {sizeof(Summary), NEVERC_IO_API_MAJOR,
                        NEVERC_IO_API_MINOR, 0};
      switch (Bundle.State) {
      case OutputBundleState::Committed:
        Summary.State = NEVERC_OUTPUT_COMMITTED;
        break;
      case OutputBundleState::Aborted:
        Summary.State = NEVERC_OUTPUT_ABORTED;
        break;
      case OutputBundleState::FailedPartial:
      case OutputBundleState::Open:
      case OutputBundleState::Prepared:
        Summary.State = NEVERC_OUTPUT_FAILED_PARTIAL;
        break;
      }
      Summary.Kind = NEVERC_OUTPUT_FILE;
      Summary.Flags = Bundle.Flags;
      Summary.Size = Image.size();
      Summary.PublicationGeneration = Bundle.PublicationGeneration;
      std::copy(Bundle.MainDigest.begin(), Bundle.MainDigest.end(),
                Summary.Digest);
      return Summary;
    };

    OutputBundleSummary BundleSummary;
    BundleSummary.State = OutputBundleState::Aborted;
    const AndroidKernelReleaseSymbolMap *PublishedMap = nullptr;
    if (PublishesReleaseMap) {
      if (!SymbolMap)
        SymbolMap.emplace();
      if (Error E = bindAndroidKernelReleaseSymbolMapToImage(
              *SymbolMap, Image,
              releaseBoundaryText(ReleaseBoundary::FinalReleaseSymbolMap)))
        return {makeSummary(BundleSummary), std::move(E)};
      PublishedMap = &*SymbolMap;
    }

    auto Published = publishAndroidKernelReleaseOutput(
        Coordinator, OutputPath, Image, PublishedMap,
        androidKernelBuildStateFromEnvironment(), std::move(LeaseOwner),
        &BundleSummary, std::move(IsCancelled));
    if (!Published)
      return {makeSummary(BundleSummary), Published.takeError()};
    return {makeSummary(*Published), Error::success()};
  };
  return Hooks;
}

} // namespace neverc::plugin
