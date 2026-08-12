#include "neverc/Foundation/AndroidKernelReleaseSymbolMap.h"

#include "neverc/Foundation/AndroidKernelReleasePaths.h"
#include "neverc/Foundation/Core/OutputDigest.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Base64.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
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
  return outputDigestText(SHA256::hash(ArrayRef<uint8_t>(
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
      fileDigestOr(androidKernelAdjacentOutputPath(
                       ImagePath, AndroidKernelBuildFlagsFilename),
                   "");
  if (!BuildIDDigest)
    return BuildIDDigest.takeError();
  auto BuildExtraDigest =
      fileDigestOr(androidKernelAdjacentOutputPath(
                       ImagePath, AndroidKernelBuildExtraFilename),
                   "-");
  if (!BuildExtraDigest)
    return BuildExtraDigest.takeError();
  return formatAndroidKernelBuildIntegrity(
      *ImageDigest, *BuildIDDigest, *BuildExtraDigest);
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

  const std::string ImageDigest = outputDigestText(Map.ImageSHA256);
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

} // namespace neverc
