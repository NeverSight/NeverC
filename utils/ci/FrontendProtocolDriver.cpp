#include "FrontendEntry.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

// A diagnostic caller of the real private C entry. It implements no frontend
// operation. Each invocation keeps its original request, response and status.
int main(int Argc, char **Argv) {
  if (Argc < 2 || std::strcmp(Argv[1], "__neverc_cpp_frontend") != 0)
    return 64;
  std::vector<const char *> Arguments(Argv + 1, Argv + Argc);
  namespace fs = std::filesystem;
  fs::path Record;
  if (Argc == 6 && std::strcmp(Argv[2], "--request") == 0 &&
      std::strcmp(Argv[4], "--output") == 0) {
    const char *Directory = std::getenv("NEVERC_CPP_PROTOCOL_EXECUTION_DIR");
    if (!Directory)
      return 65;
    Record = fs::path(Directory) / std::to_string(::getpid());
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
  // The original Python suites allow a larger outer wait. Every actual
  // frontend process still has the existing sixty-second execution limit.
  ::alarm(60);
  const int Result = neverc_cpp_frontend_main(Argc - 1, Arguments.data());
  if (!Record.empty()) {
    fs::copy_file(Argv[5], Record / "response.json");
    std::ofstream Status(Record / "returncode.txt");
    Status << Result << '\n';
    Status.close();
    if (Status.fail())
      return 67;
  }
  return Result;
}
