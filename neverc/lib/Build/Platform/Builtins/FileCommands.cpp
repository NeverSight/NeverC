#include "Platform/Builtins/Internal.h"

#include "neverc/Build/Platform.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace neverc {
namespace build {
namespace builtins {
namespace internal {

bool parseOctalMode(llvm::StringRef Text, unsigned &Mode);
std::error_code chmodPath(llvm::StringRef Path, unsigned Mode);

bool applyRmFlagCluster(llvm::StringRef Cluster, bool &Force,
                        bool &Recursive) {
  if (Cluster.size() < 2 || Cluster[0] != '-' || Cluster.starts_with("--"))
    return false;
  for (size_t I = 1; I < Cluster.size(); ++I) {
    switch (Cluster[I]) {
    case 'f':
      Force = true;
      break;
    case 'r':
    case 'R':
      Recursive = true;
      break;
    default:
      return false;
    }
  }
  return true;
}


bool applyMkdirFlagCluster(llvm::StringRef Cluster, bool &Parents,
                           bool &HaveMode, unsigned &Mode) {
  // Accept Makefile-common clusters such as `-p`, `-pm755`, and `-mp755`.
  if (Cluster.size() < 2 || Cluster[0] != '-' || Cluster.starts_with("--"))
    return false;
  for (size_t I = 1; I < Cluster.size(); ++I) {
    switch (Cluster[I]) {
    case 'p':
      Parents = true;
      break;
    case 'm': {
      llvm::StringRef ModeText = Cluster.drop_front(I + 1);
      if (ModeText.empty() || !parseOctalMode(ModeText, Mode))
        return false;
      HaveMode = true;
      return true; // mode consumes the rest of the cluster
    }
    default:
      return false;
    }
  }
  return true;
}


bool applyCpFlagCluster(llvm::StringRef Cluster, bool &Force, bool &Recursive,
                        bool &Preserve, bool &NoClobber, bool &Update) {
  if (Cluster.size() < 2 || Cluster[0] != '-' || Cluster.starts_with("--"))
    return false;
  for (size_t I = 1; I < Cluster.size(); ++I) {
    switch (Cluster[I]) {
    case 'f':
      Force = true;
      NoClobber = false;
      break;
    case 'n':
      NoClobber = true;
      Force = false;
      break;
    case 'u':
      // GNU cp -u: copy only when the source is newer than the destination
      // (or the destination is missing).
      Update = true;
      break;
    case 'r':
    case 'R':
      Recursive = true;
      break;
    case 'p':
      Preserve = true;
      break;
    case 'a':
      // Approximate GNU `-a` (`-dR --preserve=all`) as recursive + preserve
      // timestamps. Symlink no-dereference (`-d`) stays on the host tool when
      // recipes need bit-identical link copies.
      Recursive = true;
      Preserve = true;
      break;
    default:
      return false;
    }
  }
  return true;
}


bool applyMvFlagCluster(llvm::StringRef Cluster, bool &Force, bool &NoClobber) {
  if (Cluster.size() < 2 || Cluster[0] != '-' || Cluster.starts_with("--"))
    return false;
  for (size_t I = 1; I < Cluster.size(); ++I) {
    switch (Cluster[I]) {
    case 'f':
      Force = true;
      NoClobber = false;
      break;
    case 'n':
      NoClobber = true;
      Force = false;
      break;
    default:
      return false;
    }
  }
  return true;
}


bool applyLnFlagCluster(llvm::StringRef Cluster, bool &Force, bool &Symbolic,
                        bool &NoDereference) {
  if (Cluster.size() < 2 || Cluster[0] != '-' || Cluster.starts_with("--"))
    return false;
  for (size_t I = 1; I < Cluster.size(); ++I) {
    switch (Cluster[I]) {
    case 'f':
      Force = true;
      break;
    case 's':
      Symbolic = true;
      break;
    case 'n':
      // GNU -n / --no-dereference: do not treat a symlink-to-directory dest
      // as a directory to create inside of (common `ln -sfn` Makefile form).
      NoDereference = true;
      break;
    default:
      return false;
    }
  }
  return true;
}


bool applyChmodFlagCluster(llvm::StringRef Cluster, bool &Recursive) {
  if (Cluster.size() < 2 || Cluster[0] != '-' || Cluster.starts_with("--"))
    return false;
  for (size_t I = 1; I < Cluster.size(); ++I) {
    // Accept both POSIX -R and the common -r typo/alias used in recipes.
    if (Cluster[I] != 'R' && Cluster[I] != 'r')
      return false;
    Recursive = true;
  }
  return true;
}


bool isPreserveRootPath(llvm::StringRef Path) {
  // Match GNU rm --preserve-root: never remove the filesystem root, including
  // classic Makefile footguns like `rm -rf $(DESTDIR)/` when DESTDIR is empty.
  std::string Abs = platform::absolutePath(Path.str());
  while (Abs.size() > 1 && (Abs.back() == '/' || Abs.back() == '\\'))
    Abs.pop_back();
#ifdef _WIN32
  const llvm::StringRef Root = llvm::sys::path::root_path(Abs);
  return !Root.empty() && Root == Abs;
#else
  return Abs == "/";
#endif
}


bool isDotOrDotDotOperand(llvm::StringRef Path) {
  // Match GNU rm: refuse any operand whose final component is "." or "..",
  // including forms like `foo/.` and `foo/..` (with or without a trailing
  // separator). `rm -rf .` would otherwise wipe the process working directory.
  llvm::StringRef P = Path;
  while (P.size() > 1 && (P.ends_with("/") || P.ends_with("\\")))
    P = P.drop_back();
  const llvm::StringRef Name = llvm::sys::path::filename(P);
  return Name == "." || Name == "..";
}


bool removePath(llvm::StringRef Path, bool Force, bool Recursive,
                std::string &Error) {
  // Empty operands are not real paths. With `-f`, match GNU/BSD rm and treat
  // them as a successful no-op (same as a missing file). Without `-f`, fail
  // instead of letting make_absolute("") collapse to the working directory in
  // later path checks.
  if (Path.empty()) {
    if (Force)
      return true;
    Error = "cannot remove empty operand";
    return false;
  }
  if (isPreserveRootPath(Path)) {
    Error = (Path + ": refusing to remove '/' (preserve-root)").str();
    return false;
  }
  if (isDotOrDotDotOperand(Path)) {
    Error = (Path + ": refusing to remove '.' or '..' directory").str();
    return false;
  }

  // Do not follow symlinks: a symlink-to-directory is removed as a link,
  // matching POSIX rm (no -r required, and the target tree is not deleted).
  llvm::sys::fs::file_status Status;
  std::error_code EC =
      llvm::sys::fs::status(Path, Status, /*follow=*/false);
  if (EC) {
    if (Force && EC == llvm::errc::no_such_file_or_directory)
      return true;
    Error = (Path + ": " + EC.message()).str();
    return false;
  }

  if (Status.type() == llvm::sys::fs::file_type::directory_file) {
    if (!Recursive) {
      // Match POSIX rm: -f does not make directories removable without -r.
      Error = (Path + ": is a directory").str();
      return false;
    }
    EC = llvm::sys::fs::remove_directories(Path, /*IgnoreErrors=*/false);
    if (EC) {
      Error = (Path + ": " + EC.message()).str();
      return false;
    }
    return true;
  }

  EC = llvm::sys::fs::remove(Path, /*IgnoreNonExisting=*/Force);
  if (EC) {
    Error = (Path + ": " + EC.message()).str();
    return false;
  }
  return true;
}


bool tryExecuteRm(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  bool Force = false;
  bool Recursive = false;
  llvm::SmallVector<Token, 8> Operands;

  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "--") {
      for (size_t J = I + 1; J < Argv.size(); ++J)
        Operands.push_back(Argv[J]);
      break;
    }
    if (Arg == "-") {
      llvm::errs() << "neverc make: rm: cannot remove '-'\n";
      ExitCode = 1;
      return true;
    }
    if (Arg.starts_with("-") && Arg.size() > 1) {
      if (!applyRmFlagCluster(Arg, Force, Recursive))
        return false;
      continue;
    }
    Operands.push_back(Argv[I]);
  }

  // POSIX: `rm -f` with no file operands is a successful no-op.
  if (Operands.empty()) {
    if (Force) {
      ExitCode = 0;
      return true;
    }
    llvm::errs() << "neverc make: rm: missing operand\n";
    ExitCode = 1;
    return true;
  }

  bool Failed = false;
  for (const Token &Operand : Operands) {
    for (const std::string &Path : expandOperand(Operand)) {
      std::string Error;
      if (!removePath(Path, Force, Recursive, Error)) {
        llvm::errs() << "neverc make: rm: " << Error << "\n";
        Failed = true;
      }
    }
  }
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteMkdir(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  bool Parents = false;
  bool HaveMode = false;
  unsigned Mode = 0777;
  llvm::SmallVector<Token, 4> DirToks;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "--") {
      for (size_t J = I + 1; J < Argv.size(); ++J)
        DirToks.push_back(Argv[J]);
      break;
    }
    if (Arg == "-m") {
      if (I + 1 >= Argv.size()) {
        llvm::errs() << "neverc make: mkdir: option requires an argument -- m\n";
        ExitCode = 1;
        return true;
      }
      if (!parseOctalMode(Argv[++I].Text, Mode))
        return false;
      HaveMode = true;
      continue;
    }
    if (Arg.starts_with("-m") && Arg.size() > 2) {
      if (!parseOctalMode(Arg.drop_front(2), Mode))
        return false;
      HaveMode = true;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1) {
      if (!applyMkdirFlagCluster(Arg, Parents, HaveMode, Mode))
        return false;
      continue;
    }
    DirToks.push_back(Argv[I]);
  }
  if (DirToks.empty()) {
    llvm::errs() << "neverc make: mkdir: missing operand\n";
    ExitCode = 1;
    return true;
  }

  bool Failed = false;
  for (const std::string &Dir : expandOperands(DirToks)) {
    if (Dir.empty()) {
      llvm::errs() << "neverc make: mkdir: cannot create directory '': "
                      "No such file or directory\n";
      Failed = true;
      continue;
    }
    // POSIX mkdir fails when the final path exists; mkdir -p does not.
    std::error_code EC =
        Parents ? llvm::sys::fs::create_directories(Dir, /*IgnoreExisting=*/true)
                : llvm::sys::fs::create_directory(Dir, /*IgnoreExisting=*/false);
    if (EC) {
      if (Parents && llvm::sys::fs::is_directory(Dir)) {
        // Fall through so -m can still update an existing final directory.
      } else {
        llvm::errs() << "neverc make: mkdir: " << Dir << ": " << EC.message()
                     << "\n";
        Failed = true;
        continue;
      }
    }
#ifndef _WIN32
    if (HaveMode) {
      if (std::error_code ModeEC = chmodPath(Dir, Mode)) {
        llvm::errs() << "neverc make: mkdir: " << Dir << ": "
                     << ModeEC.message() << "\n";
        Failed = true;
      }
    }
#else
    (void)HaveMode;
    (void)Mode;
#endif
  }
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteTouch(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  bool NoCreate = false;
  bool ChangeAccess = true;
  bool ChangeModify = true;
  bool ExplicitTimes = false;
  std::string RefPath;
  bool HaveRef = false;
  llvm::SmallVector<Token, 4> PathToks;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "--") {
      for (size_t J = I + 1; J < Argv.size(); ++J)
        PathToks.push_back(Argv[J]);
      break;
    }
    if (Arg == "-r") {
      if (I + 1 >= Argv.size()) {
        llvm::errs()
            << "neverc make: touch: option requires an argument -- r\n";
        ExitCode = 1;
        return true;
      }
      RefPath = Argv[++I].Text;
      HaveRef = true;
      continue;
    }
    if (Arg.starts_with("-r") && Arg.size() > 2) {
      RefPath = Arg.drop_front(2).str();
      HaveRef = true;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1 && !Arg.starts_with("--")) {
      // Accept clustered `-amc` forms common in Makefiles.
      for (size_t J = 1; J < Arg.size(); ++J) {
        switch (Arg[J]) {
        case 'c':
          NoCreate = true;
          break;
        case 'a':
          if (!ExplicitTimes) {
            ChangeAccess = true;
            ChangeModify = false;
            ExplicitTimes = true;
          } else {
            ChangeAccess = true;
          }
          break;
        case 'm':
          if (!ExplicitTimes) {
            ChangeAccess = false;
            ChangeModify = true;
            ExplicitTimes = true;
          } else {
            ChangeModify = true;
          }
          break;
        default:
          return false; // flags like -t / -d stay on the host tool
        }
      }
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    PathToks.push_back(Argv[I]);
  }
  if (PathToks.empty()) {
    llvm::errs() << "neverc make: touch: missing operand\n";
    ExitCode = 1;
    return true;
  }
  if (HaveRef && (RefPath.empty() || RefPath == "-"))
    return false;
  // `-r` takes a single path; unquoted globs would change arity — leave to sh.
  if (HaveRef && hasGlobMeta(RefPath))
    return false;

  llvm::sys::TimePoint<> RefAccessTime;
  llvm::sys::TimePoint<> RefModTime;
  const auto Now = std::chrono::system_clock::now();
  if (HaveRef) {
    llvm::sys::fs::file_status RefStatus;
    if (std::error_code EC =
            llvm::sys::fs::status(RefPath, RefStatus, /*follow=*/true)) {
      llvm::errs() << "neverc make: touch: " << RefPath << ": "
                   << EC.message() << "\n";
      ExitCode = 1;
      return true;
    }
    RefAccessTime = RefStatus.getLastAccessedTime();
    RefModTime = RefStatus.getLastModificationTime();
  } else {
    RefAccessTime = Now;
    RefModTime = Now;
  }

  bool Failed = false;
  for (const std::string &Path : expandOperands(PathToks)) {
    int FD = -1;
    std::error_code EC;
    bool Existed = llvm::sys::fs::exists(Path);
    // Existing directories cannot be opened for write; POSIX touch still updates
    // their timestamps. Prefer a read-only fd when the path already exists.
    if (Existed) {
#ifndef _WIN32
      FD = ::open(Path.c_str(), O_RDONLY);
      if (FD < 0)
        EC = std::error_code(errno, std::generic_category());
#else
      EC = llvm::sys::fs::openFileForRead(Path, FD);
#endif
    } else if (NoCreate) {
      continue; // POSIX touch -c: missing files are a successful no-op
    } else {
      EC = llvm::sys::fs::openFileForReadWrite(
          Path, FD, llvm::sys::fs::CD_OpenAlways, llvm::sys::fs::OF_None);
    }
    if (EC) {
      llvm::errs() << "neverc make: touch: " << Path << ": " << EC.message()
                   << "\n";
      Failed = true;
      continue;
    }

    llvm::sys::TimePoint<> AccessTime = RefAccessTime;
    llvm::sys::TimePoint<> ModTime = RefModTime;
    // With -a / -m alone, preserve the untouched stamp on existing paths.
    if (Existed && (!ChangeAccess || !ChangeModify)) {
      llvm::sys::fs::file_status Cur;
      if (!llvm::sys::fs::status(Path, Cur, /*follow=*/true)) {
        if (!ChangeAccess)
          AccessTime = Cur.getLastAccessedTime();
        if (!ChangeModify)
          ModTime = Cur.getLastModificationTime();
      }
    }

    EC = llvm::sys::fs::setLastAccessAndModificationTime(FD, AccessTime,
                                                         ModTime);
#ifdef _WIN32
    ::_close(FD);
#else
    ::close(FD);
#endif
    if (EC) {
      llvm::errs() << "neverc make: touch: " << Path << ": " << EC.message()
                   << "\n";
      Failed = true;
    }
  }
  ExitCode = Failed ? 1 : 0;
  return true;
}


std::error_code preserveTimestamps(llvm::StringRef From, llvm::StringRef To) {
  llvm::sys::fs::file_status Status;
  if (std::error_code EC =
          llvm::sys::fs::status(From, Status, /*follow=*/true))
    return EC;
  int FD = -1;
  std::error_code EC = llvm::sys::fs::openFileForRead(To, FD);
  if (EC)
    return EC;
  EC = llvm::sys::fs::setLastAccessAndModificationTime(
      FD, Status.getLastAccessedTime(), Status.getLastModificationTime());
#ifdef _WIN32
  ::_close(FD);
#else
  ::close(FD);
#endif
  return EC;
}


std::error_code copyRegularFile(llvm::StringRef From, llvm::StringRef To,
                                bool Force, bool Preserve) {
  // Critical: `cp -f a a` must not unlink the source before copying.
  if (pathsEquivalent(From, To))
    return std::make_error_code(std::errc::invalid_argument);
  if (Force && llvm::sys::fs::exists(To) && !isDirectory(To)) {
    if (std::error_code EC = llvm::sys::fs::remove(To))
      return EC;
  }
  if (std::error_code EC = llvm::sys::fs::copy_file(From, To))
    return EC;
  if (Preserve)
    return preserveTimestamps(From, To);
  return {};
}


std::error_code copyPath(llvm::StringRef From, llvm::StringRef To, bool Force,
                         bool Recursive, bool Preserve,
                         llvm::SmallVectorImpl<llvm::sys::fs::UniqueID> &Visited) {
  llvm::sys::fs::file_status Status;
  if (std::error_code EC =
          llvm::sys::fs::status(From, Status, /*follow=*/false))
    return EC;

  // Symlink-to-directory must be followed when -r is set; otherwise
  // copy_file() hits EISDIR. Regular symlink-to-file still goes through
  // copy_file (content follow) so hosts without symlink create privileges
  // still get a usable tree copy.
  if (Status.type() == llvm::sys::fs::file_type::symlink_file) {
    llvm::sys::fs::file_status Target;
    if (std::error_code EC =
            llvm::sys::fs::status(From, Target, /*follow=*/true))
      return EC;
    if (Target.type() == llvm::sys::fs::file_type::directory_file) {
      if (!Recursive)
        return std::make_error_code(std::errc::is_a_directory);
      Status = Target;
    } else {
      return copyRegularFile(From, To, Force, Preserve);
    }
  }

  if (Status.type() == llvm::sys::fs::file_type::directory_file) {
    if (!Recursive)
      return std::make_error_code(std::errc::is_a_directory);
    // Guard against `cp -r dir dir/nested` / self-copies that would recurse
    // forever while walking the destination tree being created under From.
    if (isSameOrNestedPath(From, To))
      return std::make_error_code(std::errc::invalid_argument);
    // Match BSD/GNU cp cycle detection for trees that contain directory
    // symlinks back into an ancestor (e.g. `dir/up -> ..`).
    const llvm::sys::fs::UniqueID Id = Status.getUniqueID();
    if (llvm::is_contained(Visited, Id))
      return std::make_error_code(std::errc::invalid_argument);
    Visited.push_back(Id);
    if (std::error_code EC = llvm::sys::fs::create_directories(To)) {
      if (!(EC == llvm::errc::file_exists && isDirectory(To))) {
        Visited.pop_back();
        return EC;
      }
    }
    std::error_code IterEC;
    for (llvm::sys::fs::directory_iterator It(From, IterEC), End;
         !IterEC && It != End; It.increment(IterEC)) {
      llvm::StringRef Entry = It->path();
      llvm::SmallString<256> Dest(To);
      llvm::sys::path::append(Dest, llvm::sys::path::filename(Entry));
      if (std::error_code EC =
              copyPath(Entry, Dest, Force, Recursive, Preserve, Visited)) {
        Visited.pop_back();
        return EC;
      }
    }
    Visited.pop_back();
    if (IterEC)
      return IterEC;
    if (Preserve)
      return preserveTimestamps(From, To);
    return {};
  }

  return copyRegularFile(From, To, Force, Preserve);
}

std::error_code copyPath(llvm::StringRef From, llvm::StringRef To, bool Force,
                         bool Recursive, bool Preserve = false) {
  llvm::SmallVector<llvm::sys::fs::UniqueID, 8> Visited;
  return copyPath(From, To, Force, Recursive, Preserve, Visited);
}


std::string joinDest(llvm::StringRef DestDir, llvm::StringRef Src) {
  llvm::SmallString<256> Out(DestDir);
  llvm::sys::path::append(Out, llvm::sys::path::filename(Src));
  return std::string(Out);
}


bool tryExecuteCp(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  bool Force = false;
  bool Recursive = false;
  bool Preserve = false;
  bool NoClobber = false;
  bool Update = false;
  llvm::SmallVector<Token, 8> Operands;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "--") {
      for (size_t J = I + 1; J < Argv.size(); ++J)
        Operands.push_back(Argv[J]);
      break;
    }
    if (Arg.starts_with("-") && Arg.size() > 1) {
      if (!applyCpFlagCluster(Arg, Force, Recursive, Preserve, NoClobber,
                              Update))
        return false;
      continue;
    }
    Operands.push_back(Argv[I]);
  }
  if (Operands.size() < 2) {
    llvm::errs() << "neverc make: cp: missing file operand\n";
    ExitCode = 1;
    return true;
  }
  // stdin/stdout placeholders — builtins do not claim those forms.
  if (hasStdinDashOperand(Operands))
    return false;

  const Token &DestTok = Operands.back();
  if (DestTok.Quoted == false && hasGlobMeta(DestTok.Text)) {
    // Ambiguous destination globs are left to the shell.
    return false;
  }
  const std::string Dest = DestTok.Text;
  if (Dest.empty()) {
    llvm::errs() << "neverc make: cp: cannot create empty destination\n";
    ExitCode = 1;
    return true;
  }
  const bool DestIsDir = isDirectory(Dest);
  const llvm::SmallVector<std::string, 8> Sources =
      expandOperands(llvm::ArrayRef<Token>(Operands).drop_back());
  if (Sources.empty()) {
    llvm::errs() << "neverc make: cp: missing file operand\n";
    ExitCode = 1;
    return true;
  }
  // POSIX: multiple sources (including via glob expansion) require a directory
  // destination; otherwise the last write would silently clobber earlier ones.
  if (Sources.size() > 1 && !DestIsDir) {
    llvm::errs() << "neverc make: cp: target '" << Dest
                 << "' is not a directory\n";
    ExitCode = 1;
    return true;
  }

  bool Failed = false;
  for (const std::string &Src : Sources) {
    if (Src.empty()) {
      llvm::errs() << "neverc make: cp: cannot stat empty operand\n";
      Failed = true;
      continue;
    }
    const std::string To = DestIsDir ? joinDest(Dest, Src) : Dest;
    if (pathsEquivalent(Src, To)) {
      llvm::errs() << "neverc make: cp: '" << Src << "' and '" << To
                   << "' are the same file\n";
      Failed = true;
      continue;
    }
    // GNU cp -n: skip existing destinations without error.
    if (NoClobber && llvm::sys::fs::exists(To))
      continue;
    // GNU cp -u: skip when destination exists and is not older than source.
    if (Update && llvm::sys::fs::exists(To)) {
      const int64_t SrcTime = platform::getFileTimestamp(Src);
      const int64_t DstTime = platform::getFileTimestamp(To);
      if (SrcTime >= 0 && DstTime >= 0 && SrcTime <= DstTime)
        continue;
    }
    if (std::error_code EC = copyPath(Src, To, Force, Recursive, Preserve)) {
      llvm::errs() << "neverc make: cp: " << Src << ": " << EC.message()
                   << "\n";
      Failed = true;
    }
  }
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteMv(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  bool Force = false;
  bool NoClobber = false;
  llvm::SmallVector<Token, 8> Operands;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "--") {
      for (size_t J = I + 1; J < Argv.size(); ++J)
        Operands.push_back(Argv[J]);
      break;
    }
    if (Arg.starts_with("-") && Arg.size() > 1) {
      if (!applyMvFlagCluster(Arg, Force, NoClobber))
        return false;
      continue;
    }
    Operands.push_back(Argv[I]);
  }
  if (Operands.size() < 2) {
    llvm::errs() << "neverc make: mv: missing file operand\n";
    ExitCode = 1;
    return true;
  }
  if (hasStdinDashOperand(Operands))
    return false;

  const Token &DestTok = Operands.back();
  if (!DestTok.Quoted && hasGlobMeta(DestTok.Text))
    return false;
  const std::string Dest = DestTok.Text;
  if (Dest.empty()) {
    llvm::errs() << "neverc make: mv: cannot move to empty destination\n";
    ExitCode = 1;
    return true;
  }
  const bool DestIsDir = isDirectory(Dest);
  const llvm::SmallVector<std::string, 8> Sources =
      expandOperands(llvm::ArrayRef<Token>(Operands).drop_back());
  if (Sources.empty()) {
    llvm::errs() << "neverc make: mv: missing file operand\n";
    ExitCode = 1;
    return true;
  }
  if (Sources.size() > 1 && !DestIsDir) {
    llvm::errs() << "neverc make: mv: target '" << Dest
                 << "' is not a directory\n";
    ExitCode = 1;
    return true;
  }

  bool Failed = false;
  for (const std::string &Src : Sources) {
    if (Src.empty()) {
      llvm::errs() << "neverc make: mv: cannot stat empty operand\n";
      Failed = true;
      continue;
    }
    const std::string To = DestIsDir ? joinDest(Dest, Src) : Dest;
    // POSIX: same-file mv is a successful no-op.
    if (pathsEquivalent(Src, To))
      continue;
    // GNU mv -n: skip existing destinations without error.
    if (NoClobber && llvm::sys::fs::exists(To))
      continue;
    // Prefer rename first so a failed move cannot destroy an existing dest
    // under `-f`. Only remove+retry when the destination blocks replacement
    // (common on Windows; rare on POSIX where rename replaces files).
    std::error_code EC = llvm::sys::fs::rename(Src, To);
    if (EC && Force && llvm::sys::fs::exists(To) && !isDirectory(To)) {
      if (std::error_code RmEC = llvm::sys::fs::remove(To)) {
        llvm::errs() << "neverc make: mv: " << To << ": " << RmEC.message()
                     << "\n";
        Failed = true;
        continue;
      }
      EC = llvm::sys::fs::rename(Src, To);
    }
    if (EC) {
      // Only EXDEV is safely recoverable via copy+remove. Other rename
      // failures (permission, busy, etc.) must not silently duplicate trees.
      if (EC != std::errc::cross_device_link) {
        llvm::errs() << "neverc make: mv: " << Src << ": " << EC.message()
                     << "\n";
        Failed = true;
        continue;
      }
      if (std::error_code CopyEC =
              copyPath(Src, To, /*Force=*/true, /*Recursive=*/true)) {
        llvm::errs() << "neverc make: mv: " << Src << ": " << CopyEC.message()
                     << "\n";
        Failed = true;
        continue;
      }
      std::string Error;
      if (!removePath(Src, /*Force=*/false, /*Recursive=*/true, Error)) {
        llvm::errs() << "neverc make: mv: " << Error << "\n";
        Failed = true;
      }
      continue;
    }
  }
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteLn(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Unquoted globs must expand via the shell so multi-match arity matches sh.
  if (hasUnquotedGlob(Argv.drop_front()))
    return false;
  bool Force = false;
  bool Symbolic = false;
  bool NoDereference = false;
  llvm::SmallVector<llvm::StringRef, 4> Operands;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "--") {
      for (size_t J = I + 1; J < Argv.size(); ++J)
        Operands.push_back(Argv[J].Text);
      break;
    }
    if (Arg.starts_with("-") && Arg.size() > 1) {
      if (!applyLnFlagCluster(Arg, Force, Symbolic, NoDereference))
        return false;
      continue;
    }
    Operands.push_back(Arg);
  }
  if (Operands.size() != 2) {
    llvm::errs() << "neverc make: ln: expected SOURCE and TARGET\n";
    ExitCode = 1;
    return true;
  }

#ifdef _WIN32
  // LLVM create_link() creates hard links on Windows; do not pretend that is
  // `ln -s`. Leave symbolic links to the host tool / developer mode.
  if (Symbolic)
    return false;
#endif

  const llvm::StringRef Target = Operands[0];
  std::string LinkPath = Operands[1].str();
  // POSIX: if the final operand names an existing directory, create the link
  // inside it using the source basename. With -n, a symlink-to-directory is
  // not treated as a directory (GNU --no-dereference).
  if (isDirectory(LinkPath, /*Follow=*/!NoDereference))
    LinkPath = joinDest(LinkPath, Target);

  if (Force && llvm::sys::fs::exists(LinkPath)) {
    if (std::error_code EC =
            llvm::sys::fs::remove(LinkPath, /*IgnoreNonExisting=*/true)) {
      llvm::errs() << "neverc make: ln: " << LinkPath << ": " << EC.message()
                   << "\n";
      ExitCode = 1;
      return true;
    }
  }

  // LLVM create_*_link(to, from): `from` is created and points at `to`.
  // On Unix, create_link is a symlink; create_hard_link is a hard link.
  std::error_code EC;
  if (Symbolic)
    EC = llvm::sys::fs::create_link(Target, LinkPath);
  else
    EC = llvm::sys::fs::create_hard_link(Target, LinkPath);
  if (EC) {
    llvm::errs() << "neverc make: ln: " << EC.message() << "\n";
    ExitCode = 1;
    return true;
  }
  ExitCode = 0;
  return true;
}


bool tryExecuteLink(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // POSIX link(1): hard-link FILE1 to FILE2. No flags / no directory target form.
  if (hasUnquotedGlob(Argv.drop_front()))
    return false;
  if (Argv.size() != 3) {
    llvm::errs() << "neverc make: link: expected FILE1 and FILE2\n";
    ExitCode = 1;
    return true;
  }
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-") &&
        Argv[I].Text != "-")
      return false;
  }
  if (std::error_code EC =
          llvm::sys::fs::create_hard_link(Argv[1].Text, Argv[2].Text)) {
    llvm::errs() << "neverc make: link: " << EC.message() << "\n";
    ExitCode = 1;
    return true;
  }
  ExitCode = 0;
  return true;
}


bool parseOctalMode(llvm::StringRef Text, unsigned &Mode) {
  if (Text.empty())
    return false;
  unsigned Value = 0;
  for (char C : Text) {
    if (C < '0' || C > '7')
      return false;
    Value = (Value << 3) + static_cast<unsigned>(C - '0');
    if (Value > 07777)
      return false;
  }
  Mode = Value;
  return true;
}


std::error_code chmodPath(llvm::StringRef Path, unsigned Mode) {
#ifdef _WIN32
  (void)Path;
  (void)Mode;
  // Windows permission model does not map cleanly onto POSIX octal modes.
  return std::make_error_code(std::errc::operation_not_supported);
#else
  if (::chmod(Path.str().c_str(), static_cast<mode_t>(Mode)) != 0)
    return std::error_code(errno, std::generic_category());
  return {};
#endif
}


std::error_code chmodRecursive(llvm::StringRef Path, unsigned Mode) {
  // Match common chmod -R behavior: do not walk through symlinked directories,
  // and do not change modes of symlink entries discovered during traversal.
  llvm::sys::fs::file_status Status;
  if (std::error_code EC =
          llvm::sys::fs::status(Path, Status, /*follow=*/false))
    return EC;
  if (Status.type() == llvm::sys::fs::file_type::symlink_file)
    return {};
  if (std::error_code EC = chmodPath(Path, Mode))
    return EC;
  if (Status.type() != llvm::sys::fs::file_type::directory_file)
    return {};

  std::error_code IterEC;
  for (llvm::sys::fs::directory_iterator It(Path, IterEC), End;
       !IterEC && It != End; It.increment(IterEC)) {
    if (std::error_code EC = chmodRecursive(It->path(), Mode))
      return EC;
  }
  return IterEC;
}


bool tryExecuteChmod(llvm::ArrayRef<Token> Argv, int &ExitCode) {
#ifdef _WIN32
  (void)Argv;
  (void)ExitCode;
  // Keep host chmod/icacls behavior on Windows.
  return false;
#else
  bool Recursive = false;
  llvm::SmallVector<Token, 4> Operands;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "--") {
      for (size_t J = I + 1; J < Argv.size(); ++J)
        Operands.push_back(Argv[J]);
      break;
    }
    if (Arg.starts_with("-") && Arg.size() > 1) {
      if (!applyChmodFlagCluster(Arg, Recursive))
        return false;
      continue;
    }
    Operands.push_back(Argv[I]);
  }
  if (Operands.size() < 2) {
    llvm::errs() << "neverc make: chmod: missing operand\n";
    ExitCode = 1;
    return true;
  }

  unsigned Mode = 0;
  if (!parseOctalMode(Operands[0].Text, Mode))
    return false; // symbolic modes like u+x stay on the host tool

  bool Failed = false;
  for (const std::string &Path :
       expandOperands(llvm::ArrayRef<Token>(Operands).drop_front())) {
    std::error_code EC;
    if (Recursive) {
      // GNU chmod: symlinks listed on the command line are followed; symlinks
      // discovered during traversal are ignored (handled in chmodRecursive).
      llvm::sys::fs::file_status Status;
      if ((EC = llvm::sys::fs::status(Path, Status, /*follow=*/false))) {
        // report below
      } else if (Status.type() == llvm::sys::fs::file_type::symlink_file) {
        llvm::SmallString<256> Real;
        if (!(EC = llvm::sys::fs::real_path(Path, Real)))
          EC = chmodRecursive(Real, Mode);
      } else {
        EC = chmodRecursive(Path, Mode);
      }
    } else {
      EC = chmodPath(Path, Mode);
    }
    if (EC) {
      llvm::errs() << "neverc make: chmod: " << Path << ": " << EC.message()
                   << "\n";
      Failed = true;
    }
  }
  ExitCode = Failed ? 1 : 0;
  return true;
#endif
}


bool applyInstallFlagCluster(llvm::StringRef Cluster, bool &Directory,
                             bool &CreateLeading, bool &HaveMode,
                             unsigned &Mode) {
  // Accept GNU-style clusters used heavily in Makefiles: `-d`, `-cp`, `-dm755`,
  // `-Dm755`.
  if (Cluster.size() < 2 || Cluster[0] != '-' || Cluster.starts_with("--"))
    return false;
  for (size_t I = 1; I < Cluster.size(); ++I) {
    switch (Cluster[I]) {
    case 'd':
      Directory = true;
      break;
    case 'D':
      CreateLeading = true;
      break;
    case 'c':
    case 'p':
      break; // historical no-ops
    case 'm': {
      llvm::StringRef ModeText = Cluster.drop_front(I + 1);
      if (ModeText.empty() || !parseOctalMode(ModeText, Mode))
        return false;
      HaveMode = true;
      return true; // mode consumes the rest of the cluster
    }
    default:
      return false; // -o/-g/-v stay on the host install
    }
  }
  return true;
}


bool tryExecuteInstall(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  bool Directory = false;
  bool CreateLeading = false;
  bool HaveMode = false;
  unsigned Mode = 0755;
  bool HaveTargetDir = false;
  Token TargetDirTok;
  llvm::SmallVector<Token, 4> Operands;

  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "--") {
      for (size_t J = I + 1; J < Argv.size(); ++J)
        Operands.push_back(Argv[J]);
      break;
    }
    if (Arg == "-m") {
      if (I + 1 >= Argv.size()) {
        llvm::errs() << "neverc make: install: option requires an argument -- "
                        "m\n";
        ExitCode = 1;
        return true;
      }
      if (!parseOctalMode(Argv[++I].Text, Mode))
        return false;
      HaveMode = true;
      continue;
    }
    if (Arg.starts_with("-m") && Arg.size() > 2) {
      if (!parseOctalMode(Arg.drop_front(2), Mode))
        return false;
      HaveMode = true;
      continue;
    }
    if (Arg == "-t") {
      if (I + 1 >= Argv.size()) {
        llvm::errs() << "neverc make: install: option requires an argument -- "
                        "t\n";
        ExitCode = 1;
        return true;
      }
      TargetDirTok = Argv[++I];
      HaveTargetDir = true;
      continue;
    }
    if (Arg.starts_with("-t") && Arg.size() > 2) {
      TargetDirTok = Token{Arg.drop_front(2).str(), Argv[I].Quoted};
      HaveTargetDir = true;
      continue;
    }
    if (Arg == "-D") {
      CreateLeading = true;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1) {
      if (!applyInstallFlagCluster(Arg, Directory, CreateLeading, HaveMode,
                                   Mode))
        return false;
      continue;
    }
    Operands.push_back(Argv[I]);
  }

  if (Operands.empty()) {
    llvm::errs() << "neverc make: install: missing "
                 << (Directory ? "directory" : "file") << " operand\n";
    ExitCode = 1;
    return true;
  }
  if (hasStdinDashOperand(Operands) ||
      (HaveTargetDir && TargetDirTok.Text == "-"))
    return false;
  if (Directory && HaveTargetDir)
    return false; // -d and -t together are not a portable recipe form we claim

  bool Failed = false;
  if (Directory) {
    // `install -d [-m MODE] DIR...` (mkdir -p + optional chmod).
    for (const std::string &Dir : expandOperands(Operands)) {
      if (std::error_code EC = llvm::sys::fs::create_directories(
              Dir, /*IgnoreExisting=*/true)) {
        if (!(EC == llvm::errc::file_exists && isDirectory(Dir))) {
          llvm::errs() << "neverc make: install: " << Dir << ": "
                       << EC.message() << "\n";
          Failed = true;
          continue;
        }
      }
#ifndef _WIN32
      if (HaveMode) {
        if (std::error_code EC = chmodPath(Dir, Mode)) {
          llvm::errs() << "neverc make: install: " << Dir << ": "
                       << EC.message() << "\n";
          Failed = true;
        }
      }
#else
      (void)HaveMode;
      (void)Mode;
#endif
    }
    ExitCode = Failed ? 1 : 0;
    return true;
  }

  // File form: `install [-m MODE] SOURCE DEST`, `SOURCE... DEST_DIR`, or
  // `install -t DIR SOURCE...`.
  std::string Dest;
  bool DestIsDir = false;
  llvm::SmallVector<std::string, 8> Sources;
  if (HaveTargetDir) {
    if (TargetDirTok.Text.empty() ||
        (!TargetDirTok.Quoted && hasGlobMeta(TargetDirTok.Text)))
      return false;
    Dest = TargetDirTok.Text;
    // GNU install -Dt / -D -t: create the target directory when missing.
    // Plain -t still requires an existing directory.
    if (!isDirectory(Dest)) {
      if (!CreateLeading) {
        llvm::errs() << "neverc make: install: target '" << Dest
                     << "' is not a directory\n";
        ExitCode = 1;
        return true;
      }
      if (std::error_code EC = llvm::sys::fs::create_directories(
              Dest, /*IgnoreExisting=*/true)) {
        if (!(EC == llvm::errc::file_exists && isDirectory(Dest))) {
          llvm::errs() << "neverc make: install: " << Dest << ": "
                       << EC.message() << "\n";
          ExitCode = 1;
          return true;
        }
      }
    }
    DestIsDir = true;
    Sources = expandOperands(Operands);
  } else {
    if (Operands.size() < 2) {
      llvm::errs() << "neverc make: install: missing destination operand\n";
      ExitCode = 1;
      return true;
    }
    const Token &DestTok = Operands.back();
    if (!DestTok.Quoted && hasGlobMeta(DestTok.Text))
      return false;
    Dest = DestTok.Text;
    DestIsDir = isDirectory(Dest);
    Sources = expandOperands(llvm::ArrayRef<Token>(Operands).drop_back());
  }
  if (Sources.empty()) {
    llvm::errs() << "neverc make: install: missing file operand\n";
    ExitCode = 1;
    return true;
  }
  if (Sources.size() > 1 && !DestIsDir) {
    llvm::errs() << "neverc make: install: target '" << Dest
                 << "' is not a directory\n";
    ExitCode = 1;
    return true;
  }

  for (const std::string &Src : Sources) {
    if (isDirectory(Src, /*Follow=*/false)) {
      llvm::errs() << "neverc make: install: omitting directory '" << Src
                   << "'\n";
      Failed = true;
      continue;
    }
    const std::string To = DestIsDir ? joinDest(Dest, Src) : Dest;
    if (pathsEquivalent(Src, To)) {
      llvm::errs() << "neverc make: install: '" << Src << "' and '" << To
                   << "' are the same file\n";
      Failed = true;
      continue;
    }
    if (CreateLeading && !DestIsDir) {
      llvm::StringRef Parent = llvm::sys::path::parent_path(To);
      if (!Parent.empty()) {
        if (std::error_code EC = llvm::sys::fs::create_directories(
                Parent, /*IgnoreExisting=*/true)) {
          if (!(EC == llvm::errc::file_exists && isDirectory(Parent))) {
            llvm::errs() << "neverc make: install: " << Parent << ": "
                         << EC.message() << "\n";
            Failed = true;
            continue;
          }
        }
      }
    }
    if (std::error_code EC =
            copyRegularFile(Src, To, /*Force=*/true, /*Preserve=*/false)) {
      llvm::errs() << "neverc make: install: " << Src << ": " << EC.message()
                   << "\n";
      Failed = true;
      continue;
    }
#ifndef _WIN32
    if (std::error_code EC = chmodPath(To, HaveMode ? Mode : 0755)) {
      llvm::errs() << "neverc make: install: " << To << ": " << EC.message()
                   << "\n";
      Failed = true;
    }
#endif
  }
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteMktemp(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  bool Directory = false;
  llvm::StringRef Template = "tmp.XXXXXXXX";
  bool HaveTemplate = false;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "--") {
      if (I + 1 >= Argv.size()) {
        llvm::errs() << "neverc make: mktemp: too few arguments\n";
        ExitCode = 1;
        return true;
      }
      if (HaveTemplate || I + 1 != Argv.size() - 1) {
        // GNU mktemp accepts at most one template.
        llvm::errs() << "neverc make: mktemp: too many templates\n";
        ExitCode = 1;
        return true;
      }
      Template = Argv[I + 1].Text;
      HaveTemplate = true;
      break;
    }
    if (Arg == "-d") {
      Directory = true;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    if (HaveTemplate) {
      llvm::errs() << "neverc make: mktemp: too many templates\n";
      ExitCode = 1;
      return true;
    }
    Template = Arg;
    HaveTemplate = true;
  }

  llvm::SmallString<256> Result;
  std::error_code EC;
  if (Directory) {
    // LLVM createUniqueDirectory takes a prefix, not a GNU XXXXXX template.
    llvm::SmallString<128> Prefix(HaveTemplate ? Template : llvm::StringRef("tmp"));
    while (!Prefix.empty() && Prefix.back() == 'X')
      Prefix.pop_back();
    if (!Prefix.empty() && (Prefix.back() == '.' || Prefix.back() == '_' ||
                            Prefix.back() == '-'))
      Prefix.pop_back();
    if (Prefix.empty())
      Prefix = "tmp";
    EC = llvm::sys::fs::createUniqueDirectory(Prefix, Result);
  } else {
    // GNU mktemp only replaces a trailing run of X's (at least 3). LLVM
    // createUniqueFile uses '%' placeholders instead.
    std::string Model = Template.str();
    size_t End = Model.size();
    size_t Start = End;
    while (Start > 0 && Model[Start - 1] == 'X')
      --Start;
    const size_t XCount = End - Start;
    if (XCount == 0) {
      Model += ".%%%%%%";
    } else if (XCount < 3) {
      llvm::errs() << "neverc make: mktemp: too few X's in template '"
                   << Template << "'\n";
      ExitCode = 1;
      return true;
    } else {
      for (size_t I = Start; I < End; ++I)
        Model[I] = '%';
    }
    int FD = -1;
    EC = llvm::sys::fs::createUniqueFile(Model, FD, Result);
    if (!EC) {
#ifdef _WIN32
      ::_close(FD);
#else
      ::close(FD);
#endif
    }
  }
  if (EC) {
    llvm::errs() << "neverc make: mktemp: " << EC.message() << "\n";
    ExitCode = 1;
    return true;
  }
  llvm::outs() << Result << '\n';
  llvm::outs().flush();
  ExitCode = 0;
  return true;
}


std::error_code removeEmptyDirectory(llvm::StringRef Path) {
  // Critical: llvm::sys::fs::remove() also deletes regular files. POSIX rmdir
  // must fail with ENOTDIR on non-directories (including symlinks).
  llvm::sys::fs::file_status Status;
  if (std::error_code EC =
          llvm::sys::fs::status(Path, Status, /*follow=*/false))
    return EC;
  if (Status.type() != llvm::sys::fs::file_type::directory_file)
    return std::make_error_code(std::errc::not_a_directory);
  return llvm::sys::fs::remove(Path);
}


bool tryExecuteRmdir(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  bool Parents = false;
  llvm::SmallVector<Token, 4> Dirs;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if ((Arg == "-p" || Arg == "--parents")) {
      Parents = true;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Dirs.push_back(Argv[I]);
  }
  if (Dirs.empty()) {
    llvm::errs() << "neverc make: rmdir: missing operand\n";
    ExitCode = 1;
    return true;
  }

  bool Failed = false;
  for (const std::string &Dir : expandOperands(Dirs)) {
    if (Parents) {
      // `rmdir -p a/b/c` ≈ `rmdir a/b/c a/b a`; a non-empty parent is an error.
      llvm::SmallString<256> Cur(Dir);
      while (true) {
        if (std::error_code EC = removeEmptyDirectory(Cur)) {
          llvm::errs() << "neverc make: rmdir: " << Cur << ": " << EC.message()
                       << "\n";
          Failed = true;
          break;
        }
        llvm::StringRef Parent = llvm::sys::path::parent_path(Cur);
        if (Parent.empty() || Parent == Cur || Parent == "/" || Parent == "\\")
          break;
        Cur = Parent;
      }
      continue;
    }
    if (std::error_code EC = removeEmptyDirectory(Dir)) {
      llvm::errs() << "neverc make: rmdir: " << Dir << ": " << EC.message()
                   << "\n";
      Failed = true;
    }
  }
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool parseTruncateSize(llvm::StringRef Text, uint64_t &Bytes) {
  if (Text.empty())
    return false;
  // GNU relative sizes (`+N` / `-N`) and signed forms stay on the host tool.
  // Also reject a leading '+' that strtoull would otherwise accept as absolute.
  if (Text.starts_with("+") || Text.starts_with("-"))
    return false;
  uint64_t Multiplier = 1;
  llvm::StringRef Number = Text;
  switch (Text.back()) {
  case 'K':
  case 'k':
    Multiplier = 1024ULL;
    Number = Text.drop_back();
    break;
  case 'M':
  case 'm':
    Multiplier = 1024ULL * 1024ULL;
    Number = Text.drop_back();
    break;
  case 'G':
  case 'g':
    Multiplier = 1024ULL * 1024ULL * 1024ULL;
    Number = Text.drop_back();
    break;
  default:
    break;
  }
  if (Number.empty())
    return false;
  const std::string S = Number.str();
  char *End = nullptr;
  errno = 0;
  const unsigned long long Parsed = std::strtoull(S.c_str(), &End, 10);
  if (End == S.c_str() || (End && *End != '\0') || errno == ERANGE)
    return false;
  if (Parsed > std::numeric_limits<uint64_t>::max() / Multiplier)
    return false;
  Bytes = static_cast<uint64_t>(Parsed) * Multiplier;
  return true;
}


bool tryExecuteTruncate(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `truncate -s SIZE FILE...` (absolute sizes, optional K/M/G).
  bool HaveSize = false;
  uint64_t Size = 0;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "-s") {
      if (I + 1 >= Argv.size()) {
        llvm::errs()
            << "neverc make: truncate: option requires an argument -- s\n";
        ExitCode = 1;
        return true;
      }
      if (!parseTruncateSize(Argv[++I].Text, Size))
        return false;
      HaveSize = true;
      continue;
    }
    if (Arg.starts_with("-s") && Arg.size() > 2) {
      if (!parseTruncateSize(Arg.drop_front(2), Size))
        return false;
      HaveSize = true;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Files.push_back(Argv[I]);
  }
  if (!HaveSize || Files.empty())
    return false;

  bool Failed = false;
  for (const std::string &Path : expandOperands(Files)) {
    int FD = -1;
    std::error_code EC = llvm::sys::fs::openFileForReadWrite(
        Path, FD, llvm::sys::fs::CD_OpenAlways, llvm::sys::fs::OF_None);
    if (EC) {
      llvm::errs() << "neverc make: truncate: " << Path << ": " << EC.message()
                   << "\n";
      Failed = true;
      continue;
    }
    EC = llvm::sys::fs::resize_file(FD, Size);
#ifdef _WIN32
    ::_close(FD);
#else
    ::close(FD);
#endif
    if (EC) {
      llvm::errs() << "neverc make: truncate: " << Path << ": " << EC.message()
                   << "\n";
      Failed = true;
    }
  }
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteUnlink(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // POSIX unlink: remove a single non-directory file/symlink operand.
  if (hasUnquotedGlob(Argv.drop_front()))
    return false;
  if (Argv.size() != 2) {
    llvm::errs() << "neverc make: unlink: expected one operand\n";
    ExitCode = 1;
    return true;
  }
  if (llvm::StringRef(Argv[1].Text).starts_with("-") &&
      Argv[1].Text != "-")
    return false;

  const std::string &Path = Argv[1].Text;
  if (isDirectory(Path, /*Follow=*/false)) {
    llvm::errs() << "neverc make: unlink: " << Path << ": is a directory\n";
    ExitCode = 1;
    return true;
  }
  if (std::error_code EC =
          llvm::sys::fs::remove(Path, /*IgnoreNonExisting=*/false)) {
    llvm::errs() << "neverc make: unlink: " << Path << ": " << EC.message()
                 << "\n";
    ExitCode = 1;
    return true;
  }
  ExitCode = 0;
  return true;
}


#ifndef _WIN32
bool lookupUserGroup(llvm::StringRef Spec, uid_t &Uid, gid_t &Gid,
                     bool &HaveUid, bool &HaveGid) {
  HaveUid = false;
  HaveGid = false;
  if (Spec.empty())
    return false;

  llvm::StringRef User;
  llvm::StringRef Group;
  const size_t Colon = Spec.find_first_of(":.");
  if (Colon == llvm::StringRef::npos) {
    User = Spec;
  } else {
    User = Spec.take_front(Colon);
    Group = Spec.drop_front(Colon + 1);
  }

  auto parseId = [](llvm::StringRef Text, long &Value) {
    if (Text.empty())
      return false;
    const std::string S = Text.str();
    char *End = nullptr;
    errno = 0;
    const long Parsed = std::strtol(S.c_str(), &End, 10);
    if (End == S.c_str() || (End && *End != '\0') || errno == ERANGE ||
        Parsed < 0)
      return false;
    Value = Parsed;
    return true;
  };

  if (!User.empty()) {
    long Numeric = 0;
    if (parseId(User, Numeric)) {
      Uid = static_cast<uid_t>(Numeric);
      HaveUid = true;
    } else if (const passwd *Pw = ::getpwnam(User.str().c_str())) {
      Uid = Pw->pw_uid;
      HaveUid = true;
    } else {
      return false;
    }
  }
  if (!Group.empty()) {
    long Numeric = 0;
    if (parseId(Group, Numeric)) {
      Gid = static_cast<gid_t>(Numeric);
      HaveGid = true;
    } else if (const group *Gr = ::getgrnam(Group.str().c_str())) {
      Gid = Gr->gr_gid;
      HaveGid = true;
    } else {
      return false;
    }
  }
  return HaveUid || HaveGid;
}


std::error_code chownRecursive(llvm::StringRef Path, uid_t Uid, gid_t Gid,
                               bool HaveUid, bool HaveGid) {
  llvm::sys::fs::file_status Status;
  if (std::error_code EC =
          llvm::sys::fs::status(Path, Status, /*follow=*/false))
    return EC;
  // Do not walk through or mutate symlink entries discovered during -R.
  if (Status.type() == llvm::sys::fs::file_type::symlink_file)
    return {};
  if (::chown(Path.str().c_str(), HaveUid ? Uid : static_cast<uid_t>(-1),
              HaveGid ? Gid : static_cast<gid_t>(-1)) != 0)
    return std::error_code(errno, std::generic_category());
  if (Status.type() != llvm::sys::fs::file_type::directory_file)
    return {};
  std::error_code IterEC;
  for (llvm::sys::fs::directory_iterator It(Path, IterEC), End;
       !IterEC && It != End; It.increment(IterEC)) {
    if (std::error_code EC =
            chownRecursive(It->path(), Uid, Gid, HaveUid, HaveGid))
      return EC;
  }
  return IterEC;
}
#endif


bool tryExecuteChown(llvm::ArrayRef<Token> Argv, int &ExitCode) {
#ifdef _WIN32
  (void)Argv;
  (void)ExitCode;
  return false;
#else
  // Portable subset: `chown [-R] USER[:GROUP] FILE...` (also USER.GROUP).
  bool Recursive = false;
  llvm::SmallVector<Token, 4> Operands;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "-R" || Arg == "-r") {
      Recursive = true;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false; // -h/-H/-L/-P stay on the host tool
    Operands.push_back(Argv[I]);
  }
  if (Operands.size() < 2 ||
      hasStdinDashOperand(llvm::ArrayRef<Token>(Operands).drop_front()))
    return false;

  uid_t Uid = static_cast<uid_t>(-1);
  gid_t Gid = static_cast<gid_t>(-1);
  bool HaveUid = false;
  bool HaveGid = false;
  // Unknown / unresolvable names (NIS, directory services, etc.) stay on the
  // host tool instead of inventing a hard failure.
  // GNU `chown USER:` (empty group) also sets the login group — we do not
  // implement that, so leave the form to the host tool.
  {
    const llvm::StringRef Spec = Operands[0].Text;
    const size_t Sep = Spec.find_first_of(":.");
    if (Sep != llvm::StringRef::npos && !Spec.take_front(Sep).empty() &&
        Spec.drop_front(Sep + 1).empty())
      return false;
  }
  if (!lookupUserGroup(Operands[0].Text, Uid, Gid, HaveUid, HaveGid))
    return false;

  bool Failed = false;
  for (const std::string &Path :
       expandOperands(llvm::ArrayRef<Token>(Operands).drop_front())) {
    std::error_code EC;
    if (Recursive) {
      llvm::sys::fs::file_status Status;
      if ((EC = llvm::sys::fs::status(Path, Status, /*follow=*/false))) {
        // report below
      } else if (Status.type() == llvm::sys::fs::file_type::symlink_file) {
        llvm::SmallString<256> Real;
        if (!(EC = llvm::sys::fs::real_path(Path, Real)))
          EC = chownRecursive(Real, Uid, Gid, HaveUid, HaveGid);
      } else {
        EC = chownRecursive(Path, Uid, Gid, HaveUid, HaveGid);
      }
    } else if (::chown(Path.c_str(), HaveUid ? Uid : static_cast<uid_t>(-1),
                       HaveGid ? Gid : static_cast<gid_t>(-1)) != 0) {
      EC = std::error_code(errno, std::generic_category());
    }
    if (EC) {
      llvm::errs() << "neverc make: chown: " << Path << ": " << EC.message()
                   << "\n";
      Failed = true;
    }
  }
  ExitCode = Failed ? 1 : 0;
  return true;
#endif
}


bool tryExecuteChgrp(llvm::ArrayRef<Token> Argv, int &ExitCode) {
#ifdef _WIN32
  (void)Argv;
  (void)ExitCode;
  return false;
#else
  // Portable subset: `chgrp [-R] GROUP FILE...`.
  bool Recursive = false;
  llvm::SmallVector<Token, 4> Operands;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "-R" || Arg == "-r") {
      Recursive = true;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Operands.push_back(Argv[I]);
  }
  if (Operands.size() < 2 ||
      hasStdinDashOperand(llvm::ArrayRef<Token>(Operands).drop_front()))
    return false;

  llvm::SmallVector<Token, 8> AsChown;
  AsChown.push_back(Token{"chown", /*Quoted=*/false});
  if (Recursive)
    AsChown.push_back(Token{"-R", /*Quoted=*/false});
  AsChown.push_back(Token{(":" + Operands[0].Text), Operands[0].Quoted});
  for (size_t I = 1; I < Operands.size(); ++I)
    AsChown.push_back(Operands[I]);
  return tryExecuteChown(AsChown, ExitCode);
#endif
}


bool tryExecuteDd(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `dd if=SRC of=DST [bs=N] [count=N]` (absolute sizes).
  // conv=/seek=/skip=/status= and stdin/stdout forms stay on the host tool.
  std::string IfPath;
  std::string OfPath;
  uint64_t BlockSize = 512;
  bool HaveCount = false;
  uint64_t Count = 0;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg.starts_with("if=")) {
      IfPath = Arg.drop_front(3).str();
      continue;
    }
    if (Arg.starts_with("of=")) {
      OfPath = Arg.drop_front(3).str();
      continue;
    }
    if (Arg.starts_with("bs=")) {
      if (!parseTruncateSize(Arg.drop_front(3), BlockSize) || BlockSize == 0)
        return false;
      continue;
    }
    if (Arg.starts_with("count=")) {
      long Value = 0;
      if (!parseNonNegLong(Arg.drop_front(6), Value))
        return false;
      HaveCount = true;
      Count = static_cast<uint64_t>(Value);
      continue;
    }
    return false;
  }
  if (IfPath.empty() || OfPath.empty() || IfPath == "-" || OfPath == "-")
    return false;

  auto Buf = llvm::MemoryBuffer::getFile(IfPath);
  if (!Buf) {
    llvm::errs() << "neverc make: dd: " << IfPath << ": "
                 << Buf.getError().message() << "\n";
    ExitCode = 1;
    return true;
  }
  llvm::StringRef Data = (*Buf)->getBuffer();
  uint64_t Bytes = Data.size();
  if (HaveCount) {
    if (BlockSize != 0 && Count > std::numeric_limits<uint64_t>::max() / BlockSize) {
      llvm::errs() << "neverc make: dd: count*bs overflow\n";
      ExitCode = 1;
      return true;
    }
    const uint64_t Limit = Count * BlockSize;
    if (Limit < Bytes)
      Bytes = Limit;
  }

  int FD = -1;
  std::error_code EC = llvm::sys::fs::openFileForWrite(
      OfPath, FD, llvm::sys::fs::CD_CreateAlways, llvm::sys::fs::OF_None);
  if (EC) {
    llvm::errs() << "neverc make: dd: " << OfPath << ": " << EC.message()
                 << "\n";
    ExitCode = 1;
    return true;
  }
  const char *Ptr = Data.data();
  uint64_t Remaining = Bytes;
  while (Remaining) {
    const uint64_t Chunk =
        Remaining > static_cast<uint64_t>(BlockSize) ? BlockSize : Remaining;
#ifdef _WIN32
    const int Written =
        ::_write(FD, Ptr, static_cast<unsigned int>(Chunk));
#else
    const ssize_t Written = ::write(FD, Ptr, static_cast<size_t>(Chunk));
#endif
    if (Written < 0 || static_cast<uint64_t>(Written) != Chunk) {
#ifdef _WIN32
      ::_close(FD);
#else
      ::close(FD);
#endif
      llvm::errs() << "neverc make: dd: " << OfPath << ": write failed\n";
      ExitCode = 1;
      return true;
    }
    Ptr += Chunk;
    Remaining -= Chunk;
  }
#ifdef _WIN32
  ::_close(FD);
#else
  ::close(FD);
#endif
  ExitCode = 0;
  return true;
}


bool tryExecuteMkfifo(llvm::ArrayRef<Token> Argv, int &ExitCode) {
#ifdef _WIN32
  (void)Argv;
  (void)ExitCode;
  // Named pipes on Windows are not POSIX fifos; leave to the host tool.
  return false;
#else
  // Portable subset: `mkfifo [-m MODE] NAME...` (no `--mode=` long forms).
  bool HaveMode = false;
  unsigned Mode = 0666;
  llvm::SmallVector<Token, 4> Names;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "--") {
      for (size_t J = I + 1; J < Argv.size(); ++J)
        Names.push_back(Argv[J]);
      break;
    }
    if (Arg == "-m") {
      if (I + 1 >= Argv.size()) {
        llvm::errs()
            << "neverc make: mkfifo: option requires an argument -- m\n";
        ExitCode = 1;
        return true;
      }
      if (!parseOctalMode(Argv[++I].Text, Mode))
        return false;
      HaveMode = true;
      continue;
    }
    if (Arg.starts_with("-m") && Arg.size() > 2) {
      if (!parseOctalMode(Arg.drop_front(2), Mode))
        return false;
      HaveMode = true;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Names.push_back(Argv[I]);
  }
  if (Names.empty()) {
    llvm::errs() << "neverc make: mkfifo: missing operand\n";
    ExitCode = 1;
    return true;
  }

  bool Failed = false;
  for (const std::string &Name : expandOperands(Names)) {
    if (Name.empty()) {
      llvm::errs() << "neverc make: mkfifo: cannot create fifo '': "
                      "No such file or directory\n";
      Failed = true;
      continue;
    }
    if (::mkfifo(Name.c_str(), static_cast<mode_t>(HaveMode ? Mode : 0666)) !=
        0) {
      llvm::errs() << "neverc make: mkfifo: " << Name << ": "
                   << std::strerror(errno) << "\n";
      Failed = true;
      continue;
    }
    if (HaveMode) {
      // mkfifo mode is masked by umask; apply -m explicitly like GNU mkfifo.
      if (std::error_code EC = chmodPath(Name, Mode)) {
        llvm::errs() << "neverc make: mkfifo: " << Name << ": " << EC.message()
                     << "\n";
        Failed = true;
      }
    }
  }
  ExitCode = Failed ? 1 : 0;
  return true;
#endif
}


} // namespace internal
} // namespace builtins
} // namespace build
} // namespace neverc
