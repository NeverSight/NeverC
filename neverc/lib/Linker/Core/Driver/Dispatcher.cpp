#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Runtime/CrashRecovery.h"
#include "Linker/Core/Runtime/LinkerExecutionContext.h"
#include "neverc/Foundation/Core/ProcessResourceBroker.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TimeProfiler.h"

#include <memory>
#include <optional>

using namespace llvm;

namespace linker {

namespace {

StringRef phaseTraceFormat(Flavor Flavor) {
  switch (Flavor) {
  case Flavor::Gnu:
    return "ELF";
  case Flavor::WinLink:
    return "COFF";
  case Flavor::Darwin:
    return "Mach-O";
  case Flavor::Invalid:
    return "unknown";
  }
  llvm_unreachable("unknown linker flavor");
}

} // namespace

int dispatchLink(ArrayRef<DriverDef> Drivers, Flavor RequestedFlavor,
                 ArrayRef<const char *> Args, raw_ostream &Stdout,
                 raw_ostream &Stderr, const LinkerDriverConfig &Config) {
  crash_recovery_detail::CrashRecoveryTimeTraceOwner TraceProfiler(
      Config.timeTraceEnabled, Config.timeTraceGranularity,
      Args.empty() ? "neverc" : Args.front());
  CrashRecoveryContextCleanupRegistrar<
      LinkerExecutionContext,
      crash_recovery_detail::CrashRecoveryDestroyBackendCleanup<
          LinkerExecutionContext>>
      CrashBackend(Config.executionContext);

  const StringRef Format = timeTraceProfilerEnabled()
                               ? phaseTraceFormat(RequestedFlavor)
                               : StringRef();
  auto ResourceSession = neverc::ProcessResourceBroker::global().acquireSession(
      neverc::ResourcePhase::LinkParseResolve);
  std::unique_ptr<neverc::ResourceSessionPermit> CrashOwnedResourceSession;
  std::optional<
      CrashRecoveryContextCleanupRegistrar<neverc::ResourceSessionPermit>>
      CrashResourceSession;
  if (CrashRecoveryContext::GetCurrent()) {
    CrashOwnedResourceSession = std::make_unique<neverc::ResourceSessionPermit>(
        std::move(ResourceSession));
    CrashResourceSession.emplace(CrashOwnedResourceSession.get());
  }
  bool Success = false;
  auto Finish = make_scope_exit([&] {
    if (Config.executionHooks)
      Config.executionHooks->complete(Success);
  });

  int Result = 1;
  {
    TimeTraceScope DispatchScope("neverc.link.dispatch", Format);
    Result = [&]() -> int {
      if (Config.executionHooks) {
        TimeTraceScope HooksScope("neverc.link.hooks", Format);
        if (!Config.executionRequest) {
          Stderr << "neverc: error: linker hooks have no typed request\n";
          return 1;
        }
        auto HookResult = Config.executionHooks->execute(
            *Config.executionRequest, Config, Stdout, Stderr);
        if (!HookResult) {
          Stderr << "neverc: error: linker hook failed: "
                 << toString(HookResult.takeError()) << "\n";
          return 1;
        }
        if (HookResult->Disposition == LinkHookDisposition::Completed)
          return HookResult->ExitCode;
        if (HookResult->Disposition == LinkHookDisposition::Failed)
          return HookResult->ExitCode == 0 ? 1 : HookResult->ExitCode;
      }

      auto It = llvm::find_if(Drivers, [&](const DriverDef &Driver) {
        return Driver.f == RequestedFlavor;
      });
      if (It == Drivers.end()) {
        Stderr << "neverc: error: linker backend for this target "
                  "was not enabled at build time\n";
        return 1;
      }

      TimeTraceScope BackendScope("neverc.link.backend", Format);
      return It->d(Args, Stdout, Stderr,
                   /*exitEarly=*/false, /*disableOutput=*/false, Config)
                 ? 0
                 : 1;
    }();
  }

  if (Error E = TraceProfiler.write(Config.outputFile)) {
    Stderr << "neverc: error: could not write linker time trace: "
           << toString(std::move(E)) << "\n";
    Result = 1;
  }

  Success = Result == 0;
  return Result;
}

} // namespace linker
