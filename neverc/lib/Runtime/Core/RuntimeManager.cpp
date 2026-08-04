//===--- RuntimeManager.cpp - Cross-compilation runtime management --------===//
//
// Implements `neverc runtime {install|remove|list}` to manage per-target
// sysroot packages downloaded from GitHub Releases.
//
//===----------------------------------------------------------------------===//

#include "neverc/Runtime/RuntimeManager.h"
#include "neverc/Runtime/RuntimeManifest.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <optional>
#include <string>

using namespace llvm;

namespace neverc {
namespace runtime {

namespace {

constexpr StringLiteral RepoSlug("NeverSight/NeverC");

// ===----------------------------------------------------------------------===
// Target definitions
// ===----------------------------------------------------------------------===

struct TargetDef {
  StringLiteral Name;
  StringLiteral CheckDir; // subdirectory whose presence means "installed"
};

constexpr TargetDef Targets[] = {
    {{"windows-x64"}, {"windows/x64"}},
    {{"windows-arm64"}, {"windows/arm64"}},
    {{"linux-x64"}, {"linux/x64"}},
    {{"linux-arm64"}, {"linux/arm64"}},
    {{"macos-arm64"}, {"macos/arm64"}},
    {{"android-arm64"}, {"android/arm64"}},
    {{"android-kernel-arm64"}, {"android/kernel"}},
};

const TargetDef *lookupTarget(StringRef Name) {
  for (const auto &T : Targets)
    if (T.Name == Name)
      return &T;
  return nullptr;
}

bool isAllTarget(StringRef Name) { return Name.equals_insensitive("all"); }

void printTargetNames(raw_ostream &OS) {
  for (const auto &T : Targets)
    OS << " " << T.Name;
}

// ===----------------------------------------------------------------------===
// Path utilities
// ===----------------------------------------------------------------------===

/// Derive the NeverC root directory from the binary's on-disk location.
/// The binary lives at `<root>/bin/neverc`; this returns `<root>`.
std::string resolveRoot(const char *Argv0) {
  std::string Exe =
      sys::fs::getMainExecutable(Argv0, (void *)(intptr_t)&runRuntime);
  if (Exe.empty())
    return {};

  SmallString<256> Dir(Exe);
  sys::path::remove_filename(Dir); // strip "neverc"  → <root>/bin
  sys::path::remove_filename(Dir); // strip "bin"     → <root>
  return std::string(Dir);
}

SmallString<256> runtimeDir(StringRef Root) {
  SmallString<256> P(Root);
  sys::path::append(P, "runtime");
  return P;
}

bool isInstalled(StringRef RuntimeDir, StringRef CheckDir) {
  SmallString<256> P(RuntimeDir);
  sys::path::append(P, CheckDir);
  return sys::fs::is_directory(P);
}

// ===----------------------------------------------------------------------===
// External tool wrappers (curl / tar)
// ===----------------------------------------------------------------------===

/// Locate an executable on PATH. Returns an empty string on failure.
std::string findProgram(StringRef Name) {
  if (auto P = sys::findProgramByName(Name))
    return std::string(*P);
  return {};
}

/// RAII guard that removes a file when the scope exits.
struct TempFileGuard {
  SmallString<256> Path;
  bool Released = false;

  explicit TempFileGuard(SmallString<256> P) : Path(std::move(P)) {}
  ~TempFileGuard() {
    if (!Released) {
      std::error_code EC = sys::fs::remove(Path);
      (void)EC; // Best-effort cleanup; nothing to do on failure.
    }
  }
  void release() { Released = true; }
};

/// Run an external program with the given arguments. Returns the exit code.
int run(StringRef Program, ArrayRef<StringRef> Args) {
  SmallString<256> ErrMsg;
  int Rc =
      sys::ExecuteAndWait(Program, Args, /*Env=*/std::nullopt,
                          /*Redirects=*/{}, /*SecondsToWait=*/0,
                          /*MemoryLimit=*/0, &ErrMsg);
  if (Rc != 0 && !ErrMsg.empty())
    errs() << "error: " << sys::path::filename(Program) << ": " << ErrMsg
           << "\n";
  return Rc;
}

/// Download \p Url to \p DestPath via `curl`. Returns 0 on success.
int download(StringRef Url, StringRef DestPath) {
  std::string Curl = findProgram("curl");
  if (Curl.empty()) {
    errs() << "error: 'curl' not found — install curl and retry\n";
    return 1;
  }

  StringRef Args[] = {Curl, "-fSL", "--progress-bar", "-o", DestPath, Url};
  return run(Curl, Args);
}

/// Extract a .zip archive into \p DestDir. Returns 0 on success.
///
/// Strategy:
///   - Unix: `unzip -o <archive> -d <dest>`
///   - Windows: `tar.exe xf <archive> -C <dest>` (bsdtar, ships with
///     Windows 10 1803+). Falls back to PowerShell Expand-Archive.
int extractZip(StringRef Archive, StringRef DestDir) {
#ifdef _WIN32
  // Windows 10+ ships bsdtar which handles .zip natively.
  std::string Tar = findProgram("tar");
  if (!Tar.empty()) {
    StringRef Args[] = {Tar, "xf", Archive, "-C", DestDir};
    return run(Tar, Args);
  }
  // Fallback: PowerShell (always available on modern Windows).
  std::string PS = findProgram("powershell");
  if (PS.empty()) {
    errs() << "error: neither 'tar' nor 'powershell' found\n";
    return 1;
  }
  std::string Cmd =
      formatv("Expand-Archive -Path '{0}' -DestinationPath '{1}' -Force",
              Archive, DestDir)
          .str();
  StringRef Args[] = {PS, "-NoProfile", "-Command", Cmd};
  return run(PS, Args);
#else
  std::string Unzip = findProgram("unzip");
  if (Unzip.empty()) {
    errs() << "error: 'unzip' not found — install unzip and retry\n";
    return 1;
  }
  StringRef Args[] = {Unzip, "-o", Archive, "-d", DestDir};
  return run(Unzip, Args);
#endif
}

/// Resolve the concrete Git tag behind GitHub's "latest" release pointer.
std::optional<std::string> queryLatestReleaseTag() {
  std::string Curl = findProgram("curl");
  if (Curl.empty())
    return std::nullopt;

  SmallString<256> TmpPath;
  if (auto EC = sys::fs::createTemporaryFile("neverc-rt-tag", "json", TmpPath))
    return std::nullopt;
  TempFileGuard TmpGuard(TmpPath);

  std::string Url =
      formatv("https://api.github.com/repos/{0}/releases/latest", RepoSlug)
          .str();
  StringRef Args[] = {Curl,      "-fSL",
                      "-H",      "Accept: application/vnd.github+json",
                      "-H",      "User-Agent: neverc-runtime",
                      "-o",      TmpPath,
                      Url};
  if (run(Curl, Args) != 0)
    return std::nullopt;

  ErrorOr<std::unique_ptr<MemoryBuffer>> Buf = MemoryBuffer::getFile(TmpPath);
  if (!Buf || Buf.getError())
    return std::nullopt;

  Expected<json::Value> Parsed = json::parse(Buf.get()->getBuffer());
  if (!Parsed) {
    consumeError(Parsed.takeError());
    return std::nullopt;
  }

  if (const json::Object *Obj = Parsed->getAsObject())
    if (std::optional<StringRef> Tag = Obj->getString("tag_name")) {
      std::string Normalized = normalizeReleaseTag(*Tag);
      if (Normalized.empty() || Normalized == "latest")
        return std::nullopt;
      return Normalized;
    }
  return std::nullopt;
}

// ===----------------------------------------------------------------------===
// Interactive prompt
// ===----------------------------------------------------------------------===

/// Ask the user a yes/no question.  Returns true for "yes" (default).
bool promptYesNo(StringRef Question) {
  outs() << Question << " [Y/n] ";
  outs().flush();

  char Buf[16];
  if (!std::fgets(Buf, sizeof(Buf), stdin))
    return true; // EOF / pipe → accept default
  StringRef Answer(Buf);
  Answer = Answer.trim();
  if (Answer.empty())
    return true;
  return Answer.starts_with_insensitive("y");
}

// ===----------------------------------------------------------------------===
// Subcommands
// ===----------------------------------------------------------------------===

/// Validate the target name and return the TargetDef, printing an error
/// on failure.
const TargetDef *requireTarget(StringRef Name) {
  const TargetDef *T = lookupTarget(Name);
  if (!T) {
    errs() << "error: unknown target '" << Name << "'\n"
           << "available targets:";
    printTargetNames(errs());
    errs() << "\n";
  }
  return T;
}

/// Download and extract the runtime for \p T into \p RtDir.
/// If \p RemoveFirst is true, the existing directory is removed before
/// extraction (used for update / reinstall).
int fetchAndExtract(const TargetDef *T, StringRef RtDir, StringRef UserVersion,
                    bool DefaultToLatest, bool RemoveFirst,
                    std::string *OutReleaseTag = nullptr) {
  std::string ReleaseTag =
      resolveFetchReleaseTag(UserVersion, DefaultToLatest);
  std::string ManifestTag = ReleaseTag;

  if (ReleaseTag == "latest") {
    if (auto Resolved = queryLatestReleaseTag()) {
      ManifestTag = *Resolved;
      ReleaseTag = *Resolved;
    } else {
      errs() << "warning: could not resolve latest release tag; manifest "
                "will not be updated\n";
    }
  } else if (ReleaseTag.empty()) {
    errs() << "error: invalid release tag";
    if (!UserVersion.empty())
      errs() << " '" << UserVersion << "'";
    errs() << " — expected vMAJOR.MINOR.PATCH\n";
    return 1;
  }

  std::string Asset = formatv("neverc-runtime-{0}.zip", T->Name).str();

  std::string Url;
  if (ReleaseTag == "latest")
    Url = formatv("https://github.com/{0}/releases/latest/download/{1}",
                  RepoSlug, Asset)
              .str();
  else
    Url = formatv("https://github.com/{0}/releases/download/{1}/{2}",
                  RepoSlug, ReleaseTag, Asset)
              .str();

  outs() << "  Source: " << Url << "\n"
         << "  Dest:   " << RtDir << "/\n\n";

  // Create temp file for download.
  SmallString<256> TmpPath;
  if (auto EC = sys::fs::createTemporaryFile("neverc-rt", "zip", TmpPath)) {
    errs() << "error: cannot create temp file: " << EC.message() << "\n";
    return 1;
  }
  TempFileGuard TmpGuard(TmpPath);

  outs() << "Downloading " << Asset << "...\n";
  if (download(Url, TmpPath) != 0) {
    errs() << "error: download failed — check that release " << ReleaseTag
           << " has asset '" << Asset << "'\n";
    return 1;
  }

  // Remove old runtime before extraction if requested.
  if (RemoveFirst) {
    SmallString<256> OldDir(RtDir);
    sys::path::append(OldDir, T->CheckDir);
    if (sys::fs::is_directory(OldDir)) {
      outs() << "Removing old runtime...\n";
      if (auto EC = sys::fs::remove_directories(OldDir)) {
        errs() << "error: cannot remove old runtime: " << EC.message() << "\n";
        return 1;
      }
    }
  }

  if (auto EC = sys::fs::create_directories(RtDir)) {
    errs() << "error: cannot create " << RtDir << ": " << EC.message() << "\n";
    return 1;
  }

  outs() << "Extracting...\n";
  if (extractZip(TmpPath, RtDir) != 0) {
    errs() << "error: extraction failed\n";
    return 1;
  }

  if (ManifestTag != "latest" &&
      !recordInstalledTarget(RtDir, T->Name, ManifestTag)) {
    errs() << "warning: installed runtime but failed to update manifest\n";
  }

  if (OutReleaseTag) {
    if (ManifestTag != "latest")
      *OutReleaseTag = ManifestTag;
    else
      OutReleaseTag->clear();
  }
  return 0;
}

int doInstall(StringRef TargetName, StringRef Root, StringRef UserVersion) {
  const TargetDef *T = requireTarget(TargetName);
  if (!T)
    return 1;

  auto RtDir = runtimeDir(Root);
  std::string DesiredTag = resolveFetchReleaseTag(UserVersion, /*Latest=*/false);
  if (DesiredTag.empty()) {
    errs() << "error: invalid release tag";
    if (!UserVersion.empty())
      errs() << " '" << UserVersion << "'";
    errs() << " — expected vMAJOR.MINOR.PATCH\n";
    return 1;
  }

  if (isInstalled(RtDir, T->CheckDir)) {
    std::optional<std::string> InstalledTag =
        getInstalledReleaseTag(RtDir, T->Name);

    if (InstalledTag && *InstalledTag == DesiredTag) {
      outs() << "runtime '" << T->Name << "' is already installed at "
             << DesiredTag << ".\n";
      return 0;
    }

    outs() << "runtime '" << T->Name << "' is already installed";
    if (InstalledTag)
      outs() << " at " << *InstalledTag;
    else
      outs() << " (version unknown)";
    outs() << ".\n";

    if (!promptYesNo(formatv("Reinstall at {0}?", DesiredTag).str())) {
      outs() << "Skipped.\n";
      return 0;
    }

    outs() << "Reinstalling runtime: " << T->Name << " (" << DesiredTag
           << ")\n";
    std::string ActualTag;
    int Rc = fetchAndExtract(T, RtDir, UserVersion, /*DefaultToLatest=*/false,
                             /*RemoveFirst=*/true, &ActualTag);
    if (Rc == 0) {
      outs() << "\nDone! Runtime '" << T->Name << "' installed at "
             << (ActualTag.empty() ? DesiredTag : ActualTag) << ".\n";
    }
    return Rc;
  }

  outs() << "Installing runtime: " << T->Name << " (" << DesiredTag << ")\n";
  std::string ActualTag;
  int Rc = fetchAndExtract(T, RtDir, UserVersion, /*DefaultToLatest=*/false,
                           /*RemoveFirst=*/false, &ActualTag);
  if (Rc == 0)
    outs() << "\nDone! Runtime '" << T->Name << "' installed to " << RtDir
           << "/ (" << (ActualTag.empty() ? DesiredTag : ActualTag) << ")\n";
  return Rc;
}

int doInstallAll(StringRef Root, StringRef UserVersion) {
  auto RtDir = runtimeDir(Root);
  std::string DesiredTag = resolveFetchReleaseTag(UserVersion, /*Latest=*/false);
  if (DesiredTag.empty()) {
    errs() << "error: invalid release tag";
    if (!UserVersion.empty())
      errs() << " '" << UserVersion << "'";
    errs() << " — expected vMAJOR.MINOR.PATCH\n";
    return 1;
  }
  int Installed = 0;
  int Skipped = 0;
  int Failures = 0;

  outs() << "Installing cross-compilation runtimes into " << RtDir << "/ ("
         << DesiredTag << ")\n\n";

  for (const auto &T : Targets) {
    if (isInstalled(RtDir, T.CheckDir)) {
      std::optional<std::string> InstalledTag =
          getInstalledReleaseTag(RtDir, T.Name);
      if (InstalledTag && *InstalledTag == DesiredTag) {
        outs() << "Skipping " << T.Name << " (already installed at "
               << DesiredTag << ")\n";
      } else {
        outs() << "Skipping " << T.Name << " (already installed";
        if (InstalledTag)
          outs() << " at " << *InstalledTag;
        else
          outs() << ", version unknown";
        outs() << "; run 'neverc runtime install " << T.Name
               << "' to reinstall at " << DesiredTag << ")\n";
      }
      ++Skipped;
      continue;
    }

    outs() << "Installing runtime: " << T.Name << " (" << DesiredTag << ")\n";
    if (fetchAndExtract(&T, RtDir, UserVersion, /*DefaultToLatest=*/false,
                        /*RemoveFirst=*/false) != 0) {
      errs() << "error: failed to install runtime '" << T.Name << "'\n\n";
      ++Failures;
      continue;
    }

    outs() << "Done: " << T.Name << "\n\n";
    ++Installed;
  }

  outs() << "Summary: " << Installed << " installed, " << Skipped << " skipped";
  if (Failures)
    outs() << ", " << Failures << " failed";
  outs() << "\n";

  return Failures ? 1 : 0;
}

/// `neverc runtime update <target>` — force-update without prompting.
int doUpdate(StringRef TargetName, StringRef Root, StringRef UserVersion) {
  const TargetDef *T = requireTarget(TargetName);
  if (!T)
    return 1;

  auto RtDir = runtimeDir(Root);
  bool WasInstalled = isInstalled(RtDir, T->CheckDir);
  std::string ReleaseTag;
  std::string DesiredTag =
      resolveFetchReleaseTag(UserVersion, /*Latest=*/true);
  if (DesiredTag.empty()) {
    errs() << "error: invalid release tag";
    if (!UserVersion.empty())
      errs() << " '" << UserVersion << "'";
    errs() << " — expected vMAJOR.MINOR.PATCH or latest\n";
    return 1;
  }

  outs() << (WasInstalled ? "Updating" : "Installing") << " runtime: "
         << T->Name << " (" << DesiredTag << ")\n";
  int Rc = fetchAndExtract(T, RtDir, UserVersion, /*DefaultToLatest=*/true,
                           /*RemoveFirst=*/WasInstalled, &ReleaseTag);
  if (Rc == 0) {
    outs() << "\nDone! Runtime '" << T->Name << "' "
           << (WasInstalled ? "updated" : "installed");
    if (!ReleaseTag.empty())
      outs() << " at " << ReleaseTag;
    outs() << ".\n";
  }
  return Rc;
}

int doRemove(StringRef TargetName, StringRef Root) {
  const TargetDef *T = lookupTarget(TargetName);
  if (!T) {
    errs() << "error: unknown target '" << TargetName << "'\n";
    return 1;
  }

  auto RtDir = runtimeDir(Root);
  if (!isInstalled(RtDir, T->CheckDir)) {
    outs() << "runtime '" << T->Name << "' is not installed\n";
    return 0;
  }

  outs() << "Removing runtime: " << T->Name << "\n";

  SmallString<256> P(RtDir);
  sys::path::append(P, T->CheckDir);
  if (auto EC = sys::fs::remove_directories(P)) {
    errs() << "error: " << EC.message() << "\n";
    return 1;
  }

  if (!removeInstalledTarget(RtDir, T->Name))
    errs() << "warning: removed runtime but failed to update manifest\n";

  outs() << "  removed " << P << "\nDone.\n";
  return 0;
}

int doList(StringRef Root) {
  auto RtDir = runtimeDir(Root);
  std::map<std::string, std::string> Versions =
      readInstalledTargetVersions(RtDir);
  std::string CompilerTag = getCompilerReleaseTag();

  outs() << "NeverC runtimes  (" << RtDir << "/)\n"
         << "Compiler: " << CompilerTag << "\n\n";

  for (const auto &T : Targets) {
    bool Ok = isInstalled(RtDir, T.CheckDir);
    if (Ok) {
      auto It = Versions.find(std::string(T.Name));
      if (It != Versions.end())
        outs() << formatv("  {0,-20}  installed  {1}\n", T.Name, It->second);
      else
        outs() << formatv("  {0,-20}  installed  (version unknown)\n", T.Name);
    } else {
      outs() << formatv("  {0,-20}  not installed\n", T.Name);
    }
  }

  outs() << "\nInstall a runtime:  neverc runtime install <target>\n"
         << "Install all runtimes: neverc runtime install all\n"
         << "Update a runtime:     neverc runtime update <target>\n";
  return 0;
}

void printUsage() {
  outs() << "neverc runtime — manage cross-compilation runtimes\n\n"
         << "Usage:\n"
         << "  neverc runtime install <target>   Install at the compiler "
            "version\n"
         << "  neverc runtime install all        Install missing runtimes at "
            "the compiler version\n"
         << "  neverc runtime update  <target>   Update to the latest release "
            "(or --version)\n"
         << "  neverc runtime remove  <target>   Remove an installed runtime\n"
         << "  neverc runtime list               List available and installed "
            "runtimes\n\n"
         << "Available targets:\n"
         << " ";
  printTargetNames(outs());
  outs() << "\n\n"
         << "Options:\n"
         << "  --version <tag>     Use a specific release tag (e.g. "
            "v3389.1.2)\n"
         << "                      install defaults to the compiler version; "
            "update defaults to latest\n";
}

} // anonymous namespace

// ===----------------------------------------------------------------------===
// Entry point — dispatched from neverc main()
// ===----------------------------------------------------------------------===

int runRuntime(int Argc, const char **Argv, const char *Argv0) {
  std::string Root = resolveRoot(Argv0);
  if (Root.empty()) {
    errs() << "error: cannot determine NeverC install directory\n";
    return 1;
  }

  if (Argc < 2) {
    printUsage();
    return 0;
  }

  StringRef Cmd(Argv[1]);

  StringRef UserVersion;
  for (int I = 2; I < Argc; ++I) {
    if (StringRef(Argv[I]) == "--version" && I + 1 < Argc)
      UserVersion = Argv[++I];
  }

  if (Cmd == "install") {
    if (Argc < 3) {
      errs() << "usage: neverc runtime install <target>\n"
             << "       neverc runtime install all\n";
      return 1;
    }
    StringRef Target(Argv[2]);
    if (isAllTarget(Target))
      return doInstallAll(Root, UserVersion);
    return doInstall(Target, Root, UserVersion);
  }

  if (Cmd == "update" || Cmd == "upgrade") {
    if (Argc < 3) {
      errs() << "usage: neverc runtime update <target>\n";
      return 1;
    }
    return doUpdate(Argv[2], Root, UserVersion);
  }

  if (Cmd == "remove" || Cmd == "uninstall") {
    if (Argc < 3) {
      errs() << "usage: neverc runtime remove <target>\n";
      return 1;
    }
    return doRemove(Argv[2], Root);
  }

  if (Cmd == "list" || Cmd == "ls")
    return doList(Root);

  if (Cmd == "-h" || Cmd == "--help" || Cmd == "help") {
    printUsage();
    return 0;
  }

  errs() << "error: unknown runtime command '" << Cmd << "'\n";
  printUsage();
  return 1;
}

} // namespace runtime
} // namespace neverc
