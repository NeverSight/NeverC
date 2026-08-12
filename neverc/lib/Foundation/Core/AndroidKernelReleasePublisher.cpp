//===- AndroidKernelReleasePublisher.cpp - Atomic release output ----------===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "neverc/Foundation/AndroidKernelReleasePublisher.h"

#include "neverc/Foundation/AndroidKernelReleasePaths.h"
#include "neverc/Foundation/Core/OutputDigest.h"
#include "neverc/Foundation/Core/OutputPublicationFlags.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA256.h"

#include <vector>

using namespace llvm;

namespace neverc {
namespace {

Error publisherError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(),
                           "Android kernel release symbol map: " + Message);
}

std::array<uint8_t, 32>
appendTextOutput(std::vector<OutputBundleFile> &Outputs, StringRef Name,
                 StringRef Path, StringRef Text) {
  OutputBundleFile Output;
  Output.Name = Name.str();
  Output.Path = Path.str();
  Output.Bytes.assign(Text.bytes_begin(), Text.bytes_end());
  Output.Bytes.push_back('\n');
  const std::array<uint8_t, 32> Digest = SHA256::hash(Output.Bytes);
  Outputs.push_back(std::move(Output));
  return Digest;
}

} // namespace

Expected<OutputBundleSummary> publishAndroidKernelReleaseOutput(
    OutputCoordinator &Coordinator, StringRef ImagePath,
    ArrayRef<uint8_t> Image, const AndroidKernelReleaseSymbolMap *Map,
    const AndroidKernelBuildState &BuildState, OutputLeaseOwner LeaseOwner,
    OutputBundleSummary *FinalSummary,
    OutputCoordinator::CancellationCheck IsCancelled) {
  if (FinalSummary) {
    *FinalSummary = {};
    FinalSummary->State = OutputBundleState::Aborted;
    FinalSummary->OutputCount = 2;
    FinalSummary->MainDigest = SHA256::hash(Image);
  }
  if (ImagePath.empty())
    return publisherError("image path is empty");
  if (ImagePath == "-")
    return publisherError(
        "stream output requires an explicit symbol-map path");
  if (Image.empty())
    return publisherError("final image is empty");

  std::vector<OutputBundleFile> Outputs;
  Outputs.push_back({"image", ImagePath.str(),
                     std::vector<uint8_t>(Image.begin(), Image.end()),
                     /*Main=*/true});
  const std::string MapPath =
      androidKernelReleaseSymbolMapPath(ImagePath);
  if (Map) {
    const std::array<uint8_t, 32> Digest = SHA256::hash(Image);
    if (Map->ImageSHA256 != Digest)
      return publisherError(
          "image digest does not match the release symbol map");
    auto Serialized = serializeAndroidKernelReleaseSymbolMap(*Map);
    if (!Serialized)
      return Serialized.takeError();
    OutputBundleFile MapOutput;
    MapOutput.Name = "symbol-map";
    MapOutput.Path = MapPath;
    MapOutput.Bytes.assign(Serialized->begin(), Serialized->end());
    MapOutput.OwnerOnly = true;
    Outputs.push_back(std::move(MapOutput));
  } else {
    Outputs.push_back({"symbol-map", MapPath, {},
                       /*Main=*/false, /*Executable=*/false,
                       OutputBundleFileAction::Remove});
  }

  if (BuildState.BuildExtra && !BuildState.BuildID)
    return publisherError("build-extra state requires a build identifier");
  if (BuildState.BuildID) {
    if (BuildState.BuildID->empty())
      return publisherError("build identifier must not be empty");
    const std::array<uint8_t, 32> BuildIDDigest = appendTextOutput(
        Outputs, "build-state",
        androidKernelAdjacentOutputPath(ImagePath,
                                        AndroidKernelBuildFlagsFilename),
        *BuildState.BuildID);
    const std::string BuildExtraPath = androidKernelAdjacentOutputPath(
        ImagePath, AndroidKernelBuildExtraFilename);
    std::string BuildExtraDigest = "-";
    if (!BuildState.BuildExtra || BuildState.BuildExtra->empty())
      Outputs.push_back({"build-extra", BuildExtraPath, {},
                         /*Main=*/false, /*Executable=*/false,
                         OutputBundleFileAction::Remove});
    else
      BuildExtraDigest = outputDigestText(appendTextOutput(
          Outputs, "build-extra", BuildExtraPath, *BuildState.BuildExtra));

    const std::string Integrity = formatAndroidKernelBuildIntegrity(
        outputDigestText(SHA256::hash(Image)),
        outputDigestText(BuildIDDigest), BuildExtraDigest);
    appendTextOutput(
        Outputs, "build-integrity",
        androidKernelAdjacentOutputPath(ImagePath,
                                        AndroidKernelBuildIntegrityFilename),
        Integrity);
  }
  if (FinalSummary)
    FinalSummary->OutputCount = Outputs.size();

  auto Transaction = OutputBundleTransaction::create(
      Coordinator, Outputs, std::move(IsCancelled), {}, LeaseOwner);
  if (!Transaction)
    return joinErrors(
        publisherError("cannot create image and symbol-map transaction"),
        Transaction.takeError());
  auto Published = (*Transaction)->commit();
  if (!Published) {
    Error Failure = joinErrors(
        publisherError("cannot publish image and symbol-map transaction"),
        Published.takeError());
    OutputBundleSummary Current = (*Transaction)->summary();
    if (Current.State == OutputBundleState::Open ||
        Current.State == OutputBundleState::Prepared) {
      Failure = joinErrors(std::move(Failure), (*Transaction)->abort());
      Current = (*Transaction)->summary();
    }
    if (FinalSummary)
      *FinalSummary = Current;
    return std::move(Failure);
  }
  if (FinalSummary)
    *FinalSummary = *Published;
  return std::move(*Published);
}

Expected<OutputBundleSummary> cleanAndroidKernelReleaseOutput(
    OutputCoordinator &Coordinator, StringRef ImagePath,
    OutputLeaseOwner LeaseOwner, OutputBundleSummary *FinalSummary,
    OutputCoordinator::CancellationCheck IsCancelled) {
  constexpr size_t OutputCount = 6;
  if (FinalSummary) {
    *FinalSummary = {};
    FinalSummary->State = OutputBundleState::Aborted;
    FinalSummary->OutputCount = OutputCount;
  }
  if (ImagePath.empty())
    return publisherError("image path is empty");
  if (ImagePath == "-")
    return publisherError("stream output cannot be cleaned transactionally");

  SmallString<256> Parent(sys::path::parent_path(ImagePath));
  if (Parent.empty())
    Parent = ".";
  sys::fs::file_status ParentStatus;
  const std::error_code ParentError =
      sys::fs::status(Parent, ParentStatus, /*follow=*/true);
  if (ParentError ==
          std::make_error_code(std::errc::no_such_file_or_directory) ||
      ParentError == std::make_error_code(std::errc::not_a_directory) ||
      (!ParentError && !sys::fs::is_directory(ParentStatus))) {
    OutputBundleSummary AlreadyClean;
    AlreadyClean.State = OutputBundleState::Committed;
    AlreadyClean.Flags = OutputDurable;
    AlreadyClean.OutputCount = OutputCount;
    if (FinalSummary)
      *FinalSummary = AlreadyClean;
    return AlreadyClean;
  }
  if (ParentError)
    return joinErrors(publisherError("cannot inspect output directory"),
                      errorCodeToError(ParentError));

  auto RemovedOutput = [](StringRef Name, StringRef Path, bool Main = false) {
    return OutputBundleFile{Name.str(), Path.str(), {}, Main,
                            /*Executable=*/false,
                            OutputBundleFileAction::Remove};
  };
  std::vector<OutputBundleFile> Outputs = {
      RemovedOutput("image", ImagePath, /*Main=*/true),
      RemovedOutput("symbol-map",
                    androidKernelReleaseSymbolMapPath(ImagePath)),
      RemovedOutput(
          "build-state",
          androidKernelAdjacentOutputPath(ImagePath,
                                          AndroidKernelBuildFlagsFilename)),
      RemovedOutput(
          "build-extra",
          androidKernelAdjacentOutputPath(ImagePath,
                                          AndroidKernelBuildExtraFilename)),
      RemovedOutput(
          "build-integrity",
          androidKernelAdjacentOutputPath(
              ImagePath, AndroidKernelBuildIntegrityFilename)),
      RemovedOutput(
          "legacy-release-state",
          androidKernelAdjacentOutputPath(ImagePath,
                                          AndroidKernelLegacyReleaseFilename)),
  };

  auto Transaction = OutputBundleTransaction::create(
      Coordinator, Outputs, std::move(IsCancelled), {}, LeaseOwner);
  if (!Transaction)
    return joinErrors(publisherError("cannot create output cleanup transaction"),
                      Transaction.takeError());
  auto Cleaned = (*Transaction)->commit();
  if (!Cleaned) {
    Error Failure = joinErrors(publisherError("cannot clean output bundle"),
                               Cleaned.takeError());
    OutputBundleSummary Current = (*Transaction)->summary();
    if (Current.State == OutputBundleState::Open ||
        Current.State == OutputBundleState::Prepared) {
      Failure = joinErrors(std::move(Failure), (*Transaction)->abort());
      Current = (*Transaction)->summary();
    }
    if (FinalSummary)
      *FinalSummary = Current;
    return std::move(Failure);
  }
  if (FinalSummary)
    *FinalSummary = *Cleaned;
  return std::move(*Cleaned);
}

} // namespace neverc
