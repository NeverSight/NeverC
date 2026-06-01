#ifndef NEVERC_BUILD_PLATFORM_H
#define NEVERC_BUILD_PLATFORM_H

#include <cstdint>
#include <string>
#include <vector>

namespace neverc {
namespace build {
namespace platform {

struct ProcessResult {
  int ExitCode = -1;
  std::string Output;
};

ProcessResult shellExecute(const std::string &Command,
                           const std::string &Shell = "");
int shellExecuteNoCapture(const std::string &Command,
                          const std::string &Shell = "",
                          bool Silent = false);

int64_t getFileTimestamp(const std::string &Path);
bool fileExists(const std::string &Path);
std::vector<std::string> globFiles(const std::string &Pattern);
std::string getDefaultShell();
std::string normalizePath(const std::string &Path);
std::string realPath(const std::string &Path);
std::string absolutePath(const std::string &Path);
std::string getCwd();
bool changeCwd(const std::string &Dir);
unsigned getProcessorCount();

} // namespace platform
} // namespace build
} // namespace neverc

#endif // NEVERC_BUILD_PLATFORM_H
