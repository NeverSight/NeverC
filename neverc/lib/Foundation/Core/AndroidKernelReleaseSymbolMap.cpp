#include "neverc/Foundation/AndroidKernelReleaseSymbolMap.h"
#include "neverc/Foundation/Core/OutputTransaction.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Base64.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"

#include <set>
#include <tuple>

using namespace llvm;

namespace neverc {
namespace {

Error mapError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(),
                           "Android kernel release symbol map: " + Message);
}

std::string adjacentOutputPath(StringRef ImagePath, StringRef Filename) {
  SmallString<256> Path(sys::path::parent_path(ImagePath));
  sys::path::append(Path, Filename);
  return Path.str().str();
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

std::string digestText(ArrayRef<uint8_t> Digest) {
  static constexpr char Hex[] = "0123456789abcdef";
  std::string Result;
  Result.reserve(Digest.size() * 2);
  for (uint8_t Byte : Digest) {
    Result.push_back(Hex[Byte >> 4]);
    Result.push_back(Hex[Byte & 0xf]);
  }
  return Result;
}

std::string formatBuildIntegrity(StringRef ImageDigest,
                                 StringRef BuildIDDigest,
                                 StringRef BuildExtraDigest) {
  return "IMAGE_SHA256=" + ImageDigest.str() +
         " BUILD_ID_SHA256=" + BuildIDDigest.str() +
         " BUILD_EXTRA_SHA256=" + BuildExtraDigest.str();
}

Expected<std::string> fileDigestOr(StringRef Path, StringRef MissingValue) {
  auto Buffer = MemoryBuffer::getFile(Path);
  if (!Buffer) {
    const std::error_code EC = Buffer.getError();
    if (EC == std::make_error_code(std::errc::no_such_file_or_directory) ||
        EC == std::make_error_code(std::errc::not_a_directory))
      return MissingValue.str();
    return errorCodeToError(EC);
  }
  const StringRef Bytes = (*Buffer)->getBuffer();
  return digestText(SHA256::hash(ArrayRef<uint8_t>(
      reinterpret_cast<const uint8_t *>(Bytes.data()), Bytes.size())));
}

} // namespace

std::string androidKernelReleaseSymbolMapPath(StringRef ImagePath) {
  return ImagePath.str() + ".symbols.json";
}

Expected<std::string>
currentAndroidKernelBuildIntegrity(StringRef ImagePath) {
  if (ImagePath.empty())
    return mapError("image path is empty");
  if (ImagePath == "-")
    return mapError("stream output has no adjacent build state");

  auto ImageDigest = fileDigestOr(ImagePath, "");
  if (!ImageDigest)
    return ImageDigest.takeError();
  auto BuildIDDigest =
      fileDigestOr(adjacentOutputPath(ImagePath, ".nvk-build-flags"), "");
  if (!BuildIDDigest)
    return BuildIDDigest.takeError();
  auto BuildExtraDigest =
      fileDigestOr(adjacentOutputPath(ImagePath, ".nvk-build-extra"), "-");
  if (!BuildExtraDigest)
    return BuildExtraDigest.takeError();
  return formatBuildIntegrity(*ImageDigest, *BuildIDDigest, *BuildExtraDigest);
}

Expected<std::string> serializeAndroidKernelReleaseSymbolMap(
    const AndroidKernelReleaseSymbolMap &Map) {
  SmallVector<const AndroidKernelReleaseSymbolMapEntry *, 64> Ordered;
  Ordered.reserve(Map.Symbols.size());
  std::set<std::string> ReleaseNames;
  for (const AndroidKernelReleaseSymbolMapEntry &Entry : Map.Symbols) {
    if (Entry.OriginalName.empty() || Entry.ReleaseName.empty())
      return mapError("entries require non-empty original and release names");
    if (!json::isUTF8(Entry.ReleaseName))
      return mapError("release names require valid UTF-8");
    if (Entry.OriginalName == Entry.ReleaseName)
      return mapError("unchanged symbols must not be serialized");
    if (!ReleaseNames.insert(Entry.ReleaseName).second)
      return mapError("release names must be unique");
    Ordered.push_back(&Entry);
  }
  llvm::sort(Ordered,
             [](const AndroidKernelReleaseSymbolMapEntry *LHS,
                const AndroidKernelReleaseSymbolMapEntry *RHS) {
               return std::tie(LHS->ReleaseName, LHS->OriginalName) <
                      std::tie(RHS->ReleaseName, RHS->OriginalName);
             });

  json::Array Symbols;
  Symbols.reserve(Ordered.size());
  for (const AndroidKernelReleaseSymbolMapEntry *Entry : Ordered) {
    json::Object Symbol{{"release", Entry->ReleaseName}};
    if (json::isUTF8(Entry->OriginalName)) {
      Symbol["original"] = Entry->OriginalName;
    } else {
      Symbol["original"] = encodeBase64(Entry->OriginalName);
      Symbol["original_encoding"] = "base64";
    }
    Symbols.push_back(std::move(Symbol));
  }

  const std::string ImageDigest = digestText(Map.ImageSHA256);
  json::Object Root{
      {"format", "neverc.android-kernel-symbol-map"},
      {"version", 2},
      {"image_sha256", ImageDigest},
      {"symbols", std::move(Symbols)},
  };
  std::string Result;
  raw_string_ostream OS(Result);
  OS << formatv("{0:2}", json::Value(std::move(Root))) << '\n';
  return Result;
}

Expected<OutputBundleSummary> publishAndroidKernelReleaseOutput(
    OutputCoordinator &Coordinator, StringRef ImagePath,
    ArrayRef<uint8_t> Image, const AndroidKernelReleaseSymbolMap *Map,
    OutputLeaseOwner LeaseOwner, OutputBundleSummary *FinalSummary,
    OutputCoordinator::CancellationCheck IsCancelled) {
  if (FinalSummary) {
    *FinalSummary = {};
    FinalSummary->State = OutputBundleState::Aborted;
    FinalSummary->OutputCount = 2;
    FinalSummary->MainDigest = SHA256::hash(Image);
  }
  if (ImagePath.empty())
    return mapError("image path is empty");
  if (ImagePath == "-")
    return mapError("stream output requires an explicit symbol-map path");
  if (Image.empty())
    return mapError("final image is empty");

  std::vector<OutputBundleFile> Outputs;
  Outputs.push_back({"image", ImagePath.str(),
                     std::vector<uint8_t>(Image.begin(), Image.end()),
                     /*Main=*/true});
  const std::string MapPath =
      androidKernelReleaseSymbolMapPath(ImagePath);
  if (Map) {
    const std::array<uint8_t, 32> Digest = SHA256::hash(Image);
    if (Map->ImageSHA256 != Digest)
      return mapError("image digest does not match the release symbol map");
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
  const auto BuildID =
      sys::Process::GetEnv("NEVERC_ANDROID_KERNEL_BUILD_ID");
  const auto BuildExtra =
      sys::Process::GetEnv("NEVERC_ANDROID_KERNEL_BUILD_EXTRA");
  if (BuildExtra && !BuildID)
    return mapError("build-extra state requires a build identifier");
  if (BuildID) {
    if (BuildID->empty())
      return mapError("build identifier must not be empty");
    const std::array<uint8_t, 32> BuildIDDigest = appendTextOutput(
        Outputs, "build-state",
        adjacentOutputPath(ImagePath, ".nvk-build-flags"), *BuildID);
    const std::string BuildExtraPath =
        adjacentOutputPath(ImagePath, ".nvk-build-extra");
    std::string BuildExtraDigest = "-";
    if (!BuildExtra || BuildExtra->empty())
      Outputs.push_back({"build-extra", BuildExtraPath, {},
                         /*Main=*/false, /*Executable=*/false,
                         OutputBundleFileAction::Remove});
    else {
      BuildExtraDigest =
          digestText(appendTextOutput(Outputs, "build-extra", BuildExtraPath,
                                      *BuildExtra));
    }
    const std::string Integrity = formatBuildIntegrity(
        digestText(SHA256::hash(Image)), digestText(BuildIDDigest),
        BuildExtraDigest);
    appendTextOutput(
        Outputs, "build-integrity",
        adjacentOutputPath(ImagePath, ".nvk-build-integrity"), Integrity);
  }
  if (FinalSummary)
    FinalSummary->OutputCount = Outputs.size();

  auto Transaction = OutputBundleTransaction::create(
      Coordinator, Outputs, std::move(IsCancelled), {}, LeaseOwner);
  if (!Transaction)
    return joinErrors(
        mapError("cannot create image and symbol-map transaction"),
        Transaction.takeError());
  auto Published = (*Transaction)->commit();
  if (!Published) {
    Error Failure = joinErrors(
        mapError("cannot publish image and symbol-map transaction"),
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
    return mapError("image path is empty");
  if (ImagePath == "-")
    return mapError("stream output cannot be cleaned transactionally");

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
    return joinErrors(mapError("cannot inspect output directory"),
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
      RemovedOutput("build-state",
                    adjacentOutputPath(ImagePath, ".nvk-build-flags")),
      RemovedOutput("build-extra",
                    adjacentOutputPath(ImagePath, ".nvk-build-extra")),
      RemovedOutput("build-integrity",
                    adjacentOutputPath(ImagePath, ".nvk-build-integrity")),
      RemovedOutput("legacy-release-state",
                    adjacentOutputPath(ImagePath, ".nvk-release-bundle")),
  };

  auto Transaction = OutputBundleTransaction::create(
      Coordinator, Outputs, std::move(IsCancelled), {}, LeaseOwner);
  if (!Transaction)
    return joinErrors(mapError("cannot create output cleanup transaction"),
                      Transaction.takeError());
  auto Cleaned = (*Transaction)->commit();
  if (!Cleaned) {
    Error Failure =
        joinErrors(mapError("cannot clean output bundle"), Cleaned.takeError());
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
