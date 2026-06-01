#include "neverc/Build/Platform.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <glob.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace neverc {
namespace build {
namespace platform {

ProcessResult shellExecute(const std::string &Command,
                           const std::string &Shell) {
  ProcessResult R;

  // GNU make passes the command directly to popen() which invokes
  // /bin/sh -c <command>.  We match that behavior.
  (void)Shell;
  FILE *Pipe = popen(Command.c_str(), "r");
  if (!Pipe) {
    R.ExitCode = 127;
    return R;
  }

  char Buf[4096];
  while (fgets(Buf, sizeof(Buf), Pipe))
    R.Output += Buf;

  int Status = pclose(Pipe);
#ifdef _WIN32
  R.ExitCode = Status;
#else
  R.ExitCode = WIFEXITED(Status) ? WEXITSTATUS(Status) : 1;
#endif

  while (!R.Output.empty() && R.Output.back() == '\n')
    R.Output.pop_back();

  return R;
}

int shellExecuteNoCapture(const std::string &Command,
                          const std::string &Shell, bool Silent) {
  (void)Shell;
  std::string Cmd = Command;
  if (Silent) {
#ifdef _WIN32
    Cmd += " >nul 2>&1";
#else
    Cmd += " >/dev/null 2>&1";
#endif
  }
  int Status = std::system(Cmd.c_str());
#ifdef _WIN32
  return Status;
#else
  return WIFEXITED(Status) ? WEXITSTATUS(Status) : 1;
#endif
}

int64_t getFileTimestamp(const std::string &Path) {
  llvm::sys::fs::file_status Status;
  if (llvm::sys::fs::status(Path, Status))
    return -1;
  auto T = Status.getLastModificationTime();
  return T.time_since_epoch().count();
}

bool fileExists(const std::string &Path) {
  return llvm::sys::fs::exists(Path);
}

std::vector<std::string> globFiles(const std::string &Pattern) {
  std::vector<std::string> Results;
#ifdef _WIN32
  WIN32_FIND_DATAA FindData;
  HANDLE H = FindFirstFileA(Pattern.c_str(), &FindData);
  if (H == INVALID_HANDLE_VALUE)
    return Results;

  llvm::SmallString<256> Dir(Pattern);
  llvm::sys::path::remove_filename(Dir);
  do {
    if (FindData.cFileName[0] == '.')
      continue;
    llvm::SmallString<256> Full(Dir);
    llvm::sys::path::append(Full, FindData.cFileName);
    Results.push_back(std::string(Full));
  } while (FindNextFileA(H, &FindData));
  FindClose(H);
#else
  glob_t G;
  if (glob(Pattern.c_str(), GLOB_NOSORT | GLOB_MARK, nullptr, &G) == 0) {
    for (size_t I = 0; I < G.gl_pathc; ++I) {
      std::string P = G.gl_pathv[I];
      if (!P.empty() && P.back() != '/')
        Results.push_back(P);
    }
    globfree(&G);
  }
#endif
  return Results;
}

std::string getDefaultShell() {
#ifdef _WIN32
  return "cmd.exe";
#else
  return "/bin/sh";
#endif
}

std::string normalizePath(const std::string &Path) {
  llvm::SmallString<256> Result(Path);
  llvm::sys::path::native(Result);
  return std::string(Result);
}

std::string realPath(const std::string &Path) {
  llvm::SmallString<256> Result;
  if (llvm::sys::fs::real_path(Path, Result))
    return Path;
  return std::string(Result);
}

std::string getCwd() {
  llvm::SmallString<256> Cwd;
  if (llvm::sys::fs::current_path(Cwd))
    return ".";
  return std::string(Cwd);
}

bool changeCwd(const std::string &Dir) {
#ifdef _WIN32
  return _chdir(Dir.c_str()) == 0;
#else
  return chdir(Dir.c_str()) == 0;
#endif
}

unsigned getProcessorCount() {
  unsigned N = std::thread::hardware_concurrency();
  return N > 0 ? N : 1;
}

} // namespace platform
} // namespace build
} // namespace neverc
