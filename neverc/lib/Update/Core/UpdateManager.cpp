//===--- UpdateManager.cpp - Compiler/runtime release synchronization -----===//

#include "neverc/Update/UpdateManager.h"

#include "neverc/Release/ReleaseClient.h"
#include "neverc/Runtime/RuntimeManager.h"
#include "neverc/Runtime/RuntimeManifest.h"
#include "neverc/Update/UpdateTransaction.h"

#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include "llvm/Support/Windows/WindowsSupport.h"
#include <windows.h>
#endif

using namespace llvm;

namespace neverc {
namespace update {
namespace {

constexpr StringLiteral UpdateLockName(".neverc-update.lock");
constexpr StringLiteral UpdateStagePrefix(".neverc-update-");

struct UpdateOptions {
  std::string RequestedVersion;
  bool VersionSpecified = false;
  bool Help = false;
};

struct InstalledRuntime {
  const runtime::RuntimeTarget *Target = nullptr;
  std::optional<std::string> InstalledTag;
  bool NeedsSynchronization = false;
};

SmallString<256> joinedPath(StringRef Base, StringRef Relative) {
  SmallString<256> Result(Base);
  if (!Relative.empty())
    sys::path::append(Result, Relative);
  return Result;
}

std::string errorToString(Error E) {
  auto Message = toString(std::move(E));
  return std::string(Message.begin(), Message.end());
}

int reportError(StringRef Context, Error E) {
  errs() << "error: " << Context << ": " << errorToString(std::move(E)) << "\n";
  return 1;
}

Expected<UpdateOptions> parseOptions(int Argc, const char **Argv) {
  UpdateOptions Options;
  for (int I = 1; I < Argc; ++I) {
    StringRef Argument(Argv[I]);
    if (Argument == "-h" || Argument == "--help" || Argument == "help") {
      Options.Help = true;
      continue;
    }
    if (Argument == "--yes" || Argument == "-y")
      continue; // Updates are non-interactive; accepted for scripting symmetry.

    StringRef Version;
    if (Argument == "--version") {
      if (++I >= Argc)
        return createStringError(inconvertibleErrorCode(),
                                 "--version requires a release tag");
      Version = Argv[I];
    } else if (Argument.starts_with("--version=")) {
      Version = Argument.drop_front(StringRef("--version=").size());
    } else if (Argument.starts_with("-")) {
      return createStringError(inconvertibleErrorCode(),
                               "unknown update option '%s'",
                               Argument.str().c_str());
    } else {
      Version = Argument;
    }

    if (Options.VersionSpecified)
      return createStringError(inconvertibleErrorCode(),
                               "only one update version may be specified");
    Options.RequestedVersion = Version.str();
    Options.VersionSpecified = true;
  }
  return Options;
}

void printUsage() {
  outs() << "neverc update — update the compiler and installed runtimes\n\n"
         << "Usage:\n"
         << "  neverc update                 Update to the newest complete "
            "release\n"
         << "  neverc update <version>       Update or downgrade to one exact "
            "release\n\n"
         << "Examples:\n"
         << "  neverc update\n"
         << "  neverc update v3389.1.2\n"
         << "  neverc update 3389.1.2\n\n"
         << "Every currently installed cross-compilation runtime is aligned "
            "to the same tag.\n";
}

Expected<std::array<uint64_t, 3>> parseReleaseComponents(StringRef Tag) {
  std::string Normalized = release::normalizeReleaseTag(Tag);
  if (Normalized.empty() || Normalized == "latest")
    return createStringError(inconvertibleErrorCode(),
                             "invalid concrete release tag '%s'",
                             Tag.str().c_str());
  StringRef Body(Normalized);
  Body = Body.drop_front();
  SmallVector<StringRef, 3> Parts;
  Body.split(Parts, '.');
  if (Parts.size() != 3)
    return createStringError(inconvertibleErrorCode(),
                             "invalid release tag '%s'", Tag.str().c_str());

  std::array<uint64_t, 3> Result{};
  for (size_t I = 0; I < Parts.size(); ++I) {
    if (Parts[I].getAsInteger(10, Result[I]))
      return createStringError(inconvertibleErrorCode(),
                               "release component is too large in '%s'",
                               Tag.str().c_str());
  }
  return Result;
}

Expected<sys::fs::file_type> pathTypeWithoutFollowing(StringRef Path) {
  sys::fs::file_status Status;
  if (std::error_code EC = sys::fs::status(Path, Status, /*follow=*/false))
    return createStringError(EC, "cannot inspect '%s'", Path.str().c_str());
  return Status.type();
}

Expected<std::string> relativePathBelow(StringRef Base, StringRef FullPath) {
  if (!FullPath.starts_with(Base))
    return createStringError(inconvertibleErrorCode(),
                             "path '%s' is outside '%s'",
                             FullPath.str().c_str(), Base.str().c_str());
  StringRef Relative = FullPath.drop_front(Base.size());
  while (!Relative.empty() && sys::path::is_separator(Relative.front()))
    Relative = Relative.drop_front();
  if (Relative.empty())
    return createStringError(inconvertibleErrorCode(),
                             "cannot use an empty relative update path");
  SmallString<256> Slashes = sys::path::convert_to_slash(Relative);
  return Slashes.str().str();
}

Error validateReleaseInstallRoot(StringRef Root) {
  if (sys::path::root_path(Root) == Root)
    return createStringError(inconvertibleErrorCode(),
                             "refusing to update a filesystem root");
  SmallString<256> Cache(Root);
  sys::path::append(Cache, "CMakeCache.txt");
  SmallString<256> Ninja(Root);
  sys::path::append(Ninja, "build.ninja");
  SmallString<256> Makefile(Root);
  sys::path::append(Makefile, "Makefile");
  if (sys::fs::exists(Cache) &&
      (sys::fs::exists(Ninja) || sys::fs::exists(Makefile)))
    return createStringError(
        inconvertibleErrorCode(),
        "'%s' is a CMake build tree; update a release installation instead",
        Root.str().c_str());
  return Error::success();
}

Error writeLockOwner(StringRef LockDirectory) {
  SmallString<256> Owner(LockDirectory);
  sys::path::append(Owner, "owner");
  std::error_code EC;
  raw_fd_ostream Output(Owner, EC, sys::fs::OF_Text);
  if (EC)
    return createStringError(EC, "cannot write update lock owner");
  Output << "pid=" << sys::Process::getProcessId() << "\n";
  return Error::success();
}

Expected<std::string> stageReleaseAsset(StringRef ReleaseTag,
                                        StringRef AssetName,
                                        StringRef DownloadsDirectory,
                                        StringRef ChecksumManifest) {
  SmallString<256> Destination(DownloadsDirectory);
  sys::path::append(Destination, AssetName);
  outs() << "Downloading " << AssetName << "...\n";
  if (Error E =
          release::downloadReleaseAsset(ReleaseTag, AssetName, Destination))
    return std::move(E);
  if (Error E =
          release::verifyReleaseAsset(Destination, ChecksumManifest, AssetName))
    return std::move(E);
  outs() << "  SHA256 verified: " << AssetName << "\n";
  return Destination.str().str();
}

struct RemoveFileGuard {
  SmallString<256> Path;
  explicit RemoveFileGuard(SmallString<256> Path) : Path(std::move(Path)) {}
  ~RemoveFileGuard() { (void)sys::fs::remove(Path); }
};

Error validateCompilerVersion(StringRef Executable, StringRef TargetTag) {
  std::string Normalized = release::normalizeReleaseTag(TargetTag);
  if (Normalized.empty() || Normalized == "latest")
    return createStringError(inconvertibleErrorCode(),
                             "cannot validate an invalid compiler version");

#ifndef _WIN32
  sys::fs::file_status Status;
  if (std::error_code EC = sys::fs::status(Executable, Status))
    return createStringError(EC, "cannot inspect staged compiler '%s'",
                             Executable.str().c_str());
  if (!sys::fs::can_execute(Executable)) {
    sys::fs::perms Permissions = Status.permissions();
    Permissions |=
        sys::fs::owner_exe | sys::fs::group_exe | sys::fs::others_exe;
    if (std::error_code EC = sys::fs::setPermissions(Executable, Permissions))
      return createStringError(EC, "cannot make staged compiler executable");
  }
#endif

  SmallString<256> StdoutPath;
  if (std::error_code EC =
          sys::fs::createTemporaryFile("neverc-version", "out", StdoutPath))
    return createStringError(EC, "cannot create compiler validation output");
  RemoveFileGuard StdoutGuard(StdoutPath);
  SmallString<256> StderrPath;
  if (std::error_code EC =
          sys::fs::createTemporaryFile("neverc-version", "err", StderrPath))
    return createStringError(EC,
                             "cannot create compiler validation error output");
  RemoveFileGuard StderrGuard(StderrPath);

  StringRef Args[] = {Executable, "-dumpversion"};
  StringRef Redirects[] = {StringRef(""), StdoutPath, StderrPath};
  SmallString<256> ExecuteMessage;
  int RC = sys::ExecuteAndWait(Executable, Args, /*Env=*/std::nullopt,
                               Redirects, /*SecondsToWait=*/60,
                               /*MemoryLimit=*/0, &ExecuteMessage);
  if (RC != 0) {
    std::string Detail;
    if (ErrorOr<std::unique_ptr<MemoryBuffer>> ErrorOutput =
            MemoryBuffer::getFile(StderrPath))
      Detail = ErrorOutput.get()->getBuffer().trim().str();
    return createStringError(inconvertibleErrorCode(),
                             "staged compiler failed version validation%s%s",
                             Detail.empty() ? "" : ": ", Detail.c_str());
  }

  ErrorOr<std::unique_ptr<MemoryBuffer>> Output =
      MemoryBuffer::getFile(StdoutPath, /*IsText=*/true);
  if (!Output)
    return createStringError(Output.getError(),
                             "cannot read compiler validation output");
  StringRef Expected = StringRef(Normalized).drop_front();
  StringRef Actual = Output.get()->getBuffer().trim();
  if (Actual != Expected)
    return createStringError(
        inconvertibleErrorCode(),
        "compiler archive reports version '%s', expected '%s'",
        Actual.str().c_str(), Expected.str().c_str());
  return Error::success();
}

Error validateLiveInstallation(StringRef Root, StringRef ExecutableRelativePath,
                               StringRef TargetTag) {
  SmallString<256> Executable = joinedPath(Root, ExecutableRelativePath);
  if (Error E = validateCompilerVersion(Executable, TargetTag))
    return std::move(E);

  SmallString<256> RuntimeDirectory(Root);
  sys::path::append(RuntimeDirectory, "runtime");
  for (const runtime::RuntimeTarget &Target : runtime::getRuntimeTargets()) {
    if (!runtime::isRuntimeTargetInstalled(RuntimeDirectory, Target))
      continue;
    std::optional<std::string> InstalledTag =
        runtime::getInstalledReleaseTag(RuntimeDirectory, Target.Name);
    if (!InstalledTag || *InstalledTag != TargetTag)
      return createStringError(
          inconvertibleErrorCode(),
          "runtime '%s' did not commit at target release '%s'",
          Target.Name.str().c_str(), TargetTag.str().c_str());
    if (!Target.SharedDir.empty()) {
      SmallString<256> Shared(RuntimeDirectory);
      sys::path::append(Shared, Target.SharedDir);
      if (!sys::fs::is_directory(Shared))
        return createStringError(
            inconvertibleErrorCode(),
            "runtime '%s' is missing shared directory '%s'",
            Target.Name.str().c_str(), Target.SharedDir.str().c_str());
    }
  }
  return Error::success();
}

Expected<std::string> stageRelativePath(StringRef Stage, StringRef FullPath) {
  return relativePathBelow(Stage, FullPath);
}

Error addCompilerEntries(UpdateTransaction &Transaction, StringRef Stage,
                         StringRef InstallTree,
                         ArrayRef<std::string> InstallFiles) {
  for (const std::string &LiveRelative : InstallFiles) {
    SmallString<256> FullStaged = joinedPath(InstallTree, LiveRelative);
    Expected<std::string> StagedRelative = stageRelativePath(Stage, FullStaged);
    if (!StagedRelative)
      return StagedRelative.takeError();
    if (Error E = Transaction.addEntry(*StagedRelative, LiveRelative))
      return std::move(E);
  }
  return Error::success();
}

Error addRuntimeEntries(UpdateTransaction &Transaction, StringRef Stage,
                        StringRef RuntimeExtract,
                        ArrayRef<InstalledRuntime> Runtimes) {
  std::set<std::string> AddedSharedDirectories;
  for (const InstalledRuntime &Installed : Runtimes) {
    if (!Installed.NeedsSynchronization)
      continue;
    SmallString<256> StagedCheck =
        joinedPath(RuntimeExtract, Installed.Target->CheckDir);
    Expected<std::string> StagedRelative =
        stageRelativePath(Stage, StagedCheck);
    if (!StagedRelative)
      return StagedRelative.takeError();
    std::string Live = (Twine("runtime/") + Installed.Target->CheckDir).str();
    if (Error E = Transaction.addEntry(*StagedRelative, Live))
      return std::move(E);

    if (!Installed.Target->SharedDir.empty() &&
        AddedSharedDirectories.insert(Installed.Target->SharedDir.str())
            .second) {
      SmallString<256> StagedShared =
          joinedPath(RuntimeExtract, Installed.Target->SharedDir);
      Expected<std::string> SharedRelative =
          stageRelativePath(Stage, StagedShared);
      if (!SharedRelative)
        return SharedRelative.takeError();
      std::string SharedLive =
          (Twine("runtime/") + Installed.Target->SharedDir).str();
      if (Error E = Transaction.addEntry(*SharedRelative, SharedLive))
        return std::move(E);
    }
  }

  if (!Runtimes.empty()) {
    SmallString<256> Manifest(RuntimeExtract);
    sys::path::append(Manifest, "manifest.json");
    Expected<std::string> ManifestRelative = stageRelativePath(Stage, Manifest);
    if (!ManifestRelative)
      return ManifestRelative.takeError();
    if (Error E =
            Transaction.addEntry(*ManifestRelative, "runtime/manifest.json"))
      return std::move(E);
  }
  return Error::success();
}

#ifdef _WIN32
void scheduleSelfDeletion(StringRef Argv0) {
  std::string Executable =
      sys::fs::getMainExecutable(Argv0, (void *)(intptr_t)&runUpdateHelper);
  SmallVector<wchar_t, 260> WidePath;
  if (!sys::windows::widenPath(Executable, WidePath))
    (void)::MoveFileExW(WidePath.data(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
}

Error waitForParent(unsigned long ParentPID) {
  HANDLE Parent = ::OpenProcess(SYNCHRONIZE, FALSE, ParentPID);
  if (!Parent) {
    if (::GetLastError() == ERROR_INVALID_PARAMETER)
      return Error::success(); // Parent exited before the helper opened it.
    return createStringError(
        std::error_code(::GetLastError(), std::system_category()),
        "cannot open updater parent process");
  }
  DWORD Result = ::WaitForSingleObject(Parent, INFINITE);
  ::CloseHandle(Parent);
  if (Result != WAIT_OBJECT_0)
    return createStringError(
        std::error_code(::GetLastError(), std::system_category()),
        "cannot wait for updater parent process");
  return Error::success();
}

Expected<unsigned long> launchWindowsHelper(StringRef CurrentExecutable,
                                            StringRef Stage) {
  int FD = -1;
  SmallString<256> Helper;
  if (std::error_code EC =
          sys::fs::createTemporaryFile("neverc-updater", "exe", FD, Helper))
    return createStringError(EC, "cannot allocate temporary update helper");
  (void)sys::Process::SafelyCloseFileDescriptor(FD);
  if (std::error_code EC = sys::fs::remove(Helper))
    return createStringError(EC, "cannot prepare temporary update helper");
  if (std::error_code EC = sys::fs::copy_file(CurrentExecutable, Helper))
    return createStringError(EC, "cannot copy temporary update helper");

  std::string PID = Twine(sys::Process::getProcessId()).str();
  StringRef Args[] = {Helper, "__neverc_apply_update", Stage, PID};
  SmallString<256> Message;
  bool ExecutionFailed = false;
  sys::ProcessInfo Child =
      sys::ExecuteNoWait(Helper, Args, /*Env=*/{}, /*Redirects=*/{},
                         /*MemoryLimit=*/0, &Message, &ExecutionFailed);
  if (Child.Pid == sys::ProcessInfo::InvalidPid || ExecutionFailed) {
    (void)sys::fs::remove(Helper);
    return createStringError(inconvertibleErrorCode(),
                             "cannot launch update helper%s%s",
                             Message.empty() ? "" : ": ", Message.c_str());
  }
  if (Child.Process)
    ::CloseHandle(static_cast<HANDLE>(Child.Process));
  return Child.Pid;
}
#endif

} // namespace

Expected<VersionRelation> compareReleaseTags(StringRef CurrentTag,
                                             StringRef TargetTag) {
  Expected<std::array<uint64_t, 3>> Current =
      parseReleaseComponents(CurrentTag);
  if (!Current)
    return Current.takeError();
  Expected<std::array<uint64_t, 3>> Target = parseReleaseComponents(TargetTag);
  if (!Target)
    return Target.takeError();
  if (*Target < *Current)
    return VersionRelation::Older;
  if (*Target > *Current)
    return VersionRelation::Newer;
  return VersionRelation::Same;
}

Expected<std::vector<std::string>>
collectCompilerInstallFiles(StringRef InstallTreeRoot) {
  if (!sys::fs::is_directory(InstallTreeRoot))
    return createStringError(inconvertibleErrorCode(),
                             "compiler archive install tree is missing: '%s'",
                             InstallTreeRoot.str().c_str());

  const std::set<StringRef> ManagedRoots = {"bin", "lib", "pluginsdk"};
  const std::set<StringRef> AllowedRoots = {"bin", "lib", "pluginsdk",
                                            "runtime"};
  std::set<std::string> SeenRoots;
  std::error_code EC;
  for (sys::fs::directory_iterator I(InstallTreeRoot, EC,
                                     /*follow_symlinks=*/false),
       E;
       I != E && !EC; I.increment(EC)) {
    StringRef Name = sys::path::filename(I->path());
    if (!AllowedRoots.count(Name))
      return createStringError(inconvertibleErrorCode(),
                               "unexpected compiler archive root '%s'",
                               Name.str().c_str());
    Expected<sys::fs::file_type> Type = pathTypeWithoutFollowing(I->path());
    if (!Type)
      return Type.takeError();
    if (*Type != sys::fs::file_type::directory_file)
      return createStringError(inconvertibleErrorCode(),
                               "compiler archive root '%s' is not a directory",
                               Name.str().c_str());
    SeenRoots.insert(Name.str());
  }
  if (EC)
    return createStringError(EC, "cannot enumerate compiler install tree");
  for (StringRef Required : ManagedRoots)
    if (!SeenRoots.count(Required.str()))
      return createStringError(inconvertibleErrorCode(),
                               "compiler archive is missing '%s/'",
                               Required.str().c_str());

  std::vector<std::string> Files;
  for (StringRef RootName : ManagedRoots) {
    SmallString<256> Managed = joinedPath(InstallTreeRoot, RootName);
    std::error_code WalkError;
    for (sys::fs::recursive_directory_iterator
             I(Managed, WalkError, /*follow_symlinks=*/false),
         E;
         I != E && !WalkError; I.increment(WalkError)) {
      Expected<sys::fs::file_type> Type = pathTypeWithoutFollowing(I->path());
      if (!Type)
        return Type.takeError();
      if (*Type == sys::fs::file_type::directory_file)
        continue;
      if (*Type != sys::fs::file_type::regular_file &&
          *Type != sys::fs::file_type::symlink_file)
        return createStringError(
            inconvertibleErrorCode(),
            "unsupported file type in compiler archive: '%s'",
            I->path().c_str());
      Expected<std::string> Relative =
          relativePathBelow(InstallTreeRoot, I->path());
      if (!Relative)
        return Relative.takeError();
      Files.push_back(std::move(*Relative));
    }
    if (WalkError)
      return createStringError(WalkError,
                               "cannot enumerate compiler archive '%s/'",
                               RootName.str().c_str());
  }
  std::sort(Files.begin(), Files.end());
  return Files;
}

int runUpdate(int Argc, const char **Argv, const char *Argv0) {
  Expected<UpdateOptions> OptionsOrErr = parseOptions(Argc, Argv);
  if (!OptionsOrErr)
    return reportError("invalid update command", OptionsOrErr.takeError());
  UpdateOptions Options = std::move(*OptionsOrErr);
  if (Options.Help) {
    printUsage();
    return 0;
  }

  Expected<std::string> RootOrErr =
      release::resolveInstallRoot(Argv0, (void *)(intptr_t)&runUpdate);
  if (!RootOrErr)
    return reportError("cannot locate release installation",
                       RootOrErr.takeError());
  std::string Root = std::move(*RootOrErr);
  if (Error E = validateReleaseInstallRoot(Root))
    return reportError("refusing update", std::move(E));

  SmallString<256> LockDirectory(Root);
  sys::path::append(LockDirectory, UpdateLockName);
  if (std::error_code EC =
          sys::fs::create_directory(LockDirectory, /*IgnoreExisting=*/false)) {
    if (EC == std::errc::file_exists)
      errs() << "error: another update may be running; lock exists at "
             << LockDirectory << "\n"
             << "       remove it only after confirming no updater is active\n";
    else
      errs() << "error: cannot acquire update lock at " << LockDirectory << ": "
             << EC.message() << "\n";
    return 1;
  }
  auto LockCleanup = make_scope_exit([&] {
    (void)sys::fs::remove_directories(LockDirectory,
                                      /*IgnoreErrors=*/false);
  });
  if (Error E = writeLockOwner(LockDirectory))
    return reportError("cannot initialize update lock", std::move(E));

  Expected<release::HostDistribution> HostOrErr =
      release::getHostDistribution();
  if (!HostOrErr)
    return reportError("unsupported update host", HostOrErr.takeError());
  release::HostDistribution Host = std::move(*HostOrErr);

  std::string TargetTag;
  if (!Options.VersionSpecified ||
      StringRef(Options.RequestedVersion).equals_insensitive("latest")) {
    outs() << "Resolving newest release for " << Host.CompilerAsset << "...\n";
    Expected<std::string> Latest =
        release::queryLatestReleaseTagForAsset(Host.CompilerAsset);
    if (!Latest)
      return reportError("cannot resolve newest complete release",
                         Latest.takeError());
    TargetTag = std::move(*Latest);
  } else {
    TargetTag = release::normalizeReleaseTag(Options.RequestedVersion);
    if (TargetTag.empty() || TargetTag == "latest") {
      errs() << "error: invalid update version '" << Options.RequestedVersion
             << "' — expected vMAJOR.MINOR.PATCH\n";
      return 1;
    }
  }

  std::string CurrentTag = runtime::getCompilerReleaseTag();
  Expected<VersionRelation> RelationOrErr =
      compareReleaseTags(CurrentTag, TargetTag);
  if (!RelationOrErr)
    return reportError("cannot compare release versions",
                       RelationOrErr.takeError());
  VersionRelation Relation = *RelationOrErr;

  SmallString<256> RuntimeDirectory(Root);
  sys::path::append(RuntimeDirectory, "runtime");
  std::vector<InstalledRuntime> InstalledRuntimes;
  for (const runtime::RuntimeTarget &Target : runtime::getRuntimeTargets()) {
    if (!runtime::isRuntimeTargetInstalled(RuntimeDirectory, Target))
      continue;
    InstalledRuntime Installed;
    Installed.Target = &Target;
    Installed.InstalledTag =
        runtime::getInstalledReleaseTag(RuntimeDirectory, Target.Name);
    Installed.NeedsSynchronization = Relation != VersionRelation::Same ||
                                     !Installed.InstalledTag ||
                                     *Installed.InstalledTag != TargetTag;
    InstalledRuntimes.push_back(std::move(Installed));
  }

  std::vector<InstalledRuntime> RuntimesToSynchronize;
  for (const InstalledRuntime &Installed : InstalledRuntimes)
    if (Installed.NeedsSynchronization)
      RuntimesToSynchronize.push_back(Installed);

  std::string CurrentExecutable =
      sys::fs::getMainExecutable(Argv0, (void *)(intptr_t)&runUpdate);
  if (CurrentExecutable.empty()) {
    errs() << "error: cannot resolve live NeverC executable\n";
    return 1;
  }

  outs() << "\n  NeverC update\n"
         << "  Current:  " << CurrentTag << "\n"
         << "  Target:   " << TargetTag << "\n"
         << "  Platform: " << Host.Platform << "\n"
         << "  Runtimes: " << InstalledRuntimes.size() << " installed, "
         << RuntimesToSynchronize.size() << " to synchronize\n\n";

  if (Relation == VersionRelation::Same) {
    if (Error E = validateCompilerVersion(CurrentExecutable, TargetTag))
      return reportError("live compiler version check failed", std::move(E));
    if (RuntimesToSynchronize.empty()) {
      outs() << "NeverC and all installed runtimes are already at " << TargetTag
             << ".\n";
      return 0;
    }
  } else if (Relation == VersionRelation::Older) {
    outs() << "Downgrading the compiler and installed runtimes together.\n";
  } else {
    outs() << "Updating the compiler and installed runtimes together.\n";
  }

  SmallString<256> StagePrefix(Root);
  sys::path::append(StagePrefix, UpdateStagePrefix);
  SmallString<256> Stage;
  if (std::error_code EC = sys::fs::createUniqueDirectory(StagePrefix, Stage)) {
    errs() << "error: cannot create update staging directory under " << Root
           << ": " << EC.message() << "\n";
    return 1;
  }
  bool PreserveStage = false;
  auto StageCleanup = make_scope_exit([&] {
    if (!PreserveStage)
      (void)sys::fs::remove_directories(Stage, /*IgnoreErrors=*/false);
  });

  SmallString<256> Downloads(Stage);
  sys::path::append(Downloads, "downloads");
  if (std::error_code EC = sys::fs::create_directories(Downloads))
    return reportError("cannot create download staging area",
                       errorCodeToError(EC));

  SmallString<256> ChecksumManifest(Downloads);
  sys::path::append(ChecksumManifest, "SHA256SUMS");
  outs() << "Downloading SHA256SUMS...\n";
  if (Error E = release::downloadReleaseAsset(TargetTag, "SHA256SUMS",
                                              ChecksumManifest))
    return reportError("cannot download checksum manifest", std::move(E));

  std::optional<std::string> CompilerArchive;
  if (Relation != VersionRelation::Same) {
    Expected<std::string> Archive = stageReleaseAsset(
        TargetTag, Host.CompilerAsset, Downloads, ChecksumManifest);
    if (!Archive)
      return reportError("cannot stage compiler", Archive.takeError());
    CompilerArchive = std::move(*Archive);
  }

  std::map<std::string, std::string> RuntimeArchives;
  for (const InstalledRuntime &Installed : RuntimesToSynchronize) {
    std::string Asset =
        formatv("neverc-runtime-{0}.zip", Installed.Target->Name).str();
    Expected<std::string> Archive =
        stageReleaseAsset(TargetTag, Asset, Downloads, ChecksumManifest);
    if (!Archive)
      return reportError("cannot stage installed runtimes",
                         Archive.takeError());
    RuntimeArchives.emplace(Installed.Target->Name.str(), std::move(*Archive));
  }

  SmallString<256> CompilerInstallTree;
  std::vector<std::string> CompilerFiles;
  if (CompilerArchive) {
    SmallString<256> CompilerExtract(Stage);
    sys::path::append(CompilerExtract, "compiler");
    outs() << "Extracting " << Host.CompilerAsset << "...\n";
    if (Error E = release::extractZip(*CompilerArchive, CompilerExtract))
      return reportError("cannot extract compiler archive", std::move(E));
    CompilerInstallTree = CompilerExtract;
    if (!Host.ArchiveRoot.empty())
      sys::path::append(CompilerInstallTree, Host.ArchiveRoot);

    SmallString<256> StagedExecutable =
        joinedPath(CompilerInstallTree, Host.ExecutableRelativePath);
    if (!sys::fs::is_regular_file(StagedExecutable)) {
      errs() << "error: compiler archive is missing "
             << Host.ExecutableRelativePath << "\n";
      return 1;
    }
    if (Error E = validateCompilerVersion(StagedExecutable, TargetTag))
      return reportError("compiler archive validation failed", std::move(E));

    Expected<std::vector<std::string>> Files =
        collectCompilerInstallFiles(CompilerInstallTree);
    if (!Files)
      return reportError("compiler archive layout is invalid",
                         Files.takeError());
    CompilerFiles = std::move(*Files);
    if (!llvm::is_contained(CompilerFiles, Host.ExecutableRelativePath)) {
      errs() << "error: validated compiler executable was not selected for "
                "installation\n";
      return 1;
    }
  }

  SmallString<256> RuntimeExtract;
  if (!RuntimesToSynchronize.empty()) {
    RuntimeExtract = Stage;
    sys::path::append(RuntimeExtract, "runtimes");
    if (std::error_code EC = sys::fs::create_directories(RuntimeExtract))
      return reportError("cannot create runtime staging area",
                         errorCodeToError(EC));

    for (const InstalledRuntime &Installed : RuntimesToSynchronize) {
      StringRef Archive = RuntimeArchives.at(Installed.Target->Name.str());
      outs() << "Extracting runtime " << Installed.Target->Name << "...\n";
      if (Error E = release::extractZip(Archive, RuntimeExtract))
        return reportError("cannot extract runtime archive", std::move(E));
      SmallString<256> Check =
          joinedPath(RuntimeExtract, Installed.Target->CheckDir);
      if (!sys::fs::is_directory(Check))
        return reportError(
            "runtime archive layout is invalid",
            createStringError(inconvertibleErrorCode(), "'%s' is missing '%s/'",
                              Installed.Target->Name.str().c_str(),
                              Installed.Target->CheckDir.str().c_str()));
      if (!Installed.Target->SharedDir.empty()) {
        SmallString<256> Shared =
            joinedPath(RuntimeExtract, Installed.Target->SharedDir);
        if (!sys::fs::is_directory(Shared))
          return reportError(
              "runtime archive layout is invalid",
              createStringError(inconvertibleErrorCode(),
                                "'%s' is missing '%s/'",
                                Installed.Target->Name.str().c_str(),
                                Installed.Target->SharedDir.str().c_str()));
      }
    }

    // Build one reconciled manifest for every target that was installed at
    // command start, including targets already at TargetTag during repair.
    for (const InstalledRuntime &Installed : InstalledRuntimes) {
      if (!runtime::recordInstalledTarget(RuntimeExtract,
                                          Installed.Target->Name, TargetTag))
        return reportError(
            "cannot create synchronized runtime manifest",
            createStringError(inconvertibleErrorCode(),
                              "failed to record runtime '%s'",
                              Installed.Target->Name.str().c_str()));
    }
  }

  Expected<UpdateTransaction> TransactionOrErr =
      UpdateTransaction::create(Root, Stage, TargetTag);
  if (!TransactionOrErr)
    return reportError("cannot create update transaction",
                       TransactionOrErr.takeError());
  UpdateTransaction Transaction = std::move(*TransactionOrErr);
  if (CompilerArchive) {
    if (Error E = addCompilerEntries(Transaction, Stage, CompilerInstallTree,
                                     CompilerFiles))
      return reportError("cannot plan compiler replacement", std::move(E));
  }
  if (!RuntimesToSynchronize.empty()) {
    if (Error E = addRuntimeEntries(Transaction, Stage, RuntimeExtract,
                                    InstalledRuntimes))
      return reportError("cannot plan runtime replacement", std::move(E));
  }
  if (Error E = Transaction.writePlan())
    return reportError("cannot persist update transaction", std::move(E));

#ifdef _WIN32
  Expected<unsigned long> HelperPID =
      launchWindowsHelper(CurrentExecutable, Stage);
  if (!HelperPID)
    return reportError("cannot delegate running executable replacement",
                       HelperPID.takeError());
  PreserveStage = true;
  StageCleanup.release();
  LockCleanup.release();
  outs() << "\nUpdate staged successfully. Helper process " << *HelperPID
         << " will commit it after this process exits.\n";
  return 0;
#else
  Error Apply = Transaction.apply([&] {
    return validateLiveInstallation(Root, Host.ExecutableRelativePath,
                                    TargetTag);
  });
  if (Apply) {
    PreserveStage = true;
    errs() << "error: update transaction failed: "
           << errorToString(std::move(Apply)) << "\n"
           << "       staging/recovery data retained at " << Stage << "\n";
    return 1;
  }
  outs() << "\nDone! NeverC and " << InstalledRuntimes.size()
         << " installed runtime(s) are aligned at " << TargetTag << ".\n";
  return 0;
#endif
}

int runUpdateHelper(int Argc, const char **Argv, const char *Argv0) {
#ifndef _WIN32
  (void)Argc;
  (void)Argv;
  (void)Argv0;
  errs() << "error: the private update helper is only used on Windows\n";
  return 1;
#else
  auto SelfDelete = make_scope_exit([&] { scheduleSelfDeletion(Argv0); });
  if (Argc != 3) {
    errs() << "error: invalid private update helper invocation\n";
    return 1;
  }
  StringRef Stage(Argv[1]);
  unsigned long ParentPID = 0;
  if (StringRef(Argv[2]).getAsInteger(10, ParentPID) || ParentPID == 0) {
    errs() << "error: invalid update helper parent PID\n";
    return 1;
  }

  Expected<UpdateTransaction> TransactionOrErr =
      UpdateTransaction::readPlan(Stage);
  if (!TransactionOrErr)
    return reportError("cannot load delegated update transaction",
                       TransactionOrErr.takeError());
  UpdateTransaction Transaction = std::move(*TransactionOrErr);
  SmallString<256> LockDirectory(Transaction.root());
  sys::path::append(LockDirectory, UpdateLockName);
  if (!sys::fs::is_directory(LockDirectory)) {
    errs() << "error: delegated update lock is missing\n";
    return 1;
  }
  if (Error E = waitForParent(ParentPID))
    return reportError("delegated updater cannot wait for parent",
                       std::move(E));

  Expected<release::HostDistribution> Host = release::getHostDistribution();
  if (!Host)
    return reportError("delegated updater host is unsupported",
                       Host.takeError());
  Error Apply = Transaction.apply([&] {
    return validateLiveInstallation(Transaction.root(),
                                    Host->ExecutableRelativePath,
                                    Transaction.targetTag());
  });
  (void)sys::fs::remove_directories(LockDirectory,
                                    /*IgnoreErrors=*/false);
  if (Apply) {
    errs() << "error: delegated update transaction failed: "
           << errorToString(std::move(Apply)) << "\n"
           << "       staging/recovery data retained at " << Stage << "\n";
    return 1;
  }
  if (std::error_code EC =
          sys::fs::remove_directories(Stage, /*IgnoreErrors=*/false))
    errs() << "warning: update succeeded but staging cleanup failed: "
           << EC.message() << "\n";
  outs() << "\nDone! NeverC is aligned at " << Transaction.targetTag() << ".\n";
  return 0;
#endif
}

} // namespace update
} // namespace neverc
