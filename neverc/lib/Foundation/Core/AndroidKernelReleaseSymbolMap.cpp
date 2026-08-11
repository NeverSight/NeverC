#include "neverc/Foundation/AndroidKernelReleaseSymbolMap.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Base64.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
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

} // namespace

std::string androidKernelReleaseSymbolMapPath(StringRef ImagePath) {
  return ImagePath.str() + ".symbols.json";
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
    OutputLeaseOwner LeaseOwner, OutputBundleSummary *FinalSummary) {
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
    Outputs.push_back(
        {"symbol-map", MapPath,
         std::vector<uint8_t>(Serialized->begin(), Serialized->end())});
  } else {
    Outputs.push_back({"symbol-map", MapPath, {},
                       /*Main=*/false, /*Executable=*/false,
                       OutputBundleFileAction::Remove});
  }

  auto Transaction = OutputBundleTransaction::create(
      Coordinator, Outputs, {}, {}, LeaseOwner);
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

} // namespace neverc
