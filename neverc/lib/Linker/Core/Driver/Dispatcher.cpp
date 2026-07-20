#include "Linker/Core/Driver/Dispatcher.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/Error.h"

using namespace llvm;

namespace linker {

int dispatchLink(ArrayRef<DriverDef> Drivers, Flavor RequestedFlavor,
                 ArrayRef<const char *> Args, raw_ostream &Stdout,
                 raw_ostream &Stderr, const LinkerDriverConfig &Config) {
  bool Success = false;
  auto Finish = make_scope_exit([&] {
    if (Config.executionHooks)
      Config.executionHooks->complete(Success);
  });

  if (Config.executionHooks) {
    if (!Config.executionRequest) {
      Stderr << "neverc: error: linker hooks have no typed request\n";
      return 1;
    }
    auto Result = Config.executionHooks->execute(
        *Config.executionRequest, Config, Stdout, Stderr);
    if (!Result) {
      Stderr << "neverc: error: linker hook failed: "
             << toString(Result.takeError()) << "\n";
      return 1;
    }
    if (Result->Disposition == LinkHookDisposition::Completed) {
      Success = Result->ExitCode == 0;
      return Result->ExitCode;
    }
    if (Result->Disposition == LinkHookDisposition::Failed)
      return Result->ExitCode == 0 ? 1 : Result->ExitCode;
  }

  auto It = llvm::find_if(Drivers, [&](const DriverDef &Driver) {
    return Driver.f == RequestedFlavor;
  });
  if (It == Drivers.end()) {
    Stderr << "neverc: error: linker backend for this target "
              "was not enabled at build time\n";
    return 1;
  }
  Success = It->d(Args, Stdout, Stderr,
                  /*exitEarly=*/false, /*disableOutput=*/false, Config);
  return Success ? 0 : 1;
}

} // namespace linker
