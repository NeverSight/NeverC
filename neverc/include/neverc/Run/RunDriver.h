#ifndef NEVERC_RUN_RUNDRIVER_H
#define NEVERC_RUN_RUNDRIVER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <string>
#include <vector>

namespace neverc {
namespace run {

struct RunInvocation {
  std::vector<std::string> CompilerArguments;
  std::vector<std::string> ProgramArguments;
};

/// Split run arguments into one compiler invocation and one program
/// invocation. An explicit "--" always wins; otherwise consecutive .c/.nc
/// files use the same source-list boundary convention as `go run`.
llvm::Expected<RunInvocation>
parseRunArguments(llvm::ArrayRef<llvm::StringRef> Arguments);

int runCommand(int Argc, const char **Argv, const char *ExecutablePath,
               const char *PrependArg = nullptr);

} // namespace run
} // namespace neverc

#endif // NEVERC_RUN_RUNDRIVER_H
