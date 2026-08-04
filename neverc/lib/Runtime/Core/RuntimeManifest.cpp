//===--- RuntimeManifest.cpp - Installed runtime version metadata ---------===//
//
// Tracks which GitHub release tag each installed cross-compilation runtime
// came from so `neverc runtime install` can stay aligned with the compiler.
//
//===----------------------------------------------------------------------===//

#include "neverc/Runtime/RuntimeManifest.h"
#include "neverc/Foundation/Core/Version.h"
#include "neverc/Release/ReleaseClient.h"

#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <optional>
#include <string>
#include <system_error>

using namespace llvm;

namespace neverc {
namespace runtime {
namespace {

constexpr StringLiteral ManifestFileName("manifest.json");
constexpr StringLiteral ManifestSchemaKey("schema");
constexpr StringLiteral ManifestTargetsKey("targets");
constexpr StringLiteral ManifestReleaseTagKey("release_tag");
constexpr int ManifestSchemaVersion = 1;

SmallString<256> manifestPath(StringRef RuntimeDir) {
  SmallString<256> P(RuntimeDir);
  sys::path::append(P, ManifestFileName);
  return P;
}

Expected<json::Object> readManifestObject(StringRef RuntimeDir) {
  SmallString<256> Path = manifestPath(RuntimeDir);
  ErrorOr<std::unique_ptr<MemoryBuffer>> Buf =
      MemoryBuffer::getFile(Path, /*IsText=*/true);
  if (!Buf) {
    if (Buf.getError() == std::errc::no_such_file_or_directory)
      return json::Object{};
    return createStringError(Buf.getError(),
                             "cannot read runtime manifest '%s'", Path.c_str());
  }

  Expected<json::Value> Parsed = json::parse(Buf.get()->getBuffer());
  if (!Parsed)
    return Parsed.takeError();

  if (json::Object *Obj = Parsed->getAsObject())
    return std::move(*Obj);
  return createStringError(inconvertibleErrorCode(),
                           "runtime manifest '%s' is not a JSON object",
                           Path.c_str());
}

Error writeManifestObject(StringRef RuntimeDir, const json::Object &Obj) {
  SmallString<256> Path = manifestPath(RuntimeDir);
  if (std::error_code EC = sys::fs::create_directories(RuntimeDir))
    return errorCodeToError(EC);

  json::Object Copy(Obj);
  std::string Text;
  raw_string_ostream OS(Text);
  OS << json::Value(std::move(Copy));

  std::error_code EC;
  raw_fd_ostream Out(Path, EC, sys::fs::OF_Text);
  if (EC)
    return errorCodeToError(EC);
  Out << OS.str();
  return Error::success();
}

std::optional<std::string> readTargetReleaseTag(const json::Object &Manifest,
                                                StringRef TargetName) {
  const json::Object *Targets = Manifest.getObject(ManifestTargetsKey);
  if (!Targets)
    return std::nullopt;

  const json::Object *Target = Targets->getObject(TargetName);
  if (!Target)
    return std::nullopt;

  if (std::optional<StringRef> Tag = Target->getString(ManifestReleaseTagKey))
    return Tag->str();
  return std::nullopt;
}

} // anonymous namespace

std::string getCompilerReleaseTag() {
  return formatv("v{0}.{1}.{2}", NEVERC_VERSION_MAJOR, NEVERC_VERSION_MINOR,
                 NEVERC_VERSION_PATCHLEVEL)
      .str();
}

std::string normalizeReleaseTag(StringRef Tag) {
  return release::normalizeReleaseTag(Tag);
}

std::string resolveFetchReleaseTag(StringRef UserVersion,
                                   bool DefaultToLatest) {
  if (!UserVersion.empty())
    return normalizeReleaseTag(UserVersion);
  if (DefaultToLatest)
    return "latest";
  return getCompilerReleaseTag();
}

std::optional<std::string> getInstalledReleaseTag(StringRef RuntimeDir,
                                                  StringRef TargetName) {
  Expected<json::Object> Manifest = readManifestObject(RuntimeDir);
  if (!Manifest) {
    consumeError(Manifest.takeError());
    return std::nullopt;
  }
  return readTargetReleaseTag(*Manifest, TargetName);
}

std::map<std::string, std::string>
readInstalledTargetVersions(StringRef RuntimeDir) {
  std::map<std::string, std::string> Result;
  Expected<json::Object> Manifest = readManifestObject(RuntimeDir);
  if (!Manifest) {
    consumeError(Manifest.takeError());
    return Result;
  }

  const json::Object *Targets = Manifest->getObject(ManifestTargetsKey);
  if (!Targets)
    return Result;

  for (const auto &Entry : *Targets) {
    const json::Object *Target = Entry.second.getAsObject();
    if (!Target)
      continue;
    if (std::optional<StringRef> Tag = Target->getString(ManifestReleaseTagKey))
      Result.emplace(Entry.first.str(), Tag->str());
  }
  return Result;
}

bool recordInstalledTarget(StringRef RuntimeDir, StringRef TargetName,
                           StringRef ReleaseTag) {
  std::string Tag = normalizeReleaseTag(ReleaseTag);
  if (Tag.empty() || Tag == "latest")
    return false;
  Expected<json::Object> ManifestOrErr = readManifestObject(RuntimeDir);
  json::Object Manifest;
  if (ManifestOrErr)
    Manifest = std::move(*ManifestOrErr);
  else
    consumeError(ManifestOrErr.takeError());

  Manifest[ManifestSchemaKey] = ManifestSchemaVersion;
  json::Object *Targets = Manifest.getObject(ManifestTargetsKey);
  if (!Targets) {
    json::Object NewTargets;
    Manifest[ManifestTargetsKey] = std::move(NewTargets);
    Targets = Manifest.getObject(ManifestTargetsKey);
  }

  json::Object TargetEntry;
  TargetEntry[ManifestReleaseTagKey] = Tag;
  (*Targets)[TargetName] = std::move(TargetEntry);

  if (Error E = writeManifestObject(RuntimeDir, Manifest)) {
    consumeError(std::move(E));
    return false;
  }
  return true;
}

bool removeInstalledTarget(StringRef RuntimeDir, StringRef TargetName) {
  Expected<json::Object> ManifestOrErr = readManifestObject(RuntimeDir);
  if (!ManifestOrErr) {
    consumeError(ManifestOrErr.takeError());
    return true;
  }

  json::Object Manifest = std::move(*ManifestOrErr);
  json::Object *Targets = Manifest.getObject(ManifestTargetsKey);
  if (!Targets)
    return true;

  Targets->erase(TargetName);
  if (Targets->empty())
    Manifest.erase(ManifestTargetsKey);

  if (Error E = writeManifestObject(RuntimeDir, Manifest)) {
    consumeError(std::move(E));
    return false;
  }
  return true;
}

} // namespace runtime
} // namespace neverc
