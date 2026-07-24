//===- ConformanceEnvironment.cpp - plugin conformance harness ----------===//

#include "ConformanceEnvironment.h"

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#else
#include <process.h>
#endif

// Injected by CMake; empty defaults keep the file self-contained.
#ifndef NEVERC_CONF_NEVERC
#define NEVERC_CONF_NEVERC ""
#endif
#ifndef NEVERC_CONF_CC
#define NEVERC_CONF_CC ""
#endif
#ifndef NEVERC_CONF_SDK_INCLUDE
#define NEVERC_CONF_SDK_INCLUDE ""
#endif
#ifndef NEVERC_CONF_FIXTURES_DIR
#define NEVERC_CONF_FIXTURES_DIR ""
#endif

namespace fs = std::filesystem;

namespace neverc::conformance {

namespace {

std::string envOrDefault(const char *Name, const std::string &Fallback) {
  if (const char *Value = std::getenv(Name))
    if (Value[0] != '\0')
      return std::string(Value);
  return Fallback;
}

std::string readFile(const std::string &Path) {
  std::ifstream Stream(Path, std::ios::binary);
  std::ostringstream Buffer;
  Buffer << Stream.rdbuf();
  return Buffer.str();
}

std::string shellQuote(const std::string &Argument) {
#ifdef _WIN32
  std::string Quoted = "\"";
  for (char C : Argument) {
    if (C == '"')
      Quoted += "\\\"";
    else
      Quoted += C;
  }
  Quoted += "\"";
  return Quoted;
#else
  std::string Quoted = "'";
  for (char C : Argument) {
    if (C == '\'')
      Quoted += "'\\''";
    else
      Quoted += C;
  }
  Quoted += "'";
  return Quoted;
#endif
}

// cl and clang-cl take MSVC-style switches; every other driver we support
// (gcc, clang, Apple clang, mingw) takes GNU-style ones. The compiler can be
// overridden at run time, so this has to key off the name rather than the host.
bool usesMSVCDriver(const std::string &Compiler) {
  std::string Stem = fs::path(Compiler).stem().string();
  for (char &C : Stem)
    C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
  return Stem == "cl" || Stem == "clang-cl";
}

int normalizeExit(int Raw) {
#ifndef _WIN32
  if (WIFEXITED(Raw))
    return WEXITSTATUS(Raw);
  if (WIFSIGNALED(Raw))
    return 128 + WTERMSIG(Raw);
  return Raw;
#else
  return Raw;
#endif
}

} // namespace

bool RunResult::outContains(const std::string &Needle) const {
  return out.find(Needle) != std::string::npos;
}
bool RunResult::errContains(const std::string &Needle) const {
  return err.find(Needle) != std::string::npos;
}

Environment::Environment() {
  Neverc = envOrDefault("NEVERC_UNDER_TEST", NEVERC_CONF_NEVERC);
  CC = envOrDefault("NEVERC_CONFORMANCE_CC", NEVERC_CONF_CC);
  FixturesDir = envOrDefault("NEVERC_CONFORMANCE_FIXTURES", NEVERC_CONF_FIXTURES_DIR);

  // A released package can be pointed at with NEVERC_PLUGIN_SDK_ROOT; its public
  // headers live under <root>/include.
  if (const char *Root = std::getenv("NEVERC_PLUGIN_SDK_ROOT")) {
    if (Root[0] != '\0')
      SDKInclude = (fs::path(Root) / "include").string();
  }
  if (SDKInclude.empty())
    SDKInclude = NEVERC_CONF_SDK_INCLUDE;
}

const Environment &Environment::get() {
  static const Environment Instance;
  return Instance;
}

std::string Environment::whyUnusable() const {
  std::string Why;
  if (Neverc.empty())
    Why += "no compiler-under-test (set NEVERC_UNDER_TEST); ";
  if (CC.empty())
    Why += "no system C compiler; ";
  if (SDKInclude.empty())
    Why += "no SDK include dir (set NEVERC_PLUGIN_SDK_ROOT); ";
  if (FixturesDir.empty())
    Why += "no fixtures dir; ";
  return Why;
}

std::string Environment::pluginExtension() {
#ifdef _WIN32
  return ".dll";
#elif defined(__APPLE__)
  return ".dylib";
#else
  return ".so";
#endif
}

std::string Environment::makeTempDir(const std::string &Label) const {
  static std::atomic<uint64_t> Counter{0};
  const uint64_t Unique = Counter.fetch_add(1, std::memory_order_relaxed);
#ifndef _WIN32
  const auto Pid = static_cast<long>(::getpid());
#else
  const auto Pid = static_cast<long>(_getpid());
#endif
  fs::path Dir = fs::temp_directory_path() /
                 ("neverc-conf-" + Label + "-" + std::to_string(Pid) + "-" +
                  std::to_string(Unique));
  std::error_code EC;
  fs::remove_all(Dir, EC);
  fs::create_directories(Dir, EC);
  return Dir.string();
}

RunResult Environment::runProgram(const std::vector<std::string> &Args,
                                  const EnvVars &Extra) const {
  RunResult Result;
  if (Args.empty())
    return Result;

  fs::path Scratch =
      fs::temp_directory_path() /
      ("neverc-conf-run-" +
       std::to_string(reinterpret_cast<uintptr_t>(&Args)) + "-" +
       std::to_string(std::rand()));
  std::error_code EC;
  fs::create_directories(Scratch, EC);
  const std::string OutFile = (Scratch / "stdout.txt").string();
  const std::string ErrFile = (Scratch / "stderr.txt").string();

  std::string Command;
#ifdef _WIN32
  for (const auto &Pair : Extra)
    Command += "set " + shellQuote(Pair.first + "=" + Pair.second) + " && ";
#else
  for (const auto &Pair : Extra)
    Command += Pair.first + "=" + shellQuote(Pair.second) + " ";
#endif
  for (const std::string &Arg : Args)
    Command += shellQuote(Arg) + " ";
  Command += "> " + shellQuote(OutFile) + " 2> " + shellQuote(ErrFile);

#ifdef _WIN32
  // cmd /c strips the first and last quote of any command line it cannot read
  // as a single quoted executable name, and the redirections above always
  // disqualify this one. Add a spare pair for it to strip.
  Command = "\"" + Command + "\"";
#endif

  Result.exitCode = normalizeExit(std::system(Command.c_str()));
  Result.out = readFile(OutFile);
  Result.err = readFile(ErrFile);
  fs::remove_all(Scratch, EC);
  return Result;
}

RunResult Environment::runNeverc(const std::vector<std::string> &Args,
                                 const EnvVars &Extra) const {
  std::vector<std::string> Full;
  Full.reserve(Args.size() + 1);
  Full.push_back(Neverc);
  Full.insert(Full.end(), Args.begin(), Args.end());
  return runProgram(Full, Extra);
}

std::string Environment::buildPlugin(const std::string &Dir,
                                     const std::string &Fixture,
                                     const std::vector<std::string> &Defines,
                                     std::string &Error) const {
  const std::string Output =
      (fs::path(Dir) / (Fixture + pluginExtension())).string();
  const std::string Source =
      (fs::path(FixturesDir) / (Fixture + ".c")).string();

  std::vector<std::string> Args;
  if (usesMSVCDriver(CC)) {
    // /Fo names the object explicitly rather than a directory: a directory
    // argument would end in a backslash, and the trailing backslash would
    // escape the closing quote runProgram adds.
    Args = {CC, "/nologo", "/LD", "/std:c11"};
    Args.push_back("/I");
    Args.push_back(SDKInclude);
    Args.push_back("/I");
    Args.push_back(FixturesDir);
    for (const std::string &Define : Defines)
      Args.push_back("/D" + Define);
    Args.push_back("/Fo" + (fs::path(Dir) / (Fixture + ".obj")).string());
    Args.push_back("/Fe:" + Output);
    Args.push_back(Source);
  } else {
    Args = {CC, "-shared"};
#ifndef _WIN32
    Args.push_back("-fPIC");
#endif
    Args.push_back("-std=c11");
    Args.push_back("-I");
    Args.push_back(SDKInclude);
    Args.push_back("-I");
    Args.push_back(FixturesDir);
    for (const std::string &Define : Defines)
      Args.push_back("-D" + Define);
    Args.push_back("-o");
    Args.push_back(Output);
    Args.push_back(Source);
  }

  RunResult Result = runProgram(Args);
  if (Result.exitCode != 0) {
    Error = Result.err.empty() ? Result.out : Result.err;
    return std::string();
  }
  return Output;
}

} // namespace neverc::conformance
