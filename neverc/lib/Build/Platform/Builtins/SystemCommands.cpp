#include "Platform/Builtins/Internal.h"

#include "neverc/Build/Platform.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#ifndef _WIN32
extern char **environ;
#endif

namespace neverc {
namespace build {
namespace builtins {
namespace internal {


bool tryExecuteUname(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  bool PrintSys = false;
  bool PrintNode = false;
  bool PrintRelease = false;
  bool PrintVersion = false;
  bool PrintMachine = false;
  bool Any = false;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (!Arg.starts_with("-") || Arg.size() < 2)
      return false;
    if (Arg == "-a") {
      PrintSys = PrintNode = PrintRelease = PrintVersion = PrintMachine = true;
      Any = true;
      continue;
    }
    for (size_t J = 1; J < Arg.size(); ++J) {
      switch (Arg[J]) {
      case 's':
        PrintSys = true;
        break;
      case 'n':
        PrintNode = true;
        break;
      case 'r':
        PrintRelease = true;
        break;
      case 'v':
        PrintVersion = true;
        break;
      case 'm':
        PrintMachine = true;
        break;
      default:
        return false;
      }
      Any = true;
    }
  }
  if (!Any)
    PrintSys = true;

  std::string SysName = "unknown";
  std::string NodeName = "localhost";
  std::string Release = "unknown";
  std::string Version = "unknown";
  std::string Machine = "unknown";
#ifndef _WIN32
  struct utsname U;
  if (::uname(&U) == 0) {
    SysName = U.sysname;
    NodeName = U.nodename;
    Release = U.release;
    Version = U.version;
    Machine = U.machine;
  }
#else
  SysName = "Windows";
#if defined(_M_ARM64)
  Machine = "aarch64";
#elif defined(_M_X64)
  Machine = "x86_64";
#elif defined(_M_IX86)
  Machine = "i386";
#endif
#endif

  llvm::SmallVector<std::string, 5> Parts;
  if (PrintSys)
    Parts.push_back(SysName);
  if (PrintNode)
    Parts.push_back(NodeName);
  if (PrintRelease)
    Parts.push_back(Release);
  if (PrintVersion)
    Parts.push_back(Version);
  if (PrintMachine)
    Parts.push_back(Machine);

  for (size_t I = 0; I < Parts.size(); ++I) {
    if (I)
      llvm::outs() << ' ';
    llvm::outs() << Parts[I];
  }
  llvm::outs() << '\n';
  llvm::outs().flush();
  ExitCode = 0;
  return true;
}


bool tryExecuteSeq(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-"))
      return false; // -f / -s / -w stay on the host tool
  }
  if (Argv.size() < 2 || Argv.size() > 4) {
    llvm::errs() << "neverc make: seq: expected LAST or FIRST LAST or "
                    "FIRST INCREMENT LAST\n";
    ExitCode = 1;
    return true;
  }

  long First = 1;
  long Increment = 1;
  long Last = 0;
  auto parseLong = [](llvm::StringRef Text, long &Value) {
    if (Text.empty())
      return false;
    const std::string S = Text.str();
    char *End = nullptr;
    errno = 0;
    const long Parsed = std::strtol(S.c_str(), &End, 10);
    if (End == S.c_str() || (End && *End != '\0') || errno == ERANGE)
      return false;
    Value = Parsed;
    return true;
  };

  if (Argv.size() == 2) {
    if (!parseLong(Argv[1].Text, Last))
      return false;
  } else if (Argv.size() == 3) {
    if (!parseLong(Argv[1].Text, First) || !parseLong(Argv[2].Text, Last))
      return false;
  } else {
    if (!parseLong(Argv[1].Text, First) || !parseLong(Argv[2].Text, Increment) ||
        !parseLong(Argv[3].Text, Last))
      return false;
  }
  if (Increment == 0) {
    llvm::errs() << "neverc make: seq: zero increment\n";
    ExitCode = 1;
    return true;
  }

  // Cap runaway sequences from bad recipe arguments, and avoid signed overflow
  // on `V += Increment` near long extremes.
  constexpr long MaxTerms = 1000000;
  long Terms = 0;
  long V = First;
  while (true) {
    if (Increment > 0) {
      if (V > Last)
        break;
    } else if (V < Last) {
      break;
    }
    llvm::outs() << V << '\n';
    if (++Terms > MaxTerms) {
      llvm::errs() << "neverc make: seq: sequence too long\n";
      ExitCode = 1;
      return true;
    }
    // Use a wider accumulator so `V + Increment` near long extremes is
    // well-defined (especially when Increment is negative).
    const long long Next =
        static_cast<long long>(V) + static_cast<long long>(Increment);
    if (Next > static_cast<long long>(std::numeric_limits<long>::max()) ||
        Next < static_cast<long long>(std::numeric_limits<long>::min()))
      break;
    V = static_cast<long>(Next);
  }
  llvm::outs().flush();
  ExitCode = 0;
  return true;
}


bool tryExecuteWhich(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  if (Argv.size() != 2 ||
      (llvm::StringRef(Argv[1].Text).starts_with("-")))
    return false;

  const std::string Name = Argv[1].Text;
  if (Name.find('/') != std::string::npos
#ifdef _WIN32
      || Name.find('\\') != std::string::npos
#endif
  ) {
    if (llvm::sys::fs::can_execute(Name)) {
      llvm::outs() << Name << '\n';
      llvm::outs().flush();
      ExitCode = 0;
      return true;
    }
    ExitCode = 1;
    return true;
  }

  const char *PathEnv = std::getenv("PATH");
  if (!PathEnv) {
    ExitCode = 1;
    return true;
  }
  llvm::SmallVector<llvm::StringRef, 16> Dirs;
  llvm::StringRef(PathEnv).split(Dirs,
#ifdef _WIN32
                                 ';'
#else
                                 ':'
#endif
  );
  for (llvm::StringRef Dir : Dirs) {
    if (Dir.empty())
      Dir = ".";
    llvm::SmallString<256> Candidate(Dir);
    llvm::sys::path::append(Candidate, Name);
    if (llvm::sys::fs::can_execute(Candidate)) {
      llvm::outs() << Candidate << '\n';
      llvm::outs().flush();
      ExitCode = 0;
      return true;
    }
  }
  ExitCode = 1;
  return true;
}


bool tryExecuteHostname(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-"))
      return false;
  }
  if (Argv.size() > 1) {
    llvm::errs() << "neverc make: hostname: unexpected operand\n";
    ExitCode = 1;
    return true;
  }

  std::string Name = "localhost";
#ifndef _WIN32
  char Buf[256];
  if (::gethostname(Buf, sizeof(Buf)) == 0) {
    Buf[sizeof(Buf) - 1] = '\0';
    Name = Buf;
  }
#else
  char Buf[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD Size = sizeof(Buf);
  if (GetComputerNameA(Buf, &Size))
    Name.assign(Buf, Size);
#endif
  llvm::outs() << Name << '\n';
  llvm::outs().flush();
  ExitCode = 0;
  return true;
}


bool tryExecuteWhoami(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-"))
      return false;
  }
  if (Argv.size() > 1) {
    llvm::errs() << "neverc make: whoami: unexpected operand\n";
    ExitCode = 1;
    return true;
  }
#ifndef _WIN32
  if (const passwd *Pw = ::getpwuid(::geteuid())) {
    llvm::outs() << Pw->pw_name << '\n';
    llvm::outs().flush();
    ExitCode = 0;
    return true;
  }
  llvm::errs() << "neverc make: whoami: cannot determine user name\n";
  ExitCode = 1;
  return true;
#else
  char Buf[256];
  DWORD Size = sizeof(Buf);
  if (GetUserNameA(Buf, &Size)) {
    llvm::outs() << Buf << '\n';
    llvm::outs().flush();
    ExitCode = 0;
    return true;
  }
  llvm::errs() << "neverc make: whoami: cannot determine user name\n";
  ExitCode = 1;
  return true;
#endif
}


bool tryExecuteNproc(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-"))
      return false; // --all / --ignore stay on the host tool
  }
  if (Argv.size() > 1) {
    llvm::errs() << "neverc make: nproc: unexpected operand\n";
    ExitCode = 1;
    return true;
  }
  llvm::outs() << platform::getProcessorCount() << '\n';
  llvm::outs().flush();
  ExitCode = 0;
  return true;
}


bool formatDate(llvm::StringRef Fmt, std::string &Out) {
  const std::time_t Now = std::time(nullptr);
  std::tm Tm{};
#ifdef _WIN32
  if (localtime_s(&Tm, &Now) != 0)
    return false;
#else
  if (!localtime_r(&Now, &Tm))
    return false;
#endif
  // Support a small strftime subset commonly used in Makefiles.
  for (size_t I = 0; I < Fmt.size(); ++I) {
    if (Fmt[I] != '%' || I + 1 >= Fmt.size()) {
      Out.push_back(Fmt[I]);
      continue;
    }
    const char Spec = Fmt[I + 1];
    char Buf[64];
    const char Conv[3] = {'%', Spec, '\0'};
    switch (Spec) {
    case '%':
      Out.push_back('%');
      break;
    case 's':
      Out += std::to_string(static_cast<long long>(Now));
      break;
    case 'Y':
    case 'm':
    case 'd':
    case 'H':
    case 'M':
    case 'S':
    case 'F':
    case 'T':
      if (std::strftime(Buf, sizeof(Buf), Conv, &Tm) == 0)
        return false;
      Out += Buf;
      break;
    default:
      return false;
    }
    I += 1;
  }
  return true;
}


bool tryExecuteDate(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  llvm::StringRef Fmt;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg.starts_with("+") && Fmt.empty()) {
      Fmt = Arg.drop_front();
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false; // -u / -r / -d stay on the host tool
    return false;
  }

  std::string Out;
  if (Fmt.empty()) {
    // Default output is intentionally simple and stable for recipes.
    if (!formatDate("%Y-%m-%d %H:%M:%S", Out))
      return false;
  } else if (!formatDate(Fmt, Out)) {
    return false;
  }
  llvm::outs() << Out << '\n';
  llvm::outs().flush();
  ExitCode = 0;
  return true;
}


bool tryExecutePrintenv(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  if (Argv.size() == 1) {
#ifdef _WIN32
    return false; // full environ dump on Windows is left to the host tool
#else
    if (!environ) {
      ExitCode = 0;
      return true;
    }
    for (char **P = environ; *P; ++P)
      llvm::outs() << *P << '\n';
    llvm::outs().flush();
    ExitCode = 0;
    return true;
#endif
  }
  if (Argv.size() != 2 ||
      (llvm::StringRef(Argv[1].Text).starts_with("-")))
    return false;
  if (const char *Val = std::getenv(Argv[1].Text.c_str())) {
    llvm::outs() << Val << '\n';
    llvm::outs().flush();
    ExitCode = 0;
    return true;
  }
  ExitCode = 1;
  return true;
}


bool tryExecuteEnv(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `env` with no args (dump) or fall back when setting vars /
  // running commands (`env FOO=bar cmd`).
  if (Argv.size() == 1)
    return tryExecutePrintenv(Argv, ExitCode);
  return false;
}


bool tryExecuteId(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  bool User = false;
  bool Group = false;
  bool Name = false;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (!Arg.starts_with("-") || Arg.size() < 2)
      return false;
    for (size_t J = 1; J < Arg.size(); ++J) {
      switch (Arg[J]) {
      case 'u':
        User = true;
        break;
      case 'g':
        Group = true;
        break;
      case 'n':
        Name = true;
        break;
      default:
        return false;
      }
    }
  }
  // Portable subset: `id -u`, `id -g`, `id -un`. Full `id` / `id -gn` stay
  // on the host tool (group-name lookup and supplemental groups vary).
  if (User == Group)
    return false;
  if (Group && Name)
    return false;

#ifndef _WIN32
  if (User) {
    if (Name) {
      if (const passwd *Pw = ::getpwuid(::geteuid())) {
        llvm::outs() << Pw->pw_name << '\n';
        llvm::outs().flush();
        ExitCode = 0;
        return true;
      }
      llvm::errs() << "neverc make: id: cannot find name for user ID\n";
      ExitCode = 1;
      return true;
    }
    llvm::outs() << ::geteuid() << '\n';
    llvm::outs().flush();
    ExitCode = 0;
    return true;
  }
  llvm::outs() << ::getegid() << '\n';
  llvm::outs().flush();
  ExitCode = 0;
  return true;
#else
  (void)Name;
  (void)ExitCode;
  // Windows has no POSIX uid/gid model for recipes; leave `id` to the host.
  return false;
#endif
}


bool tryExecuteSync(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-"))
      return false; // sync FILE / sync -f stay on the host tool
  }
  if (Argv.size() > 1) {
    // GNU sync can take file operands; we only claim the bare form.
    return false;
  }
#ifndef _WIN32
  ::sync();
#else
  // No direct POSIX sync equivalent; treat bare `sync` as a successful no-op
  // so Windows recipes do not depend on a host `sync.exe`.
#endif
  ExitCode = 0;
  return true;
}


bool tryExecuteArch(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // `arch` ≈ `uname -m` on Linux/BSD recipe usage.
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-"))
      return false;
  }
  if (Argv.size() > 1) {
    llvm::errs() << "neverc make: arch: unexpected operand\n";
    ExitCode = 1;
    return true;
  }

  std::string Machine = "unknown";
#ifndef _WIN32
  struct utsname U;
  if (::uname(&U) == 0)
    Machine = U.machine;
#else
#if defined(_M_ARM64)
  Machine = "aarch64";
#elif defined(_M_X64)
  Machine = "x86_64";
#elif defined(_M_IX86)
  Machine = "i386";
#endif
#endif
  llvm::outs() << Machine << '\n';
  llvm::outs().flush();
  ExitCode = 0;
  return true;
}


bool tryExecuteCommand(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `command -v NAME` (PATH lookup, like a minimal which).
  if (Argv.size() != 3 || Argv[1].Text != "-v")
    return false;
  llvm::SmallVector<Token, 2> WhichArgv;
  WhichArgv.push_back(Token{"which", /*Quoted=*/false});
  WhichArgv.push_back(Argv[2]);
  return tryExecuteWhich(WhichArgv, ExitCode);
}


bool tryExecuteType(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: only `type -p NAME` (path-only). Bare `type NAME` prints
  // shell-specific prose ("NAME is ...") that we must not invent — leave it to
  // the host shell / builtin.
  if (Argv.size() != 3 || Argv[1].Text != "-p")
    return false;
  const llvm::StringRef Name = Argv[2].Text;
  if (Name.starts_with("-") && Name != "-")
    return false;
  llvm::SmallVector<Token, 2> WhichArgv;
  WhichArgv.push_back(Token{"which", /*Quoted=*/false});
  WhichArgv.push_back(Token{Name.str(), Argv[2].Quoted});
  return tryExecuteWhich(WhichArgv, ExitCode);
}


bool tryExecuteGetconf(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Small Makefile-oriented subset; unknown names fall back to the host tool.
  if (Argv.size() != 2 ||
      (llvm::StringRef(Argv[1].Text).starts_with("-")))
    return false;
  const llvm::StringRef Name = Argv[1].Text;
#ifndef _WIN32
  long Value = -1;
  if (Name == "NPROCESSORS_ONLN" || Name == "_NPROCESSORS_ONLN") {
    Value = static_cast<long>(platform::getProcessorCount());
  } else if (Name == "PAGE_SIZE" || Name == "PAGESIZE") {
    Value = ::sysconf(_SC_PAGESIZE);
  } else if (Name == "PATH_MAX") {
    Value = ::pathconf("/", _PC_PATH_MAX);
    if (Value < 0)
      Value = PATH_MAX;
  } else if (Name == "CLK_TCK") {
    Value = ::sysconf(_SC_CLK_TCK);
  } else {
    return false;
  }
  if (Value < 0) {
    llvm::errs() << "neverc make: getconf: " << Name << ": unavailable\n";
    ExitCode = 1;
    return true;
  }
  llvm::outs() << Value << '\n';
  llvm::outs().flush();
  ExitCode = 0;
  return true;
#else
  if (Name == "NPROCESSORS_ONLN" || Name == "_NPROCESSORS_ONLN") {
    llvm::outs() << platform::getProcessorCount() << '\n';
    llvm::outs().flush();
    ExitCode = 0;
    return true;
  }
  return false;
#endif
}


bool tryExecuteFactor(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `factor INTEGER...` (positive integers only).
  if (Argv.size() < 2)
    return false;
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-") &&
        Argv[I].Text != "-")
      return false;
  }

  auto parseU64 = [](llvm::StringRef Text, uint64_t &Value) -> bool {
    if (Text.empty() || Text.starts_with("-") || Text.starts_with("+"))
      return false;
    const std::string S = Text.str();
    char *End = nullptr;
    errno = 0;
    const unsigned long long Parsed = std::strtoull(S.c_str(), &End, 10);
    if (End == S.c_str() || (End && *End != '\0') || errno == ERANGE)
      return false;
    Value = static_cast<uint64_t>(Parsed);
    return true;
  };

  // Trial division is fine for Makefile-sized integers, but a near-2^64 prime
  // would burn ~2^32 iterations while holding the builtin I/O lock. Leave large
  // values to the host factor(1).
  constexpr uint64_t MaxClaim = 1000000000000ULL; // 10^12
  for (size_t Ai = 1; Ai < Argv.size(); ++Ai) {
    uint64_t N = 0;
    if (!parseU64(Argv[Ai].Text, N))
      return false;
    if (N > MaxClaim)
      return false;
  }

  bool Failed = false;
  for (size_t Ai = 1; Ai < Argv.size(); ++Ai) {
    uint64_t N = 0;
    if (!parseU64(Argv[Ai].Text, N)) {
      llvm::errs() << "neverc make: factor: '" << Argv[Ai].Text
                   << "' is not a valid positive integer\n";
      Failed = true;
      continue;
    }
    llvm::outs() << N << ':';
    if (N <= 1) {
      llvm::outs() << '\n';
      continue;
    }
    while ((N & 1ULL) == 0) {
      llvm::outs() << " 2";
      N >>= 1;
    }
    for (uint64_t P = 3; P <= N / P; P += 2) {
      while (N % P == 0) {
        llvm::outs() << ' ' << P;
        N /= P;
      }
    }
    if (N > 1)
      llvm::outs() << ' ' << N;
    llvm::outs() << '\n';
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteLogname(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-"))
      return false;
  }
  if (Argv.size() > 1) {
    llvm::errs() << "neverc make: logname: unexpected operand\n";
    ExitCode = 1;
    return true;
  }
#ifndef _WIN32
  // Match POSIX logname(1): only getlogin(). If that fails (common in CI /
  // non-login sessions), leave the command to the host tool instead of
  // inventing a LOGNAME/USER fallback that can disagree with /usr/bin/logname.
  if (const char *Name = ::getlogin()) {
    llvm::outs() << Name << '\n';
    llvm::outs().flush();
    ExitCode = 0;
    return true;
  }
  return false;
#else
  (void)ExitCode;
  return false;
#endif
}


bool tryExecuteYes(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `yes` / `yes STRING...` with a hard line cap so a bare
  // recipe `yes` cannot hang the build forever. Piped forms (`yes | head`)
  // still fall back to the host shell because `|` is rejected by tokenize.
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-") && Argv[I].Text != "-")
      return false;
  }
  std::string Line = "y";
  if (Argv.size() > 1) {
    Line.clear();
    for (size_t I = 1; I < Argv.size(); ++I) {
      if (I > 1)
        Line.push_back(' ');
      Line += Argv[I].Text;
    }
  }
  // Piped uses fall back at tokenize time; this cap only protects bare `yes`
  // recipe footguns from hanging the build.
  constexpr size_t MaxLines = 256;
  for (size_t I = 0; I < MaxLines; ++I)
    llvm::outs() << Line << '\n';
  llvm::outs().flush();
  ExitCode = 0;
  return true;
}


bool tryExecuteGroups(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: bare `groups` for the current user (no USER operand).
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-"))
      return false;
  }
  if (Argv.size() > 1)
    return false;

#ifndef _WIN32
  // Use getgroups(2) only — getgrouplist(3) has incompatible signatures across
  // Linux (gid_t*) and macOS (int*), which is a footgun in portable code.
  const gid_t Egid = ::getegid();
  int Count = ::getgroups(0, nullptr);
  if (Count < 0) {
    llvm::errs() << "neverc make: groups: cannot determine groups\n";
    ExitCode = 1;
    return true;
  }
  std::vector<gid_t> Gids(static_cast<size_t>(Count));
  if (Count > 0 && ::getgroups(Count, Gids.data()) < 0) {
    llvm::errs() << "neverc make: groups: cannot determine groups\n";
    ExitCode = 1;
    return true;
  }
  // Ensure the effective gid is listed (typically first) when omitted.
  if (std::find(Gids.begin(), Gids.end(), Egid) == Gids.end())
    Gids.insert(Gids.begin(), Egid);

  for (size_t I = 0; I < Gids.size(); ++I) {
    if (I)
      llvm::outs() << ' ';
    if (const group *Gr = ::getgrgid(Gids[I]))
      llvm::outs() << Gr->gr_name;
    else
      llvm::outs() << Gids[I];
  }
  llvm::outs() << '\n';
  llvm::outs().flush();
  ExitCode = 0;
  return true;
#else
  (void)ExitCode;
  return false;
#endif
}


bool tryExecuteTty(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: bare `tty` (no `-s`).
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-"))
      return false;
  }
  if (Argv.size() > 1) {
    llvm::errs() << "neverc make: tty: unexpected operand\n";
    ExitCode = 1;
    return true;
  }

#ifndef _WIN32
  if (!::isatty(STDIN_FILENO)) {
    llvm::errs() << "not a tty\n";
    ExitCode = 1;
    return true;
  }
  if (const char *Name = ::ttyname(STDIN_FILENO)) {
    llvm::outs() << Name << '\n';
    llvm::outs().flush();
    ExitCode = 0;
    return true;
  }
  llvm::errs() << "neverc make: tty: cannot determine terminal name\n";
  ExitCode = 1;
  return true;
#else
  if (!_isatty(_fileno(stdin))) {
    llvm::errs() << "not a tty\n";
    ExitCode = 1;
    return true;
  }
  // Windows has no portable ttyname equivalent for recipe use.
  return false;
#endif
}


} // namespace internal
} // namespace builtins
} // namespace build
} // namespace neverc
