#include "neverc/Translate/TranslateDriver.h"
#include "FrontendEntry.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#ifndef NEVERC_AUTHENTICATED_C23_BACKEND
#error The diagnostic must bind its authenticated existing backend at compile time
#endif
#ifndef NEVERC_PRIVATE_RUNTIME_TRACE_ROOT
#error The diagnostic must retain its actual child invocations
#endif

namespace {
int frontend(int Argc, char **Argv) {
  namespace fs = std::filesystem;
  fs::path Record;
  if (Argc == 6 && std::strcmp(Argv[2], "--request") == 0 &&
      std::strcmp(Argv[4], "--output") == 0) {
    Record = fs::path(NEVERC_PRIVATE_RUNTIME_TRACE_ROOT) / "frontend" /
             std::to_string(::getpid());
    fs::create_directories(Record);
    fs::copy_file(Argv[3], Record / "request.json");
    std::ofstream Command(Record / "argv.txt");
    for (int I = 0; I != Argc; ++I)
      Command << Argv[I] << '\n';
    Command.close();
    std::ofstream Environment(Record / "environment-before.tsv");
    for (const char *Name : {"CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH",
                             "SDKROOT", "CCC_OVERRIDE_OPTIONS"}) {
      const char *Value = std::getenv(Name);
      Environment << Name << '\t' << (Value ? Value : "") << '\n';
    }
    Environment.close();
    if (Command.fail() || Environment.fail())
      return 66;
  }
  std::vector<const char *> Arguments(Argv + 1, Argv + Argc);
  ::alarm(60);
  const int Status = neverc_cpp_frontend_main(Argc - 1, Arguments.data());
  if (!Record.empty()) {
    fs::copy_file(Argv[5], Record / "response.json");
    std::ofstream Result(Record / "returncode.txt");
    Result << Status << '\n';
    Result.close();
    if (Result.fail())
      return 67;
  }
  return Status;
}
} // namespace

// This diagnostic links the real current Translate library and the audited
// current private C entry. It implements no frontend or translation operation.
// Existing backend-only invocations execute the authenticated compiler artifact;
// this is focused component evidence, not a new complete compiler build.
int main(int Argc, char **Argv) {
  if (Argc >= 2 && std::strcmp(Argv[1], "__neverc_cpp_frontend") == 0)
    return frontend(Argc, Argv);
  if (Argc >= 2 && std::strcmp(Argv[1], "translate") == 0) {
    std::vector<const char *> Arguments(Argv + 1, Argv + Argc);
    return neverc::translate::runTranslate(Argc - 1, Arguments.data(), Argv[0]);
  }
  namespace fs = std::filesystem;
  const auto Record = fs::path(NEVERC_PRIVATE_RUNTIME_TRACE_ROOT) / "backend" /
                      std::to_string(::getpid());
  fs::create_directories(Record);
  std::ofstream Command(Record / "argv.txt");
  Command << NEVERC_AUTHENTICATED_C23_BACKEND << '\n';
  for (int I = 1; I != Argc; ++I)
    Command << Argv[I] << '\n';
  Command.close();
  if (Command.fail())
    return 68;
  std::vector<char *> Arguments;
  Arguments.push_back(const_cast<char *>(NEVERC_AUTHENTICATED_C23_BACKEND));
  Arguments.insert(Arguments.end(), Argv + 1, Argv + Argc);
  Arguments.push_back(nullptr);
  ::alarm(60);
  ::execv(Arguments[0], Arguments.data());
  return errno == ENOENT ? 127 : 126;
}
