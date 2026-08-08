#include "Platform/Builtins/Internal.h"

#include "neverc/Build/Platform.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <sys/statvfs.h>
#include <unistd.h>
#endif

namespace neverc {
namespace build {
namespace builtins {
namespace internal {


bool tryExecutePwd(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-"))
      return false;
  }
  if (Argv.size() > 1) {
    llvm::errs() << "neverc make: pwd: too many arguments\n";
    ExitCode = 1;
    return true;
  }
  llvm::outs() << platform::getCwd() << '\n';
  llvm::outs().flush();
  ExitCode = 0;
  return true;
}


/// POSIX basename/dirname strip all trailing slashes before computing the
/// result, except when the path is entirely separators (the root).
std::string stripTrailingSeparatorsForBaseName(llvm::StringRef Raw) {
  if (Raw.empty())
    return {};
  if (llvm::all_of(Raw, [](char C) { return C == '/' || C == '\\'; }))
    return "/";
  llvm::StringRef P = Raw;
  while (P.size() > 1 && (P.ends_with("/") || P.ends_with("\\")))
    P = P.drop_back();
  return P.str();
}


bool tryExecuteBasename(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  if (hasUnquotedGlob(Argv.drop_front()))
    return false;
  bool Multiple = false;
  llvm::StringRef SuffixOpt;
  bool HaveSuffixOpt = false;
  llvm::SmallVector<Token, 4> Names;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "-a") {
      Multiple = true;
      continue;
    }
    if (Arg == "-s") {
      if (I + 1 >= Argv.size()) {
        llvm::errs()
            << "neverc make: basename: option requires an argument -- s\n";
        ExitCode = 1;
        return true;
      }
      SuffixOpt = Argv[++I].Text;
      HaveSuffixOpt = true;
      Multiple = true; // GNU: -s implies multiple-name mode
      continue;
    }
    if (Arg.starts_with("-s") && Arg.size() > 2) {
      SuffixOpt = Arg.drop_front(2);
      HaveSuffixOpt = true;
      Multiple = true;
      continue;
    }
    if (Arg.starts_with("-") && Arg != "-")
      return false; // -z stays on the host tool
    Names.push_back(Argv[I]);
  }
  if (Names.empty()) {
    llvm::errs() << "neverc make: basename: expected NAME [SUFFIX]\n";
    ExitCode = 1;
    return true;
  }
  if (!Multiple && !HaveSuffixOpt && Names.size() > 2) {
    // Without -a/-s, classic form is NAME [SUFFIX] only.
    return false;
  }
  if (!Multiple && !HaveSuffixOpt && Names.size() == 2) {
    // Ambiguous NAME SUFFIX vs two names: keep classic SUFFIX form.
  } else if (!Multiple && !HaveSuffixOpt && Names.size() != 1) {
    llvm::errs() << "neverc make: basename: expected NAME [SUFFIX]\n";
    ExitCode = 1;
    return true;
  }

  auto one = [](llvm::StringRef Raw, llvm::StringRef Suffix) {
    // POSIX/GNU: basename of "" is "."; of "/" (only separators) is "/".
    if (Raw.empty()) {
      llvm::outs() << ".\n";
      return;
    }
    const std::string Normalized = stripTrailingSeparatorsForBaseName(Raw);
    std::string Out;
    if (Normalized == "/") {
      Out = "/";
    } else {
      Out = llvm::sys::path::filename(Normalized).str();
      if (Out.empty())
        Out = Normalized;
    }
    if (!Suffix.empty() && llvm::StringRef(Out).ends_with(Suffix) &&
        Out.size() > Suffix.size())
      Out.resize(Out.size() - Suffix.size());
    llvm::outs() << Out << '\n';
  };

  if (!Multiple && !HaveSuffixOpt) {
    one(Names[0].Text, Names.size() == 2 ? llvm::StringRef(Names[1].Text)
                                         : llvm::StringRef());
  } else {
    for (const Token &Name : Names)
      one(Name.Text, HaveSuffixOpt ? SuffixOpt : llvm::StringRef());
  }
  llvm::outs().flush();
  ExitCode = 0;
  return true;
}


bool tryExecuteDirname(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  if (hasUnquotedGlob(Argv.drop_front()))
    return false;
  llvm::SmallVector<Token, 4> Names;
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-") &&
        Argv[I].Text != "-")
      return false;
    Names.push_back(Argv[I]);
  }
  if (Names.empty()) {
    llvm::errs() << "neverc make: dirname: missing operand\n";
    ExitCode = 1;
    return true;
  }

  auto one = [](llvm::StringRef Raw) {
    const std::string Normalized = stripTrailingSeparatorsForBaseName(Raw);
    // POSIX: dirname of "/" is "/".
    if (Normalized == "/") {
      llvm::outs() << "/\n";
      return;
    }
    llvm::SmallString<256> Path(Normalized);
    llvm::sys::path::remove_filename(Path);
    if (Path.empty())
      Path = ".";
    // Strip a trailing separator except when the result is a root path.
    while (Path.size() > 1 && (Path.back() == '/' || Path.back() == '\\'))
      Path.pop_back();
    llvm::outs() << Path << '\n';
  };

  for (const Token &Name : Names)
    one(Name.Text);
  llvm::outs().flush();
  ExitCode = 0;
  return true;
}


bool tryExecuteRealpath(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  llvm::SmallVector<Token, 4> Paths;
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-") &&
        Argv[I].Text != "-")
      return false;
    Paths.push_back(Argv[I]);
  }
  if (Paths.empty()) {
    llvm::errs() << "neverc make: realpath: missing operand\n";
    ExitCode = 1;
    return true;
  }

  bool Failed = false;
  for (const std::string &Path : expandOperands(Paths)) {
    llvm::SmallString<256> Real;
    if (std::error_code EC = llvm::sys::fs::real_path(Path, Real)) {
      llvm::errs() << "neverc make: realpath: " << Path << ": " << EC.message()
                   << "\n";
      Failed = true;
      continue;
    }
    llvm::outs() << Real << '\n';
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteReadlink(llvm::ArrayRef<Token> Argv, int &ExitCode) {
#ifdef _WIN32
  (void)Argv;
  (void)ExitCode;
  // Leave Windows symlink resolution to the host tool.
  return false;
#else
  if (hasUnquotedGlob(Argv.drop_front()))
    return false;
  bool Canonical = false;
  llvm::SmallVector<Token, 4> Paths;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if ((Arg == "-f" || Arg == "--canonicalize")) {
      Canonical = true;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Paths.push_back(Argv[I]);
  }
  if (Paths.size() != 1) {
    llvm::errs() << "neverc make: readlink: expected one operand\n";
    ExitCode = 1;
    return true;
  }

  const std::string Path = Paths[0].Text;
  if (Canonical) {
    llvm::SmallString<256> Real;
    if (std::error_code EC = llvm::sys::fs::real_path(Path, Real)) {
      llvm::errs() << "neverc make: readlink: " << Path << ": " << EC.message()
                   << "\n";
      ExitCode = 1;
      return true;
    }
    llvm::outs() << Real << '\n';
    llvm::outs().flush();
    ExitCode = 0;
    return true;
  }

  // Grow until the full link target fits; a fixed 4K buffer silently truncates
  // long targets (and can disagree with /usr/bin/readlink).
  std::vector<char> Buf(256);
  while (true) {
    const ssize_t N = ::readlink(Path.c_str(), Buf.data(), Buf.size());
    if (N < 0) {
      llvm::errs() << "neverc make: readlink: " << Path << ": "
                   << std::error_code(errno, std::generic_category()).message()
                   << "\n";
      ExitCode = 1;
      return true;
    }
    if (static_cast<size_t>(N) < Buf.size()) {
      llvm::outs() << llvm::StringRef(Buf.data(), static_cast<size_t>(N))
                   << '\n';
      llvm::outs().flush();
      ExitCode = 0;
      return true;
    }
    if (Buf.size() >= 1024 * 1024) {
      llvm::errs() << "neverc make: readlink: " << Path
                   << ": target too long\n";
      ExitCode = 1;
      return true;
    }
    Buf.resize(Buf.size() * 2);
  }
#endif
}


bool tryExecuteLs(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  bool All = false;
  bool AlmostAll = false;
  bool DirectoryOnly = false;
  llvm::SmallVector<Token, 4> Operands;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg.starts_with("-") && Arg.size() > 1 &&
        !Arg.starts_with("--")) {
      for (size_t J = 1; J < Arg.size(); ++J) {
        switch (Arg[J]) {
        case '1':
          break; // one-per-line is our only layout
        case 'a':
          All = true;
          break;
        case 'A':
          AlmostAll = true;
          break;
        case 'd':
          DirectoryOnly = true;
          break;
        default:
          return false; // -l/-R/-h stay on the host tool
        }
      }
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Operands.push_back(Argv[I]);
  }

  auto shouldShow = [&](llvm::StringRef Name) {
    if (Name == "." || Name == "..")
      return All;
    if (Name.starts_with("."))
      return All || AlmostAll;
    return true;
  };

  auto listOne = [&](llvm::StringRef Path, bool PrintHeader) -> bool {
    if (DirectoryOnly || !isDirectory(Path)) {
      llvm::outs() << Path << '\n';
      return true;
    }
    std::error_code IterEC;
    std::vector<std::string> Names;
    for (llvm::sys::fs::directory_iterator It(Path, IterEC), End;
         !IterEC && It != End; It.increment(IterEC)) {
      llvm::StringRef Name = llvm::sys::path::filename(It->path());
      if (shouldShow(Name))
        Names.push_back(Name.str());
    }
    if (IterEC) {
      llvm::errs() << "neverc make: ls: " << Path << ": " << IterEC.message()
                   << "\n";
      return false;
    }
    if (All) {
      Names.insert(Names.begin(), "..");
      Names.insert(Names.begin(), ".");
    }
    llvm::sort(Names);
    if (PrintHeader)
      llvm::outs() << Path << ":\n";
    for (const std::string &Name : Names)
      llvm::outs() << Name << '\n';
    return true;
  };

  bool Failed = false;
  if (Operands.empty()) {
    if (!listOne(".", /*PrintHeader=*/false))
      Failed = true;
  } else {
    const auto Expanded = expandOperands(Operands);
    const bool Multi = Expanded.size() > 1;
    for (size_t I = 0; I < Expanded.size(); ++I) {
      if (Multi && I)
        llvm::outs() << '\n';
      if (!listOne(Expanded[I], /*PrintHeader=*/Multi &&
                                      isDirectory(Expanded[I]) &&
                                      !DirectoryOnly))
        Failed = true;
    }
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteDu(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `du -s [-k] FILE...` (disk usage of named paths only;
  // no directory tree walk beyond a single path's recursive size).
  bool Summarize = false;
  bool Kilobytes = false;
  llvm::SmallVector<Token, 4> Paths;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg.starts_with("-") && Arg.size() > 1 &&
        !Arg.starts_with("--")) {
      for (size_t J = 1; J < Arg.size(); ++J) {
        switch (Arg[J]) {
        case 's':
          Summarize = true;
          break;
        case 'k':
          Kilobytes = true;
          break;
        default:
          return false; // -h/-a/-d stay on the host tool
        }
      }
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Paths.push_back(Argv[I]);
  }
  if (!Summarize || Paths.empty() || hasStdinDashOperand(Paths))
    return false;

  auto dirSize = [&](auto &&Self, llvm::StringRef Path, uint64_t &Bytes) -> bool {
    llvm::sys::fs::file_status Status;
    if (llvm::sys::fs::status(Path, Status, /*follow=*/false))
      return false;
    if (Status.type() == llvm::sys::fs::file_type::directory_file) {
      std::error_code IterEC;
      for (llvm::sys::fs::directory_iterator It(Path, IterEC), End;
           !IterEC && It != End; It.increment(IterEC)) {
        if (!Self(Self, It->path(), Bytes))
          return false;
      }
      return !IterEC;
    }
    Bytes += Status.getSize();
    return true;
  };

  bool Failed = false;
  for (const std::string &Path : expandOperands(Paths)) {
    uint64_t Bytes = 0;
    if (!dirSize(dirSize, Path, Bytes)) {
      llvm::errs() << "neverc make: du: " << Path << ": cannot read\n";
      Failed = true;
      continue;
    }
    const uint64_t Units =
        Kilobytes ? (Bytes + 1023) / 1024 : (Bytes + 511) / 512;
    llvm::outs() << Units << '\t' << Path << '\n';
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool formatStatField(char Spec, llvm::StringRef Path,
                     const llvm::sys::fs::file_status &Status,
                     std::string &Out) {
  switch (Spec) {
  case '%':
    Out.push_back('%');
    return true;
  case 'n':
    Out += Path;
    return true;
  case 's':
    Out += std::to_string(Status.getSize());
    return true;
  case 'Y': {
    // Seconds since epoch of mtime (GNU %Y).
    const auto Secs = std::chrono::duration_cast<std::chrono::seconds>(
                          Status.getLastModificationTime().time_since_epoch())
                          .count();
    Out += std::to_string(Secs);
    return true;
  }
  case 'F':
    switch (Status.type()) {
    case llvm::sys::fs::file_type::regular_file:
      Out += "regular file";
      break;
    case llvm::sys::fs::file_type::directory_file:
      Out += "directory";
      break;
    case llvm::sys::fs::file_type::symlink_file:
      Out += "symbolic link";
      break;
    default:
      Out += "unknown";
      break;
    }
    return true;
  default:
    return false;
  }
}


bool tryExecuteDf(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `df -k` / `df -P` / `df -kP [PATH...]`.
  // Without path operands, report the filesystem of ".".
  bool Kilobytes = false;
  bool Portable = false;
  llvm::SmallVector<Token, 4> Paths;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg.starts_with("-") && Arg.size() > 1 && !Arg.starts_with("--")) {
      for (size_t J = 1; J < Arg.size(); ++J) {
        switch (Arg[J]) {
        case 'k':
          Kilobytes = true;
          break;
        case 'P':
          Portable = true;
          break;
        default:
          return false; // -h/-i/-T stay on the host tool
        }
      }
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Paths.push_back(Argv[I]);
  }
  // Require an explicit -k or -P so we do not invent host-specific default units.
  if (!Kilobytes && !Portable)
    return false;
  if (hasStdinDashOperand(Paths))
    return false;

  llvm::SmallVector<std::string, 4> Targets;
  if (Paths.empty())
    Targets.push_back(".");
  else
    Targets = expandOperands(Paths);

#ifdef _WIN32
  (void)ExitCode;
  (void)Targets;
  // Windows filesystem capacity queries vary by CRT; leave `df` to the host.
  return false;
#else
  // POSIX portable format header when -P is set (GNU df -P).
  if (Portable)
    llvm::outs() << "Filesystem 1024-blocks Used Available Capacity Mounted on\n";

  bool Failed = false;
  for (const std::string &Path : Targets) {
    struct statvfs Svfs {};
    if (::statvfs(Path.c_str(), &Svfs) != 0) {
      llvm::errs() << "neverc make: df: " << Path << ": "
                   << std::error_code(errno, std::generic_category()).message()
                   << "\n";
      Failed = true;
      continue;
    }
    const uint64_t FrSize = Svfs.f_frsize ? Svfs.f_frsize : Svfs.f_bsize;
    if (FrSize == 0) {
      llvm::errs() << "neverc make: df: " << Path << ": invalid block size\n";
      Failed = true;
      continue;
    }
    const uint64_t TotalBytes = static_cast<uint64_t>(Svfs.f_blocks) * FrSize;
    // POSIX/GNU: Used = total - free (f_bfree). Available for unprivileged
    // writers is f_bavail (excludes root-reserved blocks) — do not derive Used
    // from Available or reserved space is counted as "used" incorrectly.
    const uint64_t FreeBytes = static_cast<uint64_t>(Svfs.f_bfree) * FrSize;
    const uint64_t AvailBytes = static_cast<uint64_t>(Svfs.f_bavail) * FrSize;
    const uint64_t UsedBytes =
        TotalBytes > FreeBytes ? TotalBytes - FreeBytes : 0;
    const uint64_t Unit = 1024;
    const uint64_t Blocks = (TotalBytes + Unit - 1) / Unit;
    const uint64_t Used = (UsedBytes + Unit - 1) / Unit;
    const uint64_t Avail = (AvailBytes + Unit - 1) / Unit;
    unsigned Capacity = 0;
    if (Used + Avail > 0)
      Capacity = static_cast<unsigned>((Used * 100 + Used + Avail - 1) /
                                      (Used + Avail));
    // We do not resolve the device/mount point name portably; print "-" and the
    // queried path so recipes can still read the numeric columns.
    llvm::outs() << "- " << Blocks << ' ' << Used << ' ' << Avail << ' '
                 << Capacity << "% " << Path << '\n';
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
#endif
}


bool tryExecuteStat(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `stat -c FORMAT FILE...` / `stat --format=FORMAT FILE...`
  // with FORMAT specs %s %n %Y %F %%.
  llvm::StringRef Format;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "-c") {
      if (I + 1 >= Argv.size()) {
        llvm::errs() << "neverc make: stat: option requires an argument -- c\n";
        ExitCode = 1;
        return true;
      }
      Format = Argv[++I].Text;
      continue;
    }
    if (Arg.starts_with("--format=")) {
      Format = Arg.drop_front(9); // strlen("--format=")
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false; // -f/-L/-t stay on the host tool
    Files.push_back(Argv[I]);
  }
  if (Format.empty() || Files.empty() || hasStdinDashOperand(Files))
    return false;

  // Reject unknown conversion specs up front so we fall back rather than
  // inventing GNU-incompatible output.
  for (size_t I = 0; I < Format.size(); ++I) {
    if (Format[I] != '%' || I + 1 >= Format.size())
      continue;
    const char Spec = Format[I + 1];
    if (Spec != '%' && Spec != 's' && Spec != 'n' && Spec != 'Y' && Spec != 'F')
      return false;
    ++I;
  }

  bool Failed = false;
  for (const std::string &Path : expandOperands(Files)) {
    llvm::sys::fs::file_status Status;
    // Match GNU/BSD `stat` default: do not follow symlinks (`-L` stays on the
    // host tool). Otherwise `%F` reports the target type and `%s`/`%Y` disagree
    // with /usr/bin/stat on link operands.
    if (std::error_code EC =
            llvm::sys::fs::status(Path, Status, /*follow=*/false)) {
      llvm::errs() << "neverc make: stat: " << Path << ": " << EC.message()
                   << "\n";
      Failed = true;
      continue;
    }
    std::string Out;
    for (size_t I = 0; I < Format.size(); ++I) {
      if (Format[I] != '%' || I + 1 >= Format.size()) {
        Out.push_back(Format[I]);
        continue;
      }
      if (!formatStatField(Format[I + 1], Path, Status, Out))
        return false;
      ++I;
    }
    llvm::outs() << Out << '\n';
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteFile(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: bare `file PATH...` (no magic-database / -b/-i/-z).
  // Reports a small, stable type set useful for Makefile probes.
  llvm::SmallVector<Token, 4> Paths;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false; // magic / mime flags stay on the host tool
    Paths.push_back(Argv[I]);
  }
  if (Paths.empty() || hasStdinDashOperand(Paths))
    return false;

  auto classifyRegular = [](llvm::StringRef Data) -> const char * {
    if (Data.empty())
      return "empty";
    // ELF / PE / Mach-O quick checks so recipes can distinguish binaries.
    if (Data.size() >= 4 && Data[0] == '\x7f' && Data[1] == 'E' &&
        Data[2] == 'L' && Data[3] == 'F')
      return "ELF";
    if (Data.size() >= 2 &&
        ((unsigned char)Data[0] == 0x4d && (unsigned char)Data[1] == 0x5a))
      return "PE executable";
    if (Data.size() >= 4 &&
        ((Data[0] == '\xfe' && Data[1] == '\xed' && Data[2] == '\xfa' &&
          (Data[3] == '\xce' || Data[3] == '\xcf')) ||
         (Data[0] == '\xcf' && Data[1] == '\xfa' && Data[2] == '\xed' &&
          Data[3] == '\xfe') ||
         (Data[0] == '\xca' && Data[1] == '\xfe' && Data[2] == '\xba' &&
          Data[3] == '\xbe')))
      return "Mach-O";
    bool Printable = true;
    for (unsigned char C : Data) {
      if (C == '\0') {
        Printable = false;
        break;
      }
      if (C != '\t' && C != '\n' && C != '\r' && C != '\f' && C != '\v' &&
          C < 32)
        Printable = false;
    }
    return Printable ? "ASCII text" : "data";
  };

  bool Failed = false;
  for (const std::string &Path : expandOperands(Paths)) {
    llvm::sys::fs::file_status Status;
    if (std::error_code EC =
            llvm::sys::fs::status(Path, Status, /*follow=*/false)) {
      llvm::errs() << "neverc make: file: " << Path << ": " << EC.message()
                   << "\n";
      Failed = true;
      continue;
    }
    llvm::outs() << Path << ": ";
    switch (Status.type()) {
    case llvm::sys::fs::file_type::directory_file:
      llvm::outs() << "directory\n";
      break;
    case llvm::sys::fs::file_type::symlink_file:
      llvm::outs() << "symbolic link\n";
      break;
    case llvm::sys::fs::file_type::fifo_file:
      llvm::outs() << "fifo\n";
      break;
    case llvm::sys::fs::file_type::socket_file:
      llvm::outs() << "socket\n";
      break;
    case llvm::sys::fs::file_type::block_file:
      llvm::outs() << "block special\n";
      break;
    case llvm::sys::fs::file_type::character_file:
      llvm::outs() << "character special\n";
      break;
    case llvm::sys::fs::file_type::regular_file: {
      auto Buf = llvm::MemoryBuffer::getFile(Path);
      if (!Buf) {
        llvm::errs() << "neverc make: file: " << Path << ": "
                     << Buf.getError().message() << "\n";
        Failed = true;
        break;
      }
      // Cap classification to the first 8KiB — enough for magic / text probes.
      llvm::StringRef Data = (*Buf)->getBuffer().take_front(8192);
      llvm::outs() << classifyRegular(Data) << '\n';
      break;
    }
    default:
      llvm::outs() << "unknown\n";
      break;
    }
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


} // namespace internal
} // namespace builtins
} // namespace build
} // namespace neverc
