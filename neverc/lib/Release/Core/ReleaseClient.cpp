//===--- ReleaseClient.cpp - Verified GitHub release access -------------===//

#include "neverc/Release/ReleaseClient.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/SHA256.h"
#include "llvm/TargetParser/Triple.h"

#include <optional>
#include <string>
#include <system_error>

using namespace llvm;

namespace neverc {
namespace release {
namespace {

constexpr StringLiteral RepoSlug("NeverSight/NeverC");

bool isDigits(StringRef Text) {
  return !Text.empty() &&
         llvm::all_of(Text, [](char C) { return C >= '0' && C <= '9'; });
}

bool isValidConcreteTag(StringRef Tag) {
  if (!Tag.starts_with("v"))
    return false;
  size_t FirstDot = Tag.find('.', 1);
  if (FirstDot == StringRef::npos)
    return false;
  size_t SecondDot = Tag.find('.', FirstDot + 1);
  if (SecondDot == StringRef::npos ||
      Tag.find('.', SecondDot + 1) != StringRef::npos)
    return false;
  return isDigits(Tag.slice(1, FirstDot)) &&
         isDigits(Tag.slice(FirstDot + 1, SecondDot)) &&
         isDigits(Tag.drop_front(SecondDot + 1));
}

bool isSafeAssetName(StringRef AssetName) {
  return !AssetName.empty() && !AssetName.contains('/') &&
         !AssetName.contains('\\') && AssetName != "." && AssetName != "..";
}

Expected<std::string> findProgram(StringRef Name) {
  ErrorOr<SmallString<256>> Program = sys::findProgramByName(Name);
  if (!Program)
    return createStringError(Program.getError(),
                             "required program '%s' not found",
                             Name.str().c_str());
  return std::string(*Program);
}

Error execute(StringRef Program, ArrayRef<StringRef> Args) {
  SmallString<256> Message;
  bool ExecutionFailed = false;
  int RC = sys::ExecuteAndWait(Program, Args, /*Env=*/std::nullopt,
                               /*Redirects=*/{}, /*SecondsToWait=*/0,
                               /*MemoryLimit=*/0, &Message, &ExecutionFailed);
  if (RC == 0)
    return Error::success();
  if (!Message.empty())
    return createStringError(inconvertibleErrorCode(), "%s failed: %s",
                             sys::path::filename(Program).str().c_str(),
                             Message.c_str());
  if (ExecutionFailed)
    return createStringError(inconvertibleErrorCode(), "%s could not execute",
                             sys::path::filename(Program).str().c_str());
  return createStringError(inconvertibleErrorCode(), "%s exited with status %d",
                           sys::path::filename(Program).str().c_str(), RC);
}

Error downloadURL(StringRef URL, StringRef Destination,
                  bool ShowProgress = true) {
  Expected<std::string> CurlOrErr = findProgram("curl");
  if (!CurlOrErr)
    return CurlOrErr.takeError();
  std::string Curl = std::move(*CurlOrErr);

  SmallVector<StringRef, 12> Args;
  Args.push_back(Curl);
  Args.push_back("-fSL");
  if (ShowProgress)
    Args.push_back("--progress-bar");
  else
    Args.push_back("--silent");
  Args.push_back("-H");
  Args.push_back("User-Agent: neverc-release-client");
  Args.push_back("-o");
  Args.push_back(Destination);
  Args.push_back(URL);
  return execute(Curl, Args);
}

struct RemoveOnExit {
  SmallString<256> Path;
  explicit RemoveOnExit(SmallString<256> Path) : Path(std::move(Path)) {}
  ~RemoveOnExit() { (void)sys::fs::remove(Path); }
};

std::string quotePowerShellLiteral(StringRef Text) {
  std::string Result("'");
  for (char C : Text) {
    Result.push_back(C);
    if (C == '\'')
      Result.push_back('\'');
  }
  Result.push_back('\'');
  return Result;
}

} // namespace

std::string normalizeReleaseTag(StringRef Tag) {
  Tag = Tag.trim();
  if (Tag.equals_insensitive("latest"))
    return "latest";
  if (Tag.empty())
    return {};

  if (Tag.starts_with_insensitive("v"))
    Tag = Tag.drop_front();
  std::string Result = (Twine("v") + Tag).str();
  if (!isValidConcreteTag(Result))
    return {};
  return Result;
}

Expected<HostDistribution> getHostDistribution() {
  Triple Host(LLVM_HOST_TRIPLE);
  StringRef OS;
  if (Host.isOSLinux())
    OS = "linux";
  else if (Host.isMacOSX())
    OS = "macos";
  else if (Host.isOSWindows())
    OS = "windows";
  else
    return createStringError(inconvertibleErrorCode(),
                             "unsupported update host OS '%s'",
                             Host.getOSName().str().c_str());

  StringRef Arch;
  if (Host.getArch() == Triple::x86_64)
    Arch = "x64";
  else if (Host.getArch() == Triple::aarch64)
    Arch = "arm64";
  else
    return createStringError(inconvertibleErrorCode(),
                             "unsupported update host architecture '%s'",
                             Host.getArchName().str().c_str());

  std::string Platform = formatv("{0}-{1}", OS, Arch).str();
  if (Platform == "macos-x64")
    return createStringError(inconvertibleErrorCode(),
                             "NeverC does not publish a macos-x64 compiler");

  HostDistribution Result;
  Result.Platform = Platform;
  if (OS == "windows") {
    Result.CompilerAsset =
        formatv("windows-{0}-neverc-release.zip", Arch).str();
    Result.ArchiveRoot = "install";
    Result.ExecutableRelativePath = "bin/neverc.exe";
  } else {
    Result.CompilerAsset = formatv("neverc-{0}-{1}.zip", OS, Arch).str();
    Result.ExecutableRelativePath = "bin/neverc";
  }
  return Result;
}

Expected<std::string> queryLatestReleaseTagForAsset(StringRef AssetName) {
  if (!isSafeAssetName(AssetName))
    return createStringError(inconvertibleErrorCode(),
                             "invalid release asset name '%s'",
                             AssetName.str().c_str());

  SmallString<256> ResponsePath;
  if (std::error_code EC =
          sys::fs::createTemporaryFile("neverc-releases", "json", ResponsePath))
    return createStringError(EC, "cannot create release response file");
  RemoveOnExit ResponseGuard(ResponsePath);

  std::string URL =
      formatv("https://api.github.com/repos/{0}/releases?per_page=100",
              RepoSlug)
          .str();
  if (Error E = downloadURL(URL, ResponsePath, /*ShowProgress=*/false))
    return std::move(E);

  ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer =
      MemoryBuffer::getFile(ResponsePath, /*IsText=*/true);
  if (!Buffer)
    return createStringError(Buffer.getError(), "cannot read GitHub response");

  Expected<json::Value> Parsed = json::parse(Buffer.get()->getBuffer());
  if (!Parsed)
    return joinErrors(
        createStringError(inconvertibleErrorCode(),
                          "GitHub releases response is invalid JSON"),
        Parsed.takeError());
  const json::Array *Releases = Parsed->getAsArray();
  if (!Releases)
    return createStringError(inconvertibleErrorCode(),
                             "GitHub releases response is not an array");

  for (const json::Value &Value : *Releases) {
    const json::Object *Release = Value.getAsObject();
    if (!Release)
      continue;
    // This fork's JSON API returns -1 for a missing/wrong-typed boolean.
    // Only explicit false is a published stable release.
    if (Release->getBoolean("draft") != 0 ||
        Release->getBoolean("prerelease") != 0)
      continue;

    StringRef Tag = Release->getString("tag_name");
    const json::Array *Assets = Release->getArray("assets");
    if (!Tag.data() || !Assets)
      continue;
    std::string Normalized = normalizeReleaseTag(Tag);
    if (Normalized.empty() || Normalized == "latest")
      continue;

    bool ContainsAsset = llvm::any_of(*Assets, [&](const json::Value &Asset) {
      const json::Object *Object = Asset.getAsObject();
      return Object && Object->getString("name") == AssetName;
    });
    if (ContainsAsset)
      return Normalized;
  }

  return createStringError(inconvertibleErrorCode(),
                           "no published NeverC release contains asset '%s'",
                           AssetName.str().c_str());
}

Error downloadReleaseAsset(StringRef ReleaseTag, StringRef AssetName,
                           StringRef Destination) {
  std::string Tag = normalizeReleaseTag(ReleaseTag);
  if (Tag.empty() || Tag == "latest")
    return createStringError(inconvertibleErrorCode(),
                             "release asset downloads require a concrete tag");
  if (!isSafeAssetName(AssetName))
    return createStringError(inconvertibleErrorCode(),
                             "invalid release asset name '%s'",
                             AssetName.str().c_str());

  std::string URL = formatv("https://github.com/{0}/releases/download/{1}/{2}",
                            RepoSlug, Tag, AssetName)
                        .str();
  if (Error E = downloadURL(URL, Destination))
    return joinErrors(createStringError(inconvertibleErrorCode(),
                                        "failed to download %s from %s",
                                        AssetName.str().c_str(), Tag.c_str()),
                      std::move(E));
  return Error::success();
}

Expected<std::string> parseChecksumManifest(StringRef Contents,
                                            StringRef AssetName) {
  if (!isSafeAssetName(AssetName))
    return createStringError(inconvertibleErrorCode(),
                             "invalid checksum asset name '%s'",
                             AssetName.str().c_str());

  SmallVector<StringRef, 32> Lines;
  Contents.split(Lines, '\n');
  for (StringRef Line : Lines) {
    Line = Line.trim();
    if (Line.empty())
      continue;
    size_t Separator = Line.find_if([](char C) { return isSpace(C); });
    if (Separator == StringRef::npos)
      continue;
    StringRef Hash = Line.take_front(Separator);
    StringRef Name = Line.drop_front(Separator);
    Name = Name.trim();
    if (Name.starts_with("*"))
      Name = Name.drop_front();
    if (Name != AssetName)
      continue;

    if (Hash.size() != 64 || !llvm::all_of(Hash, llvm::isHexDigit))
      return createStringError(inconvertibleErrorCode(),
                               "invalid SHA256 for '%s' in checksum manifest",
                               AssetName.str().c_str());
    auto Lower = Hash.lower();
    return Lower.str().str();
  }
  return createStringError(inconvertibleErrorCode(),
                           "checksum manifest has no entry for '%s'",
                           AssetName.str().c_str());
}

Error verifyReleaseAsset(StringRef ArchivePath, StringRef ManifestPath,
                         StringRef AssetName) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> Manifest =
      MemoryBuffer::getFile(ManifestPath, /*IsText=*/true);
  if (!Manifest)
    return createStringError(Manifest.getError(),
                             "cannot read checksum manifest '%s'",
                             ManifestPath.str().c_str());
  Expected<std::string> ExpectedHash =
      parseChecksumManifest(Manifest.get()->getBuffer(), AssetName);
  if (!ExpectedHash)
    return ExpectedHash.takeError();

  ErrorOr<std::unique_ptr<MemoryBuffer>> Archive =
      MemoryBuffer::getFile(ArchivePath, /*IsText=*/false,
                            /*RequiresNullTerminator=*/false);
  if (!Archive)
    return createStringError(Archive.getError(), "cannot read archive '%s'",
                             ArchivePath.str().c_str());
  StringRef Bytes = Archive.get()->getBuffer();
  ArrayRef<uint8_t> Data(reinterpret_cast<const uint8_t *>(Bytes.data()),
                         Bytes.size());
  std::string ActualHash = toHex(SHA256::hash(Data), /*LowerCase=*/true);
  if (ActualHash != *ExpectedHash)
    return createStringError(inconvertibleErrorCode(),
                             "checksum verification failed for '%s'",
                             AssetName.str().c_str());
  return Error::success();
}

Error extractZip(StringRef ArchivePath, StringRef DestinationDirectory) {
  if (std::error_code EC = sys::fs::create_directories(DestinationDirectory))
    return createStringError(EC, "cannot create extraction directory '%s'",
                             DestinationDirectory.str().c_str());

#ifdef _WIN32
  if (Expected<std::string> Tar = findProgram("tar")) {
    std::string Program = std::move(*Tar);
    StringRef Args[] = {Program, "xf", ArchivePath, "-C", DestinationDirectory};
    return execute(Program, Args);
  } else {
    consumeError(Tar.takeError());
  }

  Expected<std::string> PowerShellOrErr = findProgram("powershell");
  if (!PowerShellOrErr)
    return PowerShellOrErr.takeError();
  std::string PowerShell = std::move(*PowerShellOrErr);
  std::string Command =
      formatv("Expand-Archive -LiteralPath {0} -DestinationPath {1} -Force",
              quotePowerShellLiteral(ArchivePath),
              quotePowerShellLiteral(DestinationDirectory))
          .str();
  StringRef Args[] = {PowerShell, "-NoProfile", "-NonInteractive", "-Command",
                      Command};
  return execute(PowerShell, Args);
#else
  Expected<std::string> UnzipOrErr = findProgram("unzip");
  if (!UnzipOrErr)
    return UnzipOrErr.takeError();
  std::string Unzip = std::move(*UnzipOrErr);
  StringRef Args[] = {Unzip,       "-q", "-o",
                      ArchivePath, "-d", DestinationDirectory};
  return execute(Unzip, Args);
#endif
}

Expected<std::string> resolveInstallRoot(const char *Argv0, void *MainAddress) {
  std::string Executable = sys::fs::getMainExecutable(Argv0, MainAddress);
  if (Executable.empty())
    return createStringError(inconvertibleErrorCode(),
                             "cannot determine NeverC executable path");

  SmallString<256> BinDirectory(Executable);
  sys::path::remove_filename(BinDirectory);
  if (sys::path::filename(BinDirectory) != "bin")
    return createStringError(inconvertibleErrorCode(),
                             "NeverC executable is not under a bin directory");

  SmallString<256> Root(BinDirectory);
  sys::path::remove_filename(Root);
  if (Root.empty() || sys::path::root_path(Root) == Root)
    return createStringError(
        inconvertibleErrorCode(),
        "refusing to use a filesystem root as an install prefix");
  return std::string(Root);
}

} // namespace release
} // namespace neverc
